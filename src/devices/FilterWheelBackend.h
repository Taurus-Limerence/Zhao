#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// All operations run on the owning worker thread. No UI or device handles cross it.
class FilterWheelBackend : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void scan() = 0;
    virtual void open(const QString &id) = 0;
    virtual void close() = 0;
    // Positions and slot counts use the hardware convention: 0-based, slot count 5/7/8.
    // (Avoid naming parameters "slots": it collides with Qt's `slots` macro.)
    virtual void moveTo(int position, bool bidirectional) = 0;
    virtual void calibrate() = 0;
    virtual void setSlotCount(int slotCount) = 0;
signals:
    void scanFinished(const QVariantList &wheels, const QString &error);
    void connectionFinished(bool connected, const QVariantMap &information, const QString &error);
    void connectionLost(const QString &error);
    // state keys: slots, position (0-based, -1 while unknown), moving, calibrating
    void stateChanged(const QVariantMap &state);
};
