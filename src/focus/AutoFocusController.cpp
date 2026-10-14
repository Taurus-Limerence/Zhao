#include "AutoFocusController.h"
#include "devices/DeviceManager.h"
#include "devices/CameraBackend.h"
#include <QScopedValueRollback>
#include <QSettings>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>

namespace {
constexpr int Camera=DeviceManager::MainCamera;
constexpr int Focuser=DeviceManager::Focuser;
bool validSetting(const QString &key,const QVariant &value) {
    bool ok=false;
    const double number=value.toDouble(&ok);
    if (!ok || !std::isfinite(number)) return false;
    if (key=="exposure") return number>=0.001 && number<=3600;
    if (number!=std::floor(number)) return false;
    if (key=="step") return number>=1 && number<=100000;
    if (key=="points") return number>=7 && number<=15 && int(number)%2;
    if (key=="frames") return number>=1 && number<=3;
    if (key=="backlash") return number>=0 && number<=100000;
    return false;
}
double median(QVector<double> values) {
    std::sort(values.begin(),values.end());
    const int middle=values.size()/2;
    return values.size()%2 ? values[middle] : (values[middle-1]+values[middle])/2;
}
}

AutoFocusController::AutoFocusController(DeviceManager *devices,QObject *parent)
    : QObject(parent),m_devices(devices) {
    QSettings stored;
    for (auto it=m_settings.begin();it!=m_settings.end();++it) {
        const auto value=stored.value("autofocus/"+it.key(),it.value());
        if (validSetting(it.key(),value)) it.value()=value;
    }
    m_timer.setInterval(100);
    connect(&m_timer,&QTimer::timeout,this,&AutoFocusController::tick);
    connect(devices,&DeviceManager::focuserStopRequested,this,&AutoFocusController::cancel);
    connect(devices,&DeviceManager::focuserCommandFailed,this,[this](const QString &reason) {
        if (!busy()) return;
        if (m_phase==Stopping) {
            m_status=QStringLiteral("电调焦停止失败，请重试停止：%1").arg(reason); emit changed();
        } else stop(QStringLiteral("调焦移动失败：%1").arg(reason),m_movePurpose!=ReturnOriginal);
    });
    connect(devices,&DeviceManager::autofocusCaptureFailed,this,[this](const QString &reason) {
        if (busy() && m_phase!=Stopping) stop(QStringLiteral("自动对焦拍摄失败：%1").arg(reason),m_movePurpose!=ReturnOriginal);
    });
    connect(devices,&DeviceManager::autofocusPhotoReady,this,[this](QVector<quint16> raw,int width,int height,QVariantMap metadata) {
        if (m_phase!=Capturing) return;
        m_phase=Analyzing; m_elapsed.restart();
        m_status=QStringLiteral("正在测量星点 HFR…"); emit changed();
        m_analysis.setFuture(QtConcurrent::run([raw=std::move(raw),width,height,metadata=std::move(metadata)] {
            return FocusMetrics::measure(raw,width,height,metadata);
        }));
    });
    connect(&m_analysis,&QFutureWatcher<FocusMetrics::Measurement>::finished,this,[this] {
        if (m_phase==Analyzing) m_phase=Measured;
    });
}

AutoFocusController::~AutoFocusController() {
    if (busy()) cancel();
    m_analysis.waitForFinished();
    // An active owner must not unlock pending device commands during teardown.
    if (busy() && cameraSettled() && focuserStopped()) m_devices->m_autofocusOwned=false;
}

QVariantList AutoFocusController::samples() const {
    QVariantList result;
    for (const auto &sample : m_samples) result.append(QVariantMap{{"position",sample.position},{"hfr",sample.hfr},{"stars",sample.stars}});
    return result;
}

void AutoFocusController::setSetting(const QString &key,const QVariant &value) {
    if (busy() || m_devices->m_sequenceOwned) return;
    if (!validSetting(key,value)) { emit notice(QStringLiteral("自动对焦参数超出有效范围")); return; }
    const QVariant normalized=key=="exposure" ? QVariant(value.toDouble()) : QVariant(value.toInt());
    if (m_settings.value(key)==normalized) return;
    m_settings.insert(key,normalized);
    QSettings().setValue("autofocus/"+key,normalized);
    emit settingsChanged();
}

QString AutoFocusController::validationError(bool forSequence) const {
    const auto &camera=m_devices->m_slots[Camera];
    const auto &focuser=m_devices->m_slots[Focuser];
    const auto &wheel=m_devices->m_slots[DeviceManager::FilterWheel];
    const auto &mount=m_devices->m_slots[DeviceManager::Mount];
    if (!camera.connected || !focuser.connected || camera.busy || focuser.busy || wheel.busy || mount.busy
        || busy() || m_devices->m_autofocusOwned || (m_devices->m_sequenceOwned && !forSequence) || m_devices->m_centeringOwned
        || m_devices->m_solveCapturePending || !cameraSettled() || !focuserStopped()
        || m_devices->m_mountGotoActive || m_devices->m_manualMoving || m_devices->m_mountGuiding
        || camera.metrics.value("previewing").toBool() || mount.metrics.value("slewing").toBool()
        || wheel.metrics.value("moving").toBool() || wheel.metrics.value("calibrating").toBool()
        || std::any_of(m_devices->m_commandsPending.begin(),m_devices->m_commandsPending.end(),[](int pending) { return pending>0; })
        || !camera.metrics.value("exposureUs").toUInt()) {
        return QStringLiteral("请连接主相机和电调焦，并等待拍摄、序列、导星及设备运动结束");
    }
    bool positionOk=false,maxOk=false;
    const int position=focuser.metrics.value("position").toInt(&positionOk);
    const int maximum=focuser.metrics.value("maxStep").toInt(&maxOk);
    const qint64 radius=qint64(m_settings.value("points").toInt()/2)*m_settings.value("step").toInt();
    if (!positionOk || !maxOk || maximum<=0 || position<0 || position>maximum
        || position-radius-m_settings.value("backlash").toInt()<0 || position+radius>maximum) {
        return QStringLiteral("扫描范围或回差补偿超出实际行程，请调整步长、点数或当前位置");
    }
    return {};
}

bool AutoFocusController::startForSequence() {
    if (busy()) return false;
    m_sequenceRun=true;
    if (start()) return true;
    m_sequenceRun=false;
    return false;
}

bool AutoFocusController::start() {
    if (busy()) return false;
    const QString error=validationError(m_sequenceRun);
    if (!error.isEmpty()) {
        m_status=error; m_succeeded=false; emit changed(); return false;
    }
    const auto &camera=m_devices->m_slots[Camera];
    const int position=m_devices->m_slots[Focuser].metrics.value("position").toInt();
    const qint64 radius=qint64(m_settings.value("points").toInt()/2)*m_settings.value("step").toInt();
    m_original=position; m_originalExposure=camera.metrics.value("exposureUs").toUInt();
    m_first=int(position-radius); m_bestPosition=-1; m_starCount=0;
    m_bestHfr=m_verifiedHfr=m_rSquared=qQNaN();
    m_samples.clear(); m_frames.clear(); m_fitCurve.clear(); m_stopReason.clear();
    m_succeeded=false; m_returnToOriginal=true;
    m_devices->invalidateFilterFocusBaseline();
    m_devices->m_autofocusOwned=true; emit m_devices->filterFocusSettingsChanged(); m_timer.start();
    const int backlash=m_settings.value("backlash").toInt();
    move(m_first-backlash,backlash ? ScanApproach : ScanPoint);
    return busy();
}

bool AutoFocusController::cameraSettled() const {
    const auto &camera=m_devices->m_slots[Camera];
    return !m_devices->m_autofocusCapturePending && !m_devices->m_stillPending[Camera]
        && !m_devices->m_unsettledStills[Camera] && !m_devices->m_commandsPending[Camera]
        && !camera.busy && (!busy() || !camera.connected || camera.metrics.value("exposureUs").toUInt()==m_originalExposure);
}

bool AutoFocusController::focuserStopped() const {
    const auto &slot=m_devices->m_slots[Focuser];
    return !slot.busy && !m_devices->m_commandsPending[Focuser]
        && (!slot.connected || (slot.metrics.value("moving").isValid() && !slot.metrics.value("moving").toBool()));
}

void AutoFocusController::move(int position,MovePurpose purpose) {
    m_target=position; m_movePurpose=purpose; m_phase=Moving; m_elapsed.restart();
    m_status=purpose==ReturnOriginal ? QStringLiteral("正在返回原位置 %1…").arg(position)
        : QStringLiteral("正在移动到 %1 · 采样 %2/%3").arg(position).arg(m_samples.size()).arg(m_settings.value("points").toInt());
    QScopedValueRollback<bool> dispatch(m_devices->m_autofocusDispatch,true);
    QScopedValueRollback<bool> sequenceDispatch(m_devices->m_sequenceDispatch,m_sequenceRun);
    if (!m_devices->moveFocuser(position)) stop(m_devices->lastError(),purpose!=ReturnOriginal);
    emit changed();
}

void AutoFocusController::capture() {
    m_phase=Capturing; m_elapsed.restart();
    m_status=m_movePurpose==BestPoint ? QStringLiteral("正在拍摄最佳位置验证帧 %1/%2…").arg(m_frames.size()+1).arg(m_settings.value("frames").toInt())
        : QStringLiteral("正在拍摄采样 %1/%2 · 帧 %3/%4…").arg(m_samples.size()+1).arg(m_settings.value("points").toInt()).arg(m_frames.size()+1).arg(m_settings.value("frames").toInt());
    QScopedValueRollback<bool> dispatch(m_devices->m_autofocusDispatch,true);
    QScopedValueRollback<bool> sequenceDispatch(m_devices->m_sequenceDispatch,m_sequenceRun);
    if (!m_devices->captureForAutofocus(m_settings.value("exposure").toDouble())) stop(m_devices->lastError(),true);
    emit changed();
}

void AutoFocusController::cancel() {
    if (busy()) stop(QStringLiteral("自动对焦已停止，保留当前位置（未验证成功）"),false);
}

void AutoFocusController::stop(const QString &reason,bool returnToOriginal) {
    if (!busy()) return;
    const bool retry=m_phase==Stopping;
    if (m_phase!=Stopping) { m_stopReason=reason; m_returnToOriginal=returnToOriginal; }
    else if (!returnToOriginal) { m_stopReason=reason; m_returnToOriginal=false; }
    m_phase=Stopping; m_elapsed.restart(); m_status=QStringLiteral("正在停止并等待设备确认…");
    m_devices->cancelAutofocusCapture();
    const auto &camera=m_devices->m_slots[Camera];
    if (retry && camera.connected && !camera.busy && !m_devices->m_autofocusCapturePending
        && !m_devices->m_commandsPending[Camera] && camera.metrics.value("exposureUs").toUInt()!=m_originalExposure) {
        m_devices->postDeviceCommand(m_devices->m_camera,Camera,[devices=m_devices,exposure=m_originalExposure] {
            devices->m_camera->setExposure(exposure);
        });
    }
    if (m_devices->m_slots[Focuser].connected && !focuserStopped()) {
        QScopedValueRollback<bool> dispatch(m_devices->m_autofocusDispatch,true);
        QScopedValueRollback<bool> sequenceDispatch(m_devices->m_sequenceDispatch,m_sequenceRun);
        if (!m_devices->haltFocuser()) m_status=QStringLiteral("电调焦停止失败，请重试停止：%1").arg(m_devices->lastError());
    }
    emit changed();
}

void AutoFocusController::finish(bool success,const QString &message) {
    m_phase=Idle; m_timer.stop(); m_succeeded=success; m_status=message;
    m_devices->m_autofocusOwned=false; m_sequenceRun=false;
    if (success) m_devices->establishFilterFocusBaseline(m_bestPosition);
    else m_devices->invalidateFilterFocusBaseline();
    emit m_devices->filterFocusSettingsChanged();
    emit changed(); emit notice(message);
}

void AutoFocusController::acceptMeasurement() {
    const auto result=m_analysis.result();
    m_starCount=result.stars;
    if (!result.error.isEmpty() || !std::isfinite(result.hfr) || result.hfr<=0 || result.stars<5) {
        stop(QStringLiteral("无法测量有效星点：%1").arg(result.error.isEmpty() ? QStringLiteral("至少需要 5 颗未饱和星点") : result.error),true); return;
    }
    m_frames.append(result); emit changed();
    if (m_frames.size()<m_settings.value("frames").toInt()) { capture(); return; }
    QVector<double> hfrs,stars;
    for (const auto &frame : m_frames) { hfrs.append(frame.hfr); stars.append(frame.stars); }
    const double hfr=median(hfrs); m_starCount=int(median(stars)); m_frames.clear();
    if (m_movePurpose==BestPoint) {
        m_verifiedHfr=hfr;
        const double bestSample=std::min_element(m_samples.begin(),m_samples.end(),[](const auto &a,const auto &b) { return a.hfr<b.hfr; })->hfr;
        if (hfr>bestSample*1.25) { stop(QStringLiteral("最佳位置验证未通过，HFR 比最佳采样值恶化超过 25%"),true); return; }
        finish(true,QStringLiteral("自动对焦成功 · 位置 %1 · 验证 HFR %2 px").arg(m_bestPosition).arg(hfr,0,'f',2)); return;
    }
    m_samples.append({m_target,hfr,m_starCount}); emit changed();
    if (m_samples.size()<m_settings.value("points").toInt()) {
        move(m_first+m_samples.size()*m_settings.value("step").toInt(),ScanPoint); return;
    }
    const auto fit=FocusMetrics::fit(m_samples);
    if (!fit.valid) { stop(QStringLiteral("对焦曲线拟合失败：%1").arg(fit.error),true); return; }
    m_bestPosition=qRound(fit.position); m_bestHfr=fit.hfr; m_rSquared=fit.rSquared;
    for (const auto &point : fit.curve) m_fitCurve.append(QVariantMap{{"position",point.x()},{"hfr",point.y()}});
    const int backlash=m_settings.value("backlash").toInt();
    move(m_bestPosition-backlash,backlash ? BestApproach : BestPoint);
}

void AutoFocusController::tick() {
    if (!busy()) return;
    const auto &focuser=m_devices->m_slots[Focuser];
    const auto &camera=m_devices->m_slots[Camera];
    const auto metrics=focuser.metrics;
    if (m_phase==Stopping) {
        if (camera.connected && !m_devices->m_autofocusCapturePending && !m_devices->m_commandsPending[Camera]
            && camera.metrics.value("exposureUs").toUInt()!=m_originalExposure) {
            const QString status=QStringLiteral("相机曝光尚未恢复，请重试停止或断开相机");
            if (m_status!=status) { m_status=status; emit changed(); }
        }
        if (!focuserStopped() || !cameraSettled() || m_analysis.isRunning()) return;
        bool ok=false;
        const int position=metrics.value("position").toInt(&ok);
        if (m_returnToOriginal && focuser.connected && ok && position>=0 && position!=m_original
            && metrics.value("maxStep").toInt()>=m_original) {
            m_returnToOriginal=false; move(m_original,ReturnOriginal);
        } else finish(false,m_stopReason);
        return;
    }
    if (!focuser.connected) { stop(QStringLiteral("电调焦断开，自动对焦未完成"),false); return; }
    if (m_movePurpose!=ReturnOriginal && (!camera.connected || m_devices->m_manualMoving || m_devices->m_mountGuiding
        || m_devices->m_slots[DeviceManager::Mount].metrics.value("slewing").toBool())) {
        stop(QStringLiteral("相机断开或检测到其他设备运动，自动对焦未完成"),true); return;
    }
    bool positionOk=false;
    const int position=metrics.value("position").toInt(&positionOk);
    if (m_phase==Moving) {
        if (m_elapsed.elapsed()>180000) { stop(QStringLiteral("调焦移动超时，未到达目标位置"),m_movePurpose!=ReturnOriginal); return; }
        if (!positionOk || position!=m_target || !focuserStopped()) return;
        if (m_movePurpose==ScanApproach) { move(m_first,ScanPoint); return; }
        if (m_movePurpose==BestApproach) { move(m_bestPosition,BestPoint); return; }
        if (m_movePurpose==ReturnOriginal) {
            if (cameraSettled() && !m_analysis.isRunning()) finish(false,m_stopReason);
            return;
        }
        m_phase=Settling; m_elapsed.restart(); m_status=QStringLiteral("等待电调焦稳定…"); emit changed(); return;
    }
    if (!positionOk || position!=m_target || !focuserStopped()) { stop(QStringLiteral("曝光期间电调焦位置发生变化，自动对焦中止"),true); return; }
    if (m_phase==Settling) {
        if (m_elapsed.elapsed()>=500 && cameraSettled()) capture();
    } else if (m_phase==Capturing) {
        if (m_elapsed.elapsed()>m_settings.value("exposure").toDouble()*1000+30000) stop(QStringLiteral("自动对焦 RAW 拍摄超时"),true);
    } else if (m_phase==Measured && cameraSettled()) acceptMeasurement();
    else if ((m_phase==Analyzing || m_phase==Measured) && m_elapsed.elapsed()>30000)
        stop(QStringLiteral("星点分析或相机恢复超时"),true);
}
