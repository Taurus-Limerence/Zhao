#include <QtTest>
#include "devices/DeviceManager.h"
#include "devices/CameraBackend.h"
#include "devices/MountBackend.h"
#include "devices/AscomMount.h"
#include "devices/MountCoordinates.h"

class IdleCamera final : public CameraBackend {
public:
    void scan() override { emit scanFinished({}, {}); }
    void open(const QString &) override {}
    void close() override {}
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

class TestMount final : public MountBackend {
public:
    QString gotoError;
    double targetRa = -1, targetDec = -91;
    QString slewToJ2000(double ra, double dec) override {
        targetRa = ra; targetDec = dec;
        if (gotoError.isEmpty()) emit stateChanged({{"gotoState", "Slewing"}});
        return gotoError;
    }
    QString stopSlew() override { emit stateChanged({{"gotoState", "Stopped"}}); return {}; }
    int moves = 0;
    int stops = 0;
    int syncs = 0;
    QString syncObserver(bool, double, double) override { ++syncs; return QStringLiteral("SiteLatitude 回读失败"); }
    QString startManualMove(int, double) override { ++moves; return {}; }
    QString stopManualMove() override { ++stops; return {}; }
    void scan() override {
        emit scanFinished({QVariantMap{{"id", "test.mount"}, {"name", "Test Mount"}},
                           QVariantMap{{"id", "test.failure"}, {"name", "Failing Mount"}}}, {});
    }
    void open(const QString &id) override {
        if (id == "test.failure") emit connectionFinished(false, {}, "Connection refused");
        else emit connectionFinished(true, {{"rightAscension", 12.5}, {"declination", -30.0},
                                             {"tracking", true}, {"canPulseGuide", true}}, {});
    }
    void close() override { emit connectionFinished(false, {}, {}); }
    void setup(const QString &id) override { emit setupFinished(id == "test.mount" ? QString{} : QStringLiteral("Setup failed")); }
};

class MountTest final : public QObject {
    Q_OBJECT
private slots:
    void skyGotoDispatchAndGuards() {
        auto *backend = new TestMount;
        DeviceManager manager(new IdleCamera, nullptr, nullptr, backend, nullptr);
        manager.refreshDevices();
        QTRY_VERIFY(!manager.scanning());
        QVERIFY(manager.selectDevice(0, "test.mount"));
        QVERIFY(manager.setConnected(0, true));
        QTRY_VERIFY(manager.device(0).value("connected").toBool());
        QVERIFY(!manager.slewMountToJ2000(qQNaN(), 0));
        QVERIFY(!manager.slewMountToJ2000(24, 0));
        QVERIFY(!manager.slewMountToJ2000(10, 91));
        manager.setMountGuiding(true);
        QVERIFY(!manager.slewMountToJ2000(12, 30));
        manager.setMountGuiding(false);
        QVERIFY(manager.slewMountToJ2000(12.125, 30.5));
        QVERIFY(!manager.slewMountToJ2000(13, 30));
        QVERIFY(!manager.startMountManualMove(0, 20));
        QVERIFY(!manager.mountPulseGuide(0, 100));
        QTRY_COMPARE(manager.device(0).value("metrics").toMap().value("gotoState").toString(), "Slewing");
        QCOMPARE(backend->targetRa, 12.125);
        QCOMPARE(backend->targetDec, 30.5);
        QVERIFY(manager.mountAbortSlew());
        QTRY_COMPARE(manager.device(0).value("metrics").toMap().value("gotoState").toString(), "Stopped");
        QMetaObject::invokeMethod(backend, [backend] { backend->gotoError = "Driver rejected"; }, Qt::BlockingQueuedConnection);
        QVERIFY(manager.slewMountToJ2000(14, 20));
        QTRY_COMPARE(manager.lastError(), "Driver rejected");
        QCOMPARE(manager.device(0).value("metrics").toMap().value("gotoState").toString(), "Failed");
        QVERIFY(manager.slewMountToJ2000(14, 20)); // Rejection releases the command guard.
    }
    void coordinateTransform() {
        double ra, dec, altitude;
        QVERIFY(!MountCoordinates::convert(12, 30, 0, {}, true, ra, dec, altitude).isEmpty());
        QVERIFY(!MountCoordinates::convert(12, 30, 1, {}, true, ra, dec, altitude).isEmpty());
#ifdef Q_OS_WIN
        const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        QVERIFY(SUCCEEDED(hr));
        const QVariantMap site{{"siteLatitude", 22.3}, {"siteLongitude", 114.2}, {"siteElevation", 30.0}};
        const QString error = MountCoordinates::convert(12, 30, 2, site, true, ra, dec, altitude);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(qAbs(ra - 12) < 1e-8);
        QVERIFY(qAbs(dec - 30) < 1e-8);
        QVERIFY(altitude >= -90 && altitude <= 90);
        QVERIFY(MountCoordinates::convert(12, 30, 1, site, true, ra, dec, altitude).isEmpty());
        QVERIFY(qAbs(ra - 12) > 1e-5); // Topocentric must not silently use J2000 unchanged.
        double backRa, backDec;
        QVERIFY(MountCoordinates::convert(ra, dec, 1, site, false, backRa, backDec, altitude).isEmpty());
        QVERIFY(qAbs(backRa - 12) < 1e-5);
        QVERIFY(qAbs(backDec - 30) < 1e-4);
        CoUninitialize();
#endif
    }
    void gotoRejectsUnsafeOrUnknownDriverState() {
        const QVariantMap ready{{"AtPark", false}, {"Slewing", false}, {"IsPulseGuiding", false},
                                {"Tracking", true}, {"CanSlewAsync", true}};
        QVERIFY(MountCoordinates::gotoStateError(ready).isEmpty());
        for (const auto &key : {"AtPark", "Slewing", "IsPulseGuiding", "Tracking", "CanSlewAsync"}) {
            auto state = ready;
            state.remove(key);
            QVERIFY(!MountCoordinates::gotoStateError(state).isEmpty());
            state = ready;
            state[key] = !ready.value(key).toBool();
            QVERIFY(!MountCoordinates::gotoStateError(state).isEmpty());
        }
    }
    void gotoAllowsTrackingToBeEnabled() {
        QVariantMap state{{"AtPark", false}, {"Slewing", false}, {"IsPulseGuiding", false},
                          {"Tracking", false}, {"CanSetTracking", true}, {"CanSlewAsync", true}};
        QVERIFY(MountCoordinates::gotoStateError(state).isEmpty());
        state["CanSetTracking"] = false;
        QVERIFY(!MountCoordinates::gotoStateError(state).isEmpty());
        state["Tracking"] = true;
        QVERIFY(MountCoordinates::gotoStateError(state).isEmpty());
    }
    void skyGotoRequiresConnection() {
        DeviceManager manager(new IdleCamera, nullptr, nullptr, new TestMount, nullptr);
        bool accepted = true;
        QVERIFY(QMetaObject::invokeMethod(&manager, "slewMountToJ2000", Qt::DirectConnection,
            Q_RETURN_ARG(bool, accepted), Q_ARG(double, 12.0), Q_ARG(double, 30.0)));
        QVERIFY(!accepted);
        QVERIFY(!manager.lastError().isEmpty());
    }
    void manualMovementGuards() {
        auto *backend = new TestMount;
        DeviceManager manager(new IdleCamera, nullptr, nullptr, backend, nullptr);
        QVERIFY(!manager.startMountManualMove(0, 1.0));
        manager.refreshDevices();
        QTRY_VERIFY(!manager.scanning());
        QVERIFY(manager.selectDevice(0, "test.mount"));
        QVERIFY(manager.setConnected(0, true));
        QTRY_VERIFY(manager.device(0).value("connected").toBool());
        QVERIFY(!manager.syncMountObserver(false, 181, 22));
        QVERIFY(!manager.syncMountObserver(false, 114, qQNaN()));
        QCOMPARE(backend->syncs, 0);
        QVERIFY(!manager.syncMountObserver(false, 114, 22));
        QCOMPARE(backend->syncs, 1);
        QCOMPARE(manager.lastError(), QStringLiteral("SiteLatitude 回读失败"));
        manager.setMountGuiding(true);
        QVERIFY(!manager.syncMountObserver(true, 0, 0));
        QCOMPARE(backend->syncs, 1);
        manager.setMountGuiding(false);
        QVERIFY(!manager.startMountManualMove(2, 1.0));
        QVERIFY(!manager.startMountManualMove(0, qQNaN()));
        QVERIFY(!manager.startMountManualMove(0, 1441));
        QVERIFY(manager.startMountManualMove(0, 1440));
        QCOMPARE(backend->moves, 1);
        QVERIFY(!manager.startMountManualMove(1, 20));
        manager.setMountGuiding(true);
        QCOMPARE(backend->stops, 1);
        QVERIFY(!manager.mountManualMoving());
        QVERIFY(!manager.startMountManualMove(0, 20));
        manager.setMountGuiding(false);
        QVERIFY(manager.startMountManualMove(1, -20));
        QSignalSpy stopRequested(&manager, &DeviceManager::mountMotionStopRequested);
        QVERIFY(manager.mountAbortSlew());
        QCOMPARE(stopRequested.count(), 1);
        QCOMPARE(backend->stops, 2);
        QVERIFY(!manager.mountManualMoving());
    }
    void guideRateUnits() {
        QVERIFY(qAbs(AscomMount::guideRateToDegrees(0.5) - 0.0020890394444444444) < 1e-15);
        QVERIFY(qAbs(AscomMount::guideRateFromDegrees(0.0020890394444444444) - 0.5) < 1e-12);
        QVERIFY(qAbs(AscomMount::guideRateToDegrees(0.1) - 0.0004178078888888889) < 1e-15);
        QVERIFY(qAbs(AscomMount::guideRateFromDegrees(0.003760271) - 0.9) < 1e-12);
    }
    void connectionAndSetupAreSeparate() {
        DeviceManager manager(new IdleCamera, nullptr, nullptr, new TestMount, nullptr);
        QVERIFY(!manager.setConnected(0, true));
        QVERIFY(!manager.openMountSetup());
        manager.refreshDevices();
        QTRY_VERIFY(!manager.scanning());
        QCOMPARE(manager.candidatesForRole(0).size(), 2);
        QVERIFY(manager.device(0).value("supported").toBool());
        QVERIFY(manager.selectDevice(0, "test.mount"));
        QVERIFY(manager.openMountSetup());
        QVERIFY(manager.device(0).value("busy").toBool());
        QVERIFY(!manager.setConnected(0, true));
        QTRY_VERIFY(!manager.device(0).value("busy").toBool());
        QVERIFY(!manager.device(0).value("connected").toBool());
        QVERIFY(manager.setConnected(0, true));
        QVERIFY(!manager.selectDevice(0, "test.failure"));
        QTRY_VERIFY(manager.device(0).value("connected").toBool());
        QCOMPARE(manager.device(0).value("metrics").toMap().value("rightAscension").toDouble(), 12.5);
        QVERIFY(!manager.openMountSetup());
        QVERIFY(manager.setConnected(0, false));
        QTRY_VERIFY(!manager.device(0).value("busy").toBool());
        QVERIFY(!manager.device(0).value("connected").toBool());
        QVERIFY(manager.device(0).value("metrics").toMap().isEmpty());
    }
    void errorsAndLossDoNotLeaveStaleCoordinates() {
        auto *backend = new TestMount;
        DeviceManager manager(new IdleCamera, nullptr, nullptr, backend, nullptr);
        manager.refreshDevices();
        QTRY_VERIFY(!manager.scanning());
        QVERIFY(!manager.selectDevice(0, "not.registered"));
        QVERIFY(manager.selectDevice(0, "test.failure"));
        QVERIFY(manager.setConnected(0, true));
        QTRY_VERIFY(!manager.device(0).value("busy").toBool());
        QVERIFY(!manager.device(0).value("connected").toBool());
        QCOMPARE(manager.lastError(), QStringLiteral("Connection refused"));
        QVERIFY(manager.selectDevice(0, "test.mount"));
        QVERIFY(manager.setConnected(0, true));
        QTRY_VERIFY(manager.device(0).value("connected").toBool());
        QMetaObject::invokeMethod(backend, [backend] { emit backend->connectionLost("Lost"); }, Qt::QueuedConnection);
        QTRY_VERIFY(!manager.device(0).value("connected").toBool());
        QVERIFY(manager.device(0).value("metrics").toMap().isEmpty());
        QCOMPARE(manager.lastError(), QStringLiteral("Lost"));
    }
};
QTEST_GUILESS_MAIN(MountTest)
#include "tst_mount.moc"
