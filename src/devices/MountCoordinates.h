#pragma once
#include "AscomDispatch.h"
#include <QtMath>
#include <QVariantMap>

namespace MountCoordinates {
inline bool valid(double ra, double dec) {
    return qIsFinite(ra) && qIsFinite(dec) && ra >= 0 && ra < 24 && dec >= -90 && dec <= 90;
}

inline QString gotoStateError(const QVariantMap &state) {
    for (const auto &key : {"AtPark", "Slewing", "IsPulseGuiding"})
        if (!state.value(key).isValid() || state.value(key).toBool())
            return QStringLiteral("停放、运动、导星或状态未知，不能指向");
    if (!state.value("Tracking").isValid()) return QStringLiteral("跟踪状态未知，不能指向");
    if (!state.value("Tracking").toBool() && !state.value("CanSetTracking").toBool())
        return QStringLiteral("跟踪未开启且驱动不支持开启跟踪，不能指向");
    if (!state.value("CanSlewAsync").toBool()) return QStringLiteral("驱动不支持异步目标指向");
    return {};
}

// ASCOM Transform handles precession, nutation and aberration in both directions.
inline QString convert(double ra, double dec, int system, const QVariantMap &site,
                       bool toDriver, double &outRa, double &outDec, double &altitude) {
    if (!valid(ra, dec) || (system != 1 && system != 2))
        return QStringLiteral("坐标无效或驱动坐标系不受支持（需要 Topocentric / J2000）");
    for (const auto &key : {"siteLatitude", "siteLongitude", "siteElevation"}) {
        bool ok = false;
        const double value = site.value(key).toDouble(&ok);
        if (!ok || !qIsFinite(value)) return QStringLiteral("赤道仪站点数据缺失，无法转换坐标");
    }
    if (qAbs(site.value("siteLatitude").toDouble()) > 90 || qAbs(site.value("siteLongitude").toDouble()) > 180)
        return QStringLiteral("赤道仪站点经纬度无效");
    AscomDispatch transform;
    QString error;
    if (!transform.create("ASCOM.Astrometry.Transform.Transform", error)) return error;
    for (const auto &pair : {qMakePair("SiteLatitude", "siteLatitude"), qMakePair("SiteLongitude", "siteLongitude"),
                            qMakePair("SiteElevation", "siteElevation")})
        if (!transform.put(pair.first, site.value(pair.second))) return transform.lastError();
    if (!transform.put("Refraction", false)) return transform.lastError();
    transform.call(toDriver || system == 2 ? "SetJ2000" : "SetTopocentric", {ra, dec});
    if (!transform.lastError().isEmpty()) return transform.lastError();
    const auto r = transform.get(toDriver && system == 1 ? "RATopocentric" : "RAJ2000");
    const auto d = transform.get(toDriver && system == 1 ? "DECTopocentric" : "DecJ2000");
    const auto a = transform.get("ElevationTopocentric");
    bool rok = false, dok = false, aok = false;
    outRa = r.toDouble(&rok); outDec = d.toDouble(&dok); altitude = a.toDouble(&aok);
    if (!rok || !dok || !aok || !valid(outRa, outDec) || !qIsFinite(altitude))
        return QStringLiteral("ASCOM 坐标转换失败");
    return {};
}
}
