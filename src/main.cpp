#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTimer>
#include <cstring>
#include <QStandardPaths>
#include <QtQml>

#include "imaging/PreviewHistogram.h"
#include "imaging/PreviewImageProvider.h"
#include "app/AppState.h"
#include "devices/DeviceManager.h"
#include "astro/AstroPlanner.h"
#include "astro/PlateSolver.h"
#include "astro/CenteringController.h"
#include "guide/GuideProcessor.h"
#include "sequence/SequenceController.h"
#include "focus/AutoFocusController.h"
#ifdef STELLAMORA_ALADIN
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#endif

int main(int argc, char *argv[])
{
#ifdef STELLAMORA_ALADIN
    // Screenshot mode never opens the sky page; skip the Chromium bootstrap there.
    const bool aladinEnabled = [&argc, &argv] {
        for (int i = 1; i < argc; ++i)
            if (std::strcmp(argv[i], "--screenshot") == 0) return false;
        return true;
    }();
    if (aladinEnabled) QtWebEngineQuick::initialize();
#else
    const bool aladinEnabled = false;
#endif
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("StellaMora"));
    QGuiApplication::setWindowIcon(
        QIcon(QStringLiteral(":/qt/qml/StellaMora/qml/assets/icons/stellamora-app.svg")));
    QGuiApplication::setOrganizationName(QStringLiteral("ToupTek Astro"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                              QStringLiteral("Save one rendered frame and exit."),
                                              QStringLiteral("path"));
    const QCommandLineOption sizeOption(QStringLiteral("size"),
                                        QStringLiteral("Screenshot size, for example 1440x900."),
                                        QStringLiteral("widthxheight"),
                                        QStringLiteral("1440x900"));
    parser.addOption(screenshotOption);
    parser.addOption(sizeOption);
    const QCommandLineOption sequenceOption(QStringLiteral("sequence"), QStringLiteral("Open the sequence workspace without starting capture."));
    parser.addOption(sequenceOption);
    parser.process(app);

    AppState appState;
    DeviceManager deviceManager; 
    PlateSolver plateSolver;
    QObject::connect(&deviceManager, &DeviceManager::mainPhotoReady, &plateSolver, &PlateSolver::setCurrentFrame);
    QObject::connect(&plateSolver, &PlateSolver::captureRequested, &deviceManager,
        [&deviceManager,&plateSolver](double seconds) {
            if (!deviceManager.captureForSolve(seconds)) plateSolver.failCapture(deviceManager.lastError());
        });
    QObject::connect(&deviceManager, &DeviceManager::solvePhotoReady, &plateSolver, &PlateSolver::acceptCapture);
    QObject::connect(&deviceManager, &DeviceManager::solveCaptureFailed, &plateSolver, &PlateSolver::failCapture);
    QObject::connect(&plateSolver, &PlateSolver::cancelCaptureRequested, &deviceManager, &DeviceManager::cancelSolveCapture);
    QObject::connect(&plateSolver, &PlateSolver::notice, &appState,
        [&appState](const QString &message) { appState.showNotification(AppState::Information, QStringLiteral("照片解析"), message); });
    qmlRegisterSingletonInstance("StellaMora.State", 1, 0, "PlateSolver", &plateSolver);
    CenteringController centeringController(&deviceManager,&plateSolver);
    qmlRegisterSingletonInstance("StellaMora.State", 1, 0, "CenteringController", &centeringController);
    QObject::connect(&centeringController,&CenteringController::notice,&appState,
        [&appState](const QString &message) { appState.showNotification(AppState::Information,QStringLiteral("指向并居中"),message); });
    AutoFocusController autoFocusController(&deviceManager);
    SequenceController sequenceController(&deviceManager,nullptr,&centeringController,&autoFocusController);
    qmlRegisterSingletonInstance("StellaMora.State", 1, 0, "AutoFocusController", &autoFocusController);
    QObject::connect(&autoFocusController, &AutoFocusController::notice, &appState,
        [&appState](const QString &message) { appState.showNotification(AppState::Information, QStringLiteral("自动对焦"), message); });
    qmlRegisterSingletonInstance("StellaMora.State", 1, 0, "SequenceController", &sequenceController);
    if (parser.isSet(sequenceOption)) appState.setWorkspace(AppState::Sequence);
    QObject::connect(&sequenceController, &SequenceController::notice, &appState,
        [&appState](const QString &message) { appState.showNotification(AppState::Information, QStringLiteral("拍摄序列"), message); });
    qmlRegisterSingletonInstance("StellaMora.State", 1, 0, "AppState", &appState);
    qmlRegisterSingletonInstance("StellaMora.State", 1, 0, "DeviceManager", &deviceManager);
    AstroPlanner astroPlanner;
    qmlRegisterSingletonInstance("StellaMora.State", 1, 0, "AstroPlanner", &astroPlanner);
    GuideProcessor guideProcessor;
    QObject::connect(&deviceManager, &DeviceManager::mountMotionStopRequested,
                     &guideProcessor, &GuideProcessor::stopGuiding);
    QObject::connect(&guideProcessor, &GuideProcessor::stateChanged, &deviceManager,
        [&deviceManager, &guideProcessor] { deviceManager.setMountGuiding(guideProcessor.guiding()); });
    qmlRegisterSingletonInstance("StellaMora.State", 1, 0, "GuideProcessor", &guideProcessor);
    // Pixel frames never cross QML: the guide stream is wired C++-side.
    QObject::connect(&deviceManager, &DeviceManager::guideFrameReady,
                     &guideProcessor, &GuideProcessor::handleFrame);
    // 单帧截存结果 → 右下角通知（成功带路径，失败给排查提示）。
    QObject::connect(&deviceManager, &DeviceManager::stillCaptured, &appState,
                     [&appState, &sequenceController](int role, const QString &path, bool ok) {
                         if (ok && (role != DeviceManager::MainCamera || !sequenceController.active()))
                             appState.showNotification(AppState::Information, QStringLiteral("图像已保存"), path);
                     });
    QObject::connect(&deviceManager, &DeviceManager::lastErrorChanged, &appState,
                     [&deviceManager, &appState] {
                         if (!deviceManager.lastError().isEmpty())
                             appState.showNotification(AppState::Warning, QStringLiteral("设备操作提示"), deviceManager.lastError());
                     });
    // Guiding pulse chain: computed corrections drive the ASCOM mount.
    // GuideProcessor direction flags (raWest/decSouth) map to ASCOM's
    // GuideDirection: North=0, South=1, East=2, West=3.
    QObject::connect(&guideProcessor, &GuideProcessor::guidePulse, &deviceManager,
        [&deviceManager](bool raWest, int raMs, bool decSouth, int decMs) {
            if (raMs > 0) deviceManager.mountPulseGuide(raWest ? 3 : 2, raMs);
            if (decMs > 0) deviceManager.mountPulseGuide(decSouth ? 1 : 0, decMs);
        });
    // Link state follows the mount: connected + CanPulseGuide => outputLinked.
    QObject::connect(&deviceManager, &DeviceManager::devicesChanged, &guideProcessor,
        [&deviceManager, &guideProcessor]() {
            const QVariantMap mount = deviceManager.device(DeviceManager::Mount);
            const QVariantMap metrics = mount.value(QStringLiteral("metrics")).toMap();
            guideProcessor.setOutputLinked(mount.value(QStringLiteral("connected")).toBool()
                                          && metrics.value(QStringLiteral("canPulseGuide")).toBool());
        });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("aladinAvailable"), aladinEnabled);
    engine.rootContext()->setContextProperty(QStringLiteral("surveyCachePath"),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(EXIT_FAILURE); },
                     Qt::QueuedConnection);
    engine.addImageProvider(QStringLiteral("live"), new PreviewImageProvider(&deviceManager));
    engine.setInitialProperties({
        {QStringLiteral("staticHistogramBins"),
         previewHistogram(QImage(QStringLiteral(":/qt/qml/StellaMora/qml/assets/images/preview.jpg")))}
    });
    engine.loadFromModule("StellaMora", "Main");

    if (parser.isSet(screenshotOption) && !engine.rootObjects().isEmpty()) {
        if (parser.isSet(sequenceOption)) sequenceController.addRow();
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const QStringList dimensions = parser.value(sizeOption).split(u'x');
        if (!window || dimensions.size() != 2)
            return EXIT_FAILURE;

        bool widthOk = false;
        bool heightOk = false;
        const int width = dimensions.at(0).toInt(&widthOk);
        const int height = dimensions.at(1).toInt(&heightOk);
        if (!widthOk || !heightOk || width < 1 || height < 1)
            return EXIT_FAILURE;

        window->setWidth(width);
        window->setHeight(height);
        const QString screenshotPath = QFileInfo(parser.value(screenshotOption)).absoluteFilePath();
        QDir().mkpath(QFileInfo(screenshotPath).absolutePath());
        QTimer::singleShot(800, &app, [window, screenshotPath] {
            const QImage frame = window->grabWindow();
            QCoreApplication::exit(!frame.isNull() && frame.save(screenshotPath)
                                       ? EXIT_SUCCESS
                                       : EXIT_FAILURE);
        });
    }

    return app.exec();
}
