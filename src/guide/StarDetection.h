#pragma once

// Star detection math ported from PHD2 (Open PHD Guiding).
// Copyright (c) 2013-2019 Open PHD Guiding development team; original star
// detection copyright (c) 2006-2010 Craig Stark, (c) 2012 Bret McKee.
// BSD 3-clause — see LICENSE.txt. Original: src/star.cpp Star::Find.
// This revision is a line-faithful port of the PHD2 arithmetic verified
// against the original source: threshold-filtered centroid
// (mean + 3 sigma), the smoothed-peak false-star guard, sample variance
// (n-1) background, and half-flux radius measured around the sub-pixel
// CENTROID with PHD2's radius-space interpolation.

#include <QtGlobal>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <vector>

namespace GuideStar {

struct Detection {
    enum Result {
        NotFound,      // no usable peak in the search region
        NearEdge,      // peak too close to the frame border
        LowMass,       // integrated signal below the floor
        LowSnr,        // signal-to-noise below the floor
        Saturated,     // peak at or beyond the sensor ceiling
        HfdOutOfRange, // half-flux diameter outside the accepted range
        Ok
    };
    Result result = NotFound;
    double x = 0.0;      // centroid (frame pixels)
    double y = 0.0;
    double mass = 0.0;   // background-subtracted summed signal
    double snr = 0.0;
    double hfd = 0.0;    // half-flux DIAMETER, pixels
    double peak = 0.0;   // raw peak value (ADU)
};

struct Params {
    int searchRegion = 15;    // +/- pixels around the last position
    int apertureRadius = 7;   // centroid aperture (also the annulus inner
                              // radius, as PHD2 shares one constant A = 7)
    int bgOuter = 12;         // background annulus outer radius
    double minSnr = 3.0;      // PHD2's LOW_SNR; selection callers raise it
    double minMass = 10.0;
    double minHfd = 1.5;
    // PHD2 defaults to 10; pure-speckle frames measure HFD ~= 9.9 (half the
    // aperture diameter), so 8.0 is required to reject empty frames on a
    // single-frame basis in phase one (PHD2 leans on multi-frame mass checks).
    double maxHfd = 8.0;
    int saturationAdu = -1;   // -1: unknown, use the flat-top heuristic
    int bitDepth = 16;        // for the flat-top heuristic
};

namespace Detail {

// PHD2's half-flux radius, verbatim structure (star.cpp hfr): radii are
// recomputed relative to the SUB-PIXEL centroid, samples sorted by r^2, the
// cumulative mass walk finds the half-mass bracket, and interpolation is
// linear in radius (sqrt space) between the bracketing samples.
struct RadiusMass {
    int x = 0;
    int y = 0;
    double m = 0.0;
    double r2 = 0.0;
    bool operator<(const RadiusMass& rhs) const { return r2 < rhs.r2; }
};

inline double hfr(std::vector<RadiusMass>& samples, double cx, double cy, double mass)
{
    if (samples.size() == 1) // hot pixel?
        return 0.25;
    for (auto& sample : samples) {
        const double dx = double(sample.x) - cx;
        const double dy = double(sample.y) - cy;
        sample.r2 = dx * dx + dy * dy;
    }
    std::sort(samples.begin(), samples.end()); // ascending radius^2

    double r20 = 0.0, r21 = 0.0, m0 = 0.0, m1 = 0.0;
    const double halfm = 0.5 * mass;
    for (const auto& sample : samples) {
        r20 = r21;
        m0 = m1;
        r21 = sample.r2;
        m1 += sample.m;
        if (m1 > halfm)
            break;
    }
    if (m1 > m0) {
        const double r0 = std::sqrt(r20);
        const double r1 = std::sqrt(r21);
        const double slope = (r1 - r0) / (m1 - m0);
        return r0 + slope * (halfm - m0);
    }
    return 0.25;
}

} // namespace Detail

// Finds and measures the star near (startX, startY). The image is a 16-bit
// grey frame in row-major order; pedestal must be pre-subtracted by the
// caller. No synthetic pedestal is added by GuideProcessor; saturationAdu
// must use the same pedestal-free ADU scale as the input.
inline Detection find(const QVector<quint16>& image, int width, int height,
                      double startX, double startY, const Params& params = {})
{
    Detection result;
    if (image.size() != width * height || width < 8 || height < 8) return result;

    // Search region bounds as PHD2 computes them (min/max against the frame
    // edges), then the smoothed scan runs INSET BY ONE on every side.
    const int startBaseX = std::max(int(startX) - params.searchRegion, 0);
    const int endBaseX = std::min(int(startX) + params.searchRegion, width - 1);
    const int startBaseY = std::max(int(startY) - params.searchRegion, 0);
    const int endBaseY = std::min(int(startY) + params.searchRegion, height - 1);
    if (endBaseX <= startBaseX || endBaseY <= startBaseY) {
        result.result = Detection::NearEdge;
        return result;
    }

    // 1. Smoothed peak search, PHD2 verbatim: UNSIGNED INTEGER kernel sum
    //    (4*c + 2*n/s/e/w + diag) with integer division by 16 — the
    //    truncation matters for the false-star guard's boundary decision.
    //    max3 keeps the three brightest raw values via PHD2's swap chain.
    quint32 peakVal = 0;
    int peakX = -1, peakY = -1;
    quint16 max3[3] = {0, 0, 0};
    for (int y = startBaseY + 1; y <= endBaseY - 1; ++y) {
        const quint16* row = image.data() + size_t(y) * width;
        for (int x = startBaseX + 1; x <= endBaseX - 1; ++x) {
            quint16 p = row[x];
            const quint32 val = 4u * quint32(p)
                + row[x - width - 1] + row[x - width + 1] + row[x + width - 1] + row[x + width + 1]
                + 2u * row[x - width] + 2u * row[x - 1] + 2u * row[x + 1] + 2u * row[x + width];
            if (val > peakVal) { peakVal = val; peakX = x; peakY = y; }
            if (p > max3[0]) std::swap(p, max3[0]);
            if (p > max3[1]) std::swap(p, max3[1]);
            if (p > max3[2]) std::swap(p, max3[2]);
        }
    }
    if (peakX < 0) return result;
    result.peak = max3[0];
    peakVal /= 16u; // integer truncation, as PHD2's unsigned peak_val

    // 3. Peak must sit clear of the border for the annulus and aperture.
    const int margin = std::max(params.bgOuter + 1, params.apertureRadius + 1);
    if (peakX < margin || peakY < margin || peakX >= width - margin || peakY >= height - margin) {
        result.result = Detection::NearEdge;
        return result;
    }

    // 4. Background noise in the annulus (inner = aperture radius A, outer
    //    = B), up to 9 rounds of full recompute with 2-sigma clipping from
    //    round two on, sample variance (n-1) — PHD2 verbatim.
    const int A = params.apertureRadius;
    const int A2 = A * A;
    const int B = params.bgOuter;
    const int B2 = B * B;
    double meanBg = 0.0, sigma2Bg = 0.0, sigmaBg = 0.0;
    unsigned nbg = 0;
    for (int iter = 0; iter < 9; ++iter) {
        double sum = 0.0, a = 0.0, q = 0.0;
        nbg = 0;
        for (int dy = -B; dy <= B; ++dy) {
            for (int dx = -B; dx <= B; ++dx) {
                const int r2 = dx * dx + dy * dy;
                if (r2 <= A2 || r2 > B2) continue;
                const double val = double(image[size_t(peakY + dy) * width + peakX + dx]);
                if (iter > 0 && (val < meanBg - 2.0 * sigmaBg || val > meanBg + 2.0 * sigmaBg))
                    continue;
                sum += val;
                ++nbg;
                const double k = double(nbg);
                const double a0 = a;
                a += (val - a) / k;
                q += (val - a0) * (val - a);
            }
        }
        if (nbg < 10) break;
        const double prevMeanBg = meanBg;
        meanBg = sum / double(nbg);
        sigma2Bg = q / double(nbg - 1);
        sigmaBg = std::sqrt(sigma2Bg);
        if (iter > 0 && std::abs(meanBg - prevMeanBg) < 0.5) break;
    }

    // 5. Threshold-filtered centroid: only pixels at or above
    //    mean + 3 sigma contribute (PHD2's false-star resistant form).
    const int thresh = int(meanBg + 3.0 * sigmaBg + 0.5);
    double cx = 0.0, cy = 0.0, mass = 0.0;
    unsigned n = 0;
    std::vector<Detail::RadiusMass> hfrSamples;
    for (int dy = -A; dy <= A; ++dy) {
        const int dy2 = dy * dy;
        if (dy2 > A2) continue;
        for (int dx = -A; dx <= A; ++dx) {
            if (dx * dx + dy2 > A2) continue;
            const int x = peakX + dx;
            const int y = peakY + dy;
            const double val = double(image[size_t(y) * width + x]);
            if (val < thresh) continue; // PHD2: exclude points below threshold
            const double d = val - meanBg;
            cx += dx * d;
            cy += dy * d;
            mass += d;
            ++n;
            hfrSamples.push_back({x, y, d, 0.0});
        }
    }
    result.mass = mass;

    // 6. SNR (Simonetti 2004 via PHD2; nominal 0.5 e-/ADU gain).
    const double gain = 0.5;
    result.snr = n > 0
        ? mass / std::sqrt(mass / gain + sigma2Bg * double(n) * (1.0 + 1.0 / double(nbg)))
        : 0.0;

    // 7. PHD2 false-star guard: a few scattered pixels over threshold can
    //    fake a high SNR; require the (truncated) smoothed peak to clear it.
    if (peakVal <= thresh && result.snr >= 3.0)
        result.snr = 2.9;

    if (mass < params.minMass) { result.result = Detection::LowMass; return result; }
    if (result.snr < params.minSnr) { result.result = Detection::LowSnr; return result; }

    result.x = peakX + cx / mass;
    result.y = peakY + cy / mass;
    result.hfd = 2.0 * Detail::hfr(hfrSamples, result.x, result.y, mass);

    if (result.hfd < params.minHfd || result.hfd > params.maxHfd) {
        result.result = Detection::HfdOutOfRange;
        return result;
    }
    result.result = Detection::Ok;
    // PHD2 checks saturation after mass/SNR/HFD, preserving measurements.
    const quint32 mx = max3[0];
    const quint32 difference = max3[0] - max3[2];
    const bool saturated = params.saturationAdu > 0
        ? mx >= quint32(params.saturationAdu)
        : (params.bitDepth < 12 ? difference * 191U < mx
                                : difference * 65535U < 32U * mx);
    if (saturated) result.result = Detection::Saturated;
    return result;
}

// Phase-one auto selection: coarse-grid candidates sorted by brightness,
// measured until one passes the gates. Candidates too close to a saturated
// peak are skipped so a clipped giant cannot shadow the clean star next to
// it. (PHD2's AutoFind uses a PSF-matched convolution; reduced first stage.)
inline Detection autoFind(const QVector<quint16>& image, int width, int height,
                          const Params& params = {})
{
    if (image.size() != width * height) return {};
    const int margin = std::max(params.bgOuter + 1, params.apertureRadius + 1) + 2;
    struct Candidate { int x; int y; double value; };
    std::vector<Candidate> candidates;
    for (int y = margin; y < height - margin; y += 4)
        for (int x = margin; x < width - margin; x += 4)
            candidates.push_back({x, y, double(image[size_t(y) * width + x])});
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.value > b.value; });
    if (candidates.empty()) return {};
    int failedX = -1000, failedY = -1000;
    for (const Candidate& candidate : candidates) {
        if (std::hypot(candidate.x - failedX, candidate.y - failedY) < 12.0) continue;
        Detection detection = find(image, width, height, candidate.x, candidate.y, params);
        if (detection.result == Detection::Ok) return detection;
        if (detection.result == Detection::Saturated && failedX < -999) {
            // Remember the saturated peak; later candidates near it measure
            // the same clipped star, not a different one.
            failedX = candidate.x;
            failedY = candidate.y;
        }
    }
    return {};
}

} // namespace GuideStar
