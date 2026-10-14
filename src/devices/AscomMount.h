#pragma once

#include "MountBackend.h"
#include <QTimer>
#include <QElapsedTimer>

// ASCOM mount backend: talks to an IDispatch telescope driver by ProgID
// (e.g. "ASCOM.TMount.Mount"). Exposes RA/Dec readout, tracking control,
// slew/park/abort, and PulseGuide — the output channel the guiding loop
// needs to close for real. Windows-only (ASCOM is a COM platform).
class AscomMount final : public MountBackend
{
    Q_OBJECT
public:
    explicit AscomMount(QObject *parent = nullptr);

    void scan() override;
    void open(const QString &progId) override;
    void close() override;
    void setup(const QString &id) override;
    QString startManualMove(int axis, double rate) override;
    QString stopManualMove() override;
    void keepManualMoveAlive() override;
    QString slewToJ2000(double ra, double dec) override;
    QString stopSlew() override;
    QString syncObserver(bool timeOnly, double longitude, double latitude) override;

    // Control (all no-ops when not connected; errors go to lastError).
    bool setTracking(bool on);
    void refreshNow();
    // ASCOM DriveRate: 0=sidereal 1=solar 2=lunar 3=king.
    bool setTrackingRate(int rate);
    // ASCOM FindHome / SetPark (当前位置设为停放零位).
    bool findHome();
    bool setCurrentPark();
    // App uses ×sidereal; ASCOM properties use degrees/second.
    static constexpr double guideRateToDegrees(double rate) { return rate * 15.041084 / 3600.0; }
    static constexpr double guideRateFromDegrees(double rate) { return rate * 3600.0 / 15.041084; }
    bool setGuideRates(double raRate, double decRate);
    QVariant trackingRate() const;
    QVariant guideRateRa() const;
    QVariant guideRateDec() const;
    bool park();
    bool unpark();
    bool abortSlew();
    // ASCOM GuideDirection: North=0, South=1, East=2, West=3.
    bool pulseGuide(int direction, int durationMs);

    // State readout (invalid QVariant when unsupported).
    QVariant rightAscension() const;
    QVariant declination() const;
    QVariant siderealTime() const;
    QVariant siteLatitude() const;
    QVariant siteLongitude() const;
    bool isConnected() const { return m_connected; }
    bool isTracking() const;
    bool isSlewing() const;
    bool isAtPark() const;
    bool canPulseGuide() const;
    bool canPark() const;
    bool canSlew() const;
    QString name() const { return m_name; }
    QString lastError() const { return m_error; }

    QVariantMap stateMap() const;

private:
    void pollState();
    void checkConnection();

    QTimer *m_pollTimer = nullptr;
    QTimer *m_manualTimer = nullptr;
    bool m_manualMoving = false;
    QTimer *m_watchdogTimer = nullptr;
    void *m_dispatch = nullptr;  // AscomDispatch* (opaque to header users)
    QString m_progId;
    QString m_name;
    QString m_error;
    bool m_connected = false;
    bool m_comInitialized = false;
    // True while WE want to be connected; only close() clears it. The
    // watchdog uses this to distinguish "ASCOM UI disconnected" (retry)
    // from "user asked to disconnect" (clean shutdown).
    bool m_expectConnected = false;
    // Cached capabilities read once at open; included in every stateMap().
    bool m_canFindHome = false;
    bool m_canSetPark = false;
    int m_missedPolls = 0;
    bool m_gotoActive = false;
    double m_gotoRa = 0, m_gotoDec = 0;
    QElapsedTimer m_gotoElapsed;
};
