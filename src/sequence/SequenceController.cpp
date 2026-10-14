#include "SequenceController.h"
#include "devices/DeviceManager.h"
#include "devices/CameraBackend.h"
#include "astro/CenteringController.h"
#include "focus/AutoFocusController.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QTemporaryFile>
#include <cmath>

namespace {
constexpr int Camera = DeviceManager::MainCamera;
constexpr int Wheel = DeviceManager::FilterWheel;
bool wheelBusy(const QVariantMap &metrics)
{
    return metrics.value("moving").toBool() || metrics.value("calibrating").toBool();
}
}

SequenceController::SequenceController(DeviceManager *devices, QObject *parent, CenteringController *centering, AutoFocusController *autofocus)
    : QObject(parent), m_devices(devices), m_centering(centering), m_autofocus(autofocus)
{
    newPlan();
    m_timer.setInterval(100);
    connect(this, &SequenceController::changed, this, &SequenceController::progressChanged);
    connect(&m_timer, &QTimer::timeout, this, &SequenceController::tick);
    connect(devices, &DeviceManager::stillCaptured, this, &SequenceController::result);
    connect(devices, &DeviceManager::focuserCommandFailed, this, [this](const QString &error) {
        if (m_phase == "FocusOffset") stopFilterOffset(QStringLiteral("滤镜焦点补偿失败：%1").arg(error));
    });
    connect(devices, &DeviceManager::focuserStopRequested, this, [this] {
        if (m_phase == "FocusOffset") stopFilterOffset(QStringLiteral("滤镜焦点补偿被紧急停止，未开始任务曝光"));
    });
    connect(devices, &DeviceManager::stillFrameReceived, this, [this](int role) {
        if (!m_inFlight || role != Camera) return;
        ++m_exposed; m_phase = "Saving"; emit changed();
    });
}

QString SequenceController::preparationStatus() const
{
    if (m_phase == "Centering" && m_centering) return m_centering->status();
    if (m_phase == "Autofocus" && m_autofocus) return m_autofocus->status();
    if (m_phase == "FocusFilter") return QStringLiteral("正在准备自动对焦滤镜，等待实际到位…");
    if (m_phase == "FocusGain") return QStringLiteral("正在准备自动对焦增益，等待设备确认…");
    if (m_phase == "FocusOffset") return QStringLiteral("正在补偿滤镜焦点，等待实际到位并稳定…");
    return {};
}

SequenceController::~SequenceController()
{
    if (m_owned) { stop(); m_devices->m_sequenceOwned = false; }
}

QVariantList SequenceController::rows() const
{
    auto result = m_rows;
    for (int i = 0; i < result.size(); ++i) {
        auto row = result[i].toMap(); row.insert("saved", m_counts.value(i)); result[i] = row;
    }
    return result;
}

int SequenceController::total() const
{
    int count = 0;
    for (const auto &item : m_rows) {
        const auto row = item.toMap();
        if (row.value("enabled").toBool()) count += row.value("count").toInt();
    }
    return count;
}

double SequenceController::progress() const
{
    if (!m_inFlight || m_row < 0) return 0;
    if (m_phase == "Saving") return 1;
    return qBound(0.0, m_elapsed.elapsed() / (m_rows[m_row].toMap().value("exposure").toDouble() * 1000), 0.99);
}

void SequenceController::newPlan()
{
    if (active()) return;
    m_plan = {{"name", QStringLiteral("新建拍摄计划")}, {"target", ""}, {"ra", ""}, {"dec", ""},
              {"directory", m_devices->stillSaveDirectory()}, {"centerBeforeCapture", false}, {"focusBeforeCapture", false}, {"focusAfterFilterChange", false},
              {"centerExposure", 2.0}, {"centerTolerance", 1.0}, {"centerCorrections", 3}};
    m_rows.clear(); m_counts.clear(); m_row = -1; m_saved = m_exposed = m_failed = 0;
    m_focused = false; m_state = "Idle"; m_phase.clear(); emit changed();
}

void SequenceController::resetProgress()
{
    if (active()) return;
    m_counts.fill(0, m_rows.size()); m_row = -1; m_saved = m_exposed = m_failed = 0;
    m_focused = false; m_state = "Idle"; m_phase.clear(); emit changed();
}

void SequenceController::setHeader(const QString &key, const QVariant &value)
{
    if (active()) return;
    if (QStringList{"name", "target", "ra", "dec", "directory"}.contains(key)) m_plan.insert(key, value.toString());
    else if (QStringList{"centerBeforeCapture", "centerExposure", "centerTolerance", "centerCorrections", "focusBeforeCapture", "focusAfterFilterChange"}.contains(key)) m_plan.insert(key, value);
    else return;
    emit changed();
}

void SequenceController::addRow()
{
    if (active() || m_rows.size() >= 1000) return;
    m_rows.append(QVariantMap{{"enabled", true}, {"type", "LIGHT"}, {"filter", -1},
                             {"exposure", 1.0}, {"gain", 100}, {"count", 1}});
    m_counts.append(0); emit changed();
}

void SequenceController::removeRow(int index)
{
    if (active() || index < 0 || index >= m_rows.size()) return;
    m_rows.removeAt(index); m_counts.removeAt(index); emit changed();
}

void SequenceController::updateRow(int index, const QString &key, const QVariant &value)
{
    if (active() || index < 0 || index >= m_rows.size()
        || !QStringList{"enabled", "type", "filter", "exposure", "gain", "count"}.contains(key)) return;
    auto row = m_rows[index].toMap(); row.insert(key, value); m_rows[index] = row; emit changed();
}

QString SequenceController::validate(const QVariantMap &plan, bool hardware) const
{
    if (plan.contains("focusBeforeCapture") && plan.value("focusBeforeCapture").metaType().id()!=QMetaType::Bool)
        return QStringLiteral("拍摄前自动对焦选项必须为布尔值");
    if (plan.contains("focusAfterFilterChange") && plan.value("focusAfterFilterChange").metaType().id()!=QMetaType::Bool)
        return QStringLiteral("换滤镜后自动对焦选项必须为布尔值");
    const bool focus = plan.value("focusBeforeCapture", false).toBool();
    for (const auto &key : QStringList{"centerExposure", "centerTolerance", "centerCorrections"})
        if (plan.contains(key) && !QJsonValue::fromVariant(plan.value(key)).isDouble())
            return QStringLiteral("居中曝光、容差和校正次数必须为数字");
    const bool center = plan.value("centerBeforeCapture", false).toBool();
    bool exposureOk=false, toleranceOk=false, limitOk=false;
    const double seconds=plan.value("centerExposure",2.0).toDouble(&exposureOk);
    const double tolerance=plan.value("centerTolerance",1.0).toDouble(&toleranceOk);
    const double limit=plan.value("centerCorrections",3).toDouble(&limitOk);
    if ((plan.contains("centerBeforeCapture") && plan.value("centerBeforeCapture").metaType().id()!=QMetaType::Bool)
        || !exposureOk || !std::isfinite(seconds) || seconds<=0 || seconds>3600
        || !toleranceOk || !std::isfinite(tolerance) || tolerance<0.1 || tolerance>10
        || !limitOk || !std::isfinite(limit) || limit<1 || limit>3 || limit!=std::floor(limit))
        return QStringLiteral("居中参数无效，请检查解析曝光、容差和校正次数");
    if (center) {
        bool raOk=false, decOk=false;
        const double ra=plan.value("ra").toDouble(&raOk), dec=plan.value("dec").toDouble(&decOk);
        if (plan.value("target").toString().trimmed().isEmpty() || !raOk || !decOk || !CenteringMath::valid(ra,dec))
            return QStringLiteral("请先从星图导入具有有效 J2000 坐标的拍摄目标");
        if (hardware && !m_centering) return QStringLiteral("居中服务未就绪，未开始拍摄");
    }
    if (plan.value("rows").metaType().id() != QMetaType::QVariantList)
        return QStringLiteral("计划任务列表格式错误");
    const auto rows = plan.value("rows").toList();
    if (rows.size() > 1000) return QStringLiteral("任务行过多");
    int enabled = 0;
    bool hasLight = false;
    const auto parameters = m_devices->cameraParameters(Camera);
    const auto &wheel = m_devices->m_slots[Wheel];
    for (const auto &item : rows) {
        const auto row = item.toMap();
        bool exposureOk = false, gainOk = false, countOk = false, filterOk = false;
        const double exposure = row.value("exposure").toDouble(&exposureOk), gain = row.value("gain").toDouble(&gainOk);
        const double count = row.value("count").toDouble(&countOk), filter = row.value("filter").toDouble(&filterOk);
        if (!exposureOk || !gainOk || !countOk || !filterOk || row.value("enabled").metaType().id() != QMetaType::Bool
            || !QStringList{"LIGHT", "DARK", "FLAT", "BIAS"}.contains(row.value("type").toString())
            || !std::isfinite(exposure) || exposure < 0.000001 || exposure > 4294.967295
            || !std::isfinite(gain) || gain < 0 || gain > 65535 || gain != std::floor(gain)
            || !std::isfinite(count) || count < 1 || count > 100000 || count != std::floor(count)
            || !row.contains("filter") || !std::isfinite(filter) || filter < -1 || filter > 255 || filter != std::floor(filter))
            return QStringLiteral("任务参数无效：请检查类型、曝光、增益、张数和滤镜位置");
        if (!row.value("enabled").toBool()) continue;
        if (row.value("type").toString() == "LIGHT") hasLight = true;
        if (center && row.value("type").toString() != "LIGHT")
            return QStringLiteral("拍摄前居中仅用于 LIGHT 计划；校准帧请关闭此选项");
        ++enabled;
        if (!hardware) continue;
        if ((parameters.contains("exposureMinSeconds") && exposure < parameters.value("exposureMinSeconds").toDouble())
            || (parameters.contains("exposureMaxSeconds") && exposure > parameters.value("exposureMaxSeconds").toDouble())
            || (parameters.contains("gainMin") && gain < parameters.value("gainMin").toDouble())
            || (parameters.contains("gainMax") && gain > parameters.value("gainMax").toDouble()))
            return QStringLiteral("任务曝光或增益超出当前相机范围");
        if (filter >= 0 && (!wheel.connected || filter >= wheel.metrics.value("slots").toInt()))
            return QStringLiteral("指定滤镜不可用，请连接滤镜轮并检查档位");
    }
    if (focus && !hasLight) return QStringLiteral("拍摄前自动对焦需要至少一行启用的 LIGHT 任务；校准帧计划请关闭此选项");
    if (!hardware) return {};
    if (m_devices->m_centeringOwned) return QStringLiteral("请先停止指向并居中");
    if (m_devices->m_autofocusOwned) return QStringLiteral("请先停止自动对焦");
    if (!enabled) return QStringLiteral("请至少启用一行拍摄任务");
    if (!m_devices->m_slots[Camera].connected) return QStringLiteral("请先连接主相机");
    if (m_devices->m_commandsPending[Camera] > 0 || m_devices->m_slots[Camera].busy || m_devices->m_slots[Camera].metrics.value("previewing").toBool()
        || m_devices->m_unsettledStills[Camera] > 0 || m_devices->m_saveQueue->pendingCount() > 0)
        return QStringLiteral("请先停止主相机预览并等待曝光和保存完成");
    if (m_devices->m_commandsPending[Wheel] > 0 || wheel.busy || wheelBusy(wheel.metrics)) return QStringLiteral("请等待滤镜轮动作完成");
    if (plan.value("directory").toString().trimmed().isEmpty()) return QStringLiteral("请选择保存目录");
    if ((focus && (!active() || !m_focused)) || (plan.value("focusAfterFilterChange").toBool() && !m_devices->m_filterFocusEnabled)) {
        if (!m_autofocus) return QStringLiteral("自动对焦服务未就绪，未开始拍摄");
        const auto error = m_autofocus->validationError(true);
        if (!error.isEmpty()) return error;
    }
    return {};
}

bool SequenceController::start()
{
    if (active() && m_state != "Paused") return false;
    auto candidate = m_plan; candidate.insert("rows", m_rows);
    const auto error = validate(candidate, true);
    if (!error.isEmpty()) { emit notice(error); return false; }
    const QString directory = m_plan.value("directory").toString();
    QTemporaryFile probe(QDir(directory).filePath(".stellamora-XXXXXX"));
    if (!QDir().mkpath(directory) || !probe.open()) { emit notice(QStringLiteral("保存目录不可写")); return false; }
    if (!active()) {
        if (m_devices->m_sequenceOwned) return false;
        m_counts.fill(0, m_rows.size()); m_row = -1; m_saved = m_exposed = m_failed = 0;
        m_owned = m_devices->m_sequenceOwned = true;
        emit m_devices->filterFocusSettingsChanged();
        m_centered = false; m_focused = false;
        m_pendingFilterRow = -1; m_offsetTarget = -1; m_filterAutofocus = false;
    }
    m_state = "Running"; m_phase.clear(); m_timer.start();
    if (needsCentering()) {
        m_phase = "Centering";
        if (!m_centering->startForSequence(m_plan.value("ra").toDouble(),m_plan.value("dec").toDouble(),
                m_plan.value("target").toString(),m_plan.value("centerExposure").toDouble(),
                m_plan.value("centerTolerance").toDouble(),m_plan.value("centerCorrections").toInt())) {
            suspend(QStringLiteral("无法开始拍摄前居中，请检查设备和 ASTAP 后重试，或停止计划"));
            return false;
        }
        emit changed();
    } else prepareAutofocus();
    return true;
}

void SequenceController::prepareAutofocus()
{
    if (!needsAutofocus()) { advance(); return; }
    m_focusRow = 0;
    while (m_focusRow < m_rows.size()) {
        const auto row = m_rows[m_focusRow].toMap();
        if (row.value("enabled").toBool() && row.value("type").toString() == "LIGHT") break;
        ++m_focusRow;
    }
    const auto &wheel = m_devices->m_slots[Wheel];
    const int filter = m_rows[m_focusRow].toMap().value("filter").toInt();
    if (filter >= 0 && (!wheel.connected || wheel.busy || wheelBusy(wheel.metrics))) {
        suspend(QStringLiteral("自动对焦准备失败：滤镜轮未就绪")); return;
    }
    if (filter >= 0 && wheel.metrics.value("position", -1).toInt() != filter) {
        QScopedValueRollback<bool> allow(m_devices->m_sequenceDispatch, true);
        if (!m_devices->moveFilterWheel(filter)) { suspend(m_devices->lastError()); return; }
        m_phase = "FocusFilter"; m_elapsed.restart(); emit changed(); return;
    }
    prepareFocusGain();
}

void SequenceController::prepareFocusGain()
{
    m_phase = "FocusGain"; m_elapsed.restart();
    const auto error = m_focusGainError = std::make_shared<QString>();
    auto *camera = m_devices->m_camera;
    const unsigned gain = m_rows[m_focusRow].toMap().value("gain").toUInt();
    m_devices->postDeviceCommand(camera, Camera, [camera, gain, error] {
        const auto listener = QObject::connect(camera, &CameraBackend::commandFailed, camera,
            [error](const QString &message) { *error = message; }, Qt::DirectConnection);
        camera->setGain(gain);
        QObject::disconnect(listener);
    });
    emit changed();
}

void SequenceController::advance()
{
    if (m_state != "Running" || m_inFlight) return;
    m_row = 0;
    while (m_row < m_rows.size()) {
        const auto row = m_rows[m_row].toMap();
        if (row.value("enabled").toBool() && m_counts[m_row] < row.value("count").toInt()) break;
        ++m_row;
    }
    if (m_row == m_rows.size()) { release("Completed"); emit notice(QStringLiteral("拍摄计划已完成，所有帧已保存")); return; }
    const auto &wheel = m_devices->m_slots[Wheel];
    const int filter = m_rows[m_row].toMap().value("filter").toInt();
    if (filter >= 0 && !wheel.connected) { suspend(QStringLiteral("滤镜轮已断开，序列暂停")); return; }
    if (wheel.connected && wheelBusy(wheel.metrics)) { suspend(QStringLiteral("滤镜轮尚未就绪")); return; }
    if (filter >= 0 && wheel.metrics.value("position", -1).toInt() != filter) {
        if (m_pendingFilterRow != m_row) {
            m_pendingFilterRow = m_row; m_offsetTarget = -1;
        }
        if (m_devices->m_filterFocusEnabled && m_offsetTarget < 0 && !planFilterOffset(filter)) return;
        QScopedValueRollback<bool> allow(m_devices->m_sequenceDispatch, true);
        if (!m_devices->moveFilterWheel(filter)) { suspend(m_devices->lastError()); return; }
        m_phase = "Filter"; m_elapsed.restart(); emit changed(); return;
    }
    if (m_pendingFilterRow == m_row) { prepareChangedFilter(); return; }
    capture();
}

bool SequenceController::planFilterOffset(int filter)
{
    const auto &focuser = m_devices->m_slots[DeviceManager::Focuser];
    int targetOffset = 0, currentOffset = 0;
    const int currentFilter = m_devices->m_slots[Wheel].metrics.value("position", -1).toInt();
    if (!m_devices->m_filterFocusBaselineValid || !m_devices->filterFocusOffset(currentFilter, currentOffset)
        || !m_devices->filterFocusOffset(filter, targetOffset)) {
        suspend(QStringLiteral("滤镜焦点补偿缺少有效基准或滤镜偏移；请停止计划、完成实测偏移并成功自动对焦后重试（不会自动回退自动对焦）")); return false;
    }
    const qint64 target = m_devices->m_filterFocusReferencePosition + targetOffset;
    const int backlash = m_autofocus ? m_autofocus->settings().value("backlash").toInt() : 0;
    if (!focuser.connected || !offsetFocuserStopped() || focuser.metrics.value("position", -1).toInt() != m_devices->m_filterFocusPosition
        || target < 0 || target > focuser.metrics.value("maxStep").toLongLong()
        || (target != focuser.metrics.value("position", -1).toInt() && target - backlash < 0)) {
        suspend(QStringLiteral("滤镜焦点补偿位置超出行程或电调焦未就绪，未开始任务曝光")); return false;
    }
    m_offsetTarget = int(target); m_offsetStage = -1; return true;
}

bool SequenceController::offsetFocuserStopped() const
{
    const auto &focuser = m_devices->m_slots[DeviceManager::Focuser];
    return !focuser.busy && !m_devices->m_commandsPending[DeviceManager::Focuser]
        && (!focuser.connected || (focuser.metrics.contains("moving") && !focuser.metrics.value("moving").toBool()));
}

void SequenceController::prepareChangedFilter()
{
    if (m_state != "Running") return;
    if (m_devices->m_filterFocusEnabled) {
        if (m_offsetTarget < 0 || !m_devices->m_filterFocusBaselineValid) {
            suspend(QStringLiteral("滤镜焦点补偿基准无效，请停止计划并重新自动对焦")); return;
        }
        if (!m_devices->m_slots[DeviceManager::Focuser].connected) {
            suspend(QStringLiteral("电调焦连接中断，滤镜焦点补偿未完成")); return;
        }
        if (offsetFocuserStopped() && m_devices->m_slots[DeviceManager::Focuser].metrics.value("position", -1).toInt() == m_offsetTarget) {
            if (m_offsetStage < 0) {
                m_devices->m_filterFocusPosition = m_offsetTarget; m_pendingFilterRow = -1; capture(); return;
            }
            m_devices->m_filterFocusMoving = true; m_offsetStopping = false; m_offsetError.clear();
            m_offsetMoveTarget = m_offsetTarget; m_offsetStage = 1;
            m_phase = "FocusOffset"; m_elapsed.restart(); m_offsetStable.start(); emit changed(); return;
        }
        m_offsetStage = 0; m_offsetStopping = false; m_offsetError.clear(); m_offsetStable.invalidate();
        m_devices->m_filterFocusMoving = true; moveFilterOffset(); return;
    }
    const auto row = m_rows[m_row].toMap();
    if (m_plan.value("focusAfterFilterChange").toBool() && row.value("type").toString() == "LIGHT") {
        m_filterAutofocus = true; m_focusRow = m_row; prepareFocusGain(); return;
    }
    m_pendingFilterRow = -1; capture();
}

void SequenceController::moveFilterOffset()
{
    const int backlash = m_autofocus ? m_autofocus->settings().value("backlash").toInt() : 0;
    m_offsetMoveTarget = m_offsetTarget - (m_offsetStage == 0 ? backlash : 0);
    if (!backlash) m_offsetStage = 1;
    m_phase = "FocusOffset"; m_elapsed.restart(); m_offsetStable.invalidate();
    QScopedValueRollback<bool> allow(m_devices->m_sequenceDispatch, true);
    if (!m_devices->moveFocuser(m_offsetMoveTarget)) stopFilterOffset(m_devices->lastError());
    emit changed();
}

void SequenceController::stopFilterOffset(const QString &error)
{
    if (!error.isEmpty()) {
        m_offsetError = error; m_devices->invalidateFilterFocusBaseline();
        if (m_state == "Running") m_state = "Pausing";
    }
    if (m_offsetStopping && (!error.isEmpty() || m_devices->m_commandsPending[DeviceManager::Focuser])) return;
    m_offsetStopping = true; m_offsetStable.invalidate();
    QScopedValueRollback<bool> allow(m_devices->m_sequenceDispatch, true);
    if (m_devices->m_slots[DeviceManager::Focuser].connected) m_devices->haltFocuser();
    emit changed();
}

void SequenceController::capture()
{
    const auto row = m_rows[m_row].toMap();
    StillRequest request; request.role = Camera;
    request.directory = m_plan.value("directory").toString(); request.targetName = m_plan.value("target").toString();
    request.imageType = row.value("type").toString();
    const auto &wheel = m_devices->m_slots[Wheel];
    if (wheel.connected) {
        const int position = wheel.metrics.value("position", -1).toInt();
        if (position < 0 || wheelBusy(wheel.metrics)) { suspend(QStringLiteral("滤镜位置未知，序列暂停")); return; }
        const auto channels = m_devices->m_filterWheelChannels;
        const auto suffix = position < channels.size() ? channels[position].toMap().value("suffix").toString() : QString();
        request.header.insert("FILTER", suffix.isEmpty() ? QString::number(position + 1) : suffix);
    }
    request.header.insert("SEQROW", QString::number(m_row + 1));
    request.header.insert("SEQFRAME", QString::number(m_counts[m_row] + 1));
    QScopedValueRollback<bool> allow(m_devices->m_sequenceDispatch, true);
    m_inFlight = true; m_phase = "Exposing"; m_elapsed.restart();
    if (!m_devices->beginStill(Camera, std::move(request), row.value("exposure").toDouble(), row.value("gain").toInt())) {
        m_inFlight = false; suspend(m_devices->lastError()); return;
    }
    emit changed();
}

void SequenceController::tick()
{
    if (!active()) return;
    if (m_phase == "FocusOffset") {
        if (m_state == "Paused") return;
        const auto &focuser = m_devices->m_slots[DeviceManager::Focuser];
        if (m_offsetStopping) {
            if (!offsetFocuserStopped() || m_devices->m_commandsPending[Wheel]) return;
            m_devices->m_filterFocusPosition = focuser.metrics.value("position", -1).toInt();
            m_devices->m_filterFocusMoving = false;
            if (m_state == "Stopping") release("Idle"); else suspend(m_offsetError);
            return;
        }
        const auto &wheel = m_devices->m_slots[Wheel];
        if (!focuser.connected || !wheel.connected || !m_devices->m_slots[Camera].connected
            || wheelBusy(wheel.metrics) || wheel.metrics.value("position", -1).toInt() != m_rows[m_row].toMap().value("filter").toInt()
            || !m_devices->m_filterFocusBaselineValid) {
            stopFilterOffset(QStringLiteral("设备连接或滤镜焦点基准发生变化，补偿中止，未开始任务曝光")); return;
        }
        if (m_elapsed.elapsed() > 180000) { stopFilterOffset(QStringLiteral("滤镜焦点补偿超时，未开始任务曝光")); return; }
        if (!offsetFocuserStopped() || focuser.metrics.value("position", -1).toInt() != m_offsetMoveTarget) {
            if (m_offsetStable.isValid()) {
                stopFilterOffset(QStringLiteral("滤镜焦点稳定期间检测到位置漂移，未开始任务曝光")); return;
            }
            m_offsetStable.invalidate(); return;
        }
        if (m_offsetStage == 0) { m_offsetStage = 1; moveFilterOffset(); return; }
        if (!m_offsetStable.isValid()) { m_offsetStable.start(); return; }
        if (m_offsetStable.elapsed() < 500) return;
        m_devices->m_filterFocusPosition = m_offsetTarget; m_devices->m_filterFocusMoving = false;
        m_pendingFilterRow = -1; m_offsetTarget = -1; capture(); return;
    }
    if (m_phase == "Centering") {
        if (m_state == "Paused") return;
        if (m_centering->busy()) { emit changed(); return; }
        if (m_state == "Stopping") { release("Idle"); return; }
        if (m_state == "Pausing") { suspend({}); return; }
        if (!m_centering->succeeded()) {
            suspend(QStringLiteral("拍摄前居中未完成，未开始任务曝光。可重试居中或停止计划")); return;
        }
        m_centered = true; m_phase.clear(); prepareAutofocus(); return;
    }
    if (m_phase == "FocusFilter" || m_phase == "FocusGain") {
        if (m_state == "Paused") return;
        if (m_devices->m_commandsPending[Camera] || m_devices->m_commandsPending[Wheel]) return;
        const auto &wheel = m_devices->m_slots[Wheel];
        if (m_state == "Stopping") {
            const bool movingFilter = wheelBusy(wheel.metrics);
            release("Idle");
            if (movingFilter) emit notice(QStringLiteral("序列已停止；滤镜轮仍可能完成已下发的动作，请等待到位或在设备页断开处理"));
            return;
        }
        if (!m_devices->m_slots[Camera].connected) { suspend(QStringLiteral("主相机已断开，自动对焦准备中止")); return; }
        if (m_phase == "FocusFilter") {
            if (!wheel.connected) { suspend(QStringLiteral("滤镜轮连接中断，自动对焦准备中止")); return; }
            if (!wheel.busy && !wheelBusy(wheel.metrics)
                && wheel.metrics.value("position", -1).toInt() == m_rows[m_focusRow].toMap().value("filter").toInt()) {
                if (m_state == "Pausing") suspend({}); else prepareFocusGain();
            } else if (m_elapsed.elapsed() > 60000) suspend(QStringLiteral("自动对焦滤镜准备超时，请检查设备后重试或停止"));
            return;
        }
        // The worker writes the result before the counted command acknowledgement reaches this thread.
        if (!m_focusGainError->isEmpty()) { suspend(QStringLiteral("自动对焦增益设置失败：%1").arg(*m_focusGainError)); return; }
        if (m_state == "Pausing") { suspend({}); return; }
        bool gainOk = false;
        const int actualGain = m_devices->m_slots[Camera].metrics.value("gain").toInt(&gainOk);
        if (!gainOk || actualGain != m_rows[m_focusRow].toMap().value("gain").toInt()) {
            suspend(QStringLiteral("自动对焦增益回读与任务不一致，未开始调焦或曝光")); return;
        }
        m_phase = "Autofocus";
        if (!m_autofocus->startForSequence()) suspend(QStringLiteral("无法开始拍摄前自动对焦：%1").arg(m_autofocus->status()));
        else emit changed();
        return;
    }
    if (m_phase == "Autofocus") {
        if (m_state == "Paused") return;
        if (m_autofocus->busy()) { emit changed(); return; }
        if (m_state == "Stopping") { release("Idle"); return; }
        if (m_state == "Pausing") { suspend({}); return; }
        if (!m_autofocus->succeeded()) {
            suspend(QStringLiteral("拍摄前自动对焦未完成，未开始任务曝光。可重试自动对焦或停止计划")); return;
        }
        if (m_filterAutofocus) {
            m_filterAutofocus = false; m_pendingFilterRow = -1; m_phase.clear(); capture(); return;
        }
        m_focused = true; m_phase.clear(); advance(); return;
    }
    if (m_phase == "Filter") {
        const auto &wheel = m_devices->m_slots[Wheel];
        if (m_state != "Running" && m_state != "Pausing") return;
        if (!wheel.connected) { suspend(QStringLiteral("滤镜轮连接中断")); return; }
        if (!m_devices->m_commandsPending[Wheel] && !wheel.busy && !wheelBusy(wheel.metrics) && wheel.metrics.value("position", -1).toInt() == m_rows[m_row].toMap().value("filter").toInt()) {
            if (m_state == "Pausing") suspend({}); else prepareChangedFilter();
        } else if (m_elapsed.elapsed() > 60000) suspend(QStringLiteral("等待滤镜到位超时，请检查设备后继续或停止"));
    } else if (m_inFlight) emit progressChanged();
}

void SequenceController::result(int role, const QString &, bool ok)
{
    if (!m_inFlight || role != Camera) return;
    m_inFlight = false;
    if (ok) { ++m_saved; ++m_counts[m_row]; }
    else if (m_state != "Stopping") ++m_failed;
    if (m_state == "Stopping") { release("Idle"); return; }
    if (!ok) { suspend(QStringLiteral("本帧未保存，序列已暂停。排除故障后继续将重新拍摄本帧")); return; }
    if (m_state == "Pausing") { suspend({}); return; }
    advance();
}

void SequenceController::pause()
{
    if (m_state != "Running") return;
    if (m_phase == "FocusOffset") { m_state = "Pausing"; stopFilterOffset(); return; }
    if (m_phase == "Centering") {
        m_state = "Pausing"; m_centering->cancel(); emit changed(); return;
    }
    if (m_phase == "Autofocus") {
        m_state = "Pausing"; m_autofocus->cancel(); emit changed(); return;
    }
    if (m_phase == "FocusFilter" || m_phase == "FocusGain") { m_state = "Pausing"; emit changed(); return; }
    if (m_inFlight || m_phase == "Filter") { m_state = "Pausing"; emit changed(); }
    else suspend({});
}

void SequenceController::suspend(const QString &message)
{
    m_state = "Paused"; emit changed();
    if (!message.isEmpty()) emit notice(message);
}

void SequenceController::stop()
{
    if (!active()) return;
    if (m_phase == "FocusOffset" || (m_pendingFilterRow >= 0 && m_offsetTarget >= 0))
        m_devices->invalidateFilterFocusBaseline();
    m_state = "Stopping";
    if (m_phase == "FocusOffset") { stopFilterOffset(); return; }
    if (m_phase == "Centering") { m_centering->cancel(); emit changed(); return; }
    if (m_phase == "Autofocus") { m_autofocus->cancel(); emit changed(); return; }
    if (m_phase == "FocusFilter" || m_phase == "FocusGain") { emit changed(); return; }
    if (m_inFlight) {
        if (m_phase != "Saving") {
            auto *camera = m_devices->m_camera;
            QMetaObject::invokeMethod(camera, [camera] { camera->stopPreview(); }, Qt::QueuedConnection);
        }
        emit changed();
    } else {
        const bool movingFilter = m_phase == "Filter";
        release("Idle");
        if (movingFilter) emit notice(QStringLiteral("序列已停止；滤镜轮仍可能完成已下发的动作，请等待到位或在设备页断开处理"));
    }
}

void SequenceController::release(const QString &state)
{
    m_timer.stop(); m_owned = m_devices->m_sequenceOwned = false;
    m_devices->m_filterFocusMoving = false; m_devices->m_filterFocusWheelTarget = -1;
    emit m_devices->filterFocusSettingsChanged();
    m_state = state; m_phase.clear(); m_row = -1; emit changed();
}

bool SequenceController::savePlan(const QUrl &url)
{
    auto data = m_plan; data.insert("rows", m_rows); data.insert("version", 1);
    const auto error = validate(data, false);
    if (!error.isEmpty()) { emit notice(error); return false; }
    QSaveFile file(url.toLocalFile());
    const auto bytes = QJsonDocument::fromVariant(data).toJson();
    if (!url.isLocalFile() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        emit notice(QStringLiteral("计划保存失败：") + file.errorString()); return false;
    }
    emit notice(QStringLiteral("计划已保存")); return true;
}

bool SequenceController::loadPlan(const QUrl &url)
{
    if (active()) return false;
    QFile file(url.toLocalFile());
    if (!url.isLocalFile() || !file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) { emit notice(QStringLiteral("无法读取计划文件")); return false; }
    QJsonParseError parse;
    const auto data = QJsonDocument::fromJson(file.readAll(), &parse).toVariant().toMap();
    if (parse.error != QJsonParseError::NoError || data.value("version").toInt() != 1 || !data.contains("rows")) {
        emit notice(QStringLiteral("计划格式或版本不支持")); return false;
    }
    const auto error = validate(data, false);
    if (!error.isEmpty()) { emit notice(error); return false; }
    newPlan();
    for (const auto &key : QStringList{"name", "target", "ra", "dec", "directory"}) m_plan[key] = data.value(key).toString();
    for (const auto &key : QStringList{"centerBeforeCapture", "centerExposure", "centerTolerance", "centerCorrections", "focusBeforeCapture", "focusAfterFilterChange"})
        if (data.contains(key)) m_plan[key] = data.value(key);
    m_rows = data.value("rows").toList(); m_counts.fill(0, m_rows.size()); emit changed(); return true;
}
