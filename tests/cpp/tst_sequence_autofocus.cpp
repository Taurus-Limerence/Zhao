#include <QtTest>
#include <QTemporaryDir>
#include <QMutexLocker>
#include <atomic>
#include <cmath>
#include <memory>
#include "astro/CenteringController.h"
#include "astro/PlateSolver.h"
#include "focus/AutoFocusController.h"
#include "sequence/SequenceController.h"
#include "devices/DeviceManager.h"
#include "devices/CameraBackend.h"
#include "devices/FilterWheelBackend.h"
#include "devices/FocuserBackend.h"
#include "devices/MountBackend.h"

struct SequenceFocusRig {
    std::atomic<int> position{5000}, moves{0}, halts{0}, focusFrames{0}, taskFrames{0};
    std::atomic<int> centerFrames{0};
    std::atomic<int> filter{0}, gain{100}, gainDelay{0}, restoreDelay{0}, restores{0};
    std::atomic<int> moveDelay{0};
    std::atomic<int> partialPosition{-1}, returnedMoves{0};
    std::atomic<bool> moving{false}, holdMove{false}, holdFocus{false}, blank{false};
    std::atomic<bool> rejectHalt{false};
    std::atomic<bool> holdFilter{false}, rejectGain{false}, wrongGain{false}, taskPending{false};
    std::atomic<bool> centerPending{false};
    QMutex mutex;
    QStringList events;
    void record(const QString &event) { QMutexLocker lock(&mutex); events.append(event); }
    QStringList log() { QMutexLocker lock(&mutex); return events; }
};

class SequenceFocusCamera final : public CameraBackend {
public:
    explicit SequenceFocusCamera(std::shared_ptr<SequenceFocusRig> rig) : rig(std::move(rig)) {}
    std::shared_ptr<SequenceFocusRig> rig;
    unsigned exposure=5000000;
    bool pending=false;
    void scan() override { emit scanFinished({QVariantMap{{"id","camera"},{"name","Camera"}}},{}); }
    void open(const QString &) override {
        emit connectionFinished(true,{{"exposureUs",exposure},{"exposureMinUs",100},{"exposureMaxUs",10000000},
            {"gain",100},{"gainMin",0},{"gainMax",500},{"previewing",false}},{});
    }
    void close() override { emit connectionFinished(false,{},{}); }
    void setExposure(unsigned value) override {
        exposure=value; emit stateChanged({{"exposureUs",value}});
        if (value==5000000) {
            ++rig->restores;
            if (rig->restoreDelay) QThread::msleep(rig->restoreDelay);
            rig->record("exposure-restored");
        }
    }
    void setGain(unsigned value) override {
        rig->gain=int(value)+(rig->wrongGain ? 1 : 0);
        emit stateChanged({{"gain",rig->gain.load()}});
        rig->record(QString("gain-readback:%1").arg(rig->gain.load()));
        if (rig->gainDelay) QThread::msleep(rig->gainDelay);
        if (rig->rejectGain) emit commandFailed("gain rejected after readback");
        rig->record(QString("gain-return:%1").arg(value));
    }
    void setOffset(int) override {}
    void setBinning(int) override {}
    void setCoolerEnabled(bool) override {}
    void setTargetTemperature(double) override {}
    void setFanSpeed(int) override {}
    void setHeatLevel(int) override {}
    void setGainMode(int) override {}
    void setLowNoise(bool) override {}
    void startPreview(bool) override {}
    void stopPreview() override {
        if (!pending) return;
        pending=false; rig->taskPending=false; emit stillFailed("cancelled");
    }
    void requestStill() override {
        pending=true;
        if (exposure==20000) {
            ++rig->centerFrames; rig->centerPending=true; rig->record("center-frame"); return;
        }
        if (exposure!=10000) {
            ++rig->taskFrames; rig->taskPending=true; rig->record("task-request"); return;
        }
        ++rig->focusFrames;
        rig->record(QString("focus-frame:%1:%2").arg(rig->filter.load()).arg(rig->gain.load()));
        if (rig->holdFocus) return;
        constexpr int width=320,height=240;
        QVector<quint16> raw(width*height,100);
        if (!rig->blank) {
            const double sigma=std::sqrt(2.2*2.2+std::pow((rig->position.load()-5040)/100.0,2));
            for (int cy : {50,120,190}) for (int cx : {60,160,260}) {
                for (int y=cy-22;y<=cy+22;++y) for (int x=cx-22;x<=cx+22;++x) {
                    const double r2=(x-cx)*(x-cx)+(y-cy)*(y-cy);
                    raw[y*width+x]+=quint16(16000*std::exp(-r2/(2*sigma*sigma)));
                }
            }
        }
        pending=false;
        emit rawStillReady(raw,width,height,{{"RAWBITS",16}});
    }
    void deliverCenter() {
        if (!pending || !rig->centerPending) return;
        pending=false; rig->centerPending=false;
        emit rawStillReady(QVector<quint16>(5000,100),100,50,{{"RAWBITS",16}});
    }
    void deliverTask() {
        if (!pending || !rig->taskPending) return;
        pending=false; rig->taskPending=false;
        emit rawStillReady({100,1000,40000,65535},2,2,{{"RAWBITS",16},{"EXPTIME",double(exposure)/1e6},{"GAIN",rig->gain.load()}});
    }
};

class SequenceFocusWheel final : public FilterWheelBackend {
public:
    explicit SequenceFocusWheel(std::shared_ptr<SequenceFocusRig> rig) : rig(std::move(rig)) {}
    std::shared_ptr<SequenceFocusRig> rig;
    int destination=0;
    void scan() override { emit scanFinished({QVariantMap{{"id","wheel"},{"name","Wheel"}}},{}); }
    void open(const QString &) override { emit connectionFinished(true,{{"slots",5},{"position",0},{"moving",false}},{}); }
    void close() override { emit connectionFinished(false,{},{}); }
    void moveTo(int position,bool) override {
        destination=position; rig->record(QString("filter-request:%1").arg(position));
        emit stateChanged({{"moving",true},{"position",-1}});
        if (!rig->holdFilter) arrive();
    }
    void arrive() {
        rig->filter=destination; rig->record(QString("filter-ready:%1").arg(destination));
        emit stateChanged({{"moving",false},{"position",destination}});
    }
    void calibrate() override {}
    void setSlotCount(int) override {}
};

class SequenceFocusFocuser final : public FocuserBackend {
public:
    explicit SequenceFocusFocuser(std::shared_ptr<SequenceFocusRig> rig) : rig(std::move(rig)) {}
    std::shared_ptr<SequenceFocusRig> rig;
    void scan() override { emit scanFinished({QVariantMap{{"id","focuser"},{"name","Focuser"}}},{}); }
    void open(const QString &) override { emit connectionFinished(true,{{"position",rig->position.load()},{"maxStep",65000},{"moving",false}},{}); }
    void close() override { emit connectionFinished(false,{},{}); }
    void moveTo(int position) override {
        ++rig->moves; rig->position=rig->holdMove && rig->partialPosition>=0 ? rig->partialPosition.load() : position;
        rig->moving=true; rig->record("focus-move");
        rig->record(QString("focus-target:%1").arg(position));
        emit stateChanged({{"position",rig->position.load()},{"moving",true}});
        if (!rig->holdMove) {
            rig->moving=false; emit stateChanged({{"position",position},{"moving",false}});
        }
        if (rig->moveDelay) QThread::msleep(rig->moveDelay);
        ++rig->returnedMoves;
    }
    void halt() override {
        ++rig->halts;
        if (rig->rejectHalt) { emit stateChanged({{"error","halt rejected"},{"moving",true}}); return; }
        // Command completion is deliberately earlier than physical stop.
        QTimer::singleShot(500,this,[this] {
            rig->moving=false; emit stateChanged({{"position",rig->position.load()},{"moving",false}});
        });
    }
    void setReversed(bool) override {}
    void setMaxStep(int) override {}
    void setZero() override {}
    void setCurrentPosition(int) override {}
};

class SequenceFocusMount final : public MountBackend {
public:
    explicit SequenceFocusMount(std::shared_ptr<SequenceFocusRig> rig) : rig(std::move(rig)) {}
    std::shared_ptr<SequenceFocusRig> rig;
    void scan() override { emit scanFinished({QVariantMap{{"id","mount"},{"name","Mount"}}},{}); }
    void open(const QString &) override {
        emit connectionFinished(true,{{"skyRa",10.0},{"skyDec",0.0},{"tracking",true},{"slewing",false},{"atPark",false}},{});
    }
    void close() override { emit connectionFinished(false,{},{}); }
    void setup(const QString &) override {}
    QString slewToJ2000(double ra,double dec) override {
        rig->record("center-move");
        emit stateChanged({{"gotoState","Arrived"},{"slewing",false},{"skyRa",ra},{"skyDec",dec}});
        return {};
    }
    QString stopSlew() override {
        emit stateChanged({{"gotoState","Stopped"},{"slewing",false}}); return {};
    }
};

class SequenceAutofocusTest final : public QObject {
    Q_OBJECT
    static bool connectRig(DeviceManager &dm) {
        dm.refreshDevices();
        QElapsedTimer timeout; timeout.start();
        while (dm.scanning() && timeout.elapsed()<3000) QTest::qWait(10);
        for (const auto &[role,id] : {std::pair{DeviceManager::MainCamera,"camera"},
                std::pair{DeviceManager::FilterWheel,"wheel"},std::pair{DeviceManager::Focuser,"focuser"}}) {
            if (!dm.selectDevice(role,id) || !dm.setConnected(role,true)) return false;
        }
        while (dm.connectedCount()!=3 && timeout.elapsed()<3000) QTest::qWait(10);
        return dm.connectedCount()==3;
    }
    static void configure(SequenceController &seq,AutoFocusController &focus,const QString &directory) {
        focus.setSetting("points",7); focus.setSetting("exposure",0.01);
        focus.setSetting("frames",1); focus.setSetting("backlash",0);
        seq.setHeader("directory",directory); seq.setHeader("focusBeforeCapture",true);
        seq.addRow(); seq.updateRow(0,"exposure",0.1); seq.updateRow(0,"gain",173);
    }
    static void deliver(SequenceFocusCamera *camera) {
        QMetaObject::invokeMethod(camera,[camera] { camera->deliverTask(); },Qt::QueuedConnection);
    }
private slots:
    void init() { QSettings().clear(); }
    void filterOffsetsUseVerifiedAnchorWithoutAnotherScan_data() {
        QTest::addColumn<int>("offset");
        QTest::addColumn<int>("backlash");
        QTest::newRow("positive")<<120<<0; QTest::newRow("negative")<<-120<<0; QTest::newRow("zero")<<0<<0;
        QTest::newRow("positive-backlash")<<120<<40; QTest::newRow("negative-backlash")<<-120<<40; QTest::newRow("zero-backlash")<<0<<40;
    }
    void filterOffsetsUseVerifiedAnchorWithoutAnotherScan() {
        QFETCH(int,offset);
        QFETCH(int,backlash);
        auto rig=std::make_shared<SequenceFocusRig>(); auto *camera=new SequenceFocusCamera(rig);
        DeviceManager dm(camera,new SequenceFocusWheel(rig),new SequenceFocusFocuser(rig));
        QVERIFY(connectRig(dm)); AutoFocusController focus(&dm); SequenceController seq(&dm,nullptr,nullptr,&focus); QTemporaryDir dir;
        configure(seq,focus,dir.path()); seq.setHeader("focusAfterFilterChange",true); seq.updateRow(0,"filter",0);
        focus.setSetting("backlash",backlash);
        seq.addRow(); seq.updateRow(1,"filter",1); seq.updateRow(1,"exposure",0.1);
        seq.addRow(); seq.updateRow(2,"filter",0); seq.updateRow(2,"exposure",0.1);
        bool accepted=false;
        QVERIFY(QMetaObject::invokeMethod(&dm,"setFilterWheelFocusOffset",Q_RETURN_ARG(bool,accepted),Q_ARG(int,2),Q_ARG(QVariant,QVariant(offset)))); QVERIFY(accepted);
        QVERIFY(QMetaObject::invokeMethod(&dm,"setFilterFocusEnabled",Q_RETURN_ARG(bool,accepted),Q_ARG(bool,true))); QVERIFY(accepted);
        QVERIFY(seq.start()); QTRY_COMPARE_WITH_TIMEOUT(rig->taskFrames.load(),1,20000);
        const int anchor=rig->position.load(), scans=rig->focusFrames.load(), moves=rig->moves.load();
        QVERIFY(dm.property("filterFocusSettings").toMap().value("baselineValid").toBool());
        deliver(camera); QTRY_COMPARE_WITH_TIMEOUT(rig->taskFrames.load(),2,5000);
        QCOMPARE(rig->position.load(),anchor+offset); QCOMPARE(rig->focusFrames.load(),scans);
        deliver(camera); QTRY_COMPARE_WITH_TIMEOUT(rig->taskFrames.load(),3,5000);
        QCOMPARE(rig->position.load(),anchor); QCOMPARE(rig->focusFrames.load(),scans);
        QCOMPARE(rig->moves.load(),moves+(offset ? (backlash ? 4 : 2) : 0));
        if (offset && backlash) {
            QVERIFY(rig->log().contains(QString("focus-target:%1").arg(anchor+offset-backlash)));
            QVERIFY(rig->log().contains(QString("focus-target:%1").arg(anchor-backlash)));
        }
        deliver(camera); QTRY_COMPARE(seq.state(),QString("Completed"));
        QVERIFY(dm.moveFocuser(anchor+1));
        QVERIFY(!dm.property("filterFocusSettings").toMap().value("baselineValid").toBool());
    }
    void missingFilterBaselineNeverFallsBackToAutofocus() {
        auto rig=std::make_shared<SequenceFocusRig>(); auto *camera=new SequenceFocusCamera(rig);
        DeviceManager dm(camera,new SequenceFocusWheel(rig),new SequenceFocusFocuser(rig));
        QVERIFY(connectRig(dm)); AutoFocusController focus(&dm); SequenceController seq(&dm,nullptr,nullptr,&focus); QTemporaryDir dir;
        configure(seq,focus,dir.path()); seq.setHeader("focusBeforeCapture",false); seq.setHeader("focusAfterFilterChange",true); seq.updateRow(0,"filter",1);
        bool accepted=false;
        QVERIFY(QMetaObject::invokeMethod(&dm,"setFilterWheelFocusOffset",Q_RETURN_ARG(bool,accepted),Q_ARG(int,2),Q_ARG(QVariant,QVariant(100)))); QVERIFY(accepted);
        QVERIFY(QMetaObject::invokeMethod(&dm,"setFilterFocusEnabled",Q_RETURN_ARG(bool,accepted),Q_ARG(bool,true))); QVERIFY(accepted);
        QVERIFY(seq.start()); QTRY_COMPARE(seq.state(),QString("Paused"));
        QCOMPARE(rig->taskFrames.load(),0); QCOMPARE(rig->focusFrames.load(),0); QCOMPARE(rig->moves.load(),0); seq.stop();
    }
    void filterOffsetPauseStopAndAcknowledgement_data() {
        QTest::addColumn<QString>("action");
        for (const auto &action : {"pause-wheel","pause-offset","stop-offset","retry-stop","emergency","delay-ack","hold-actual-stop"})
            QTest::newRow(action)<<QString(action);
    }
    void filterOffsetPauseStopAndAcknowledgement() {
        QFETCH(QString,action);
        auto rig=std::make_shared<SequenceFocusRig>(); auto *camera=new SequenceFocusCamera(rig);
        auto *wheel=new SequenceFocusWheel(rig); auto *focuser=new SequenceFocusFocuser(rig);
        DeviceManager dm(camera,wheel,focuser); QVERIFY(connectRig(dm));
        AutoFocusController focus(&dm); SequenceController seq(&dm,nullptr,nullptr,&focus); QTemporaryDir dir;
        configure(seq,focus,dir.path()); seq.updateRow(0,"filter",0);
        seq.addRow(); seq.updateRow(1,"filter",1); seq.updateRow(1,"exposure",0.1);
        QVERIFY(dm.setFilterWheelFocusOffset(2,120)); QVERIFY(dm.setFilterFocusEnabled(true));
        QVERIFY(seq.start()); QTRY_COMPARE_WITH_TIMEOUT(rig->taskFrames.load(),1,20000);
        const int anchor=rig->position.load(), scans=rig->focusFrames.load();
        QVERIFY(!dm.setFilterWheelFocusOffset(2,150)); QVERIFY(!dm.setFilterFocusEnabled(false));
        if (action=="pause-wheel") rig->holdFilter=true;
        else if (action=="delay-ack") rig->moveDelay=1000;
        else rig->holdMove=true;
        deliver(camera);
        if (action=="pause-wheel") {
            QTRY_COMPARE(seq.phase(),QString("Filter")); seq.pause();
            QMetaObject::invokeMethod(wheel,[wheel] { wheel->arrive(); },Qt::QueuedConnection);
            QTRY_COMPARE(seq.state(),QString("Paused"));
            QCOMPARE(rig->taskFrames.load(),1); QVERIFY(seq.start());
        } else {
            QTRY_COMPARE(seq.phase(),QString("FocusOffset"));
            QTRY_VERIFY(rig->moving.load() || action=="delay-ack");
            if (action=="delay-ack") {
                QTest::qWait(300); QCOMPARE(rig->taskFrames.load(),1);
            } else if (action=="hold-actual-stop") {
                QTest::qWait(700); QCOMPARE(rig->taskFrames.load(),1);
                QMetaObject::invokeMethod(focuser,[focuser,rig] {
                    rig->moving=false; emit focuser->stateChanged({{"position",rig->position.load()},{"moving",false}});
                },Qt::QueuedConnection);
            } else {
                if (action=="pause-offset") seq.pause();
                else if (action=="stop-offset" || action=="retry-stop") {
                    rig->rejectHalt=action=="retry-stop"; seq.stop();
                }
                else QVERIFY(dm.haltFocuser());
                QTest::qWait(200); QVERIFY(seq.active()); QCOMPARE(rig->taskFrames.load(),1);
                if (action=="retry-stop") {
                    QTRY_COMPARE(rig->halts.load(),1); QTest::qWait(600); QVERIFY(seq.active());
                    rig->rejectHalt=false; seq.stop(); QTRY_COMPARE(rig->halts.load(),2);
                }
                if (action=="stop-offset" || action=="retry-stop") {
                    QTRY_VERIFY(!seq.active()); QCOMPARE(rig->taskFrames.load(),1);
                    QVERIFY(!dm.filterFocusSettings().value("baselineValid").toBool()); return;
                }
                QTRY_COMPARE(seq.state(),QString("Paused")); QCOMPARE(rig->taskFrames.load(),1);
                if (action=="emergency") { QVERIFY(!dm.filterFocusSettings().value("baselineValid").toBool()); seq.stop(); return; }
                rig->holdMove=false; QVERIFY(seq.start());
                QTest::qWait(200); QCOMPARE(rig->taskFrames.load(),1);
            }
        }
        QTRY_COMPARE_WITH_TIMEOUT(rig->taskFrames.load(),2,5000);
        QCOMPARE(rig->position.load(),anchor+120); QCOMPARE(rig->focusFrames.load(),scans);
        deliver(camera); QTRY_COMPARE(seq.state(),QString("Completed"));
    }
    void filterOnlyAutofocusRestoresTaskParametersAndSkipsSameFilter() {
        auto rig=std::make_shared<SequenceFocusRig>(); auto *camera=new SequenceFocusCamera(rig);
        DeviceManager dm(camera,new SequenceFocusWheel(rig),new SequenceFocusFocuser(rig)); QVERIFY(connectRig(dm));
        AutoFocusController focus(&dm); SequenceController seq(&dm,nullptr,nullptr,&focus); QTemporaryDir dir;
        configure(seq,focus,dir.path()); seq.setHeader("focusBeforeCapture",false); seq.setHeader("focusAfterFilterChange",true);
        seq.updateRow(0,"filter",1); seq.updateRow(0,"count",2);
        seq.addRow(); seq.updateRow(1,"filter",1); seq.updateRow(1,"gain",211); seq.updateRow(1,"exposure",0.2);
        QVERIFY(seq.start()); QTRY_COMPARE_WITH_TIMEOUT(rig->taskFrames.load(),1,20000);
        const int scans=rig->focusFrames.load(); QVERIFY(scans>=8); QCOMPARE(rig->gain.load(),173);
        unsigned actualExposure=0;
        QMetaObject::invokeMethod(camera,[&] { actualExposure=camera->exposure; },Qt::BlockingQueuedConnection);
        QCOMPARE(actualExposure,100000u);
        deliver(camera); QTRY_COMPARE(rig->taskFrames.load(),2); QCOMPARE(rig->focusFrames.load(),scans);
        deliver(camera); QTRY_COMPARE(rig->taskFrames.load(),3); QCOMPARE(rig->focusFrames.load(),scans); QCOMPARE(rig->gain.load(),211);
        QMetaObject::invokeMethod(camera,[&] { actualExposure=camera->exposure; },Qt::BlockingQueuedConnection);
        QCOMPARE(actualExposure,200000u);
        deliver(camera); QTRY_COMPARE(seq.state(),QString("Completed"));
    }
    void partialBacklashMoveResumesOriginalAbsoluteTarget() {
        auto rig=std::make_shared<SequenceFocusRig>(); auto *camera=new SequenceFocusCamera(rig);
        DeviceManager dm(camera,new SequenceFocusWheel(rig),new SequenceFocusFocuser(rig)); QVERIFY(connectRig(dm));
        AutoFocusController focus(&dm); SequenceController seq(&dm,nullptr,nullptr,&focus); QTemporaryDir dir;
        configure(seq,focus,dir.path()); focus.setSetting("backlash",40); seq.updateRow(0,"filter",0);
        seq.addRow(); seq.updateRow(1,"filter",1); seq.updateRow(1,"exposure",0.1);
        QVERIFY(dm.setFilterWheelFocusOffset(2,120)); QVERIFY(dm.setFilterFocusEnabled(true));
        QVERIFY(seq.start()); QTRY_COMPARE_WITH_TIMEOUT(rig->taskFrames.load(),1,20000);
        const int anchor=rig->position.load(), moves=rig->moves.load(), returned=rig->returnedMoves.load();
        const int eventCount=rig->log().size(), scans=rig->focusFrames.load();
        rig->holdMove=true; rig->partialPosition=anchor+40; deliver(camera);
        QTRY_COMPARE(seq.phase(),QString("FocusOffset")); QTRY_COMPARE(rig->moves.load(),moves+1);
        QCOMPARE(rig->position.load(),anchor+40); QVERIFY(rig->moving.load());
        QTRY_COMPARE(dm.device(DeviceManager::Focuser).value("metrics").toMap().value("position").toInt(),anchor+40);
        seq.pause(); QTest::qWait(200); QCOMPARE(seq.state(),QString("Pausing"));
        QVERIFY(seq.active()); QCOMPARE(rig->taskFrames.load(),1);
        QTRY_COMPARE(seq.state(),QString("Paused")); QVERIFY(!rig->moving.load());
        QCOMPARE(rig->position.load(),anchor+40); QVERIFY(dm.filterFocusSettings().value("baselineValid").toBool());
        rig->partialPosition=-1; rig->holdMove=false; rig->moveDelay=800; QVERIFY(seq.start());
        QTRY_COMPARE(rig->moves.load(),moves+2); QCOMPARE(rig->position.load(),anchor+80);
        QTest::qWait(200); QCOMPARE(rig->taskFrames.load(),1);
        QTRY_COMPARE(rig->moves.load(),moves+3); QCOMPARE(rig->position.load(),anchor+120);
        QTest::qWait(200); QCOMPARE(rig->taskFrames.load(),1);
        QTRY_COMPARE(rig->returnedMoves.load(),returned+3);
        QTest::qWait(200); QCOMPARE(rig->taskFrames.load(),1);
        QTRY_COMPARE_WITH_TIMEOUT(rig->taskFrames.load(),2,5000);
        QCOMPARE(rig->position.load(),anchor+120); QCOMPARE(rig->moves.load(),moves+3); QCOMPARE(rig->focusFrames.load(),scans);
        QStringList targets;
        for (const auto &event : rig->log().mid(eventCount)) if (event.startsWith("focus-target:")) targets.append(event);
        QCOMPARE(targets,QStringList({QString("focus-target:%1").arg(anchor+80),QString("focus-target:%1").arg(anchor+80),QString("focus-target:%1").arg(anchor+120)}));
        deliver(camera); QTRY_COMPARE(seq.state(),QString("Completed"));
    }
    void verifiedBaselineAndUnsafeFilterPreparation_data() {
        QTest::addColumn<QString>("change");
        for (const auto &change : {"missing-offset","limit","disconnect","reconnect","manual","drift","max-readback","wheel-manual"})
            QTest::newRow(change)<<QString(change);
    }
    void verifiedBaselineAndUnsafeFilterPreparation() {
        QFETCH(QString,change);
        auto rig=std::make_shared<SequenceFocusRig>(); auto *camera=new SequenceFocusCamera(rig); auto *focuser=new SequenceFocusFocuser(rig);
        DeviceManager dm(camera,new SequenceFocusWheel(rig),focuser); QVERIFY(connectRig(dm));
        AutoFocusController focus(&dm); SequenceController seq(&dm,nullptr,nullptr,&focus); QTemporaryDir dir;
        configure(seq,focus,dir.path()); seq.setHeader("focusBeforeCapture",false); seq.setHeader("focusAfterFilterChange",true); seq.updateRow(0,"filter",1);
        if (change!="missing-offset") QVERIFY(dm.setFilterWheelFocusOffset(2,change=="limit" ? 1000000 : 120));
        QVERIFY(dm.setFilterFocusEnabled(true)); QVERIFY(focus.start()); QTRY_VERIFY_WITH_TIMEOUT(!focus.busy(),20000); QVERIFY(focus.succeeded());
        QVERIFY(dm.filterFocusSettings().value("baselineValid").toBool());
        const int scans=rig->focusFrames.load(), anchor=rig->position.load();
        if (change=="disconnect" || change=="reconnect") {
            QVERIFY(dm.setConnected(DeviceManager::Focuser,false)); QTRY_VERIFY(!dm.device(DeviceManager::Focuser).value("connected").toBool());
            if (change=="reconnect") { QVERIFY(dm.setConnected(DeviceManager::Focuser,true)); QTRY_VERIFY(dm.device(DeviceManager::Focuser).value("connected").toBool()); }
        } else if (change=="manual") {
            QVERIFY(dm.moveFocuser(anchor+10)); QTRY_COMPARE(rig->position.load(),anchor+10);
        } else if (change=="drift" || change=="max-readback") {
            QMetaObject::invokeMethod(focuser,[focuser,change,anchor] {
                emit focuser->stateChanged(change=="drift" ? QVariantMap{{"position",anchor+1}} : QVariantMap{{"maxStep",64000}});
            },Qt::QueuedConnection);
            QTRY_VERIFY(!dm.filterFocusSettings().value("baselineValid").toBool());
        } else if (change=="wheel-manual") {
            QVERIFY(dm.moveFilterWheel(0)); QTRY_VERIFY(!dm.device(DeviceManager::FilterWheel).value("metrics").toMap().value("moving").toBool());
        }
        if (change!="missing-offset" && change!="limit") QVERIFY(!dm.filterFocusSettings().value("baselineValid").toBool());
        QTRY_VERIFY(seq.start()); QTRY_COMPARE(seq.state(),QString("Paused"));
        QCOMPARE(rig->taskFrames.load(),0); QCOMPARE(rig->focusFrames.load(),scans); seq.stop();
    }
    void centeringFinishesBeforeAutofocusAndTaskCapture() {
        auto rig=std::make_shared<SequenceFocusRig>(); auto *camera=new SequenceFocusCamera(rig);
        DeviceManager dm(camera,new SequenceFocusWheel(rig),nullptr,new SequenceFocusMount(rig),nullptr,new SequenceFocusFocuser(rig));
        PlateSolver solver; solver.setExecutable(QCoreApplication::applicationFilePath());
        connect(&solver,&PlateSolver::captureRequested,&dm,[&](double seconds) {
            if (!dm.captureForSolve(seconds)) solver.failCapture(dm.lastError());
        });
        connect(&dm,&DeviceManager::solvePhotoReady,&solver,&PlateSolver::acceptCapture);
        connect(&dm,&DeviceManager::solveCaptureFailed,&solver,&PlateSolver::failCapture);
        connect(&solver,&PlateSolver::cancelCaptureRequested,&dm,&DeviceManager::cancelSolveCapture);
        CenteringController center(&dm,&solver); AutoFocusController focus(&dm);
        SequenceController seq(&dm,nullptr,&center,&focus); QTemporaryDir directory;
        QVERIFY(connectRig(dm));
        QVERIFY(dm.selectDevice(DeviceManager::Mount,"mount")); QVERIFY(dm.setConnected(DeviceManager::Mount,true));
        QTRY_COMPARE(dm.connectedCount(),4);
        configure(seq,focus,directory.path()); dm.setStillSaveDirectory(directory.path()); seq.updateRow(0,"filter",2);
        seq.setHeader("target","Center then focus"); seq.setHeader("ra","10"); seq.setHeader("dec","0");
        seq.setHeader("centerBeforeCapture",true); seq.setHeader("centerExposure",0.02);
        QSignalSpy saves(&dm,&DeviceManager::stillCaptured), frames(&dm,&DeviceManager::stillFrameReceived);
        connect(&center,&CenteringController::changed,&seq,[&] {
            if (!center.busy() && center.succeeded()) rig->record("center-verified");
        });
        connect(&focus,&AutoFocusController::changed,&seq,[&] {
            if (!focus.busy() && focus.succeeded()) rig->record("focus-verified");
        });
        QVERIFY(seq.start()); QCOMPARE(seq.phase(),"Centering");
        QTRY_VERIFY_WITH_TIMEOUT(rig->centerPending.load(),5000);
        QVERIFY(center.busy()); QVERIFY(!focus.busy()); QCOMPARE(rig->moves.load(),0);
        QCOMPARE(rig->gain.load(),100); QCOMPARE(rig->taskFrames.load(),0);
        QCOMPARE(seq.saved(),0); QCOMPARE(seq.exposed(),0); QCOMPARE(saves.size(),0); QCOMPARE(frames.size(),0);
        QMetaObject::invokeMethod(camera,[camera] { camera->deliverCenter(); },Qt::QueuedConnection);
        QTRY_VERIFY_WITH_TIMEOUT(rig->taskPending.load(),20000);
        QVERIFY2(center.succeeded(),qPrintable(center.status())); QVERIFY(!solver.busy());
        QVERIFY2(focus.succeeded(),qPrintable(focus.status()));
        QCOMPARE(rig->centerFrames.load(),1); QCOMPARE(rig->focusFrames.load(),8);
        QCOMPARE(seq.saved(),0); QCOMPARE(seq.exposed(),0); QCOMPARE(saves.size(),0); QCOMPARE(frames.size(),0);
        QCOMPARE(QDir(directory.path()).entryList({"*.fits"},QDir::Files).size(),0);
        const auto events=rig->log();
        QVERIFY(events.indexOf("center-verified")>=0);
        QVERIFY(events.indexOf("center-verified")<events.indexOf("filter-request:2"));
        QVERIFY(events.indexOf("center-verified")<events.indexOf("gain-readback:173"));
        QVERIFY(events.indexOf("gain-return:173")<events.indexOf("focus-move"));
        QVERIFY(events.indexOf("focus-verified")>=0);
        QVERIFY(events.indexOf("focus-verified")<events.indexOf("task-request"));
        deliver(camera); QTRY_COMPARE(seq.state(),"Completed");
        QCOMPARE(seq.saved(),1); QCOMPARE(seq.exposed(),1); QCOMPARE(seq.failed(),0);
        QCOMPARE(saves.size(),1); QCOMPARE(frames.size(),1);
        QCOMPARE(QDir(directory.path()).entryList({"*.fits"},QDir::Files).size(),1);
    }
    void firstEnabledLightIsPreparedAndVerifiedBeforeAnyTask() {
        auto rig=std::make_shared<SequenceFocusRig>(); rig->holdFilter=true; rig->gainDelay=500;
        auto *camera=new SequenceFocusCamera(rig); auto *wheel=new SequenceFocusWheel(rig);
        DeviceManager dm(camera,wheel,new SequenceFocusFocuser(rig)); AutoFocusController focus(&dm);
        SequenceController seq(&dm,nullptr,nullptr,&focus); QTemporaryDir directory;
        QVERIFY(connectRig(dm)); configure(seq,focus,directory.path()); dm.setStillSaveDirectory(directory.path());
        seq.updateRow(0,"enabled",false); seq.updateRow(0,"filter",4); seq.updateRow(0,"gain",400);
        seq.addRow(); seq.updateRow(1,"type","DARK"); seq.updateRow(1,"exposure",0.1); seq.updateRow(1,"gain",44);
        seq.addRow(); seq.updateRow(2,"filter",2); seq.updateRow(2,"exposure",0.1); seq.updateRow(2,"gain",173);
        seq.updateRow(2,"count",2);
        QSignalSpy saves(&dm,&DeviceManager::stillCaptured), frames(&dm,&DeviceManager::stillFrameReceived), photos(&dm,&DeviceManager::mainPhotoReady);
        connect(&focus,&AutoFocusController::changed,&seq,[&] {
            if (!focus.busy() && focus.succeeded()) rig->record("focus-verified");
        });
        QVERIFY(seq.start()); QCOMPARE(seq.phase(),"FocusFilter");
        QTRY_VERIFY(rig->log().contains("filter-request:2"));
        QCOMPARE(rig->focusFrames.load(),0); QCOMPARE(rig->taskFrames.load(),0);
        QVERIFY(!focus.start()); QVERIFY(!dm.moveFocuser(5100));
        const auto settings=focus.settings(); focus.setSetting("step",90); QCOMPARE(focus.settings(),settings);
        QMetaObject::invokeMethod(wheel,[wheel] { wheel->arrive(); },Qt::QueuedConnection);
        QTRY_VERIFY(rig->log().contains("gain-readback:173"));
        QCOMPARE(seq.phase(),"FocusGain");
        QTest::qWait(100); QCOMPARE(rig->moves.load(),0); QCOMPARE(rig->focusFrames.load(),0);
        rig->gainDelay=0;
        QTRY_VERIFY_WITH_TIMEOUT(rig->taskPending.load(),15000);
        QVERIFY2(focus.succeeded(),qPrintable(focus.status())); QVERIFY(!seq.needsAutofocus());
        QCOMPARE(rig->focusFrames.load(),8); QCOMPARE(seq.currentRow(),1);
        QCOMPARE(seq.saved(),0); QCOMPARE(seq.exposed(),0); QCOMPARE(seq.failed(),0);
        QCOMPARE(saves.size(),0); QCOMPARE(frames.size(),0); QCOMPARE(photos.size(),0);
        QCOMPARE(QDir(directory.path()).entryList({"*.fits"},QDir::Files).size(),0);
        const auto events=rig->log();
        QVERIFY(events.indexOf("filter-ready:2")<events.indexOf("gain-readback:173"));
        QVERIFY(events.indexOf("gain-return:173")<events.indexOf("focus-move"));
        QCOMPARE(events.count("focus-frame:2:173"),8);
        QVERIFY(events.indexOf("focus-verified")>=0);
        QVERIFY(events.indexOf("focus-verified")<events.indexOf("task-request"));
        seq.pause(); deliver(camera);
        QTRY_COMPARE(seq.state(),"Paused"); QCOMPARE(seq.saved(),1); QCOMPARE(seq.exposed(),1);
        QVERIFY(!focus.start()); QVERIFY(!dm.moveFocuser(5100));
        focus.setSetting("step",90); QCOMPARE(focus.settings(),settings);
        QVERIFY(seq.start()); QTRY_VERIFY(rig->taskPending.load());
        QCOMPARE(rig->focusFrames.load(),8); QCOMPARE(seq.currentRow(),2);
        deliver(camera); QTRY_COMPARE(seq.saved(),2); QTRY_VERIFY(rig->taskPending.load());
        deliver(camera); QTRY_COMPARE(seq.state(),"Completed");
        QCOMPARE(seq.saved(),3); QCOMPARE(seq.exposed(),3); QCOMPARE(seq.failed(),0);
        QCOMPARE(rig->focusFrames.load(),8); QCOMPARE(saves.size(),3);
        QCOMPARE(QDir(directory.path()).entryList({"*.fits"},QDir::Files).size(),3);
    }
    void unconfirmedGainNeverMovesOrCaptures_data() {
        QTest::addColumn<bool>("rejected");
        QTest::newRow("failure-after-correct-readback")<<true;
        QTest::newRow("incorrect-readback")<<false;
    }
    void unconfirmedGainNeverMovesOrCaptures() {
        QFETCH(bool,rejected);
        auto rig=std::make_shared<SequenceFocusRig>(); rig->rejectGain=rejected; rig->wrongGain=!rejected; rig->gainDelay=300;
        DeviceManager dm(new SequenceFocusCamera(rig),new SequenceFocusWheel(rig),new SequenceFocusFocuser(rig));
        AutoFocusController focus(&dm); SequenceController seq(&dm,nullptr,nullptr,&focus); QTemporaryDir directory;
        QVERIFY(connectRig(dm)); configure(seq,focus,directory.path()); QSignalSpy notices(&seq,&SequenceController::notice);
        QVERIFY(seq.start()); QTRY_COMPARE(seq.state(),"Paused");
        QCOMPARE(rig->moves.load(),0); QCOMPARE(rig->focusFrames.load(),0); QCOMPARE(rig->taskFrames.load(),0);
        QCOMPARE(seq.saved(),0); QCOMPARE(seq.exposed(),0); QVERIFY(!notices.isEmpty());
        QVERIFY(seq.needsAutofocus()); QVERIFY(!focus.busy());
        seq.stop(); QTRY_VERIFY(!seq.active());
    }
    void cancellationWaitsForGainCommandCompletion_data() {
        QTest::addColumn<bool>("pause");
        QTest::newRow("stop-gain")<<false;
        QTest::newRow("pause-gain")<<true;
    }
    void cancellationWaitsForGainCommandCompletion() {
        QFETCH(bool,pause);
        auto rig=std::make_shared<SequenceFocusRig>(); rig->gainDelay=500;
        DeviceManager dm(new SequenceFocusCamera(rig),new SequenceFocusWheel(rig),new SequenceFocusFocuser(rig));
        AutoFocusController focus(&dm); SequenceController seq(&dm,nullptr,nullptr,&focus); QTemporaryDir directory;
        QVERIFY(connectRig(dm)); configure(seq,focus,directory.path());
        QVERIFY(seq.start()); QTRY_VERIFY(rig->log().contains("gain-readback:173")); QCOMPARE(seq.phase(),"FocusGain");
        if (pause) seq.pause(); else seq.stop();
        QTest::qWait(100);
        QVERIFY(!rig->log().contains("gain-return:173"));
        QVERIFY(seq.active()); QCOMPARE(seq.state(),pause ? "Pausing" : "Stopping");
        QVERIFY(!focus.busy()); QCOMPARE(rig->moves.load(),0); QCOMPARE(rig->focusFrames.load(),0); QCOMPARE(rig->taskFrames.load(),0);
        QVERIFY(!dm.setCameraGain(100)); QVERIFY(!dm.moveFocuser(5000)); QVERIFY(!focus.start()); QVERIFY(!seq.start());
        QTRY_COMPARE(seq.state(),pause ? "Paused" : "Idle");
        QVERIFY(rig->log().contains("gain-return:173"));
        QCOMPARE(seq.saved(),0); QCOMPARE(seq.exposed(),0); QCOMPARE(seq.failed(),0);
        QCOMPARE(rig->moves.load(),0); QCOMPARE(rig->focusFrames.load(),0); QCOMPARE(rig->taskFrames.load(),0);
        if (pause) { QVERIFY(seq.active()); QVERIFY(seq.needsAutofocus()); seq.stop(); }
        QTRY_VERIFY(!seq.active()); QVERIFY(dm.setCameraGain(100));
    }
    void failedAutofocusPausesAndRetryRunsPreparationAgain() {
        auto rig=std::make_shared<SequenceFocusRig>(); rig->blank=true;
        auto *camera=new SequenceFocusCamera(rig);
        DeviceManager dm(camera,new SequenceFocusWheel(rig),new SequenceFocusFocuser(rig));
        AutoFocusController focus(&dm); SequenceController seq(&dm,nullptr,nullptr,&focus); QTemporaryDir directory;
        QVERIFY(connectRig(dm)); configure(seq,focus,directory.path()); QSignalSpy notices(&seq,&SequenceController::notice);
        QVERIFY(seq.start()); QTRY_COMPARE_WITH_TIMEOUT(seq.state(),"Paused",5000);
        QVERIFY(!focus.succeeded()); QVERIFY(!focus.busy()); QVERIFY(seq.needsAutofocus());
        QCOMPARE(rig->focusFrames.load(),1); QCOMPARE(rig->position.load(),5000); QCOMPARE(rig->taskFrames.load(),0);
        QCOMPARE(seq.saved(),0); QCOMPARE(seq.exposed(),0); QCOMPARE(seq.failed(),0); QVERIFY(!notices.isEmpty());
        QCOMPARE(QDir(directory.path()).entryList({"*.fits"},QDir::Files).size(),0);
        rig->blank=false; QVERIFY(seq.start()); QTRY_VERIFY_WITH_TIMEOUT(rig->taskPending.load(),15000);
        QVERIFY2(focus.succeeded(),qPrintable(focus.status())); QCOMPARE(rig->focusFrames.load(),9);
        deliver(camera); QTRY_COMPARE(seq.state(),"Completed"); QCOMPARE(seq.saved(),1); QCOMPARE(seq.exposed(),1);
    }
    void cancellationWaitsForActualStopAndRestore_data() {
        QTest::addColumn<bool>("pause"); QTest::addColumn<bool>("duringRaw");
        QTest::newRow("stop-moving")<<false<<false;
        QTest::newRow("pause-moving")<<true<<false;
        QTest::newRow("stop-raw")<<false<<true;
        QTest::newRow("pause-raw")<<true<<true;
    }
    void cancellationWaitsForActualStopAndRestore() {
        QFETCH(bool,pause); QFETCH(bool,duringRaw);
        auto rig=std::make_shared<SequenceFocusRig>();
        rig->holdMove=!duringRaw; rig->holdFocus=duringRaw; rig->restoreDelay=500;
        DeviceManager dm(new SequenceFocusCamera(rig),new SequenceFocusWheel(rig),new SequenceFocusFocuser(rig));
        AutoFocusController focus(&dm); SequenceController seq(&dm,nullptr,nullptr,&focus); QTemporaryDir directory;
        QVERIFY(connectRig(dm)); configure(seq,focus,directory.path());
        QVERIFY(seq.start());
        if (duringRaw) QTRY_COMPARE(rig->focusFrames.load(),1); else QTRY_VERIFY(rig->moving.load());
        if (pause) seq.pause(); else seq.stop();
        if (duringRaw) QTRY_COMPARE(rig->restores.load(),1); else QTRY_COMPARE(rig->halts.load(),1);
        QTest::qWait(100);
        QVERIFY(focus.busy()); QVERIFY(seq.active()); QCOMPARE(seq.state(),pause ? "Pausing" : "Stopping");
        QVERIFY(!dm.setCameraGain(100)); QVERIFY(!dm.moveFocuser(5000)); QVERIFY(!focus.start()); QVERIFY(!seq.start());
        QTRY_COMPARE_WITH_TIMEOUT(seq.state(),pause ? "Paused" : "Idle",5000);
        QVERIFY(!focus.busy()); QVERIFY(!focus.succeeded()); QVERIFY(!rig->moving.load());
        QCOMPARE(dm.cameraParameters(DeviceManager::MainCamera).value("exposureSeconds").toDouble(),5.0);
        QCOMPARE(seq.saved(),0); QCOMPARE(seq.exposed(),0); QCOMPARE(seq.failed(),0); QCOMPARE(rig->taskFrames.load(),0);
        QCOMPARE(QDir(directory.path()).entryList({"*.fits"},QDir::Files).size(),0);
        if (pause) {
            QVERIFY(seq.active()); QVERIFY(seq.needsAutofocus());
            QVERIFY(seq.start());
            if (duringRaw) QTRY_COMPARE(rig->focusFrames.load(),2); else QTRY_COMPARE(rig->moves.load(),2);
            seq.stop(); QTRY_COMPARE_WITH_TIMEOUT(seq.state(),"Idle",5000);
        }
        QVERIFY(!seq.active()); QVERIFY(dm.setCameraGain(100));
    }
};

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QCoreApplication::setOrganizationName("StellaMoraTests"); QCoreApplication::setApplicationName("SequenceAutofocus");
    const auto args=app.arguments();
    if (args.contains("-f")) {
        const auto input=args.value(args.indexOf("-f")+1);
        QFile ini(QFileInfo(input).path()+"/"+QFileInfo(input).completeBaseName()+".ini");
        if (!ini.open(QIODevice::WriteOnly)) return 2;
        ini.write("PLTSOLVD=T\nCRVAL1=150\nCRVAL2=0\nCRPIX1=50.5\nCRPIX2=25.5\nCD1_1=-0.01\nCD1_2=0\nCD2_1=0\nCD2_2=0.01\n");
        return 0;
    }
    SequenceAutofocusTest test; return QTest::qExec(&test,argc,argv);
}
#include "tst_sequence_autofocus.moc"
