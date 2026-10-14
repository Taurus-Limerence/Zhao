#pragma once

// Guiding analytics for phase one (image analysis only, no hardware pulses):
// lock a star, track its centroid frame to frame, detect loss, and publish
// the pixel-error curve. Detection math lives in StarDetection.h, ported
// from PHD2 (BSD 3-clause, Open PHD Guiding).

#include <QObject>
#include <QString>
#include <QVariantList>
#include <algorithm>
#include <deque>
#include "guide/StarDetection.h"
#include "guide/GuideAlgorithms.h"
#include "guide/GuideCalibration.h"

class GuideProcessor final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY stateChanged)
    Q_PROPERTY(double starX READ starX NOTIFY starChanged)
    Q_PROPERTY(double starY READ starY NOTIFY starChanged)
    Q_PROPERTY(double snr READ snr NOTIFY starChanged)
    Q_PROPERTY(double hfd READ hfd NOTIFY starChanged)
    Q_PROPERTY(double mass READ mass NOTIFY starChanged)
    Q_PROPERTY(double errorX READ errorX NOTIFY starChanged)
    Q_PROPERTY(double errorY READ errorY NOTIFY starChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY frameChanged)
    Q_PROPERTY(int missCount READ missCount NOTIFY stateChanged)
    Q_PROPERTY(double frameWidth READ frameWidth NOTIFY frameChanged)
    Q_PROPERTY(double frameHeight READ frameHeight NOTIFY frameChanged)
    Q_PROPERTY(double lastExposure READ lastExposure NOTIFY frameChanged)
    Q_PROPERTY(QVariantList curve READ curve NOTIFY starChanged)
    // Closed loop (phase two): manual calibration + per-axis algorithms +
    // computed pulses. Pulses are EMITTED (guidePulse); the sink decides
    // what they drive - the bundled sink logs them until an ST-4 or mount
    // channel is wired in phase three.
    Q_PROPERTY(bool guiding READ guiding NOTIFY stateChanged)
    // True once a hardware pulse sink (mount PulseGuide) is bound and the
    // mount reports it connected with CanPulseGuide; set from main.cpp.
    Q_PROPERTY(bool outputLinked READ outputLinked NOTIFY outputLinkedChanged)
    bool outputLinked() const { return m_outputLinked; }
public:
    void setOutputLinked(bool linked)
    {
        if (m_outputLinked == linked) return;
        m_outputLinked = linked;
        if (!linked) m_guiding = false; // hardware gone: loop must stop
        emit outputLinkedChanged();
        emit stateChanged();
    }
private:
    Q_PROPERTY(bool calibrated READ calibrated NOTIFY stateChanged)
    Q_PROPERTY(QString algorithm READ algorithm WRITE setAlgorithm NOTIFY stateChanged)
    Q_PROPERTY(QString pulseLog READ pulseLog NOTIFY starChanged)
    Q_PROPERTY(double raErrorArcsec READ raErrorArcsec NOTIFY starChanged)
    Q_PROPERTY(double decErrorArcsec READ decErrorArcsec NOTIFY starChanged)
    Q_PROPERTY(double rmsRa READ rmsRa NOTIFY starChanged)
    Q_PROPERTY(double rmsDec READ rmsDec NOTIFY starChanged)
    Q_PROPERTY(int raHysteresisPercent READ raHysteresisPercent WRITE setRaHysteresisPercent NOTIFY parametersChanged)
    Q_PROPERTY(int decHysteresisPercent READ decHysteresisPercent WRITE setDecHysteresisPercent NOTIFY parametersChanged)
    Q_PROPERTY(int raAggressionPercent READ raAggressionPercent WRITE setRaAggressionPercent NOTIFY parametersChanged)
    Q_PROPERTY(int decAggressionPercent READ decAggressionPercent WRITE setDecAggressionPercent NOTIFY parametersChanged)
    Q_PROPERTY(double raMinMovePx READ raMinMovePx WRITE setRaMinMovePx NOTIFY parametersChanged)
    Q_PROPERTY(double decMinMovePx READ decMinMovePx WRITE setDecMinMovePx NOTIFY parametersChanged)

public:
    explicit GuideProcessor(QObject *parent = nullptr) : QObject(parent) {}

    QString state() const
    {
        // Misses within tolerance still count as locked (PHD2 semantics);
        // exhausting them fires starLost and drops back to looping.
        if (m_haveLock) return QStringLiteral("locked");
        return m_sawFrame ? QStringLiteral("looping") : QStringLiteral("idle");
    }
    bool locked() const { return m_haveLock && m_missCount == 0; }
    double starX() const { return m_star.x; }
    double starY() const { return m_star.y; }
    double snr() const { return m_star.snr; }
    double hfd() const { return m_star.hfd; }
    double mass() const { return m_star.mass; }
    double errorX() const { return m_star.x - m_lockX; }
    double errorY() const { return m_star.y - m_lockY; }
    int frameCount() const { return m_frameCount; }
    int missCount() const { return m_missCount; }
    double frameWidth() const { return double(m_frameWidth); }
    double frameHeight() const { return double(m_frameHeight); }
    double lastExposure() const { return m_lastExposure; }
    QVariantList curve() const { return m_curve; }
    bool guiding() const { return m_guiding; }
    bool calibrated() const { return m_calibration.valid; }
    QString algorithm() const { return m_algorithm; }
    QString pulseLog() const { return m_pulseLog; }
    double raErrorArcsec() const { return m_raErrorPx * m_imageScale; }
    double decErrorArcsec() const { return m_decErrorPx * m_imageScale; }
    double rmsRa() const { return m_rmsRa; }
    double rmsDec() const { return m_rmsDec; }
    int raHysteresisPercent() const { return int(std::lround(m_raAlgo.hysteresis() * 100)); }
    int decHysteresisPercent() const { return int(std::lround(m_decAlgo.hysteresis() * 100)); }
    int raAggressionPercent() const { return int(std::lround(m_raAlgo.aggression() * 100)); }
    int decAggressionPercent() const { return int(std::lround(m_decAlgo.aggression() * 100)); }
    double raMinMovePx() const { return m_raAlgo.minMove(); }
    double decMinMovePx() const { return m_decAlgo.minMove(); }
    void setRaHysteresisPercent(int value) { m_raAlgo.setHysteresisPercent(value); m_raAlgo.reset(); emit parametersChanged(); }
    void setDecHysteresisPercent(int value) { m_decAlgo.setHysteresisPercent(value); m_decAlgo.reset(); emit parametersChanged(); }
    void setRaAggressionPercent(int value) { m_raAlgo.setAggressionPercent(value); m_raAlgo.reset(); emit parametersChanged(); }
    void setDecAggressionPercent(int value) { m_decAlgo.setAggressionPercent(value); m_decAlgo.reset(); emit parametersChanged(); }
    void setRaMinMovePx(double value) {
        if (!std::isfinite(value) || value < 0.0) return;
        m_raAlgo.setMinMove(value); m_raAlgo.reset(); emit parametersChanged();
    }
    void setDecMinMovePx(double value) {
        if (!std::isfinite(value) || value < 0.0) return;
        m_decAlgo.setMinMove(value); m_decAlgo.reset(); emit parametersChanged();
    }

    // Manual calibration from rig parameters; camera angle in degrees.
    // Validate-first: a rejected attempt must NOT overwrite a working
    // calibration out from under a running loop.
    Q_INVOKABLE bool manualCalibrate(double imageScaleArcsecPx, double guideRateSidereal,
                                     double declinationDeg, double cameraAngleDeg = 0.0)
    {
        const GuideCal::Calibration candidate = GuideCal::manual(
            imageScaleArcsecPx, guideRateSidereal, declinationDeg, cameraAngleDeg);
        if (!candidate.valid) return false;
        m_imageScale = imageScaleArcsecPx;
        m_calibration = candidate;
        // SmartDefaultMinMove returns PIXELS at the given image scale —
        // dividing again once halved the deadband and over-corrected.
        const double deadbandPx = GuideAlgo::smartDefaultMinMove(imageScaleArcsecPx);
        m_raAlgo.setMinMove(deadbandPx);
        m_decAlgo.setMinMove(deadbandPx);
        emit parametersChanged();
        // Rates changed: loop statistics and algorithm history no longer
        // describe the same rig — restart them (guiding itself continues
        // only by explicit user intent, see adoptLock policy).
        m_raAlgo.reset();
        m_decAlgo.reset();
        resetLoopStatistics();
        emit stateChanged();
        emit starChanged();
        return true;
    }

    Q_INVOKABLE void clearCalibration()
    {
        m_calibration = {};
        m_guiding = false;
        emit stateChanged();
    }

    Q_INVOKABLE bool startGuiding()
    {
        // A transient star loss (missCount > 0) is not a lock: refuse to
        // start until the star is actually measured again.
        if (!m_calibration.valid || !m_haveLock || m_missCount > 0 || !m_outputLinked) return false;
        m_raAlgo.reset();
        m_decAlgo.reset();
        resetLoopStatistics();
        m_guiding = true;
        emit stateChanged();
        emit starChanged();
        return true;
    }

    Q_INVOKABLE void stopGuiding()
    {
        if (!m_guiding) return;
        m_guiding = false;
        emit stateChanged();
    }

    void setAlgorithm(const QString& name)
    {
        if (name == QStringLiteral("hysteresis") || name == QStringLiteral("identity")) {
            m_algorithm = name;
            m_raAlgo.reset();
            m_decAlgo.reset();
            emit stateChanged();
        }
    }

    // Compatibility for existing callers that intentionally configure both axes.
    Q_INVOKABLE void setHysteresisPercent(int percent) { setRaHysteresisPercent(percent); setDecHysteresisPercent(percent); }
    Q_INVOKABLE void setAggressionPercent(int percent) { setRaAggressionPercent(percent); setDecAggressionPercent(percent); }
    Q_INVOKABLE void setMinMovePx(double px) { setRaMinMovePx(px); setDecMinMovePx(px); }

    // Raw guide frame from DeviceManager (auto-queued onto this thread).
    Q_INVOKABLE void handleFrame(QVector<quint16> data, int width, int height, double exposureSeconds)
    {
        m_lastFrame = std::move(data);
        m_frameWidth = width;
        m_frameHeight = height;
        m_lastExposure = exposureSeconds;
        m_sawFrame = true;
        ++m_frameCount;
        emit frameChanged();
        if (!m_haveLock) {
            emit stateChanged();
            return;
        }

        GuideStar::Params params = m_params;
        params.minSnr = 6.0;
        const GuideStar::Detection star = GuideStar::find(
            m_lastFrame, m_frameWidth, m_frameHeight, m_star.x, m_star.y, params);
        // Conservative single-star identity gate, not PHD2 multi-star matching.
        // Keep the original lock signature so small changes cannot accumulate
        // into a different identity. Normalize flux for exposure changes.
        const double massRatio = m_lockMass > 0.0 && m_lockExposure > 0.0 && exposureSeconds > 0.0
            ? (star.mass / m_lockMass) * (m_lockExposure / exposureSeconds) : 0.0;
        const double hfdRatio = m_lockHfd > 0.0 ? star.hfd / m_lockHfd : 0.0;
        if (star.result == GuideStar::Detection::Ok && massRatio >= 0.5 && massRatio <= 2.0
            && hfdRatio >= 0.65 && hfdRatio <= 1.5) {
            if (m_missCount > 0) { m_raAlgo.reset(); m_decAlgo.reset(); }
            m_star = star;
            m_missCount = 0;
            const double dx = star.x - m_lockX;
            const double dy = star.y - m_lockY;
            appendCurve(dx, dy, m_frameCount);
            if (m_guiding && m_calibration.valid)
                runClosedLoop(dx, dy);
        } else {
            ++m_missCount;
            // Phase-one loss policy: a handful of consecutive misses drops
            // the lock and asks for a fresh selection.
            if (m_missCount >= m_maxMisses) {
                m_haveLock = false;
                m_missCount = 0;
                m_guiding = false;
                emit starLost();
            }
        }
        emit stateChanged();
        emit starChanged();
    }

    // Locks onto the star near the given frame pixel (user click).
    Q_INVOKABLE bool selectStarAt(double x, double y)
    {
        if (!m_sawFrame) return false;
        GuideStar::Params params = m_params;
        params.minSnr = 6.0; // selection gate
        const GuideStar::Detection star = GuideStar::find(
            m_lastFrame, m_frameWidth, m_frameHeight, x, y, params);
        if (star.result != GuideStar::Detection::Ok) return false;
        adoptLock(star);
        return true;
    }

    Q_INVOKABLE bool autoSelect()
    {
        if (!m_sawFrame) return false;
        GuideStar::Params params = m_params;
        // Selection gate matches the tracking gate: a star that just clears
        // a lower bar would be dropped on the very next frame.
        params.minSnr = 6.0;
        const GuideStar::Detection star = GuideStar::autoFind(
            m_lastFrame, m_frameWidth, m_frameHeight, params);
        if (star.result != GuideStar::Detection::Ok) return false;
        adoptLock(star);
        return true;
    }

    Q_INVOKABLE void clearStar()
    {
        // The processor guarantees its own invariants: dropping the lock
        // ends the loop and forgets the algorithm history even if the UI
        // never issued a stop.
        m_haveLock = false;
        m_guiding = false;
        m_missCount = 0;
        m_curve.clear();
        resetLoopStatistics();
        m_raAlgo.reset();
        m_decAlgo.reset();
        emit stateChanged();
        emit starChanged();
    }

signals:
    void parametersChanged();
    void stateChanged();
    void starChanged();
    void frameChanged();
    void starLost();
    // Computed guide correction; direction flags follow PHD2 conventions.
    void guidePulse(bool raWest, int raMs, bool decSouth, int decMs);
    void outputLinkedChanged();

private:
    // The frame index lets the chart break the line across star loss
    // instead of bridging gaps with fake data.
    // Adopts a freshly measured star as the lock: history, curve, RMS and
    // stale error/pulse readouts restart so nothing leaks into the new lock
    // period. POLICY: adopting a new lock ALWAYS ends an active loop — the
    // lock reference changed, so guiding must be restarted explicitly. This
    // holds regardless of the order the UI issues its calls.
    void adoptLock(const GuideStar::Detection& star)
    {
        m_star = star;
        m_lockX = star.x;
        m_lockY = star.y;
        m_lockMass = star.mass;
        m_lockHfd = star.hfd;
        m_lockExposure = m_lastExposure;
        m_haveLock = true;
        m_missCount = 0;
        m_guiding = false;
        m_curve.clear();
        resetLoopStatistics();
        m_raAlgo.reset();
        m_decAlgo.reset();
        emit stateChanged();
        emit starChanged();
    }

    void resetLoopStatistics()
    {
        m_rmsRa = 0.0;
        m_rmsDec = 0.0;
        m_rmsCount = 0;
        m_rmsRaWindow.clear();
        m_rmsDecWindow.clear();
        // Stale axis errors and the last pulse text describe the old lock
        // period; clear them with the rest.
        m_raErrorPx = 0.0;
        m_decErrorPx = 0.0;
        m_pulseLog.clear();
    }

    void appendCurve(double dx, double dy, int frame)
    {
        m_curve.append(QVariantMap{{"dx", dx}, {"dy", dy}, {"f", frame}});
        while (m_curve.size() > 240) m_curve.removeFirst();
    }

    void runClosedLoop(double dx, double dy)
    {
        const auto mount = GuideCal::cameraToMount(m_calibration, dx, dy);
        m_raErrorPx = mount.first;
        m_decErrorPx = mount.second;
        const double raOut = m_algorithm == QStringLiteral("identity")
            ? mount.first : m_raAlgo.result(mount.first);
        const double decOut = m_algorithm == QStringLiteral("identity")
            ? mount.second : m_decAlgo.result(mount.second);
        const GuideCal::Pulse pulse = GuideCal::pulseFor(m_calibration, raOut, decOut);
        if (pulse.raMs > 0 || pulse.decMs > 0) {
            m_pulseLog = QStringLiteral("RA %1 %2ms / Dec %3 %4ms")
                             .arg(pulse.raWest ? QStringLiteral("W") : QStringLiteral("E"))
                             .arg(pulse.raMs)
                             .arg(pulse.decSouth ? QStringLiteral("S") : QStringLiteral("N"))
                             .arg(pulse.decMs);
            emit guidePulse(pulse.raWest, pulse.raMs, pulse.decSouth, pulse.decMs);
        } else {
            m_pulseLog = QStringLiteral("deadband");
        }
        // Sliding-window RMS over the last 30 guiding frames (arcsec) —
        // a plain window statistic, not an exponential average.
        m_rmsRaWindow.push_back(raErrorArcsec() * raErrorArcsec());
        m_rmsDecWindow.push_back(decErrorArcsec() * decErrorArcsec());
        while (m_rmsRaWindow.size() > 30) m_rmsRaWindow.pop_front();
        while (m_rmsDecWindow.size() > 30) m_rmsDecWindow.pop_front();
        m_rmsCount = int(m_rmsRaWindow.size());
        auto windowRms = [](const std::deque<double>& window) {
            double sum = 0.0;
            for (const double value : window) sum += value;
            return std::sqrt(sum / double(window.size()));
        };
        m_rmsRa = windowRms(m_rmsRaWindow);
        m_rmsDec = windowRms(m_rmsDecWindow);
    }

    QVector<quint16> m_lastFrame;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    double m_lastExposure = 0.0;
    GuideStar::Detection m_star;
    GuideStar::Params m_params;
    double m_lockX = 0.0;
    double m_lockY = 0.0;
    double m_lockMass = 0.0;
    double m_lockHfd = 0.0;
    double m_lockExposure = 0.0;
    bool m_haveLock = false;
    bool m_sawFrame = false;
    int m_frameCount = 0;
    int m_missCount = 0;
    int m_maxMisses = 5;
    QVariantList m_curve;
    GuideCal::Calibration m_calibration;
    GuideAlgo::Hysteresis m_raAlgo;
    GuideAlgo::Hysteresis m_decAlgo;
    QString m_algorithm = QStringLiteral("hysteresis");
    double m_imageScale = 1.0;
    double m_raErrorPx = 0.0;
    double m_decErrorPx = 0.0;
    double m_rmsRa = 0.0;
    double m_rmsDec = 0.0;
    int m_rmsCount = 0;
    std::deque<double> m_rmsRaWindow;
    std::deque<double> m_rmsDecWindow;
    bool m_guiding = false;
    bool m_outputLinked = false;
    QString m_pulseLog;
};
