// toupcam.h pulls windows.h whose min/max macros clash with <algorithm>.
#define NOMINMAX
#include <QCoreApplication>
#include <QTimer>
#include <QDateTime>
#include <cstdio>
#include <cstdarg>
static void logf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
}
#include "devices/ToupcamCamera.h"
#include "guide/StarDetection.h"

// Real-hardware smoke check for the guiding pipeline: enumerate, open the
// first camera in guide mode, collect raw Grey16 frames through the event
// loop, and run auto star selection on the last frame. Console tool — not
// part of ctest; run beside toupcam.dll.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("StellaMora-GuideSmoke"));

    ToupcamCamera camera;
    camera.setGuideMode(true);

    QVariantList found;
    QObject::connect(&camera, &CameraBackend::scanFinished,
                     [&](const QVariantList &cameras, const QString &) { found = cameras; });
    camera.scan();
    logf("枚举到 %d 台设备\n", int(found.size()));
    for (const auto &device : found) {
        const auto map = device.toMap();
        logf("  %ls  [%ls]\n",
                    reinterpret_cast<const wchar_t *>(map.value("name").toString().utf16()),
                    reinterpret_cast<const wchar_t *>(map.value("id").toString().utf16()));
    }
    if (found.isEmpty()) {
        logf("未发现设备\n");
        return 1;
    }

    QString openError;
    QVariantMap info;
    QObject::connect(&camera, &CameraBackend::connectionFinished,
                     [&](bool connected, const QVariantMap &information, const QString &error) {
                         openError = error;
                         if (connected) info = information;
                     });
    const QString id = found.first().toMap().value("id").toString();
    camera.open(id);
    if (!openError.isEmpty()) {
        logf("打开失败: %ls\n", reinterpret_cast<const wchar_t *>(openError.utf16()));
        return 2;
    }
    logf("已连接: %ls  SN %ls  SDK %ls  %dx%d\n",
                reinterpret_cast<const wchar_t *>(info.value("model").toString().utf16()),
                reinterpret_cast<const wchar_t *>(info.value("serial").toString().utf16()),
                reinterpret_cast<const wchar_t *>(info.value("sdkVersion").toString().utf16()),
                info.value("width").toInt(), info.value("height").toInt());

    int frames = 0;
    int width = 0, height = 0;
    double exposure = 0.0;
    QVector<quint16> lastFrame;
    QObject::connect(&camera, &CameraBackend::rawFrameReady,
                     [&](QVector<quint16> data, int w, int h, double seconds) {
                         ++frames;
                         width = w;
                         height = h;
                         exposure = seconds;
                         lastFrame = std::move(data);
                     });
    camera.setExposure(100000);   // 100 ms — desk-friendly
    camera.setGain(200);
    camera.startPreview(true);

    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &app, &QCoreApplication::quit);
    deadline.start(6000);
    app.exec();

    camera.stopPreview();
    logf("收到原始帧: %d  尺寸 %dx%d  曝光 %.3fs\n", frames, width, height, exposure);
    if (lastFrame.isEmpty()) return 3;

    double sum = 0.0;
    quint32 median = 0;
    QVector<quint16> sorted(lastFrame);
    std::sort(sorted.begin(), sorted.end());
    median = sorted.at(sorted.size() / 2);
    for (const auto value : lastFrame) sum += value;
    logf("帧统计: 平均 %.1f ADU  中位 %u ADU\n", sum / lastFrame.size(), median);

    const GuideStar::Detection star = GuideStar::autoFind(lastFrame, width, height);
    logf("自动选星: result=%d  x=%.2f y=%.2f  snr=%.1f hfd=%.2f mass=%.0f peak=%.0f\n",
                int(star.result), star.x, star.y, star.snr, star.hfd, star.mass, star.peak);
    logf(star.result == GuideStar::Detection::Ok
                    ? "结论: 管道正常，已锁定候选星点\n"
                    : "结论: 管道正常（无合格星点属预期——室内/盖镜头时无星）\n");

    camera.close();
    return 0;
}
