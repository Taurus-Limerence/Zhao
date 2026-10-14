// Real-hardware smoke check for the ASCOM mount backend: enumerate ASCOM
// Telescope drivers, connect to the first (or TMount), read coordinates,
// send a guide pulse, disconnect. Run with the driver installed.
#define NOMINMAX
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
#include <cstdarg>
#include "devices/AscomMount.h"

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
    QCoreApplication::setApplicationName(QStringLiteral("StellaMora-AscomSmoke"));

    AscomMount mount;

    QVariantList found;
    QObject::connect(&mount, &AscomMount::scanFinished,
                     [&](const QVariantList &devices, const QString &) { found = devices; });
    mount.scan();
    logf("ASCOM 赤道仪驱动: %d 台\n", int(found.size()));
    for (const auto &device : found)
        logf("  %ls  [%ls]\n",
             reinterpret_cast<const wchar_t *>(device.toMap().value("name").toString().utf16()),
             reinterpret_cast<const wchar_t *>(device.toMap().value("id").toString().utf16()));
    if (found.isEmpty()) return 1;

    // Prefer TMount if present; otherwise take the first.
    QString target = found.first().toMap().value("id").toString();
    for (const auto &device : found)
        if (device.toMap().value("id").toString().contains("TMount", Qt::CaseInsensitive))
            target = device.toMap().value("id").toString();

    QVariantMap info;
    QString openError;
    QObject::connect(&mount, &AscomMount::connectionFinished,
                     [&](bool connected, const QVariantMap &i, const QString &error) {
                         openError = error;
                         if (connected) info = i;
                     });
    logf("连接 %ls …\n", reinterpret_cast<const wchar_t *>(target.utf16()));
    mount.open(target);
    if (!mount.isConnected()) {
        logf("连接失败: %ls\n", reinterpret_cast<const wchar_t *>(openError.utf16()));
        return 2;
    }
    logf("已连接: %ls  驱动 %ls  PulseGuide=%d Park=%d Slew=%d\n",
         reinterpret_cast<const wchar_t *>(mount.name().utf16()),
         reinterpret_cast<const wchar_t *>(info.value("driverVersion").toString().utf16()),
         mount.canPulseGuide() ? 1 : 0, mount.canPark() ? 1 : 0, mount.canSlew() ? 1 : 0);

    QTimer::singleShot(500, [&] {
        QVariantMap st = mount.stateMap();
        logf("能力: canFindHome=%d canSetPark=%d canPark=%d canUnpark=%d\n",
             st.value("canFindHome").toBool() ? 1 : 0,
             st.value("canSetPark").toBool() ? 1 : 0,
             st.value("canPark").toBool() ? 1 : 0,
             st.value("canUnpark").toBool() ? 1 : 0);
    });
    QTimer::singleShot(1500, [&] {
        logf("RA=%s  Dec=%s  Tracking=%d\n",
             mount.rightAscension().isValid() ? std::to_string(mount.rightAscension().toDouble()).c_str() : "—",
             mount.declination().isValid() ? std::to_string(mount.declination().toDouble()).c_str() : "—",
             mount.isTracking() ? 1 : 0);

        if (mount.canPulseGuide() && !mount.isAtPark()) {
            logf("发送 500ms 北向脉冲…\n");
            mount.pulseGuide(0, 500);
            QTimer::singleShot(2000, [&] {
                logf("脉冲后 RA=%s Dec=%s\n",
                     mount.rightAscension().isValid() ? std::to_string(mount.rightAscension().toDouble()).c_str() : "—",
                     mount.declination().isValid() ? std::to_string(mount.declination().toDouble()).c_str() : "—");
                mount.close();
                QCoreApplication::exit(0);
            });
        } else {
            logf("跳过脉冲测试（不支持导星或已停放）\n");
            mount.close();
            QCoreApplication::exit(0);
        }
    });
    return app.exec();
}
