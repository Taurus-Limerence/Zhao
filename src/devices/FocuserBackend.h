#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// All operations run on the owning worker thread. No UI or device handles cross it.
class FocuserBackend : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void scan() = 0;
    virtual void open(const QString &id) = 0;
    virtual void close() = 0;
    // Absolute target position in device steps (NINA/IFocuser semantics).
    virtual void moveTo(int position) = 0;
    virtual void halt() = 0;
    virtual void setReversed(bool reversed) = 0;
    // AAF action 0x1b: cap the travel range (device-persistent).
    virtual void setMaxStep(int maxStep) = 0;
    // AAF action 0x03: define the current physical spot as position zero
    // (NINA's ResetPosition — no motion, the counter is re-anchored).
    virtual void setZero() = 0;
    // Same action carrying a value: re-anchor the counter so the current
    // physical spot READS as the given position. The SDK header documents
    // only the zero form; whether the device honors a non-zero argument is
    // hardware behavior — implementations must report refusal honestly.
    virtual void setCurrentPosition(int position) = 0;
signals:
    void scanFinished(const QVariantList &focusers, const QString &error);
    void connectionFinished(bool connected, const QVariantMap &information, const QString &error);
    void connectionLost(const QString &error);
    // state keys: position, moving, maxStep, reversed, temperature, stepSize
    void stateChanged(const QVariantMap &state);
};
