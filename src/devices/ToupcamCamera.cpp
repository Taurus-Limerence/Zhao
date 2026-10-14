#include "ToupcamCamera.h"
#include "ToupcamParams.h"
#include "UsbIdentity.h"
#include "imaging/PreviewFrame.h"
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <cmath>
#include <cstring>
#include <QVector>
#include <limits>
#include <algorithm>

namespace {
#ifdef Q_OS_WIN
QString sdkText(const wchar_t *value) { return QString::fromWCharArray(value); }
#else
QString sdkText(const char *value) { return QString::fromUtf8(value); }
#endif
}

ToupcamCamera::ToupcamCamera()
    : m_presenceTimer(new QTimer(this)),
      m_pollTimer(new QTimer(this))
{
    m_library.setParent(this);
    m_presenceTimer->setInterval(1500);
    connect(m_presenceTimer, &QTimer::timeout, this, &ToupcamCamera::checkPresence);
    m_pollTimer->setInterval(2000);
    connect(m_pollTimer, &QTimer::timeout, this, &ToupcamCamera::pollTemperatureTick);
    m_stillTimer = new QTimer(this);
    m_stillTimer->setSingleShot(true);
    connect(m_stillTimer, &QTimer::timeout, this, [this] { finishStill(QStringLiteral("RAW曝光等待超时")); });
}

ToupcamCamera::~ToupcamCamera() { release(); }

bool ToupcamCamera::loadSdk(QString &error)
{
    if (m_enum) return true;
#ifdef Q_OS_WIN
    const auto file = QStringLiteral("toupcam.dll");
#else
    const auto file = QStringLiteral("libtoupcam.so");
#endif
    m_library.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath(file));
    if (!m_library.load()) {
        error = QStringLiteral("无法加载相机 SDK：%1\n%2").arg(m_library.fileName(), m_library.errorString());
        return false;
    }
    const auto enumerateFunction = reinterpret_cast<decltype(m_enum)>(m_library.resolve("Toupcam_EnumV2"));
    m_open = reinterpret_cast<decltype(m_open)>(m_library.resolve("Toupcam_Open"));
    m_close = reinterpret_cast<decltype(m_close)>(m_library.resolve("Toupcam_Close"));
    m_serial = reinterpret_cast<decltype(m_serial)>(m_library.resolve("Toupcam_get_SerialNumber"));
    m_version = reinterpret_cast<decltype(m_version)>(m_library.resolve("Toupcam_Version"));
    m_getExpoTime = reinterpret_cast<decltype(m_getExpoTime)>(m_library.resolve("Toupcam_get_ExpoTime"));
    m_putExpoTime = reinterpret_cast<decltype(m_putExpoTime)>(m_library.resolve("Toupcam_put_ExpoTime"));
    m_getExpTimeRange = reinterpret_cast<decltype(m_getExpTimeRange)>(m_library.resolve("Toupcam_get_ExpTimeRange"));
    m_getExpoAGain = reinterpret_cast<decltype(m_getExpoAGain)>(m_library.resolve("Toupcam_get_ExpoAGain"));
    m_putExpoAGain = reinterpret_cast<decltype(m_putExpoAGain)>(m_library.resolve("Toupcam_put_ExpoAGain"));
    m_getExpoAGainRange = reinterpret_cast<decltype(m_getExpoAGainRange)>(m_library.resolve("Toupcam_get_ExpoAGainRange"));
    m_putOption = reinterpret_cast<decltype(m_putOption)>(m_library.resolve("Toupcam_put_Option"));
    m_getOption = reinterpret_cast<decltype(m_getOption)>(m_library.resolve("Toupcam_get_Option"));
    m_getTemperature = reinterpret_cast<decltype(m_getTemperature)>(m_library.resolve("Toupcam_get_Temperature"));
    m_fanMaxSpeed = reinterpret_cast<decltype(m_fanMaxSpeed)>(m_library.resolve("Toupcam_get_FanMaxSpeed"));
    m_maxBitDepth = reinterpret_cast<decltype(m_maxBitDepth)>(m_library.resolve("Toupcam_get_MaxBitDepth"));
    m_startPullMode = reinterpret_cast<decltype(m_startPullMode)>(m_library.resolve("Toupcam_StartPullModeWithCallback"));
    m_stop = reinterpret_cast<decltype(m_stop)>(m_library.resolve("Toupcam_Stop"));
    m_trigger = reinterpret_cast<decltype(m_trigger)>(m_library.resolve("Toupcam_Trigger"));
    m_pullImageV3 = reinterpret_cast<decltype(m_pullImageV3)>(m_library.resolve("Toupcam_PullImageV3"));
    m_putAutoExpoEnable = reinterpret_cast<decltype(m_putAutoExpoEnable)>(m_library.resolve("Toupcam_put_AutoExpoEnable"));
    m_putEsize = reinterpret_cast<decltype(m_putEsize)>(m_library.resolve("Toupcam_put_eSize"));
    m_getEsize = reinterpret_cast<decltype(m_getEsize)>(m_library.resolve("Toupcam_get_eSize"));
    m_getRawFormat = reinterpret_cast<decltype(m_getRawFormat)>(m_library.resolve("Toupcam_get_RawFormat"));
    m_getAutoExpoEnable = reinterpret_cast<decltype(m_getAutoExpoEnable)>(m_library.resolve("Toupcam_get_AutoExpoEnable"));
    if (!enumerateFunction || !m_open || !m_close || !m_serial || !m_version
        || !m_getExpoTime || !m_putExpoTime || !m_getExpTimeRange
        || !m_getExpoAGain || !m_putExpoAGain || !m_getExpoAGainRange
        || !m_putOption || !m_getOption || !m_getTemperature || !m_fanMaxSpeed || !m_maxBitDepth
        || !m_startPullMode || !m_stop || !m_pullImageV3 || !m_putAutoExpoEnable) {
        error = QStringLiteral("相机 SDK 缺少必要接口，请使用项目内配套的 DLL/SO。");
        m_library.unload();
        return false;
    }
    m_enum = enumerateFunction;
    return true;
}

unsigned ToupcamCamera::listDevices(ToupcamDeviceV2 *devices) const
{
    return (std::min)(m_enum(devices), unsigned(TOUPCAM_MAX));
}

QVariantMap ToupcamCamera::describeDevice(const ToupcamDeviceV2 &device) const
{
    const auto *model = device.model;
    const QString id = sdkText(device.id);
    const QString name = sdkText(device.displayname);
    return {{"id", id}, {"name", name + QStringLiteral(" [%1]").arg(id.right(12))},
            {"model", sdkText(model->name)}, {"width", model->res[0].width},
            {"height", model->res[0].height}, {"mono", bool(model->flag & TOUPCAM_FLAG_MONO)},
            {"sensorWidthMm", double(model->res[0].width) * model->xpixsz / 1000.0},
            {"sensorHeightMm", double(model->res[0].height) * model->ypixsz / 1000.0},
            {"cooling", bool(model->flag & (TOUPCAM_FLAG_TEC | TOUPCAM_FLAG_TEC_ONOFF))}};
}

QVariantList ToupcamCamera::enumerate()
{
    ToupcamDeviceV2 devices[TOUPCAM_MAX]{};
    const unsigned count = listDevices(devices);
    QVariantList result;
    for (unsigned i = 0; i < count; ++i) {
        const auto &device = devices[i];
        if (!device.model || !device.model->preview || (device.model->flag & (TOUPCAM_FLAG_FILTERWHEEL | TOUPCAM_FLAG_AUTOFOCUSER))) continue;
        result.append(describeDevice(device));
    }
    return result;
}

void ToupcamCamera::scan()
{
    QString error;
    if (!loadSdk(error)) { emit scanFinished({}, error); return; }
    emit scanFinished(enumerate(), {});
}

void ToupcamCamera::open(const QString &id)
{
    QString error;
    if (!loadSdk(error)) { emit connectionFinished(false, {}, error); return; }
    if (m_handle) { emit connectionFinished(true, {}, QStringLiteral("相机已经打开，请先断开。")); return; }
    ToupcamDeviceV2 devices[TOUPCAM_MAX]{};
    const unsigned count = listDevices(devices);
    const ToupcamDeviceV2 *matched = nullptr;
    for (unsigned i = 0; i < count && !matched; ++i) {
        const auto &device = devices[i];
        if (!device.model || !device.model->preview || (device.model->flag & (TOUPCAM_FLAG_FILTERWHEEL | TOUPCAM_FLAG_AUTOFOCUSER))) continue;
        if (sdkText(device.id) == id) matched = &device;
    }
    if (id.isEmpty() || !matched) {
        emit connectionFinished(false, {}, QStringLiteral("所选相机已不可用，请刷新设备列表。"));
        return;
    }
#ifdef Q_OS_WIN
    const std::wstring deviceId = id.toStdWString();
    m_handle = m_open(deviceId.c_str());
#else
    const QByteArray deviceId = id.toUtf8();
    m_handle = m_open(deviceId.constData());
#endif
    if (!m_handle) {
        emit connectionFinished(false, {}, QStringLiteral("相机打开失败。请检查 USB 连接、驱动/访问权限，并关闭可能占用相机的软件。"));
        return;
    }
    m_id = id;
    m_missingScans = 0;
    m_flags = matched->model->flag;
    m_sensorWidth = matched->model->res[0].width;
    m_sensorHeight = matched->model->res[0].height;
    pinEngineConventions();
    QVariantMap information = describeDevice(*matched);
    char serial[32]{};
    if (SUCCEEDED(m_serial(m_handle, serial))) information.insert("serial", QString::fromLatin1(serial, int(std::find(serial, serial + 32, '\0') - serial)));
    information.insert("sdkVersion", sdkText(m_version()));
    const UsbIdentity usb = usbIdentityForDeviceId(id);
    information.insert(QStringLiteral("vendorId"), usb.vendorId);
    information.insert(QStringLiteral("productId"), usb.productId);
    information.insert(QStringLiteral("deviceId"), usb.instanceId);
    unsigned resolutionIndex = 0;
    if (m_getEsize && SUCCEEDED(m_getEsize(m_handle, &resolutionIndex))) {
        QVariantList resolutions;
        m_resolutions.clear();
        m_resolutionCount = int(matched->model->preview);
        for (int i = 0; i < m_resolutionCount; ++i) {
            const auto &res = matched->model->res[i];
            m_resolutions.append(QSize(int(res.width), int(res.height)));
            resolutions.append(QStringLiteral("%1 × %2").arg(res.width).arg(res.height));
        }
        information.insert(QStringLiteral("resolutions"), resolutions);
        information.insert(QStringLiteral("resolutionIndex"), resolutionIndex);
    }
    queryRanges();
    m_presenceTimer->start();
    m_pollTimer->start();
    emit connectionFinished(true, information, {});
    refreshState();
}

// The engine's defaults are platform-dependent (byte order BGR on Windows but
// RGB on Linux/macOS/Android); pin every convention PreviewFrame relies on.
void ToupcamCamera::pinEngineConventions()
{
    m_putOption(m_handle, TOUPCAM_OPTION_BYTEORDER, 1);     // BGR on all platforms
    m_putOption(m_handle, TOUPCAM_OPTION_UPSIDE_DOWN, 0);   // rows delivered bottom-up
}

void ToupcamCamera::queryRanges()
{    unsigned exposureMin = 0, exposureMax = 0, exposureDef = 0;
    if (SUCCEEDED(m_getExpTimeRange(m_handle, &exposureMin, &exposureMax, &exposureDef)) && exposureMax > 0) {
        m_exposureMinUs = exposureMin;
        m_exposureMaxUs = exposureMax;
    } else {
        m_exposureMinUs = 100;
        m_exposureMaxUs = 3600000000u;
    }
    unsigned short gainMin = 0, gainMax = 0, gainDef = 0;
    if (SUCCEEDED(m_getExpoAGainRange(m_handle, &gainMin, &gainMax, &gainDef)) && gainMax > 0) {
        m_gainMin = gainMin;
        m_gainMax = gainMax;
    } else {
        m_gainMin = 100;
        m_gainMax = 10000;
    }
    m_offsetMax = -1;
    if (blackLevelSupported()) {
        const HRESULT bitDepth = m_maxBitDepth(m_handle);
        m_offsetMax = CamParams::blackLevelMaxForBitDepth(bitDepth >= 0 ? int(bitDepth) : 8);
    }
    int packed = 0;
    if (tecSupported() && SUCCEEDED(m_getOption(m_handle, TOUPCAM_OPTION_TECTARGET_RANGE, &packed))) {
        m_tempMinTenths = CamParams::tecTargetRangeMin(unsigned(packed));
        m_tempMaxTenths = CamParams::tecTargetRangeMax(unsigned(packed));
    } else {
        m_tempMinTenths = -500;
        m_tempMaxTenths = 300;
    }
    // These queries return the value itself; a negative result means unsupported.
    const HRESULT fanMax = m_fanMaxSpeed(m_handle);
    m_fanMax = fanMax >= 0 ? int(fanMax) : -1;
    m_heatMax = 100;
    if (heatSupported()) {
        int heatMax = 0;
        if (SUCCEEDED(m_getOption(m_handle, TOUPCAM_OPTION_HEAT_MAX, &heatMax)) && heatMax > 0) m_heatMax = heatMax;
    }
}

void ToupcamCamera::refreshState()
{
    if (!m_handle) return;
    QVariantMap state;
    state.insert(QStringLiteral("exposureMinUs"), m_exposureMinUs);
    state.insert(QStringLiteral("exposureMaxUs"), m_exposureMaxUs);
    unsigned exposure = 0;
    if (SUCCEEDED(m_getExpoTime(m_handle, &exposure))) state.insert(QStringLiteral("exposureUs"), exposure);
    state.insert(QStringLiteral("gainMin"), m_gainMin);
    state.insert(QStringLiteral("gainMax"), m_gainMax);
    unsigned short gain = 0;
    if (SUCCEEDED(m_getExpoAGain(m_handle, &gain))) state.insert(QStringLiteral("gain"), unsigned(gain));
    int option = 0;
    if (blackLevelSupported()) {
        state.insert(QStringLiteral("canBlackLevel"), true);
        state.insert(QStringLiteral("offsetMax"), m_offsetMax);
        if (SUCCEEDED(m_getOption(m_handle, TOUPCAM_OPTION_BLACKLEVEL, &option))) state.insert(QStringLiteral("offset"), option);
    }
    if (SUCCEEDED(m_getOption(m_handle, TOUPCAM_OPTION_BINNING, &option)))
        state.insert(QStringLiteral("binning"), CamParams::binningFactor(option));
    if (tecSupported()) {
        state.insert(QStringLiteral("canTec"), true);
        state.insert(QStringLiteral("tempMin"), CamParams::tenthsToCelsius(m_tempMinTenths));
        state.insert(QStringLiteral("tempMax"), CamParams::tenthsToCelsius(m_tempMaxTenths));
        if (SUCCEEDED(m_getOption(m_handle, TOUPCAM_OPTION_TEC, &option))) state.insert(QStringLiteral("tecEnabled"), option != 0);
        if (SUCCEEDED(m_getOption(m_handle, TOUPCAM_OPTION_TECTARGET, &option))) state.insert(QStringLiteral("targetTemp"), CamParams::tenthsToCelsius(option));
    }
    if (temperatureSupported()) state.insert(QStringLiteral("canTemp"), true);
    if (fanSupported()) {
        state.insert(QStringLiteral("canFan"), true);
        state.insert(QStringLiteral("fanMax"), m_fanMax);
        if (SUCCEEDED(m_getOption(m_handle, TOUPCAM_OPTION_FAN, &option))) state.insert(QStringLiteral("fanSpeed"), option);
    }
    if (heatSupported()) {
        state.insert(QStringLiteral("canHeat"), true);
        state.insert(QStringLiteral("heatMax"), m_heatMax);
        if (SUCCEEDED(m_getOption(m_handle, TOUPCAM_OPTION_HEAT, &option))) state.insert(QStringLiteral("heatLevel"), option);
    }
    if (gainModeSupported()) {
        state.insert(QStringLiteral("canGainMode"), true);
        if (SUCCEEDED(m_getOption(m_handle, TOUPCAM_OPTION_CG, &option))) state.insert(QStringLiteral("gainMode"), option);
    }
    if (lowNoiseSupported()) {
        state.insert(QStringLiteral("canLowNoise"), true);
        if (SUCCEEDED(m_getOption(m_handle, TOUPCAM_OPTION_LOW_NOISE, &option))) state.insert(QStringLiteral("lowNoise"), option != 0);
    }
    state.insert(pollTemperature());
    emit stateChanged(state);
}

QVariantMap ToupcamCamera::pollTemperature()
{
    QVariantMap delta;
    if (!m_handle || !temperatureSupported()) return delta;
    short temperature = 0;
    if (SUCCEEDED(m_getTemperature(m_handle, &temperature))) {
        const double celsius = CamParams::tenthsToCelsius(temperature);
        if (std::isnan(m_reportedTemp) || std::abs(celsius - m_reportedTemp) >= 0.05) {
            m_reportedTemp = celsius;
            delta.insert(QStringLiteral("currentTemp"), celsius);
        }
    }
    if (tecSupported()) {
        int voltage = 0;
        int maximum = 0;
        if (SUCCEEDED(m_getOption(m_handle, TOUPCAM_OPTION_TEC_VOLTAGE, &voltage))
            && SUCCEEDED(m_getOption(m_handle, TOUPCAM_OPTION_TEC_VOLTAGE_MAX, &maximum))) {
            const double percent = CamParams::tecPowerPercent(voltage, maximum);
            if (std::isnan(m_reportedPower) || std::abs(percent - m_reportedPower) >= 0.5) {
                m_reportedPower = percent;
                delta.insert(QStringLiteral("tecPower"), percent);
            }
        }
    }
    return delta;
}

void ToupcamCamera::pollTemperatureTick()
{
    const QVariantMap delta = pollTemperature();
    if (!delta.isEmpty()) emit stateChanged(delta);
}

void ToupcamCamera::setExposure(unsigned microseconds)
{
    if (m_stillPending) { emit commandFailed(QStringLiteral("RAW曝光期间不能修改采集参数")); return; }
    if (!m_handle) return;
    const unsigned value = CamParams::clamped(microseconds, m_exposureMinUs, m_exposureMaxUs);
    if (FAILED(m_putExpoTime(m_handle, value))) {
        emit commandFailed(QStringLiteral("设置曝光时间失败，相机拒绝了该参数。"));
        return;
    }
    emit stateChanged({{QStringLiteral("exposureUs"), value}});
}

void ToupcamCamera::setGain(unsigned gain)
{
    if (m_stillPending) { emit commandFailed(QStringLiteral("RAW曝光期间不能修改采集参数")); return; }
    if (!m_handle) return;
    const unsigned value = CamParams::clamped(gain, m_gainMin, m_gainMax);
    if (FAILED(m_putExpoAGain(m_handle, unsigned short(value)))) {
        emit commandFailed(QStringLiteral("设置增益失败，相机拒绝了该参数。"));
        return;
    }
    unsigned short actual = 0;
    if (FAILED(m_getExpoAGain(m_handle, &actual))) {
        emit commandFailed(QStringLiteral("读取实际增益失败，未确认增益设置。"));
        return;
    }
    emit stateChanged({{QStringLiteral("gain"), unsigned(actual)}});
}

void ToupcamCamera::setOffset(int offset)
{
    if (m_stillPending) { emit commandFailed(QStringLiteral("RAW曝光期间不能修改采集参数")); return; }
    if (!m_handle) return;
    if (!blackLevelSupported() || m_offsetMax < 0) {
        emit commandFailed(QStringLiteral("该相机不支持偏置设置。"));
        return;
    }
    const int value = CamParams::clamped(offset, 0, m_offsetMax);
    if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_BLACKLEVEL, value))) {
        emit commandFailed(QStringLiteral("设置偏置失败，相机拒绝了该参数。"));
        return;
    }
    emit stateChanged({{QStringLiteral("offset"), value}});
}

void ToupcamCamera::setBinning(int factor)
{
    if (m_stillPending) { emit commandFailed(QStringLiteral("RAW曝光期间不能修改采集参数")); return; }
    if (m_rawSession && !endRawSession()) return;
    if (!m_handle) return;
    const int value = CamParams::clamped(factor, 1, 8);
    if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_BINNING, CamParams::binningOptionValue(value)))) {
        emit commandFailed(QStringLiteral("设置合并像素失败，相机拒绝了该参数。"));
        return;
    }
    emit stateChanged({{QStringLiteral("binning"), value}});
}

void ToupcamCamera::setCoolerEnabled(bool enabled)
{
    if (!m_handle) return;
    if (!tecSupported()) {
        emit commandFailed(QStringLiteral("该相机不支持 TEC 开关。"));
        return;
    }
    if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_TEC, enabled ? 1 : 0))) {
        emit commandFailed(QStringLiteral("切换 TEC 制冷失败。"));
        return;
    }
    emit stateChanged({{QStringLiteral("tecEnabled"), enabled}});
}

void ToupcamCamera::setTargetTemperature(double celsius)
{
    if (!m_handle) return;
    if (!tecSupported()) {
        emit commandFailed(QStringLiteral("该相机不支持设置目标温度。"));
        return;
    }
    const int tenths = CamParams::clamped(CamParams::temperatureToTenths(celsius), m_tempMinTenths, m_tempMaxTenths);
    if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_TECTARGET, tenths))) {
        emit commandFailed(QStringLiteral("设置目标温度失败，相机拒绝了该参数。"));
        return;
    }
    emit stateChanged({{QStringLiteral("targetTemp"), CamParams::tenthsToCelsius(tenths)}});
}

void ToupcamCamera::setFanSpeed(int speed)
{
    if (!m_handle) return;
    if (!fanSupported()) {
        emit commandFailed(QStringLiteral("该相机不支持风扇控制。"));
        return;
    }
    int value = speed < -1 ? -1 : speed;
    if (m_fanMax >= 0 && value > m_fanMax) value = m_fanMax;
    if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_FAN, value))) {
        emit commandFailed(QStringLiteral("设置风扇转速失败。"));
        return;
    }
    emit stateChanged({{QStringLiteral("fanSpeed"), value}});
}

void ToupcamCamera::setHeatLevel(int level)
{
    if (!m_handle) return;
    if (!heatSupported()) {
        emit commandFailed(QStringLiteral("该相机不支持加热控制。"));
        return;
    }
    const int value = CamParams::clamped(level, 0, m_heatMax);
    if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_HEAT, value))) {
        emit commandFailed(QStringLiteral("设置加热级别失败。"));
        return;
    }
    emit stateChanged({{QStringLiteral("heatLevel"), value}});
}

void ToupcamCamera::setGainMode(int mode)
{
    if (m_stillPending) { emit commandFailed(QStringLiteral("RAW曝光期间不能修改采集参数")); return; }
    if (!m_handle) return;
    if (!gainModeSupported()) {
        emit commandFailed(QStringLiteral("该相机不支持增益模式切换。"));
        return;
    }
    const int value = CamParams::clamped(mode, 0, 2);
    if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_CG, value))) {
        emit commandFailed(QStringLiteral("切换增益模式失败。"));
        return;
    }
    emit stateChanged({{QStringLiteral("gainMode"), value}});
}

void ToupcamCamera::setLowNoise(bool enabled)
{
    if (m_stillPending) { emit commandFailed(QStringLiteral("RAW曝光期间不能修改采集参数")); return; }
    if (!m_handle) return;
    if (!lowNoiseSupported()) {
        emit commandFailed(QStringLiteral("该相机不支持低噪声模式。"));
        return;
    }
    if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_LOW_NOISE, enabled ? 1 : 0))) {
        emit commandFailed(QStringLiteral("切换低噪声模式失败。"));
        return;
    }
    emit stateChanged({{QStringLiteral("lowNoise"), enabled}});
}

void ToupcamCamera::startPreview(bool continuous)
{
    if (m_rawSession && !m_stillPending && !endRawSession()) return;
    if (!m_handle || m_previewing) return;
    // The engine ships with auto exposure enabled on several models (observed on
    // ATR2600M), which would silently override the user's exposure and gain.
    if (FAILED(m_putAutoExpoEnable(m_handle, 0))) {
        emit commandFailed(QStringLiteral("关闭自动曝光失败，曝光时间可能被相机自动调整。"));
    }
    m_singleShot = !continuous;
    m_previewBuffer.resize(size_t(m_sensorWidth) * m_sensorHeight * 3 + 4096);
    if (m_guideMode)
        // Grey16 raw path: pre-sized here because the frame info only exists
        // AFTER the pull — resizing with the zeroed struct once overflowed
        // a 4096-byte buffer with a full 3840x2160 frame.
        m_rawBuffer.resize(size_t(m_sensorWidth) * m_sensorHeight * 2 + 4096);
    QElapsedTimer startElapsed; startElapsed.start();
    const HRESULT hr = m_startPullMode(m_handle, &ToupcamCamera::onEvent, this);
    qInfo() << "[RawCapture] engine start ms=" << startElapsed.elapsed() << "result=" << Qt::hex << unsigned(hr);
    if (FAILED(hr)) {
        m_previewBuffer.clear();
        emit commandFailed(QStringLiteral("启动相机采集失败（0x%1）。").arg(unsigned(hr), 8, 16, QChar('0')));
        return;
    }
    m_previewing = true;
    emit stateChanged({{QStringLiteral("previewing"), true}});
}

void ToupcamCamera::setResolution(int index)
{
    if (m_stillPending) { emit commandFailed(QStringLiteral("RAW曝光期间不能修改采集参数")); return; }
    if (m_rawSession && !endRawSession()) return;
    if (!m_handle || index < 0 || index >= m_resolutionCount) {
        emit commandFailed(QStringLiteral("分辨率索引无效。"));
        return;
    }
    // SDK 约定 put_eSize 需在采集停止时设置；采集中切换则先停后启。
    const bool wasPreviewing = m_previewing;
    if (wasPreviewing) stopPreview();
    if (FAILED(m_putEsize(m_handle, unsigned(index)))) {
        emit commandFailed(QStringLiteral("切换分辨率失败，相机拒绝了该参数。"));
    } else {
        const QSize &size = m_resolutions.at(index);
        emit stateChanged({{QStringLiteral("resolutionIndex"), unsigned(index)},
                           {QStringLiteral("width"), unsigned(size.width())},
                           {QStringLiteral("height"), unsigned(size.height())}});
    }
    if (wasPreviewing) startPreview(true);
}

void ToupcamCamera::requestStill()
{
    // Keep the RAW engine ready between requests; only Trigger(1) starts an exposure.
    if (!m_handle || m_stillPending || (m_guideMode && m_previewing)) {
        emit stillFailed(QStringLiteral("相机未连接、已有截存请求或导星取流中，不能开始RAW截存"));
        return;
    }
    if (!m_trigger || !(m_flags & TOUPCAM_FLAG_TRIGGER_SOFTWARE)) {
        emit stillFailed(QStringLiteral("当前相机不支持软件触发RAW拍摄"));
        return;
    }
    if (!m_rawSession) {
        if (!m_getRawFormat || !m_getEsize || !m_putEsize || !m_getAutoExpoEnable
            || FAILED(m_getOption(m_handle, TOUPCAM_OPTION_RAW, &m_savedRaw))
            || FAILED(m_getOption(m_handle, TOUPCAM_OPTION_BITDEPTH, &m_savedDepth))
            || FAILED(m_getOption(m_handle, TOUPCAM_OPTION_BINNING, &m_savedBinning))
            || FAILED(m_getAutoExpoEnable(m_handle, &m_savedAutoExpo))
            || FAILED(m_getEsize(m_handle, &m_savedResolution))
            || FAILED(m_getOption(m_handle, TOUPCAM_OPTION_FRAME_DEQUE_LENGTH, &m_savedQueueLength))) {
            emit stillFailed(QStringLiteral("无法读取RAW切换前的相机配置"));
            return;
        }
        m_resumePreview = m_previewing && !m_singleShot;
        m_savedTrigger = -1;
        if (m_flags & (TOUPCAM_FLAG_TRIGGER_SOFTWARE | TOUPCAM_FLAG_TRIGGER_EXTERNAL | TOUPCAM_FLAG_SELFTRIGGER)) {
            int trigger = 0;
            if (FAILED(m_getOption(m_handle, TOUPCAM_OPTION_TRIGGER, &trigger))) {
                emit stillFailed(QStringLiteral("无法读取相机触发模式"));
                return;
            }
            m_savedTrigger = trigger;
        }
        stopPreview();
        m_rawSession = true;
        m_stillPending = true;
        if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_TRIGGER, 1))) {
            finishStill(QStringLiteral("无法切换RAW采集触发模式"));
            return;
        }
        if (FAILED(m_putAutoExpoEnable(m_handle, 0))
            || FAILED(m_putOption(m_handle, TOUPCAM_OPTION_RAW, 1))
            || FAILED(m_putOption(m_handle, TOUPCAM_OPTION_BITDEPTH, 1))
            || FAILED(m_putOption(m_handle, TOUPCAM_OPTION_BINNING, 1))
            || FAILED(m_putEsize(m_handle, 0))
            || FAILED(m_putOption(m_handle, TOUPCAM_OPTION_FRAME_DEQUE_LENGTH, 2))) {
            finishStill(QStringLiteral("相机拒绝16位RAW配置"));
            return;
        }
        int raw = 0, depth = 0, trigger = 0;
        if (FAILED(m_getOption(m_handle, TOUPCAM_OPTION_RAW, &raw))
            || FAILED(m_getOption(m_handle, TOUPCAM_OPTION_BITDEPTH, &depth))
            || FAILED(m_getOption(m_handle, TOUPCAM_OPTION_TRIGGER, &trigger))
            || raw != 1 || depth != 1 || trigger != 1) {
            finishStill(QStringLiteral("RAW采集配置回读不一致"));
            return;
        }
        qInfo() << "[RawCapture] session ready raw=" << raw << "depth=" << depth << "trigger=" << trigger;
    }
    m_stillPending = true;
    unsigned fourcc = 0, bits = 0, exposure = 0;
    unsigned short gain = 0;
    if (FAILED(m_getRawFormat(m_handle, &fourcc, &bits)) || bits <= 8 || bits > 16
        || FAILED(m_getExpoTime(m_handle, &exposure)) || FAILED(m_getExpoAGain(m_handle, &gain))) {
        finishStill(QStringLiteral("无法确认RAW位深或曝光参数"));
        return;
    }
    m_stillMetadata = {{"EXPTIME", double(exposure) / 1e6}, {"GAIN", gain}, {"RAWBITS", bits},
                       {"XBINNING", 1}, {"YBINNING", 1},
                       {"DATE-OBS", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    if (!(m_flags & TOUPCAM_FLAG_MONO)) {
        QByteArray pattern;
        for (int i = 0; i < 4; ++i) pattern.append(char((fourcc >> (8*i)) & 255));
        if (!QList<QByteArray>{"RGGB", "BGGR", "GBRG", "GRBG"}.contains(pattern)) {
            finishStill(QStringLiteral("不支持或无法确认的RAW拜耳格式"));
            return;
        }
        m_stillMetadata.insert("BAYERPAT", QString::fromLatin1(pattern));
    }
    short temperature = 0;
    if (SUCCEEDED(m_getTemperature(m_handle, &temperature))) m_stillMetadata.insert("CCD-TEMP", temperature / 10.0);
    m_stillBuffer.resize(size_t(m_sensorWidth) * m_sensorHeight * 2 + 4096);
    qInfo() << "[RawCapture] start exposureUs=" << exposure << "previousTrigger=" << m_savedTrigger
            << "bits=" << bits << "sensor=" << m_sensorWidth << m_sensorHeight;
    startPreview(false);
    if (!m_previewing) { finishStill(QStringLiteral("RAW采集启动失败")); return; }
    if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_FLUSH, 3))) {
        finishStill(QStringLiteral("清理RAW采集缓存失败"));
        return;
    }
    // Discard events queued before this request; the flushed data is no longer available.
    ++m_generation;
    m_stillTimer->start(int(qMin<quint64>(quint64(exposure) / 1000 + 30000, INT_MAX)));
    const HRESULT triggered = m_trigger(m_handle, 1);
    qInfo() << "[RawCapture] trigger count=1 result=" << Qt::hex << unsigned(triggered);
    if (FAILED(triggered)) finishStill(QStringLiteral("相机软件触发失败（0x%1）").arg(unsigned(triggered), 8, 16, QChar('0')));
}

void ToupcamCamera::finishStill(const QString &error)
{
    if (!m_stillPending) return;
    qInfo() << "[RawCapture] finish error=" << error;
    m_stillPending = false;
    m_stillTimer->stop();
    if (error.isEmpty() && !m_resumePreview) {
        // The engine is running but no exposure is in flight and this is not live preview.
        emit stateChanged({{QStringLiteral("previewing"), false}});
        return;
    }
    if (!error.isEmpty() && m_handle && m_previewing && m_trigger) {
        const HRESULT cancelled = m_trigger(m_handle, 0);
        qInfo() << "[RawCapture] trigger count=0 result=" << Qt::hex << unsigned(cancelled);
        if (FAILED(cancelled)) emit commandFailed(QStringLiteral("取消相机触发失败，将停止采集"));
    }
    const bool restored = endRawSession();
    if (!error.isEmpty()) emit stillFailed(error);
    const bool resume = m_resumePreview && restored;
    m_resumePreview = false;
    if (resume) startPreview(true);
}

bool ToupcamCamera::endRawSession()
{
    if (!m_rawSession) return true;
    m_rawSession = false;
    stopPreview();
    bool restored = true;
    if (m_handle) {
        restored &= SUCCEEDED(m_putOption(m_handle, TOUPCAM_OPTION_RAW, m_savedRaw));
        restored &= SUCCEEDED(m_putOption(m_handle, TOUPCAM_OPTION_BITDEPTH, m_savedDepth));
        restored &= SUCCEEDED(m_putOption(m_handle, TOUPCAM_OPTION_BINNING, m_savedBinning));
        restored &= SUCCEEDED(m_putEsize(m_handle, m_savedResolution));
        restored &= SUCCEEDED(m_putAutoExpoEnable(m_handle, m_savedAutoExpo));
        if (m_savedTrigger >= 0)
            restored &= SUCCEEDED(m_putOption(m_handle, TOUPCAM_OPTION_TRIGGER, m_savedTrigger));
        restored &= SUCCEEDED(m_putOption(m_handle, TOUPCAM_OPTION_FRAME_DEQUE_LENGTH, m_savedQueueLength));
    }
    if (!restored) emit commandFailed(QStringLiteral("RAW截存后恢复相机配置失败，请重新连接相机"));
    return restored;
}

void ToupcamCamera::stopPreview()
{
    if (m_stillPending) {
        m_resumePreview = false;
        finishStill(QStringLiteral("RAW截存已取消"));
        return;
    }
    if (m_rawSession) { endRawSession(); return; }
    if (!m_handle || !m_previewing) return;
    QElapsedTimer stopElapsed; stopElapsed.start();
    const HRESULT stopped = m_stop(m_handle);
    qInfo() << "[RawCapture] engine stop ms=" << stopElapsed.elapsed() << "result=" << Qt::hex << unsigned(stopped);
    ++m_generation;
    m_previewing = false;
    m_singleShot = false;
    emit stateChanged({{QStringLiteral("previewing"), false}});
}

// Dim 8-bit preview decimated from the Grey16 guide frame, auto-stretched
// for DISPLAY only: a sampled median anchors the black point and the sampled
// maximum the white point, so faint guide stars stay visible on screen. The
// analytics always consume the untouched raw 16-bit stream.
QImage ToupcamCamera::guidePreviewFrame(const unsigned char *data, unsigned width, unsigned height) const
{
    const auto* src = reinterpret_cast<const quint16 *>(data);
    // Sample the decimated lattice for the stretch anchors.
    const unsigned targetWidth = 720;
    const unsigned step = width > targetWidth ? (width + targetWidth - 1) / targetWidth : 1;
    const unsigned outWidth = width / step;
    const unsigned outHeight = height / step;
    std::vector<quint16> sampled;
    sampled.reserve(size_t(outWidth) * outHeight);
    quint16 maxVal = 0;
    for (unsigned y = 0; y < outHeight; ++y)
        for (unsigned x = 0; x < outWidth; ++x) {
            const quint16 value = src[size_t(y * step) * width + x * step];
            sampled.push_back(value);
            if (value > maxVal) maxVal = value;
        }
    std::sort(sampled.begin(), sampled.end());
    const quint16 median = sampled.empty() ? 0 : sampled[sampled.size() / 2];
    const double lo = median;
    const double hi = std::max<double>(maxVal, lo + 1.0);
    const double span = hi - lo;
    QImage image(int(outWidth), int(outHeight), QImage::Format_Grayscale8);
    for (unsigned y = 0; y < outHeight; ++y) {
        auto* dst = image.scanLine(int(y));
        const auto* row = src + size_t(y * step) * width;
        for (unsigned x = 0; x < outWidth; ++x) {
            const double value = row[x * step];
            dst[x] = quint8(std::clamp((value - lo) / span * 255.0, 0.0, 255.0));
        }
    }
    return image;
}

QImage ToupcamCamera::downscaledFrame(const unsigned char *data, unsigned width, unsigned height) const
{
    // See PreviewFrame: BGR byte order pinned on every platform, bottom-up rows,
    // rowPitch = -1 so each row is exactly width*3 bytes.
    const PreviewFrame::Request request{width, height};
    return PreviewFrame::downscaledBgr(data, request);
}

#ifdef _WIN32
void __stdcall ToupcamCamera::onEvent(unsigned nEvent, void *ctx)
#else
void ToupcamCamera::onEvent(unsigned nEvent, void *ctx)
#endif
{
    auto *self = static_cast<ToupcamCamera *>(ctx);
    const unsigned generation = self->m_generation.load();
    QMetaObject::invokeMethod(self, [self, nEvent, generation] {
        if (generation == self->m_generation.load() && self->m_handle) self->processEvent(nEvent);
    }, Qt::QueuedConnection);
}

void ToupcamCamera::processEvent(unsigned nEvent)
{
    auto *self = this;
    if (nEvent == TOUPCAM_EVENT_IMAGE && !m_previewing) return;
    if (nEvent == TOUPCAM_EVENT_IMAGE && m_rawSession && !m_stillPending) {
        // Drain unsolicited images instead of interpreting RAW bytes as RGB preview.
        ToupcamFrameInfoV3 ignored{};
        m_pullImageV3(m_handle, m_stillBuffer.data(), 0, 0, -1, &ignored);
        return;
    }
    if (m_stillPending) qInfo() << "[RawCapture] event=" << Qt::hex << nEvent;
    if (m_stillPending && (nEvent == TOUPCAM_EVENT_TRIGGERFAIL || nEvent == TOUPCAM_EVENT_NOFRAMETIMEOUT)) {
        finishStill(nEvent == TOUPCAM_EVENT_TRIGGERFAIL
            ? QStringLiteral("相机报告软件触发失败") : QStringLiteral("相机报告等待图像超时"));
        return;
    }
    if (m_stillPending && (nEvent == TOUPCAM_EVENT_ERROR || nEvent == TOUPCAM_EVENT_DISCONNECTED)) {
        m_resumePreview = false;
        finishStill(QStringLiteral("RAW采集错误或设备断开"));
        return;
    }
    if (nEvent == TOUPCAM_EVENT_IMAGE) {
        ToupcamFrameInfoV3 info{};
        if (m_stillPending) {
            const HRESULT hr = m_pullImageV3(m_handle, m_stillBuffer.data(), 0, 0, -1, &info);
            qInfo() << "[RawCapture] pull result=" << Qt::hex << unsigned(hr)
                    << Qt::dec << "size=" << info.width << info.height;
            if (FAILED(hr)
                || !info.width || !info.height || quint64(info.width) * info.height * 2 > m_stillBuffer.size()) {
                finishStill(QStringLiteral("RAW取帧失败或尺寸无效"));
                return;
            }
            QVector<quint16> pixels(qsizetype(info.width) * info.height);
            std::memcpy(pixels.data(), m_stillBuffer.data(), size_t(pixels.size()) * 2);
            const auto preview = guidePreviewFrame(m_stillBuffer.data(), info.width, info.height);
            const auto metadata = m_stillMetadata;
            finishStill();
            emit frameReady(preview);
            emit rawStillReady(std::move(pixels), int(info.width), int(info.height), metadata);
            return;
        }
        if (self->m_guideMode) {
            // Guiding analytics: full-resolution Grey16, tightly packed. The
            // buffer was sized in startPreview from the sensor dimensions.
            if (size_t(info.width) * info.height * 2 + 4096 <= self->m_rawBuffer.size()
                && SUCCEEDED(self->m_pullImageV3(self->m_handle, self->m_rawBuffer.data(), 0, 16, -1, &info))) {
                unsigned exposureUs = 0;
                if (FAILED(self->m_getExpoTime(self->m_handle, &exposureUs))) exposureUs = 0;
                QVector<quint16> pixels(size_t(info.width) * info.height);
                std::memcpy(pixels.data(), self->m_rawBuffer.data(), size_t(pixels.size()) * 2);
                emit self->rawFrameReady(std::move(pixels), int(info.width), int(info.height),
                                         double(exposureUs) / 1000000.0);
                emit self->frameReady(self->guidePreviewFrame(self->m_rawBuffer.data(), info.width, info.height));
                if (self->m_singleShot)
                    QMetaObject::invokeMethod(self, [self] { self->stopPreview(); }, Qt::QueuedConnection);
            }
            return;
        }
        // rowPitch -1: tightly packed rows, matching the PreviewFrame contract.
        if (SUCCEEDED(self->m_pullImageV3(self->m_handle, self->m_previewBuffer.data(), 0, 24, -1, &info))) {
            emit self->frameReady(self->downscaledFrame(self->m_previewBuffer.data(), info.width, info.height));
            // Single exposure: hand the first frame over, then stop the engine
            // from the owning worker thread (never from this callback context).
            if (self->m_singleShot)
                QMetaObject::invokeMethod(self, [self] { self->stopPreview(); }, Qt::QueuedConnection);
        }
    } else if (nEvent == TOUPCAM_EVENT_EXPOSURE) {
        // The engine reports the effective exposure/gain; publish the readback.
        unsigned exposure = 0;
        unsigned short gain = 0;
        if (SUCCEEDED(self->m_getExpoTime(self->m_handle, &exposure)) && SUCCEEDED(self->m_getExpoAGain(self->m_handle, &gain)))
            emit self->stateChanged({{QStringLiteral("exposureUs"), exposure}, {QStringLiteral("gain"), unsigned(gain)}});
    }
}

void ToupcamCamera::release()
{
    if (m_stillPending) { m_resumePreview = false; finishStill(QStringLiteral("相机关闭，RAW截存已取消")); }
    endRawSession();
    ++m_generation;
    m_presenceTimer->stop();
    m_pollTimer->stop();
    if (m_handle) {
        if (m_previewing) { m_stop(m_handle); m_previewing = false; }
        m_close(m_handle);
        m_handle = nullptr;
    }
    m_singleShot = false;
    m_stillPending = false;
    m_stillBuffer.clear();
    m_resolutions.clear();
    m_resolutionCount = 0;
    m_previewBuffer.clear();
    m_id.clear();
    m_missingScans = 0;
    m_flags = 0;
    m_reportedTemp = std::numeric_limits<double>::quiet_NaN();
    m_reportedPower = std::numeric_limits<double>::quiet_NaN();
}

void ToupcamCamera::close()
{
    release();
    emit connectionFinished(false, {}, {});
}

void ToupcamCamera::checkPresence()
{
    if (!m_handle) return;
    const auto cameras = enumerate();
    for (const auto &camera : cameras) {
        if (camera.toMap().value("id").toString() == m_id) { m_missingScans = 0; return; }
    }
    // Two missed scans avoid marking a transient discovery gap as a disconnect.
    if (++m_missingScans < 2) return;
    release();
    emit connectionLost(QStringLiteral("相机连接已丢失，请检查设备后刷新并重新连接。"));
    emit scanFinished(cameras, {});
}
