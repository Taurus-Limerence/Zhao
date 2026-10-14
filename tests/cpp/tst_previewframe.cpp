#include <QtTest>
#include "imaging/FitsWriter.h"
#include <QFile>
#include <QTemporaryDir>
#include <QImage>
#include <QThread>
#include <QDir>
#include "imaging/StillSaveQueue.h"
#include "imaging/PreviewFrame.h"

using namespace PreviewFrame;

namespace {
// Builds a tightly packed bottom-up BGR buffer whose pixel at sensor (x, y)
// carries the deterministic BGR triple (x % 256, y % 256, 77).
std::vector<unsigned char> makePattern(unsigned width, unsigned height)
{
    std::vector<unsigned char> data(size_t(width) * height * 3);
    for (unsigned y = 0; y < height; ++y) {
        auto *row = data.data() + size_t(height - 1 - y) * width * 3;
        for (unsigned x = 0; x < width; ++x) {
            row[x * 3 + 0] = x % 256;
            row[x * 3 + 1] = y % 256;
            row[x * 3 + 2] = 77;
        }
    }
    return data;
}
}

class PreviewFrameTest final : public QObject
{
    Q_OBJECT
private slots:
    void saveQueueBoundsAndDrains() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QThread worker;
        auto *queue = new StillSaveQueue;
        queue->moveToThread(&worker);
        connect(&worker, &QThread::finished, queue, &QObject::deleteLater);
        QSignalSpy saved(queue, &StillSaveQueue::saved);
        auto request = [&] {
            StillRequest r;
            r.directory = dir.path(); r.targetName = "M31/unsafe";
            r.imageType = "DARK"; r.width = 2; r.height = 1; r.data = {0, 65535};
            return r;
        };
        for (int i = 0; i < 4; ++i) QVERIFY(queue->enqueue(request()));
        QVERIFY(!queue->enqueue(request()));
        QCOMPARE(queue->pendingCount(), 4);
        worker.start();
        queue->finish();
        QCOMPARE(queue->pendingCount(), 0);
        QVERIFY(!queue->enqueue(request()));
        worker.quit(); worker.wait();
        QTRY_COMPARE(saved.count(), 4);
        for (const auto &result : saved) QVERIFY(result.at(2).toBool());
        const auto files = QDir(dir.path()).entryList({"*.fits"}, QDir::Files);
        QCOMPARE(files.size(), 4);
        for (const auto &name : files) {
            QVERIFY(name.contains("M31_unsafe_DARK"));
            QFile file(dir.filePath(name)); QVERIFY(file.open(QIODevice::ReadOnly));
            QVERIFY(file.readAll().contains("IMAGETYP= 'DARK'"));
        }
    }
    void decimationTargetsPreviewWidth() {
        QCOMPARE(decimationStep(500, 960), 1u);
        QCOMPARE(decimationStep(960, 960), 1u);
        QCOMPARE(decimationStep(961, 960), 2u);
        QCOMPARE(decimationStep(6000, 960), 7u);
        QCOMPARE(decimationStep(0, 960), 1u);
    }

    void nullOrEmptyInputYieldsNullImage() {
        Request request{100, 50};
        QVERIFY(downscaledBgr(nullptr, request).isNull());
        request.sensorWidth = 0;
        QVERIFY(downscaledBgr(nullptr, request).isNull());
    }

    void passthroughKeepsPixelsAndFlipsBottomUp() {
        const unsigned width = 8, height = 4;
        const auto data = makePattern(width, height);
        const QImage image = downscaledBgr(data.data(), {width, height});
        QCOMPARE(image.width(), int(width));
        QCOMPARE(image.height(), int(height));
        // Bottom-up source: output row 0 mirrors image row 0, whose y byte is 0.
        QCOMPARE(image.pixel(3, 0), qRgb(77, 0, 3));
        QCOMPARE(image.pixel(3, height - 1), qRgb(77, 3, 3));
        QCOMPARE(image.pixel(7, 2), qRgb(77, 2, 7));
    }

    void decimationSamplesEveryStepthPixel() {
        const unsigned width = 10, height = 6;
        const auto data = makePattern(width, height);
        // targetWidth 5 on a 10px sensor selects step 2 -> 5x3 output.
        const QImage image = downscaledBgr(data.data(), {width, height, 5});
        QCOMPARE(image.width(), 5);
        QCOMPARE(image.height(), 3);
        // Output pixel (1, 1) samples image row 2 (buffer row 3), x = 2.
        QCOMPARE(image.pixel(1, 1), qRgb(77, 2, 2));
        QCOMPARE(image.pixel(0, 0), qRgb(77, 0, 0));
        QCOMPARE(image.pixel(4, 2), qRgb(77, 4, 8));
    }
    void fitsWriterMono16LayoutAndEndianness() {
        // 4x2 像素已知值：验证头部块对齐、BZERO/IMAGETYP 键、大端字节序。
        QVector<quint16> pixels{0x1234, 0xABCD, 0x0000, 0xFFFF,
                                0x00FF, 0xFF00, 0x8000, 0x7FFF};
        QTemporaryDir dir;
        const QString path = dir.filePath("mono16.fits");
        QMap<QString, QString> header{{"EXPTIME", "2.500000"}, {"IMAGETYP", "LIGHT"}};
        QVERIFY(FitsWriter::writeMono16(path, pixels, 4, 2, header));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray raw = file.readAll();
        // 头块 2880 + 数据 8 字节补齐到 2880 = 5760
        QCOMPARE(raw.size(), 5760);
        QVERIFY(raw.left(80).startsWith("SIMPLE  = T"));
        QCOMPARE(raw.mid(80, 8), QByteArray("BITPIX  "));
        QVERIFY(raw.contains("BZERO   = 32768"));
        // FITS 的等号固定在第 9 列。
        QVERIFY(raw.contains("EXPTIME = 2.500000"));
        QVERIFY(raw.contains("IMAGETYP= 'LIGHT'"));
        // 首像素 0x1234 的 FITS 大端字节序
        for (int i = 0; i < pixels.size(); ++i) {
            const unsigned encoded = unsigned(quint8(raw[2880 + 2*i])) * 256 + quint8(raw[2881 + 2*i]);
            const int signedValue = encoded >= 32768 ? int(encoded) - 65536 : int(encoded);
            QCOMPARE(signedValue + 32768, int(pixels[i]));
        }
        // 拒绝尺寸不匹配的数据
        QVector<quint16> shortData{1, 2, 3};
        QVERIFY(!FitsWriter::writeMono16(dir.filePath("bad.fits"), shortData, 4, 2, {}));
    }
};

QTEST_GUILESS_MAIN(PreviewFrameTest)
#include "tst_previewframe.moc"
