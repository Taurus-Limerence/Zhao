#include "CenteringController.h"
#include "PlateSolver.h"
#include "devices/DeviceManager.h"
#include <QFileInfo>
#include <QScopedValueRollback>

CenteringController::CenteringController(DeviceManager *devices,PlateSolver *solver,QObject *parent)
    : QObject(parent),m_devices(devices),m_solver(solver) {
    m_timer.setInterval(100);
    connect(&m_timer,&QTimer::timeout,this,&CenteringController::tick);
    connect(devices,&DeviceManager::mountMotionStopRequested,this,[this] { if (busy() && m_phase!=Stopping) cancel(); });
}
CenteringController::~CenteringController() {
    if (busy()) cancel();
    m_devices->m_centeringOwned=false;
    m_solver->m_centeringOwned=false;
}
bool CenteringController::startForSequence(double ra,double dec,const QString &name,double seconds,double tolerance,int maxCorrections) {
    if (busy()) return false;
    m_sequenceRun=true;
    if (start(ra,dec,name,seconds,0,tolerance,maxCorrections)) return true;
    m_sequenceRun=false;
    return false;
}
bool CenteringController::start(double ra,double dec,const QString &name,double seconds,double fov,double tolerance,int maxCorrections) {
    if (busy()) return false;
    const auto &camera=m_devices->m_slots[DeviceManager::MainCamera];
    const auto &mount=m_devices->m_slots[DeviceManager::Mount];
    const auto &wheel=m_devices->m_slots[DeviceManager::FilterWheel];
    const auto &focuser=m_devices->m_slots[DeviceManager::Focuser];
    if (!CenteringMath::valid(ra,dec) || name.trimmed().isEmpty() || !std::isfinite(seconds) || seconds<=0 || seconds>3600
        || !std::isfinite(fov) || fov<0 || fov>180 || !std::isfinite(tolerance) || tolerance<0.1 || tolerance>10 || maxCorrections<1 || maxCorrections>3) {
        emit notice(QStringLiteral("目标或居中参数无效")); return false;
    }
    if (!camera.connected || !mount.connected || camera.busy || mount.busy || wheel.busy || focuser.busy
        || m_solver->busy() || (m_devices->m_sequenceOwned && !m_sequenceRun) || m_devices->m_centeringOwned || m_devices->m_autofocusOwned
        || m_devices->m_mountGotoActive || m_devices->m_mountGuiding || m_devices->m_manualMoving
        || m_devices->m_unsettledStills[DeviceManager::MainCamera] || m_devices->m_solveCapturePending
        || camera.metrics.value("previewing").toBool() || mount.metrics.value("slewing").toBool()
        || wheel.metrics.value("moving").toBool() || wheel.metrics.value("calibrating").toBool() || focuser.metrics.value("moving").toBool()
        || std::any_of(m_devices->m_commandsPending.begin(),m_devices->m_commandsPending.end(),[](int n){return n>0;})
        || !camera.metrics.value("exposureUs").toUInt() || !QFileInfo(m_solver->executable()).isFile()) {
        emit notice(QStringLiteral("请连接主相机和赤道仪、配置 ASTAP，并等待拍摄/序列/导星/设备运动结束")); return false;
    }
    bool rok=false,dok=false;
    const auto r=mount.metrics.value("skyRa").toDouble(&rok), d=mount.metrics.value("skyDec").toDouble(&dok);
    if (!rok || !dok || !CenteringMath::valid(r,d)) { emit notice(QStringLiteral("赤道仪 J2000 坐标回读无效")); return false; }
    m_ra=ra; m_dec=dec; m_name=name; m_seconds=seconds; m_fov=fov; m_tolerance=tolerance; m_limit=maxCorrections;
    m_corrections=0; m_separation=qQNaN(); m_succeeded=false;
    m_devices->m_centeringOwned=true; m_solver->m_centeringOwned=true;
    m_timer.start(); move(ra,dec);
    return busy();
}
void CenteringController::move(double ra,double dec) {
    m_phase=Slewing; m_elapsed.restart(); m_status=QStringLiteral("正在指向 · 校正 %1/%2").arg(m_corrections).arg(m_limit);
    QScopedValueRollback<bool> dispatch(m_devices->m_centeringDispatch,true);
    QScopedValueRollback<bool> sequenceDispatch(m_devices->m_sequenceDispatch,m_sequenceRun);
    if (!m_devices->slewMountToJ2000(ra,dec)) stop(m_devices->lastError());
    emit changed();
}
void CenteringController::cancel() { if (busy()) stop(QStringLiteral("居中已停止（未验证成功）")); }
void CenteringController::stop(const QString &reason) {
    m_phase=Stopping; m_stopReason=reason; m_status=QStringLiteral("正在停止居中…");
    if (m_solver->busy()) m_solver->cancel();
    if (m_devices->m_mountGotoActive || m_devices->m_slots[DeviceManager::Mount].metrics.value("slewing").toBool()) m_devices->mountAbortSlew();
    emit changed();
}
void CenteringController::finish(bool success,const QString &message) {
    m_phase=Idle; m_timer.stop(); m_succeeded=success; m_status=message;
    m_devices->m_centeringOwned=false; m_solver->m_centeringOwned=false;
    m_sequenceRun=false;
    emit changed(); emit notice(message);
}
void CenteringController::tick() {
    if (!busy()) return;
    const auto &mount=m_devices->m_slots[DeviceManager::Mount];
    const auto &camera=m_devices->m_slots[DeviceManager::MainCamera];
    const auto metrics=mount.metrics;
    const auto state=metrics.value("gotoState").toString();
    if (m_phase==Stopping) {
        if (!m_solver->busy() && !m_devices->m_mountGotoActive && !m_devices->m_solveCapturePending
            && (!mount.connected || (metrics.value("slewing").isValid() && !metrics.value("slewing").toBool()))
            && m_devices->m_commandsPending[DeviceManager::MainCamera]==0) finish(false,m_stopReason);
        else if (state=="StopFailed") { m_status=QStringLiteral("赤道仪停止失败，请重试停止或检查驱动"); emit changed(); }
        return;
    }
    if (!mount.connected || !camera.connected) { stop(QStringLiteral("设备断开，居中未完成")); return; }
    if (m_devices->m_mountGuiding || m_devices->m_manualMoving) { stop(QStringLiteral("检测到其他运动，居中已中止")); return; }
    if (m_phase==Slewing) {
        if (state=="Failed" || state=="Stopped" || state=="StopFailed") { stop(QStringLiteral("赤道仪指向未完成：%1").arg(m_devices->lastError())); return; }
        if (m_elapsed.elapsed()>210000) { stop(QStringLiteral("指向超时，居中未完成")); return; }
        if (state=="Arrived" && !m_devices->m_mountGotoActive) { m_phase=Settling; m_elapsed.restart(); m_status=QStringLiteral("等待赤道仪稳定…"); emit changed(); }
        return;
    }
    if (!metrics.value("slewing").isValid() || !metrics.value("atPark").isValid()
        || metrics.value("slewing").toBool() || metrics.value("atPark").toBool() || !metrics.value("tracking").toBool()) {
        stop(QStringLiteral("赤道仪未保持静止跟踪，居中中止")); return;
    }
    if (m_phase==Completing) {
        if (!m_devices->m_commandsPending[DeviceManager::MainCamera]) finish(true,QStringLiteral("居中成功 · 偏差 %1′ · 校正 %2 次").arg(m_separation,0,'f',2).arg(m_corrections));
        return;
    }
    if (m_phase==Settling) {
        if (m_elapsed.elapsed()<1000) return;
        bool rok=false,dok=false;
        m_reportedRa=metrics.value("skyRa").toDouble(&rok); m_reportedDec=metrics.value("skyDec").toDouble(&dok);
        if (!rok || !dok || !CenteringMath::valid(m_reportedRa,m_reportedDec)) { stop(QStringLiteral("赤道仪坐标回读无效")); return; }
        m_phase=Solving; m_elapsed.restart();
        QScopedValueRollback<bool> dispatch(m_devices->m_centeringDispatch,true);
        QScopedValueRollback<bool> sequenceDispatch(m_devices->m_sequenceDispatch,m_sequenceRun);
        QScopedValueRollback<bool> solveDispatch(m_solver->m_centeringDispatch,true);
        if (!m_solver->captureAndSolve(m_seconds,m_fov)) stop(QStringLiteral("无法开始居中验证拍摄，请查看设备通知"));
        return;
    }
    if (m_phase!=Solving) return;
    if (m_solver->busy()) {
        const auto status=QStringLiteral("验证 %1 · %2").arg(m_corrections+1).arg(m_solver->status());
        if (m_status!=status) { m_status=status; emit changed(); }
        if (m_elapsed.elapsed()>(m_seconds+150)*1000) stop(QStringLiteral("居中拍摄解析超时"));
        return;
    }
    const auto result=m_solver->result();
    bool rok=false,dok=false;
    const double ra=result.value("raHours").toDouble(&rok),dec=result.value("decDeg").toDouble(&dok);
    if (!rok || !dok || !CenteringMath::valid(ra,dec) || result.value("corners").toList().size()!=4) { stop(QStringLiteral("解析失败或已取消，未执行后续校正")); return; }
    m_separation=CenteringMath::separation(m_ra,m_dec,ra,dec); emit changed();
    if (m_separation<=m_tolerance) { m_phase=Completing; return; }
    if (m_corrections>=m_limit) { stop(QStringLiteral("达到 %1 次校正上限，偏差仍为 %2′，未居中").arg(m_limit).arg(m_separation,0,'f',2)); return; }
    double correctedRa,correctedDec;
    if (!CenteringMath::correct(m_ra,m_dec,m_reportedRa,m_reportedDec,ra,dec,correctedRa,correctedDec)) {
        stop(QStringLiteral("测得指向偏差超过 5° 或坐标异常，请检查站点、时间和目标")); return;
    }
    ++m_corrections; move(correctedRa,correctedDec);
}
