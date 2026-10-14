#pragma once

#include <QImage>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QThread>

#include <array>
#include <functional>
#include "imaging/StillSaveQueue.h"

class CameraBackend;
class FilterWheelBackend;
class FocuserBackend;
class MountBackend;

class DeviceManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(int connectedCount READ connectedCount NOTIFY devicesChanged)
    Q_PROPERTY(QString statusSummary READ statusSummary NOTIFY devicesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList filterWheelChannels READ filterWheelChannels NOTIFY filterWheelChannelsChanged)
    Q_PROPERTY(QVariantMap filterFocusSettings READ filterFocusSettings NOTIFY filterFocusSettingsChanged)
    // High-frequency camera readouts (temperature, TEC power) live here so the
    // settings pages can refresh them without rebuilding their whole row model.
    Q_PROPERTY(QVariantMap cameraTelemetry READ cameraTelemetry NOTIFY cameraTelemetryChanged)
    Q_PROPERTY(QImage previewFrame READ previewFrame NOTIFY previewFrameChanged)
    Q_PROPERTY(int previewCounter READ previewCounter NOTIFY previewFrameChanged)
    Q_PROPERTY(QVariantList previewHistogram READ previewHistogram NOTIFY previewFrameChanged)
    // Guide-camera streams: dimmed preview for display plus frame id.
    Q_PROPERTY(QImage guidePreviewFrame READ guidePreviewFrame NOTIFY guidePreviewFrameChanged)
    Q_PROPERTY(int guidePreviewCounter READ guidePreviewCounter NOTIFY guidePreviewFrameChanged)
    Q_PROPERTY(bool mountManualMoving READ mountManualMoving NOTIFY devicesChanged)
    Q_PROPERTY(bool mountGuiding READ mountGuiding NOTIFY devicesChanged)
    // 单帧保存目录：空串恢复默认（图片/StellaMora）；经 QSettings 跨会话持久化。
    Q_PROPERTY(QString stillSaveDirectory READ stillSaveDirectory WRITE setStillSaveDirectory NOTIFY stillSaveDirectoryChanged)
    // 单帧曝光是否自动落盘（默认开；关=仅预览，NINA Snap 语义）。
    Q_PROPERTY(bool autoSaveStills READ autoSaveStills WRITE setAutoSaveStills NOTIFY autoSaveStillsChanged)
    Q_PROPERTY(QString stillImageType MEMBER m_stillImageType NOTIFY stillMetadataChanged)
    Q_PROPERTY(QString stillTargetName MEMBER m_stillTargetName NOTIFY stillMetadataChanged)

public:
    enum DeviceRole { Mount, MainCamera, GuideCamera, FilterWheel, Focuser };
    Q_ENUM(DeviceRole)

    explicit DeviceManager(QObject *parent = nullptr);
    // Camera-only manager, used by tests; filter-wheel operations stay unsupported.
    explicit DeviceManager(CameraBackend *backend, QObject *parent = nullptr);
    explicit DeviceManager(CameraBackend *camera, FilterWheelBackend *wheel, QObject *parent = nullptr);
    explicit DeviceManager(CameraBackend *camera, FilterWheelBackend *wheel, FocuserBackend *focuser,
                           QObject *parent = nullptr);
    explicit DeviceManager(CameraBackend *camera, FilterWheelBackend *wheel, CameraBackend *guide,
                           MountBackend *mount, QObject *parent = nullptr, FocuserBackend *focuser = nullptr);
    ~DeviceManager() override;

    QVariantList devices() const;
    int connectedCount() const;
    QString statusSummary() const;
    QString lastError() const;
    bool scanning() const { return m_scanning; }
    QString stillSaveDirectory() const;
    void setStillSaveDirectory(const QString &directory);
    bool autoSaveStills() const { return m_autoSaveStills; }
    void setAutoSaveStills(bool enabled);
    QVariantList filterWheelChannels() const { return m_filterWheelChannels; }
    QVariantMap filterFocusSettings() const;
    QVariantMap cameraTelemetry() const { return m_cameraTelemetry; }
    QImage previewFrame() const { return m_previewFrame; }
    int previewCounter() const { return m_previewCounter; }
    QVariantList previewHistogram() const { return m_previewHistogram; }
    QImage guidePreviewFrame() const { return m_guidePreviewFrame; }
    int guidePreviewCounter() const { return m_guidePreviewCounter; }
    Q_INVOKABLE void refreshDevices();

    Q_INVOKABLE QVariantMap device(int role) const;
    Q_INVOKABLE QVariantList candidatesForRole(int role) const;
    Q_INVOKABLE bool selectDevice(int role, const QString &id);
    Q_INVOKABLE bool setConnected(int role, bool connected);
    Q_INVOKABLE bool toggleConnected(int role);

    // Filter-wheel controls are 0-based like the hardware; all return false with lastError set when refused.
    Q_INVOKABLE bool moveFilterWheel(int position);
    Q_INVOKABLE bool calibrateFilterWheel();
    Q_INVOKABLE bool setFilterWheelSlots(int slotCount);
    Q_INVOKABLE bool setFilterWheelBidirectional(bool bidirectional);
    // Per-slot capture settings (slots are 1-based here). Saved to QSettings only;
    // the acquisition module is expected to read and apply them later.
    Q_INVOKABLE bool setFilterWheelChannel(int slot, const QString &suffix, int gain, double exposureSeconds);
    Q_INVOKABLE bool setFilterFocusEnabled(bool enabled);
    Q_INVOKABLE bool setFilterFocusReference(int slot);
    Q_INVOKABLE bool setFilterWheelFocusOffset(int slot, const QVariant &offset);

    // Camera controls use display units: exposure in seconds, gain as the analog
    // percent (100 = 1x), offset black level, binning factor n of n*n (1..8),
    // target temperature in °C, fan speed -1 (default) / 0 (off) / [1, max],
    // heat level 0..max, gain mode 0 = LCG, 1 = HCG, 2 = HDR.
    Q_INVOKABLE bool setCameraExposure(double seconds);
    Q_INVOKABLE bool setCameraGain(int gain);
    Q_INVOKABLE bool setCameraOffset(int offset);
    Q_INVOKABLE bool setCameraBinning(int factor);
    Q_INVOKABLE bool setCameraCoolerEnabled(bool enabled);
    Q_INVOKABLE bool setCameraTargetTemperature(double celsius);
    Q_INVOKABLE bool setCameraFanSpeed(int speed);
    Q_INVOKABLE bool setCameraHeatLevel(int level);
    Q_INVOKABLE bool setCameraGainMode(int mode);
    Q_INVOKABLE bool setCameraLowNoise(bool enabled);
    // Live preview control: startCameraPreview loops exposures until stopped;
    // captureSingleExposure takes one exposure and stops automatically.
    // Resolution index into the camera's "resolutions" list (role: 1 主相机 / 2 导星).
    Q_INVOKABLE bool setCameraResolution(int role, int index);
    // Capture the next full-resolution frame to stillSaveDirectory as FITS;
    // the outcome arrives via stillCaptured.
    Q_INVOKABLE bool captureStill(int role);
    bool captureForSolve(double seconds);
    void cancelSolveCapture();
    // 相机参数快照（曝光/增益/分辨率表/TEC 等），取自连接信息与状态回读。
    Q_INVOKABLE QVariantMap cameraParameters(int role) const;
    Q_INVOKABLE bool startCameraPreview();
    Q_INVOKABLE bool captureSingleExposure();
    Q_INVOKABLE bool stopCameraPreview();
    // Guide camera (role 2): continuous loop delivering raw 16-bit frames
    // through guideFrameReady for the guiding analytics.
    Q_INVOKABLE bool startGuideLoop();
    Q_INVOKABLE bool stopGuideLoop();
    // Guide camera exposure/gain in display units (seconds / analog percent).
    Q_INVOKABLE bool setGuideExposure(double seconds);
    Q_INVOKABLE bool setGuideGain(int gain);
    // Mount (role 0): opens the driver's own setup dialog.
    Q_INVOKABLE bool openMountSetup();
    // Mount controls (require connection; errors go to lastError).
    Q_INVOKABLE bool setMountTracking(bool on);
    Q_INVOKABLE bool setMountTrackingRate(int rate);
    Q_INVOKABLE bool mountFindHome();
    Q_INVOKABLE bool mountSetCurrentPark();
    Q_INVOKABLE bool setMountGuideRates(double raRate, double decRate);
    Q_INVOKABLE bool mountPark();
    Q_INVOKABLE bool mountUnpark();
    Q_INVOKABLE bool mountAbortSlew();
    Q_INVOKABLE bool slewMountToJ2000(double raHours, double decDeg);
    Q_INVOKABLE bool syncMountObserver(bool timeOnly, double longitude, double latitude);
    Q_INVOKABLE bool startMountManualMove(int axis, double siderealRate);
    Q_INVOKABLE bool stopMountManualMove();
    Q_INVOKABLE void keepMountManualMoveAlive();
    bool mountManualMoving() const { return m_manualMoving; }
    bool mountGuiding() const { return m_mountGuiding; }
    void setMountGuiding(bool guiding);
    // Guide pulse through the ASCOM mount (directions: 0=N 1=S 2=E 3=W).
    Q_INVOKABLE bool mountPulseGuide(int direction, int durationMs);
    // Focuser (role 4): absolute move in device steps, halt, direction flip.
    Q_INVOKABLE bool moveFocuser(int position);
    Q_INVOKABLE bool haltFocuser();
    Q_INVOKABLE bool setFocuserReversed(bool reversed);
    Q_INVOKABLE bool setFocuserMaxStep(int maxStep);
    // Re-anchors the current physical spot as position zero (no motion).
    Q_INVOKABLE bool resetFocuserPosition();
    // Reads the current physical spot AS the given position (no motion) —
    // refused by the manager when the device rejects the value form.
    Q_INVOKABLE bool setCurrentFocuserPosition(int position);

signals:
    void mountMotionStopRequested();
    void devicesChanged();
    // Candidate lists (per-role device menus) changed via a completed scan.
    // Separate from devicesChanged so 1 Hz mount state polling never resets
    // open candidate dropdowns.
    void candidatesChanged();
    void lastErrorChanged();
    void guidePreviewFrameChanged();
    // Raw 16-bit guide frame for analytics consumers (C++-side connection).
    void guideFrameReady(QVector<quint16> data, int width, int height, double exposureSeconds);
    void stillSaveDirectoryChanged();
    void stillMetadataChanged();
    void autoSaveStillsChanged();
    void stillCaptured(int role, const QString &path, bool ok);
    void stillFrameReceived(int role);
    void mainPhotoReady(QVector<quint16> data, int width, int height, QVariantMap metadata);
    void solvePhotoReady(QVector<quint16> data, int width, int height, QVariantMap metadata);
    void solveCaptureFailed(const QString &message);
    void autofocusPhotoReady(QVector<quint16> data, int width, int height, QVariantMap metadata);
    void autofocusCaptureFailed(const QString &message);
    void focuserCommandFailed(const QString &message);
    void focuserStopRequested();
    void filterWheelChannelsChanged();
    void filterFocusSettingsChanged();
    void cameraTelemetryChanged();
    void previewFrameChanged();

private:
    friend class SequenceController;
    friend class CenteringController;
    friend class AutoFocusController;
    bool autofocusActionBlocked();
    bool m_autofocusOwned = false;
    bool m_autofocusDispatch = false;
    bool captureForAutofocus(double seconds);
    void cancelAutofocusCapture();
    void finishAutofocusCapture();
    bool m_autofocusCapturePending = false;
    unsigned m_beforeAutofocusExposure = 0;
    bool centeringActionBlocked();
    bool m_centeringOwned = false;
    bool m_centeringDispatch = false;
    bool sequenceActionBlocked();
    void postDeviceCommand(QObject *backend, int role, std::function<void()> action);
    std::array<int, 5> m_commandsPending{};
    bool beginStill(int role, StillRequest request, double seconds = -1, int gain = 0);
    bool m_sequenceOwned = false;
    bool m_sequenceDispatch = false;
    struct DeviceSlot {
        QString title;
        QString selectedId;
        QString selectedName;
        bool connected = false;
        bool busy = false;
        QString error;
        QVariantMap metrics;
    };

    bool validRole(int role) const;
    void enqueueStill(int role, QVector<quint16> &&data, int width, int height, const QVariantMap &metadata);
    void setLastError(const QString &message);
    QVariantMap toMap(int role, const DeviceSlot &slot) const;
    bool wheelActionBlocked();
    bool cameraActionBlocked();
    void clearCameraTelemetry();
    void finishScan(const QString &error);
    QString channelStorageKey() const;
    void loadFilterWheelChannels();
    void saveFilterWheelChannels() const;
    void invalidateFilterFocusBaseline();
    void establishFilterFocusBaseline(int verifiedPosition);
    bool filterFocusOffset(int position, int &offset) const;
    void setupGuideCamera();
    void setupFocuser();
    void setupMount();

    std::array<DeviceSlot, 5> m_slots;
    QString m_stillSaveDirectory;
    bool m_autoSaveStills = true;
    QString m_stillImageType = QStringLiteral("LIGHT");
    QString m_stillTargetName;
    std::array<StillRequest, 5> m_stillRequests;
    std::array<bool, 5> m_stillPending{};
    std::array<int, 5> m_unsettledStills{};
    bool m_shuttingDown = false;
    bool m_solveCapturePending = false;
    unsigned m_beforeSolveExposure = 0;
    void finishSolveCapture();
    class StillSaveQueue *m_saveQueue = nullptr;
    class QThread *m_saveThread = nullptr;
    QString m_lastError;
    QVariantList m_cameras;
    QVariantList m_wheels;
    QVariantList m_guideCameras;
    QVariantList m_filterWheelChannels;
    QString m_wheelSerial;
    bool m_filterFocusEnabled = false, m_filterFocusBaselineValid = false;
    int m_filterFocusReference = 1;
    qint64 m_filterFocusReferencePosition = 0;
    int m_filterFocusPosition = -1, m_filterFocusFilter = -1;
    int m_filterFocusMaxStep = 0;
    bool m_filterFocusReversed = false;
    int m_filterFocusWheelTarget = -1;
    bool m_filterFocusMoving = false;
    QVariantMap m_cameraTelemetry;
    QImage m_previewFrame;
    int m_previewCounter = 0;
    QVariantList m_previewHistogram;
    bool m_scanning = false;
    int m_pendingScans = 0;
    bool m_wheelBidirectional = false;
    QThread m_cameraThread;
    CameraBackend *m_camera;
    QThread m_wheelThread;
    FilterWheelBackend *m_wheel = nullptr;
    QThread m_focuserThread;
    FocuserBackend *m_focuser = nullptr;
    QThread m_mountThread;
    MountBackend *m_mount = nullptr;
    bool m_manualMoving = false;
    bool m_mountGuiding = false;
    bool m_mountGotoActive = false;
    QVariantList m_mounts;
    QVariantList m_focusers;
    QThread m_guideThread;
    CameraBackend *m_guideCamera = nullptr;
    QImage m_guidePreviewFrame;
    int m_guidePreviewCounter = 0;
};
