#include <QtTest>
#include "imaging/PreviewHistogram.h"

class PreviewHistogramTest final : public QObject
{
    Q_OBJECT
private slots:
    void emptyImageHasNoStatistics() {
        QVERIFY(previewHistogram(QImage()).isEmpty());
    }
    void countsKnownPixelsWithoutChangingSource() {
        QImage image(3, 2, QImage::Format_RGB32);
        image.setPixel(0, 0, qRgb(0, 0, 0));
        image.setPixel(1, 0, qRgb(255, 255, 255));
        image.setPixel(2, 0, qRgb(255, 0, 0));
        image.setPixel(0, 1, qRgb(0, 255, 0));
        image.setPixel(1, 1, qRgb(0, 0, 255));
        image.setPixel(2, 1, qRgb(0, 0, 0));
        const QImage original = image.copy();
        const auto bins = previewHistogram(image);
        QCOMPARE(bins.size(), 256);
        QCOMPARE(bins[0].toULongLong(), 2u);
        QCOMPARE(bins[255].toULongLong(), 1u);
        QCOMPARE(bins[87].toULongLong(), 1u);
        QCOMPARE(bins[127].toULongLong(), 1u);
        QCOMPARE(bins[39].toULongLong(), 1u);
        qulonglong total = 0;
        for (const auto &count : bins) total += count.toULongLong();
        QCOMPARE(total, 6u);
        QCOMPARE(image, original);
    }
};
QTEST_APPLESS_MAIN(PreviewHistogramTest)
#include "tst_previewhistogram.moc"
