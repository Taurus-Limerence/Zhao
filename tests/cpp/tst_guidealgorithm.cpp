#include <QtTest>
#include <cmath>
#include <vector>
#include "guide/GuideAlgorithms.h"
#include "guide/GuideCalibration.h"
#include "guide/GuideProcessor.h"

using namespace GuideCal;

class GuideAlgorithmTest final : public QObject
{
    Q_OBJECT
private slots:
    void deadbandRegressionAtTwoImageScales() {
        // Gated passthrough (hysteresis 0%, aggression 100%) makes the
        // deadband the ONLY decision: below it no pulse, above it the raw
        // error converts to milliseconds. Catches the historical double
        // scale conversion (deadband divided by scale once more).
        auto frameAt = [](double offset, unsigned seed) {
            std::mt19937 rng(seed);
            std::normal_distribution<double> noise(0.0, 3.0);
            QVector<quint16> pixels(96 * 96);
            for (int y = 0; y < 96; ++y)
                for (int x = 0; x < 96; ++x) {
                    const double dx = x - (48.0 + offset), dy = y - 48.0;
                    const double value = 500 + 9000 * std::exp(-(dx * dx + dy * dy) / (2.0 * 2.2 * 2.2)) + noise(rng);
                    pixels[y * 96 + x] = quint16(std::clamp(value, 0.0, 65535.0));
                }
            return pixels;
        };
        auto prepare = [&frameAt](GuideProcessor& processor, double scale) {
            processor.manualCalibrate(scale, 0.5, 0.0);
            processor.setAlgorithm("hysteresis");
            processor.setHysteresisPercent(0);
            processor.setAggressionPercent(100);
            processor.handleFrame(frameAt(0.0, 1), 96, 96, 2.0);
            processor.selectStarAt(48, 48);
            processor.setOutputLinked(true); processor.startGuiding();
        };

        // Scale 0.75"/px: deadband = 0.1515 + 0.1548/0.75 = 0.3579 px.
        // 0.30 px error suppressed; 0.45 px fires. (The old double
        // conversion raised the deadband to 0.477 px and suppressed 0.45.)
        {
            GuideProcessor processor;
            prepare(processor, 0.75);
            int pulses = 0;
            QObject::connect(&processor, &GuideProcessor::guidePulse, [&] { ++pulses; });
            processor.handleFrame(frameAt(0.30, 2), 96, 96, 2.0);
            QCOMPARE(pulses, 0);
            processor.handleFrame(frameAt(0.45, 3), 96, 96, 2.0);
            QCOMPARE(pulses, 1);
        }

        // Scale 5.0"/px: deadband = 0.1515 + 0.1548/5 = 0.1825 px.
        // 0.15 px suppressed; 0.25 px fires.
        {
            GuideProcessor processor;
            prepare(processor, 5.0);
            int pulses = 0;
            QObject::connect(&processor, &GuideProcessor::guidePulse, [&] { ++pulses; });
            processor.handleFrame(frameAt(0.15, 4), 96, 96, 2.0);
            QCOMPARE(pulses, 0);
            processor.handleFrame(frameAt(0.25, 5), 96, 96, 2.0);
            QCOMPARE(pulses, 1);
        }
    }

    void rmsSlidingWindowDropsOldOutliers() {
        // One large excursion then steady small errors: while the outlier
        // sits inside the 30-frame window the RMS stays high; on the 31st
        // good frame it must fall to the steady value.
        auto frameAt = [](double offset, unsigned seed) {
            std::mt19937 rng(seed);
            std::normal_distribution<double> noise(0.0, 3.0);
            QVector<quint16> pixels(96 * 96);
            for (int y = 0; y < 96; ++y)
                for (int x = 0; x < 96; ++x) {
                    const double dx = x - (48.0 + offset), dy = y - 48.0;
                    const double value = 500 + 9000 * std::exp(-(dx * dx + dy * dy) / (2.0 * 2.2 * 2.2)) + noise(rng);
                    pixels[y * 96 + x] = quint16(std::clamp(value, 0.0, 65535.0));
                }
            return pixels;
        };
        GuideProcessor processor;
        processor.manualCalibrate(1.0, 0.5, 0.0);
        processor.setAlgorithm("identity");
        processor.handleFrame(frameAt(0.0, 10), 96, 96, 2.0);
        processor.selectStarAt(48, 48);
        processor.setOutputLinked(true); processor.startGuiding();

        processor.handleFrame(frameAt(6.0, 11), 96, 96, 2.0); // 6" excursion
        QVERIFY(processor.rmsRa() > 4.0);
        for (int i = 0; i < 29; ++i) {
            processor.handleFrame(frameAt(0.5, 20 + i), 96, 96, 2.0);
            QVERIFY2(processor.locked(), qPrintable(QStringLiteral("Frame %1 rejected: misses=%2").arg(i).arg(processor.missCount())));
        }
        // Window = outlier + 29 steady frames -> RMS still elevated.
        QVERIFY(processor.rmsRa() > 1.0);
        // 31st steady frame: the outlier leaves the window entirely.
        processor.handleFrame(frameAt(0.5, 60), 96, 96, 2.0);
        QVERIFY2(processor.rmsRa() < 0.6, qPrintable(QStringLiteral("RMS=%1 error=%2 misses=%3").arg(processor.rmsRa()).arg(processor.raErrorArcsec()).arg(processor.missCount())));
    }

    void manualCalibrateValidationRefusesAndPreserves() {
        GuideProcessor processor;
        QVERIFY(processor.manualCalibrate(0.75, 0.5, 30.0));
        // Invalid attempts: NaN / infinity / bad declination / zero rate.
        QVERIFY(!processor.manualCalibrate(std::numeric_limits<double>::quiet_NaN(), 0.5, 0.0));
        QVERIFY(!processor.manualCalibrate(0.75, std::numeric_limits<double>::infinity(), 0.0));
        QVERIFY(!processor.manualCalibrate(0.75, 0.5, 95.0));
        QVERIFY(!processor.manualCalibrate(0.75, 0.0, 0.0));
        QVERIFY(!processor.manualCalibrate(-1.0, 0.5, 0.0));
        // The working calibration survives every refusal.
        QVERIFY(processor.calibrated());
        processor.setOutputLinked(true); QVERIFY(!processor.startGuiding()); // no star yet — separate guard
    }

    void hysteresisMatchesHandComputedSequence() {
        GuideAlgo::Hysteresis algo;
        algo.setHysteresisPercent(10);   // -> 0.10 (PHD2 maps 1:1)
        algo.setAggressionPercent(70);   // -> 0.7
        algo.setMinMove(0.0);
        QCOMPARE(algo.hysteresis(), 0.10);
        QCOMPARE(algo.aggression(), 0.7);
        // Frame 1: out = (1-h)*in * a
        QCOMPARE(algo.result(1.0), 0.9 * 0.7);
        // Frame 2: out = ((1-h)*in + h*last) * a
        const double last = 0.9 * 0.7;
        QCOMPARE(algo.result(1.0), ((0.9 * 1.0) + 0.10 * last) * 0.7);
    }

    void hysteresisDeadbandZeroesSmallInput() {
        GuideAlgo::Hysteresis algo;
        algo.setMinMove(0.5);
        QCOMPARE(algo.result(0.3), 0.0);
        QVERIFY(algo.result(1.0) != 0.0);
        algo.reset();
        QCOMPARE(algo.result(1.0), (1.0 - algo.hysteresis()) * 1.0 * algo.aggression());
    }

    void identityPassesThrough() {
        GuideAlgo::Identity algo;
        QCOMPARE(algo.result(-2.5), -2.5);
        algo.reset();
        QCOMPARE(algo.result(0.0), 0.0);
    }

    void smartDefaultMinMoveFollowsPhd2Curve() {
        QCOMPARE(GuideAlgo::smartDefaultMinMove(1.0), 0.3063);
        QVERIFY(GuideAlgo::smartDefaultMinMove(0.1) > 1.0);
        // The formula stays just above its 0.15 floor even at huge scales.
        QCOMPARE(GuideAlgo::smartDefaultMinMove(1000.0), 0.1515 + 0.1548 / 1000.0);
    }

    void cameraMountTransformRoundTrips() {
        Calibration cal = manual(0.75, 0.5, 30.0, 15.0);
        QVERIFY(cal.valid);
        for (const auto [dx, dy] : std::vector<std::pair<double, double>>{
                 {3.0, 0.0}, {0.0, -2.5}, {1.5, 2.5}, {-4.0, -1.0}, {0.0, 0.0}}) {
            const auto mount = cameraToMount(cal, dx, dy);
            const auto back = mountToCamera(cal, mount.first, mount.second);
            QVERIFY(std::abs(back.first - dx) < 1e-9);
            QVERIFY(std::abs(back.second - dy) < 1e-9);
        }
    }

    void orthogonalAnglesMapAxesDirectly() {
        Calibration cal = manual(1.0, 0.5, 0.0, 0.0); // camera x == RA
        const auto raOnly = cameraToMount(cal, 3.0, 0.0);
        QVERIFY(std::abs(raOnly.first - 3.0) < 1e-9);
        QVERIFY(std::abs(raOnly.second) < 1e-9);
        const auto decOnly = cameraToMount(cal, 0.0, -2.0);
        QVERIFY(std::abs(decOnly.first) < 1e-9);
        QVERIFY(std::abs(decOnly.second + 2.0) < 1e-9);
    }

    void manualRatesFollowImageScaleAndSpeed() {
        const Calibration cal = manual(0.75, 0.5, 0.0);
        // 0.5x sidereal = 7.5"/s = 0.0075"/ms; / 0.75"/px = 0.01 px/ms.
        QCOMPARE(cal.xRate, 0.01);
        QCOMPARE(cal.yRate, 0.01);
        const Calibration dec60 = manual(0.75, 0.5, 60.0);
        QCOMPARE(dec60.xRate, 0.01 * std::cos(60.0 * M_PI / 180.0)); // less sky per ms
        QCOMPARE(dec60.yRate, 0.01);
        QVERIFY(!manual(-1.0, 0.5, 0.0).valid);
        QVERIFY(!manual(0.75, 0.0, 0.0).valid);
    }

    void declinationCompensationRescalesRaRate() {
        Calibration cal = manual(0.75, 0.5, 0.0);
        const double rateAtEquator = cal.xRate;
        QVERIFY(adjustForDeclination(cal, 60.0));
        QCOMPARE(cal.xRate, rateAtEquator * std::cos(60.0 * M_PI / 180.0));
        QVERIFY(!adjustForDeclination(cal, 89.9)); // PHD2 refuses near the pole
    }

    void pulseComputationMatchesRate() {
        Calibration cal = manual(0.75, 0.5, 0.0); // 0.01 px/ms
        const Pulse pulse = pulseFor(cal, 3.0, -2.0);
        QCOMPARE(pulse.raMs, 300);
        QCOMPARE(pulse.decMs, 200);
        QVERIFY(pulse.raWest);     // +RA offset guides West
        QVERIFY(!pulse.decSouth);  // -Dec offset guides North
        // Cap: 50 px -> 5000 ms clamps to the PHD2-style ceiling.
        const Pulse capped = pulseFor(cal, 50.0, 0.0, 1000, 1000);
        QCOMPARE(capped.raMs, 1000);
        QCOMPARE(capped.decMs, 0);
        QCOMPARE(pulseFor(cal, 0.0, 0.0).raMs, 0);
    }

    void processorClosedLoopEmitsPulses() {
        GuideProcessor processor;
        processor.setOutputLinked(true); QVERIFY(!processor.startGuiding());       // no calibration/star yet
        QVERIFY(processor.manualCalibrate(0.75, 0.5, 0.0));
        // PHD2's SmartDefaultMinMove returns PIXELS; the deadband must not
        // be divided by the image scale again (a past double conversion
        // halved it at 2"/px and over-corrected).
        QVERIFY(processor.calibrated());

        // Synthetic frame, star at (48,48); identity algorithm for exact math.
        processor.setAlgorithm("identity");
        auto frame = [](double sx, double sy, unsigned seed) {
            std::mt19937 rng(seed);
            std::normal_distribution<double> noise(0.0, 5.0);
            QVector<quint16> pixels(96 * 96);
            for (int y = 0; y < 96; ++y)
                for (int x = 0; x < 96; ++x) {
                    const double dx = x - sx, dy = y - sy;
                    const double value = 500 + 4000 * std::exp(-(dx * dx + dy * dy) / (2.0 * 2.2 * 2.2)) + noise(rng);
                    pixels[y * 96 + x] = quint16(std::clamp(value, 0.0, 65535.0));
                }
            return pixels;
        };
        processor.handleFrame(frame(48, 48, 1), 96, 96, 2.0);
        QVERIFY(processor.selectStarAt(48, 48));
        processor.setOutputLinked(true); QVERIFY(processor.startGuiding());
        QVERIFY(processor.guiding());

        // Star drifted +3px along camera x == RA: identity output 3px ->
        // 3 / 0.01 = 300ms West; deadband far below.
        int raMs = 0, decMs = 0;
        bool raWest = false, decSouth = false;
        QObject::connect(&processor, &GuideProcessor::guidePulse,
                         [&](bool w, int rm, bool s, int dm) { raWest = w; raMs = rm; decSouth = s; decMs = dm; });
        processor.handleFrame(frame(51.0, 48.0, 2), 96, 96, 2.0);
        QVERIFY(raMs > 280 && raMs < 320);
        QVERIFY(raWest);
        QVERIFY(decMs < 20);
        QVERIFY(std::abs(processor.raErrorArcsec() - 3.0 * 0.75) < 0.2);
        QVERIFY(processor.rmsRa() > 0.0);

        // Star loss while guiding drops the loop and the lock together.
        // Decisively empty frames: faint speckle cannot clear the gates.
        auto emptyFrame = [](unsigned seed) {
            std::mt19937 rng(seed);
            std::normal_distribution<double> noise(0.0, 1.5);
            QVector<quint16> pixels(96 * 96);
            for (int i = 0; i < pixels.size(); ++i)
                pixels[i] = quint16(std::clamp(500.0 + noise(rng), 0.0, 65535.0));
            return pixels;
        };
        for (int i = 0; i < 6; ++i)
            processor.handleFrame(emptyFrame(10 + i), 96, 96, 2.0);
        QVERIFY(!processor.guiding());
        QVERIFY(!processor.locked());

        // State protection: clearing the star mid-guiding must stop the
        // loop on its own, even without a UI stop.
        processor.handleFrame(frame(48, 48, 30), 96, 96, 2.0);
        processor.selectStarAt(48, 48);
        processor.setAlgorithm("identity");
        processor.setOutputLinked(true); QVERIFY(processor.startGuiding());
        processor.handleFrame(frame(51.0, 48.0, 31), 96, 96, 2.0); // pulse fires, log set
        QVERIFY(processor.pulseLog().length() > 0);
        processor.clearStar();
        QVERIFY(!processor.guiding());
        QVERIFY(processor.curve().size() == 0);
        // Stale errors and the last pulse text do not survive either.
        QCOMPARE(processor.raErrorArcsec(), 0.0);
        QCOMPARE(processor.pulseLog(), QString());

        // Lock policy: adopting a new lock always ends the loop — guiding
        // must be restarted explicitly after re-locking.
        processor.handleFrame(frame(48, 48, 32), 96, 96, 2.0);
        QVERIFY(processor.selectStarAt(48, 48));
        processor.setOutputLinked(true); QVERIFY(processor.startGuiding());
        QVERIFY(processor.guiding());
        processor.handleFrame(frame(48, 48, 33), 96, 96, 2.0);
        QVERIFY(processor.selectStarAt(48, 48)); // re-lock while guiding
        QVERIFY(!processor.guiding());           // loop ended by policy
        processor.setOutputLinked(true); QVERIFY(processor.startGuiding());       // explicit restart works

        // Transient star loss is not a lock: startGuiding refuses during
        // an active miss burst.
        processor.setAlgorithm("identity");
        for (int i = 0; i < 3; ++i)
            processor.handleFrame(emptyFrame(70 + i), 96, 96, 2.0);
        processor.setOutputLinked(true); QVERIFY(!processor.startGuiding());

        // Clearing the calibration stops any further pulse computation.
        processor.handleFrame(frame(48, 48, 20), 96, 96, 2.0);
        processor.selectStarAt(48, 48);
        processor.clearCalibration();
        QVERIFY(!processor.calibrated());
        processor.setOutputLinked(true); QVERIFY(!processor.startGuiding());
    }
};

QTEST_GUILESS_MAIN(GuideAlgorithmTest)
#include "tst_guidealgorithm.moc"
