#pragma once
#include <QFile>
#include <QMap>
#include <QSize>
#include <QPointF>
#include <QVariantMap>
#include <QtMath>
#include <cmath>

namespace PlateSolveResult {
inline QSize fitsSize(const QString &path, QString *error) {
    QFile file(path);
    auto fail = [&] { *error = QStringLiteral("需要完整、未压缩的二维 FITS 主图像"); return QSize(); };
    if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return {}; }
    QMap<QByteArray, QByteArray> cards;
    for (int i = 0; i < 13104; ++i) {
        const auto card = file.read(80);
        if (card.size() != 80) return fail();
        const auto key = card.first(8).trimmed();
        if (key == "END") {
            bool wOk, hOk, bOk;
            const int w = cards["NAXIS1"].toInt(&wOk), h = cards["NAXIS2"].toInt(&hOk);
            const int bits = cards["BITPIX"].toInt(&bOk);
            if (cards["SIMPLE"] != "T" || cards["NAXIS"] != "2" || !wOk || !hOk || !bOk
                || w <= 0 || h <= 0 || w > 100000 || h > 100000
                || !QList<int>{8,16,32,64,-32,-64}.contains(bits)) return fail();
            const qint64 header = ((file.pos()+2879)/2880)*2880;
            if (file.size() < header+qint64(w)*h*(std::abs(bits)/8)) return fail();
            return {w,h};
        }
        if (card[8] == '=') cards.insert(key, card.mid(10).split('/').first().trimmed());
    }
    return fail();
}

inline QVariantMap fromAstap(const QByteArray &ini, QSize size, QString *error) {
    QMap<QByteArray,QByteArray> fields;
    for (const auto &line : ini.split('\n')) {
        const auto eq = line.indexOf('=');
        if (eq > 0) fields.insert(line.first(eq).trimmed(), line.mid(eq+1).trimmed());
    }
    auto fail = [&](QString message) -> QVariantMap { *error = message; return {}; };
    if (fields.value("PLTSOLVD") != "T")
        return fail(QStringLiteral("未找到解析解：") + QString::fromUtf8(fields.value("ERROR")));
    double v[8];
    const QList<QByteArray> keys{"CRVAL1","CRVAL2","CRPIX1","CRPIX2","CD1_1","CD1_2","CD2_1","CD2_2"};
    for (int i=0; i<8; ++i) {
        bool ok;
        auto number = fields.value(keys[i]); number.replace('D','E');
        v[i] = number.toDouble(&ok);
        if (!ok || !std::isfinite(v[i])) return fail(QStringLiteral("解析结果缺少有效 WCS：")+QString::fromLatin1(keys[i]));
    }
    if (size.isEmpty() || v[0]<0 || v[0]>=360 || std::abs(v[1])>90
        || v[2]<0.5 || v[2]>size.width()+0.5 || v[3]<0.5 || v[3]>size.height()+0.5
        || !std::isfinite(v[4]*v[7]-v[5]*v[6])
        || std::abs(v[4]*v[7]-v[5]*v[6])<1e-18
        || std::hypot(v[4],v[6])*size.width()>180 || std::hypot(v[5],v[7])*size.height()>180)
        return fail(QStringLiteral("解析结果 WCS 范围或矩阵无效"));
    // FITS WCS TAN inverse projection, with the full CD matrix preserving parity.
    // Reference: Calabretta & Greisen 2002, A&A 395, 1077 (FITS WCS Paper II).
    auto sky = [&](double x, double y) -> QVariantList {
        const double xi = qDegreesToRadians(v[4]*(x-v[2])+v[5]*(y-v[3]));
        const double eta = qDegreesToRadians(v[6]*(x-v[2])+v[7]*(y-v[3]));
        const double dec0 = qDegreesToRadians(v[1]);
        const double base = std::cos(dec0)-eta*std::sin(dec0);
        double ra = v[0]+qRadiansToDegrees(std::atan2(xi,base));
        ra = std::fmod(ra+720.0,360.0);
        const double dec = qRadiansToDegrees(std::atan2(std::sin(dec0)+eta*std::cos(dec0),std::hypot(base,xi)));
        return {ra,dec};
    };
    QVariantList corners;
    for (const auto &p : {QPointF(0.5,0.5),QPointF(size.width()+0.5,0.5),
                         QPointF(size.width()+0.5,size.height()+0.5),QPointF(0.5,size.height()+0.5)})
        corners.append(QVariant(sky(p.x(),p.y())));
    const auto center = sky((size.width()+1)/2.0,(size.height()+1)/2.0);
    const double sx = std::hypot(v[4],v[6]), sy = std::hypot(v[5],v[7]);
    return {{"raHours",center[0].toDouble()/15.0},{"decDeg",center[1]}, {"corners",corners},
        {"widthDeg",qRadiansToDegrees(2*std::atan(qDegreesToRadians(sx*size.width())/2))},
        {"heightDeg",qRadiansToDegrees(2*std::atan(qDegreesToRadians(sy*size.height())/2))},
        {"pixelScale",sy*3600}, {"positionAngle",std::fmod(qRadiansToDegrees(std::atan2(v[5],v[7]))+360,360)},
        {"warning",QString::fromUtf8(fields.value("WARNING"))}};
}
}
