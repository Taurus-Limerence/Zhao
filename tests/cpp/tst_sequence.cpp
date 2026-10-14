#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include "sequence/SequenceController.h"
#include "devices/DeviceManager.h"
#include "devices/CameraBackend.h"
#include "devices/FilterWheelBackend.h"

class SequenceCamera : public CameraBackend {
public:
    bool pending = false;
    bool rejectGain = false;
    int requests = 0;
    unsigned exposure = 0, gain = 0;
    void scan() override { emit scanFinished({QVariantMap{{"id","sim"},{"name","sim"}}}, {}); }
    void open(const QString &) override { emit connectionFinished(true, {{"exposureMinUs",100},{"exposureMaxUs",10000000},{"gainMin",0},{"gainMax",500}}, {}); }
    void close() override { stopPreview(); emit connectionFinished(false,{},{}); }
    void setExposure(unsigned v) override { exposure=v; }
    void setGain(unsigned v) override { if (rejectGain) emit commandFailed("gain rejected"); else gain=v; }
    void setOffset(int) override {}
    void setBinning(int) override {}
    void setCoolerEnabled(bool) override {}
    void setTargetTemperature(double) override {}
    void setFanSpeed(int) override {}
    void setHeatLevel(int) override {}
    void setGainMode(int) override {}
    void setLowNoise(bool) override {}
    void startPreview(bool) override {}
    void requestStill() override { ++requests; pending=true; }
    void stopPreview() override { if(pending) { pending=false; emit stillFailed("cancelled"); } }
    void deliver(bool ok=true) {
        if (!pending) return;
        pending=false;
        if (!ok) { emit stillFailed("simulated capture failure"); return; }
        emit rawStillReady({0,65535},2,1,{{"EXPTIME",double(exposure)/1e6},{"GAIN",gain}});
    }
};
class SequenceWheel : public FilterWheelBackend {
public:
    int destination = -1;
    void scan() override { emit scanFinished({QVariantMap{{"id","wheel"},{"name","wheel"}}},{}); }
    void open(const QString &) override { emit connectionFinished(true,{{"slots",5},{"position",0},{"moving",false}},{}); }
    void close() override { emit connectionFinished(false,{},{}); }
    void moveTo(int position, bool) override { destination=position; emit stateChanged({{"moving",true},{"position",-1}}); }
    void calibrate() override {}
    void setSlotCount(int) override {}
    void arrive() { emit stateChanged({{"moving",false},{"position",destination}}); }
};
class SequenceTest : public QObject {
    Q_OBJECT
    void connectCamera(DeviceManager &dm) {
        dm.refreshDevices(); QTRY_COMPARE(dm.candidatesForRole(1).size(),1);
        QVERIFY(dm.selectDevice(1,"sim")); QVERIFY(dm.setConnected(1,true)); QTRY_COMPARE(dm.connectedCount(),1);
    }
    void pending(SequenceCamera *cam) {
        bool value=false;
        QTRY_VERIFY(([&]{ QMetaObject::invokeMethod(cam,[&]{value=cam->pending;},Qt::BlockingQueuedConnection); return value; })());
    }
private slots:
    void filterFocusFlagDefaultsAndRoundTrips() {
        DeviceManager dm(new SequenceCamera); SequenceController seq(&dm); QTemporaryDir dir;
        QCOMPARE(seq.plan().value("focusAfterFilterChange"),QVariant(false));
        seq.setHeader("focusAfterFilterChange",true); seq.addRow();
        const auto url=QUrl::fromLocalFile(dir.filePath("filter-focus.json"));
        QVERIFY(seq.savePlan(url)); seq.newPlan(); QVERIFY(seq.loadPlan(url));
        QCOMPARE(seq.plan().value("focusAfterFilterChange"),QVariant(true));
        QCOMPARE(seq.plan().value("focusBeforeCapture"),QVariant(false));
        seq.setHeader("focusAfterFilterChange","true"); QVERIFY(!seq.savePlan(url));
        for (const auto &invalid : {QVariant("false"),QVariant(0),QVariant(1)}) {
            seq.setHeader("focusAfterFilterChange",invalid); QVERIFY(!seq.savePlan(url));
        }
    }
    void autofocusDefaultsToDisabled() {
        DeviceManager dm(new SequenceCamera); SequenceController seq(&dm);
        QCOMPARE(seq.plan().value("focusBeforeCapture"),QVariant(false));
        QCOMPARE(seq.plan().value("focusAfterFilterChange"),QVariant(false));
        QVERIFY(!seq.property("needsAutofocus").toBool());
    }
    void rejectsMalformedAutofocusFlag_data() {
        QTest::addColumn<QVariant>("value");
        QTest::newRow("string-false")<<QVariant("false");
        QTest::newRow("string-true")<<QVariant("true");
        QTest::newRow("integer-zero")<<QVariant(0);
        QTest::newRow("integer-one")<<QVariant(1);
    }
    void rejectsMalformedAutofocusFlag() {
        QFETCH(QVariant,value);
        DeviceManager dm(new SequenceCamera); SequenceController seq(&dm); QTemporaryDir dir;
        auto invalid=seq.plan(); invalid.insert("version",1); invalid.insert("rows",QVariantList{}); invalid.insert("focusBeforeCapture",value);
        QFile file(dir.filePath("invalid.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument::fromVariant(invalid).toJson()); file.close();
        const auto previous=seq.plan(); QVERIFY(!seq.loadPlan(QUrl::fromLocalFile(file.fileName()))); QCOMPARE(seq.plan(),previous);
    }
    void autofocusPlanRoundTrip() {
        DeviceManager dm(new SequenceCamera); SequenceController seq(&dm); QTemporaryDir dir;
        const auto url=QUrl::fromLocalFile(dir.filePath("autofocus.json"));
        seq.addRow(); seq.setHeader("focusBeforeCapture",true); QVERIFY(seq.savePlan(url));
        seq.newPlan(); QVERIFY(seq.loadPlan(url));
        QCOMPARE(seq.plan().value("focusBeforeCapture"),QVariant(true)); QVERIFY(seq.property("needsAutofocus").toBool());
        QFile file(url.toLocalFile()); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).toVariant().toMap().value("version").toInt(),1);
    }
    void autofocusCannotSilentlySkipMissingService() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; DeviceManager dm(cam); connectCamera(dm);
        SequenceController seq(&dm); seq.addRow(); seq.setHeader("directory",dir.path()); seq.setHeader("focusBeforeCapture",true);
        QSignalSpy notices(&seq,&SequenceController::notice);
        QVERIFY(!seq.start()); QVERIFY(!seq.active()); QCOMPARE(seq.exposed(),0); QCOMPARE(seq.saved(),0);
        QVERIFY(!notices.isEmpty()); QVERIFY(notices.last().first().toString().contains(QStringLiteral("自动对焦")));
    }
    void autofocusRequiresEnabledLightRow() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; DeviceManager dm(cam); connectCamera(dm);
        SequenceController seq(&dm); seq.addRow(); seq.updateRow(0,"type","DARK");
        seq.addRow(); seq.updateRow(1,"enabled",false);
        seq.setHeader("directory",dir.path()); seq.setHeader("focusBeforeCapture",true);
        QSignalSpy notices(&seq,&SequenceController::notice);
        QVERIFY(!seq.start()); QVERIFY(!seq.active()); QCOMPARE(seq.exposed(),0); QCOMPARE(seq.saved(),0);
        QVERIFY(!notices.isEmpty()); QVERIFY(notices.last().first().toString().contains("LIGHT"));
    }
    void rejectsMalformedCenteringPlan_data() {
        QTest::addColumn<QString>("key"); QTest::addColumn<QVariant>("value");
        QTest::newRow("bool-exposure")<<QString("centerExposure")<<QVariant(true);
        QTest::newRow("string-limit")<<QString("centerCorrections")<<QVariant("2.0");
        QTest::newRow("string-tolerance")<<QString("centerTolerance")<<QVariant("1");
        QTest::newRow("bad-enable")<<QString("centerBeforeCapture")<<QVariant("false");
        QTest::newRow("fraction-limit")<<QString("centerCorrections")<<QVariant(1.5);
        QTest::newRow("negative-exposure")<<QString("centerExposure")<<QVariant(-1.0);
    }
    void rejectsMalformedCenteringPlan() {
        QFETCH(QString,key); QFETCH(QVariant,value);
        DeviceManager dm(new SequenceCamera); SequenceController seq(&dm);
        QTemporaryDir dir; const auto path=dir.filePath("plan.json");
        auto malformed=seq.plan(); malformed.insert("version",1); malformed.insert("rows",QVariantList{}); malformed.insert(key,value);
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument::fromVariant(malformed).toJson()); file.close();
        const auto previous=seq.plan(); QVERIFY(!seq.loadPlan(QUrl::fromLocalFile(path))); QCOMPARE(seq.plan(),previous);
    }
    void legacyPlanDefaultsToNoMovement() {
        DeviceManager dm(new SequenceCamera); SequenceController seq(&dm); QTemporaryDir dir;
        QFile file(dir.filePath("legacy.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"version\":1,\"name\":\"Legacy\",\"rows\":[]}"); file.close();
        QVERIFY(seq.loadPlan(QUrl::fromLocalFile(file.fileName()))); QVERIFY(!seq.needsCentering());
        QCOMPARE(seq.plan().value("focusBeforeCapture"),QVariant(false));
        QVERIFY(!seq.property("needsAutofocus").toBool());
        QCOMPARE(seq.plan().value("centerExposure").toDouble(),2.0);
    }
    void centeringCannotSilentlySkipMissingService() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; DeviceManager dm(cam); connectCamera(dm);
        SequenceController seq(&dm); seq.addRow(); seq.setHeader("directory",dir.path());
        seq.setHeader("target","M31"); seq.setHeader("ra","0.7"); seq.setHeader("dec","41.2");
        seq.setHeader("centerBeforeCapture",true);
        QVERIFY(!seq.start()); QVERIFY(!seq.active());
    }
    void centeringPlanRoundTrip() {
        DeviceManager dm(new SequenceCamera); SequenceController seq(&dm);
        QTemporaryDir dir; const auto file=QUrl::fromLocalFile(dir.filePath("plan.json"));
        seq.setHeader("target","M31"); seq.setHeader("ra","0.7"); seq.setHeader("dec","41.2");
        seq.setHeader("centerBeforeCapture",true); seq.setHeader("centerExposure",3.5);
        seq.setHeader("centerTolerance",0.5); seq.setHeader("centerCorrections",2); seq.addRow();
        QVERIFY(seq.savePlan(file)); seq.newPlan(); QVERIFY(seq.loadPlan(file));
        QCOMPARE(seq.plan().value("centerBeforeCapture"),QVariant(true));
        QCOMPARE(seq.plan().value("centerExposure").toDouble(),3.5);
        QCOMPARE(seq.plan().value("centerTolerance").toDouble(),0.5);
        QCOMPARE(seq.plan().value("centerCorrections").toInt(),2);
        seq.setHeader("ra",""); QVERIFY(!seq.savePlan(file));
    }
    void resetPreservesPlanAndRestartsFromFirstRow() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; DeviceManager dm(cam); connectCamera(dm);
        SequenceController seq(&dm); seq.setHeader("directory",dir.path()); seq.setHeader("target","M31");
        seq.addRow(); seq.addRow(); seq.updateRow(1,"gain",120);
        const auto plan=seq.plan(); const auto rows=seq.rows();
        QVERIFY(seq.start());
        for (int i=0;i<2;++i) {
            pending(cam); QMetaObject::invokeMethod(cam,[cam]{cam->deliver();},Qt::QueuedConnection);
            QTRY_COMPARE(seq.saved(),i+1);
        }
        QTRY_COMPARE(seq.state(),"Completed");
        const auto files=QDir(dir.path()).entryList({"*.fits"},QDir::Files);
        QCOMPARE(files.size(),2);
        QVERIFY(QMetaObject::invokeMethod(&seq,"resetProgress",Qt::DirectConnection));
        QCOMPARE(seq.state(),"Idle"); QCOMPARE(seq.saved(),0); QCOMPARE(seq.exposed(),0); QCOMPARE(seq.failed(),0);
        QCOMPARE(seq.currentRow(),-1); QCOMPARE(seq.progress(),0.0);
        QCOMPARE(seq.plan(),plan); QCOMPARE(seq.rows(),rows);
        QCOMPARE(QDir(dir.path()).entryList({"*.fits"},QDir::Files),files);
        QVERIFY(seq.start()); pending(cam); QCOMPARE(seq.currentRow(),0);
        seq.stop(); QTRY_VERIFY(!seq.active());
    }
    void resetRejectedUntilStopped() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; DeviceManager dm(cam); connectCamera(dm);
        SequenceController seq(&dm); seq.setHeader("directory",dir.path()); seq.addRow();
        QVERIFY(seq.start()); pending(cam);
        QVERIFY(QMetaObject::invokeMethod(&seq,"resetProgress",Qt::DirectConnection));
        QCOMPARE(seq.state(),"Running"); QVERIFY(seq.active());
        QMetaObject::invokeMethod(cam,[cam]{cam->deliver(false);},Qt::QueuedConnection);
        QTRY_COMPARE(seq.state(),"Paused"); QCOMPARE(seq.failed(),1);
        QVERIFY(QMetaObject::invokeMethod(&seq,"resetProgress",Qt::DirectConnection));
        QCOMPARE(seq.state(),"Paused"); QCOMPARE(seq.failed(),1);
        seq.stop(); QTRY_VERIFY(!seq.active());
        QVERIFY(QMetaObject::invokeMethod(&seq,"resetProgress",Qt::DirectConnection));
        QCOMPARE(seq.failed(),0); QCOMPARE(seq.state(),"Idle");
    }
    void queuedManualMovementBlocksSequenceStart() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; auto *wheel=new SequenceWheel;
        DeviceManager dm(cam,wheel); connectCamera(dm); QTRY_VERIFY(!dm.scanning());
        QVERIFY(dm.selectDevice(3,"wheel")); QVERIFY(dm.setConnected(3,true)); QTRY_COMPARE(dm.connectedCount(),2);
        SequenceController seq(&dm); seq.setHeader("directory",dir.path()); seq.addRow();
        QVERIFY(dm.moveFilterWheel(2));
        // No event processing: the worker's state report has not reached the manager.
        QVERIFY(!seq.start()); QVERIFY(!seq.active());
    }
    void queuedManualPreviewBlocksSequenceStart() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; DeviceManager dm(cam); connectCamera(dm);
        SequenceController seq(&dm); seq.setHeader("directory",dir.path()); seq.addRow();
        QVERIFY(dm.startCameraPreview()); QVERIFY(!seq.start());
    }
    void stopDoesNotWaitForeverForWheel() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; auto *wheel=new SequenceWheel;
        DeviceManager dm(cam,wheel); connectCamera(dm); QTRY_VERIFY(!dm.scanning());
        QVERIFY(dm.selectDevice(3,"wheel")); QVERIFY(dm.setConnected(3,true)); QTRY_COMPARE(dm.connectedCount(),2);
        SequenceController seq(&dm); seq.setHeader("directory",dir.path()); seq.addRow(); seq.updateRow(0,"filter",1);
        QVERIFY(seq.start()); QTRY_VERIFY(dm.device(3).value("metrics").toMap().value("moving").toBool());
        seq.stop(); QCOMPARE(seq.state(),"Idle"); QVERIFY(!seq.active());
        QVERIFY(!seq.start()); QVERIFY(dm.setConnected(3,false));
    }
    void cannotStartInsideManualFrameDelivery() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; DeviceManager dm(cam); connectCamera(dm);
        dm.setStillSaveDirectory(dir.path()); SequenceController seq(&dm); seq.setHeader("directory",dir.path()); seq.addRow();
        bool attempted=false, accepted=true;
        connect(&dm,&DeviceManager::stillFrameReceived,&seq,[&](int){attempted=true; accepted=seq.start();});
        QVERIFY(dm.captureStill(1)); pending(cam); QMetaObject::invokeMethod(cam,[cam]{cam->deliver();},Qt::QueuedConnection);
        QTRY_VERIFY(attempted); QVERIFY(!accepted); QCOMPARE(seq.saved(),0);
    }
    void disconnectedCameraTerminatesPendingFrame() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; DeviceManager dm(cam); connectCamera(dm);
        SequenceController seq(&dm); seq.setHeader("directory",dir.path()); seq.addRow(); QVERIFY(seq.start()); pending(cam);
        QMetaObject::invokeMethod(cam,[cam]{cam->pending=false; emit cam->connectionLost("unplugged");},Qt::QueuedConnection);
        QTRY_COMPARE(seq.state(),"Paused"); QCOMPARE(seq.failed(),1); seq.stop(); QVERIFY(!seq.active());
    }
    void stopDrainsReceivedFrame() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; DeviceManager dm(cam); connectCamera(dm);
        SequenceController seq(&dm); seq.setHeader("directory",dir.path()); seq.addRow(); seq.updateRow(0,"count",2);
        connect(&dm,&DeviceManager::stillFrameReceived,&seq,[&seq](int){seq.stop();});
        QVERIFY(seq.start()); pending(cam); QMetaObject::invokeMethod(cam,[cam]{cam->deliver();},Qt::QueuedConnection);
        QTRY_COMPARE(seq.state(),"Idle"); QCOMPARE(seq.saved(),1); QCOMPARE(seq.exposed(),1); QCOMPARE(seq.failed(),0);
        QCOMPARE(QDir(dir.path()).entryList({"*.fits"},QDir::Files).size(),1);
    }
    void parameterFailureNeverExposes() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; cam->rejectGain=true;
        DeviceManager dm(cam); connectCamera(dm); SequenceController seq(&dm);
        seq.setHeader("directory",dir.path()); seq.addRow(); QVERIFY(seq.start());
        QTRY_COMPARE(seq.state(),"Paused"); QCOMPARE(seq.failed(),1); QCOMPARE(seq.exposed(),0);
        int requests=-1; QMetaObject::invokeMethod(cam,[&]{requests=cam->requests;},Qt::BlockingQueuedConnection);
        QCOMPARE(requests,0); seq.stop(); QVERIFY(!seq.active());
    }
    void filterArrivalPrecedesExposure() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; auto *wheel=new SequenceWheel;
        DeviceManager dm(cam,wheel); connectCamera(dm);
        QTRY_VERIFY(!dm.scanning()); QVERIFY(dm.selectDevice(3,"wheel")); QVERIFY(dm.setConnected(3,true)); QTRY_COMPARE(dm.connectedCount(),2);
        SequenceController seq(&dm); seq.setHeader("directory",dir.path()); seq.addRow(); seq.updateRow(0,"filter",2);
        QVERIFY(seq.start()); QCOMPARE(seq.phase(),"Filter");
        QTRY_VERIFY(dm.device(3).value("metrics").toMap().value("moving").toBool());
        int requests=-1; QMetaObject::invokeMethod(cam,[&]{requests=cam->requests;},Qt::BlockingQueuedConnection); QCOMPARE(requests,0);
        QVERIFY(!dm.moveFilterWheel(1)); QVERIFY(!dm.setCameraExposure(5)); QVERIFY(!dm.setConnected(1,false));
        QMetaObject::invokeMethod(wheel,[wheel]{wheel->arrive();},Qt::QueuedConnection);
        pending(cam); QMetaObject::invokeMethod(cam,[cam]{cam->deliver();},Qt::QueuedConnection);
        QTRY_COMPARE(seq.state(),"Completed");
        const auto files=QDir(dir.path()).entryList({"*.fits"},QDir::Files); QCOMPARE(files.size(),1);
        QFile f(dir.filePath(files[0])); QVERIFY(f.open(QIODevice::ReadOnly)); QVERIFY(f.readAll().contains("FILTER  ="));
    }
    void stopWhileFilterMovingNeverExposes() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; auto *wheel=new SequenceWheel;
        DeviceManager dm(cam,wheel); connectCamera(dm); QTRY_VERIFY(!dm.scanning());
        QVERIFY(dm.selectDevice(3,"wheel")); QVERIFY(dm.setConnected(3,true)); QTRY_COMPARE(dm.connectedCount(),2);
        SequenceController seq(&dm); seq.setHeader("directory",dir.path()); seq.addRow(); seq.updateRow(0,"filter",1);
        QVERIFY(seq.start()); seq.stop(); QCOMPARE(seq.state(),"Idle");
        QTRY_VERIFY(dm.device(3).value("metrics").toMap().value("moving").toBool());
        QMetaObject::invokeMethod(wheel,[wheel]{wheel->arrive();},Qt::QueuedConnection);
        QTRY_COMPARE(seq.state(),"Idle");
        int requests=-1; QMetaObject::invokeMethod(cam,[&]{requests=cam->requests;},Qt::BlockingQueuedConnection); QCOMPARE(requests,0);
    }
    void saveFailurePausesWithoutNextExposure() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; DeviceManager dm(cam); connectCamera(dm);
        SequenceController seq(&dm); seq.setHeader("directory",dir.filePath("frames")); seq.addRow(); seq.updateRow(0,"count",2);
        QVERIFY(seq.start()); pending(cam);
        // Replace this test's empty output directory with a file after preflight.
        QVERIFY(QDir().rmdir(dir.filePath("frames")));
        QFile obstruction(dir.filePath("frames")); QVERIFY(obstruction.open(QIODevice::WriteOnly)); obstruction.close();
        QMetaObject::invokeMethod(cam,[cam]{cam->deliver();},Qt::QueuedConnection);
        QTRY_COMPARE(seq.state(),"Paused"); QCOMPARE(seq.exposed(),1); QCOMPARE(seq.saved(),0); QCOMPARE(seq.failed(),1);
        int requests=-1; QMetaObject::invokeMethod(cam,[&]{requests=cam->requests;},Qt::BlockingQueuedConnection); QCOMPARE(requests,1);
        seq.stop();
    }
    void savesFramesAndPausesAtBoundary() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; DeviceManager dm(cam); connectCamera(dm);
        SequenceController seq(&dm); seq.setHeader("directory",dir.path()); seq.setHeader("target","M31");
        seq.addRow(); seq.updateRow(0,"count",2); seq.updateRow(0,"exposure",0.1);
        QVERIFY(seq.start()); QVERIFY(!dm.startCameraPreview()); pending(cam);
        seq.pause(); QMetaObject::invokeMethod(cam,[cam]{cam->deliver();},Qt::QueuedConnection);
        QTRY_COMPARE(seq.state(),"Paused"); QCOMPARE(seq.saved(),1); QCOMPARE(seq.exposed(),1);
        QVERIFY(seq.start()); pending(cam); QMetaObject::invokeMethod(cam,[cam]{cam->deliver();},Qt::QueuedConnection);
        QTRY_COMPARE(seq.state(),"Completed"); QCOMPARE(seq.saved(),2); QVERIFY(!seq.active());
        const auto files=QDir(dir.path()).entryList({"*.fits"},QDir::Files); QCOMPARE(files.size(),2);
        QFile f(dir.filePath(files[0])); QVERIFY(f.open(QIODevice::ReadOnly)); QVERIFY(f.readAll().contains("OBJECT  = 'M31'"));
    }
    void failurePausesAndStopReleases() {
        QTemporaryDir dir; auto *cam=new SequenceCamera; DeviceManager dm(cam); connectCamera(dm);
        SequenceController seq(&dm); seq.setHeader("directory",dir.path()); seq.addRow();
        QVERIFY(seq.start()); pending(cam); QMetaObject::invokeMethod(cam,[cam]{cam->deliver(false);},Qt::QueuedConnection);
        QTRY_COMPARE(seq.state(),"Paused"); QCOMPARE(seq.failed(),1); QCOMPARE(seq.saved(),0);
        QVERIFY(seq.start()); pending(cam); seq.stop(); QTRY_COMPARE(seq.state(),"Idle"); QVERIFY(!seq.active());
    }
    void invalidAndPlanRoundTrip() {
        QTemporaryDir dir; DeviceManager dm(new SequenceCamera); SequenceController seq(&dm);
        QVERIFY(!seq.start()); seq.addRow(); seq.setHeader("target","M42");
        auto file=QUrl::fromLocalFile(dir.filePath("plan.json")); QVERIFY(seq.savePlan(file));
        seq.newPlan(); QVERIFY(seq.loadPlan(file)); QCOMPARE(seq.plan().value("target").toString(),"M42"); QCOMPARE(seq.rows().size(),1);
        seq.updateRow(0,"exposure",-1); QVERIFY(!seq.start());
        QFile broken(dir.filePath("broken.json")); QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{\"version\":1,\"rows\":\"not a list\"}"); broken.close();
        QVERIFY(!seq.loadPlan(QUrl::fromLocalFile(broken.fileName())));
        QCOMPARE(seq.plan().value("target").toString(),"M42"); QCOMPARE(seq.rows().size(),1);
    }
};
QTEST_MAIN(SequenceTest)
#include "tst_sequence.moc"
