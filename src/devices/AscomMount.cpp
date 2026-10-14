#include "AscomMount.h"
#include "AscomDispatch.h"
#include "MountCoordinates.h"
#include <QDateTime>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#include <QDir>
#include <vector>
#endif

#ifdef Q_OS_WIN
namespace {
// ASCOM Platform version (e.g. "7.1") lives in the registry; COM drivers
// register under WOW6432Node, newer native installs under the native view.
// The driver's own DriverVersion (e.g. "6.5") is a separate, lower number.
QString ascomPlatformVersion()
{
    for (const wchar_t *const location : {L"SOFTWARE\\ASCOM", L"SOFTWARE\\WOW6432Node\\ASCOM"}) {
        wchar_t buffer[64]{};
        DWORD size = sizeof(buffer);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, location, L"PlatformVersion",
                         RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS)
            return QString::fromWCharArray(buffer);
    }
    return QString();
}

QString registryString(HKEY root, const std::wstring &subkey, const wchar_t *value)
{
    wchar_t buffer[MAX_PATH]{};
    DWORD size = sizeof(buffer);
    // An empty name reads the key's default value; nullptr is not guaranteed to.
    const wchar_t *const name = value ? value : L"";
    if (RegGetValueW(root, subkey.c_str(), name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
        return QString();
    return QString::fromWCharArray(buffer);
}

// NINA displays the ASCOM driver's ProductVersion ("6.5.32687.20260919").
// Resolution chain: ProgId -> CLSID -> InprocServer32 CodeBase (file URL for
// .NET hosts) or the native server path, then the file's version resource —
// string table first (NINA's value), numeric fields as fallback. Each failed
// stage logs to qWarning so a missing value is diagnosable in the IDE output.
QString ascomDriverProductVersion(const QString &progId)
{
    const std::wstring clsidKey = progId.toStdWString() + L"\\CLSID";
    const QString clsid = registryString(HKEY_CLASSES_ROOT, clsidKey, nullptr);
    if (clsid.isEmpty()) {
        qWarning("ascomDriverProductVersion: no CLSID for %ls", reinterpret_cast<const wchar_t *>(progId.utf16()));
        return QString();
    }
    const std::wstring serverKey = (L"CLSID\\" + clsid.toStdWString() + L"\\InprocServer32");
    QString serverPath = registryString(HKEY_CLASSES_ROOT, serverKey, L"CodeBase");
    if (serverPath.isEmpty())
        serverPath = registryString(HKEY_CLASSES_ROOT, serverKey, nullptr);
    if (serverPath.isEmpty())
        serverPath = registryString(HKEY_CLASSES_ROOT,
                                    L"CLSID\\" + clsid.toStdWString() + L"\\LocalServer32", nullptr);
    if (serverPath.startsWith(QStringLiteral("file:///")))
        serverPath = serverPath.mid(8).replace(QStringLiteral("%20"), QStringLiteral(" "));
    if (serverPath.isEmpty()) {
        qWarning("ascomDriverProductVersion: no server path for %ls", reinterpret_cast<const wchar_t *>(clsid.utf16()));
        return QString();
    }

    const std::wstring nativePath = QDir::toNativeSeparators(serverPath).toStdWString();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(nativePath.c_str(), &handle);
    if (size == 0) {
        qWarning("ascomDriverProductVersion: version info size failed for %ls (gle=%lu)",
                 nativePath.c_str(), GetLastError());
        return QString();
    }
    std::vector<unsigned char> buffer(size);
    if (!GetFileVersionInfoW(nativePath.c_str(), 0, size, buffer.data())) {
        qWarning("ascomDriverProductVersion: version info read failed (gle=%lu)", GetLastError());
        return QString();
    }
    // String table carries the NINA-visible value ("6.5.32687.20260919");
    // its language/codepage is addressed through the translation table.
    struct Translation { unsigned short language; unsigned short codePage; };
    Translation *translation = nullptr;
    unsigned translationSize = 0;
    if (VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation",
                       reinterpret_cast<LPVOID *>(&translation), &translationSize) && translationSize >= sizeof(Translation)) {
        wchar_t query[64]{};
        swprintf(query, 64, L"\\StringFileInfo\\%04x%04x\\ProductVersion",
                 translation->language, translation->codePage);
        wchar_t *text = nullptr;
        unsigned textSize = 0;
        if (VerQueryValueW(buffer.data(), query, reinterpret_cast<LPVOID *>(&text), &textSize) && text && *text)
            return QString::fromWCharArray(text);
    }
    VS_FIXEDFILEINFO *info = nullptr;
    unsigned infoSize = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<LPVOID *>(&info), &infoSize)
        || infoSize < sizeof(VS_FIXEDFILEINFO)) {
        qWarning("ascomDriverProductVersion: no version fields in %ls", nativePath.c_str());
        return QString();
    }
    return QStringLiteral("%1.%2.%3.%4")
        .arg(HIWORD(info->dwProductVersionMS)).arg(LOWORD(info->dwProductVersionMS))
        .arg(HIWORD(info->dwProductVersionLS)).arg(LOWORD(info->dwProductVersionLS));
}
}
#endif

AscomMount::AscomMount(QObject *parent)
    : MountBackend(parent)
    , m_pollTimer(new QTimer(this))
    , m_watchdogTimer(new QTimer(this))
{
    // Children of this QObject: moveToThread carries them together, so
    // start() always runs on the owning worker thread (QTimer demands it).
    connect(m_pollTimer, &QTimer::timeout, this, &AscomMount::pollState);
    m_pollTimer->setInterval(1000);
    connect(m_watchdogTimer, &QTimer::timeout, this, &AscomMount::checkConnection);
    m_watchdogTimer->setInterval(3000);
}

void AscomMount::scan()
{
    QVariantList mounts;
#ifdef Q_OS_WIN
    // Enumerate ASCOM Telescope drivers from the registry (what NINA does
    // via ASCOM.Com.Profile.GetDrivers — same tree, direct read).
    HKEY root{};
    const auto key = LR"(SOFTWARE\ASCOM\Telescope)";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ | KEY_WOW64_32KEY, &root) == ERROR_SUCCESS
        || RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &root) == ERROR_SUCCESS) {
        DWORD index = 0;
        wchar_t name[256]{};
        DWORD nameLen = 256;
        while (RegEnumKeyExW(root, index++, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            const QString progId = QString::fromWCharArray(name);
            // Read the friendly name from the sub-key.
            QString friendly = progId;
            HKEY sub{};
            const QString subPath = QStringLiteral("SOFTWARE\\ASCOM\\Telescope\\%1").arg(progId);
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                              reinterpret_cast<LPCWSTR>(subPath.utf16()), 0,
                              KEY_READ, &sub) == ERROR_SUCCESS) {
                wchar_t display[256]{};
                DWORD displayLen = sizeof(display);
                if (RegQueryValueExW(sub, L"Driver Name", nullptr, nullptr,
                                     reinterpret_cast<LPBYTE>(display), &displayLen) == ERROR_SUCCESS)
                    friendly = QString::fromWCharArray(display);
                else if (RegQueryValueExW(sub, L"FriendlyName", nullptr, nullptr,
                                          reinterpret_cast<LPBYTE>(display), &displayLen) == ERROR_SUCCESS)
                    friendly = QString::fromWCharArray(display);
                RegCloseKey(sub);
            }
            mounts.append(QVariantMap{{"id", progId}, {"name", friendly}});
            nameLen = 256;
        }
        RegCloseKey(root);
    }
#endif
    // Fallback: many drivers (e.g. TMount) register their COM ProgID but
    // don't populate the ASCOM profile tree. Scan Classes for ASCOM.*.Mount.
    if (mounts.isEmpty()) {
        HKEY classes{};
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Classes)", 0,
                          KEY_READ, &classes) == ERROR_SUCCESS) {
            DWORD index = 0;
            wchar_t name[256]{};
            DWORD nameLen = 256;
            while (RegEnumKeyExW(classes, index++, name, &nameLen, nullptr,
                                 nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                const QString progId = QString::fromWCharArray(name);
                if (progId.startsWith(QStringLiteral("ASCOM.")) && progId.endsWith(QStringLiteral(".Mount"))) {
                    mounts.append(QVariantMap{{"id", progId}, {"name", progId}});
                }
                nameLen = 256;
            }
            RegCloseKey(classes);
        }
    }
    emit scanFinished(mounts, {});
}

void AscomMount::open(const QString &progId)
{
#ifdef Q_OS_WIN
    // COM must be initialized on EVERY thread that uses it; this method
    // runs on the mount worker thread, not the main thread.
    HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_comInitialized = SUCCEEDED(comInit);
    auto *dispatch = new AscomDispatch();
    if (!dispatch->create(progId, m_error)) {
        delete dispatch;
        emit connectionFinished(false, {}, m_error);
        return;
    }
    m_dispatch = dispatch;
    if (!dispatch->put("Connected", true)) {
        m_error = dispatch->lastError();
        emit connectionFinished(false, {}, m_error);
        return;
    }
    m_progId = progId;
    m_connected = true;
    m_error.clear();

    QVariantMap info;
    info["progId"] = progId;
    if (auto v = dispatch->get("Name"); v.isValid()) { m_name = v.toString(); info["name"] = m_name; }
    else m_name = progId;
    if (auto v = dispatch->get("DriverVersion"); v.isValid()) info["driverVersion"] = v.toString();
    if (auto v = dispatch->get("DriverInfo"); v.isValid()) info["driverInfo"] = v.toString();
#ifdef Q_OS_WIN
    info["platformVersion"] = ascomPlatformVersion();
    info["driverProductVersion"] = ascomDriverProductVersion(progId);
#endif
    if (auto v = dispatch->get("CanPulseGuide"); v.isValid()) info["canPulseGuide"] = v.toBool();
    if (auto v = dispatch->get("CanPark"); v.isValid()) info["canPark"] = v.toBool();
    if (auto v = dispatch->get("CanFindHome"); v.isValid()) { m_canFindHome = v.toBool(); info["canFindHome"] = m_canFindHome; }
    if (auto v = dispatch->get("CanSetPark"); v.isValid()) { m_canSetPark = v.toBool(); info["canSetPark"] = m_canSetPark; }
    if (auto v = dispatch->get("CanUnpark"); v.isValid()) info["canUnpark"] = v.toBool();
    if (auto v = dispatch->get("CanSetGuideRates"); v.isValid()) info["canSetGuideRates"] = v.toBool();
    if (auto v = dispatch->get("CanSlew"); v.isValid()) info["canSlew"] = v.toBool();
    // This driver's declared AxisRates range is 0..6 deg/s on both axes.
    info["canManualMove"] = progId == QStringLiteral("ASCOM.TMount.Mount")
        && dispatch->call("CanMoveAxis", {0}).toBool()
        && dispatch->call("CanMoveAxis", {1}).toBool();
    if (auto v = dispatch->get("SiteLatitude"); v.isValid()) info["siteLatitude"] = v.toDouble();
    if (auto v = dispatch->get("SiteLongitude"); v.isValid()) info["siteLongitude"] = v.toDouble();

    m_expectConnected = true;
    m_pollTimer->start();
    m_watchdogTimer->start();
    emit connectionFinished(true, info, {});
#else
    Q_UNUSED(progId)
    emit connectionFinished(false, {}, QStringLiteral("ASCOM 仅支持 Windows 平台"));
#endif
}

void AscomMount::setup(const QString &id)
{
#ifdef Q_OS_WIN
    // ASCOM SetupDialog is a blocking modal call — run on a temporary
    // instance (the driver's own dialog, not ours).
    auto *dispatch = new AscomDispatch();
    QString error;
    if (!dispatch->create(id, error)) {
        delete dispatch;
        emit setupFinished(error);
        return;
    }
    dispatch->call("SetupDialog");
    delete dispatch;
    emit setupFinished({});
#else
    Q_UNUSED(id)
    emit setupFinished(QStringLiteral("ASCOM 仅支持 Windows 平台"));
#endif
}

void AscomMount::close()
{
    if (m_gotoActive) stopSlew();
    m_gotoActive = false;
    if (m_manualMoving) stopManualMove();
    // Mark that WE are initiating the disconnect, so the watchdog doesn't
    // try to reconnect during the shutdown sequence.
    m_expectConnected = false;
    m_pollTimer->stop();
    m_watchdogTimer->stop();
#ifdef Q_OS_WIN
    if (auto *dispatch = static_cast<AscomDispatch *>(m_dispatch)) {
        if (m_connected) dispatch->put("Connected", false);
        delete dispatch;
    }
    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }
#endif
    m_dispatch = nullptr;
    m_connected = false;
    m_progId.clear();
    emit connectionFinished(false, {}, {});
}

bool AscomMount::setTracking(bool on)
{
    if (!m_connected) { m_error = QStringLiteral("赤道仪未连接"); return false; }
    auto *dispatch = static_cast<AscomDispatch *>(m_dispatch);
    // Some drivers refuse Tracking=true without a TrackingRate already set;
    // write the current rate back first (read-modify-write, harmless if set).
    if (on) {
        QVariant rate = dispatch->get("TrackingRate");
        if (rate.isValid()) dispatch->put("TrackingRate", rate);
    }
    if (!dispatch->put("Tracking", on)) {
        m_error = dispatch->lastError();
        return false;
    }
    return true;
}

bool AscomMount::unpark()
{
    if (!m_connected) { m_error = QStringLiteral("赤道仪未连接"); return false; }
    auto *dispatch = static_cast<AscomDispatch *>(m_dispatch);
    dispatch->call("Unpark");
    return true;
}

bool AscomMount::setTrackingRate(int rate)
{
    if (!m_connected) { m_error = QStringLiteral("赤道仪未连接"); return false; }
    auto *dispatch = static_cast<AscomDispatch *>(m_dispatch);
    return dispatch->put("TrackingRate", rate);
}

QVariant AscomMount::trackingRate() const
{
    if (!m_connected) return {};
    return static_cast<AscomDispatch *>(m_dispatch)->get("TrackingRate");
}

QVariant AscomMount::guideRateRa() const
{
    if (!m_connected) return {};
    const auto rate = static_cast<AscomDispatch *>(m_dispatch)->get("GuideRateRightAscension");
    return rate.isValid() ? QVariant(guideRateFromDegrees(rate.toDouble())) : QVariant{};
}

QVariant AscomMount::guideRateDec() const
{
    if (!m_connected) return {};
    const auto rate = static_cast<AscomDispatch *>(m_dispatch)->get("GuideRateDeclination");
    return rate.isValid() ? QVariant(guideRateFromDegrees(rate.toDouble())) : QVariant{};
}

bool AscomMount::findHome()
{
    if (!m_connected) { m_error = QStringLiteral("赤道仪未连接"); return false; }
    auto *dispatch = static_cast<AscomDispatch *>(m_dispatch);
    // Check capability first.
    QVariant canFindHome = dispatch->get("CanFindHome");
    if (canFindHome.isValid() && !canFindHome.toBool()) {
        m_error = QStringLiteral("该赤道仪不支持寻找零位");
        return false;
    }
    dispatch->call("FindHome");
    return true;
}

bool AscomMount::setCurrentPark()
{
    if (!m_connected) { m_error = QStringLiteral("赤道仪未连接"); return false; }
    auto *dispatch = static_cast<AscomDispatch *>(m_dispatch);
    QVariant canSetPark = dispatch->get("CanSetPark");
    if (canSetPark.isValid() && !canSetPark.toBool()) {
        m_error = QStringLiteral("该赤道仪不支持设置停放位");
        return false;
    }
    dispatch->call("SetPark");
    return true;
}

bool AscomMount::setGuideRates(double raRate, double decRate)
{
    if (!m_connected) { m_error = QStringLiteral("赤道仪未连接"); return false; }
    if (!qIsFinite(raRate) || !qIsFinite(decRate) || raRate < 0.1 || raRate > 0.9 || decRate < 0.1 || decRate > 0.9) {
        m_error = QStringLiteral("导星速率需在 0.1 ~ 0.9 倍恒星速之间");
        return false;
    }
    const bool sharedRate = m_progId == QStringLiteral("ASCOM.TMount.Mount");
    if (sharedRate && !qFuzzyCompare(raRate, decRate)) {
        m_error = QStringLiteral("TMount 的 RA / Dec 共用导星速率");
        return false;
    }
    auto *dispatch = static_cast<AscomDispatch *>(m_dispatch);
    QVariant canSet = dispatch->get("CanSetGuideRates");
    if (!canSet.isValid() || !canSet.toBool()) {
        m_error = QStringLiteral("该赤道仪不支持设置导星速率");
        return false;
    }
    bool ok = dispatch->put("GuideRateRightAscension", guideRateToDegrees(raRate));
    if (ok && !sharedRate) ok = dispatch->put("GuideRateDeclination", guideRateToDegrees(decRate));
    if (!ok) m_error = dispatch->lastError();
    return ok;
}

bool AscomMount::park()
{
    if (!m_connected) { m_error = "赤道仪未连接"; return false; }
    auto *dispatch = static_cast<AscomDispatch *>(m_dispatch);
    if (!canPark()) { m_error = QStringLiteral("赤道仪不支持停放"); return false; }
    dispatch->call("Park");
    return true;
}

bool AscomMount::abortSlew()
{
    if (!m_connected) { m_error = "赤道仪未连接"; return false; }
    auto *dispatch = static_cast<AscomDispatch *>(m_dispatch);
    dispatch->call("AbortSlew");
    m_error = dispatch->lastError();
    if (!m_error.isEmpty()) return false;
    m_gotoActive = false;
    auto state = stateMap();
    state["gotoState"] = "Stopped";
    // AbortSlew acceptance is not proof that deceleration has finished.
    if (!state.contains("slewing")) state["slewing"] = QVariant();
    emit stateChanged(state);
    m_manualMoving = false;
    if (m_manualTimer) m_manualTimer->stop();
    return true;
}

QString AscomMount::stopSlew()
{
    return abortSlew() ? QString{} : m_error;
}

QString AscomMount::slewToJ2000(double ra, double dec)
{
    if (!m_connected || m_manualMoving || m_gotoActive || !MountCoordinates::valid(ra, dec))
        return QStringLiteral("当前连接、运动状态或目标坐标不允许指向");
    auto *dispatch = static_cast<AscomDispatch *>(m_dispatch);
    QVariantMap motion;
    for (const auto &key : {"AtPark", "Slewing", "IsPulseGuiding", "Tracking", "CanSetTracking", "CanSlewAsync"})
        motion[key] = dispatch->get(key);
    const QString motionError = MountCoordinates::gotoStateError(motion);
    if (!motionError.isEmpty()) return motionError;
    QVariantMap site;
    site["siteLatitude"] = dispatch->get("SiteLatitude");
    site["siteLongitude"] = dispatch->get("SiteLongitude");
    site["siteElevation"] = dispatch->get("SiteElevation");
    double targetRa, targetDec, altitude;
    const QString error = MountCoordinates::convert(ra, dec, dispatch->get("EquatorialSystem").toInt(),
                                                    site, true, targetRa, targetDec, altitude);
    if (!error.isEmpty()) return error;
    if (altitude < 10) return QStringLiteral("目标当前高度低于 10°，已拒绝指向");
    // Enable only after all safety checks; a rejected target must not start tracking.
    if (!motion.value("Tracking").toBool()) {
        if (!dispatch->put("Tracking", true)) return dispatch->lastError();
        const auto tracking = dispatch->get("Tracking");
        if (!tracking.isValid() || !tracking.toBool())
            return QStringLiteral("开启跟踪后未确认成功，已取消指向，请检查驱动状态");
        emit stateChanged({{"tracking", true}});
    }
    dispatch->call("SlewToCoordinatesAsync", {targetRa, targetDec});
    if (!dispatch->lastError().isEmpty()) return dispatch->lastError();
    m_gotoRa = targetRa; m_gotoDec = targetDec;
    m_gotoActive = true; m_gotoElapsed.start();
    emit stateChanged({{"gotoState", "Slewing"}});
    return {};
}

QString AscomMount::startManualMove(int axis, double rate)
{
    if (!m_connected || m_progId != QStringLiteral("ASCOM.TMount.Mount"))
        return QStringLiteral("当前驱动未开放已验证的手动控制");
    if (axis < 0 || axis > 1 || !qIsFinite(rate) || qAbs(rate) < 0.25 || qAbs(rate) > 1440 || m_manualMoving)
        return QStringLiteral("手动移动参数无效或已有移动正在执行");
    auto *dispatch = static_cast<AscomDispatch *>(m_dispatch);
    const auto parked = dispatch->get("AtPark");
    const auto slewing = dispatch->get("Slewing");
    const auto pulsing = dispatch->get("IsPulseGuiding");
    if (!parked.isValid() || !slewing.isValid() || !pulsing.isValid()
        || parked.toBool() || slewing.toBool() || pulsing.toBool())
        return QStringLiteral("停放、运动、导星或状态未知时不能手动移动");
    if (!m_manualTimer) {
        m_manualTimer = new QTimer(this);
        m_manualTimer->setSingleShot(true);
        m_manualTimer->setInterval(1000);
        connect(m_manualTimer, &QTimer::timeout, this, [this] {
            const QString error = stopManualMove();
            emit stateChanged({{"manualMoving", m_manualMoving}, {"manualError", error}});
        });
    }
    // Arm before the command: even an uncertain COM result must be followed by STOP.
    m_manualMoving = true;
    m_manualTimer->start();
    dispatch->call("MoveAxis", {axis, guideRateToDegrees(rate)});
    const QString error = dispatch->lastError();
    if (!error.isEmpty()) stopManualMove();
    return error;
}

QString AscomMount::syncObserver(bool timeOnly, double longitude, double latitude)
{
    if (!m_connected || m_progId != QStringLiteral("ASCOM.TMount.Mount") || m_manualMoving)
        return QStringLiteral("当前驱动或运动状态不允许同步");
    if (!timeOnly && (!qIsFinite(longitude) || !qIsFinite(latitude)
                     || qAbs(longitude) > 180 || qAbs(latitude) > 90))
        return QStringLiteral("经纬度超出范围");
    auto *dispatch = static_cast<AscomDispatch *>(m_dispatch);
    const auto slewing = dispatch->get("Slewing");
    const auto guiding = dispatch->get("IsPulseGuiding");
    if (!slewing.isValid() || !guiding.isValid() || slewing.toBool() || guiding.toBool())
        return QStringLiteral("运动、导星或状态未知时不能同步");
    QStringList errors;
    if (timeOnly) {
        if (!dispatch->put("UTCDate", QDateTime::currentDateTimeUtc()))
            errors << dispatch->lastError();
        else {
            const auto readback = dispatch->get("UTCDate").toDateTime();
            if (!readback.isValid() || qAbs(readback.msecsTo(QDateTime::currentDateTimeUtc())) > 5000)
                errors << QStringLiteral("时间已写入，但回读核验失败（允许误差5秒）");
        }
    } else {
        const auto writeCoordinate = [&](const QString &name, double value) {
            if (!dispatch->put(name, value)) { errors << dispatch->lastError(); return; }
            const auto actual = dispatch->get(name);
            bool ok = false;
            const double measured = actual.toDouble(&ok);
            if (!ok || !qIsFinite(measured) || qAbs(measured - value) > 1.5 / 3600.0)
                errors << name + QStringLiteral(" 已写入，但回读核验失败（允许误差1.5角秒）");
        };
        writeCoordinate(QStringLiteral("SiteLongitude"), longitude);
        writeCoordinate(QStringLiteral("SiteLatitude"), latitude);
    }
    refreshNow();
    return errors.join(QStringLiteral("；"));
}

QString AscomMount::stopManualMove()
{
    if (!m_connected) return QStringLiteral("赤道仪未连接");
    if (!abortSlew()) {
        if (m_manualTimer && m_manualMoving) m_manualTimer->start();
        return m_error;
    }
    return {};
}

void AscomMount::keepManualMoveAlive()
{
    if (m_manualMoving && m_manualTimer) m_manualTimer->start();
}

bool AscomMount::pulseGuide(int direction, int durationMs)
{
    if (m_manualMoving) { m_error = QStringLiteral("手动移动中不能发送导星脉冲"); return false; }
    if (!m_connected) { m_error = "赤道仪未连接"; return false; }
    auto *dispatch = static_cast<AscomDispatch *>(m_dispatch);
    if (!canPulseGuide()) { m_error = "赤道仪不支持导星脉冲"; return false; }
    const QVariant result = dispatch->call("PulseGuide", { direction, durationMs });
    if (!result.isValid()) {
        m_error = dispatch->lastError();
        return false;
    }
    return true;
}

QVariant AscomMount::rightAscension() const
{
    if (!m_connected) return {};
    return static_cast<AscomDispatch *>(m_dispatch)->get("RightAscension");
}

QVariant AscomMount::declination() const
{
    if (!m_connected) return {};
    return static_cast<AscomDispatch *>(m_dispatch)->get("Declination");
}

QVariant AscomMount::siderealTime() const
{
    if (!m_connected) return {};
    return static_cast<AscomDispatch *>(m_dispatch)->get("SiderealTime");
}

QVariant AscomMount::siteLatitude() const
{
    if (!m_connected) return {};
    return static_cast<AscomDispatch *>(m_dispatch)->get("SiteLatitude");
}

QVariant AscomMount::siteLongitude() const
{
    if (!m_connected) return {};
    return static_cast<AscomDispatch *>(m_dispatch)->get("SiteLongitude");
}

bool AscomMount::isTracking() const
{
    if (!m_connected) return false;
    const QVariant v = static_cast<AscomDispatch *>(m_dispatch)->get("Tracking");
    return v.isValid() && v.toBool();
}

bool AscomMount::isSlewing() const
{
    if (!m_connected) return false;
    const QVariant v = static_cast<AscomDispatch *>(m_dispatch)->get("Slewing");
    return v.isValid() && v.toBool();
}

bool AscomMount::isAtPark() const
{
    if (!m_connected) return false;
    const QVariant v = static_cast<AscomDispatch *>(m_dispatch)->get("AtPark");
    return v.isValid() && v.toBool();
}

bool AscomMount::canPulseGuide() const
{
    if (!m_connected) return false;
    const QVariant v = static_cast<AscomDispatch *>(m_dispatch)->get("CanPulseGuide");
    return v.isValid() && v.toBool();
}

bool AscomMount::canPark() const
{
    if (!m_connected) return false;
    const QVariant v = static_cast<AscomDispatch *>(m_dispatch)->get("CanPark");
    return v.isValid() && v.toBool();
}

bool AscomMount::canSlew() const
{
    if (!m_connected) return false;
    const QVariant v = static_cast<AscomDispatch *>(m_dispatch)->get("CanSlew");
    return v.isValid() && v.toBool();
}

void AscomMount::refreshNow()
{
    if (!m_connected) return;
    emit stateChanged(stateMap());
}

void AscomMount::pollState()
{
    if (!m_connected) { m_pollTimer->stop(); return; }
    QVariantMap state = stateMap();
    if (m_gotoActive) {
        if (m_gotoElapsed.isValid() && m_gotoElapsed.elapsed() > 180000) {
            const auto error = stopSlew();
            state["gotoState"] = error.isEmpty() ? "Failed" : "StopFailed";
            state["gotoError"] = QStringLiteral("指向超过 180 秒，已请求停止。") + error;
            m_gotoActive = !error.isEmpty();
            m_gotoElapsed.invalidate();
        } else if (state.contains("slewing") && !state.value("slewing").toBool()) {
            const double dra = qDegreesToRadians((state.value("ra").toDouble() - m_gotoRa) * 15);
            const double d1 = qDegreesToRadians(state.value("dec").toDouble()), d2 = qDegreesToRadians(m_gotoDec);
            const double distance = qRadiansToDegrees(std::acos(std::clamp(std::sin(d1)*std::sin(d2)
                + std::cos(d1)*std::cos(d2)*std::cos(dra), -1.0, 1.0)));
            const bool arrived = state.contains("ra") && state.contains("dec") && qIsFinite(distance) && distance < 0.1;
            state["gotoState"] = arrived ? "Arrived" : "Failed";
            if (!arrived) state["gotoError"] = QStringLiteral("赤道仪停止，但坐标未到目标（容差 0.1°）");
            m_gotoActive = false;
        }
    }
    emit stateChanged(state);
}

void AscomMount::checkConnection()
{
    if (!m_connected || !m_expectConnected) { m_watchdogTimer->stop(); return; }
    auto *dispatch = static_cast<AscomDispatch *>(m_dispatch);
    const QVariant v = dispatch->get("Connected");
    if (v.isValid() && v.toBool()) {
        m_missedPolls = 0;
        return;
    }
    // The driver reports disconnected but WE expect to be connected — this
    // is the ASCOM UI disconnecting (not our user). Like NINA, immediately
    // try to reconnect instead of dropping the session.
    dispatch->put("Connected", true);
    QVariant retry = dispatch->get("Connected");
    if (retry.isValid() && retry.toBool()) {
        m_missedPolls = 0;
        return; // reconnected transparently
    }
    // Reconnection failed; only give up after multiple attempts.
    if (++m_missedPolls < 3) return;
    const QString error = QStringLiteral("赤道仪连接已丢失且无法恢复（ASCOM 驱动报告未连接）");
    m_error = error;
    close();
    emit connectionLost(error);
}

QVariantMap AscomMount::stateMap() const
{
    if (!m_connected) return {};
    auto *dispatch = static_cast<AscomDispatch *>(m_dispatch);
    QVariantMap state;
    state["skyRa"] = QVariant();
    state["skyDec"] = QVariant();
    if (auto v = dispatch->get("RightAscension"); v.isValid()) state["ra"] = v.toDouble();
    if (auto v = dispatch->get("Declination"); v.isValid()) state["dec"] = v.toDouble();
    state["slewing"] = dispatch->get("Slewing");
    state["tracking"] = dispatch->get("Tracking");
    state["atPark"] = dispatch->get("AtPark");
    if (auto v = dispatch->get("SiderealTime"); v.isValid()) state["siderealTime"] = v.toDouble();
    state["siteLongitude"] = dispatch->get("SiteLongitude");
    state["siteLatitude"] = dispatch->get("SiteLatitude");
    state["siteElevation"] = dispatch->get("SiteElevation");
    state["canSlewAsync"] = dispatch->get("CanSlewAsync");
    state["equatorialSystem"] = dispatch->get("EquatorialSystem");
    double skyRa, skyDec, altitude;
    if (state.contains("ra") && state.contains("dec") && MountCoordinates::convert(
            state.value("ra").toDouble(), state.value("dec").toDouble(), state.value("equatorialSystem").toInt(),
            state, false, skyRa, skyDec, altitude).isEmpty()) {
        state["skyRa"] = skyRa; state["skyDec"] = skyDec;
    }
    const auto utc = dispatch->get("UTCDate").toDateTime();
    state["utcTimeText"] = utc.isValid() ? utc.toUTC().toString("yyyy-MM-dd HH:mm:ss 'UTC'") : QStringLiteral("—");
    if (auto v = dispatch->get("Altitude"); v.isValid()) state["altitude"] = v.toDouble();
    if (auto v = dispatch->get("Azimuth"); v.isValid()) state["azimuth"] = v.toDouble();
    if (auto v = dispatch->get("TrackingRate"); v.isValid()) state["trackingRate"] = v.toInt();
    state["guideRateRa"] = guideRateRa();
    state["guideRateDec"] = guideRateDec();
    state["canFindHome"] = m_canFindHome;
    state["canSetPark"] = m_canSetPark;
    return state;
}
