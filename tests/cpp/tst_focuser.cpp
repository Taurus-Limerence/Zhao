#include <QtTest>
#include "devices/DeviceManager.h"
#include "devices/CameraBackend.h"
#include "devices/FilterWheelBackend.h"
#include "devices/FocuserBackend.h"

class FakeCamera final : public CameraBackend
{
public:
    void scan() override { emit scanFinished({QVariantMap{{"id", "camera-a"}, {"name", "Camera A"}}}, {}); }
    void open(const QString &id) override { emit connectionFinished(!id.isEmpty(), {{"serial", "SN"}}, {}); }
    void close() override { emit connectionFinished(false, {}, {}); }
    void setExposure(unsigned) override {}
    void setGain(unsigned) override {}
    void setOffset(int) override {}
    void setBinning(int) override {}
    void setCoolerEnabled(bool) override {}
    void setTargetTemperature(double) override {}
    void setFanSpeed(int) override {}
    void setHeatLevel(int) override {}
    void setGainMode(int) override {}
    void setLowNoise(bool) override {}
    void startPreview(bool) override {}
    void stopPreview() override {}
};

class FakeWheel final : public FilterWheelBackend
{
public:
    void scan() override { emit scanFinished({}, {}); }
    void open(const QString &) override { emit connectionFinished(false, {}, {}); }
    void close() override {}
    void moveTo(int, bool) override {}
    void calibrate() override {}
    void setSlotCount(int) override {}
};

class FakeFocuser final : public FocuserBackend
{
public:
    int lastTarget = -1;
    int halts = 0;
    bool reversedWritten = false;
    bool holdMoving = false;
    int maxStepWritten = -1;
    int zeros = 0;
    int currentSetTo = -1;

    void scan() override {
        emit scanFinished({QVariantMap{{"id", "aaf-a"}, {"name", "AAF One"}},
                           QVariantMap{{"id", "aaf-b"}, {"name", "AAF Two"}}}, {});
    }
    void open(const QString &id) override {
        if (id == "aaf-b") emit connectionFinished(false, {}, QStringLiteral("Open failed"));
        else emit connectionFinished(true, {{"serial", "AAF-SN"}, {"maxStep", 60000}, {"position", 5000},
                                            {"moving", false}, {"reversed", false}, {"temperature", 21.5}}, {});
    }
    void close() override { emit connectionFinished(false, {}, {}); }
    void moveTo(int position) override {
        lastTarget = position;
        emit stateChanged({{"position", position}, {"moving", true}});
        if (!holdMoving)
            emit stateChanged({{"position", position}, {"moving", false}, {"temperature", 21.6}});
    }
    void halt() override { ++halts; }
    void setMaxStep(int maxStep) override {
        maxStepWritten = maxStep;
        emit stateChanged({{"maxStep", maxStep}});
    }
    void setZero() override {
        ++zeros;
        emit stateChanged({{"position", 0}, {"moving", false}});
    }
    void setCurrentPosition(int position) override {
        currentSetTo = position;
        emit stateChanged({{"position", position}, {"moving", false}});
    }
    void setReversed(bool reversed) override {
        reversedWritten = reversed;
        emit stateChanged({{"reversed", reversed}});
    }
};

class FocuserTest final : public QObject
{
    Q_OBJECT
private slots:
    void focuserLifecycle() {
        auto *focuser = new FakeFocuser;
        DeviceManager manager(new FakeCamera, new FakeWheel, focuser);
        QCOMPARE(manager.device(DeviceManager::Focuser)["supported"].toBool(), true);
        QVERIFY(!manager.setConnected(DeviceManager::Focuser, true));
        QCOMPARE(manager.lastError(), QStringLiteral("请先选择电调焦"));
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::Focuser).size(), 2);
        QVERIFY(manager.selectDevice(DeviceManager::Focuser, "aaf-a"));
        QVERIFY(manager.setConnected(DeviceManager::Focuser, true));
        QTRY_VERIFY(manager.device(DeviceManager::Focuser)["connected"].toBool());

        auto metrics = manager.device(DeviceManager::Focuser)["metrics"].toMap();
        QCOMPARE(metrics["maxStep"].toInt(), 60000);
        QCOMPARE(metrics["position"].toInt(), 5000);
        QCOMPARE(metrics["temperature"].toDouble(), 21.5);

        // Absolute move lands in metrics via stateChanged.
        QVERIFY(manager.moveFocuser(5100));
        QTRY_COMPARE(manager.device(DeviceManager::Focuser)["metrics"].toMap()["position"].toInt(), 5100);
        QCOMPARE(focuser->lastTarget, 5100);
        QTRY_VERIFY(!manager.device(DeviceManager::Focuser)["metrics"].toMap()["moving"].toBool());

        QVERIFY(manager.haltFocuser());
        QTRY_COMPARE(focuser->halts, 1);
        QVERIFY(manager.setFocuserReversed(true));
        QTRY_VERIFY(manager.device(DeviceManager::Focuser)["metrics"].toMap()["reversed"].toBool());
        QVERIFY(focuser->reversedWritten);

        // While moving, metrics keep streaming and disconnect clears them.
        focuser->holdMoving = true;
        QVERIFY(manager.moveFocuser(5200));
        QTRY_COMPARE(manager.device(DeviceManager::Focuser)["metrics"].toMap()["moving"].toBool(), true);
        QVERIFY(manager.setConnected(DeviceManager::Focuser, false));
        QTRY_VERIFY(manager.device(DeviceManager::Focuser)["metrics"].toMap().isEmpty());
        QVERIFY(!manager.moveFocuser(5300));
        QCOMPARE(manager.lastError(), QStringLiteral("电调焦未连接"));
    }

    void maxStepAndZeroControls() {
        auto *focuser = new FakeFocuser;
        DeviceManager manager(new FakeCamera, new FakeWheel, focuser);
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::Focuser).size(), 2);
        QVERIFY(manager.selectDevice(DeviceManager::Focuser, "aaf-a"));
        QVERIFY(manager.setConnected(DeviceManager::Focuser, true));
        QTRY_VERIFY(manager.device(DeviceManager::Focuser)["connected"].toBool());

        // Max travel write lands in metrics.
        QVERIFY(manager.setFocuserMaxStep(45000));
        QTRY_COMPARE(manager.device(DeviceManager::Focuser)["metrics"].toMap()["maxStep"].toInt(), 45000);
        QCOMPARE(focuser->maxStepWritten, 45000);
        QVERIFY(!manager.setFocuserMaxStep(0));
        QCOMPARE(manager.lastError(), QStringLiteral("最大行程必须为正数"));

        // Zero re-anchors the position counter without motion.
        QVERIFY(manager.resetFocuserPosition());
        QTRY_COMPARE(focuser->zeros, 1);
        QTRY_COMPARE(manager.device(DeviceManager::Focuser)["metrics"].toMap()["position"].toInt(), 0);

        // After zeroing, a further zero request is refused (already zero).
        QVERIFY(!manager.resetFocuserPosition());
        QCOMPARE(manager.lastError(), QStringLiteral("当前位置已为零或未知，无需清零"));

        // Setting the current spot to an arbitrary reading re-anchors it.
        QVERIFY(manager.setCurrentFocuserPosition(1234));
        QTRY_COMPARE(focuser->currentSetTo, 1234);
        QTRY_COMPARE(manager.device(DeviceManager::Focuser)["metrics"].toMap()["position"].toInt(), 1234);
        QVERIFY(!manager.setCurrentFocuserPosition(-1));
        QCOMPARE(manager.lastError(), QStringLiteral("设定位置不能为负数"));
    }

    void invalidSelectionsRefused() {
        auto *focuser = new FakeFocuser;
        DeviceManager manager(new FakeCamera, new FakeWheel, focuser);
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::Focuser).size(), 2);
        QVERIFY(!manager.selectDevice(DeviceManager::Focuser, "missing"));
        QCOMPARE(manager.lastError(), QStringLiteral("所选滤镜轮不可用，请刷新设备列表"));
        QVERIFY(manager.selectDevice(DeviceManager::Focuser, "aaf-b"));
        QVERIFY(manager.setConnected(DeviceManager::Focuser, true));
        QTRY_COMPARE(manager.connectedCount(), 0);
        QTRY_COMPARE(manager.lastError(), QStringLiteral("Open failed"));
        QVERIFY(!manager.moveFocuser(100));
        QCOMPARE(manager.lastError(), QStringLiteral("电调焦未连接"));
    }

    void negativePositionRefused() {
        auto *focuser = new FakeFocuser;
        DeviceManager manager(new FakeCamera, new FakeWheel, focuser);
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::Focuser).size(), 2);
        QVERIFY(manager.selectDevice(DeviceManager::Focuser, "aaf-a"));
        QVERIFY(manager.setConnected(DeviceManager::Focuser, true));
        QTRY_VERIFY(manager.device(DeviceManager::Focuser)["connected"].toBool());
        QVERIFY(!manager.moveFocuser(-5));
        QCOMPARE(manager.lastError(), QStringLiteral("调焦位置不能为负数"));
        QCOMPARE(focuser->lastTarget, -1);
    }
};

QTEST_GUILESS_MAIN(FocuserTest)
#include "tst_focuser.moc"
