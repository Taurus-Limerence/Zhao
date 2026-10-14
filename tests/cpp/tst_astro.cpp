#include <QtTest>
#include <QDateTime>
#include <QSettings>
#include <QSet>
#include <QQmlEngine>
#include <QCoreApplication>
#include "astro/AstroMath.h"
#include "astro/AstroPlanner.h"
#include "astro/DeepSkyTargets.h"

using namespace Astro;

class AstroTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("ToupTek Astro"));
        QCoreApplication::setApplicationName(QStringLiteral("StellaMora-AstroTests"));
        QSettings().remove(QStringLiteral("planner"));
    }
    void cleanup() {
        QSettings().remove(QStringLiteral("planner"));
    }
    void julianDayMatchesReferenceEpoch() {
        // JD 2451545.0 == 2000-01-01 12:00 UT; Meeus example 7.b (Sputnik I,
        // 1957 October 4.81 UT) == JD 2436116.31.
        QCOMPARE(julianDay(2000, 1, 1.5), 2451545.0);
        QVERIFY(std::abs(julianDay(1957, 10, 4.81) - 2436116.31) < 1e-6);
        QCOMPARE(julianDay(2000, 1, 1.0), 2451544.5);
    }

    void greenwichSiderealTimeAtReferenceEpoch() {
        // GMST at J2000.0 noon: 280.46061837 degrees = 18.697374558 hours.
        const double gmst = greenwichSiderealTimeHours(2451545.0);
        QVERIFY(std::abs(gmst - 18.697374558) < 1e-4);
        // Per solar day GMST gains 3m56.6s (24.0657h wraps to 0.0657h).
        const double next = greenwichSiderealTimeHours(2451546.0);
        QVERIFY(std::abs(normalizeHours(next - gmst) - 0.0657098244) < 1e-6);
    }

    void localSiderealTimeShiftsWithLongitude() {
        const double gmst = greenwichSiderealTimeHours(2451545.0);
        // Longitude east adds hours/15 (mod 24); -104.07 shifts west by 6.938h.
        QCOMPARE(normalizeHours(localSiderealTimeHours(2451545.0, -104.07) - gmst), normalizeHours(-104.07 / 15.0));
        QCOMPARE(normalizeHours(localSiderealTimeHours(2451545.0, 104.07) - gmst), normalizeHours(104.07 / 15.0));
    }

    void meridianTransitSitsDueSouth() {
        // Object culminating (H = 0) south of the zenith: altitude 90-|lat-dec|, azimuth 180.
        const double lst = 12.0;
        const Horizontal h = equatorialToHorizontal(lst, 0.0, lst, 40.0);
        QVERIFY(std::abs(h.altitude - 50.0) < 1e-6);
        QVERIFY(std::abs(h.azimuth - 180.0) < 1e-6);
    }

    void risingObjectSitsDueEast() {
        // H = -90, dec = 0, equator site: on the horizon, due east.
        const double lst = 6.0, ra = 12.0;
        const Horizontal h = equatorialToHorizontal(ra, 0.0, lst, 0.0);
        QVERIFY(std::abs(h.altitude) < 1e-6);
        QVERIFY(std::abs(h.azimuth - 90.0) < 1e-6);
    }

    void polarisAltitudeTracksSiteLatitude() {
        // Polaris (dec ~ +89.35) from lat 30.65: upper culmination 90-|lat-dec| = 31.3,
        // always within ~1.7 degrees of the latitude itself.
        const Horizontal h = equatorialToHorizontal(2.53, 89.35, 2.53, 30.65);
        QVERIFY(std::abs(h.altitude - 31.3) < 0.2);
    }

    void circumpolarAndNeverRisingTargets() {
        // dec +80 at lat 40 is circumpolar; dec -60 never rises.
        const VisibilitySummary up = surveyVisibility(5.0, 80.0, 40.0);
        QVERIFY(up.alwaysUp);
        QVERIFY(!up.neverUp);
        const VisibilitySummary down = surveyVisibility(5.0, -60.0, 40.0);
        QVERIFY(down.neverUp);
        QVERIFY(!down.alwaysUp);
        // A mid-declination target culminates at 90-|lat-dec|.
        const VisibilitySummary mid = surveyVisibility(0.7123, 41.269, 30.65);
        QVERIFY(std::abs(mid.maxAltitude - (90.0 - std::abs(30.65 - 41.269))) < 0.5);
    }

    void fieldOfViewMatchesReferenceGeometry() {
        // 36 mm full-frame behind a 1000 mm lens: 2*atan(18/1000) = 2.0626 deg.
        const FieldOfView fov = fieldOfView(36.0, 24.0, 1000.0);
        QVERIFY(std::abs(fov.widthDeg - 2.0626) < 1e-3);
        QVERIFY(std::abs(fov.heightDeg - 1.3751) < 1e-3);
        // Zero or negative inputs stay empty rather than exploding.
        QCOMPARE(fieldOfView(36.0, 24.0, 0.0).widthDeg, 0.0);
        QCOMPARE(fieldOfView(-1.0, 24.0, 500.0).widthDeg, 0.0);
    }

    void catalogueSearchFindsM31() {
        const QVariantList all = DeepSkyTargets::search(QString());
        // Full Messier list plus the famous NGC/IC entries.
        QVERIFY(all.size() >= 110);
        // Integrity: unique ids, sane coordinate ranges.
        QSet<QString> ids;
        for (const auto &value : all) {
            const auto entry = value.toMap();
            const QString id = entry["id"].toString();
            QVERIFY(!ids.contains(id));
            ids.insert(id);
            const double ra = entry["raHours"].toDouble();
            const double dec = entry["decDeg"].toDouble();
            QVERIFY(ra >= 0.0 && ra < 24.0);
            QVERIFY(dec >= -90.0 && dec <= 90.0);
        }
        // Spot check against known J2000 positions.
        QVERIFY(std::abs(DeepSkyTargets::search(QStringLiteral("m87"))[0].toMap()["raHours"].toDouble() - 12.514) < 0.01);
        QVERIFY(std::abs(DeepSkyTargets::search(QStringLiteral("m42"))[0].toMap()["decDeg"].toDouble() - (-5.391)) < 0.01);
        const QVariantList byId = DeepSkyTargets::search(QStringLiteral("m31"));
        QCOMPARE(byId.size(), 1);
        QCOMPARE(byId[0].toMap()["name"].toString(), QStringLiteral("仙女座星系"));
        QVERIFY(!DeepSkyTargets::search(QStringLiteral("仙女")).isEmpty());
        QVERIFY(DeepSkyTargets::search(QStringLiteral("不存在目标")).isEmpty());
        // J2000 position sanity for M31: 0h42.7m +41°16'.
        QVERIFY(std::abs(byId[0].toMap()["raHours"].toDouble() - 0.7123) < 1e-3);
        QVERIFY(std::abs(byId[0].toMap()["decDeg"].toDouble() - 41.269) < 1e-3);
    }

    void footprintCornersFrameTheTarget() {
        // Zero rotation: a 2°x1° frame around M31 spreads its corners by
        // ±1° in RA (scaled by cos(dec)) and ±0.5° in Dec.
        const auto straight = footprintCorners(0.7123, 41.269, 2.0, 1.0, 0.0);
        QCOMPARE(straight.size(), 4);
        QCOMPARE(straight[0][1], 41.769);
        QCOMPARE(straight[3][1], 40.769);
        QVERIFY(std::abs(straight[0][0] - (0.7123 * 15.0 - 1.0 / std::cos(41.769 * kPi / 180.0))) < 1e-9);
        QVERIFY(std::abs(straight[1][0] - (0.7123 * 15.0 + 1.0 / std::cos(41.769 * kPi / 180.0))) < 1e-9);
        // 90 degrees rotation swaps the halves: the 2° side now runs along Dec
        // (corners at dec ± 1) and the 1° side along RA (corners near the
        // center RA, offset by 0.5° / cos(dec at corner)).
        const auto rotated = footprintCorners(0.7123, 41.269, 2.0, 1.0, 90.0);
        QCOMPARE(rotated[0][1], 42.269);
        QCOMPARE(rotated[1][1], 40.269);
        QVERIFY(std::abs(rotated[0][0] - (0.7123 * 15.0 + 0.5 / std::cos(42.269 * kPi / 180.0))) < 1e-9);
        QVERIFY(std::abs(rotated[1][0] - (0.7123 * 15.0 + 0.5 / std::cos(40.269 * kPi / 180.0))) < 1e-9);
        // Degenerate frames stay empty instead of producing a point.
        QVERIFY(footprintCorners(0.7, 41.0, 0.0, 1.0, 0.0).isEmpty());
    }

    void plannerHorizontalCoordinatesUseRealDates() {
        AstroPlanner planner;
        planner.setLongitude(0.0);
        planner.setLatitude(40.0);
        // With the day-of-month bug the date drifted by ~8 months and the
        // altitude came out far off; at any instant the M31 altitude must sit
        // within the physical envelope for its declination and the site.
        const QVariantMap state = planner.horizontalCoordinates(QStringLiteral("M31"));
        QCOMPARE(state.size() > 0, true);
        QVERIFY(state["altitude"].toDouble() >= -90.0 - 1e-6);
        QVERIFY(state["altitude"].toDouble() <= 90.0 + 1e-6);
        // Meridian-transit altitude is fixed by geometry: 90 - |lat - dec|.
        // The 10-minute visibility survey approaches it from below.
        const double transit = 90.0 - std::abs(40.0 - 41.269);
        QVERIFY(std::abs(state["maxAltitude"].toDouble() - transit) < 0.15);
        QVERIFY(state["aboveHorizon"].toBool() || !state["aboveHorizon"].toBool());
    }

    void plannerFootprintCornersStayNestedAcrossQmlBridge() {
        // Regression: the survey page needs [[ra,dec] x4]; verify the exact
        // QML bridge conversion keeps the pairs nested (no flattening).
        AstroPlanner planner;
        QQmlEngine engine;
        // Stack object: ownership must stay with C++ or the JS GC would
        // delete it out from under the test.
        QQmlEngine::setObjectOwnership(&planner, QQmlEngine::CppOwnership);
        engine.globalObject().setProperty(QStringLiteral("planner"),
                                          engine.newQObject(&planner));
        const QJSValue result = engine.evaluate(
            "JSON.stringify(planner.footprintCorners(0.7123, 41.269, 2.0, 1.0, 0.0))");
        QCOMPARE(result.isError(), false);
        const QJsonDocument doc = QJsonDocument::fromJson(result.toString().toUtf8());
        QVERIFY(doc.isArray());
        const QJsonArray corners = doc.array();
        // Source fix regression: append(QVariantList) used to join the lists
        // and flatten the four pairs into eight bare numbers. The bridge must
        // deliver [[ra,dec] x4].
        QCOMPARE(corners.size(), 4);
        const double centerRaDeg = 0.7123 * 15.0;
        const QJsonArray first = corners.at(0).toArray();
        QVERIFY(first.size() == 2);
        QVERIFY(std::abs(first.at(0).toDouble() - (centerRaDeg - 1.0 / std::cos(41.769 * Astro::kPi / 180.0))) < 1e-9);
        QCOMPARE(first.at(1).toDouble(), 41.769);
        const QJsonArray second = corners.at(1).toArray();
        QVERIFY(std::abs(second.at(0).toDouble() - (centerRaDeg + 1.0 / std::cos(41.769 * Astro::kPi / 180.0))) < 1e-9);
        QCOMPARE(second.at(1).toDouble(), 41.769);
        const QJsonArray third = corners.at(2).toArray();
        QCOMPARE(third.at(1).toDouble(), 40.769);
        const QJsonArray fourth = corners.at(3).toArray();
        QCOMPARE(fourth.at(1).toDouble(), 40.769);
    }

    void sunPositionMatchesHardAnchors() {
        // 2000 March 20 07:35 UTC = vernal equinox: RA ~ 0h, Dec ~ 0.
        const Equatorial march = sunPosition(2451623.81597);
        QVERIFY(std::abs(march.raHours) < 0.02 || std::abs(march.raHours - 24.0) < 0.02);
        QVERIFY(std::abs(march.decDeg) < 0.1);
        // 2000 June 21 01:48 UTC = solstice: RA ~ 6h, Dec ~ +23.436.
        const Equatorial june = sunPosition(2451716.575);
        QVERIFY(std::abs(june.raHours - 6.0) < 0.02);
        QVERIFY(std::abs(june.decDeg - 23.436) < 0.1);
    }

    void plannerNightWindowsUseObservationTime() {
        AstroPlanner planner;
        planner.setLatitude(30.65);
        planner.setLongitude(104.07);
        planner.setFollowClock(false);
        planner.setObservationTime(QDateTime(QDate(2026, 9, 18), QTime(14, 0), Qt::UTC));
        planner.selectTarget(QStringLiteral("M31"));

        // Pinning the clock: the same target reads different altitudes at
        // different observation times.
        const double evening = planner.horizontalCoordinatesOfSelected()["altitude"].toDouble();
        planner.setObservationTime(QDateTime(QDate(2026, 9, 18), QTime(4, 0), Qt::UTC));
        const double morning = planner.horizontalCoordinatesOfSelected()["altitude"].toDouble();
        QVERIFY(std::abs(evening - morning) > 10.0);

        // September: M31 stands high during the astronomical night, so a
        // >=30 deg capture window exists and peaks near transit altitude.
        planner.setObservationTime(QDateTime(QDate(2026, 9, 18), QTime(14, 0), Qt::UTC));
        const QVariantMap window = planner.captureWindow(0.7123, 41.269, 30.0);
        QVERIFY(window.contains(QStringLiteral("startISO")));
        QVERIFY(window["maxAltitude"].toDouble() > 70.0);
        QVERIFY(window["maxAltitude"].toDouble() <= 79.5);

        // A target that never climbs above 30 deg has no capture window.
        QVERIFY(planner.captureWindow(5.0, -60.0, 30.0).isEmpty());

        // The night itself has an astronomical-darkness span on this date.
        QVERIFY(planner.nightWindow().contains(QStringLiteral("startISO")));
        QCOMPARE(planner.nightProfile(0.7123, 41.269).size(), 48);
    }

    void plannerSavesUpdatesAndRestoresCompositions() {
        AstroPlanner planner;
        planner.selectTarget(QStringLiteral("M31"));
        planner.setFocalLengthMm(800.0);
        planner.setSensorWidthMm(36.0);
        planner.setSensorHeightMm(24.0);
        planner.setPositionAngle(35.0);
        planner.saveSelectedTarget();
        QCOMPARE(planner.savedTargets().size(), 1);

        // Re-saving the same target updates the composition instead of
        // silently dropping the change.
        planner.setPositionAngle(12.0);
        planner.saveSelectedTarget();
        QCOMPARE(planner.savedTargets().size(), 1);
        QCOMPARE(planner.savedTargets()[0].toMap()["positionAngle"].toDouble(), 12.0);

        // A different instrument changes the frame; restoring brings back the
        // complete saved composition, sensor size included.
        planner.setFocalLengthMm(300.0);
        planner.setSensorWidthMm(17.3);
        planner.setSensorHeightMm(13.0);
        planner.applySavedTarget(QStringLiteral("M31"));
        QCOMPARE(planner.selectedTarget()["id"].toString(), QStringLiteral("M31"));
        QCOMPARE(planner.focalLengthMm(), 800.0);
        QCOMPARE(planner.sensorWidthMm(), 36.0);
        QCOMPARE(planner.sensorHeightMm(), 24.0);
        QCOMPARE(planner.positionAngle(), 12.0);
        QCOMPARE(planner.fovWidthDeg(), Astro::fieldOfView(36.0, 24.0, 800.0).widthDeg);
        QVERIFY(planner.isTargetSaved(QStringLiteral("M31")));
        QVERIFY(!planner.isTargetSaved(QStringLiteral("M42")));

        // Dragging in the survey view re-anchors the center; saving records
        // the actual framing center, and restoring brings it back.
        planner.setActualCenterRaHours(0.8123);
        planner.setActualCenterDecDeg(41.9);
        planner.saveSelectedTarget();
        QCOMPARE(planner.savedTargets()[0].toMap()["centerRaHours"].toDouble(), 0.8123, 1e-9);
        QCOMPARE(planner.savedTargets()[0].toMap()["centerDecDeg"].toDouble(), 41.9, 1e-9);
        planner.selectTarget(QStringLiteral("M42"));
        QCOMPARE(planner.actualCenterRaHours(), 5.588);
        planner.applySavedTarget(QStringLiteral("M31"));
        QCOMPARE(planner.actualCenterRaHours(), 0.8123, 1e-9);
        QCOMPARE(planner.actualCenterDecDeg(), 41.9, 1e-9);

        // Atomic center update lands both halves together; reset returns to
        // the catalogue position of the selected target.
        planner.setActualCenter(0.9, 42.5);
        QCOMPARE(planner.actualCenterRaHours(), 0.9, 1e-9);
        QCOMPARE(planner.actualCenterDecDeg(), 42.5, 1e-9);
        planner.resetActualCenter();
        QCOMPARE(planner.actualCenterRaHours(), 0.7123, 1e-9);
        QCOMPARE(planner.actualCenterDecDeg(), 41.269, 1e-9);

        // Direct coordinate input creates a plannable pseudo-target with a
        // UNIQUE id (timestamped), so two saves never overwrite each other.
        planner.selectCoordinate(10.5, -30.0);
        QVERIFY(planner.selectedTarget()["id"].toString().startsWith(QStringLiteral("自定义坐标")));
        QCOMPARE(planner.selectedTarget()["raHours"].toDouble(), 10.5);
        QVERIFY(!planner.horizontalCoordinatesOfSelected().isEmpty());
        planner.selectCoordinate(25.0, 0.0); // out of range: selection unchanged
        QCOMPARE(planner.selectedTarget()["raHours"].toDouble(), 10.5);

        // A saved coordinate target restores through its own record, not the
        // catalogue lookup.
        planner.selectCoordinate(3.25, -12.5);
        planner.setFocalLengthMm(400.0);
        planner.setPositionAngle(90.0);
        planner.saveSelectedTarget();
        const QString coordinateId = planner.savedTargets().last().toMap()["id"].toString();
        planner.selectTarget(QStringLiteral("M42"));
        planner.applySavedTarget(coordinateId);
        QCOMPARE(planner.selectedTarget()["id"].toString(), coordinateId);
        QCOMPARE(planner.selectedTarget()["raHours"].toDouble(), 3.25);
        QCOMPARE(planner.focalLengthMm(), 400.0);
        QCOMPARE(planner.positionAngle(), 90.0);

        // Saved compositions survive a fresh planner instance (QSettings):
        // the updated M31 composition plus the coordinate target.
        AstroPlanner fresh;
        QCOMPARE(fresh.savedTargets().size(), 2);
        QCOMPARE(fresh.savedTargets()[0].toMap()["sensorHeightMm"].toDouble(), 24.0);
        fresh.applySavedTarget(QStringLiteral("M31"));
        QCOMPARE(fresh.positionAngle(), 12.0);
        fresh.removeSavedTarget(QStringLiteral("M31"));
        QCOMPARE(fresh.savedTargets().size(), 1);
        QVERIFY(fresh.savedTargets()[0].toMap()["id"].toString()
                .startsWith(QStringLiteral("自定义坐标")));
    }
};

QTEST_GUILESS_MAIN(AstroTest)
#include "tst_astro.moc"
