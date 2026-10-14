#include <QtTest>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include "focus/AutoFocusController.h"
#include "devices/DeviceManager.h"
#include "devices/CameraBackend.h"
#include "devices/FilterWheelBackend.h"
#include "devices/FocuserBackend.h"

struct FocusRig {
    std::atomic<int> position{5000}, captures{0}, moves{0}, halts{0};
    std::atomic<bool> moving{false}, holdMove{false}, holdCapture{false}, blank{false}, rejectHalt{false}, badVerification{false};
    std::atomic<int> haltDelay{0}, restoreDelay{0}, cancelDelay{0}, restores{0};
    std::atomic<bool> outlierFrames{false}, delayedPhysicalHalt{false}, capturedMoving{false}, disconnectOnCancel{false}, rejectRestore{false};
    std::atomic<qint64> stoppedAt{0}, shortestSettle{1000000};
};

static qint64 monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

class FocusWheel final : public FilterWheelBackend {
public:
    void scan() override { emit scanFinished({},{}); }
    void open(const QString &) override { emit connectionFinished(false,{},{}); }
    void close() override {}
    void moveTo(int,bool) override {}
    void calibrate() override {}
    void setSlotCount(int) override {}
};

class FocusCamera final : public CameraBackend {
public:
    explicit FocusCamera(std::shared_ptr<FocusRig> rig) : rig(std::move(rig)) {}
    std::shared_ptr<FocusRig> rig;
    void scan() override { emit scanFinished({QVariantMap{{"id","camera"},{"name","Camera"}}},{}); }
    void open(const QString &) override { emit connectionFinished(true,{{"exposureUs",5000000},{"gain",100},{"previewing",false}},{}); }
    void close() override { emit connectionFinished(false,{},{}); }
    void setExposure(unsigned us) override {
        if (us==5000000) {
            ++rig->restores;
            if (rig->rejectRestore) { emit commandFailed("restore rejected"); return; }
        }
        emit stateChanged({{"exposureUs",us}});
        if (us==5000000 && rig->restoreDelay) QThread::msleep(rig->restoreDelay);
    }
    void setGain(unsigned value) override { emit stateChanged({{"gain",value}}); }
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
        if (rig->disconnectOnCancel) emit connectionLost("disconnected while cancelling");
        else emit stillFailed("cancelled");
        if (rig->cancelDelay) QThread::msleep(rig->cancelDelay);
    }
    void requestStill() override {
        ++rig->captures;
        const qint64 settled=monotonicMs()-rig->stoppedAt.load();
        rig->shortestSettle=std::min(rig->shortestSettle.load(),settled);
        if (rig->moving) rig->capturedMoving=true;
        if (rig->holdCapture) return;
        constexpr int width=320,height=240;
        QVector<quint16> raw(width*height,100);
        if (!rig->blank) {
            const int frame=rig->captures-1;
            const bool outlier=rig->outlierFrames && frame%3==(frame/3)%3;
            const double sigma=std::sqrt(2.2*2.2+std::pow((rig->position.load()-5040)/100.0,2))
                *((rig->badVerification && rig->captures>=10) || outlier ? 2 : 1);
            for (int cy : {50,120,190}) for (int cx : {60,160,260}) {
                for (int y=cy-22;y<=cy+22;++y) for (int x=cx-22;x<=cx+22;++x) {
                    const double r2=(x-cx)*(x-cx)+(y-cy)*(y-cy);
                    raw[y*width+x]+=quint16(16000*std::exp(-r2/(2*sigma*sigma)));
                }
            }
        }
        emit rawStillReady(raw,width,height,{{"RAWBITS",16}});
    }
};

class FocusFocuser final : public FocuserBackend {
public:
    explicit FocusFocuser(std::shared_ptr<FocusRig> rig) : rig(std::move(rig)) {}
    std::shared_ptr<FocusRig> rig;
    QList<int> targets;
    void scan() override { emit scanFinished({QVariantMap{{"id","focuser"},{"name","Focuser"}}},{}); }
    void open(const QString &) override { emit connectionFinished(true,{{"position",rig->position.load()},{"maxStep",65000},{"moving",false}},{}); }
    void close() override { emit connectionFinished(false,{},{}); }
    void moveTo(int position) override {
        ++rig->moves; targets.append(position); rig->position=position; rig->moving=true;
        emit stateChanged({{"position",position},{"moving",true}});
        if (!rig->holdMove) { rig->moving=false; rig->stoppedAt=monotonicMs(); emit stateChanged({{"position",position},{"moving",false}}); }
    }
    void halt() override {
        ++rig->halts;
        if (rig->rejectHalt) { emit stateChanged({{"error","stop rejected"}}); return; }
        if (rig->delayedPhysicalHalt) {
            QTimer::singleShot(500,this,[this] { rig->moving=false; emit stateChanged({{"position",rig->position.load()},{"moving",false}}); });
            return;
        }
        rig->moving=false;
        emit stateChanged({{"position",rig->position.load()},{"moving",false}});
        if (rig->haltDelay) QThread::msleep(rig->haltDelay);
    }
    void setReversed(bool) override {}
    void setMaxStep(int) override {}
    void setZero() override {}
    void setCurrentPosition(int) override {}
};

class AutoFocusTest final : public QObject {
    Q_OBJECT
    static bool connectRig(DeviceManager &dm) {
        dm.refreshDevices();
        QElapsedTimer timeout; timeout.start();
        while (dm.scanning() && timeout.elapsed()<3000) QTest::qWait(10);
        if (!dm.selectDevice(DeviceManager::MainCamera,"camera") || !dm.selectDevice(DeviceManager::Focuser,"focuser")) return false;
        if (!dm.setConnected(DeviceManager::MainCamera,true) || !dm.setConnected(DeviceManager::Focuser,true)) return false;
        while (dm.connectedCount()!=2 && timeout.elapsed()<3000) QTest::qWait(10);
        return dm.connectedCount()==2;
    }
private slots:
    void init() { QSettings().clear(); }
    void fullCycle_data() { QTest::addColumn<int>("frames"); QTest::newRow("single")<<1; QTest::newRow("median-three")<<3; }
    void fullCycle() {
        QFETCH(int,frames);
        auto rig=std::make_shared<FocusRig>(); rig->outlierFrames=frames==3;
        auto *focuser=new FocusFocuser(rig);
        DeviceManager dm(new FocusCamera(rig),new FocusWheel,focuser);
        AutoFocusController focus(&dm);
        QTemporaryDir dir; dm.setStillSaveDirectory(dir.path());
        QSignalSpy saves(&dm,&DeviceManager::stillCaptured), framesReceived(&dm,&DeviceManager::stillFrameReceived), photos(&dm,&DeviceManager::mainPhotoReady);
        QVERIFY(connectRig(dm));
        focus.setSetting("exposure",0.001); focus.setSetting("frames",frames); focus.setSetting("backlash",40);
        QVERIFY(focus.start());
        QVERIFY(!focus.start()); QVERIFY(!dm.setCameraGain(200)); QVERIFY(!dm.moveFocuser(4900)); QVERIFY(!dm.captureStill(1));
        QTRY_VERIFY_WITH_TIMEOUT(!focus.busy(),20000);
        QVERIFY2(focus.succeeded(),qPrintable(focus.status()));
        QCOMPARE(focus.samples().size(),9); QVERIFY(focus.fitCurve().size()>9);
        for (const auto &entry : focus.samples()) {
            const auto sample=entry.toMap();
            const double expected=std::sqrt(2.2*2.2+std::pow((sample.value("position").toInt()-5040)/100.0,2))*std::sqrt(2*std::log(2.0));
            QVERIFY(std::abs(sample.value("hfr").toDouble()-expected)<0.45);
        }
        QVERIFY(qAbs(focus.bestPosition()-5040)<=20);
        QVERIFY(std::isfinite(focus.bestHfr())); QVERIFY(focus.verifiedHfr()<=focus.bestHfr()*1.25);
        QVERIFY(focus.rSquared()>=0.8); QVERIFY(focus.starCount()>=5);
        QCOMPARE(rig->position.load(),focus.bestPosition()); QCOMPARE(rig->captures.load(),10*frames);
        QVERIFY(!rig->capturedMoving); QVERIFY(rig->shortestSettle>=500);
        QList<int> targets;
        QMetaObject::invokeMethod(focuser,[&] { targets=focuser->targets; },Qt::BlockingQueuedConnection);
        QCOMPARE(targets.first(),4640); QCOMPARE(targets[1],4680);
        QCOMPARE(targets[targets.size()-2],focus.bestPosition()-40); QCOMPARE(targets.last(),focus.bestPosition());
        QCOMPARE(dm.cameraParameters(1).value("exposureSeconds").toDouble(),5.0);
        QCOMPARE(saves.size(),0); QCOMPARE(framesReceived.size(),0); QCOMPARE(photos.size(),0);
        QCOMPARE(QDir(dir.path()).entryList({"*.fits"},QDir::Files).size(),0);
        QVERIFY(dm.setCameraGain(150));
    }
    void failedStarsReturnToOriginal() {
        auto rig=std::make_shared<FocusRig>(); rig->blank=true;
        DeviceManager dm(new FocusCamera(rig),new FocusWheel,new FocusFocuser(rig)); AutoFocusController focus(&dm);
        QVERIFY(connectRig(dm)); QVERIFY(focus.start());
        QTRY_VERIFY_WITH_TIMEOUT(!focus.busy(),7000);
        QVERIFY(!focus.succeeded()); QCOMPARE(rig->position.load(),5000); QCOMPARE(rig->captures.load(),1);
        QCOMPARE(dm.cameraParameters(1).value("exposureSeconds").toDouble(),5.0);
        QVERIFY(dm.setCameraGain(150));
    }
    void worseVerificationReturnsToOriginal() {
        auto rig=std::make_shared<FocusRig>(); rig->badVerification=true;
        DeviceManager dm(new FocusCamera(rig),new FocusWheel,new FocusFocuser(rig)); AutoFocusController focus(&dm);
        QVERIFY(connectRig(dm)); QVERIFY(focus.start());
        QTRY_VERIFY_WITH_TIMEOUT(!focus.busy(),20000);
        QVERIFY(!focus.succeeded()); QVERIFY(std::isfinite(focus.verifiedHfr()));
        QCOMPARE(rig->position.load(),5000); QCOMPARE(rig->captures.load(),10);
        QCOMPARE(dm.cameraParameters(1).value("exposureSeconds").toDouble(),5.0);
    }
    void stopWaitsForHaltAcknowledgement() {
        auto rig=std::make_shared<FocusRig>(); rig->holdMove=true; rig->haltDelay=500;
        DeviceManager dm(new FocusCamera(rig),new FocusWheel,new FocusFocuser(rig)); AutoFocusController focus(&dm);
        QVERIFY(connectRig(dm)); QVERIFY(focus.start());
        QTRY_VERIFY(rig->moving.load()); focus.cancel();
        QTRY_COMPARE(rig->halts.load(),1); QTest::qWait(100);
        QVERIFY(focus.busy()); QVERIFY(!dm.setCameraGain(150)); QVERIFY(!dm.moveFocuser(5000));
        QTRY_VERIFY(!focus.busy()); QVERIFY(!focus.succeeded()); QCOMPARE(rig->moves.load(),1);
        QCOMPARE(rig->position.load(),4680); QCOMPARE(rig->captures.load(),0);
    }
    void stopWaitsForPhysicalHaltAfterAcknowledgement() {
        auto rig=std::make_shared<FocusRig>(); rig->holdMove=true; rig->delayedPhysicalHalt=true;
        DeviceManager dm(new FocusCamera(rig),new FocusWheel,new FocusFocuser(rig)); AutoFocusController focus(&dm);
        QVERIFY(connectRig(dm)); QVERIFY(focus.start()); QTRY_VERIFY(rig->moving.load());
        focus.cancel(); QTRY_COMPARE(rig->halts.load(),1); QTest::qWait(150);
        QVERIFY(focus.busy()); QVERIFY(!dm.setCameraGain(150)); QVERIFY(rig->moving.load());
        QTRY_VERIFY(!focus.busy()); QVERIFY(!rig->moving.load()); QCOMPARE(rig->moves.load(),1);
    }
    void repeatedStopRetriesFailedHalt() {
        auto rig=std::make_shared<FocusRig>(); rig->holdMove=true; rig->rejectHalt=true;
        DeviceManager dm(new FocusCamera(rig),new FocusWheel,new FocusFocuser(rig)); AutoFocusController focus(&dm);
        QVERIFY(connectRig(dm)); QVERIFY(focus.start()); QTRY_VERIFY(rig->moving.load());
        focus.cancel(); QTRY_COMPARE(rig->halts.load(),1); QTest::qWait(200);
        QVERIFY(focus.busy()); QVERIFY(!dm.moveFocuser(5000));
        rig->rejectHalt=false; focus.cancel();
        QTRY_VERIFY(!focus.busy()); QCOMPARE(rig->halts.load(),2); QCOMPARE(rig->moves.load(),1);
    }
    void stopRawWaitsForExposureRestore() {
        auto rig=std::make_shared<FocusRig>(); rig->holdCapture=true; rig->restoreDelay=500;
        DeviceManager dm(new FocusCamera(rig),new FocusWheel,new FocusFocuser(rig)); AutoFocusController focus(&dm);
        QVERIFY(connectRig(dm)); focus.setSetting("exposure",0.01); QVERIFY(focus.start());
        QTRY_COMPARE_WITH_TIMEOUT(rig->captures.load(),1,3000); focus.cancel(); QTest::qWait(100);
        QVERIFY(focus.busy()); QVERIFY(!dm.setCameraGain(150));
        QTRY_VERIFY(!focus.busy()); QVERIFY(!focus.succeeded()); QCOMPARE(rig->moves.load(),1);
        QCOMPARE(dm.cameraParameters(1).value("exposureSeconds").toDouble(),5.0);
    }
    void disconnectEndsWithoutFurtherMotion() {
        auto rig=std::make_shared<FocusRig>(); rig->holdMove=true;
        auto *focuser=new FocusFocuser(rig);
        DeviceManager dm(new FocusCamera(rig),new FocusWheel,focuser); AutoFocusController focus(&dm);
        QVERIFY(connectRig(dm)); QVERIFY(focus.start()); QTRY_VERIFY(rig->moving.load());
        QMetaObject::invokeMethod(focuser,[focuser] { emit focuser->connectionLost("disconnected"); },Qt::QueuedConnection);
        QTRY_VERIFY(!focus.busy()); QVERIFY(!focus.succeeded()); QCOMPARE(rig->moves.load(),1); QCOMPARE(rig->captures.load(),0);
        QVERIFY(dm.setCameraGain(150));
    }
    void disconnectedRawWaitsForCancellationAcknowledgement() {
        auto rig=std::make_shared<FocusRig>(); rig->holdCapture=true; rig->disconnectOnCancel=true; rig->cancelDelay=500;
        DeviceManager dm(new FocusCamera(rig),new FocusWheel,new FocusFocuser(rig)); AutoFocusController focus(&dm);
        QVERIFY(connectRig(dm)); QVERIFY(focus.start());
        QTRY_COMPARE_WITH_TIMEOUT(rig->captures.load(),1,3000); focus.cancel(); QTest::qWait(100);
        QVERIFY(focus.busy()); QVERIFY(!dm.moveFocuser(5000));
        QTRY_VERIFY(!focus.busy()); QVERIFY(!focus.succeeded()); QCOMPARE(rig->moves.load(),1);
    }
    void userCanDisconnectMainCameraDuringAutofocus() {
        auto rig=std::make_shared<FocusRig>(); rig->holdCapture=true;
        DeviceManager dm(new FocusCamera(rig),new FocusWheel,new FocusFocuser(rig)); AutoFocusController focus(&dm);
        QVERIFY(connectRig(dm)); QVERIFY(focus.start()); QTRY_COMPARE_WITH_TIMEOUT(rig->captures.load(),1,3000);
        QVERIFY(dm.setConnected(DeviceManager::MainCamera,false));
        QTRY_VERIFY(!focus.busy()); QVERIFY(!focus.succeeded());
        QVERIFY(!dm.device(DeviceManager::MainCamera).value("connected").toBool());
        QCOMPARE(rig->captures.load(),1); QVERIFY(dm.moveFocuser(5000));
    }
    void failedExposureRestoreKeepsOwnershipUntilStopRetry() {
        auto rig=std::make_shared<FocusRig>(); rig->holdCapture=true; rig->rejectRestore=true;
        DeviceManager dm(new FocusCamera(rig),new FocusWheel,new FocusFocuser(rig)); AutoFocusController focus(&dm);
        QVERIFY(connectRig(dm)); focus.setSetting("exposure",0.01); QVERIFY(focus.start());
        QTRY_COMPARE_WITH_TIMEOUT(rig->captures.load(),1,3000); focus.cancel();
        QTRY_COMPARE(rig->restores.load(),1); QTest::qWait(200);
        QVERIFY(focus.busy()); QVERIFY(!dm.moveFocuser(5000));
        QCOMPARE(dm.cameraParameters(1).value("exposureSeconds").toDouble(),0.01);
        rig->rejectRestore=false; focus.cancel();
        QTRY_VERIFY(!focus.busy()); QCOMPARE(rig->restores.load(),2);
        QCOMPARE(dm.cameraParameters(1).value("exposureSeconds").toDouble(),5.0);
        QCOMPARE(rig->moves.load(),1);
    }
    void travelValidationDoesNotClip() {
        auto rig=std::make_shared<FocusRig>(); rig->position=330;
        DeviceManager dm(new FocusCamera(rig),new FocusWheel,new FocusFocuser(rig)); AutoFocusController focus(&dm);
        QVERIFY(connectRig(dm)); focus.setSetting("backlash",20);
        QVERIFY(!focus.start()); QVERIFY(!focus.busy()); QCOMPARE(rig->moves.load(),0);
        focus.setSetting("backlash",0); QVERIFY(focus.start()); focus.cancel(); QTRY_VERIFY(!focus.busy());
    }
    void settingsRejectInvalidValuesAndPersist() {
        auto rig=std::make_shared<FocusRig>();
        DeviceManager dm(new FocusCamera(rig),new FocusWheel,new FocusFocuser(rig));
        AutoFocusController focus(&dm);
        focus.setSetting("step",100); focus.setSetting("points",7); focus.setSetting("exposure",1.25); focus.setSetting("frames",3); focus.setSetting("backlash",20);
        const auto valid=focus.settings();
        QCOMPARE(valid.value("step").toInt(),100); QCOMPARE(valid.value("points").toInt(),7);
        QCOMPARE(valid.value("exposure").toDouble(),1.25); QCOMPARE(valid.value("frames").toInt(),3); QCOMPARE(valid.value("backlash").toInt(),20);
        focus.setSetting("step",0); focus.setSetting("points",8); focus.setSetting("exposure",qQNaN()); focus.setSetting("frames",4); focus.setSetting("backlash",-1);
        QCOMPARE(focus.settings(),valid);
        AutoFocusController restored(&dm); QCOMPARE(restored.settings(),valid);
    }
};

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QCoreApplication::setOrganizationName("StellaMoraTests"); QCoreApplication::setApplicationName("AutoFocus");
    AutoFocusTest test; return QTest::qExec(&test,argc,argv);
}
#include "tst_autofocus.moc"
