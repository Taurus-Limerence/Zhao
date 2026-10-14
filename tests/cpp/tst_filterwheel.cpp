#include <QtTest>
#include <QSettings>
#include "devices/DeviceManager.h"
#include "devices/CameraBackend.h"
#include "devices/FilterWheelBackend.h"
#include "devices/ToupcamFilterWheel.h"

namespace {
int sdkPosition = 0;
int sdkCommand = 0;
int sdkSlots = 5;
#ifdef Q_OS_WIN
#define WHEEL_CALL __stdcall
#else
#define WHEEL_CALL
#endif
HRESULT WHEEL_CALL fakeWheelPut(HToupcam, unsigned option, int value) {
    if (option == TOUPCAM_OPTION_FILTERWHEEL_SLOT) sdkSlots = value;
    else sdkCommand = value;
    return 0;
}
HRESULT WHEEL_CALL fakeWheelGet(HToupcam, unsigned option, int *value) {
    *value = option == TOUPCAM_OPTION_FILTERWHEEL_SLOT ? sdkSlots : sdkPosition;
    return 0;
}
void WHEEL_CALL fakeWheelClose(HToupcam) {}
}

class FakeCamera final : public CameraBackend
{
public:
    void scan() override {
        emit scanFinished({QVariantMap{{"id", "camera-a"}, {"name", "Camera A"}}}, {});
    }
    void open(const QString &id) override {
        if (id == "camera-b") emit connectionFinished(false, {}, QStringLiteral("Open failed"));
        else emit connectionFinished(true, {{"serial", "TEST-SN"}}, {});
    }
    void close() override { emit connectionFinished(false, {}, {}); }
    // Parameter controls are out of scope here; the wheel is the subject.
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

class FakeFilterWheel final : public FilterWheelBackend
{
public:
    // When set, moveTo reports motion but never arrives until emitArrival is called.
    bool holdMotion = false;
    int lastPosition = -999;
    bool lastBidirectional = false;
    int slotCountRequested = 0;
    bool calibrated = false;
    QString serial;

    void scan() override {
        emit scanFinished({QVariantMap{{"id", "wheel-a"}, {"name", "Wheel A"}},
                           QVariantMap{{"id", "wheel-b"}, {"name", "Wheel B"}}}, {});
    }
    void open(const QString &id) override {
        if (id == "wheel-b") emit connectionFinished(false, {}, QStringLiteral("Open failed"));
        else emit connectionFinished(true, {{"model", "EFW-7"}, {"serial",serial}, {"slots", 7}, {"position", 2},
                                            {"moving", false}, {"calibrating", false}}, {});
    }
    void close() override { emit connectionFinished(false, {}, {}); }
    void moveTo(int position, bool bidirectional) override {
        lastPosition = position;
        lastBidirectional = bidirectional;
        emit stateChanged({{"slots", 7}, {"position", position}, {"moving", true}, {"calibrating", false}});
        if (!holdMotion) emitArrival(position);
    }
    void calibrate() override {
        calibrated = true;
        emit stateChanged({{"slots", 7}, {"position", -1}, {"moving", true}, {"calibrating", true}});
        emit stateChanged({{"slots", 7}, {"position", 0}, {"moving", false}, {"calibrating", false}});
    }
    void setSlotCount(int slotCount) override {
        slotCountRequested = slotCount;
        emit stateChanged({{"slots", slotCount}, {"position", 0}, {"moving", false}, {"calibrating", false}});
    }
    void emitArrival(int position) {
        emit stateChanged({{"slots", 7}, {"position", position}, {"moving", false}, {"calibrating", false}});
    }
};

class FilterWheelTest final : public QObject
{
    Q_OBJECT
private slots:
    void focusOffsetValidationAndWheelProfiles() {
        QSettings().clear();
        auto *wheel=new FakeFilterWheel; wheel->serial="profile-a"; DeviceManager dm(new FakeCamera,wheel);
        dm.refreshDevices(); QTRY_VERIFY(!dm.scanning()); QVERIFY(dm.selectDevice(DeviceManager::FilterWheel,"wheel-a"));
        QVERIFY(dm.setConnected(DeviceManager::FilterWheel,true)); QTRY_VERIFY(dm.device(DeviceManager::FilterWheel).value("connected").toBool());
        for (const auto &invalid : {QVariant(true),QVariant("12"),QVariant("bad"),QVariant(1.5),QVariant(1000001),QVariant(-1000001)})
            QVERIFY(!dm.setFilterWheelFocusOffset(2,invalid));
        QVERIFY(dm.setFilterWheelFocusOffset(2,1000000)); QVERIFY(dm.setFilterWheelFocusOffset(3,-1000000));
        QVERIFY(!dm.setFilterFocusReference(2)); QCOMPARE(dm.filterFocusSettings().value("reference").toInt(),1);
        QVERIFY(dm.setFilterWheelFocusOffset(2,120)); QVERIFY(dm.setFilterWheelFocusOffset(3,QVariant()));
        QVERIFY(dm.setFilterFocusReference(2)); QVERIFY(dm.setFilterFocusEnabled(true));
        QVERIFY(dm.setFilterWheelFocusOffset(3,"")); QVERIFY(dm.filterWheelChannels()[2].toMap().value("focusOffset").isNull());
        QVERIFY(dm.setConnected(DeviceManager::FilterWheel,false)); QTRY_VERIFY(!dm.device(DeviceManager::FilterWheel).value("connected").toBool());
        QMetaObject::invokeMethod(wheel,[wheel] { wheel->serial="profile-b"; },Qt::BlockingQueuedConnection);
        QVERIFY(dm.setConnected(DeviceManager::FilterWheel,true)); QTRY_VERIFY(dm.device(DeviceManager::FilterWheel).value("connected").toBool());
        QCOMPARE(dm.filterFocusSettings().value("enabled"),QVariant(false)); QCOMPARE(dm.filterFocusSettings().value("reference").toInt(),1);
        QVERIFY(dm.filterWheelChannels()[1].toMap().value("focusOffset").isNull());
        QVERIFY(dm.setConnected(DeviceManager::FilterWheel,false)); QTRY_VERIFY(!dm.device(DeviceManager::FilterWheel).value("connected").toBool());
        QMetaObject::invokeMethod(wheel,[wheel] { wheel->serial="profile-a"; },Qt::BlockingQueuedConnection);
        QVERIFY(dm.setConnected(DeviceManager::FilterWheel,true)); QTRY_VERIFY(dm.device(DeviceManager::FilterWheel).value("connected").toBool());
        QCOMPARE(dm.filterFocusSettings().value("enabled"),QVariant(true)); QCOMPARE(dm.filterFocusSettings().value("reference").toInt(),2);
        QCOMPARE(dm.filterWheelChannels()[0].toMap().value("focusOffset").toInt(),-120); QCOMPARE(wheel->lastPosition,-999);
        QSettings().clear();
    }
    void nullableFocusOffsetsRebaseAndPersistWithoutMotion() {
        QSettings().clear();
        auto *wheel=new FakeFilterWheel; DeviceManager manager(new FakeCamera,wheel);
        const auto settings=manager.property("filterFocusSettings").toMap();
        QCOMPARE(settings.value("enabled"),QVariant(false));
        QCOMPARE(settings.value("reference"),QVariant(1));
        QCOMPARE(manager.filterWheelChannels()[0].toMap().value("focusOffset"),QVariant(0));
        QVERIFY(manager.filterWheelChannels()[1].toMap().value("focusOffset").isNull());
        bool accepted=false;
        QVERIFY(QMetaObject::invokeMethod(&manager,"setFilterWheelFocusOffset",Q_RETURN_ARG(bool,accepted),Q_ARG(int,2),Q_ARG(QVariant,QVariant(120))));
        QVERIFY(accepted);
        QVERIFY(QMetaObject::invokeMethod(&manager,"setFilterFocusReference",Q_RETURN_ARG(bool,accepted),Q_ARG(int,2))); QVERIFY(accepted);
        QCOMPARE(manager.filterWheelChannels()[0].toMap().value("focusOffset").toInt(),-120);
        QCOMPARE(manager.filterWheelChannels()[1].toMap().value("focusOffset").toInt(),0);
        QVERIFY(QMetaObject::invokeMethod(&manager,"setFilterWheelFocusOffset",Q_RETURN_ARG(bool,accepted),Q_ARG(int,3),Q_ARG(QVariant,QVariant("junk")))); QVERIFY(!accepted);
        QVERIFY(QMetaObject::invokeMethod(&manager,"setFilterFocusReference",Q_RETURN_ARG(bool,accepted),Q_ARG(int,3))); QVERIFY(!accepted);
        QVERIFY(manager.setFilterWheelChannel(1,"L",123,4));
        DeviceManager reader(new FakeCamera,new FakeFilterWheel);
        QCOMPARE(reader.property("filterFocusSettings").toMap().value("reference").toInt(),2);
        QCOMPARE(reader.filterWheelChannels()[0].toMap().value("focusOffset").toInt(),-120);
        QCOMPARE(wheel->lastPosition,-999);
        QSettings().clear();
    }
    void initTestCase() {
        // QSettings() inside DeviceManager needs an identity; keep tests off the real app store.
        QCoreApplication::setOrganizationName(QStringLiteral("ToupTek Astro"));
        QCoreApplication::setApplicationName(QStringLiteral("StellaMora-FilterWheelTests"));
        QSettings().remove(QStringLiteral("filterWheel/channels"));
    }
    void cleanupTestCase() {
        QSettings().remove(QStringLiteral("filterWheel/channels"));
    }
    void cameraOnlyManagerLeavesWheelUnsupported() {
        DeviceManager manager(new FakeCamera);
        QCOMPARE(manager.device(DeviceManager::FilterWheel)["supported"].toBool(), false);
        QVERIFY(manager.candidatesForRole(DeviceManager::FilterWheel).isEmpty());
        QVERIFY(!manager.setConnected(DeviceManager::FilterWheel, true));
        QCOMPARE(manager.lastError(), QStringLiteral("该设备驱动尚未接入"));
        QVERIFY(!manager.moveFilterWheel(0));
        QVERIFY(!manager.calibrateFilterWheel());
        QVERIFY(!manager.setFilterWheelSlots(7));
        QCOMPARE(manager.lastError(), QStringLiteral("滤镜轮未连接"));
        manager.refreshDevices();
        QTRY_VERIFY(!manager.scanning());
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::MainCamera).size(), 1);
    }

    void sdkConnectionDefaultsToSevenSlots() {
        ToupcamFilterWheel wheel;
        wheel.m_handle = reinterpret_cast<HToupcam>(quintptr(1));
        wheel.m_putOption = &fakeWheelPut;
        wheel.m_getOption = &fakeWheelGet;
        wheel.m_close = &fakeWheelClose;
        sdkSlots = 5;
        sdkPosition = 0;
        QVariantMap information;
        QString error;
        QVERIFY(wheel.refreshCapability(information, error));
        QCOMPARE(information.value("slots").toInt(), 7);
        QCOMPARE(wheel.m_position, 0);
        wheel.setSlotCount(5);
        QCOMPARE(wheel.m_slots, 5);
        wheel.setSlotCount(8);
        QCOMPARE(wheel.m_slots, 8);
        QVERIFY(wheel.refreshCapability(information, error));
        QCOMPARE(information.value("slots").toInt(), 7);
    }
    void sdkMoveRequiresRequestedPosition() {
        ToupcamFilterWheel wheel;
        wheel.m_handle=reinterpret_cast<HToupcam>(quintptr(1));
        wheel.m_slots=7;
        wheel.m_putOption=&fakeWheelPut;
        wheel.m_getOption=&fakeWheelGet;
        wheel.m_close=&fakeWheelClose;
        wheel.moveTo(5,true);
        QCOMPARE(sdkCommand,261);
        sdkPosition=4;
        wheel.pollMovement();
        QVERIFY(wheel.m_moving);
        sdkPosition=5;
        wheel.pollMovement();
        QVERIFY(!wheel.m_moving);
        QCOMPARE(wheel.m_position,5);
        wheel.moveTo(3,false);
        QCOMPARE(sdkCommand,3);
        sdkPosition=-1;
        wheel.pollMovement();
        QVERIFY(wheel.m_moving);
        sdkPosition=3;
        wheel.pollMovement();
        QVERIFY(!wheel.m_moving);
    }
    void sdkInvalidPositionDisconnects() {
        ToupcamFilterWheel wheel;
        wheel.m_handle = reinterpret_cast<HToupcam>(quintptr(1));
        wheel.m_slots = 7;
        wheel.m_putOption = &fakeWheelPut;
        wheel.m_getOption = &fakeWheelGet;
        wheel.m_close = &fakeWheelClose;
        QSignalSpy lost(&wheel, &FilterWheelBackend::connectionLost);
        wheel.moveTo(5, true);
        sdkPosition = 7;
        wheel.pollMovement();
        QCOMPARE(lost.count(), 1);
        QVERIFY(!wheel.m_handle);
        QVERIFY(!wheel.m_moving);
    }
    void queuedWheelCommandRejectsSecondAction() {
        auto *wheel = new FakeFilterWheel;
        DeviceManager manager(new FakeCamera, wheel);
        manager.refreshDevices();
        QTRY_VERIFY(!manager.scanning());
        QVERIFY(manager.selectDevice(DeviceManager::FilterWheel, "wheel-a"));
        QVERIFY(manager.setConnected(DeviceManager::FilterWheel, true));
        QTRY_VERIFY(manager.device(DeviceManager::FilterWheel)["connected"].toBool());
        QVERIFY(manager.moveFilterWheel(5));
        // No main-thread event delivery yet: the first command is still pending.
        QVERIFY(!manager.moveFilterWheel(3));
        QVERIFY(!manager.calibrateFilterWheel());
        QVERIFY(!manager.setFilterWheelSlots(5));
    }
    void wheelLifecycle() {
        auto *wheel = new FakeFilterWheel;
        DeviceManager manager(new FakeCamera, wheel);
        QCOMPARE(manager.device(DeviceManager::FilterWheel)["supported"].toBool(), true);
        QVERIFY(manager.candidatesForRole(DeviceManager::FilterWheel).isEmpty());
        QVERIFY(!manager.setConnected(DeviceManager::FilterWheel, true));
        QCOMPARE(manager.lastError(), QStringLiteral("请先选择滤镜轮"));
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::FilterWheel).size(), 2);
        QTRY_VERIFY(!manager.scanning());
        QVERIFY(!manager.selectDevice(DeviceManager::FilterWheel, "missing"));
        QCOMPARE(manager.lastError(), QStringLiteral("所选滤镜轮不可用，请刷新设备列表"));
        QVERIFY(manager.selectDevice(DeviceManager::FilterWheel, "wheel-a"));
        QVERIFY(manager.setConnected(DeviceManager::FilterWheel, true));
        QTRY_VERIFY(manager.device(DeviceManager::FilterWheel)["connected"].toBool());
        QCOMPARE(manager.connectedCount(), 1);

        auto metrics = manager.device(DeviceManager::FilterWheel)["metrics"].toMap();
        QCOMPARE(metrics["slots"].toInt(), 7);
        QCOMPARE(metrics["position"].toInt(), 2);
        QCOMPARE(metrics["bidirectional"].toBool(), false);
        QCOMPARE(metrics["model"].toString(), "EFW-7");

        QVERIFY(manager.moveFilterWheel(4));
        QTRY_COMPARE(manager.device(DeviceManager::FilterWheel)["metrics"].toMap()["position"].toInt(), 4);
        QCOMPARE(wheel->lastPosition, 4);
        QCOMPARE(wheel->lastBidirectional, false);

        QVERIFY(!manager.moveFilterWheel(7));
        QCOMPARE(manager.lastError(), QStringLiteral("滤镜位置超出范围"));
        QVERIFY(!manager.moveFilterWheel(-1));
        QCOMPARE(manager.lastError(), QStringLiteral("滤镜位置超出范围"));

        QVERIFY(manager.setFilterWheelBidirectional(true));
        QCOMPARE(manager.device(DeviceManager::FilterWheel)["metrics"].toMap()["bidirectional"].toBool(), true);
        QVERIFY(manager.moveFilterWheel(1));
        QTRY_COMPARE(manager.device(DeviceManager::FilterWheel)["metrics"].toMap()["position"].toInt(), 1);
        QCOMPARE(wheel->lastBidirectional, true);

        QVERIFY(manager.calibrateFilterWheel());
        QTRY_VERIFY(wheel->calibrated);
        QTRY_COMPARE(manager.device(DeviceManager::FilterWheel)["metrics"].toMap()["position"].toInt(), 0);
        QCOMPARE(manager.device(DeviceManager::FilterWheel)["metrics"].toMap()["calibrating"].toBool(), false);

        QVERIFY(!manager.setFilterWheelSlots(6));
        QCOMPARE(manager.lastError(), QStringLiteral("轮孔数量仅支持 5 孔、7 孔或 8 孔"));
        QVERIFY(manager.setFilterWheelSlots(5));
        QTRY_COMPARE(wheel->slotCountRequested, 5);
        QTRY_COMPARE(manager.device(DeviceManager::FilterWheel)["metrics"].toMap()["slots"].toInt(), 5);

        QVERIFY(manager.setConnected(DeviceManager::FilterWheel, false));
        QTRY_VERIFY(!manager.device(DeviceManager::FilterWheel)["connected"].toBool());
        QVERIFY(manager.device(DeviceManager::FilterWheel)["metrics"].toMap().isEmpty());
        QVERIFY(!manager.moveFilterWheel(1));
        QCOMPARE(manager.lastError(), QStringLiteral("滤镜轮未连接"));
    }

    void motionBlocksFurtherActions() {
        auto *wheel = new FakeFilterWheel;
        DeviceManager manager(new FakeCamera, wheel);
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::FilterWheel).size(), 2);
        QVERIFY(manager.selectDevice(DeviceManager::FilterWheel, "wheel-a"));
        QVERIFY(manager.setConnected(DeviceManager::FilterWheel, true));
        QTRY_VERIFY(manager.device(DeviceManager::FilterWheel)["connected"].toBool());

        wheel->holdMotion = true;
        QVERIFY(manager.moveFilterWheel(3));
        QTRY_COMPARE(manager.device(DeviceManager::FilterWheel)["metrics"].toMap()["moving"].toBool(), true);
        QVERIFY(!manager.moveFilterWheel(4));
        QCOMPARE(manager.lastError(), QStringLiteral("滤镜轮动作进行中，请稍候"));
        QVERIFY(!manager.calibrateFilterWheel());
        QCOMPARE(manager.lastError(), QStringLiteral("滤镜轮动作进行中，请稍候"));
        QVERIFY(!manager.setFilterWheelSlots(5));
        QCOMPARE(manager.lastError(), QStringLiteral("滤镜轮动作进行中，请稍候"));

        wheel->emitArrival(3);
        QTRY_COMPARE(manager.device(DeviceManager::FilterWheel)["metrics"].toMap()["moving"].toBool(), false);
        QVERIFY(manager.calibrateFilterWheel());
    }

    void wheelFailureDoesNotConnect() {
        DeviceManager manager(new FakeCamera, new FakeFilterWheel);
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::FilterWheel).size(), 2);
        QVERIFY(manager.selectDevice(DeviceManager::FilterWheel, "wheel-b"));
        QVERIFY(manager.setConnected(DeviceManager::FilterWheel, true));
        QTRY_VERIFY(!manager.device(DeviceManager::FilterWheel)["busy"].toBool());
        QCOMPARE(manager.connectedCount(), 0);
        QCOMPARE(manager.lastError(), QStringLiteral("Open failed"));
        QVERIFY(!manager.moveFilterWheel(0));
        QCOMPARE(manager.lastError(), QStringLiteral("滤镜轮未连接"));
    }

    void lostConnectionClearsWheelState() {
        auto *wheel = new FakeFilterWheel;
        DeviceManager manager(new FakeCamera, wheel);
        manager.refreshDevices();
        QTRY_COMPARE(manager.candidatesForRole(DeviceManager::FilterWheel).size(), 2);
        QVERIFY(manager.selectDevice(DeviceManager::FilterWheel, "wheel-a"));
        QVERIFY(manager.setConnected(DeviceManager::FilterWheel, true));
        QTRY_COMPARE(manager.connectedCount(), 1);
        QMetaObject::invokeMethod(wheel, [wheel] { emit wheel->connectionLost(QStringLiteral("Wheel removed")); });
        QTRY_COMPARE(manager.connectedCount(), 0);
        QVERIFY(manager.device(DeviceManager::FilterWheel)["metrics"].toMap().isEmpty());
        QCOMPARE(manager.lastError(), QStringLiteral("Wheel removed"));
        QVERIFY(!manager.moveFilterWheel(0));
        QCOMPARE(manager.lastError(), QStringLiteral("滤镜轮未连接"));
    }

    void channelStoreValidatesAndPersists() {
        QVariantList channels;
        for (int slot = 1; slot <= 8; ++slot)
            channels.append(QVariantMap{{"slot", slot}, {"suffix", ""}, {"gain", 100}, {"exposure", 3.0}});
        DeviceManager writer(new FakeCamera, new FakeFilterWheel);
        QCOMPARE(writer.filterWheelChannels().size(), 8);
        QVERIFY(writer.setFilterWheelChannel(1, "  L ", 120, 15.5));
        QVERIFY(writer.setFilterWheelChannel(3, "G", 200, 300.0));

        // A fresh manager reads the same store back: values survive restarts.
        DeviceManager reader(new FakeCamera, new FakeFilterWheel);
        channels = reader.filterWheelChannels();
        QCOMPARE(channels.size(), 8);
        auto first = channels[0].toMap();
        QCOMPARE(first["slot"].toInt(), 1);
        QCOMPARE(first["suffix"].toString(), QStringLiteral("L"));
        QCOMPARE(first["gain"].toInt(), 120);
        QCOMPARE(first["exposure"].toDouble(), 15.5);
        auto untouched = channels[1].toMap();
        QCOMPARE(untouched["suffix"].toString(), QString());
        QCOMPARE(untouched["gain"].toInt(), 100);

        QVERIFY(!writer.setFilterWheelChannel(0, "X", 100, 1.0));
        QCOMPARE(writer.lastError(), QStringLiteral("滤镜孔位超出范围"));
        QVERIFY(!writer.setFilterWheelChannel(9, "X", 100, 1.0));
        QVERIFY(!writer.setFilterWheelChannel(2, "X", -1, 1.0));
        QCOMPARE(writer.lastError(), QStringLiteral("增益不能为负数"));
        QVERIFY(!writer.setFilterWheelChannel(2, "X", 100, 0.0));
        QCOMPARE(writer.lastError(), QStringLiteral("曝光时间需在 0 ~ 3600 秒之间"));
        QVERIFY(!writer.setFilterWheelChannel(2, "X", 100, 3601.0));

        // Restore defaults for slot 1/3 so repeated runs stay deterministic.
        QVERIFY(writer.setFilterWheelChannel(1, "", 100, 3.0));
        QVERIFY(writer.setFilterWheelChannel(3, "", 100, 3.0));
    }
};

QTEST_GUILESS_MAIN(FilterWheelTest)
#include "tst_filterwheel.moc"
