#include <QtTest>
#include <cmath>
#include <random>
#include "guide/GuideProcessor.h"

namespace {

constexpr int kWidth = 96;
constexpr int kHeight = 96;

QVector<quint16> synthesize(double starX, double starY, double amplitude,
                            int background, unsigned seed, double sigma = 2.2)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, 6.0);
    QVector<quint16> frame(kWidth * kHeight);
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) {
            const double dx = x - starX;
            const double dy = y - starY;
            const double value = background
                + amplitude * std::exp(-(dx * dx + dy * dy) / (2.0 * sigma * sigma)) + noise(rng);
            frame[y * kWidth + x] = quint16(std::clamp(value, 0.0, 65535.0));
        }
    return frame;
}

} // namespace

class GuideProcessorTest final : public QObject
{
    Q_OBJECT
private slots:
    void shapeChangeIsRejectedEvenAtSimilarFlux() {
        GuideProcessor processor;
        QVERIFY(processor.manualCalibrate(1, 0.5, 0));
        processor.handleFrame(synthesize(48, 48, 4000, 500, 1), kWidth, kHeight, 2.0);
        QVERIFY(processor.selectStarAt(48, 48));
        processor.setOutputLinked(true); QVERIFY(processor.startGuiding());
        QSignalSpy pulses(&processor, &GuideProcessor::guidePulse);
        processor.handleFrame(synthesize(50, 48, 11455, 500, 2, 1.3), kWidth, kHeight, 2.0);
        QCOMPARE(processor.missCount(), 1);
        QCOMPARE(pulses.count(), 0);
        QVERIFY(std::abs(processor.starX() - 48) < 0.2);
    }

    void exposureChangeNormalizesIdentityFlux() {
        GuideProcessor processor;
        processor.handleFrame(synthesize(48, 48, 4000, 500, 1), kWidth, kHeight, 2.0);
        QVERIFY(processor.selectStarAt(48, 48));
        processor.handleFrame(synthesize(49, 48, 16000, 500, 2), kWidth, kHeight, 8.0);
        QCOMPARE(processor.missCount(), 0);
        QVERIFY(processor.locked());
        QVERIFY(std::abs(processor.errorX() - 1) < 0.2);
    }

    void axisParametersOnlyAffectTheirOwnPulse() {
        GuideProcessor processor;
        QVERIFY(processor.manualCalibrate(1, 0.5, 0));
        QVERIFY(processor.setProperty("raMinMovePx", 2.0));
        QVERIFY(processor.setProperty("decMinMovePx", 0.1));
        QVERIFY(processor.setProperty("raHysteresisPercent", 0));
        QVERIFY(processor.setProperty("decHysteresisPercent", 0));
        QVERIFY(processor.setProperty("raAggressionPercent", 100));
        QVERIFY(processor.setProperty("decAggressionPercent", 50));
        processor.handleFrame(synthesize(48, 48, 4000, 500, 1), kWidth, kHeight, 2.0);
        QVERIFY(processor.selectStarAt(48, 48));
        processor.setOutputLinked(true); QVERIFY(processor.startGuiding());
        QSignalSpy pulses(&processor, &GuideProcessor::guidePulse);
        processor.handleFrame(synthesize(49, 49, 4000, 500, 2), kWidth, kHeight, 2.0);
        QCOMPARE(pulses.count(), 1);
        QCOMPARE(pulses.last()[1].toInt(), 0);
        QVERIFY(std::abs(pulses.last()[3].toInt() - 67) < 5);
        QVERIFY(processor.setProperty("raMinMovePx", 0.1));
        pulses.clear();
        processor.handleFrame(synthesize(49, 49, 4000, 500, 2), kWidth, kHeight, 2.0);
        QCOMPARE(pulses.count(), 1);
        QVERIFY(std::abs(pulses.last()[1].toInt() - 133) < 5);
        QVERIFY(std::abs(pulses.last()[3].toInt() - 67) < 5);
    }

    void searchFollowsMeasuredStarButErrorKeepsOriginalLock() {
        GuideProcessor processor;
        processor.handleFrame(synthesize(24, 48, 4000, 500, 1), kWidth, kHeight, 2.0);
        QVERIFY(processor.selectStarAt(24, 48));
        for (int x = 26; x <= 68; x += 2) {
            processor.handleFrame(synthesize(x, 48, 4000, 500, x), kWidth, kHeight, 2.0);
            QVERIFY(processor.locked());
            QVERIFY(std::abs(processor.starX() - x) < 0.2);
            QVERIFY(std::abs(processor.errorX() - (x - 24)) < 0.2);
        }
    }

    void changedCandidateCannotMoveLockOrSendPulses() {
        GuideProcessor processor;
        QVERIFY(processor.manualCalibrate(1, 0.5, 0));
        processor.handleFrame(synthesize(48, 48, 4000, 500, 1), kWidth, kHeight, 2.0);
        QVERIFY(processor.selectStarAt(48, 48));
        processor.setOutputLinked(true); QVERIFY(processor.startGuiding());
        QSignalSpy pulses(&processor, &GuideProcessor::guidePulse);
        processor.handleFrame(synthesize(54, 48, 16000, 500, 2), kWidth, kHeight, 2.0);
        QCOMPARE(pulses.count(), 0);
        QCOMPARE(processor.missCount(), 1);
        QVERIFY(std::abs(processor.starX() - 48) < 0.2);
        // The original star returns: recover without moving its fixed lock.
        processor.handleFrame(synthesize(49, 48, 4000, 500, 3), kWidth, kHeight, 2.0);
        QVERIFY(processor.locked());
        QVERIFY(std::abs(processor.errorX() - 1) < 0.2);
        QVERIFY(pulses.count() > 0);
    }

    void unselectedFrameNotifiesDeclaredProperties() {
        GuideProcessor processor;
        const auto property = processor.metaObject()->property(processor.metaObject()->indexOfProperty("frameCount"));
        QSignalSpy countChanges(&processor, property.notifySignal());
        QSignalSpy stateChanges(&processor, &GuideProcessor::stateChanged);
        processor.handleFrame(synthesize(48, 48, 4000, 500, 1), kWidth, kHeight, 2.0);
        QVERIFY(countChanges.count() > 0);
        QVERIFY(stateChanges.count() > 0);
    }

    void loopResetsNotifyReadouts() {
        GuideProcessor processor;
        QVERIFY(processor.manualCalibrate(1.0, 0.5, 0));
        processor.handleFrame(synthesize(48, 48, 4000, 500, 1), kWidth, kHeight, 2.0);
        QVERIFY(processor.selectStarAt(48, 48));
        QSignalSpy readouts(&processor, &GuideProcessor::starChanged);
        processor.setOutputLinked(true); QVERIFY(processor.startGuiding());
        QVERIFY(readouts.count() > 0);
        readouts.clear();
        QVERIFY(processor.manualCalibrate(0.75, 0.5, 0));
        QVERIFY(readouts.count() > 0);
    }

    void loopWithoutSelectionJustCountsFrames() {
        GuideProcessor processor;
        QCOMPARE(processor.state(), "idle");
        processor.handleFrame(synthesize(48, 48, 4000, 500, 1), kWidth, kHeight, 2.0);
        QCOMPARE(processor.state(), "looping");
        QCOMPARE(processor.frameCount(), 1);
        QCOMPARE(processor.frameWidth(), double(kWidth));
        QVERIFY(!processor.locked());
    }

    void selectTracksAndReportsErrors() {
        GuideProcessor processor;
        // Frame 1: star at (48,48) — user picks it.
        processor.handleFrame(synthesize(48.0, 48.0, 4000, 500, 2), kWidth, kHeight, 2.0);
        QVERIFY(processor.selectStarAt(48, 48));
        QCOMPARE(processor.state(), "locked");
        QVERIFY(processor.snr() > 10.0);

        // Frame 2: star drifted (+1.2, -0.8) — tracked from the lock.
        processor.handleFrame(synthesize(49.2, 47.2, 4000, 500, 3), kWidth, kHeight, 2.0);
        QCOMPARE(processor.state(), "locked");
        QVERIFY(std::abs(processor.errorX() - 1.2) < 0.2);
        QVERIFY(std::abs(processor.errorY() + 0.8) < 0.2);
        QCOMPARE(processor.curve().size(), 1);

        // Frame 3: bigger drift (+3.0, +2.0 cumulative from lock).
        processor.handleFrame(synthesize(51.0, 50.0, 4000, 500, 4), kWidth, kHeight, 2.0);
        QCOMPARE(processor.curve().size(), 2);
        QVERIFY(std::abs(processor.errorX() - 3.0) < 0.25);
    }

    void autoSelectLocksTheStar() {
        GuideProcessor processor;
        processor.handleFrame(synthesize(60.0, 36.0, 5000, 500, 5), kWidth, kHeight, 2.0);
        QVERIFY(processor.autoSelect());
        QCOMPARE(processor.state(), "locked");
        QVERIFY(std::abs(processor.starX() - 60.0) < 1.0);
        QVERIFY(std::abs(processor.starY() - 36.0) < 1.0);
    }

    void starLossDropsTheLock() {
        GuideProcessor processor;
        processor.handleFrame(synthesize(48.0, 48.0, 4000, 500, 6), kWidth, kHeight, 2.0);
        QVERIFY(processor.selectStarAt(48, 48));
        // Star vanishes (clouds): empty frames keep arriving.
        bool lostEmitted = false;
        QObject::connect(&processor, &GuideProcessor::starLost, [&] { lostEmitted = true; });
        for (int i = 0; i < 4; ++i)
            processor.handleFrame(synthesize(-1000.0, -1000.0, 0.0, 500, 7 + i), kWidth, kHeight, 2.0);
        QCOMPARE(processor.state(), "locked");   // misses within tolerance stay locked
        QCOMPARE(processor.missCount(), 4);
        processor.handleFrame(synthesize(-1000.0, -1000.0, 0.0, 500, 11), kWidth, kHeight, 2.0);
        QVERIFY(lostEmitted);                    // 5th miss drops the lock
        QCOMPARE(processor.state(), "looping");
        QVERIFY(!processor.locked());
    }

    void curveIsCappedAndClears() {
        GuideProcessor processor;
        processor.handleFrame(synthesize(48.0, 48.0, 4000, 500, 12), kWidth, kHeight, 2.0);
        QVERIFY(processor.selectStarAt(48, 48));
        for (int i = 1; i <= 300; ++i)
            processor.handleFrame(synthesize(48.0 + i * 0.01, 48.0, 4000, 500, 20 + i), kWidth, kHeight, 2.0);
        QCOMPARE(processor.curve().size(), 240);
        processor.clearStar();
        QCOMPARE(processor.curve().size(), 0);
        QCOMPARE(processor.state(), "looping");
    }
};

QTEST_GUILESS_MAIN(GuideProcessorTest)
#include "tst_guideprocessor.moc"
