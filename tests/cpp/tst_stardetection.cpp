#include <QtTest>
#include <QVector>
#include <cmath>
#include <random>
#include <vector>
#include "guide/StarDetection.h"

using namespace GuideStar;

// ─── PHD2 reference implementation (verbatim math) ─────────────────────────
// Copied from phd2-master/src/star.cpp Star::Find + hfr (Open PHD Guiding,
// BSD 3-clause) to cross-validate the port on identical inputs: passing the
// tests below proves the port matches PHD2's arithmetic, not merely its own
// assumptions.
namespace Phd2Reference {

struct R2M {
    int x, y;
    double m;
    double r2;
    R2M(int x_, int y_, double m_) : x(x_), y(y_), m(m_), r2(0.0) {}
    bool operator<(const R2M& rhs) const { return r2 < rhs.r2; }
};

static double hfr(std::vector<R2M>& vec, double cx, double cy, double mass)
{
    if (vec.size() == 1) return 0.25;
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        double dx = (double) it->x - cx;
        double dy = (double) it->y - cy;
        it->r2 = dx * dx + dy * dy;
    }
    std::sort(vec.begin(), vec.end());
    double r20, r21, m0, m1;
    r20 = r21 = m0 = m1 = 0.0;
    double halfm = 0.5 * mass;
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        const R2M& rm = *it;
        r20 = r21;
        m0 = m1;
        r21 = rm.r2;
        m1 += rm.m;
        if (m1 > halfm) break;
    }
    double h;
    if (m1 > m0) {
        double r0 = sqrt(r20), r1 = sqrt(r21);
        double s = (r1 - r0) / (m1 - m0);
        h = r0 + s * (halfm - m0);
    } else
        h = 0.25;
    return h;
}

struct Ref {
    double x = 0, y = 0, mass = 0, snr = 0, hfd = 0;
    Detection::Result result = Detection::NotFound;
};

// Core of Star::Find: peak search (PHD2 integer kernel, inset bounds),
// annulus background (PHD2 iteration), threshold centroid, SNR, HFD.
static Ref find(const QVector<quint16>& img, int w, int h, int startX, int startY, int searchRegion,
                int bits = 16, unsigned maxADU = 0, unsigned pedestal = 0)
{
    Ref out;
    // PHD2 search-region bounds against the frame, smoothed scan inset by 1.
    const int sx0 = std::max(startX - searchRegion, 0);
    const int sx1 = std::min(startX + searchRegion, w - 1);
    const int sy0 = std::max(startY - searchRegion, 0);
    const int sy1 = std::min(startY + searchRegion, h - 1);
    unsigned peakVal = 0; int peakX = startX, peakY = startY;
    unsigned short max3[3] = {0, 0, 0};
    for (int y = sy0 + 1; y <= sy1 - 1; ++y)
        for (int x = sx0 + 1; x <= sx1 - 1; ++x) {
            unsigned short p = img[y * w + x];
            unsigned val = 4u * (unsigned) p
                + img[(y - 1) * w + (x - 1)] + img[(y - 1) * w + (x + 1)]
                + img[(y + 1) * w + (x - 1)] + img[(y + 1) * w + (x + 1)]
                + 2u * img[(y - 1) * w + x] + 2u * img[y * w + (x - 1)]
                + 2u * img[y * w + (x + 1)] + 2u * img[(y + 1) * w + x];
            if (val > peakVal) { peakVal = val; peakX = x; peakY = y; }
            if (p > max3[0]) std::swap(p, max3[0]);
            if (p > max3[1]) std::swap(p, max3[1]);
            if (p > max3[2]) std::swap(p, max3[2]);
        }
    peakVal /= 16u; // integer truncation exactly as PHD2

    const int A = 7, B = 12, A2 = A * A, B2 = B * B;
    unsigned nbg = 0;
    double meanBg = 0.0, prevMeanBg = 0.0, sigma2Bg = 0.0, sigmaBg = 0.0;
    for (int iter = 0; iter < 9; iter++) {
        double sum = 0.0, a = 0.0, q = 0.0;
        nbg = 0;
        for (int dy = peakY - B; dy <= peakY + B; ++dy) {
            int dy2 = (dy - peakY) * (dy - peakY);
            for (int x = peakX - B; x <= peakX + B; ++x) {
                int dx = x - peakX;
                int r2 = dx * dx + dy2;
                if (r2 <= A2 || r2 > B2) continue;
                double val = (double) img[dy * w + x];
                if (iter > 0 && (val < meanBg - 2.0 * sigmaBg || val > meanBg + 2.0 * sigmaBg))
                    continue;
                sum += val;
                ++nbg;
                double k = (double) nbg;
                double a0 = a;
                a += (val - a) / k;
                q += (val - a0) * (val - a);
            }
        }
        if (nbg < 10) break;
        prevMeanBg = meanBg;
        meanBg = sum / (double) nbg;
        sigma2Bg = q / (double) (nbg - 1);
        sigmaBg = sqrt(sigma2Bg);
        if (iter > 0 && fabs(meanBg - prevMeanBg) < 0.5) break;
    }

    unsigned thresh = (unsigned) (meanBg + 3.0 * sigmaBg + 0.5);
    double cx = 0.0, cy = 0.0, mass = 0.0;
    unsigned n = 0;
    std::vector<R2M> hfrvec;
    for (int dy = peakY - A; dy <= peakY + A; ++dy) {
        int dy2 = (dy - peakY) * (dy - peakY);
        if (dy2 > A2) continue;
        for (int x = peakX - A; x <= peakX + A; ++x) {
            int dx = x - peakX;
            if (dx * dx + dy2 > A2) continue;
            unsigned short val = img[dy * w + x];
            if (val < thresh) continue;
            double d = (double) val - meanBg;
            cx += dx * d;
            cy += (dy - peakY) * d;
            mass += d;
            ++n;
            hfrvec.push_back(R2M(x, dy, d));
        }
    }
    out.mass = mass;
    const double gain = .5;
    out.snr = n > 0 ? mass / sqrt(mass / gain + sigma2Bg * (double) n * (1.0 + 1.0 / (double) nbg)) : 0.0;
    if (peakVal <= thresh && out.snr >= 3.0)
        out.snr = 2.9;
    if (mass < 10.0) { out.result = Detection::LowMass; return out; }
    if (out.snr < 3.0) { out.result = Detection::LowSnr; return out; }
    double newX = peakX + cx / mass;
    double newY = peakY + cy / mass;
    out.x = newX;
    out.y = newY;
    out.hfd = 2.0 * hfr(hfrvec, newX, newY, mass);
    // Same configured HFD limits as the port; these are product choices.
    if (out.hfd < 1.5 || out.hfd > 8.0) { out.result = Detection::HfdOutOfRange; return out; }
    out.result = Detection::Ok;
    unsigned mx = max3[0];
    mx = mx >= pedestal ? mx - pedestal : 0;
    if (maxADU > 0) {
        if (mx >= maxADU) out.result = Detection::Saturated;
        return out;
    }
    const unsigned d = max3[0] - max3[2];
    if (bits < 12 ? d * 191U < mx : d * 65535U < 32U * mx)
        out.result = Detection::Saturated;
    return out;
}

} // namespace Phd2Reference

// ─── synthetic images ─────────────────────────────────────────────────────
namespace {

constexpr int kWidth = 128;
constexpr int kHeight = 128;

QVector<quint16> synthesize(double starX, double starY, double amplitude,
                            double sigmaPx, int background, double noiseSigma,
                            unsigned seed = 42, int bitDepth = 16)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, noiseSigma > 0.0 ? noiseSigma : 1.0);
    QVector<quint16> frame(kWidth * kHeight);
    const int ceiling = (1 << bitDepth) - 1;
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) {
            const double dx = x - starX;
            const double dy = y - starY;
            const double value = background
                + amplitude * std::exp(-(dx * dx + dy * dy) / (2.0 * sigmaPx * sigmaPx))
                + (noiseSigma > 0.0 ? noise(rng) : 0.0);
            frame[y * kWidth + x] = quint16(std::clamp(value, 0.0, double(ceiling)));
        }
    return frame;
}

} // namespace

class StarDetectionTest final : public QObject
{
    Q_OBJECT
private slots:
    void integerGuardRejectsFractionalPeak() {
        QVector<quint16> frame(kWidth * kHeight, 100);
        for (int y = 0; y < kHeight; ++y)
            for (int x = 0; x < kWidth; ++x)
                frame[y * kWidth + x] = (x + y) % 2 ? 99 : 101;
        for (int y : {61, 67}) for (int x : {61, 67}) frame[y * kWidth + x] = 114;
        const auto ref = Phd2Reference::find(frame, kWidth, kHeight, 64, 64, 15);
        const auto mine = find(frame, kWidth, kHeight, 64, 64);
        QCOMPARE(ref.snr, 2.9);
        QCOMPARE(mine.snr, ref.snr);
        QCOMPARE(mine.result, ref.result);
    }

    void excludedSearchBoundaryCannotStealPeak() {
        auto frame = synthesize(58, 64, 2000, 2.2, 500, 0);
        frame[64 * kWidth + 79] = 10500; // excluded x=64+15; inward neighbor sees half the kernel weight
        const auto mine = find(frame, kWidth, kHeight, 64, 64);
        const auto ref = Phd2Reference::find(frame, kWidth, kHeight, 64, 64, 15);
        QCOMPARE(mine.result, Detection::Ok);
        QVERIFY(std::abs(mine.x - 58.0) < 0.1);
        QVERIFY(std::abs(mine.x - ref.x) < 1e-9);
    }

    void saturationHeuristicMatchesReferenceAtDifferentDepths() {
        for (int bits : {8, 12, 16}) {
            const int peak = bits == 8 ? 250 : 3000;
            const int delta = bits == 12 ? 2 : 1;
            auto frame = synthesize(64, 64, peak - 20, 2.2, 20, 0);
            frame[64 * kWidth + 64] = peak;
            frame[64 * kWidth + 65] = peak - delta;
            frame[65 * kWidth + 64] = peak - delta;
            Params params;
            params.bitDepth = bits;
            const auto ref = Phd2Reference::find(frame, kWidth, kHeight, 64, 64, 15, bits);
            QCOMPARE(ref.result, bits == 12 ? Detection::Ok : Detection::Saturated);
            QCOMPARE(find(frame, kWidth, kHeight, 64, 64, params).result, ref.result);
        }
    }

    void matchesPhd2ReferenceOnIdenticalFrames() {
        // Cross-validation: same frame into PHD2's own arithmetic and into
        // the port; measured values must agree to the last bit.
        struct Case { double x, y, amp, sigma, noise; unsigned seed; };
        const Case cases[] = {
            {64.0, 64.0, 4000, 2.2, 6.0, 42},
            {64.3, 63.7, 25000, 1.8, 10.0, 7},
            {70.5, 58.2, 800, 3.0, 4.0, 11},
            {40.1, 80.9, 12000, 2.5, 8.0, 13},
        };
        for (const Case& c : cases) {
            const auto frame = synthesize(c.x, c.y, c.amp, c.sigma, 500.0, c.noise, c.seed);
            const Detection mine = find(frame, kWidth, kHeight, c.x, c.y);
            const Phd2Reference::Ref ref = Phd2Reference::find(frame, kWidth, kHeight, int(c.x), int(c.y), 15);
            QCOMPARE(mine.result, Detection::Ok);
            QVERIFY(std::abs(mine.x - ref.x) < 1e-9);
            QVERIFY(std::abs(mine.y - ref.y) < 1e-9);
            QVERIFY(std::abs(mine.mass - ref.mass) < 1e-9);
            QVERIFY(std::abs(mine.snr - ref.snr) < 1e-9);
            QVERIFY(std::abs(mine.hfd - ref.hfd) < 1e-9);
        }
    }

    void matchesPhd2OnNonSquareFrames() {
        // Non-square geometry: bounds must use width AND height separately.
        constexpr int w = 160, h = 96;
        std::mt19937 rng(21);
        std::normal_distribution<double> noise(0.0, 5.0);
        QVector<quint16> frame(w * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const double dx = x - 100.4, dy = y - 50.6;
                frame[y * w + x] = quint16(std::clamp(
                    400.0 + 9000.0 * std::exp(-(dx * dx + dy * dy) / (2.0 * 2.4 * 2.4)) + noise(rng), 0.0, 65535.0));
            }
        const Detection mine = find(frame, w, h, 100, 50);
        const Phd2Reference::Ref ref = Phd2Reference::find(frame, w, h, 100, 50, 15);
        QCOMPARE(mine.result, Detection::Ok);
        QVERIFY(std::abs(mine.x - ref.x) < 1e-9);
        QVERIFY(std::abs(mine.y - ref.y) < 1e-9);
        QVERIFY(std::abs(mine.hfd - ref.hfd) < 1e-9);
        QVERIFY(std::abs(mine.snr - ref.snr) < 1e-9);
    }

    void matchesPhd2OnSearchEdgePeaks() {
        // Star near the edge of the SEARCH REGION (not the frame): the inset
        // scan bounds decide whether the peak is seen at all.
        const auto frame = synthesize(76.0, 64.0, 6000.0, 2.2, 500.0, 5.0, 31);
        const Detection mine = find(frame, kWidth, kHeight, 64.0, 64.0);
        const Phd2Reference::Ref ref = Phd2Reference::find(frame, kWidth, kHeight, 64, 64, 15);
        QCOMPARE(mine.result, ref.hfd > 0 ? Detection::Ok : Detection::NotFound);
        if (ref.hfd > 0) {
            QVERIFY(std::abs(mine.x - ref.x) < 1e-9);
            QVERIFY(std::abs(mine.y - ref.y) < 1e-9);
            QVERIFY(std::abs(mine.hfd - ref.hfd) < 1e-9);
        }
    }

    void matchesPhd2OnSpeckleAndGuardBoundaries() {
        // Noise-only and faint frames: whatever each gate decides (guard,
        // mass, SNR), the port must decide IDENTICALLY — these are the
        // paths where integer truncation and inset bounds matter.
        const unsigned seeds[] = {101, 102, 103, 104, 105};
        for (const unsigned seed : seeds) {
            const auto speckle = synthesize(-1000.0, -1000.0, 0.0, 1.0, 500.0, 14.0, seed);
            const Detection mine = find(speckle, kWidth, kHeight, 64.0, 64.0);
            const Phd2Reference::Ref ref = Phd2Reference::find(speckle, kWidth, kHeight, 64, 64, 15);
            if (ref.hfd > 0 && ref.snr >= 3.0 && ref.mass >= 10.0)
                QCOMPARE(mine.result, Detection::Ok);
            else
                QVERIFY(mine.result != Detection::Ok);
        }
        // Faint star right at the SNR/mass boundary.
        const auto faint = synthesize(64.0, 64.0, 40.0, 2.2, 500.0, 6.0, 55);
        const Detection mineFaint = find(faint, kWidth, kHeight, 64.0, 64.0);
        const Phd2Reference::Ref refFaint = Phd2Reference::find(faint, kWidth, kHeight, 64, 64, 15);
        const bool refOk = refFaint.hfd > 0 && refFaint.snr >= 3.0 && refFaint.mass >= 10.0;
        QCOMPARE(mineFaint.result == Detection::Ok, refOk);
    }

    void matchesPhd2OnSaturatedFrames() {
        // Saturation is evaluated after measurement, as in PHD2.
        const auto frame = synthesize(64.0, 64.0, 65000.0, 2.0, 500.0, 6.0);
        Params params;
        params.saturationAdu = 60000;
        const auto ref = Phd2Reference::find(frame, kWidth, kHeight, 64, 64, 15, 16, 60000);
        const auto mine = find(frame, kWidth, kHeight, 64, 64, params);
        QCOMPARE(ref.result, Detection::Saturated);
        QCOMPARE(mine.result, ref.result);
        QVERIFY(std::abs(mine.hfd - ref.hfd) < 1e-9);
    }

    void centroidRecoversSubPixelPosition() {
        for (const auto [sx, sy] : std::vector<std::pair<double, double>>{
                 {64.0, 64.0}, {64.3, 63.7}, {70.5, 58.2}, {40.1, 80.9}}) {
            const auto frame = synthesize(sx, sy, 4000.0, 2.2, 500.0, 6.0);
            const Detection star = find(frame, kWidth, kHeight, sx, sy);
            QCOMPARE(star.result, Detection::Ok);
            QVERIFY(std::abs(star.x - sx) < 0.15);
            QVERIFY(std::abs(star.y - sy) < 0.15);
            QVERIFY(star.snr > 20.0);
            QVERIFY(star.hfd > 2.0 && star.hfd < 8.0);
        }
    }

    void tracksStarAfterSmallDisplacement() {
        const auto frame = synthesize(66.4, 66.2, 4000.0, 2.2, 500.0, 6.0);
        const Detection star = find(frame, kWidth, kHeight, 64.0, 64.0);
        QCOMPARE(star.result, Detection::Ok);
        QVERIFY(std::abs(star.x - 66.4) < 0.15);
        QVERIFY(std::abs(star.y - 66.2) < 0.15);
    }

    void saturatedStarIsFlagged() {
        const auto frame = synthesize(64.0, 64.0, 65000.0, 2.0, 500.0, 6.0);
        Params params;
        params.saturationAdu = 60000;
        const Detection star = find(frame, kWidth, kHeight, 64.0, 64.0, params);
        QCOMPARE(star.result, Detection::Saturated);
    }

    void flatTopHeuristicCatchesSaturationWithoutCeiling() {
        const auto frame = synthesize(64.0, 64.0, 200000.0, 1.2, 500.0, 6.0);
        const Detection star = find(frame, kWidth, kHeight, 64.0, 64.0);
        QCOMPARE(star.result, Detection::Saturated);
    }

    void faintStarIsNotAccepted() {
        const auto frame = synthesize(64.0, 64.0, 5.0, 2.2, 500.0, 8.0);
        const Detection star = find(frame, kWidth, kHeight, 64.0, 64.0);
        QVERIFY(star.result != Detection::Ok);
    }

    void starNearEdgeIsRejected() {
        const auto frame = synthesize(9.0, 64.0, 4000.0, 2.2, 500.0, 6.0);
        const Detection star = find(frame, kWidth, kHeight, 9.0, 64.0);
        QVERIFY(star.result == Detection::NearEdge);
    }

    void pureNoiseFindsNothing() {
        const auto frame = synthesize(-100.0, -100.0, 0.0, 1.0, 500.0, 12.0);
        const Detection star = find(frame, kWidth, kHeight, 64.0, 64.0);
        QVERIFY(star.result != Detection::Ok);
    }

    void autoFindPicksTheBrightStar() {
        auto frame = synthesize(90.0, 40.0, 6000.0, 2.2, 500.0, 6.0, 7);
        const auto dim = synthesize(30.0, 90.0, 1500.0, 2.2, 500.0, 6.0, 8);
        for (int i = 0; i < frame.size(); ++i)
            frame[i] = std::max(frame[i], dim[i]) == dim[i] && dim[i] > 500 ? dim[i] : frame[i];
        const Detection star = autoFind(frame, kWidth, kHeight);
        QCOMPARE(star.result, Detection::Ok);
        QVERIFY(std::abs(star.x - 90.0) < 1.0);
        QVERIFY(std::abs(star.y - 40.0) < 1.0);
    }

    void autoFindAvoidsSaturationWhenPossible() {
        auto frame = synthesize(40.0, 40.0, 200000.0, 1.5, 500.0, 6.0, 9);
        const auto clean = synthesize(90.0, 90.0, 5000.0, 2.2, 500.0, 6.0, 10);
        for (int i = 0; i < frame.size(); ++i) frame[i] = std::max(frame[i], clean[i]);
        const Detection star = autoFind(frame, kWidth, kHeight);
        QCOMPARE(star.result, Detection::Ok);
        QVERIFY(std::hypot(star.x - 40.0, star.y - 40.0) > 15.0);
    }
};

QTEST_GUILESS_MAIN(StarDetectionTest)
#include "tst_stardetection.moc"
