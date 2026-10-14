#pragma once

#include "FocuserBackend.h"
#include <QLibrary>
#include <QTimer>
#include <toupcam.h>

class ToupcamFocuser final : public FocuserBackend
{
public:
    ToupcamFocuser();
    ~ToupcamFocuser() override;
    void scan() override;
    void open(const QString &id) override;
    void close() override;
    void moveTo(int position) override;
    void halt() override;
    void setReversed(bool reversed) override;
    void setMaxStep(int maxStep) override;
    void setZero() override;
    void setCurrentPosition(int position) override;
private:
    bool loadSdk(QString &error);
    QVariantList enumerate();
    // NINA-verified Toupcam_AAF convention: the by-value parameter carries
    // the value INTO the device (set actions); the pointer receives the
    // result OUT of it (get actions). A valid pointer is always supplied.
    bool aaf(int action, int value, int *result);
    void refreshState();
    void checkPresence();
    void reportPosition();
    void release();

    QLibrary m_library;
    QTimer *m_presenceTimer;
    QTimer *m_pollTimer;
    HToupcam m_handle = nullptr;
    QString m_id;
    int m_missingScans = 0;
    int m_position = -1;
    bool m_moving = false;
    int m_maxStep = 0;
    bool m_reversed = false;
    double m_temperature = 0.0;
    int m_stepSize = 0;
    decltype(&Toupcam_EnumV2) m_enum = nullptr;
    decltype(&Toupcam_Open) m_open = nullptr;
    decltype(&Toupcam_Close) m_close = nullptr;
    decltype(&Toupcam_AAF) m_aafFn = nullptr;
    decltype(&Toupcam_get_SerialNumber) m_serial = nullptr;
    decltype(&Toupcam_Version) m_version = nullptr;
};
