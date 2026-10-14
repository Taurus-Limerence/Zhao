#include <QtTest>
#include "focus/FocusMetrics.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace {
constexpr int width = 512;
constexpr int height = 384;

void addStar(QVector<quint16> &image, double cx, double cy, double sx,
             double amplitude = 16000.0, double sy = 0.0, int limit = 65535)
{
    if (sy <= 0.0)
        sy = sx;
    const int radius = int(std::ceil(5.0 * std::max(sx, sy)));
    for (int y = std::max(0, int(cy) - radius); y <= std::min(height - 1, int(cy) + radius); ++y) {
        for (int x = std::max(0, int(cx) - radius); x <= std::min(width - 1, int(cx) + radius); ++x) {
            const double dx = (x - cx) / sx;
            const double dy = (y - cy) / sy;
            const double value = image[y * width + x] + amplitude * std::exp(-0.5 * (dx * dx + dy * dy));
            image[y * width + x] = quint16(std::min(double(limit), std::round(value)));
        }
    }
}

QVector<quint16> starField(double sigma, int count = 12, double amplitude = 16000.0, int limit = 65535)
{
    QVector<quint16> image(width * height, 1000);
    for (int i = 0; i < count; ++i)
        addStar(image, 64.3 + (i % 4) * 120, 64.2 + (i / 4) * 120, sigma, amplitude, 0.0, limit);
    return image;
}

QVector<FocusMetrics::Sample> hyperbola(int center = 1000000000, double offset = 17.0)
{
    QVector<FocusMetrics::Sample> samples;
    for (int i = -4; i <= 4; ++i) {
        const double distance = i * 80.0 - offset;
        samples.append({center + i * 80, std::sqrt(4.0 + 0.0001 * distance * distance), 12});
    }
    return samples;
}
}

class TestFocusMetrics : public QObject
{
    Q_OBJECT
private slots:
    void gaussianHfr_data();
    void gaussianHfr();
    void gaussianHfrWithNoise();
    void bayerHfrUsesSensorPixels();
    void medianRejectsOutlierSizes();
    void rejectsTooFewStars();
    void rejectsSaturatedStars_data();
    void rejectsSaturatedStars();
    void rejectsHotPixelsAndEdges();
    void rejectsUniformAndNoise();
    void rejectsElongatedObjects();
    void rejectsExcessiveDefocus_data();
    void rejectsExcessiveDefocus();
    void rejectsMalformedFrame();
    void fitsBracketedHyperbola();
    void ignoresInvalidSamples();
    void rejectsUntrustworthyFits_data();
    void rejectsUntrustworthyFits();
};

void TestFocusMetrics::gaussianHfr_data()
{
    QTest::addColumn<double>("sigma");
    QTest::addColumn<double>("expected");
    QTest::newRow("focused") << 2.0 << 2.355;
    QTest::newRow("wide") << 4.0 << 4.710;
    QTest::newRow("defocused") << 8.0 << 9.419;
}

void TestFocusMetrics::gaussianHfr()
{
    QFETCH(double, sigma);
    QFETCH(double, expected);
    const auto result = FocusMetrics::measure(starField(sigma), width, height);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(result.stars >= 9);
    QVERIFY2(std::abs(result.hfr - expected) < 0.4, qPrintable(QString::number(result.hfr)));
}

void TestFocusMetrics::bayerHfrUsesSensorPixels()
{
    auto image = starField(3.2);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double factor = (y % 2 == 0 && x % 2 == 0) ? 0.55 : (y % 2 && x % 2) ? 0.8 : 1.0;
            image[y * width + x] = quint16(1000 + (image[y * width + x] - 1000) * factor);
        }
    }
    const auto result = FocusMetrics::measure(image, width, height, {{"BAYERPAT", "RGGB"}});
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(result.stars >= 9);
    QVERIFY(std::abs(result.hfr - 3.768) < 0.5);
}

void TestFocusMetrics::gaussianHfrWithNoise()
{
    auto image = starField(4.0);
    std::mt19937 random(4321);
    std::normal_distribution<double> noise(0.0, 80.0);
    for (auto &value : image)
        value = quint16(std::clamp(double(value) + noise(random), 0.0, 65535.0));
    const auto result = FocusMetrics::measure(image, width, height);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(result.stars >= 9);
    QVERIFY(std::abs(result.hfr - 4.710) < 0.4);
}

void TestFocusMetrics::medianRejectsOutlierSizes()
{
    auto image = starField(2.0, 7);
    addStar(image, 424.3, 184.2, 7.0);
    addStar(image, 64.3, 304.2, 7.0);
    const auto result = FocusMetrics::measure(image, width, height);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(std::abs(result.hfr - 2.355) < 0.4);
}

void TestFocusMetrics::rejectsTooFewStars()
{
    const auto result = FocusMetrics::measure(starField(2.0, 4), width, height);
    QCOMPARE(result.stars, 4);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!std::isfinite(result.hfr));
}

void TestFocusMetrics::rejectsSaturatedStars_data()
{
    QTest::addColumn<QVariantMap>("metadata");
    QTest::addColumn<int>("limit");
    QTest::newRow("16-bit-default") << QVariantMap{} << 65535;
    QTest::newRow("12-bit-raw") << QVariantMap{{"RAWBITS", 12}} << 4095;
    QTest::newRow("datamax-precedence") << QVariantMap{{"DATAMAX", 4095}, {"RAWBITS", 16}} << 4095;
    QTest::newRow("bayer-saturated-channel") << QVariantMap{{"BAYERPAT", "RGGB"}, {"RAWBITS", 12}} << 4095;
}

void TestFocusMetrics::rejectsSaturatedStars()
{
    QFETCH(QVariantMap, metadata);
    QFETCH(int, limit);
    const auto result = FocusMetrics::measure(starField(2.0, 12, limit * 2.0, limit), width, height, metadata);
    QCOMPARE(result.stars, 0);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!std::isfinite(result.hfr));
}

void TestFocusMetrics::rejectsHotPixelsAndEdges()
{
    QVector<quint16> image(width * height, 1000);
    for (int i = 0; i < 10; ++i) {
        image[(40 + i * 20) * width + 100] = 25000;
        addStar(image, 1.5, 30.0 + i * 30, 3.0);
    }
    const auto result = FocusMetrics::measure(image, width, height);
    QCOMPARE(result.stars, 0);
    QVERIFY(!result.error.isEmpty());
}

void TestFocusMetrics::rejectsUniformAndNoise()
{
    QVector<quint16> image(width * height, 1000);
    QVERIFY(!FocusMetrics::measure(image, width, height).error.isEmpty());
    std::mt19937 random(1234);
    std::normal_distribution<double> noise(1000.0, 200.0);
    for (auto &value : image)
        value = quint16(std::max(0.0, noise(random)));
    const auto result = FocusMetrics::measure(image, width, height);
    QCOMPARE(result.stars, 0);
    QVERIFY(!std::isfinite(result.hfr));
}

void TestFocusMetrics::rejectsElongatedObjects()
{
    QVector<quint16> image(width * height, 1000);
    for (int i = 0; i < 12; ++i)
        addStar(image, 64.3 + (i % 4) * 120, 64.2 + (i / 4) * 120, 7.0, 16000.0, 0.8);
    const auto result = FocusMetrics::measure(image, width, height);
    QCOMPARE(result.stars, 0);
    QVERIFY(!std::isfinite(result.hfr));
}

void TestFocusMetrics::rejectsExcessiveDefocus_data()
{
    QTest::addColumn<QVariantMap>("metadata");
    QTest::newRow("mono") << QVariantMap{};
    QTest::newRow("bayer") << QVariantMap{{"BAYERPAT", "RGGB"}};
}

void TestFocusMetrics::rejectsExcessiveDefocus()
{
    QFETCH(QVariantMap, metadata);
    const auto result = FocusMetrics::measure(starField(18.0), width, height, metadata);
    QCOMPARE(result.stars, 0);
    QVERIFY(!result.error.isEmpty());
}

void TestFocusMetrics::rejectsMalformedFrame()
{
    QVERIFY(!FocusMetrics::measure({}, 0, 0).error.isEmpty());
    QVERIFY(!FocusMetrics::measure({1, 2, 3}, 2, 2).error.isEmpty());
    QVERIFY(!FocusMetrics::measure({1}, -1, -1).error.isEmpty());
}

void TestFocusMetrics::fitsBracketedHyperbola()
{
    const auto result = FocusMetrics::fit(hyperbola());
    QVERIFY2(result.valid, qPrintable(result.error));
    QVERIFY(std::abs(result.position - 1000000017.0) < 0.01);
    QVERIFY(std::abs(result.hfr - 2.0) < 0.001);
    QVERIFY(result.rSquared > 0.99999);
    QVERIFY(result.curve.size() > 10);
    for (const auto &point : result.curve)
        QVERIFY(std::isfinite(point.x()) && std::isfinite(point.y()) && point.y() > 0.0);
}

void TestFocusMetrics::ignoresInvalidSamples()
{
    auto samples = hyperbola();
    samples.append({1, 0.0, 20});
    samples.append({2, 3.0, 2});
    samples.append({3, std::numeric_limits<double>::quiet_NaN(), 20});
    QVERIFY(FocusMetrics::fit(samples).valid);
}

void TestFocusMetrics::rejectsUntrustworthyFits_data()
{
    QTest::addColumn<int>("scenario");
    QTest::newRow("too-few-points") << 0;
    QTest::newRow("duplicate-positions") << 1;
    QTest::newRow("minimum-outside-scan") << 2;
    QTest::newRow("only-one-point-on-left") << 3;
    QTest::newRow("insufficient-two-sided-growth") << 4;
    QTest::newRow("flat") << 5;
    QTest::newRow("negative-curvature") << 6;
    QTest::newRow("poor-hfr-fit") << 7;
    QTest::newRow("nonpositive-minimum") << 8;
}

void TestFocusMetrics::rejectsUntrustworthyFits()
{
    QFETCH(int, scenario);
    auto samples = hyperbola(1000);
    switch (scenario) {
    case 0: samples.resize(6); break;
    case 1: samples[4].position = samples[3].position; break;
    case 2: samples = hyperbola(1000, 400.0); break;
    case 3: samples = hyperbola(1000, -280.0); break;
    case 4:
        for (auto &sample : samples) sample.hfr = 2.0 + (sample.hfr - 2.0) * 0.05;
        break;
    case 5:
        for (auto &sample : samples) sample.hfr = 2.0;
        break;
    case 6:
        for (auto &sample : samples) sample.hfr = 6.0 - sample.hfr;
        break;
    case 7:
        samples[1].hfr *= 2.0;
        samples[7].hfr *= 2.0;
        break;
    case 8:
        for (auto &sample : samples) {
            const double distance = sample.position - 1017.0;
            sample.hfr = std::sqrt(std::max(0.01, 0.0001 * distance * distance - 0.01));
        }
        break;
    }
    const auto result = FocusMetrics::fit(samples);
    QVERIFY(!result.valid);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!std::isfinite(result.position));
    QVERIFY(result.curve.isEmpty());
}

QTEST_GUILESS_MAIN(TestFocusMetrics)
#include "tst_focusmetrics.moc"
