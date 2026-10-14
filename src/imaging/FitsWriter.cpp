#include "FitsWriter.h"

#include <QFile>
#include <QSaveFile>
#include <QElapsedTimer>
#include <QMap>

#include <cstring>

namespace {

// FITS 卡片固定 80 字节，头块与数据块均 2880 字节对齐。
constexpr int kCardBytes = 80;
constexpr int kBlockBytes = 2880;

QByteArray card(const QByteArray &raw)
{
    QByteArray padded = raw.left(kCardBytes);
    padded += QByteArray(kCardBytes - padded.size(), ' ');
    return padded;
}

QByteArray valueCard(const QByteArray &key, const QString &value)
{
    // 字符串值走 FITS 的 '...' 形式，数值/逻辑原样输出。
    bool numeric = false;
    value.toDouble(&numeric);
    const bool logical = value == QLatin1String("T") || value == QLatin1String("F");
    if (numeric || logical)
        return card(key + "= " + value.toLatin1());
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QLatin1String("''"));
    return card(key + "= '" + escaped.toLatin1() + "'");
}

QByteArray finishHeader(QList<QByteArray> cards)
{
    cards.append(card("END"));
    QByteArray header;
    header.reserve(((cards.size() * kCardBytes + kBlockBytes - 1) / kBlockBytes) * kBlockBytes);
    for (const QByteArray &entry : cards)
        header += entry;
    header += QByteArray((kBlockBytes - header.size() % kBlockBytes) % kBlockBytes, ' ');
    return header;
}

QByteArray paddedData(const QByteArray &data)
{
    QByteArray padded = data;
    padded += QByteArray((kBlockBytes - padded.size() % kBlockBytes) % kBlockBytes, '\0');
    return padded;
}

QByteArray primaryHeader(const QSize &size, const QMap<QString, QString> &extra)
{
    QList<QByteArray> cards;
    cards << card("SIMPLE  = T")
          << card(QString("BITPIX  = 8").toLatin1())
          << card(QString("NAXIS   = 2").toLatin1())
          << card(QString("NAXIS1  = %1").arg(size.width()).toLatin1())
          << card(QString("NAXIS2  = %1").arg(size.height()).toLatin1())
          << card(QString("BZERO   = 0").toLatin1())
          << card(QString("BSCALE  = 1").toLatin1());
    // 自定义键（EXPTIME / GAIN / CCD-TEMP / RESOLUTN 等）：键名补齐 8 列。
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) {
        QByteArray key = it.key().toLatin1().leftJustified(8, ' ', true);
        cards << valueCard(key, it.value());
    }
    return finishHeader(cards);
}

QByteArray extensionHeader(const QSize &size, const QByteArray &plane)
{
    QList<QByteArray> cards;
    cards << card("XTENSION= 'IMAGE   '")
          << card(QString("BITPIX  = 8").toLatin1())
          << card(QString("NAXIS   = 2").toLatin1())
          << card(QString("NAXIS1  = %1").arg(size.width()).toLatin1())
          << card(QString("NAXIS2  = %1").arg(size.height()).toLatin1())
          << card(QString("PCOUNT  = 0").toLatin1())
          << card(QString("GCOUNT  = 1").toLatin1())
          << card(plane);
    return finishHeader(cards);
}

QByteArray planeData(const QImage &rgb, int channel)
{
    QByteArray data;
    data.reserve(rgb.width() * rgb.height());
    for (int y = 0; y < rgb.height(); ++y) {
        const QRgb *pixels = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            const QRgb pixel = pixels[x];
            data.append(char(channel == 0 ? qRed(pixel)
                                          : channel == 1 ? qGreen(pixel)
                                                         : qBlue(pixel)));
        }
    }
    return data;
}

} // namespace

bool FitsWriter::write(const QString &path, const QImage &frame,
                       const QMap<QString, QString> &header)
{
    if (frame.isNull())
        return false;
    const QImage rgb = frame.convertToFormat(QImage::Format_RGB32);
    if (rgb.width() <= 0 || rgb.height() <= 0)
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    const QSize size = rgb.size();
    const char *planeNames[3] = {"PLANE1  = 'RED'", "PLANE2  = 'GREEN'", "PLANE3  = 'BLUE'"};
    // 每个 HDU = 头块 + 数据块：主 HDU 承载红平面，两个扩展 HDU 承载绿/蓝。
    const auto writeAll = [&file](const QByteArray &bytes) { return file.write(bytes) == bytes.size(); };
    if (!writeAll(primaryHeader(size, header)))
        return false;
    if (!writeAll(paddedData(planeData(rgb, 0))))
        return false;
    for (int channel = 1; channel < 3; ++channel) {
        if (!writeAll(extensionHeader(size, QByteArray(planeNames[channel]))))
            return false;
        if (!writeAll(paddedData(planeData(rgb, channel))))
            return false;
    }
    return file.commit();
}

bool FitsWriter::writeMono16(const QString &path, const QVector<quint16> &data,
                             int width, int height,
                             const QMap<QString, QString> &header)
{
    const qint64 count = qint64(width) * height;
    if (width <= 0 || height <= 0 || count != data.size() || count > INT_MAX / 2)
        return false;

    QList<QByteArray> cards;
    cards << card("SIMPLE  = T")
          << card(QString("BITPIX  = 16").toLatin1())
          << card(QString("NAXIS   = 2").toLatin1())
          << card(QString("NAXIS1  = %1").arg(width).toLatin1())
          << card(QString("NAXIS2  = %1").arg(height).toLatin1())
          << card(QString("BZERO   = 32768").toLatin1())
          << card(QString("BSCALE  = 1").toLatin1());
    for (auto it = header.constBegin(); it != header.constEnd(); ++it) {
        QByteArray key = it.key().toLatin1().leftJustified(8, ' ', true);
        cards << valueCard(key, it.value());
    }

    // FITS 数据是大端：逐像素交换字节序后按块对齐写出。
    QByteArray pixels;
    pixels.resize(width * height * 2);
    for (int i = 0; i < width * height; ++i) {
        const quint16 value = data.at(i) ^ 0x8000u;
        pixels[2 * i] = char(value >> 8);
        pixels[2 * i + 1] = char(value & 0xFF);
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const auto bytes = finishHeader(cards);
    if (file.write(bytes) != bytes.size())
        return false;
    const auto payload = paddedData(pixels);
    QElapsedTimer deadline;
    deadline.start();
    // Cooperative deadline: an OS-blocked write/commit cannot be killed safely.
    for (qsizetype offset = 0; offset < payload.size();) {
        if (deadline.elapsed() > 30000) return false;
        const qsizetype chunk = qMin(qsizetype(1024 * 1024), payload.size() - offset);
        if (file.write(payload.constData() + offset, chunk) != chunk) return false;
        offset += chunk;
    }
    return file.commit();
}
