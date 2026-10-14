#pragma once

#include <QPointF>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <limits>

namespace FocusMetrics {

struct Measurement {
    double hfr = std::numeric_limits<double>::quiet_NaN();
    int stars = 0;
    QString error;
};

struct Sample {
    int position = 0;
    double hfr = std::numeric_limits<double>::quiet_NaN();
    int stars = 0;
};

struct Fit {
    bool valid = false;
    double position = std::numeric_limits<double>::quiet_NaN();
    double hfr = std::numeric_limits<double>::quiet_NaN();
    double rSquared = std::numeric_limits<double>::quiet_NaN();
    QVector<QPointF> curve;
    QString error;
};

Measurement measure(const QVector<quint16> &data, int width, int height,
                    const QVariantMap &metadata = {});
Fit fit(const QVector<Sample> &samples);

}
