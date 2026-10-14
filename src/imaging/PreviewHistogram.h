#pragma once

#include <QImage>
#include <QVariantList>
#include <array>

// Display-only statistics for the bundled 8-bit JPEG, never for RAW/FITS data.
inline QVariantList previewHistogram(const QImage &source)
{
    if (source.isNull())
        return {};

    const QImage image = source.convertToFormat(QImage::Format_RGB32);
    std::array<qulonglong, 256> counts{};
    for (int y = 0; y < image.height(); ++y) {
        const auto *pixels = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            ++counts[qGray(pixels[x])];
    }
    QVariantList bins;
    bins.reserve(256);
    for (const auto count : counts)
        bins.append(QVariant::fromValue(count));
    return bins;
}
