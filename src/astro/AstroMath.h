#pragma once

#include <QList>
#include <algorithm>
#include <cmath>

// Pure astronomy math for the observing planner: sidereal time, equatorial to
// horizontal conversion and field-of-view geometry. No SDK or hardware input —
// every formula is unit-testable against reference values (Meeus, ch. 8/12/13).
namespace Astro {

constexpr double kPi = 3.14159265358979323846;

inline double degToRad(double degrees) { return degrees * kPi / 180.0; }
inline double radToDeg(double radians) { return radians * 180.0 / kPi; }
inline double sinDeg(double degrees) { return std::sin(degToRad(degrees)); }
inline double cosDeg(double degrees) { return std::cos(degToRad(degrees)); }
inline double tanDeg(double degrees) { return std::tan(degToRad(degrees)); }

inline double normalizeDegrees(double degrees)
{
    const double value = std::fmod(degrees, 360.0);
    return value < 0.0 ? value + 360.0 : value;
}

inline double normalizeHours(double hours)
{
    const double value = std::fmod(hours, 24.0);
    return value < 0.0 ? value + 24.0 : value;
}

// Julian Day from a Gregorian UTC calendar moment; day carries the fraction
// (day 1.5 = noon). Meeus ch.7, valid after the 1582 Gregorian reform.
inline double julianDay(int year, int month, double day)
{
    if (month <= 2) { year -= 1; month += 12; }
    const int a = year / 100;
    const int b = 2 - a + a / 4;
    return std::floor(365.25 * (year + 4716)) + std::floor(30.6001 * (month + 1))
         + day + b - 1524.5;
}

// Greenwich mean sidereal time in hours. Meeus ch.12, accuracy ~0.1s per century.
inline double greenwichSiderealTimeHours(double jd)
{
    const double t = (jd - 2451545.0) / 36525.0;
    const double degrees = 280.46061837 + 360.98564736629 * (jd - 2451545.0)
                         + 0.000387933 * t * t - t * t * t / 38710000.0;
    return normalizeHours(normalizeDegrees(degrees) / 15.0);
}

inline double localSiderealTimeHours(double jd, double longitudeEastDeg)
{
    return normalizeHours(greenwichSiderealTimeHours(jd) + longitudeEastDeg / 15.0);
}

struct Horizontal {
    double altitude = 0.0;  // degrees above horizon
    double azimuth = 0.0;   // degrees from north, counting eastward
};

// Equatorial (RA hours, Dec degrees) to horizontal at an observing site.
// Azimuth 0 = north, 90 = east, 180 = south.
inline Horizontal equatorialToHorizontal(double raHours, double decDeg,
                                         double lstHours, double latitudeDeg)
{
    const double hourAngleDeg = normalizeDegrees((lstHours - raHours) * 15.0);
    Horizontal result;
    const double sinAlt = sinDeg(decDeg) * sinDeg(latitudeDeg)
                        + cosDeg(decDeg) * cosDeg(latitudeDeg) * cosDeg(hourAngleDeg);
    result.altitude = radToDeg(std::asin(std::clamp(sinAlt, -1.0, 1.0)));
    const double y = -sinDeg(hourAngleDeg) * cosDeg(decDeg);
    const double x = cosDeg(latitudeDeg) * sinDeg(decDeg)
                   - sinDeg(latitudeDeg) * cosDeg(decDeg) * cosDeg(hourAngleDeg);
    result.azimuth = normalizeDegrees(radToDeg(std::atan2(y, x)));
    return result;
}

// Rectangular field of view from a sensor of width/height (mm, along/against
// the camera's long axis) behind a lens of focalLengthMm.
struct FieldOfView {
    double widthDeg = 0.0;
    double heightDeg = 0.0;
};

inline FieldOfView fieldOfView(double sensorWidthMm, double sensorHeightMm, double focalLengthMm)
{
    FieldOfView result;
    if (focalLengthMm <= 0.0 || sensorWidthMm <= 0.0 || sensorHeightMm <= 0.0) return result;
    result.widthDeg = 2.0 * radToDeg(std::atan(sensorWidthMm / (2.0 * focalLengthMm)));
    result.heightDeg = 2.0 * radToDeg(std::atan(sensorHeightMm / (2.0 * focalLengthMm)));
    return result;
}

// The four RA/Dec corners of the planned capture frame, ordered clockwise
// starting at the top-left as seen on the sky. Small-field flat-sky
// approximation: offsets rotate in a tangent plane, RA shrinks by cos(dec).
// Returns degrees pairs [[ra, dec], ...] for overlay footprints.
inline QList<QList<double>> footprintCorners(double raHours, double decDeg,
                                             double fovWidthDeg, double fovHeightDeg,
                                             double positionAngleDeg)
{
    QList<QList<double>> corners;
    const double halfWidth = fovWidthDeg / 2.0;
    const double halfHeight = fovHeightDeg / 2.0;
    if (fovWidthDeg <= 0.0 || fovHeightDeg <= 0.0) return corners;
    const double angle = degToRad(positionAngleDeg);
    const double cosA = std::cos(angle);
    const double sinA = std::sin(angle);
    const double local[][2] = {{-halfWidth, halfHeight}, {halfWidth, halfHeight},
                               {halfWidth, -halfHeight}, {-halfWidth, -halfHeight}};
    for (const auto *offset : local) {
        const double east = offset[0] * cosA + offset[1] * sinA;
        const double north = -offset[0] * sinA + offset[1] * cosA;
        const double dec = std::clamp(decDeg + north, -89.999, 89.999);
        const double ra = normalizeHours(raHours + east / (15.0 * cosDeg(dec)));
        corners.append({ra * 15.0, dec});
    }
    return corners;
}

// Samples the altitude of a fixed target through one sidereal rotation. Used
// for rise/set style summaries without iterative ephemeris solvers.
struct VisibilitySummary {
    bool alwaysUp = false;      // circumpolar above the horizon
    bool neverUp = false;
    double maxAltitude = -90.0;
    double minAltitude = 90.0;
    double maxAltitudeLstHours = -1.0; // sidereal time of the meridian transit
};

inline VisibilitySummary surveyVisibility(double raHours, double decDeg,
                                          double latitudeDeg, int sampleCount = 24 * 6)
{
    VisibilitySummary summary;
    for (int i = 0; i < sampleCount; ++i) {
        const double lst = normalizeHours(i * 24.0 / sampleCount);
        const Horizontal h = equatorialToHorizontal(raHours, decDeg, lst, latitudeDeg);
        if (h.altitude > summary.maxAltitude) {
            summary.maxAltitude = h.altitude;
            summary.maxAltitudeLstHours = lst;
        }
        if (h.altitude < summary.minAltitude) summary.minAltitude = h.altitude;
    }
    summary.alwaysUp = summary.minAltitude > 0.0;
    summary.neverUp = summary.maxAltitude < 0.0;
    return summary;
}

// Low-precision solar position (Meeus ch. 25): good to ~0.01°, ample for
// twilight windows and night-boundary planning.
struct Equatorial {
    double raHours = 0.0;
    double decDeg = 0.0;
};

inline Equatorial sunPosition(double jd)
{
    const double t = (jd - 2451545.0) / 36525.0;
    const double meanLongitude = normalizeDegrees(280.46646 + 36000.76983 * t + 0.0003032 * t * t);
    const double meanAnomaly = 357.52911 + 35999.05029 * t - 0.0001537 * t * t;
    const double mRad = degToRad(meanAnomaly);
    const double equationOfCenter =
        (1.914602 - 0.004817 * t - 0.000014 * t * t) * std::sin(mRad)
        + (0.019993 - 0.000101 * t) * std::sin(2.0 * mRad)
        + 0.000289 * std::sin(3.0 * mRad);
    const double trueLongitude = meanLongitude + equationOfCenter;
    const double omega = 125.04 - 1934.136 * t;
    const double apparentLongitude = trueLongitude - 0.00569 - 0.00478 * std::sin(degToRad(omega));
    const double epsilon0 = 23.4392911 - (46.8150 * t + 0.00059 * t * t - 0.001813 * t * t * t) / 3600.0;
    const double epsilon = epsilon0 + 0.00256 * std::cos(degToRad(omega));
    const double lambdaRad = degToRad(apparentLongitude);
    const double epsilonRad = degToRad(epsilon);
    Equatorial sun;
    sun.raHours = normalizeHours(radToDeg(std::atan2(std::cos(epsilonRad) * std::sin(lambdaRad),
                                                     std::cos(lambdaRad))) / 15.0);
    sun.decDeg = radToDeg(std::asin(std::sin(epsilonRad) * std::sin(lambdaRad)));
    return sun;
}

// Altitude of the sun at a site and moment (degrees).
inline double sunAltitude(double jd, double longitudeEastDeg, double latitudeDeg)
{
    const Equatorial sun = sunPosition(jd);
    const double lst = localSiderealTimeHours(jd, longitudeEastDeg);
    return equatorialToHorizontal(sun.raHours, sun.decDeg, lst, latitudeDeg).altitude;
}

} // namespace Astro
