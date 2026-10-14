#pragma once

#include "FilterWheelBackend.h"
#include <QLibrary>
#include <QTimer>
#include <QElapsedTimer>
#include <toupcam.h>

class ToupcamFilterWheel final : public FilterWheelBackend
{
public:
    ToupcamFilterWheel();
    ~ToupcamFilterWheel() override;
    void scan() override;
    void open(const QString &id) override;
    void close() override;
    void moveTo(int position, bool bidirectional) override;
    void calibrate() override;
    void setSlotCount(int slotCount) override;
private:
    friend class FilterWheelTest;
    bool loadSdk(QString &error);
    QVariantList enumerate();
    bool refreshCapability(QVariantMap &information, QString &error);
    void checkPresence();
    void pollMovement();
    void reportState();
    void handleIoFailure(const QString &action);
    void release();

    QLibrary m_library;
    QTimer *m_presenceTimer;
    QTimer *m_pollTimer;
    HToupcam m_handle = nullptr;
    QString m_id;
    int m_missingScans = 0;
    int m_slots = 0;
    int m_position = -1;
    bool m_moving = false;
    bool m_calibrating = false;
    int m_targetPosition = -1;
    int m_lastReadPosition = -999;
    QElapsedTimer m_motionElapsed;
    decltype(&Toupcam_EnumV2) m_enum = nullptr;
    decltype(&Toupcam_Open) m_open = nullptr;
    decltype(&Toupcam_Close) m_close = nullptr;
    decltype(&Toupcam_put_Option) m_putOption = nullptr;
    decltype(&Toupcam_get_Option) m_getOption = nullptr;
    decltype(&Toupcam_Version) m_version = nullptr;
    decltype(&Toupcam_get_SerialNumber) m_serial = nullptr;
};
