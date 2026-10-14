#pragma once

// Calibration math ported from PHD2 (Open PHD Guiding).
// Copyright (c) 2013-2019 Open PHD Guiding development team. BSD 3-clause.
// Originals: src/mount.cpp TransformCameraCoordinatesToMountCoordinates /
// TransformMountCoordinatesToCameraCoordinates / AdjustCalibrationForScope-
// Pointing and src/calstep_dialog.cpp step math. Auto-calibration walking
// (GO_WEST..NUDGE_SOUTH) requires mount pulses and joins in phase three;
// phase two ships the transforms, rates and manual calibration.

#include <cmath>
#include <utility>

namespace GuideCal {

inline double normAngle(double radians)
{
    double value = std::fmod(radians, 2.0 * M_PI);
    if (value <= -M_PI / 2.0) value += 2.0 * M_PI;
    if (value > 3.0 * M_PI / 2.0) value -= 2.0 * M_PI;
    return value;
}

struct Calibration {
    double xRate = 0.0;      // px per ms along the RA axis
    double yRate = 0.0;      // px per ms along the Dec axis
    double xAngle = 0.0;     // camera-x vs RA-axis angle, radians
    double yAngle = M_PI / 2.0;
    double declinationDeg = 0.0;
    bool valid = false;

    // Camera axes are orthogonal when yAngle == xAngle + PI/2; the error
    // term drives the transform exactly as in PHD2's SetCalibration.
    double yAngleError() const { return normAngle(xAngle - yAngle + M_PI / 2.0); }
};

// Manual calibration from the rig parameters: image scale (arcsec/px),
// mount guide rate (fraction of sidereal) and pointing declination. Camera
// angle 0 means the sensor x-axis aligns with RA.
inline Calibration manual(double imageScaleArcsecPx, double guideRateSidereal,
                          double declinationDeg, double cameraAngleDeg = 0.0)
{
    Calibration cal;
    // Guard rails for every caller: finite values, sane declination (the
    // 1/cos(deg) rate math blows up at the poles) and non-zero rates.
    if (!std::isfinite(imageScaleArcsecPx) || !std::isfinite(guideRateSidereal)
        || !std::isfinite(declinationDeg) || !std::isfinite(cameraAngleDeg))
        return cal;
    if (imageScaleArcsecPx <= 0.0 || guideRateSidereal <= 0.0) return cal;
    if (std::abs(declinationDeg) >= 89.0) return cal;
    if (std::cos(declinationDeg * M_PI / 180.0) < 0.017) return cal; // |dec| ~ > 89 deg
    // Sidereal 15"/s. A fixed RA-axis rate covers less SKY at higher dec
    // (cos), so the camera sees FEWER px per ms: xRate = base * cos(dec).
    // Pulses therefore lengthen as 1/cos(dec) away from the equator, the
    // PHD2 calstep behavior.
    const double arcsecPerMs = guideRateSidereal * 15.0 / 1000.0;
    cal.xRate = arcsecPerMs / imageScaleArcsecPx * std::cos(declinationDeg * M_PI / 180.0);
    cal.yRate = arcsecPerMs / imageScaleArcsecPx;
    cal.xAngle = cameraAngleDeg * M_PI / 180.0;
    cal.yAngle = cal.xAngle + M_PI / 2.0;
    cal.declinationDeg = declinationDeg;
    // Near-zero rates would ask for minutes-long pulses; refuse instead.
    cal.valid = cal.xRate > 1e-6 && cal.yRate > 1e-6;
    return cal;
}

// Camera pixel offset -> mount axes offset, both in pixels
// (first = RA, second = Dec), with PHD2's orthogonality handling.
inline std::pair<double, double> cameraToMount(const Calibration& cal, double dx, double dy)
{
    const double hyp = std::hypot(dx, dy);
    if (hyp == 0.0) return {0.0, 0.0};
    const double theta = std::atan2(dy, dx);
    const double xAngle = theta - cal.xAngle;
    const double yAngle = theta - (cal.xAngle + cal.yAngleError());
    return {std::cos(xAngle) * hyp, std::sin(yAngle) * hyp};
}

// Inverse transform (mount -> camera), for tests and displays.
inline std::pair<double, double> mountToCamera(const Calibration& cal, double raPx, double decPx)
{
    const double hyp = std::hypot(raPx, decPx);
    if (hyp == 0.0) return {0.0, 0.0};
    const double theta = std::atan2(decPx, raPx);
    const double xAngle = theta + cal.xAngle;
    const double yAngle = theta + (cal.xAngle + cal.yAngleError());
    return {std::cos(xAngle) * hyp, std::sin(yAngle) * hyp};
}

// PHD2 AdjustCalibrationForScopePointing: RA rate varies with declination
// (King rate); re-scale when guiding starts at a new declination. Returns
// false when the correction is skipped (|cal dec| beyond 85 deg, as PHD2).
inline bool adjustForDeclination(Calibration& cal, double newDeclinationDeg)
{
    if (std::abs(cal.declinationDeg) > 85.0 || std::abs(newDeclinationDeg) > 89.0) return false;
    cal.xRate = (cal.xRate / std::cos(cal.declinationDeg * M_PI / 180.0))
              * std::cos(newDeclinationDeg * M_PI / 180.0);
    cal.declinationDeg = newDeclinationDeg;
    return true;
}

struct Pulse {
    int raMs = 0;      // magnitude in milliseconds
    int decMs = 0;
    bool raWest = false;   // direction flags (PHD2: +RA offset guides West)
    bool decSouth = false; // +Dec offset guides South
};

// Pixel error (mount axes) -> guide pulse durations. Alarms mirror PHD2's
// Scope::MoveAxis clamps: zero rate refuses, durations capped.
inline Pulse pulseFor(const Calibration& cal, double raPx, double decPx,
                      int maxRaMs = 1000, int maxDecMs = 1000)
{
    Pulse pulse;
    if (!cal.valid || (cal.xRate <= 0.0 && cal.yRate <= 0.0)) return pulse;
    auto duration = [](double px, double rate, int cap) {
        if (px == 0.0 || rate <= 0.0) return 0;
        return int(std::min<double>(std::round(std::abs(px) / rate) + 0.5, cap));
    };
    pulse.raMs = duration(raPx, cal.xRate, maxRaMs);
    pulse.decMs = duration(decPx, cal.yRate, maxDecMs);
    pulse.raWest = raPx > 0.0;
    pulse.decSouth = decPx > 0.0;
    return pulse;
}

} // namespace GuideCal
