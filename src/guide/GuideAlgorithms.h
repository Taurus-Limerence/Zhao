#pragma once

// Guide algorithms ported from PHD2 (Open PHD Guiding).
// Copyright (c) 2013-2019 Open PHD Guiding development team; original
// hysteresis algorithm copyright (c) Bret McKee 2012. BSD 3-clause — see
// LICENSE.txt. Originals: src/guide_algorithm_identity.cpp,
// src/guide_algorithm_hysteresis.cpp, src/guide_algorithm.cpp.
// Only the pure result() math was ported; dialogs and config are native.

#include <cmath>
#include <algorithm>

namespace GuideAlgo {

// PHD2 GuideAlgorithm::SmartDefaultMinMove: pixel deadband for an arcsec/px scale.
inline double smartDefaultMinMove(double imageScaleArcsecPx)
{
    return std::max(0.1515 + 0.1548 / imageScaleArcsecPx, 0.15);
}

// Identity: passthrough.
class Identity
{
public:
    void reset() {}
    double result(double input) { return input; }
};

// Hysteresis (PHD2's default RA algorithm): blends the new measurement with
// the previous output to ride through seeing, scaled by aggression and
// gated by the minMove deadband. Inputs/outputs are axis errors in pixels.
class Hysteresis
{
public:
    void reset() { m_lastMove = 0.0; }
    double result(double input)
    {
        double output = (1.0 - m_hysteresis) * input + m_hysteresis * m_lastMove;
        output *= m_aggression;
        if (std::abs(input) < m_minMove)
            output = 0.0;
        m_lastMove = output;
        return output;
    }

    // PHD2 semantics: the UI percentage maps 1:1 (10% -> 0.10). PHD2 caps
    // hysteresis at 0.99 (MaxHysteresis); we clamp at 0.9 as a deliberate
    // product parameter-range choice, not a guarantee of loop stability.
    void setHysteresisPercent(int percent) { m_hysteresis = std::clamp(percent / 100.0, 0.0, 0.9); }
    void setAggressionPercent(int percent) { m_aggression = std::clamp(percent / 100.0, 0.05, 1.0); }
    void setMinMove(double pixels) { m_minMove = std::max(0.0, pixels); }

    double hysteresis() const { return m_hysteresis; }
    double aggression() const { return m_aggression; }
    double minMove() const { return m_minMove; }

private:
    double m_hysteresis = 0.10; // PHD2 default
    double m_aggression = 0.7;
    double m_minMove = 0.2;
    double m_lastMove = 0.0;
};

} // namespace GuideAlgo
