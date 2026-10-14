#include <QtTest>
#include <QTemporaryDir>
#include "astro/CenteringController.h"
#include "astro/PlateSolver.h"
#include "devices/DeviceManager.h"
#include "devices/CameraBackend.h"
#include "devices/MountBackend.h"
#include "sequence/SequenceController.h"

class CenterCamera : public CameraBackend {
public:
    bool hold = false;
    int captures = 0;
    void scan() override { emit scanFinished({QVariantMap{{"id","camera"},{"name","Camera"}}},{}); }
    void open(const QString &) override { emit connectionFinished(true,{{"exposureUs",5000000}},{}); }
    void close() override { emit connectionFinished(false,{},{}); }
    void setExposure(unsigned us) override { emit stateChanged({{"exposureUs",us}}); }
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
    void stopPreview() override { emit stillFailed("cancelled"); }
    void requestStill() override {
        ++captures;
        if (!hold) emit rawStillReady(QVector<quint16>(5000,100),100,50,{});
    }
};
class CenterMount : public MountBackend {
public:
    bool hold = false;
    QString failure;
    bool delayedStop = false;
    bool rejectStop = false;
    QList<QPair<double,double>> targets;
    void scan() override { emit scanFinished({QVariantMap{{"id","mount"},{"name","Mount"}}},{}); }
    void open(const QString &) override { emit connectionFinished(true,{{"skyRa",10.0},{"skyDec",0.0},{"tracking",true},{"slewing",false},{"atPark",false}},{}); }
    void close() override { emit connectionFinished(false,{},{}); }
    void setup(const QString &) override {}
    QString slewToJ2000(double ra,double dec) override {
        targets.append({ra,dec});
        if (!failure.isEmpty()) return failure;
        emit stateChanged({{"gotoState",hold?"Slewing":"Arrived"},{"slewing",hold},{"skyRa",ra},{"skyDec",dec}});
        return {};
    }
    QString stopSlew() override {
        if (rejectStop) return "stop rejected";
        emit stateChanged({{"gotoState","Stopped"},{"slewing",delayedStop}});
        if (delayedStop) QTimer::singleShot(500,this,[this] { emit stateChanged({{"slewing",false}}); });
        return {};
    }
};
class CenteringTest : public QObject {
    Q_OBJECT
private slots:
    void sequencePreparation_data() {
        QTest::addColumn<QString>("mode");
        for (const auto &mode : {"converge","solvefail","cancelmove","pause","retry","pauseResume"}) QTest::newRow(mode)<<QString(mode);
    }
    void sequencePreparation() {
        QFETCH(QString,mode);
        QTemporaryDir dir;
        auto *camera=new CenterCamera; auto *mount=new CenterMount;
        mount->hold=(mode=="cancelmove" || mode=="pause" || mode=="pauseResume"); mount->delayedStop=true;
        DeviceManager dm(camera,nullptr,nullptr,mount,nullptr);
        PlateSolver solver; solver.setExecutable(QCoreApplication::applicationFilePath());
        connect(&solver,&PlateSolver::captureRequested,&dm,[&](double s) { if (!dm.captureForSolve(s)) solver.failCapture(dm.lastError()); });
        connect(&dm,&DeviceManager::solvePhotoReady,&solver,&PlateSolver::acceptCapture);
        connect(&dm,&DeviceManager::solveCaptureFailed,&solver,&PlateSolver::failCapture);
        connect(&solver,&PlateSolver::cancelCaptureRequested,&dm,&DeviceManager::cancelSolveCapture);
        CenteringController center(&dm,&solver);
        SequenceController seq(&dm,nullptr,&center);
        dm.refreshDevices(); QTRY_VERIFY(!dm.scanning());
        QVERIFY(dm.selectDevice(0,"mount")); QVERIFY(dm.selectDevice(1,"camera"));
        QVERIFY(dm.setConnected(0,true)); QVERIFY(dm.setConnected(1,true));
        QTRY_COMPARE(dm.connectedCount(),2);
        seq.addRow(); seq.setHeader("directory",dir.path()); seq.setHeader("target","Test");
        seq.setHeader("ra","10"); seq.setHeader("dec","0"); seq.setHeader("centerBeforeCapture",true);
        qputenv("CENTER_TEST_COUNTER",QFile::encodeName(dir.filePath("counter")));
        qputenv("CENTER_TEST_MODE",mode=="retry" ? QByteArray("solvefail") : mode.toUtf8());
        QVERIFY(seq.start()); QCOMPARE(seq.phase(),"Centering"); QCOMPARE(seq.saved(),0);
        QVERIFY(!dm.captureStill(1)); QVERIFY(!dm.slewMountToJ2000(11,0));
        QVERIFY(!center.start(11,0,"Other",2,0));
        if (mode=="cancelmove" || mode=="pause" || mode=="pauseResume") {
            QTRY_COMPARE(dm.device(0).value("metrics").toMap().value("gotoState").toString(),"Slewing");
            if (mode=="pause" || mode=="pauseResume") seq.pause(); else seq.stop();
            QTest::qWait(200); QVERIFY(seq.active()); QVERIFY(center.busy());
            if (mode=="pause" || mode=="pauseResume") {
                QTRY_COMPARE(seq.state(),"Paused"); QVERIFY(seq.needsCentering());
                QCOMPARE(seq.exposed(),0);
                if (mode=="pauseResume") {
                    QMetaObject::invokeMethod(mount,[mount] { mount->hold=false; },Qt::BlockingQueuedConnection);
                    qputenv("CENTER_TEST_MODE","converge"); QVERIFY(seq.start()); QCOMPARE(seq.phase(),"Centering");
                    QTRY_COMPARE_WITH_TIMEOUT(seq.state(),"Completed",15000); QCOMPARE(seq.saved(),1);
                    QList<QPair<double,double>> targets;
                    QMetaObject::invokeMethod(mount,[&] { targets=mount->targets; },Qt::BlockingQueuedConnection);
                    QCOMPARE(targets.size(),3);
                } else seq.stop();
            }
            QTRY_VERIFY(!seq.active()); QCOMPARE(seq.saved(),mode=="pauseResume"?1:0); QCOMPARE(seq.exposed(),mode=="pauseResume"?1:0);
        } else {
            if (mode=="solvefail" || mode=="retry") {
                QTRY_COMPARE_WITH_TIMEOUT(seq.state(),"Paused",10000);
                QCOMPARE(seq.saved(),0); QCOMPARE(seq.exposed(),0); QVERIFY(seq.needsCentering());
                if (mode=="retry") { qputenv("CENTER_TEST_MODE","converge"); QVERIFY(seq.start()); }
                else { seq.stop(); QTRY_VERIFY(!seq.active()); }
            }
            if (mode=="converge" || mode=="retry") {
                QTRY_COMPARE_WITH_TIMEOUT(seq.state(),"Completed",15000);
                QCOMPARE(seq.saved(),1); QCOMPARE(seq.exposed(),1); QVERIFY(center.succeeded());
                QCOMPARE(QDir(dir.path()).entryList({"*.fits"},QDir::Files).size(),1);
            }
        }
        int captures=0;
        QMetaObject::invokeMethod(camera,[&] { captures=camera->captures; },Qt::BlockingQueuedConnection);
        QCOMPARE(captures,(mode=="converge" || mode=="pauseResume")?3 : mode=="retry"?4 : mode=="solvefail"?1:0);
        QVERIFY(!center.busy());
    }
    void sphericalCorrection() {
        double ra,dec;
        QVERIFY(CenteringMath::correct(0.1,0,23.9,0,23.8,0,ra,dec));
        QVERIFY(qAbs(ra-0.2)<1e-8); QVERIFY(qAbs(dec)<1e-8);
        QVERIFY(CenteringMath::correct(4,89.8,4,89.8,4,89.7,ra,dec));
        QVERIFY(qAbs(dec-89.9)<1e-7);
        QVERIFY(!CenteringMath::correct(1,0,1,0,qQNaN(),0,ra,dec));
        QVERIFY(!CenteringMath::correct(1,0,1,0,13,0,ra,dec));
    }
    void lifecycle_data() {
        QTest::addColumn<QString>("mode");
        for (const auto &m : {"converge","limit","cancel","movefail","solvefail","disconnect","cancelmove","stopfail","excessive","finalunsafe","externalmove"}) QTest::newRow(m)<<QString(m);
    }
    void lifecycle() {
        QFETCH(QString,mode);
        auto *camera = new CenterCamera;
        auto *mount = new CenterMount;
        if (mode=="cancel" || mode=="externalmove") camera->hold=true;
        if (mode=="disconnect") mount->hold=true;
        if (mode=="cancelmove" || mode=="stopfail") { mount->hold=true; mount->delayedStop=true; }
        if (mode=="stopfail") mount->rejectStop=true;
        if (mode=="movefail") mount->failure="driver rejected";
        DeviceManager dm(camera,nullptr,nullptr,mount,nullptr);
        PlateSolver solver;
        solver.setExecutable(QCoreApplication::applicationFilePath());
        connect(&solver,&PlateSolver::captureRequested,&dm,[&](double s) { if (!dm.captureForSolve(s)) solver.failCapture(dm.lastError()); });
        connect(&dm,&DeviceManager::solvePhotoReady,&solver,&PlateSolver::acceptCapture);
        connect(&dm,&DeviceManager::solveCaptureFailed,&solver,&PlateSolver::failCapture);
        connect(&solver,&PlateSolver::cancelCaptureRequested,&dm,&DeviceManager::cancelSolveCapture);
        CenteringController controller(&dm,&solver);
        if (mode=="finalunsafe") connect(&controller,&CenteringController::changed,&dm,[&] {
            if (controller.busy() && controller.separationArcmin()<1)
                QMetaObject::invokeMethod(mount,[mount] { emit mount->stateChanged({{"tracking",false}}); },Qt::QueuedConnection);
        });
        QVERIFY(!controller.start(10,0,"Test",2,1,1,3));
        dm.refreshDevices(); QTRY_VERIFY(!dm.scanning());
        QVERIFY(dm.selectDevice(0,"mount")); QVERIFY(dm.selectDevice(1,"camera"));
        QVERIFY(dm.setConnected(0,true)); QVERIFY(dm.setConnected(1,true));
        QTRY_VERIFY(dm.device(0).value("connected").toBool() && dm.device(1).value("connected").toBool());
        QVERIFY(!controller.start(qQNaN(),0,"Test",2,1,1,3));
        QVERIFY(!controller.start(10,0,"Test",2,1,0,3));
        QTemporaryDir dir;
        SequenceController sequence(&dm);
        sequence.addRow(); sequence.setHeader("directory",dir.path());
        qputenv("CENTER_TEST_COUNTER",QFile::encodeName(dir.filePath("counter")));
        qputenv("CENTER_TEST_MODE",mode.toUtf8());
        QVERIFY(controller.start(10,0,"Test",2,1,1,3));
        QVERIFY(!controller.start(10,0,"Other",2,1,1,3));
        QVERIFY(!dm.setCameraGain(200)); QVERIFY(!dm.slewMountToJ2000(11,0));
        QVERIFY(!dm.startMountManualMove(0,20));
        QVERIFY(!solver.captureAndSolve(2,1));
        QVERIFY(!solver.solveFile(QUrl::fromLocalFile("unused.fits")));
        QVERIFY(!sequence.start());
        if (mode=="cancel") {
            QTRY_VERIFY_WITH_TIMEOUT(solver.busy(),5000);
            controller.cancel();
        }
        if (mode=="disconnect") {
            QMetaObject::invokeMethod(mount,[mount] { mount->close(); },Qt::QueuedConnection);
        }
        if (mode=="externalmove") {
            QTRY_VERIFY_WITH_TIMEOUT(solver.busy(),5000);
            QMetaObject::invokeMethod(mount,[mount] { emit mount->stateChanged({{"slewing",true}}); },Qt::QueuedConnection);
        }
        if (mode=="cancelmove" || mode=="stopfail") {
            QTRY_COMPARE(dm.device(0).value("metrics").toMap().value("gotoState").toString(),"Slewing");
            controller.cancel();
            QTest::qWait(200);
            QVERIFY(controller.busy());
            QVERIFY(!dm.slewMountToJ2000(11,0));
            if (mode=="stopfail") {
                QVERIFY(controller.status().contains(QStringLiteral("停止失败")));
                QMetaObject::invokeMethod(mount,[mount] { mount->rejectStop=false; },Qt::BlockingQueuedConnection);
                controller.cancel();
            }
        }
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),15000);
        QVERIFY(!solver.busy());
        QList<QPair<double,double>> targets;
        int captures;
        QMetaObject::invokeMethod(mount,[&] { targets=mount->targets; },Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(camera,[&] { captures=camera->captures; },Qt::BlockingQueuedConnection);
        if (mode=="converge") {
            QVERIFY(controller.succeeded()); QCOMPARE(targets.size(),2); QCOMPARE(captures,2);
            QVERIFY(qAbs(targets[1].first-10.01)<1e-5); QCOMPARE(controller.corrections(),1);
            QVERIFY(controller.separationArcmin()<1);
        } else {
            QVERIFY(!controller.succeeded());
            if (mode=="limit") { QCOMPARE(targets.size(),4); QCOMPARE(captures,4); QCOMPARE(controller.corrections(),3); }
            else QCOMPARE(targets.size(),mode=="finalunsafe" ? 2 : 1);
        }
        QTRY_VERIFY(dm.setCameraGain(100));
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QCoreApplication::setOrganizationName("StellaMoraTests"); QCoreApplication::setApplicationName("Centering");
    auto args=app.arguments();
    if (args.contains("-f")) {
        if (qgetenv("CENTER_TEST_MODE")=="solvefail") return 1;
        QFile counter(QString::fromLocal8Bit(qgetenv("CENTER_TEST_COUNTER")));
        int count=0;
        if (counter.open(QIODevice::ReadOnly)) { count=counter.readAll().toInt(); counter.close(); }
        if (!counter.open(QIODevice::WriteOnly)) return 2;
        counter.write(QByteArray::number(count+1)); counter.close();
        const auto mode=qgetenv("CENTER_TEST_MODE");
        const double ra=mode=="excessive" ? 130.0 : ((mode=="converge" || mode=="finalunsafe") && count>0)?150.0:149.85;
        const auto input=args.value(args.indexOf("-f")+1);
        QFile ini(QFileInfo(input).path()+"/"+QFileInfo(input).completeBaseName()+".ini");
        if (!ini.open(QIODevice::WriteOnly)) return 2;
        ini.write("PLTSOLVD=T\nCRVAL1="+QByteArray::number(ra,'f',8)+"\nCRVAL2=0\nCRPIX1=50.5\nCRPIX2=25.5\nCD1_1=-0.01\nCD1_2=0\nCD2_1=0\nCD2_2=0.01\n");
        return 0;
    }
    CenteringTest test; return QTest::qExec(&test,argc,argv);
}
#include "tst_centering.moc"
