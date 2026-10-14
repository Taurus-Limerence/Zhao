#include <QtTest>
#include <QTemporaryDir>
#include "astro/PlateSolveResult.h"
#include "imaging/FitsWriter.h"
#include "astro/PlateSolver.h"
#include <QJSEngine>

class TestPlateSolver : public QObject {
    Q_OBJECT
private slots:
    void captureSolveWaitsForOwnedFrameAndCancels() {
        PlateSolver solver;
        solver.setExecutable(QCoreApplication::applicationFilePath());
        qputenv("STELLAMORA_TEST_SOLVER","success");
        QSignalSpy requests(&solver,&PlateSolver::captureRequested);
        QSignalSpy stops(&solver,&PlateSolver::cancelCaptureRequested);
        QVERIFY(solver.captureAndSolve(2,1));
        QVERIFY(solver.busy()); QCOMPARE(requests.count(),1);
        QVERIFY(!solver.captureAndSolve(2,1));
        solver.setCurrentFrame(QVector<quint16>(5000,4),100,50,{});
        QVERIFY(solver.result().isEmpty()); QVERIFY(solver.busy());
        solver.cancel(); QCOMPARE(stops.count(),1); QVERIFY(solver.busy());
        solver.acceptCapture(QVector<quint16>(5000,4),100,50,{});
        QVERIFY(!solver.busy()); QVERIFY(solver.result().isEmpty());
        QVERIFY(solver.captureAndSolve(2,1));
        solver.failCapture("camera lost"); QVERIFY(!solver.busy());
        QVERIFY(solver.captureAndSolve(2,1));
        solver.acceptCapture(QVector<quint16>(5000,4),100,50,{});
        QTRY_VERIFY_WITH_TIMEOUT(!solver.busy(),5000);
        QCOMPARE(solver.result().value("corners").toList().size(),4);
    }
    void realImageSolve() {
        const QString input=qEnvironmentVariable("STELLAMORA_ASTAP_IMAGE");
        const QString exe=qEnvironmentVariable("STELLAMORA_ASTAP_SMOKE");
        if (input.isEmpty() || exe.isEmpty()) QSKIP("Provide STELLAMORA_ASTAP_IMAGE and STELLAMORA_ASTAP_SMOKE for real-image acceptance");
        QFile original(input); QVERIFY(original.open(QIODevice::ReadOnly));
        const auto before=QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256); original.close();
        auto cancel=std::make_shared<std::atomic_bool>(false);
        const auto result=PlateSolveJob::run(exe,input,{}, {},0,cancel);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256),before);
        QVERIFY2(!result.contains("error"),qPrintable(result.value("error").toString()));
        QVERIFY(result.value("corners").toList().size()==4);
        qInfo().noquote()<<QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact);
    }
    void installedAstapFailureSmoke() {
        const QString exe=qEnvironmentVariable("STELLAMORA_ASTAP_SMOKE");
        if (exe.isEmpty()) QSKIP("Set STELLAMORA_ASTAP_SMOKE to check an installed CLI without hardware");
        auto cancel=std::make_shared<std::atomic_bool>(false);
        const auto result=PlateSolveJob::run(exe,{},QVector<quint16>(512*512,100),{512,512},1,cancel,10000);
        QVERIFY(result.contains("error"));
        qInfo().noquote()<<result.value("error").toString();
    }
    void mirroredRotatedPolarAndInvalidMatrices() {
        QString error;
        const QByteArray base="PLTSOLVD=T\nCRVAL1=0\nCRVAL2=0\nCRPIX1=50.5\nCRPIX2=25.5\n";
        auto rotated=PlateSolveResult::fromAstap(base+"CD1_1=0\nCD1_2=0.01\nCD2_1=0.01\nCD2_2=0",{100,50},&error);
        QCOMPARE(rotated.value("positionAngle").toDouble(),90.0);
        auto c=rotated.value("corners").toList().first().toList();
        QVERIFY(c[0].toDouble()>359.7 && c[0].toDouble()<359.8);
        QVERIFY(c[1].toDouble()<-0.49);
        auto mirrored=PlateSolveResult::fromAstap(base+"CD1_1=0.01\nCD1_2=0\nCD2_1=0\nCD2_2=0.01",{100,50},&error);
        QVERIFY(mirrored.value("corners").toList().first().toList()[0].toDouble()>359.4);
        auto polar=base; polar.replace("CRVAL2=0","CRVAL2=90");
        auto pole=PlateSolveResult::fromAstap(polar+"CD1_1=-0.01\nCD1_2=0\nCD2_1=0\nCD2_2=0.01",{100,50},&error);
        QVERIFY(!pole.isEmpty());
        for(const auto &corner:pole.value("corners").toList()) {
            auto p=corner.toList(); QVERIFY(std::isfinite(p[0].toDouble())); QVERIFY(p[1].toDouble()>89 && p[1].toDouble()<90);
        }
        QVERIFY(PlateSolveResult::fromAstap(base+"CD1_1=0\nCD1_2=0\nCD2_1=0\nCD2_2=0",{100,50},&error).isEmpty());
        QVERIFY(PlateSolveResult::fromAstap(base+"CD1_1=1e308\nCD1_2=0\nCD2_1=0\nCD2_2=1e308",{100,50},&error).isEmpty());
    }
    void webFootprintDoesNotMoveViewOrPlanningLayer() {
        QFile html(QStringLiteral(STELLAMORA_SOURCE_DIR)+"/qml/assets/aladin.html");
        QVERIFY(html.open(QIODevice::ReadOnly));
        auto bytes=html.readAll();
        const int start=bytes.indexOf("<script>")+8, end=bytes.indexOf("</script>",start);
        QJSEngine js;
        js.evaluate("var window=this;");
        // Keep lexical `let aladin` and the browser doubles in the same script scope.
        QVERIFY(!js.evaluate(QString::fromUtf8(bytes.mid(start,end-start))+R"(
            var measured=[], moves=0, plannedClears=0, lastCenter=[], zoomChanges=0;
            aladin={addOverlay:function(o){},gotoRaDec:function(ra,dec){moves++;lastCenter=[ra,dec]},setFoV:function(){zoomChanges++}};
            overlay={removeAll:function(){plannedClears++}};
            var A={graphicOverlay:function(){return {removeAll:function(){measured=[]},addFootprints:function(p){measured=p}}},
                polygon:function(c,s){return {corners:c,style:s}}};
            stellaSolved([[359.9,0],[0.1,0],[0.1,1],[359.9,1]]);
        )").isError());
        QCOMPARE(js.evaluate("measured[0].corners[0][0]").toNumber(),359.9);
        QCOMPARE(js.evaluate("moves+plannedClears").toInt(),0);
        js.evaluate("stellaSolved([])");
        QCOMPARE(js.evaluate("measured.length").toInt(),0);
        js.evaluate("stellaInspectPhoto({raHours:1,decDeg:2,widthDeg:1,heightDeg:1})");
        QCOMPARE(js.evaluate("moves").toInt(),1);
        QCOMPARE(js.evaluate("plannedClears").toInt(),0);
        js.evaluate("stellaInspectMount([48,21.5])");
        QCOMPARE(js.evaluate("moves").toInt(),2);
        QCOMPARE(js.evaluate("lastCenter[0]").toNumber(),48.0);
        QCOMPARE(js.evaluate("lastCenter[1]").toNumber(),21.5);
        QCOMPARE(js.evaluate("zoomChanges").toInt(),1); // Photo inspection only.
        QCOMPARE(js.evaluate("plannedClears").toInt(),0);
        js.evaluate("stellaInspectMount([NaN,20]); stellaInspectMount([48,91]); stellaInspectMount(null)");
        QCOMPARE(js.evaluate("moves").toInt(),2);
    }
    void workerPreservesOriginalAndUsesPrivateFiles() {
        QTemporaryDir dir;
        auto path = dir.filePath("original with spaces.fits");
        QVERIFY(FitsWriter::writeMono16(path,QVector<quint16>(5000,12),100,50));
        QFile original(path); QVERIFY(original.open(QIODevice::ReadOnly)); const auto before = original.readAll(); original.close();
        qputenv("STELLAMORA_TEST_SOLVER", "success");
        auto cancel = std::make_shared<std::atomic_bool>(false);
        const auto result = PlateSolveJob::run(QCoreApplication::applicationFilePath(),path,{}, {},0,cancel);
        QVERIFY2(!result.contains("error"),qPrintable(result.value("error").toString()));
        QCOMPARE(result.value("corners").toList().size(),4);
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(),before);
        QVERIFY(!QFile::exists(dir.filePath("original with spaces.ini")));
    }
    void workerFailureTimeoutAndCancel() {
        auto cancel = std::make_shared<std::atomic_bool>(false);
        const QVector<quint16> pixels(5000,1);
        qputenv("STELLAMORA_TEST_SOLVER","failure");
        auto result = PlateSolveJob::run(QCoreApplication::applicationFilePath(),{},pixels,{100,50},1,cancel);
        QVERIFY(result.value("error").toString().contains("no stars"));
        qputenv("STELLAMORA_TEST_SOLVER","sleep");
        result = PlateSolveJob::run(QCoreApplication::applicationFilePath(),{},pixels,{100,50},1,cancel,100);
        QVERIFY(result.value("error").toString().contains(QStringLiteral("超时")));
        cancel->store(true);
        result = PlateSolveJob::run(QCoreApplication::applicationFilePath(),{},pixels,{100,50},1,cancel);
        QVERIFY(result.value("error").toString().contains(QStringLiteral("取消")));
    }
    void serviceSnapshotsAndRejectsOverlap() {
        PlateSolver solver;
        solver.setExecutable(QCoreApplication::applicationFilePath());
        qputenv("STELLAMORA_TEST_SOLVER","success");
        QVERIFY(!solver.solveCurrent(1));
        solver.setCurrentFrame(QVector<quint16>(5000,4),100,50,{});
        QVERIFY(solver.currentAvailable());
        QVERIFY(solver.solveCurrent(1));
        QVERIFY(solver.busy());
        QVERIFY(!solver.solveCurrent(1));
        solver.setCurrentFrame(QVector<quint16>(1200,4),40,30,{});
        QTRY_VERIFY_WITH_TIMEOUT(!solver.busy(),5000);
        QVERIFY(!solver.result().isEmpty());
        // Snapshot size remains 100x50 even though a new frame arrived.
        QVERIFY(solver.result().value("widthDeg").toDouble()>0.99);
        qputenv("STELLAMORA_TEST_SOLVER","sleep");
        QVERIFY(solver.solveCurrent(1));
        QVERIFY(solver.result().isEmpty());
        solver.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!solver.busy(),5000);
        QVERIFY(solver.result().isEmpty());
    }
    void tangentFootprint() {
        const QByteArray ini = "PLTSOLVD=T\nCRVAL1=0\nCRVAL2=0\nCRPIX1=50.5\nCRPIX2=25.5\nCD1_1=-0.01\nCD1_2=0\nCD2_1=0\nCD2_2=0.01\n";
        QString error;
        auto result = PlateSolveResult::fromAstap(ini, QSize(100,50), &error);
        QVERIFY2(!result.isEmpty(), qPrintable(error));
        QCOMPARE(result.value("raHours").toDouble(), 0.0);
        QCOMPARE(result.value("decDeg").toDouble(), 0.0);
        const auto corners = result.value("corners").toList();
        QCOMPARE(corners.size(), 4);
        // TAN projection, 0.5 degrees east and 0.25 degrees south.
        const auto first = corners[0].toList();
        QVERIFY(qAbs(first[0].toDouble()-0.49998731) < 1e-7);
        QVERIFY(qAbs(first[1].toDouble()+0.24998889) < 1e-7);
        QVERIFY(corners[1].toList()[0].toDouble() > 359.0);
        QCOMPARE(result.value("pixelScale").toDouble(), 36.0);
    }
    void rejectsIncompleteAndFalseSolution() {
        QString error;
        QVERIFY(PlateSolveResult::fromAstap("PLTSOLVD=F\nERROR=no stars", QSize(100,50), &error).isEmpty());
        QVERIFY(error.contains("no stars"));
        QVERIFY(PlateSolveResult::fromAstap("PLTSOLVD=T\nCRVAL1=nan", QSize(100,50), &error).isEmpty());
    }
    void readsRealFitsDimensions() {
        QTemporaryDir dir;
        auto path = dir.filePath("image.fits");
        QVERIFY(FitsWriter::writeMono16(path, QVector<quint16>(5000, 12),100,50));
        QString error;
        QCOMPARE(PlateSolveResult::fitsSize(path, &error), QSize(100,50));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("not fits"); file.close();
        QVERIFY(!PlateSolveResult::fitsSize(path, &error).isValid());
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc,argv);
    QCoreApplication::setOrganizationName("StellaMoraTests");
    QCoreApplication::setApplicationName("PlateSolver");
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    const auto args = app.arguments();
    if (args.contains("-f")) {
        const auto mode = qgetenv("STELLAMORA_TEST_SOLVER");
        if (mode=="sleep") { QThread::msleep(2000); return 1; }
        const QString path = args.value(args.indexOf("-f")+1);
        if (!args.contains("-r") || args.value(args.indexOf("-r")+1)!="180" || !args.contains("-z")) return 5;
        QFile ini(QFileInfo(path).path()+"/image.ini");
        if (!ini.open(QIODevice::WriteOnly)) return 6;
        ini.write(mode=="failure" ? "PLTSOLVD=F\nERROR=no stars\n" :
            "PLTSOLVD=T\nCRVAL1=0\nCRVAL2=0\nCRPIX1=50.5\nCRPIX2=25.5\nCD1_1=-0.01\nCD1_2=0\nCD2_1=0\nCD2_2=0.01\n");
        ini.close();
        QFile input(path); if (input.open(QIODevice::WriteOnly)) input.write("modified by solver");
        return mode=="failure" ? 1 : 0;
    }
    TestPlateSolver test; return QTest::qExec(&test,argc,argv);
}
#include "tst_platesolver.moc"
