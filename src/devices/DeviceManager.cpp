#include "DeviceManager.h"
#include "CameraBackend.h"
#include <QSettings>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QEvent>
#include "imaging/StillSaveQueue.h"
#include <QThread>
#include "FilterWheelBackend.h"
#include "ToupcamCamera.h"
#include "ToupcamFilterWheel.h"
#include "ToupcamFocuser.h"
#include "MountBackend.h"
#include "AscomMount.h"
#include "MountCoordinates.h"
#include "imaging/PreviewHistogram.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <cmath>

namespace {
// Fixed 1..8 store: entries survive wheel slot-count switches (7 -> 5 -> 7 keeps data).
constexpr int kMaxWheelSlots = 8;
QVariantMap defaultChannel(int slot)
{
    return {{"slot", slot}, {"suffix", QString{}}, {"gain", 100}, {"exposure", 3.0}};
}

// Suffixes feed file names later, so strip everything a filesystem rejects.
QString sanitizedSuffix(QString value)
{
    value = value.trimmed().left(32);
    static const QString forbidden = QStringLiteral("\\/:*?\"<>|");
    for (const QChar ch : forbidden) value.remove(ch);
    for (const QChar ch : value) {
        if (ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format) {
            value.remove(ch);
        }
    }
    return value.trimmed();
}
}

DeviceManager::DeviceManager(QObject *parent)
    : DeviceManager(new ToupcamCamera, new ToupcamFilterWheel, parent)
{
    // 单帧保存目录跨会话记忆（空串 = 默认位置）。
    m_stillSaveDirectory = QSettings().value(QStringLiteral("stillSaveDirectory"), QString()).toString();
    m_autoSaveStills = QSettings().value(QStringLiteral("autoSaveStills"), true).toBool();
    // 单帧保存目录跨会话记忆（空串 = 默认位置）。
    auto *guide = new ToupcamCamera;
    guide->setGuideMode(true);
    m_guideCamera = guide;
    setupGuideCamera();
    auto *focuser = new ToupcamFocuser;
    m_focuser = focuser;
    setupFocuser();
#ifdef Q_OS_WIN
    m_mount = new AscomMount;
    setupMount();
#endif
}

DeviceManager::DeviceManager(CameraBackend *backend, QObject *parent)
    : DeviceManager(backend, nullptr, parent)
{
}

DeviceManager::DeviceManager(CameraBackend *camera, FilterWheelBackend *wheel, CameraBackend *guide,
                             MountBackend *mount, QObject *parent, FocuserBackend *focuser)
    : DeviceManager(camera, wheel, focuser, parent)
{
    m_guideCamera = guide;
    if (m_guideCamera) setupGuideCamera();
    m_mount = mount;
    if (m_mount) setupMount();
}

DeviceManager::DeviceManager(CameraBackend *camera, FilterWheelBackend *wheel, QObject *parent)
    : DeviceManager(camera, wheel, nullptr, parent)
{
}

DeviceManager::DeviceManager(CameraBackend *camera, FilterWheelBackend *wheel, FocuserBackend *focuser,
                             QObject *parent)
    : QObject(parent), m_camera(camera), m_wheel(wheel), m_focuser(focuser)
{
    const QStringList titles{QStringLiteral("赤道仪"), QStringLiteral("主相机"), QStringLiteral("导星相机"),
                             QStringLiteral("滤镜轮"), QStringLiteral("电调焦")};
    for (int role = 0; role < int(m_slots.size()); ++role) m_slots[role].title = titles[role];
    loadFilterWheelChannels();
    connect(this, &DeviceManager::devicesChanged, this, &DeviceManager::filterFocusSettingsChanged);
    // 专职保存线程（NINA ImageSaveController 裁剪版）：入队即返回，带重试。
    m_saveQueue = new StillSaveQueue;
    m_saveThread = new QThread(this);
    m_saveQueue->moveToThread(m_saveThread);
    connect(m_saveThread, &QThread::finished, m_saveQueue, &QObject::deleteLater);
    connect(m_saveQueue, &StillSaveQueue::saved, this, [this](int role, const QString &path, bool ok) {
        --m_unsettledStills[role];
        emit stillCaptured(role, path, ok);
    });
    m_saveThread->start();
    connect(m_saveQueue, &StillSaveQueue::saveError, this, &DeviceManager::setLastError);
    Q_ASSERT(camera && !camera->parent());
    m_camera->moveToThread(&m_cameraThread);
    connect(&m_cameraThread, &QThread::finished, m_camera, &QObject::deleteLater);
    connect(m_camera, &CameraBackend::scanFinished, this, [this](const QVariantList &cameras, const QString &error) {
        m_cameras = cameras;
        auto &slot = m_slots[MainCamera];
        if (!slot.connected && !slot.busy) {
            bool present = false;
            for (const auto &cameraEntry : cameras) if (cameraEntry.toMap().value("id").toString() == slot.selectedId) present = true;
            if (!present) { slot.selectedId.clear(); slot.selectedName.clear(); }
        }
        emit candidatesChanged();
        finishScan(error);
    });
    connect(m_camera, &CameraBackend::connectionFinished, this, [this](bool connected, const QVariantMap &information, const QString &error) {
        auto &slot = m_slots[MainCamera];
        slot.busy = false;
        slot.connected = connected;
        slot.metrics = connected ? information : QVariantMap{};
        slot.error = error;
        if (!connected) clearCameraTelemetry();
        if (!connected && m_autofocusCapturePending) {
            m_stillPending[MainCamera] = false;
            --m_unsettledStills[MainCamera];
            finishAutofocusCapture();
            emit autofocusCaptureFailed(QStringLiteral("主相机已断开"));
        }
        setLastError(error);
        emit devicesChanged();
    });
    connect(m_camera, &CameraBackend::connectionLost, this, [this](const QString &error) {
        auto &slot = m_slots[MainCamera];
        slot.connected = false;
        slot.busy = false;
        slot.metrics.clear();
        slot.error = error;
        clearCameraTelemetry();
        setLastError(error);
        if (m_stillPending[MainCamera]) {
            m_stillPending[MainCamera] = false;
            --m_unsettledStills[MainCamera];
            if (m_autofocusCapturePending) { finishAutofocusCapture(); emit autofocusCaptureFailed(error); }
            else if (m_solveCapturePending) { finishSolveCapture(); emit solveCaptureFailed(error); }
            else emit stillCaptured(MainCamera, {}, false);
        }
        emit devicesChanged();
    });
    connect(m_camera, &CameraBackend::stateChanged, this, [this](const QVariantMap &state) {
        auto &slot = m_slots[MainCamera];
        bool telemetryOnly = true;
        for (auto it = state.begin(); it != state.end(); ++it) {
            slot.metrics.insert(it.key(), it.value());
            // High-frequency readouts bypass devicesChanged so live edits in the
            // settings pages are not wiped by the two-second polling cadence.
            if (it.key() == QLatin1String("currentTemp") || it.key() == QLatin1String("tecPower"))
                m_cameraTelemetry.insert(it.key(), it.value());
            else
                telemetryOnly = false;
        }
        if (telemetryOnly)
            emit cameraTelemetryChanged();
        else {
            emit cameraTelemetryChanged();
            emit devicesChanged();
        }
    });
    connect(m_camera, &CameraBackend::commandFailed, this, [this](const QString &message) {
        setLastError(message);
        if (m_autofocusOwned && !m_autofocusCapturePending) emit autofocusCaptureFailed(message);
    });
    connect(m_camera, &CameraBackend::frameReady, this, [this](const QImage &frame) {
        m_previewFrame = frame;
        ++m_previewCounter;
        m_previewHistogram = ::previewHistogram(frame);
        emit previewFrameChanged();
    });
    connect(m_camera, &CameraBackend::stillFailed, this, [this](const QString &error) {
        if (!m_stillPending[MainCamera]) return;
        m_stillPending[MainCamera] = false;
        --m_unsettledStills[MainCamera];
        if (m_autofocusCapturePending) { finishAutofocusCapture(); emit autofocusCaptureFailed(error); return; }
        if (m_solveCapturePending) { finishSolveCapture(); emit solveCaptureFailed(error); return; }
        setLastError(error);
        emit stillCaptured(MainCamera, {}, false);
    });
    connect(m_camera, &CameraBackend::rawStillReady, this, [this](QVector<quint16> data, int width, int height, QVariantMap metadata) {
        enqueueStill(MainCamera, std::move(data), width, height, metadata);
    });
    m_cameraThread.start();
    if (!m_wheel) return;
    m_wheel->moveToThread(&m_wheelThread);
    connect(&m_wheelThread, &QThread::finished, m_wheel, &QObject::deleteLater);
    connect(m_wheel, &FilterWheelBackend::scanFinished, this, [this](const QVariantList &wheels, const QString &error) {
        m_wheels = wheels;
        auto &slot = m_slots[FilterWheel];
        if (!slot.connected && !slot.busy) {
            bool present = false;
            for (const auto &wheelEntry : wheels) if (wheelEntry.toMap().value("id").toString() == slot.selectedId) present = true;
            if (!present) { slot.selectedId.clear(); slot.selectedName.clear(); }
        }
        emit candidatesChanged();
        finishScan(error);
    });
    connect(m_wheel, &FilterWheelBackend::connectionFinished, this, [this](bool connected, const QVariantMap &information, const QString &error) {
        invalidateFilterFocusBaseline();
        auto &slot = m_slots[FilterWheel];
        slot.busy = false;
        slot.connected = connected;
        slot.metrics = connected ? information : QVariantMap{};
        if (connected) {
            // Per-device channel store: switching wheels switches suffixes/gain/exposure.
            const QString serial = information.value(QStringLiteral("serial")).toString();
            if (serial != m_wheelSerial) {
                m_wheelSerial = serial;
                loadFilterWheelChannels();
                emit filterWheelChannelsChanged();
            }
            slot.metrics.insert(QStringLiteral("bidirectional"), m_wheelBidirectional);
        }
        slot.error = error;
        setLastError(error);
        emit devicesChanged();
    });
    connect(m_wheel, &FilterWheelBackend::connectionLost, this, [this](const QString &error) {
        invalidateFilterFocusBaseline();
        auto &slot = m_slots[FilterWheel];
        slot.connected = false;
        slot.busy = false;
        slot.metrics.clear();
        slot.error = error;
        setLastError(error);
        emit devicesChanged();
    });
    connect(m_wheel, &FilterWheelBackend::stateChanged, this, [this](const QVariantMap &state) {
        auto &slot = m_slots[FilterWheel];
        for (auto it = state.begin(); it != state.end(); ++it) slot.metrics.insert(it.key(), it.value());
        slot.metrics.insert(QStringLiteral("bidirectional"), m_wheelBidirectional);
        if (m_filterFocusBaselineValid) {
            const int position = slot.metrics.value("position", -1).toInt();
            const bool moving = slot.metrics.value("moving").toBool() || slot.metrics.value("calibrating").toBool();
            if (m_filterFocusWheelTarget >= 0) {
                if (!moving && position == m_filterFocusWheelTarget) {
                    m_filterFocusFilter = position; m_filterFocusWheelTarget = -1;
                }
            } else if (moving || position != m_filterFocusFilter) invalidateFilterFocusBaseline();
        }
        emit devicesChanged();
    });
    m_wheelThread.start();
    if (m_guideCamera) setupGuideCamera();
    if (m_focuser) setupFocuser();
}

// Guide camera mirrors the main-camera plumbing on its own worker thread;
// the same ToupcamCamera class in guide mode delivers raw Grey16 frames.
void DeviceManager::setupGuideCamera()
{
    Q_ASSERT(m_guideCamera && !m_guideCamera->parent());
    m_guideCamera->moveToThread(&m_guideThread);
    connect(&m_guideThread, &QThread::finished, m_guideCamera, &QObject::deleteLater);
    connect(m_guideCamera, &CameraBackend::scanFinished, this, [this](const QVariantList &cameras, const QString &error) {
        m_guideCameras = cameras;
        auto &slot = m_slots[GuideCamera];
        if (!slot.connected && !slot.busy) {
            bool present = false;
            for (const auto &cameraEntry : cameras) if (cameraEntry.toMap().value("id").toString() == slot.selectedId) present = true;
            if (!present) { slot.selectedId.clear(); slot.selectedName.clear(); }
        }
        emit candidatesChanged();
        finishScan(error);
    });
    connect(m_guideCamera, &CameraBackend::connectionFinished, this, [this](bool connected, const QVariantMap &information, const QString &error) {
        auto &slot = m_slots[GuideCamera];
        slot.busy = false;
        slot.connected = connected;
        slot.metrics = connected ? information : QVariantMap{};
        slot.error = error;
        setLastError(error);
        emit devicesChanged();
    });
    connect(m_guideCamera, &CameraBackend::connectionLost, this, [this](const QString &error) {
        auto &slot = m_slots[GuideCamera];
        slot.connected = false;
        slot.busy = false;
        slot.metrics.clear();
        slot.error = error;
        setLastError(error);
        emit devicesChanged();
    });
    connect(m_guideCamera, &CameraBackend::frameReady, this, [this](const QImage &frame) {
        m_guidePreviewFrame = frame;
        ++m_guidePreviewCounter;
        emit guidePreviewFrameChanged();
    });
    connect(m_guideCamera, &CameraBackend::rawFrameReady, this, &DeviceManager::guideFrameReady);
    connect(m_guideCamera, &CameraBackend::commandFailed, this, &DeviceManager::setLastError);
    connect(m_guideCamera, &CameraBackend::stateChanged, this, [this](const QVariantMap &state) {
        auto &slot = m_slots[GuideCamera];
        for (auto it = state.begin(); it != state.end(); ++it) slot.metrics.insert(it.key(), it.value());
        emit devicesChanged();
    });
    connect(m_guideCamera, &CameraBackend::stillFailed, this, [this](const QString &error) {
        if (!m_stillPending[GuideCamera]) return;
        m_stillPending[GuideCamera] = false;
        --m_unsettledStills[GuideCamera];
        setLastError(error);
        emit stillCaptured(GuideCamera, {}, false);
    });
    connect(m_guideCamera, &CameraBackend::rawStillReady, this, [this](QVector<quint16> data, int width, int height, QVariantMap metadata) {
        enqueueStill(GuideCamera, std::move(data), width, height, metadata);
    });
    m_guideThread.start();
}

// Focuser (role 4) mirrors the wheel plumbing on its own worker thread.
void DeviceManager::setupFocuser()
{
    Q_ASSERT(m_focuser && !m_focuser->parent());
    m_focuser->moveToThread(&m_focuserThread);
    connect(&m_focuserThread, &QThread::finished, m_focuser, &QObject::deleteLater);
    connect(m_focuser, &FocuserBackend::scanFinished, this, [this](const QVariantList &focusers, const QString &error) {
        m_focusers = focusers;
        auto &slot = m_slots[Focuser];
        if (!slot.connected && !slot.busy) {
            bool present = false;
            for (const auto &entry : focusers) if (entry.toMap().value("id").toString() == slot.selectedId) present = true;
            if (!present) { slot.selectedId.clear(); slot.selectedName.clear(); }
        }
        emit candidatesChanged();
        finishScan(error);
    });
    connect(m_focuser, &FocuserBackend::connectionFinished, this, [this](bool connected, const QVariantMap &information, const QString &error) {
        invalidateFilterFocusBaseline();
        auto &slot = m_slots[Focuser];
        slot.busy = false;
        slot.connected = connected;
        slot.metrics = connected ? information : QVariantMap{};
        slot.error = error;
        setLastError(error);
        emit devicesChanged();
    });
    connect(m_focuser, &FocuserBackend::connectionLost, this, [this](const QString &error) {
        invalidateFilterFocusBaseline();
        auto &slot = m_slots[Focuser];
        slot.connected = false;
        slot.busy = false;
        slot.metrics.clear();
        slot.error = error;
        setLastError(error);
        emit devicesChanged();
    });
    connect(m_focuser, &FocuserBackend::stateChanged, this, [this](const QVariantMap &state) {
        auto &slot = m_slots[Focuser];
        const QString message = state.value(QStringLiteral("error")).toString();
        if (!message.isEmpty()) { setLastError(message); emit focuserCommandFailed(message); }
        for (auto it = state.begin(); it != state.end(); ++it)
            if (it.key() != QLatin1String("error")) slot.metrics.insert(it.key(), it.value());
        if (m_filterFocusBaselineValid && (slot.metrics.value("maxStep").toInt() != m_filterFocusMaxStep
            || slot.metrics.value("reversed").toBool() != m_filterFocusReversed
            || (!m_filterFocusMoving && !m_autofocusOwned
                && (slot.metrics.value("moving").toBool() || slot.metrics.value("position", -1).toInt() != m_filterFocusPosition))))
            invalidateFilterFocusBaseline();
        emit devicesChanged();
    });
    m_focuserThread.start();
}

// Mount (role 0): ASCOM telescope on its own worker thread.
void DeviceManager::setupMount()
{
    Q_ASSERT(m_mount && !m_mount->parent());
    m_mount->moveToThread(&m_mountThread);
    connect(&m_mountThread, &QThread::finished, m_mount, &QObject::deleteLater);
    connect(m_mount, &MountBackend::scanFinished, this, [this](const QVariantList &mounts, const QString &error) {
        m_mounts = mounts;
        auto &slot = m_slots[Mount];
        if (!slot.connected && !slot.busy) {
            bool present = false;
            for (const auto &entry : mounts) if (entry.toMap().value(QStringLiteral("id")).toString() == slot.selectedId) present = true;
            if (!present) { slot.selectedId.clear(); slot.selectedName.clear(); }
        }
        emit candidatesChanged();
        finishScan(error);
    });
    connect(m_mount, &MountBackend::connectionFinished, this, [this](bool connected, const QVariantMap &information, const QString &error) {
        if (!connected) { m_manualMoving = false; m_mountGotoActive = false; }
        auto &slot = m_slots[Mount];
        slot.busy = false;
        slot.connected = connected;
        slot.metrics = connected ? information : QVariantMap{};
        slot.error = error;
        setLastError(error);
        emit devicesChanged();
    });
    connect(m_mount, &MountBackend::connectionLost, this, [this](const QString &error) {
        m_manualMoving = false;
        m_mountGotoActive = false;
        auto &slot = m_slots[Mount];
        slot.connected = false;
        slot.busy = false;
        slot.metrics.clear();
        slot.error = error;
        setLastError(error);
        emit devicesChanged();
    });
    connect(m_mount, &MountBackend::stateChanged, this, [this](const QVariantMap &state) {
        auto &slot = m_slots[Mount];
        if (!slot.connected) return;
        if (state.contains("gotoState")) {
            const auto status = state.value("gotoState").toString();
            m_mountGotoActive = status == "Slewing" || status == "StopFailed";
            if (!state.value("gotoError").toString().isEmpty()) setLastError(state.value("gotoError").toString());
        }
        if (state.contains("manualMoving")) m_manualMoving = state.value("manualMoving").toBool();
        if (!state.value("manualError").toString().isEmpty()) setLastError(state.value("manualError").toString());
        for (auto it = state.begin(); it != state.end(); ++it) slot.metrics.insert(it.key(), it.value());
        emit devicesChanged();
    });
    connect(m_mount, &MountBackend::setupFinished, this, [this](const QString &error) {
        auto &slot = m_slots[Mount];
        slot.busy = false;
        if (!error.isEmpty()) setLastError(error);
        emit devicesChanged();
    });
    m_mountThread.start();
}

DeviceManager::~DeviceManager()
{
    m_shuttingDown = true;
    // Finish queued work, then release the handles on their owning threads before exit.
    QMetaObject::invokeMethod(m_camera, [this] { m_camera->close(); }, Qt::BlockingQueuedConnection);
    m_cameraThread.quit();
    m_cameraThread.wait();
    if (m_wheel) {
        QMetaObject::invokeMethod(m_wheel, [this] { m_wheel->close(); }, Qt::BlockingQueuedConnection);
        m_wheelThread.quit();
        m_wheelThread.wait();
    }
    if (m_guideCamera) {
        QMetaObject::invokeMethod(m_guideCamera, [this] { m_guideCamera->close(); }, Qt::BlockingQueuedConnection);
        m_guideThread.quit();
        m_guideThread.wait();
    }
    if (m_focuser) {
        QMetaObject::invokeMethod(m_focuser, [this] { m_focuser->close(); }, Qt::BlockingQueuedConnection);
        m_focuserThread.quit();
        m_focuserThread.wait();
    }
    // Deliver completed camera frames before closing save admission, then drain writes.
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    m_saveQueue->finish();
    m_saveThread->quit();
    m_saveThread->wait();
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    if (!m_mount) return;
    QMetaObject::invokeMethod(m_mount, [this] { m_mount->close(); }, Qt::BlockingQueuedConnection);
    m_mountThread.quit();
    m_mountThread.wait();
}

void DeviceManager::finishScan(const QString &error)
{
    // Watchdog scans may arrive outside a refresh cycle; the list update still notifies.
    if (m_scanning && --m_pendingScans > 0) {
        if (!error.isEmpty()) setLastError(error);
        return;
    }
    m_scanning = false;
    if (!error.isEmpty()) setLastError(error);
    emit devicesChanged();
}

namespace {
// 默认单帧保存位置：图片/StellaMora（与兄弟版本一致）。
QString defaultStillDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
           + QStringLiteral("/StellaMora");
}
} // namespace

QString DeviceManager::stillSaveDirectory() const
{
    return m_stillSaveDirectory.isEmpty() ? defaultStillDirectory() : m_stillSaveDirectory;
}

void DeviceManager::setStillSaveDirectory(const QString &directory)
{
    if (directory == m_stillSaveDirectory)
        return;
    m_stillSaveDirectory = directory;
    QSettings().setValue(QStringLiteral("stillSaveDirectory"), directory);
    emit stillSaveDirectoryChanged();
}

bool DeviceManager::setCameraResolution(int role, int index)
{
    if (role == MainCamera && sequenceActionBlocked()) return false;
    if (role != MainCamera && role != GuideCamera) {
        setLastError(QStringLiteral("无效设备角色"));
        return false;
    }
    auto &slot = m_slots[role];
    if (!slot.connected) {
        setLastError(QStringLiteral("相机未连接"));
        return false;
    }
    CameraBackend *backend = role == MainCamera ? m_camera : m_guideCamera;
    QMetaObject::invokeMethod(backend, [backend, index] { backend->setResolution(index); },
                              Qt::QueuedConnection);
    return true;
}

bool DeviceManager::captureStill(int role)
{
    StillRequest request;
    request.role = role;
    request.directory = stillSaveDirectory();
    request.imageType = m_stillImageType;
    request.targetName = m_stillTargetName;
    return beginStill(role, std::move(request));
}

bool DeviceManager::captureForSolve(double seconds)
{
    if (cameraActionBlocked()) return false;
    const auto &camera = m_slots[MainCamera];
    const auto &wheel = m_slots[FilterWheel];
    if (!std::isfinite(seconds) || seconds <= 0 || seconds > 3600) {
        setLastError(QStringLiteral("解析曝光时间需在 0～3600 秒之间")); return false;
    }
    if (camera.busy || camera.metrics.value("previewing").toBool() || m_unsettledStills[MainCamera] > 0
        || m_commandsPending[MainCamera] > 0 || m_commandsPending[FilterWheel] > 0 || wheel.busy
        || wheel.metrics.value("moving").toBool() || wheel.metrics.value("calibrating").toBool()
        || m_manualMoving || m_slots[Mount].metrics.value("slewing").toBool()) {
        setLastError(QStringLiteral("设备正在拍摄或移动，请停止循环拍摄并等待设备空闲后解析")); return false;
    }
    m_beforeSolveExposure = camera.metrics.value("exposureUs").toUInt();
    if (!m_beforeSolveExposure) { setLastError(QStringLiteral("尚未读取相机曝光参数，请稍后重试")); return false; }
    StillRequest request; request.role = MainCamera; request.imageType = "LIGHT";
    if (!beginStill(MainCamera,std::move(request),seconds,-1)) return false;
    m_solveCapturePending = true;
    return true;
}

void DeviceManager::cancelSolveCapture()
{
    if (!m_solveCapturePending) return;
    QMetaObject::invokeMethod(m_camera,[this] { m_camera->stopPreview(); },Qt::QueuedConnection);
}

void DeviceManager::finishSolveCapture()
{
    m_solveCapturePending = false;
    if (!m_shuttingDown && m_slots[MainCamera].connected) {
        const auto exposure = m_beforeSolveExposure;
        postDeviceCommand(m_camera,MainCamera,[this,exposure] { m_camera->setExposure(exposure); });
    }
}

bool DeviceManager::captureForAutofocus(double seconds)
{
    if (cameraActionBlocked()) return false;
    const auto &camera = m_slots[MainCamera];
    if (!m_autofocusOwned || !std::isfinite(seconds) || seconds < 0.001 || seconds > 3600
        || camera.busy || camera.metrics.value("previewing").toBool()
        || m_unsettledStills[MainCamera] || m_commandsPending[MainCamera]) {
        setLastError(QStringLiteral("自动对焦曝光参数无效或相机正忙")); return false;
    }
    m_beforeAutofocusExposure = camera.metrics.value("exposureUs").toUInt();
    if (!m_beforeAutofocusExposure) {
        setLastError(QStringLiteral("尚未读取相机曝光参数，请稍后重试")); return false;
    }
    StillRequest request; request.role = MainCamera; request.imageType = "LIGHT";
    if (!beginStill(MainCamera, std::move(request), seconds, -1)) return false;
    m_autofocusCapturePending = true;
    return true;
}

void DeviceManager::cancelAutofocusCapture()
{
    if (!m_autofocusCapturePending) return;
    postDeviceCommand(m_camera, MainCamera, [this] { m_camera->stopPreview(); });
}

void DeviceManager::finishAutofocusCapture()
{
    m_autofocusCapturePending = false;
    if (!m_shuttingDown && m_slots[MainCamera].connected) {
        const auto exposure = m_beforeAutofocusExposure;
        postDeviceCommand(m_camera, MainCamera, [this, exposure] { m_camera->setExposure(exposure); });
    }
}

bool DeviceManager::beginStill(int role, StillRequest request, double seconds, int gain)
{
    if (role == MainCamera && m_mountGotoActive) { setLastError(QStringLiteral("赤道仪正在指向，不能曝光")); return false; }
    if (role == MainCamera && sequenceActionBlocked()) return false;
    if (m_shuttingDown) return false;
    if (role != MainCamera && role != GuideCamera) {
        setLastError(QStringLiteral("无效设备角色"));
        return false;
    }
    auto &slot = m_slots[role];
    if (!slot.connected) {
        setLastError(QStringLiteral("相机未连接"));
        return false;
    }
    if (m_stillPending[role]) {
        setLastError(QStringLiteral("单帧曝光正在进行中"));
        return false;
    }
    if (!QStringList{"LIGHT", "DARK", "FLAT", "BIAS"}.contains(request.imageType)) {
        setLastError(QStringLiteral("无效图像类型"));
        return false;
    }
    m_stillRequests[role] = std::move(request);
    m_stillPending[role] = true;
    ++m_unsettledStills[role];
    CameraBackend *backend = role == MainCamera ? m_camera : m_guideCamera;
    QMetaObject::invokeMethod(backend, [backend, seconds, gain] {
        // Parameters and exposure are one worker operation. Never expose after
        // a rejected setting, or let another queued command interleave here.
        QString error;
        if (seconds > 0) {
            const auto listener = QObject::connect(backend, &CameraBackend::commandFailed,
                backend, [&error](const QString &message) { error = message; }, Qt::DirectConnection);
            backend->setExposure(unsigned(qRound64(seconds * 1e6)));
            if (error.isEmpty() && gain >= 0) backend->setGain(unsigned(gain));
            QObject::disconnect(listener);
        }
        if (error.isEmpty()) backend->requestStill();
        else emit backend->stillFailed(error);
    },
                              Qt::QueuedConnection);
    return true;
}

void DeviceManager::enqueueStill(int role, QVector<quint16> &&data, int width, int height, const QVariantMap &metadata)
{
    if (!m_stillPending[role]) return;
    m_stillPending[role] = false;
    if (role == MainCamera && m_autofocusCapturePending) {
        --m_unsettledStills[role];
        finishAutofocusCapture();
        emit autofocusPhotoReady(std::move(data), width, height, metadata);
        return;
    }
    if (role == MainCamera && m_solveCapturePending) {
        --m_unsettledStills[role];
        finishSolveCapture();
        emit solvePhotoReady(data,width,height,metadata);
        return;
    }
    if (role == MainCamera) emit mainPhotoReady(data, width, height, metadata);
    emit stillFrameReceived(role);
    StillRequest request = std::move(m_stillRequests[role]);
    for (auto it = metadata.cbegin(); it != metadata.cend(); ++it)
        request.header.insert(it.key(), it.value().toString());
    request.capturedAt = QDateTime::fromString(metadata.value("DATE-OBS").toString(), Qt::ISODateWithMs);
    if (!request.targetName.isEmpty()) request.header.insert("OBJECT", request.targetName);
    request.data = std::move(data);
    request.width = width;
    request.height = height;
    if (!m_saveQueue->enqueue(std::move(request))) {
        --m_unsettledStills[role];
        setLastError(QStringLiteral("保存队列已满或已停止，请等待写盘完成"));
        emit stillCaptured(role, {}, false);
    }
}

void DeviceManager::setAutoSaveStills(bool enabled)
{
    if (enabled == m_autoSaveStills)
        return;
    m_autoSaveStills = enabled;
    QSettings().setValue(QStringLiteral("autoSaveStills"), enabled);
    emit autoSaveStillsChanged();
}

QVariantMap DeviceManager::cameraParameters(int role) const
{
    if (role != MainCamera && role != GuideCamera)
        return {};
    const QVariantMap &metrics = m_slots.at(size_t(role)).metrics;
    QVariantMap result;
    if (metrics.contains(QStringLiteral("exposureUs")))
        result.insert(QStringLiteral("exposureSeconds"), metrics.value(QStringLiteral("exposureUs")).toDouble() / 1e6);
    if (metrics.contains(QStringLiteral("exposureMinUs")))
        result.insert(QStringLiteral("exposureMinSeconds"), metrics.value(QStringLiteral("exposureMinUs")).toDouble() / 1e6);
    if (metrics.contains(QStringLiteral("exposureMaxUs")))
        result.insert(QStringLiteral("exposureMaxSeconds"), metrics.value(QStringLiteral("exposureMaxUs")).toDouble() / 1e6);
    for (const char *key : {"gain", "gainMin", "gainMax", "binning", "resolutions", "resolutionIndex",
                            "width", "height", "tecEnabled", "targetTemp", "currentTemp"})
        if (metrics.contains(QLatin1String(key)))
            result.insert(QLatin1String(key), metrics.value(QLatin1String(key)));
    return result;
}

void DeviceManager::refreshDevices()
{
    if (m_scanning || m_slots[MainCamera].busy || m_slots[FilterWheel].busy) {
        return;
    }
    m_pendingScans = 0;
    if (m_camera) ++m_pendingScans;
    if (m_wheel) ++m_pendingScans;
    if (m_guideCamera) ++m_pendingScans;
    if (m_focuser) ++m_pendingScans;
    if (m_mount) ++m_pendingScans;
    if (m_pendingScans == 0) return;
    m_scanning = true;
    setLastError({});
    emit devicesChanged();
    QMetaObject::invokeMethod(m_camera, [this] { m_camera->scan(); }, Qt::QueuedConnection);
    if (m_wheel) QMetaObject::invokeMethod(m_wheel, [this] { m_wheel->scan(); }, Qt::QueuedConnection);
    if (m_guideCamera) QMetaObject::invokeMethod(m_guideCamera, [this] { m_guideCamera->scan(); }, Qt::QueuedConnection);
    if (m_focuser) QMetaObject::invokeMethod(m_focuser, [this] { m_focuser->scan(); }, Qt::QueuedConnection);
    if (m_mount) QMetaObject::invokeMethod(m_mount, [this] { m_mount->scan(); }, Qt::QueuedConnection);
}

QVariantList DeviceManager::devices() const
{
    QVariantList result;
    for (int role = 0; role < int(m_slots.size()); ++role) result.append(toMap(role, m_slots[role]));
    return result;
}

int DeviceManager::connectedCount() const
{
    int count = 0;
    for (const auto &slot : m_slots) if (slot.connected) ++count;
    return count;
}

QString DeviceManager::statusSummary() const { return QStringLiteral("设备 %1/5").arg(connectedCount()); }
QString DeviceManager::lastError() const { return m_lastError; }

QVariantMap DeviceManager::device(int role) const
{
    return validRole(role) ? toMap(role, m_slots[role]) : QVariantMap{};
}

QVariantList DeviceManager::candidatesForRole(int role) const
{
    if (role == MainCamera) return m_cameras;
    if (role == FilterWheel && m_wheel) return m_wheels;
    if (role == GuideCamera && m_guideCamera) return m_guideCameras;
    if (role == Focuser && m_focuser) return m_focusers;
    if (role == Mount && m_mount) return m_mounts;
    return QVariantList{};
}

bool DeviceManager::selectDevice(int role, const QString &id)
{
    if ((role == MainCamera || role == FilterWheel) && sequenceActionBlocked()) return false;
    if (!validRole(role)) { setLastError(QStringLiteral("无效设备角色")); return false; }
    const bool cameraRole = role == MainCamera || role == GuideCamera;
    if (role != MainCamera && role != GuideCamera && !(role == FilterWheel && m_wheel)
        && !(role == Focuser && m_focuser) && !(role == Mount && m_mount)) {
        setLastError(QStringLiteral("该设备驱动尚未接入"));
        return false;
    }
    auto &slot = m_slots[role];
    if (slot.busy || slot.connected || m_scanning) {
        setLastError(cameraRole ? QStringLiteral("请等待操作完成并断开相机后再选择设备")
                                : QStringLiteral("请等待操作完成并断开滤镜轮后再选择设备"));
        return false;
    }
    // A camera already opened by the other camera role cannot serve two
    // masters; refuse the duplicate selection outright.
    const int otherRole = role == MainCamera ? GuideCamera : MainCamera;
    if (cameraRole && m_slots[otherRole].connected
            && m_slots[otherRole].selectedId == id && !id.isEmpty()) {
        setLastError(QStringLiteral("该相机正被另一相机角色使用，请先断开"));
        return false;
    }
    const QVariantList &candidates = role == MainCamera ? m_cameras
                                   : role == GuideCamera ? m_guideCameras
                                   : role == Focuser ? m_focusers
                                   : role == Mount ? m_mounts : m_wheels;
    QString name;
    for (const auto &candidate : candidates) {
        const auto value = candidate.toMap();
        if (value.value("id").toString() == id) { name = value.value("name").toString(); break; }
    }
    if (!id.isEmpty() && name.isEmpty()) {
        setLastError(cameraRole ? QStringLiteral("所选相机不可用，请刷新设备列表")
                                : QStringLiteral("所选滤镜轮不可用，请刷新设备列表"));
        return false;
    }
    slot.selectedId = id;
    slot.selectedName = name;
    slot.error.clear();
    slot.metrics.clear();
    setLastError({});
    emit devicesChanged();
    return true;
}

bool DeviceManager::setConnected(int role, bool connected)
{
    const bool autofocusDisconnect = m_autofocusOwned && !connected && (role == MainCamera || role == Focuser);
    if (!autofocusDisconnect && (role == MainCamera || role == FilterWheel) && sequenceActionBlocked()) return false;
    if (connected && role == Focuser && autofocusActionBlocked()) return false;
    if (!validRole(role)) { setLastError(QStringLiteral("无效设备角色")); return false; }
    const bool cameraRole = role == MainCamera || role == GuideCamera;
    if (role != MainCamera && role != GuideCamera && !(role == FilterWheel && m_wheel)
        && !(role == Focuser && m_focuser) && !(role == Mount && m_mount)) {
        setLastError(QStringLiteral("该设备驱动尚未接入"));
        return false;
    }
    auto &slot = m_slots[role];
    if (slot.busy || m_scanning) { setLastError(QStringLiteral("设备操作进行中，请稍候")); return false; }
    if (connected && slot.selectedId.isEmpty()) {
        setLastError(role == FilterWheel ? QStringLiteral("请先选择滤镜轮")
                    : role == Focuser ? QStringLiteral("请先选择电调焦")
                    : QStringLiteral("请先选择相机"));
        return false;
    }
    if (connected && cameraRole) {
        const int otherRole = role == MainCamera ? GuideCamera : MainCamera;
        if (m_slots[otherRole].connected && m_slots[otherRole].selectedId == slot.selectedId) {
            setLastError(QStringLiteral("该相机正被另一相机角色使用，请先断开"));
            return false;
        }
    }
    if (slot.connected == connected) return true;
    if (role == FilterWheel || role == Focuser) invalidateFilterFocusBaseline();
    if (autofocusDisconnect) emit focuserStopRequested();
    slot.busy = true;
    slot.error.clear();
    setLastError({});
    const QString id = slot.selectedId;
    emit devicesChanged();
    if (role == MainCamera) {
        postDeviceCommand(m_camera, role, [this, connected, id] {
            if (connected) m_camera->open(id);
            else m_camera->close();
        });
    } else if (role == GuideCamera) {
        QMetaObject::invokeMethod(m_guideCamera, [this, connected, id] {
            if (connected) m_guideCamera->open(id);
            else m_guideCamera->close();
        }, Qt::QueuedConnection);
    } else if (role == Focuser) {
        postDeviceCommand(m_focuser, role, [this, connected, id] {
            if (connected) m_focuser->open(id);
            else m_focuser->close();
        });
    } else if (role == Mount && m_mount) {
        QMetaObject::invokeMethod(m_mount, [this, connected, id] {
            if (connected) m_mount->open(id);
            else m_mount->close();
        }, Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(m_wheel, [this, connected, id] {
            if (connected) m_wheel->open(id);
            else m_wheel->close();
        }, Qt::QueuedConnection);
    }
    // Accepted for asynchronous execution, not proof of a successful connection.
    return true;
}

bool DeviceManager::toggleConnected(int role)
{
    if (!validRole(role)) { setLastError(QStringLiteral("无效设备角色")); return false; }
    return setConnected(role, !m_slots[role].connected);
}

bool DeviceManager::sequenceActionBlocked()
{
    if (centeringActionBlocked()) return true;
    if (m_solveCapturePending) {
        setLastError(QStringLiteral("正在拍摄解析照片，请等待完成或取消解析")); return true;
    }
    if (!m_sequenceOwned || m_sequenceDispatch) return false;
    setLastError(QStringLiteral("序列正在使用主相机、滤镜轮和电调焦，请先停止序列"));
    return true;
}

bool DeviceManager::centeringActionBlocked()
{
    if (autofocusActionBlocked()) return true;
    if (!m_centeringOwned || m_centeringDispatch) return false;
    setLastError(QStringLiteral("正在指向并居中，请先停止居中"));
    return true;
}

bool DeviceManager::autofocusActionBlocked()
{
    if (!m_autofocusOwned || m_autofocusDispatch) return false;
    setLastError(QStringLiteral("自动对焦正在使用设备，请先停止自动对焦"));
    return true;
}

void DeviceManager::postDeviceCommand(QObject *backend, int role, std::function<void()> action)
{
    // Account for the command before its worker reports state. Older queued
    // telemetry must not let a sequence start in this handover window.
    ++m_commandsPending[role];
    QMetaObject::invokeMethod(backend, [this, role, action = std::move(action)] {
        action();
        QMetaObject::invokeMethod(this, [this, role] {
            --m_commandsPending[role];
            emit devicesChanged();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

bool DeviceManager::cameraActionBlocked()
{
    if (sequenceActionBlocked()) return true;
    if (!m_slots[MainCamera].connected) {
        setLastError(QStringLiteral("相机未连接"));
        return true;
    }
    return false;
}

void DeviceManager::clearCameraTelemetry()
{
    m_cameraTelemetry.clear();
    emit cameraTelemetryChanged();
}

bool DeviceManager::wheelActionBlocked()
{
    if (sequenceActionBlocked()) return true;
    if (!m_wheel || !m_slots[FilterWheel].connected) {
        setLastError(QStringLiteral("滤镜轮未连接"));
        return true;
    }
    const auto &metrics = m_slots[FilterWheel].metrics;
    if (m_commandsPending[FilterWheel] > 0 || metrics.value(QStringLiteral("moving")).toBool() || metrics.value(QStringLiteral("calibrating")).toBool()) {
        setLastError(QStringLiteral("滤镜轮动作进行中，请稍候"));
        return true;
    }
    return false;
}

bool DeviceManager::moveFilterWheel(int position)
{
    if (wheelActionBlocked()) return false;
    const int slotCount = m_slots[FilterWheel].metrics.value(QStringLiteral("slots")).toInt();
    if (position < 0 || (slotCount > 0 && position >= slotCount)) {
        setLastError(QStringLiteral("滤镜位置超出范围"));
        return false;
    }
    setLastError({});
    if (m_sequenceDispatch && m_filterFocusBaselineValid) m_filterFocusWheelTarget = position;
    else invalidateFilterFocusBaseline();
    const bool bidirectional = m_wheelBidirectional;
    postDeviceCommand(m_wheel, FilterWheel, [this, position, bidirectional] {
        m_wheel->moveTo(position, bidirectional);
    });
    // Accepted for asynchronous execution; arrival is reported via stateChanged.
    return true;
}

bool DeviceManager::calibrateFilterWheel()
{
    if (wheelActionBlocked()) return false;
    invalidateFilterFocusBaseline();
    setLastError({});
    postDeviceCommand(m_wheel, FilterWheel, [this] { m_wheel->calibrate(); });
    return true;
}

bool DeviceManager::setFilterWheelSlots(int slotCount)
{
    if (slotCount != 5 && slotCount != 7 && slotCount != 8) {
        setLastError(QStringLiteral("轮孔数量仅支持 5 孔、7 孔或 8 孔"));
        return false;
    }
    if (wheelActionBlocked()) return false;
    invalidateFilterFocusBaseline();
    setLastError({});
    postDeviceCommand(m_wheel, FilterWheel, [this, slotCount] { m_wheel->setSlotCount(slotCount); });
    return true;
}

bool DeviceManager::setFilterWheelBidirectional(bool bidirectional)
{
    if (sequenceActionBlocked()) return false;
    if (m_wheelBidirectional == bidirectional) return true;
    m_wheelBidirectional = bidirectional;
    if (m_slots[FilterWheel].connected) {
        m_slots[FilterWheel].metrics.insert(QStringLiteral("bidirectional"), bidirectional);
        emit devicesChanged();
    }
    return true;
}

bool DeviceManager::setFilterWheelChannel(int slot, const QString &suffix, int gain, double exposureSeconds)
{
    if (sequenceActionBlocked()) return false;
    if (slot < 1 || slot > kMaxWheelSlots) { setLastError(QStringLiteral("滤镜孔位超出范围")); return false; }
    if (gain < 0) { setLastError(QStringLiteral("增益不能为负数")); return false; }
    if (exposureSeconds <= 0 || exposureSeconds > 3600) {
        setLastError(QStringLiteral("曝光时间需在 0 ~ 3600 秒之间"));
        return false;
    }
    auto channel = m_filterWheelChannels[slot - 1].toMap();
    channel[QStringLiteral("suffix")] = sanitizedSuffix(suffix);
    channel[QStringLiteral("gain")] = gain;
    channel[QStringLiteral("exposure")] = exposureSeconds;
    m_filterWheelChannels[slot - 1] = channel;
    saveFilterWheelChannels();
    setLastError({});
    emit filterWheelChannelsChanged();
    return true;
}

bool DeviceManager::setCameraExposure(double seconds)
{
    if (cameraActionBlocked()) return false;
    if (!(seconds > 0.0) || seconds > 86400.0) {
        setLastError(QStringLiteral("曝光时间需在 0 ~ 86400 秒之间"));
        return false;
    }
    setLastError({});
    // The SDK exposure is an unsigned microsecond count; cap the conversion so
    // extreme values wrap into the backend's range clamp instead of overflowing.
    double microseconds = seconds * 1000000.0;
    if (microseconds > 4294967295.0) microseconds = 4294967295.0;
    const unsigned value = unsigned(microseconds);
    postDeviceCommand(m_camera, MainCamera, [this, value] { m_camera->setExposure(value); });
    return true;
}

bool DeviceManager::setCameraGain(int gain)
{
    if (cameraActionBlocked()) return false;
    if (gain < 0) {
        setLastError(QStringLiteral("增益不能为负数"));
        return false;
    }
    setLastError({});
    QMetaObject::invokeMethod(m_camera, [this, gain] { m_camera->setGain(unsigned(gain)); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::setCameraOffset(int offset)
{
    if (cameraActionBlocked()) return false;
    if (offset < 0) {
        setLastError(QStringLiteral("偏置不能为负数"));
        return false;
    }
    setLastError({});
    QMetaObject::invokeMethod(m_camera, [this, offset] { m_camera->setOffset(offset); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::setCameraBinning(int factor)
{
    if (cameraActionBlocked()) return false;
    if (factor < 1 || factor > 8) {
        setLastError(QStringLiteral("合并像素仅支持 1×1 ~ 8×8"));
        return false;
    }
    setLastError({});
    QMetaObject::invokeMethod(m_camera, [this, factor] { m_camera->setBinning(factor); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::setCameraCoolerEnabled(bool enabled)
{
    if (cameraActionBlocked()) return false;
    setLastError({});
    QMetaObject::invokeMethod(m_camera, [this, enabled] { m_camera->setCoolerEnabled(enabled); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::setCameraTargetTemperature(double celsius)
{
    if (cameraActionBlocked()) return false;
    setLastError({});
    QMetaObject::invokeMethod(m_camera, [this, celsius] { m_camera->setTargetTemperature(celsius); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::setCameraFanSpeed(int speed)
{
    if (cameraActionBlocked()) return false;
    if (speed < -1) {
        setLastError(QStringLiteral("风扇速度无效"));
        return false;
    }
    setLastError({});
    QMetaObject::invokeMethod(m_camera, [this, speed] { m_camera->setFanSpeed(speed); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::setCameraHeatLevel(int level)
{
    if (cameraActionBlocked()) return false;
    if (level < 0) {
        setLastError(QStringLiteral("加热级别不能为负数"));
        return false;
    }
    setLastError({});
    QMetaObject::invokeMethod(m_camera, [this, level] { m_camera->setHeatLevel(level); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::setCameraGainMode(int mode)
{
    if (cameraActionBlocked()) return false;
    if (mode < 0 || mode > 2) {
        setLastError(QStringLiteral("增益模式无效"));
        return false;
    }
    setLastError({});
    QMetaObject::invokeMethod(m_camera, [this, mode] { m_camera->setGainMode(mode); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::setCameraLowNoise(bool enabled)
{
    if (cameraActionBlocked()) return false;
    setLastError({});
    QMetaObject::invokeMethod(m_camera, [this, enabled] { m_camera->setLowNoise(enabled); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::startCameraPreview()
{
    if (cameraActionBlocked()) return false;
    setLastError({});
    postDeviceCommand(m_camera, MainCamera, [this] { m_camera->startPreview(true); });
    return true;
}

bool DeviceManager::captureSingleExposure()
{
    if (cameraActionBlocked()) return false;
    if (m_autoSaveStills) return captureStill(MainCamera);
    setLastError({});
    postDeviceCommand(m_camera, MainCamera, [this] { m_camera->startPreview(false); });
    return true;
}

bool DeviceManager::stopCameraPreview()
{
    if (cameraActionBlocked()) return false;
    setLastError({});
    postDeviceCommand(m_camera, MainCamera, [this] { m_camera->stopPreview(); });
    return true;
}

bool DeviceManager::startGuideLoop()
{
    if (!m_guideCamera || !m_slots[GuideCamera].connected) {
        setLastError(QStringLiteral("导星相机未连接"));
        return false;
    }
    setLastError({});
    QMetaObject::invokeMethod(m_guideCamera, [this] { m_guideCamera->startPreview(true); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::stopGuideLoop()
{
    if (!m_guideCamera || !m_slots[GuideCamera].connected) {
        setLastError(QStringLiteral("导星相机未连接"));
        return false;
    }
    setLastError({});
    QMetaObject::invokeMethod(m_guideCamera, [this] { m_guideCamera->stopPreview(); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::openMountSetup()
{
    if (centeringActionBlocked()) return false;
    if (!m_mount) {
        setLastError(QStringLiteral("赤道仪驱动尚未接入"));
        return false;
    }
    auto &slot = m_slots[Mount];
    if (slot.selectedId.isEmpty()) {
        setLastError(QStringLiteral("请先选择赤道仪"));
        return false;
    }
    if (slot.connected) {
        setLastError(QStringLiteral("赤道仪已连接，请先断开再打开设置"));
        return false;
    }
    if (slot.busy || m_scanning) {
        setLastError(QStringLiteral("设备操作进行中，请稍候"));
        return false;
    }
    slot.busy = true;
    const QString id = slot.selectedId;
    emit devicesChanged();
    QMetaObject::invokeMethod(m_mount, [this, id] { m_mount->setup(id); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::setMountTracking(bool on)
{
    if (centeringActionBlocked()) return false;
    if (m_mountGotoActive) { setLastError(QStringLiteral("请先停止指向，再修改跟踪")); return false; }
    if (!m_mount || !m_slots[Mount].connected) {
        setLastError(QStringLiteral("赤道仪未连接"));
        return false;
    }
    setLastError({});
    bool ok = false;
    QString error;
    auto *mount = static_cast<AscomMount *>(m_mount);
    // COM STA: the IDispatch pointer lives on the mount thread; invoke there.
    QMetaObject::invokeMethod(m_mount, [mount, on, &ok, &error] {
        ok = mount->setTracking(on);
        if (!ok) error = mount->lastError();
        else mount->refreshNow();
    }, Qt::BlockingQueuedConnection);
    if (!ok) { setLastError(error); return false; }
    return true;
}

bool DeviceManager::mountPark()
{
    if (centeringActionBlocked()) return false;
    if (m_solveCapturePending) { setLastError(QStringLiteral("正在拍摄解析照片，请先取消解析")); return false; }
    if (m_mountGotoActive) { setLastError(QStringLiteral("请先停止指向，再停放")); return false; }
    if (!m_mount || !m_slots[Mount].connected) {
        setLastError(QStringLiteral("赤道仪未连接"));
        return false;
    }
    setLastError({});
    bool ok = false;
    QString error;
    auto *mount = static_cast<AscomMount *>(m_mount);
    QMetaObject::invokeMethod(m_mount, [mount, &ok, &error] {
        ok = mount->park();
        if (!ok) error = mount->lastError();
    }, Qt::BlockingQueuedConnection);
    if (!ok) { setLastError(error); return false; }
    return true;
}

bool DeviceManager::setMountTrackingRate(int rate)
{
    if (centeringActionBlocked()) return false;
    if (!m_mount || !m_slots[Mount].connected) {
        setLastError(QStringLiteral("赤道仪未连接"));
        return false;
    }
    if (rate < 0 || rate > 3) { setLastError(QStringLiteral("跟踪速率无效")); return false; }
    setLastError({});
    bool ok = false;
    QString error;
    auto *mount = static_cast<AscomMount *>(m_mount);
    QMetaObject::invokeMethod(m_mount, [mount, rate, &ok, &error] {
        ok = mount->setTrackingRate(rate);
        if (!ok) error = mount->lastError();
        else mount->refreshNow();
    }, Qt::BlockingQueuedConnection);
    if (!ok) { setLastError(error); return false; }
    return true;
}

bool DeviceManager::mountFindHome()
{
    if (centeringActionBlocked()) return false;
    if (m_solveCapturePending) { setLastError(QStringLiteral("正在拍摄解析照片，请先取消解析")); return false; }
    if (m_mountGotoActive) { setLastError(QStringLiteral("请先停止指向，再寻找零位")); return false; }
    if (!m_mount || !m_slots[Mount].connected) { setLastError(QStringLiteral("赤道仪未连接")); return false; }
    setLastError({});
    bool ok = false;
    QString error;
    auto *mount = static_cast<AscomMount *>(m_mount);
    QMetaObject::invokeMethod(m_mount, [mount, &ok, &error] {
        ok = mount->findHome();
        if (!ok) error = mount->lastError();
    }, Qt::BlockingQueuedConnection);
    if (!ok) { setLastError(error); return false; }
    return true;
}

bool DeviceManager::mountSetCurrentPark()
{
    if (centeringActionBlocked()) return false;
    if (!m_mount || !m_slots[Mount].connected) { setLastError(QStringLiteral("赤道仪未连接")); return false; }
    setLastError({});
    bool ok = false;
    QString error;
    auto *mount = static_cast<AscomMount *>(m_mount);
    QMetaObject::invokeMethod(m_mount, [mount, &ok, &error] {
        ok = mount->setCurrentPark();
        if (!ok) error = mount->lastError();
    }, Qt::BlockingQueuedConnection);
    if (!ok) { setLastError(error); return false; }
    return true;
}

bool DeviceManager::setMountGuideRates(double raRate, double decRate)
{
    if (centeringActionBlocked()) return false;
    if (!m_mount || !m_slots[Mount].connected) { setLastError(QStringLiteral("赤道仪未连接")); return false; }
    if (!qIsFinite(raRate) || !qIsFinite(decRate) || raRate < 0.1 || raRate > 0.9 || decRate < 0.1 || decRate > 0.9) {
        setLastError(QStringLiteral("导星速率需在 0.1 ~ 0.9 之间"));
        return false;
    }
    setLastError({});
    bool ok = false;
    QString error;
    auto *mount = static_cast<AscomMount *>(m_mount);
    QMetaObject::invokeMethod(m_mount, [mount, raRate, decRate, &ok, &error] {
        ok = mount->setGuideRates(raRate, decRate);
        if (!ok) error = mount->lastError();
        mount->refreshNow();
    }, Qt::BlockingQueuedConnection);
    if (!ok) { setLastError(error); return false; }
    return true;
}

bool DeviceManager::mountUnpark()
{
    if (centeringActionBlocked()) return false;
    if (!m_mount || !m_slots[Mount].connected) {
        setLastError(QStringLiteral("赤道仪未连接"));
        return false;
    }
    setLastError({});
    bool ok = false;
    QString error;
    auto *mount = static_cast<AscomMount *>(m_mount);
    QMetaObject::invokeMethod(m_mount, [mount, &ok, &error] {
        ok = mount->unpark();
        if (!ok) error = mount->lastError();
    }, Qt::BlockingQueuedConnection);
    if (!ok) { setLastError(error); return false; }
    return true;
}

bool DeviceManager::slewMountToJ2000(double ra, double dec)
{
    if (centeringActionBlocked()) return false;
    if (!m_mount || !m_slots[Mount].connected || m_slots[Mount].busy || m_mountGuiding || m_manualMoving
        || m_mountGotoActive || (m_sequenceOwned && !m_sequenceDispatch) || m_stillPending[MainCamera] || !MountCoordinates::valid(ra, dec)) {
        setLastError(QStringLiteral("未连接、拍摄/导星/运动中或目标坐标无效，不能指向"));
        return false;
    }
    m_mountGotoActive = true;
    m_slots[Mount].metrics["gotoState"] = "Pending";
    setLastError({});
    emit devicesChanged();
    QMetaObject::invokeMethod(m_mount, [this, ra, dec] {
        const QString error = m_mount->slewToJ2000(ra, dec);
        QMetaObject::invokeMethod(this, [this, error] {
            if (!error.isEmpty()) {
                m_mountGotoActive = false;
                m_slots[Mount].metrics["gotoState"] = "Failed";
                setLastError(error);
                emit devicesChanged();
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::mountAbortSlew()
{
    emit mountMotionStopRequested();
    if (m_manualMoving) return stopMountManualMove();
    if (!m_mount || !m_slots[Mount].connected) {
        setLastError(QStringLiteral("赤道仪未连接"));
        return false;
    }
    setLastError({});
    QMetaObject::invokeMethod(m_mount, [this] {
        const QString error = m_mount->stopSlew();
        QMetaObject::invokeMethod(this, [this, error] {
            if (!error.isEmpty()) {
                m_mountGotoActive = true;
                m_slots[Mount].metrics["gotoState"] = "StopFailed";
                setLastError(error);
            } else { m_mountGotoActive = false; m_slots[Mount].metrics["gotoState"] = "Stopped"; }
            emit devicesChanged();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::startMountManualMove(int axis, double rate)
{
    if (centeringActionBlocked()) return false;
    if (m_solveCapturePending) { setLastError(QStringLiteral("正在拍摄解析照片，请先取消解析")); return false; }
    if (!m_mount || !m_slots[Mount].connected || m_slots[Mount].busy || m_mountGuiding || m_manualMoving
        || m_mountGotoActive || axis < 0 || axis > 1 || !qIsFinite(rate) || qAbs(rate) < 0.25 || qAbs(rate) > 1440) {
        setLastError(QStringLiteral("当前状态或速率不允许手动移动"));
        return false;
    }
    QString error;
    QMetaObject::invokeMethod(m_mount, [this, axis, rate, &error] {
        error = m_mount->startManualMove(axis, rate);
    }, Qt::BlockingQueuedConnection);
    setLastError(error);
    m_manualMoving = error.isEmpty();
    emit devicesChanged();
    return error.isEmpty();
}

bool DeviceManager::syncMountObserver(bool timeOnly, double longitude, double latitude)
{
    if (centeringActionBlocked()) return false;
    if (!m_mount || !m_slots[Mount].connected || m_slots[Mount].busy || m_mountGuiding || m_manualMoving
        || m_mountGotoActive || (!timeOnly && (!qIsFinite(longitude) || !qIsFinite(latitude)
                         || qAbs(longitude) > 180 || qAbs(latitude) > 90))) {
        setLastError(QStringLiteral("连接状态、运动状态或经纬度不允许同步"));
        return false;
    }
    QString error;
    QMetaObject::invokeMethod(m_mount, [this, timeOnly, longitude, latitude, &error] {
        error = m_mount->syncObserver(timeOnly, longitude, latitude);
    }, Qt::BlockingQueuedConnection);
    setLastError(error);
    return error.isEmpty();
}

bool DeviceManager::stopMountManualMove()
{
    if (!m_mount || !m_slots[Mount].connected) { m_manualMoving = false; emit devicesChanged(); return false; }
    QString error;
    QMetaObject::invokeMethod(m_mount, [this, &error] { error = m_mount->stopManualMove(); }, Qt::BlockingQueuedConnection);
    if (error.isEmpty()) m_manualMoving = false;
    setLastError(error);
    emit devicesChanged();
    return error.isEmpty();
}

void DeviceManager::keepMountManualMoveAlive()
{
    if (m_mount && m_manualMoving)
        QMetaObject::invokeMethod(m_mount, [this] { m_mount->keepManualMoveAlive(); }, Qt::QueuedConnection);
}

void DeviceManager::setMountGuiding(bool guiding)
{
    if (m_mountGuiding == guiding) return;
    m_mountGuiding = guiding;
    if (guiding && m_manualMoving) stopMountManualMove();
    emit devicesChanged();
}

bool DeviceManager::mountPulseGuide(int direction, int durationMs)
{
    if (centeringActionBlocked()) return false;
    if (m_mountGotoActive) { setLastError(QStringLiteral("赤道仪正在指向，不能发送导星脉冲")); return false; }
    if (m_manualMoving) { setLastError(QStringLiteral("手动移动中不能发送导星脉冲")); return false; }
    if (!m_mount || !m_slots[Mount].connected) {
        setLastError(QStringLiteral("赤道仪未连接"));
        return false;
    }
    if (direction < 0 || direction > 3 || durationMs <= 0 || durationMs > 10000) {
        setLastError(QStringLiteral("导星脉冲参数无效"));
        return false;
    }
    setLastError({});
    bool ok = false;
    QString error;
    auto *mount = static_cast<AscomMount *>(m_mount);
    QMetaObject::invokeMethod(m_mount, [mount, direction, durationMs, &ok, &error] {
        ok = mount->pulseGuide(direction, durationMs);
        if (!ok) error = mount->lastError();
    }, Qt::BlockingQueuedConnection);
    if (!ok) { setLastError(error); return false; }
    return true;
}

bool DeviceManager::setGuideExposure(double seconds)
{
    if (!m_guideCamera || !m_slots[GuideCamera].connected) {
        setLastError(QStringLiteral("导星相机未连接"));
        return false;
    }
    if (!(seconds > 0.0) || seconds > 3600.0) {
        setLastError(QStringLiteral("曝光时间需在 0 ~ 3600 秒之间"));
        return false;
    }
    setLastError({});
    double microseconds = seconds * 1000000.0;
    if (microseconds > 4294967295.0) microseconds = 4294967295.0;
    const unsigned value = unsigned(microseconds);
    QMetaObject::invokeMethod(m_guideCamera, [this, value] { m_guideCamera->setExposure(value); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::moveFocuser(int position)
{
    if (sequenceActionBlocked()) return false;
    if (!m_focuser || !m_slots[Focuser].connected) {
        setLastError(QStringLiteral("电调焦未连接"));
        return false;
    }
    if (position < 0) {
        setLastError(QStringLiteral("调焦位置不能为负数"));
        return false;
    }
    setLastError({});
    if (!m_filterFocusMoving) invalidateFilterFocusBaseline();
    postDeviceCommand(m_focuser,Focuser,[this,position] { m_focuser->moveTo(position); });
    return true;
}

bool DeviceManager::haltFocuser()
{
    if (m_autofocusOwned && !m_autofocusDispatch) { emit focuserStopRequested(); return true; }
    if (!m_sequenceDispatch && !m_autofocusDispatch) {
        invalidateFilterFocusBaseline(); emit focuserStopRequested();
        if (m_sequenceOwned && m_filterFocusMoving) return true;
    }
    if (!m_focuser || !m_slots[Focuser].connected) {
        setLastError(QStringLiteral("电调焦未连接"));
        return false;
    }
    setLastError({});
    postDeviceCommand(m_focuser, Focuser, [this] { m_focuser->halt(); });
    return true;
}

bool DeviceManager::setFocuserReversed(bool reversed)
{
    if (sequenceActionBlocked()) return false;
    if (!m_focuser || !m_slots[Focuser].connected) {
        setLastError(QStringLiteral("电调焦未连接"));
        return false;
    }
    setLastError({});
    invalidateFilterFocusBaseline();
    QMetaObject::invokeMethod(m_focuser, [this, reversed] { m_focuser->setReversed(reversed); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::setFocuserMaxStep(int maxStep)
{
    if (sequenceActionBlocked()) return false;
    if (!m_focuser || !m_slots[Focuser].connected) {
        setLastError(QStringLiteral("电调焦未连接"));
        return false;
    }
    if (maxStep <= 0) {
        setLastError(QStringLiteral("最大行程必须为正数"));
        return false;
    }
    setLastError({});
    invalidateFilterFocusBaseline();
    QMetaObject::invokeMethod(m_focuser, [this, maxStep] { m_focuser->setMaxStep(maxStep); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::resetFocuserPosition()
{
    if (sequenceActionBlocked()) return false;
    if (!m_focuser || !m_slots[Focuser].connected) {
        setLastError(QStringLiteral("电调焦未连接"));
        return false;
    }
    const int position = m_slots[Focuser].metrics.value(QStringLiteral("position")).toInt();
    if (position <= 0) {
        setLastError(QStringLiteral("当前位置已为零或未知，无需清零"));
        return false;
    }
    setLastError({});
    invalidateFilterFocusBaseline();
    QMetaObject::invokeMethod(m_focuser, [this] { m_focuser->setZero(); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::setCurrentFocuserPosition(int position)
{
    if (sequenceActionBlocked()) return false;
    if (!m_focuser || !m_slots[Focuser].connected) {
        setLastError(QStringLiteral("电调焦未连接"));
        return false;
    }
    if (position < 0) {
        setLastError(QStringLiteral("设定位置不能为负数"));
        return false;
    }
    setLastError({});
    invalidateFilterFocusBaseline();
    QMetaObject::invokeMethod(m_focuser, [this, position] { m_focuser->setCurrentPosition(position); }, Qt::QueuedConnection);
    return true;
}

bool DeviceManager::setGuideGain(int gain)
{
    if (!m_guideCamera || !m_slots[GuideCamera].connected) {
        setLastError(QStringLiteral("导星相机未连接"));
        return false;
    }
    if (gain < 0) {
        setLastError(QStringLiteral("增益不能为负数"));
        return false;
    }
    setLastError({});
    QMetaObject::invokeMethod(m_guideCamera, [this, gain] { m_guideCamera->setGain(unsigned(gain)); }, Qt::QueuedConnection);
    return true;
}

QString DeviceManager::channelStorageKey() const
{
    // A connected wheel gets its own store; before any connection the legacy
    // global key keeps pre-connection edits working without a device attached.
    return m_wheelSerial.isEmpty()
        ? QStringLiteral("filterWheel/channels")
        : QStringLiteral("filterWheel/%1/channels").arg(m_wheelSerial);
}

QVariantMap DeviceManager::filterFocusSettings() const
{
    return {{"enabled", m_filterFocusEnabled}, {"reference", m_filterFocusReference},
            {"editable", !m_sequenceOwned && !m_autofocusOwned && !m_centeringOwned},
            {"baselineValid", m_filterFocusBaselineValid}};
}

void DeviceManager::invalidateFilterFocusBaseline()
{
    m_filterFocusWheelTarget = -1;
    if (!m_filterFocusBaselineValid) return;
    m_filterFocusBaselineValid = false;
    emit filterFocusSettingsChanged();
}

bool DeviceManager::filterFocusOffset(int position, int &offset) const
{
    if (position < 0 || position >= m_filterWheelChannels.size()) return false;
    const auto value = m_filterWheelChannels[position].toMap().value("focusOffset");
    if (value.isNull()) return false;
    offset = value.toInt(); return true;
}

void DeviceManager::establishFilterFocusBaseline(int verifiedPosition)
{
    invalidateFilterFocusBaseline();
    const auto &wheel = m_slots[FilterWheel]; const auto &focuser = m_slots[Focuser];
    const int filter = wheel.metrics.value("position", -1).toInt();
    int offset = 0;
    if (!wheel.connected || wheel.busy || m_commandsPending[FilterWheel]
        || wheel.metrics.value("moving").toBool() || wheel.metrics.value("calibrating").toBool()
        || filter >= wheel.metrics.value("slots").toInt() || !filterFocusOffset(filter, offset)
        || !focuser.connected || focuser.busy || m_commandsPending[Focuser]
        || !focuser.metrics.contains("moving") || focuser.metrics.value("moving").toBool()
        || focuser.metrics.value("position", -1).toInt() != verifiedPosition || verifiedPosition < 0) return;
    m_filterFocusReferencePosition = qint64(verifiedPosition) - offset;
    m_filterFocusPosition = verifiedPosition; m_filterFocusFilter = filter;
    m_filterFocusMaxStep = focuser.metrics.value("maxStep").toInt();
    m_filterFocusReversed = focuser.metrics.value("reversed").toBool();
    m_filterFocusBaselineValid = true;
    emit filterFocusSettingsChanged();
}

bool DeviceManager::setFilterFocusEnabled(bool enabled)
{
    if (sequenceActionBlocked()) return false;
    if (m_filterFocusEnabled == enabled) return true;
    m_filterFocusEnabled = enabled; invalidateFilterFocusBaseline(); saveFilterWheelChannels();
    setLastError({}); emit filterFocusSettingsChanged(); return true;
}

bool DeviceManager::setFilterFocusReference(int slot)
{
    if (sequenceActionBlocked()) return false;
    if (slot < 1 || slot > kMaxWheelSlots
        || (m_slots[FilterWheel].connected && slot > m_slots[FilterWheel].metrics.value("slots").toInt())) {
        setLastError(QStringLiteral("参考滤镜孔位超出范围")); return false;
    }
    if (slot == m_filterFocusReference) return true;
    int origin = 0;
    if (!filterFocusOffset(slot - 1, origin)) {
        setLastError(QStringLiteral("请先填写新参考滤镜的实测焦点偏移，再切换参考滤镜")); return false;
    }
    auto channels = m_filterWheelChannels;
    for (int i = 0; i < channels.size(); ++i) {
        auto channel = channels[i].toMap(); const auto value = channel.value("focusOffset");
        if (value.isNull()) continue;
        const qint64 rebased = qint64(value.toInt()) - origin;
        if (rebased < -1000000 || rebased > 1000000) {
            setLastError(QStringLiteral("参考切换后的偏移超出 ±1000000 步，请检查标定值")); return false;
        }
        channel["focusOffset"] = int(rebased); channels[i] = channel;
    }
    m_filterWheelChannels = channels; m_filterFocusReference = slot;
    invalidateFilterFocusBaseline(); saveFilterWheelChannels(); setLastError({});
    emit filterWheelChannelsChanged(); emit filterFocusSettingsChanged(); return true;
}

bool DeviceManager::setFilterWheelFocusOffset(int slot, const QVariant &offset)
{
    if (sequenceActionBlocked()) return false;
    if (slot < 1 || slot > kMaxWheelSlots) { setLastError(QStringLiteral("滤镜孔位超出范围")); return false; }
    const bool clear = offset.isNull() || (offset.metaType().id() == QMetaType::QString && offset.toString().trimmed().isEmpty());
    const double number = offset.toDouble();
    if ((!clear && (!QJsonValue::fromVariant(offset).isDouble() || !std::isfinite(number)
                   || number != std::floor(number) || number < -1000000 || number > 1000000))
        || (slot == m_filterFocusReference && (clear || number != 0))) {
        setLastError(QStringLiteral("焦点偏移必须为 ±1000000 范围内的整数；参考滤镜固定为 0")); return false;
    }
    auto channel = m_filterWheelChannels[slot - 1].toMap();
    const QVariant value = clear ? QVariant() : QVariant(int(number));
    if (channel.value("focusOffset") == value) return true;
    channel["focusOffset"] = value; m_filterWheelChannels[slot - 1] = channel;
    invalidateFilterFocusBaseline(); saveFilterWheelChannels(); setLastError({});
    emit filterWheelChannelsChanged(); emit filterFocusSettingsChanged(); return true;
}

void DeviceManager::loadFilterWheelChannels()
{
    invalidateFilterFocusBaseline();
    m_filterWheelChannels.clear();
    const QJsonDocument doc = QJsonDocument::fromJson(
        QSettings().value(channelStorageKey()).toString().toUtf8());
    const auto focus = doc.object().value("focus").toObject();
    m_filterFocusEnabled = focus.value("enabled").toBool(false);
    m_filterFocusReference = focus.value("reference").toInt(1);
    if (m_filterFocusReference < 1 || m_filterFocusReference > kMaxWheelSlots) m_filterFocusReference = 1;
    for (int slot = 1; slot <= kMaxWheelSlots; ++slot) {
        QVariantMap channel = defaultChannel(slot);
        channel.insert("focusOffset", QVariant());
        const QJsonObject entry = doc.object().value(QString::number(slot)).toObject();
        if (!entry.isEmpty()) {
            channel[QStringLiteral("suffix")] = entry.value(QStringLiteral("suffix")).toString();
            channel[QStringLiteral("gain")] = entry.value(QStringLiteral("gain")).toInt(100);
            channel[QStringLiteral("exposure")] = entry.value(QStringLiteral("exposure")).toDouble(3.0);
            const auto offset = entry.value("focusOffset");
            if (offset.isDouble() && std::isfinite(offset.toDouble()) && offset.toDouble() == std::floor(offset.toDouble())
                && offset.toDouble() >= -1000000 && offset.toDouble() <= 1000000) channel["focusOffset"] = int(offset.toDouble());
        }
        if (slot == m_filterFocusReference) channel["focusOffset"] = 0;
        m_filterWheelChannels.append(channel);
    }
    emit filterFocusSettingsChanged();
}

void DeviceManager::saveFilterWheelChannels() const
{
    QJsonObject root;
    root.insert("focus", QJsonObject{{"enabled", m_filterFocusEnabled}, {"reference", m_filterFocusReference}});
    for (const auto &value : m_filterWheelChannels) {
        const auto channel = value.toMap();
        const int slot = channel.value(QStringLiteral("slot")).toInt();
        root.insert(QString::number(slot), QJsonObject{
            {QStringLiteral("suffix"), channel.value(QStringLiteral("suffix")).toString()},
            {QStringLiteral("gain"), channel.value(QStringLiteral("gain")).toInt()},
            {QStringLiteral("exposure"), channel.value(QStringLiteral("exposure")).toDouble()},
            {QStringLiteral("focusOffset"), QJsonValue::fromVariant(channel.value("focusOffset"))},
        });
    }
    QSettings settings;
    settings.setValue(channelStorageKey(),
                      QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

bool DeviceManager::validRole(int role) const { return role >= Mount && role <= Focuser; }

void DeviceManager::setLastError(const QString &message)
{
    if (m_lastError == message) return;
    m_lastError = message;
    emit lastErrorChanged();
}

QVariantMap DeviceManager::toMap(int role, const DeviceSlot &slot) const
{
    return {{"role", role}, {"title", slot.title}, {"selectedId", slot.selectedId}, {"selectedName", slot.selectedName},
            {"connected", slot.connected}, {"busy", slot.busy}, {"error", slot.error}, {"metrics", slot.metrics},
            {"supported", role == MainCamera || role == GuideCamera || role == Focuser
                          || role == Mount || (role == FilterWheel && m_wheel)}};
}
