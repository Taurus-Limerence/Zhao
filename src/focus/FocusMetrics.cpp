#include "FocusMetrics.h"
#include <algorithm>
#include <cmath>

namespace {
double median(QVector<double> values)
{
    const auto middle = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), middle, values.end());
    const double upper = *middle;
    return values.size() % 2 ? upper : (upper + *std::max_element(values.begin(), middle)) * 0.5;
}

double noiseEstimate(const QVector<double> &values, double background)
{
    QVector<double> deviations;
    deviations.reserve(values.size());
    for (double value : values)
        deviations.append(std::abs(value - background));
    return std::max(1.0, 1.4826 * median(deviations));
}

struct Candidate {
    int x;
    int y;
    double peak;
};

struct Star {
    double x;
    double y;
    double radius;
    double hfr;
};

bool measureStar(const QVector<float> &image, const QVector<bool> &saturated,
                 int width, int height, int scale, const Candidate &candidate,
                 double globalNoise, Star &star)
{
    const int x = candidate.x;
    const int y = candidate.y;
    const int maxRadius = std::min({40 / scale, x, y, width - x - 1, height - y - 1});
    if (maxRadius < 6)
        return false;

    QVector<double> outer;
    for (int dy = -maxRadius; dy <= maxRadius; ++dy) {
        for (int dx = -maxRadius; dx <= maxRadius; ++dx) {
            const int distance = dx * dx + dy * dy;
            if (distance >= (maxRadius - 2) * (maxRadius - 2) && distance <= maxRadius * maxRadius)
                outer.append(image[qsizetype(y + dy) * width + x + dx]);
        }
    }
    const double background = median(outer);
    const double noise = std::max(globalNoise, noiseEstimate(outer, background));
    const double peak = candidate.peak - background;
    if (peak < 8.0 * noise)
        return false;

    int radius = 0;
    for (int r = 3; r <= maxRadius - 2; ++r) {
        QVector<double> ring;
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                const int distance = dx * dx + dy * dy;
                if (distance >= (r - 1) * (r - 1) && distance <= r * r)
                    ring.append(image[qsizetype(y + dy) * width + x + dx]);
            }
        }
        // Ring medians average many pixels; per-pixel noise would hide still-bright, truncated wings.
        const double ringUncertainty = 4.0 * noise / std::sqrt(double(ring.size()));
        if (median(ring) - background <= std::max(ringUncertainty, peak * 0.002)) {
            radius = r;
            break;
        }
    }
    // A star whose wings never reach background inside the bounded aperture is not measurable.
    if (radius == 0)
        return false;

    double flux = 0.0;
    double xSum = 0.0;
    double ySum = 0.0;
    int pixels = 0;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > radius * radius)
                continue;
            const qsizetype index = qsizetype(y + dy) * width + x + dx;
            if (saturated[index])
                return false;
            const double weight = std::max(0.0, image[index] - background);
            flux += weight;
            xSum += dx * weight;
            ySum += dy * weight;
            ++pixels;
        }
    }
    if (flux <= 0.0 || flux < 20.0 * noise * std::sqrt(double(pixels)) || peak / flux > 0.5)
        return false;

    const double cx = xSum / flux;
    const double cy = ySum / flux;
    if (std::hypot(cx, cy) > radius * 0.25)
        return false;
    double xx = 0.0;
    double yy = 0.0;
    double xy = 0.0;
    QVector<QPointF> radialFlux;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > radius * radius)
                continue;
            const double weight = std::max(0.0, image[qsizetype(y + dy) * width + x + dx] - background);
            const double rx = dx - cx;
            const double ry = dy - cy;
            xx += weight * rx * rx;
            yy += weight * ry * ry;
            xy += weight * rx * ry;
            if (weight > 0.0)
                radialFlux.append(QPointF(std::hypot(rx, ry), weight));
        }
    }
    const double trace = xx + yy;
    const double difference = std::hypot(xx - yy, 2.0 * xy);
    const double minor = (trace - difference) * 0.5;
    const double major = (trace + difference) * 0.5;
    if (minor <= 0.0 || major > 4.0 * minor)
        return false;

    std::sort(radialFlux.begin(), radialFlux.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    double accumulated = 0.0;
    double previousRadius = 0.0;
    for (const auto &pixel : radialFlux) {
        if (accumulated + pixel.y() >= flux * 0.5) {
            const double fraction = (flux * 0.5 - accumulated) / pixel.y();
            const double hfr = previousRadius + fraction * (pixel.x() - previousRadius);
            if (hfr < 0.5 || hfr > radius * 0.6)
                return false;
            star = {x + cx, y + cy, double(radius), hfr * scale};
            return true;
        }
        accumulated += pixel.y();
        previousRadius = pixel.x();
    }
    return false;
}
}

namespace FocusMetrics {

Measurement measure(const QVector<quint16> &data, int width, int height, const QVariantMap &metadata)
{
    Measurement result;
    if (width <= 0 || height <= 0 || qint64(width) * height != data.size()) {
        result.error = QStringLiteral("RAW 帧尺寸或数据无效");
        return result;
    }
    const int scale = metadata.value(QStringLiteral("BAYERPAT")).toString().trimmed().isEmpty() ? 1 : 2;
    const int w = width / scale;
    const int h = height / scale;
    if (w < 16 || h < 16) {
        result.error = QStringLiteral("RAW 帧太小，无法测量恒星");
        return result;
    }
    double saturation = metadata.value(QStringLiteral("DATAMAX")).toDouble();
    if (!std::isfinite(saturation) || saturation <= 0.0 || saturation > 65535.0) {
        const int bits = metadata.value(QStringLiteral("RAWBITS"), 16).toInt();
        saturation = bits > 0 && bits <= 16 ? double((1u << bits) - 1u) : 65535.0;
    }

    QVector<float> image(qsizetype(w) * h);
    QVector<bool> saturated(image.size(), false);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double sum = 0.0;
            const qsizetype index = qsizetype(y) * w + x;
            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    const auto value = data[qsizetype(y * scale + dy) * width + x * scale + dx];
                    sum += value;
                    saturated[index] = saturated[index] || value >= saturation * 0.98;
                }
            }
            // Equal CFA-cell sampling avoids detecting the Bayer checkerboard as separate stars.
            image[index] = float(sum / (scale * scale));
        }
    }

    QVector<double> backgroundPixels;
    const qsizetype stride = std::max<qsizetype>(1, image.size() / 32768);
    for (qsizetype i = 0; i < image.size(); i += stride)
        backgroundPixels.append(image[i]);
    const double background = median(backgroundPixels);
    const double noise = noiseEstimate(backgroundPixels, background);
    QVector<Candidate> candidates;
    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            const qsizetype index = qsizetype(y) * w + x;
            const double peak = image[index];
            if (peak < background + 8.0 * noise || saturated[index])
                continue;
            bool maximum = true;
            int support = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0)
                        continue;
                    const qsizetype neighbor = qsizetype(y + dy) * w + x + dx;
                    if (image[neighbor] > peak || (image[neighbor] == peak && neighbor < index))
                        maximum = false;
                    if (image[neighbor] - background > std::max(3.0 * noise, (peak - background) * 0.1))
                        ++support;
                }
            }
            if (maximum && support >= 4)
                candidates.append({x, y, peak});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.peak > b.peak; });
    QVector<Star> stars;
    QVector<double> hfrs;
    const qsizetype limit = std::min<qsizetype>(candidates.size(), 1024);
    for (qsizetype i = 0; i < limit && stars.size() < 128; ++i) {
        const auto &candidate = candidates[i];
        bool overlaps = false;
        for (const auto &star : stars) {
            if (std::hypot(candidate.x - star.x, candidate.y - star.y) < star.radius) {
                overlaps = true;
                break;
            }
        }
        Star star;
        if (!overlaps && measureStar(image, saturated, w, h, scale, candidate, noise, star)) {
            stars.append(star);
            hfrs.append(star.hfr);
        }
    }
    result.stars = int(stars.size());
    if (result.stars < 5) {
        result.error = QStringLiteral("可用恒星不足 5 颗（检测到 %1 颗）").arg(result.stars);
        return result;
    }
    result.hfr = median(hfrs);
    return result;
}

Fit fit(const QVector<Sample> &samples)
{
    Fit result;
    QVector<Sample> valid;
    for (const auto &sample : samples) {
        if (sample.stars >= 5 && std::isfinite(sample.hfr) && sample.hfr > 0.0)
            valid.append(sample);
    }
    if (valid.size() < 7) {
        result.error = QStringLiteral("拟合需要至少 7 个有效采样点");
        return result;
    }
    std::sort(valid.begin(), valid.end(), [](const Sample &a, const Sample &b) { return a.position < b.position; });
    for (qsizetype i = 1; i < valid.size(); ++i) {
        if (valid[i].position == valid[i - 1].position) {
            result.error = QStringLiteral("拟合采样位置重复");
            return result;
        }
    }
    const double center = (double(valid.first().position) + valid.last().position) * 0.5;
    const double scale = (double(valid.last().position) - valid.first().position) * 0.5;
    double matrix[3][4] = {};
    for (const auto &sample : valid) {
        const double x = (sample.position - center) / scale;
        const double terms[3] = {x * x, x, 1.0};
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column)
                matrix[row][column] += terms[row] * terms[column];
            matrix[row][3] += terms[row] * sample.hfr * sample.hfr;
        }
    }
    for (int column = 0; column < 3; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 3; ++row) {
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column]))
                pivot = row;
        }
        if (std::abs(matrix[pivot][column]) < 1e-12) {
            result.error = QStringLiteral("采样分布无法可靠拟合");
            return result;
        }
        for (int j = column; j < 4; ++j)
            std::swap(matrix[column][j], matrix[pivot][j]);
        const double divisor = matrix[column][column];
        for (int j = column; j < 4; ++j)
            matrix[column][j] /= divisor;
        for (int row = 0; row < 3; ++row) {
            if (row == column)
                continue;
            const double factor = matrix[row][column];
            for (int j = column; j < 4; ++j)
                matrix[row][j] -= factor * matrix[column][j];
        }
    }
    const double a = matrix[0][3];
    const double b = matrix[1][3];
    const double c = matrix[2][3];
    const double minimum = -b / (2.0 * a);
    const double minimumSquared = c - b * b / (4.0 * a);
    if (!std::isfinite(a) || a <= 0.0 || !std::isfinite(minimumSquared) || minimumSquared <= 0.0) {
        result.error = QStringLiteral("拟合曲线没有有效的正曲率最小值");
        return result;
    }
    const double position = center + minimum * scale;
    int left = 0;
    int right = 0;
    double leftMax = 0.0;
    double rightMax = 0.0;
    double best = std::numeric_limits<double>::infinity();
    double mean = 0.0;
    for (const auto &sample : valid) {
        if (sample.position < position) {
            ++left;
            leftMax = std::max(leftMax, sample.hfr);
        }
        if (sample.position > position) {
            ++right;
            rightMax = std::max(rightMax, sample.hfr);
        }
        best = std::min(best, sample.hfr);
        mean += sample.hfr;
    }
    if (left < 2 || right < 2 || leftMax < best * 1.15 || rightMax < best * 1.15) {
        result.error = QStringLiteral("最低点未被充分包围，或两侧 HFR 增幅不足 15%");
        return result;
    }
    mean /= valid.size();
    double residual = 0.0;
    double variation = 0.0;
    for (const auto &sample : valid) {
        const double x = (sample.position - center) / scale;
        const double predicted = std::sqrt(a * (x - minimum) * (x - minimum) + minimumSquared);
        residual += (sample.hfr - predicted) * (sample.hfr - predicted);
        variation += (sample.hfr - mean) * (sample.hfr - mean);
    }
    const double rSquared = variation > 0.0 ? 1.0 - residual / variation : 0.0;
    if (!std::isfinite(rSquared) || rSquared < 0.8) {
        result.error = QStringLiteral("HFR 拟合质量不足（R² < 0.8）");
        return result;
    }
    result.valid = true;
    result.position = position;
    result.hfr = std::sqrt(minimumSquared);
    result.rSquared = rSquared;
    for (int i = 0; i <= 100; ++i) {
        const double x = -1.0 + i * 0.02;
        result.curve.append(QPointF(center + x * scale, std::sqrt(a * (x - minimum) * (x - minimum) + minimumSquared)));
    }
    return result;
}

}
