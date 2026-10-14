#pragma once

#include <QObject>
#include <QDateTime>
#include <cmath>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>

#include "astro/AstroMath.h"
#include "astro/DeepSkyTargets.h"

// QML-facing observing planner: catalogue search, alt/az for the configured
// site and moment, field-of-view geometry and a saved-target list. The mount
// is not involved — every value here is planning data, never a live pointing.
class AstroPlanner final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double latitude READ latitude WRITE setLatitude NOTIFY siteChanged)
    Q_PROPERTY(double longitude READ longitude WRITE setLongitude NOTIFY siteChanged)
    Q_PROPERTY(double focalLengthMm READ focalLengthMm WRITE setFocalLengthMm NOTIFY instrumentChanged)
    Q_PROPERTY(double sensorWidthMm READ sensorWidthMm WRITE setSensorWidthMm NOTIFY instrumentChanged)
    Q_PROPERTY(double sensorHeightMm READ sensorHeightMm WRITE setSensorHeightMm NOTIFY instrumentChanged)
    Q_PROPERTY(double positionAngle READ positionAngle WRITE setPositionAngle NOTIFY instrumentChanged)
    Q_PROPERTY(double fovWidthDeg READ fovWidthDeg NOTIFY instrumentChanged)
    Q_PROPERTY(double fovHeightDeg READ fovHeightDeg NOTIFY instrumentChanged)
    Q_PROPERTY(QVariantMap selectedTarget READ selectedTarget NOTIFY selectedTargetChanged)
    Q_PROPERTY(QVariantList savedTargets READ savedTargets NOTIFY savedTargetsChanged)
    // Manual observing time: horizontal readouts use the wall clock only when
    // followClock is on; otherwise the pinned observation time is used.
    Q_PROPERTY(QDateTime observationTime READ observationTime WRITE setObservationTime NOTIFY observationTimeChanged)
    // Epoch milliseconds of the (effective) observation moment — a plain
    // number crosses the QML bridge without QDateTime/Date conversion gaps.
    Q_PROPERTY(double observationTimeMs READ observationTimeMs NOTIFY observationTimeChanged)
    double observationTimeMs() const { return double(toJulianDayMs(effectiveObservationTime())); }
    static double toJulianDayMs(const QDateTime &utc) { return double(utc.toMSecsSinceEpoch()); }
    Q_PROPERTY(bool followClock READ followClock WRITE setFollowClock NOTIFY followClockChanged)

public:
    explicit AstroPlanner(QObject *parent = nullptr) : QObject(parent) { loadPersisted(); }

    double latitude() const { return m_latitude; }
    double longitude() const { return m_longitude; }
    void setLatitude(double value)
    {
        if (m_latitude == value) return;
        m_latitude = value;
        saveSite();
        emit selectedTargetChanged();
        emit siteChanged();
    }
    void setLongitude(double value)
    {
        if (m_longitude == value) return;
        m_longitude = value;
        saveSite();
        emit selectedTargetChanged();
        emit siteChanged();
    }

    double focalLengthMm() const { return m_focalLengthMm; }
    double sensorWidthMm() const { return m_sensorWidthMm; }
    double sensorHeightMm() const { return m_sensorHeightMm; }
    double positionAngle() const { return m_positionAngle; }
    void setFocalLengthMm(double value)
    {
        if (m_focalLengthMm == value) return;
        m_focalLengthMm = value;
        saveInstrument();
        emit instrumentChanged();
    }
    void setSensorWidthMm(double value)
    {
        if (m_sensorWidthMm == value) return;
        m_sensorWidthMm = value;
        saveInstrument();
        emit instrumentChanged();
    }
    void setSensorHeightMm(double value)
    {
        if (m_sensorHeightMm == value) return;
        m_sensorHeightMm = value;
        saveInstrument();
        emit instrumentChanged();
    }
    void setPositionAngle(double value)
    {
        if (m_positionAngle == value) return;
        m_positionAngle = Astro::normalizeDegrees(value);
        saveInstrument();
        emit instrumentChanged();
    }

    double fovWidthDeg() const
    {
        return Astro::fieldOfView(m_sensorWidthMm, m_sensorHeightMm, m_focalLengthMm).widthDeg;
    }
    double fovHeightDeg() const
    {
        return Astro::fieldOfView(m_sensorWidthMm, m_sensorHeightMm, m_focalLengthMm).heightDeg;
    }

    QVariantMap selectedTarget() const { return m_selectedTarget; }
    QVariantList savedTargets() const { return m_savedTargets; }

    QDateTime observationTime() const { return m_observationTime; }
    void setObservationTime(const QDateTime &value)
    {
        const QDateTime utc = value.isValid() ? value.toUTC() : QDateTime();
        if (!utc.isValid() || utc == m_observationTime) return;
        m_observationTime = utc;
        emit observationTimeChanged();
    }
    bool followClock() const { return m_followClock; }
    void setFollowClock(bool value)
    {
        if (m_followClock == value) return;
        m_followClock = value;
        emit followClockChanged();
    }

    // The moment every alt/az computation refers to.
    QDateTime effectiveObservationTime() const
    {
        return m_followClock ? QDateTime::currentDateTimeUtc() : m_observationTime;
    }

    static double toJulianDay(const QDateTime &utc)
    {
        return Astro::julianDay(utc.date().year(), utc.date().month(),
                                utc.date().day() + utc.time().msecsSinceStartOfDay() / 86400000.0);
    }

    // One night of target/sun altitudes for the curve plot: 48 samples of 30
    // minutes starting at local solar noon of the observation date.
    Q_INVOKABLE QVariantList nightProfile(double raHours, double decDeg) const
    {
        QDateTime start = effectiveObservationTime();
        start.setTime(QTime(12, 0));
        start = start.toUTC().addSecs(qint64(-m_longitude * 3600.0 / 15.0));
        if (start > effectiveObservationTime()) start = start.addSecs(-86400);
        const double startJd = toJulianDay(start);
        QVariantList samples;
        for (int i = 0; i < 48; ++i) {
            const double jd = startJd + i * 30.0 / 1440.0;
            const double lst = Astro::localSiderealTimeHours(jd, m_longitude);
            const Astro::Horizontal target = Astro::equatorialToHorizontal(raHours, decDeg, lst, m_latitude);
            QVariantMap sample;
            sample.insert(QStringLiteral("offsetHours"), i * 0.5);
            sample.insert(QStringLiteral("altitude"), target.altitude);
            sample.insert(QStringLiteral("sunAltitude"), Astro::sunAltitude(jd, m_longitude, m_latitude));
            samples.append(sample);
        }
        return samples;
    }

    // Astronomical night (sun <= -18 deg) of the observation date.
    Q_INVOKABLE QVariantMap nightWindow() const
    {
        return nightSpan(0.0, 0.0, -90.0);
    }

    // Longest span of the astronomical night where the target stands at or
    // above minAltitude — the capture window. One sample walk computes sun
    // and target together: no offsets cross functions through strings, which
    // once produced daytime "best capture" windows.
    Q_INVOKABLE QVariantMap captureWindow(double raHours, double decDeg, double minAltitude) const
    {
        return nightSpan(raHours, decDeg, minAltitude);
    }

    Q_INVOKABLE QVariantMap captureWindow(double raHours, double decDeg) const
    {
        return captureWindow(raHours, decDeg, 30.0);
    }

    Q_INVOKABLE QVariantList searchTargets(const QString &query) const
    {
        return DeepSkyTargets::search(query);
    }

    // Altitude/azimuth of a catalogue entry at the current moment; time is
    // always the system clock in phase one.
    Q_INVOKABLE QVariantMap horizontalCoordinates(const QString &id) const
    {
        DeepSkyTargets::Entry entry;
        if (!DeepSkyTargets::findById(id, &entry)) return {};
        // julianDay wants the day of month plus the day fraction — not the
        // day of year, which would shift the date by months.
        return horizontalState(DeepSkyTargets::toMap(entry), toJulianDay(effectiveObservationTime()));
    }

    // Altitude/azimuth for whatever is currently selected — a catalogue entry
    // or a coordinate target created by selectCoordinate.
    Q_INVOKABLE QVariantMap horizontalCoordinatesOfSelected() const
    {
        if (m_selectedTarget.isEmpty()) return {};
        return horizontalState(m_selectedTarget, toJulianDay(effectiveObservationTime()));
    }

    // Selects a raw sky position (RA in hours, Dec in degrees) without a
    // catalogue entry — the direct coordinate input path.
    Q_INVOKABLE void selectCoordinate(double raHours, double decDeg)
    {
        if (!(raHours >= 0.0 && raHours < 24.0) || !(decDeg >= -90.0 && decDeg <= 90.0)) return;
        const QString coordinateId = QStringLiteral("自定义坐标 %1")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("MMdd-HHmmss")));
        m_selectedTarget = QVariantMap{
            {QStringLiteral("id"), coordinateId},
            {QStringLiteral("name"), QStringLiteral("坐标目标")},
            {QStringLiteral("constellation"), QStringLiteral("—")},
            {QStringLiteral("type"), QStringLiteral("坐标")},
            {QStringLiteral("raHours"), raHours}, {QStringLiteral("decDeg"), decDeg},
            {QStringLiteral("magnitude"), QVariant()}, {QStringLiteral("sizeArcmin"), QVariant()}};
        m_actualCenterRaHours = raHours;
        m_actualCenterDecDeg = decDeg;
        emit selectedTargetChanged();
    }

    Q_INVOKABLE void selectTarget(const QString &id)
    {
        DeepSkyTargets::Entry entry;
        if (!DeepSkyTargets::findById(id, &entry)) return;
        m_selectedTarget = DeepSkyTargets::toMap(entry);
        // A fresh selection re-anchors the framing center on the catalogue
        // position. The assignment MUST go through the emitting setters:
        // a bare write never marks the QML center binding dirty, which once
        // sent every search after the first to the previous target.
        setActualCenterRaHours(entry.raHours);
        setActualCenterDecDeg(entry.decDeg);
        emit selectedTargetChanged();
    }

    // The framing center the user actually dragged to in the survey view.
    // Saved compositions record this instead of the catalogue position.
    Q_PROPERTY(double actualCenterRaHours READ actualCenterRaHours WRITE setActualCenterRaHours NOTIFY actualCenterChanged)
    Q_PROPERTY(double actualCenterDecDeg READ actualCenterDecDeg WRITE setActualCenterDecDeg NOTIFY actualCenterChanged)
    double actualCenterRaHours() const { return m_actualCenterRaHours; }
    double actualCenterDecDeg() const { return m_actualCenterDecDeg; }
    void setActualCenterRaHours(double value)
    {
        if (!(value >= 0.0 && value < 24.0) || std::abs(value - m_actualCenterRaHours) < 1e-6) return;
        m_actualCenterRaHours = value;
        emit actualCenterChanged();
    }
    void setActualCenterDecDeg(double value)
    {
        if (!(value >= -90.0 && value <= 90.0) || std::abs(value - m_actualCenterDecDeg) < 1e-5) return;
        m_actualCenterDecDeg = value;
        emit actualCenterChanged();
    }
    // Atomic RA+Dec update from the survey-view drag poll: both halves land
    // together so no repaint ever sees a mixed old/new center.
    Q_INVOKABLE void setActualCenter(double raHours, double decDeg)
    {
        const bool raValid = raHours >= 0.0 && raHours < 24.0
            && std::abs(raHours - m_actualCenterRaHours) >= 1e-6;
        const bool decValid = decDeg >= -90.0 && decDeg <= 90.0
            && std::abs(decDeg - m_actualCenterDecDeg) >= 1e-5;
        if (!raValid && !decValid) return;
        if (raValid) m_actualCenterRaHours = raHours;
        if (decValid) m_actualCenterDecDeg = decDeg;
        emit actualCenterChanged();
    }
    // Re-anchors the framing center on the selected target's catalogue
    // position — the "回到目标" action.
    Q_INVOKABLE void resetActualCenter()
    {
        const double ra = m_selectedTarget.value(QStringLiteral("raHours")).toDouble();
        const double dec = m_selectedTarget.value(QStringLiteral("decDeg")).toDouble();
        if (ra <= 0.0 && m_selectedTarget.isEmpty()) return;
        setActualCenterRaHours(ra);
        setActualCenterDecDeg(dec);
        emit selectedTargetChanged();
    }

    // Saves or updates the composition of the selected target: the full
    // instrument setup travels with it, so restoring reproduces the frame.
    // The framing center actually dragged in the survey view is recorded, not
    // the catalogue position.
    Q_INVOKABLE void saveSelectedTarget()
    {
        if (m_selectedTarget.isEmpty()) return;
        const QString id = m_selectedTarget.value(QStringLiteral("id")).toString();
        QVariantMap entry = m_selectedTarget;
        entry.insert(QStringLiteral("positionAngle"), m_positionAngle);
        entry.insert(QStringLiteral("focalLengthMm"), m_focalLengthMm);
        entry.insert(QStringLiteral("sensorWidthMm"), m_sensorWidthMm);
        entry.insert(QStringLiteral("sensorHeightMm"), m_sensorHeightMm);
        entry.insert(QStringLiteral("centerRaHours"), m_actualCenterRaHours);
        entry.insert(QStringLiteral("centerDecDeg"), m_actualCenterDecDeg);
        entry.insert(QStringLiteral("savedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
        for (int i = 0; i < m_savedTargets.size(); ++i) {
            if (m_savedTargets[i].toMap().value(QStringLiteral("id")).toString() == id) {
                m_savedTargets[i] = entry;
                saveSavedTargets();
                emit savedTargetsChanged();
                return;
            }
        }
        m_savedTargets.append(entry);
        saveSavedTargets();
        emit savedTargetsChanged();
    }

    // Restores a saved composition: selects the target and reapplies the
    // focal length, sensor size, position angle and actual framing center.
    Q_INVOKABLE void applySavedTarget(const QString &id)
    {
        for (const auto &value : m_savedTargets) {
            const auto entry = value.toMap();
            if (entry.value(QStringLiteral("id")).toString() != id) continue;
            if (entry.value(QStringLiteral("type")).toString() == QStringLiteral("坐标")) {
                // Coordinate targets have no catalogue entry: restore straight
                // from the saved record.
                m_selectedTarget = entry;
                setFocalLengthMm(entry.value(QStringLiteral("focalLengthMm")).toDouble());
                setSensorWidthMm(entry.value(QStringLiteral("sensorWidthMm")).toDouble());
                setSensorHeightMm(entry.value(QStringLiteral("sensorHeightMm")).toDouble());
                setPositionAngle(entry.value(QStringLiteral("positionAngle")).toDouble());
                setActualCenterRaHours(entry.value(QStringLiteral("raHours")).toDouble());
                setActualCenterDecDeg(entry.value(QStringLiteral("decDeg")).toDouble());
                emit selectedTargetChanged();
                return;
            }
            selectTarget(id);
            setFocalLengthMm(entry.value(QStringLiteral("focalLengthMm")).toDouble());
            setSensorWidthMm(entry.value(QStringLiteral("sensorWidthMm")).toDouble());
            setSensorHeightMm(entry.value(QStringLiteral("sensorHeightMm")).toDouble());
            setPositionAngle(entry.value(QStringLiteral("positionAngle")).toDouble());
            const double centerRa = entry.contains(QStringLiteral("centerRaHours"))
                ? entry.value(QStringLiteral("centerRaHours")).toDouble() : -1.0;
            const double centerDec = entry.contains(QStringLiteral("centerDecDeg"))
                ? entry.value(QStringLiteral("centerDecDeg")).toDouble() : -91.0;
            if (centerRa >= 0.0 && centerDec >= -90.0) {
                setActualCenterRaHours(centerRa);
                setActualCenterDecDeg(centerDec);
            }
            emit selectedTargetChanged();
            return;
        }
    }

    Q_INVOKABLE bool isTargetSaved(const QString &id) const
    {
        for (const auto &value : m_savedTargets) {
            if (value.toMap().value(QStringLiteral("id")).toString() == id) return true;
        }
        return false;
    }

    // RA/Dec corners of the planned capture frame for survey-view footprints:
    // [[raDeg, decDeg], ...] clockwise from the frame's top-left.
    Q_INVOKABLE QVariantList footprintCorners(double raHours, double decDeg,
                                              double fovWidthDeg, double fovHeightDeg,
                                              double positionAngleDeg) const
    {
        QVariantList result;
        for (const auto &corner : Astro::footprintCorners(raHours, decDeg, fovWidthDeg, fovHeightDeg, positionAngleDeg))
            // The explicit QVariant wrapper matters: append(QVariantList)
            // resolves to the "join both lists" overload, which flattened the
            // four pairs into eight bare numbers on the QML side.
            result.append(QVariant(QVariantList{corner[0], corner[1]}));
        return result;
    }

    Q_INVOKABLE void removeSavedTarget(const QString &id)
    {
        for (int i = 0; i < m_savedTargets.size(); ++i) {
            if (m_savedTargets[i].toMap().value(QStringLiteral("id")).toString() == id) {
                m_savedTargets.removeAt(i);
                saveSavedTargets();
                emit savedTargetsChanged();
                return;
            }
        }
    }

signals:
    void siteChanged();
    void instrumentChanged();
    void selectedTargetChanged();
    void actualCenterChanged();
    void savedTargetsChanged();
    void observationTimeChanged();
    void followClockChanged();

private:
    // Walks the observation date's night in 30-minute steps and returns the
    // longest span where the sun is below -18 deg AND the target (when its
    // minAltitude gate is meaningful) stands above minTargetAltitude.
    QVariantMap nightSpan(double raHours, double decDeg, double minTargetAltitude) const
    {
        QDateTime start = effectiveObservationTime();
        start.setTime(QTime(12, 0));
        start = start.toUTC().addSecs(qint64(-m_longitude * 3600.0 / 15.0));
        if (start > effectiveObservationTime()) start = start.addSecs(-86400);
        const double startJd = toJulianDay(start);
        double bestFrom = -1.0, bestTo = -1.0, best = -90.0;
        double from = -1.0, to = -1.0;
        for (int i = 0; i <= 48; ++i) {
            const double jd = startJd + i * 30.0 / 1440.0;
            const double lst = Astro::localSiderealTimeHours(jd, m_longitude);
            const Astro::Horizontal target = Astro::equatorialToHorizontal(raHours, decDeg, lst, m_latitude);
            const double sunAlt = Astro::sunAltitude(jd, m_longitude, m_latitude);
            const bool inside = sunAlt <= -18.0 && target.altitude >= minTargetAltitude;
            if (inside) {
                if (from < 0) from = jd;
                to = jd;
                best = std::max(best, target.altitude);
            } else if (from >= 0) {
                // A gap closes the current span; keep the longest one.
                if (to - from > bestTo - bestFrom) { bestFrom = from; bestTo = to; }
                from = -1.0;
            }
        }
        if (from >= 0 && to - from > bestTo - bestFrom) { bestFrom = from; bestTo = to; }
        if (bestFrom < 0) return {};
        from = bestFrom;
        to = bestTo;
        return {{"startISO", fromJulianDay(from).toString(Qt::ISODate)},
                {"endISO", fromJulianDay(to).toString(Qt::ISODate)},
                {"startOffsetHours", (from - startJd) * 24.0},
                {"endOffsetHours", (to - startJd) * 24.0},
                {"maxAltitude", best}};
    }

    static QDateTime fromJulianDay(double jd)
    {
        return QDateTime::fromMSecsSinceEpoch(qint64((jd - 2440587.5) * 86400000.0), Qt::UTC);
    }

    QVariantMap horizontalState(const QVariantMap &base, double jd) const
    {
        const double raHours = base.value(QStringLiteral("raHours")).toDouble();
        const double decDeg = base.value(QStringLiteral("decDeg")).toDouble();
        const double lst = Astro::localSiderealTimeHours(jd, m_longitude);
        const Astro::Horizontal h = Astro::equatorialToHorizontal(raHours, decDeg, lst, m_latitude);
        const Astro::VisibilitySummary visibility = Astro::surveyVisibility(raHours, decDeg, m_latitude);
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const double jdMidnight = Astro::julianDay(now.date().year(), now.date().month(), now.date().day());
        QVariantMap state = base;
        state.insert(QStringLiteral("altitude"), h.altitude);
        state.insert(QStringLiteral("azimuth"), h.azimuth);
        state.insert(QStringLiteral("aboveHorizon"), h.altitude > 0.0);
        state.insert(QStringLiteral("lstHours"), lst);
        state.insert(QStringLiteral("maxAltitude"), visibility.maxAltitude);
        state.insert(QStringLiteral("maxAltitudeLstHours"), visibility.maxAltitudeLstHours);
        state.insert(QStringLiteral("alwaysUp"), visibility.alwaysUp);
        state.insert(QStringLiteral("neverUp"), visibility.neverUp);
        state.insert(QStringLiteral("jdMidnight"), jdMidnight);
        return state;
    }

    void loadPersisted()
    {
        QSettings settings;
        m_latitude = settings.value(QStringLiteral("planner/latitude"), 30.65).toDouble();
        m_longitude = settings.value(QStringLiteral("planner/longitude"), 104.07).toDouble();
        m_focalLengthMm = settings.value(QStringLiteral("planner/focalLengthMm"), 550.0).toDouble();
        m_sensorWidthMm = settings.value(QStringLiteral("planner/sensorWidthMm"), 23.5).toDouble();
        m_sensorHeightMm = settings.value(QStringLiteral("planner/sensorHeightMm"), 15.7).toDouble();
        m_positionAngle = settings.value(QStringLiteral("planner/positionAngle"), 0.0).toDouble();
        const QJsonDocument doc = QJsonDocument::fromJson(
            settings.value(QStringLiteral("planner/savedTargets")).toString().toUtf8());
        for (const auto &value : doc.array())
            m_savedTargets.append(value.toObject().toVariantMap());
    }
    void saveSite()
    {
        QSettings settings;
        settings.setValue(QStringLiteral("planner/latitude"), m_latitude);
        settings.setValue(QStringLiteral("planner/longitude"), m_longitude);
    }
    void saveInstrument()
    {
        QSettings settings;
        settings.setValue(QStringLiteral("planner/focalLengthMm"), m_focalLengthMm);
        settings.setValue(QStringLiteral("planner/sensorWidthMm"), m_sensorWidthMm);
        settings.setValue(QStringLiteral("planner/sensorHeightMm"), m_sensorHeightMm);
        settings.setValue(QStringLiteral("planner/positionAngle"), m_positionAngle);
    }
    void saveSavedTargets()
    {
        QJsonArray array;
        for (const auto &value : m_savedTargets) array.append(QJsonObject::fromVariantMap(value.toMap()));
        QSettings settings;
        settings.setValue(QStringLiteral("planner/savedTargets"),
                          QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    }

    double m_latitude = 30.65;
    double m_longitude = 104.07;
    double m_focalLengthMm = 550.0;
    double m_sensorWidthMm = 23.5;
    double m_sensorHeightMm = 15.7;
    double m_positionAngle = 0.0;
    double m_actualCenterRaHours = -1.0;
    double m_actualCenterDecDeg = -91.0;
    QDateTime m_observationTime = QDateTime::currentDateTimeUtc();
    bool m_followClock = true;
    QVariantMap m_selectedTarget;
    QVariantList m_savedTargets;
};
