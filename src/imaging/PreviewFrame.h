#pragma once

#include <QImage>
#include <cstring>
#include <vector>

// Decoding contract shared with the Toupcam engine, pinned explicitly in
// ToupcamCamera::openPlatformDefaults:
//   - TOUPCAM_OPTION_BYTEORDER = 1 (BGR) on every platform, so the engine does
//     not fall back to its platform defaults (BGR on Windows, RGB elsewhere).
//   - TOUPCAM_OPTION_UPSIDE_DOWN = 0, rows arrive bottom-up.
//   - Toupcam_PullImageV3 rowPitch = -1, so every row is exactly width*3 bytes
//     (zero padding); never assume the 4-byte-aligned default pitch.
namespace PreviewFrame {

struct Request {
    unsigned sensorWidth = 0;
    unsigned sensorHeight = 0;
    // Decimation: one out of every `step` pixels/rows is kept, targeting a
    // ~960px-wide image so the UI thread never receives the full sensor frame.
    unsigned targetWidth = 960;
};

inline unsigned decimationStep(unsigned sensorWidth, unsigned targetWidth)
{
    if (sensorWidth <= targetWidth) return 1;
    return (sensorWidth + targetWidth - 1) / targetWidth;
}

// data: tightly packed bottom-up BGR rows of sensorWidth * 3 bytes each.
inline QImage downscaledBgr(const unsigned char *data, const Request &request)
{
    const unsigned width = request.sensorWidth;
    const unsigned height = request.sensorHeight;
    if (!data || width == 0 || height == 0) return {};
    const unsigned step = decimationStep(width, request.targetWidth);
    const unsigned outWidth = width / step;
    const unsigned outHeight = height / step;
    if (outWidth == 0 || outHeight == 0) return {};
    QImage image(int(outWidth), int(outHeight), QImage::Format_BGR888);
    for (unsigned y = 0; y < outHeight; ++y) {
        // Bottom-up source: output row 0 mirrors the last sensor row.
        const unsigned char *src = data + size_t(height - 1 - y * step) * width * 3;
        unsigned char *dst = image.scanLine(int(y));
        for (unsigned x = 0; x < outWidth; ++x)
            std::memcpy(dst + size_t(x) * 3, src + size_t(x * step) * 3, 3);
    }
    return image;
}

} // namespace PreviewFrame
