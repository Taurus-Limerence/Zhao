#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "devices/DeviceManager.h"
#include "devices/CameraBackend.h"
#include "devices/MountBackend.h"

class SolveTestMount final : public MountBackend {
public:
    void scan() override { emit scanFinished({QVariantMap{{"id","test-mount"},{"name","Test mount"}}},{}); }
    void open(const QString &) override { emit connectionFinished(true,{},{}); }
    void close() override { emit connectionFinished(false,{},{}); }
    void setup(const QString &) override {}
    QString startManualMove(int,double) override { return {}; }
};

class FakeCamera final : public CameraBackend
{
public:
    unsigned exposure = 0;
    unsigned gain = 0;
    int offset = -1;
    int binning = 0;
    bool cooler = false;
    double targetTemp = 0.0;
    int fanSpeed = 0;
    int heatLevel = -1;
    int gainMode = -1;
    bool lowNoise = false;
    bool failStill = false;
    bool holdStill = false;
    bool held = false;
    void requestStill() override {
        if (holdStill) { held=true; return; }
        if (failStill) { emit stillFailed("fake capture failure"); return; }
        emit rawStillReady({0, 65535}, 2, 1, {{"EXPTIME", "2.500000"}, {"GAIN", 42},
                           {"DATE-OBS", "2026-10-09T01:02:03.000Z"}, {"BAYERPAT", "RGGB"}});
    }

    void scan() override {
        emit scanFinished({QVariantMap{{"id", "camera-a"}, {"name", "Camera A"}},
                           QVariantMap{{"id", "camera-b"}, {"name", "Camera B"}}}, {});
    }
    void open(const QString &id) override {
        if (id == "camera-b") emit connectionFinished(false, {}, QStringLiteral("Open failed"));
        else emit connectionFinished(true, {{"serial", "TEST-SN"}}, {});
    }
    void close() override { emit connectionFinished(false, {}, {}); }
    void setExposure(unsigned microseconds) override {
        exposure = microseconds;
        emit stateChanged({{"exposureUs", exposure}});
    }
    void setGain(unsigned value) override {
        gain = value;
        emit stateChanged({{"gain", gain}});
    }
    void setOffset(int value) override {
        offset = value;
        emit stateChanged({{"offset", offset}});
    }
    void setBinning(int factor) override {
        binning = factor;
        emit stateChanged({{"binning", binning}});
    }
    void setCoolerEnabled(bool enabled) override {
        cooler = enabled;
        emit stateChanged({{"tecEnabled", cooler}});
    }
    void setTargetTemperature(double celsius) override {
        targetTemp = celsius;
        emit stateChanged({{"targetTemp", targetTemp}});
    }
    void setFanSpeed(int speed) override {
        fanSpeed = speed;
        emit stateChanged({{"fanSpeed", fanSpeed}});
    }
    void setHeatLevel(int level) override {
        heatLevel = level;
        emit stateChanged({{"heatLevel", heatLevel}});
    }
    void setGainMode(int mode) override {
        gainMode = mode;
        emit stateChanged({{"gainMode", gainMode}});
    }
    void setLowNoise(bool enabled) override {
        lowNoise = enabled;
        emit stateChanged({{"lowNoise", lowNoise}});
    }
    void startPreview(bool continuous) override {
        previewing = true;
        previewContinuous = continuous;
        emit stateChanged({{"previewing", previewing}});
    }
    void stopPreview() override {
        if (held) { held=false; emit stillFailed("cancelled"); }
        previewing = false;
        emit stateChanged({{"previewing", previewing}});
    }
    bool previewing = false;
    bool previewContinuous = false;
};

class DeviceManagerTest final : public QObject
{
    Q_OBJECT
private slots:
    void solveCaptureIsTemporaryAndRestoresExposure() {
        QTemporaryDir dir;
        auto *backend=new FakeCamera;
        DeviceManager manager(backend,nullptr,nullptr,new SolveTestMount);
        manager.setStillSaveDirectory(dir.path());
        manager.setAutoSaveStills(false);
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::MainCamera).size(),2);
        QVERIFY(manager.selectDevice(DeviceManager::MainCamera,"camera-a"));
        QVERIFY(manager.setConnected(DeviceManager::MainCamera,true));
        QTRY_COMPARE(manager.connectedCount(),1);
        QVERIFY(manager.selectDevice(DeviceManager::Mount,"test-mount"));
        QVERIFY(manager.setConnected(DeviceManager::Mount,true));
        QTRY_COMPARE(manager.connectedCount(),2);
        QVERIFY(manager.setCameraExposure(5));
        QTRY_COMPARE(manager.cameraParameters(DeviceManager::MainCamera).value("exposureSeconds").toDouble(),5.0);
        QSignalSpy photos(&manager,&DeviceManager::solvePhotoReady);
        QSignalSpy saved(&manager,&DeviceManager::stillCaptured);
        QVERIFY(manager.setCameraExposure(7));
        QVERIFY(!manager.captureForSolve(2));
        QTRY_COMPARE(manager.cameraParameters(DeviceManager::MainCamera).value("exposureSeconds").toDouble(),7.0);
        QVERIFY(manager.setCameraExposure(5));
        QTRY_COMPARE(manager.cameraParameters(DeviceManager::MainCamera).value("exposureSeconds").toDouble(),5.0);
        QVERIFY(manager.captureForSolve(2));
        QVERIFY(!manager.captureForSolve(2));
        QVERIFY(!manager.setCameraGain(123));
        QTRY_COMPARE(photos.count(),1);
        QTRY_COMPARE(manager.cameraParameters(DeviceManager::MainCamera).value("exposureSeconds").toDouble(),5.0);
        QCOMPARE(saved.count(),0);
        QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
        QSignalSpy failed(&manager,&DeviceManager::solveCaptureFailed);
        QMetaObject::invokeMethod(backend,[backend]{backend->failStill=true;},Qt::BlockingQueuedConnection);
        QVERIFY(manager.captureForSolve(2));
        QTRY_COMPARE(failed.count(),1);
        QTRY_COMPARE(manager.cameraParameters(DeviceManager::MainCamera).value("exposureSeconds").toDouble(),5.0);
        QMetaObject::invokeMethod(backend,[backend]{backend->failStill=false;backend->holdStill=true;},Qt::BlockingQueuedConnection);
        QVERIFY(manager.captureForSolve(2));
        QVERIFY(!manager.startMountManualMove(0,720));
        QVERIFY(!manager.mountPark());
        QVERIFY(!manager.mountFindHome());
        manager.cancelSolveCapture();
        QTRY_COMPARE(failed.count(),2);
        QCOMPARE(photos.count(),1);
        QTRY_COMPARE(manager.cameraParameters(DeviceManager::MainCamera).value("exposureSeconds").toDouble(),5.0);
        QMetaObject::invokeMethod(backend,[backend]{backend->holdStill=false;},Qt::BlockingQueuedConnection);
        QVERIFY(manager.captureForSolve(2)); // Destruction drains the frame without posting to a deleted camera.
    }
    void guideCommandFailureReachesGlobalError() {
        auto *guide = new FakeCamera;
        DeviceManager manager(new FakeCamera, nullptr, guide, nullptr);
        QSignalSpy errors(&manager, &DeviceManager::lastErrorChanged);
        QMetaObject::invokeMethod(guide, [guide] { emit guide->commandFailed("guide exposure failed"); }, Qt::QueuedConnection);
        QTRY_COMPARE(errors.count(), 1);
        QCOMPARE(manager.lastError(), "guide exposure failed");
    }
    void stillMetadataAndFailure() {
        QTemporaryDir dir;
        auto *backend = new FakeCamera;
        DeviceManager manager(backend);
        manager.setStillSaveDirectory(dir.path());
        manager.setProperty("stillImageType", "DARK");
        manager.setProperty("stillTargetName", "M31");
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::MainCamera).size(), 2);
        QVERIFY(manager.selectDevice(DeviceManager::MainCamera, "camera-a"));
        QVERIFY(manager.setConnected(DeviceManager::MainCamera, true));
        QTRY_COMPARE(manager.connectedCount(), 1);
        QSignalSpy result(&manager, &DeviceManager::stillCaptured);
        QSignalSpy photo(&manager, &DeviceManager::mainPhotoReady);
        QVERIFY(manager.captureStill(DeviceManager::MainCamera));
        QVERIFY(!manager.captureStill(DeviceManager::MainCamera));
        manager.setProperty("stillImageType", "FLAT");
        manager.setProperty("stillTargetName", "M42");
        QTRY_COMPARE(result.count(), 1);
        QCOMPARE(photo.count(),1);
        QCOMPARE(photo[0][0].value<QVector<quint16>>(),QVector<quint16>({0,65535}));
        QCOMPARE(photo[0][1].toInt(),2);
        QCOMPARE(photo[0][2].toInt(),1);
        QVERIFY(result[0][2].toBool());
        QFile file(result[0][1].toString()); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        QVERIFY(bytes.contains("IMAGETYP= 'DARK'"));
        QVERIFY(bytes.contains("OBJECT  = 'M31'"));
        QVERIFY(bytes.contains("EXPTIME = 2.500000"));
        QVERIFY(bytes.contains("BAYERPAT= 'RGGB'"));
        QVERIFY(!backend->previewing);
        QMetaObject::invokeMethod(backend, [backend] { backend->failStill = true; }, Qt::BlockingQueuedConnection);
        QVERIFY(manager.captureStill(DeviceManager::MainCamera));
        QTRY_COMPARE(result.count(), 2);
        QVERIFY(!result[1][2].toBool());
        QCOMPARE(photo.count(),1);
        QVERIFY(manager.captureStill(DeviceManager::MainCamera));
        QTRY_COMPARE(result.count(), 3);
    }
    void connectionLifecycle() {
        DeviceManager manager(new FakeCamera);
        QCOMPARE(manager.connectedCount(), 0);
        QVERIFY(manager.candidatesForRole(DeviceManager::MainCamera).isEmpty());
        QVERIFY(!manager.setConnected(DeviceManager::MainCamera, true));
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::MainCamera).size(), 2);
        QVERIFY(manager.selectDevice(DeviceManager::MainCamera, "camera-a"));
        QVERIFY(manager.setConnected(DeviceManager::MainCamera, true));
        QVERIFY(manager.device(DeviceManager::MainCamera)["busy"].toBool());
        QVERIFY(!manager.selectDevice(DeviceManager::MainCamera, "camera-b"));
        QTRY_COMPARE(manager.connectedCount(), 1);
        QCOMPARE(manager.device(DeviceManager::MainCamera)["metrics"].toMap()["serial"].toString(), "TEST-SN");
        QVERIFY(!manager.selectDevice(DeviceManager::MainCamera, "camera-b"));
        QVERIFY(manager.setConnected(DeviceManager::MainCamera, false));
        QTRY_COMPARE(manager.connectedCount(), 0);
        QVERIFY(manager.device(DeviceManager::MainCamera)["metrics"].toMap().isEmpty());
    }
    void failureDoesNotConnect() {
        DeviceManager manager(new FakeCamera);
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::MainCamera).size(), 2);
        QVERIFY(!manager.selectDevice(DeviceManager::MainCamera, "missing"));
        QVERIFY(manager.selectDevice(DeviceManager::MainCamera, "camera-b"));
        QVERIFY(manager.setConnected(DeviceManager::MainCamera, true));
        QTRY_VERIFY(!manager.device(DeviceManager::MainCamera)["busy"].toBool());
        QCOMPARE(manager.connectedCount(), 0);
        QCOMPARE(manager.lastError(), "Open failed");
        QVERIFY(!manager.setConnected(DeviceManager::Mount, true));
        QVERIFY(manager.candidatesForRole(DeviceManager::Mount).isEmpty());
    }
    void lostConnectionClearsState() {
        auto *backend = new FakeCamera;
        DeviceManager manager(backend);
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::MainCamera).size(), 2);
        QVERIFY(manager.selectDevice(DeviceManager::MainCamera, "camera-a"));
        QVERIFY(manager.setConnected(DeviceManager::MainCamera, true));
        QTRY_COMPARE(manager.connectedCount(), 1);
        QMetaObject::invokeMethod(backend, [backend] { emit backend->connectionLost(QStringLiteral("Device removed")); });
        QTRY_COMPARE(manager.connectedCount(), 0);
        QVERIFY(manager.device(DeviceManager::MainCamera)["metrics"].toMap().isEmpty());
        QCOMPARE(manager.lastError(), "Device removed");
    }
    // 弹窗打开期间插入相机的契约：后端回报新枚举后，候选列表更新且
    // devicesChanged 再次发出——QML 侧的候选下拉框依赖该通知重取列表。
    void lateScanUpdatesCandidatesAndNotifies() {
        auto *backend = new FakeCamera;
        DeviceManager manager(backend);
        QSignalSpy spy(&manager, &DeviceManager::devicesChanged);
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::MainCamera).size(), 2);

        const int emissionsBefore = spy.count();
        QMetaObject::invokeMethod(backend, [backend] {
            emit backend->scanFinished(
                {QVariantMap{{"id", "camera-c"}, {"name", "Camera C"}}}, {});
        }, Qt::QueuedConnection);
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::MainCamera).size(), 1);
        QCOMPARE(manager.candidatesForRole(DeviceManager::MainCamera).at(0).toMap()["id"].toString(), "camera-c");
        QVERIFY(spy.count() > emissionsBefore);
        QVERIFY(!manager.scanning());
    }
    void cameraParametersRequireConnection() {
        DeviceManager manager(new FakeCamera);
        QVERIFY(!manager.setCameraExposure(1.0));
        QCOMPARE(manager.lastError(), "相机未连接");
        QVERIFY(!manager.setCameraGain(100));
        QCOMPARE(manager.lastError(), "相机未连接");
        QVERIFY(!manager.setCameraBinning(2));
        QCOMPARE(manager.lastError(), "相机未连接");
        QVERIFY(!manager.setCameraTargetTemperature(-10.0));
        QCOMPARE(manager.lastError(), "相机未连接");
        QVERIFY(!manager.setCameraHeatLevel(5));
        QCOMPARE(manager.lastError(), "相机未连接");
    }
    void cameraParametersForwardAndPublish() {
        auto *backend = new FakeCamera;
        DeviceManager manager(backend);
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::MainCamera).size(), 2);
        QVERIFY(manager.selectDevice(DeviceManager::MainCamera, "camera-a"));
        QVERIFY(manager.setConnected(DeviceManager::MainCamera, true));
        QTRY_COMPARE(manager.connectedCount(), 1);

        QVERIFY(manager.setCameraExposure(0.02));
        QTRY_COMPARE(backend->exposure, 20000u);
        QTRY_COMPARE(manager.device(DeviceManager::MainCamera)["metrics"].toMap()["exposureUs"].toUInt(), 20000u);
        QVERIFY(manager.setCameraGain(180));
        QTRY_COMPARE(backend->gain, 180u);
        QTRY_COMPARE(manager.device(DeviceManager::MainCamera)["metrics"].toMap()["gain"].toUInt(), 180u);
        QVERIFY(manager.setCameraOffset(20));
        QTRY_COMPARE(backend->offset, 20);
        QTRY_COMPARE(manager.device(DeviceManager::MainCamera)["metrics"].toMap()["offset"].toInt(), 20);
        QVERIFY(manager.setCameraBinning(2));
        QTRY_COMPARE(backend->binning, 2);
        QTRY_COMPARE(manager.device(DeviceManager::MainCamera)["metrics"].toMap()["binning"].toInt(), 2);
        QVERIFY(manager.setCameraCoolerEnabled(true));
        QTRY_VERIFY(backend->cooler);
        QTRY_VERIFY(manager.device(DeviceManager::MainCamera)["metrics"].toMap()["tecEnabled"].toBool());
        QVERIFY(manager.setCameraTargetTemperature(-10.0));
        QTRY_COMPARE(backend->targetTemp, -10.0);
        QTRY_COMPARE(manager.device(DeviceManager::MainCamera)["metrics"].toMap()["targetTemp"].toDouble(), -10.0);
        QVERIFY(manager.setCameraFanSpeed(0));
        QTRY_COMPARE(backend->fanSpeed, 0);
        QVERIFY(manager.setCameraHeatLevel(25));
        QTRY_COMPARE(backend->heatLevel, 25);
        QVERIFY(manager.setCameraGainMode(1));
        QTRY_COMPARE(backend->gainMode, 1);
        QVERIFY(manager.setCameraLowNoise(true));
        QTRY_VERIFY(backend->lowNoise);

        QVERIFY(!manager.setCameraExposure(0.0));
        QVERIFY(!manager.setCameraExposure(-1.0));
        QVERIFY(!manager.setCameraGain(-5));
        QVERIFY(!manager.setCameraOffset(-1));
        QVERIFY(!manager.setCameraBinning(0));
        QVERIFY(!manager.setCameraBinning(9));
        QVERIFY(!manager.setCameraFanSpeed(-2));
        QVERIFY(!manager.setCameraHeatLevel(-3));
        QVERIFY(!manager.setCameraGainMode(3));
        QCOMPARE(backend->exposure, 20000u);
    }
    void cameraPreviewControl() {
        auto *backend = new FakeCamera;
        DeviceManager manager(backend);
        manager.setAutoSaveStills(false);
        QVERIFY(!manager.startCameraPreview());
        QCOMPARE(manager.lastError(), "相机未连接");
        QVERIFY(!manager.captureSingleExposure());
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::MainCamera).size(), 2);
        QVERIFY(manager.selectDevice(DeviceManager::MainCamera, "camera-a"));
        QVERIFY(manager.setConnected(DeviceManager::MainCamera, true));
        QTRY_COMPARE(manager.connectedCount(), 1);
        QVERIFY(manager.captureSingleExposure());
        QTRY_VERIFY(backend->previewing);
        QVERIFY(!backend->previewContinuous);
        QTRY_VERIFY(manager.device(DeviceManager::MainCamera)["metrics"].toMap()["previewing"].toBool());
        QVERIFY(manager.stopCameraPreview());
        QTRY_VERIFY(!backend->previewing);
        QTRY_VERIFY(!manager.device(DeviceManager::MainCamera)["metrics"].toMap()["previewing"].toBool());
        QVERIFY(manager.startCameraPreview());
        QTRY_VERIFY(backend->previewContinuous);
        QVERIFY(manager.stopCameraPreview());
        QTRY_VERIFY(!backend->previewing);
    }
    void cameraStateAndFailuresReachUi() {
        auto *backend = new FakeCamera;
        DeviceManager manager(backend);
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::MainCamera).size(), 2);
        QVERIFY(manager.selectDevice(DeviceManager::MainCamera, "camera-a"));
        QVERIFY(manager.setConnected(DeviceManager::MainCamera, true));
        QTRY_COMPARE(manager.connectedCount(), 1);
        QMetaObject::invokeMethod(backend, [backend] {
            emit backend->stateChanged({{"currentTemp", -9.5}, {"tecPower", 62.0}, {"canTec", true}});
        });
        QTRY_COMPARE(manager.device(DeviceManager::MainCamera)["metrics"].toMap().value("currentTemp").toDouble(), -9.5);
        QTRY_COMPARE(manager.device(DeviceManager::MainCamera)["metrics"].toMap().value("tecPower").toDouble(), 62.0);
        QTRY_VERIFY(manager.device(DeviceManager::MainCamera)["metrics"].toMap().value("canTec").toBool());
        QMetaObject::invokeMethod(backend, [backend] { emit backend->commandFailed(QStringLiteral("设置曝光时间失败")); });
        QTRY_COMPARE(manager.lastError(), "设置曝光时间失败");
    }
};

QTEST_GUILESS_MAIN(DeviceManagerTest)
#include "tst_devicemanager.moc"
