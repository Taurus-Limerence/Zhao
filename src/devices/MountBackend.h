#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// All operations run on the owning worker thread. No UI or device handles cross it.
class MountBackend : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void scan() = 0;
    virtual void open(const QString &id) = 0;
    virtual void close() = 0;
    // Opens the driver's own setup dialog (ASCOM SetupDialog).
    virtual void setup(const QString &id) = 0;
    // Manual motion rates are signed multiples of sidereal; empty string means success.
    virtual QString startManualMove(int, double) { return QStringLiteral("不支持手动移动"); }
    virtual QString stopManualMove() { return QStringLiteral("不支持停止手动移动"); }
    virtual void keepManualMoveAlive() {}
    virtual QString slewToJ2000(double, double) { return QStringLiteral("驱动不支持目标指向"); }
    virtual QString stopSlew() { return QStringLiteral("驱动不支持停止指向"); }
    virtual QString syncObserver(bool, double, double) { return QStringLiteral("驱动不支持站点或时间同步"); }
signals:
    void scanFinished(const QVariantList &mounts, const QString &error);
    void connectionFinished(bool connected, const QVariantMap &information, const QString &error);
    void connectionLost(const QString &error);
    void setupFinished(const QString &error);
    // Periodic state snapshot (ra, dec, tracking, slewing, atPark, etc.).
    void stateChanged(const QVariantMap &state);
};
