// Real-hardware smoke check for the ToupTek AAF focuser: enumerate, open,
// read state, nudge outward, wait, read back, halt, close. Console tool —
// run beside toupcam.dll.
#define NOMINMAX
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
#include <cstdarg>
#include "devices/ToupcamFocuser.h"

static void logf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("StellaMora-FocuserSmoke"));

    ToupcamFocuser focuser;

    QVariantList found;
    QObject::connect(&focuser, &FocuserBackend::scanFinished,
                     [&](const QVariantList &devices, const QString &) { found = devices; });
    focuser.scan();
    logf("枚举到 %d 台 AAF 设备\n", int(found.size()));
    for (const auto &device : found)
        logf("  %ls  [%ls]\n",
             reinterpret_cast<const wchar_t *>(device.toMap().value("name").toString().utf16()),
             reinterpret_cast<const wchar_t *>(device.toMap().value("id").toString().utf16()));
    if (found.isEmpty()) return 1;

    QVariantMap info;
    QString openError;
    QObject::connect(&focuser, &FocuserBackend::connectionFinished,
                     [&](bool connected, const QVariantMap &information, const QString &error) {
                         openError = error;
                         if (connected) info = information;
                     });
    int lastPosition = -1;
    bool moving = false;
    QObject::connect(&focuser, &FocuserBackend::stateChanged, [&](const QVariantMap &state) {
        if (state.contains("position")) lastPosition = state.value("position").toInt();
        moving = state.value("moving", moving).toBool();
    });
    const QString id = found.first().toMap().value("id").toString();
    focuser.open(id);
    if (!openError.isEmpty()) {
        logf("打开失败: %ls\n", reinterpret_cast<const wchar_t *>(openError.utf16()));
        return 2;
    }
    logf("已连接: maxStep=%d reversed=%d stepSize=%d 温度=%.1f°C 位置=%d SN=%ls\n",
         info.value("maxStep").toInt(), info.value("reversed").toBool() ? 1 : 0,
         info.value("stepSize").toInt(), info.value("temperature").toDouble(),
         info.value("position").toInt(),
         reinterpret_cast<const wchar_t *>(info.value("serial").toString().utf16()));

    // Move toward mid-travel; the device may sit at either end of its range.
    const int maxStep = info.value("maxStep").toInt();
    const int current = info.value("position").toInt();
    const int target = current >= maxStep / 2 ? (current > 200 ? current - 200 : 0) : (current + 200 < maxStep ? current + 200 : maxStep);
    logf("移动到 %d …\n", target);
    focuser.moveTo(target);

    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &app, [&] {
        logf("到位检查: 位置=%d moving=%d（目标 %d）\n", lastPosition, moving ? 1 : 0, target);
        // Re-anchor probes: zero first, then the value form of SETZERO.
        focuser.setZero();
        QTimer::singleShot(400, &app, [&] {
            logf("清零后位置=%d\n", lastPosition);
            focuser.setCurrentPosition(500);
            QTimer::singleShot(400, &app, [&] {
                logf("设为500后位置=%d（%ls）\n", lastPosition,
                     lastPosition == 500 ? L"设备支持任意设值"
                     : lastPosition == 0 ? L"设备仅支持清零（值形式被忽略）"
                                         : L"设备返回了其他读数");
                focuser.close();
                QCoreApplication::exit(0);
            });
        });
    });
    deadline.start(4000);
    return app.exec();
}
