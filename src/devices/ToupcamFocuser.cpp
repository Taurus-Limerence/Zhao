#include "ToupcamFocuser.h"
#include "UsbIdentity.h"
#include <QCoreApplication>
#include <QDir>
#include <algorithm>
#include <cmath>

namespace {
#ifdef Q_OS_WIN
QString sdkText(const wchar_t *value) { return QString::fromWCharArray(value); }
#else
QString sdkText(const char *value) { return QString::fromUtf8(value); }
#endif

// AAF action codes (toupcam.h "AAF: Astro Auto Focuser" block).
constexpr int kAafSetPosition = 0x01;
constexpr int kAafGetPosition = 0x02;
constexpr int kAafSetZero = 0x03;
constexpr int kAafSetDirection = 0x05;
constexpr int kAafGetDirection = 0x06;
constexpr int kAafGetMaxIncrement = 0x08;
constexpr int kAafGetTemp = 0x14;
constexpr int kAafIsMoving = 0x16;
constexpr int kAafHalt = 0x17;
constexpr int kAafSetMaxStep = 0x1b;
constexpr int kAafGetMaxStep = 0x1c;
constexpr int kAafGetStepSize = 0x1e;
} // namespace

ToupcamFocuser::ToupcamFocuser()
    : m_presenceTimer(new QTimer(this))
    , m_pollTimer(new QTimer(this))
{
    m_library.setParent(this);
    m_presenceTimer->setInterval(1500);
    connect(m_presenceTimer, &QTimer::timeout, this, &ToupcamFocuser::checkPresence);
    // Motion completion is found by polling ISMOVING, the NINA convention
    // (the SDK's focus-position event is not consumed by its driver either).
    m_pollTimer->setInterval(200);
    connect(m_pollTimer, &QTimer::timeout, this, &ToupcamFocuser::refreshState);
}

ToupcamFocuser::~ToupcamFocuser() { release(); }

bool ToupcamFocuser::loadSdk(QString &error)
{
    if (m_enum) return true;
#ifdef Q_OS_WIN
    const auto file = QStringLiteral("toupcam.dll");
#else
    const auto file = QStringLiteral("libtoupcam.so");
#endif
    m_library.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath(file));
    if (!m_library.load()) {
        error = QStringLiteral("无法加载电调焦 SDK：%1\n%2").arg(m_library.fileName(), m_library.errorString());
        return false;
    }
    const auto enumerateFunction = reinterpret_cast<decltype(m_enum)>(m_library.resolve("Toupcam_EnumV2"));
    m_open = reinterpret_cast<decltype(m_open)>(m_library.resolve("Toupcam_Open"));
    m_close = reinterpret_cast<decltype(m_close)>(m_library.resolve("Toupcam_Close"));
    m_aafFn = reinterpret_cast<decltype(m_aafFn)>(m_library.resolve("Toupcam_AAF"));
    m_serial = reinterpret_cast<decltype(m_serial)>(m_library.resolve("Toupcam_get_SerialNumber"));
    m_version = reinterpret_cast<decltype(m_version)>(m_library.resolve("Toupcam_Version"));
    if (!enumerateFunction || !m_open || !m_close || !m_aafFn || !m_serial || !m_version) {
        error = QStringLiteral("电调焦 SDK 缺少必要接口，请使用项目内配套的 DLL/SO。");
        m_library.unload();
        return false;
    }
    m_enum = enumerateFunction;
    return true;
}

bool ToupcamFocuser::aaf(int action, int value, int *result)
{
    if (!m_handle || !m_aafFn) return false;
    int storage = 0;
    // NINA-verified convention: value travels in the by-value parameter,
    // the pointer receives the answer. It is never null.
    const HRESULT hr = m_aafFn(m_handle, action, value, result ? result : &storage);
    return SUCCEEDED(hr);
}

QVariantList ToupcamFocuser::enumerate()
{
    ToupcamDeviceV2 devices[TOUPCAM_MAX]{};
    const unsigned count = (std::min)(m_enum(devices), unsigned(TOUPCAM_MAX));
    QVariantList result;
    for (unsigned i = 0; i < count; ++i) {
        const auto &device = devices[i];
        const auto *model = device.model;
        if (!model || !(model->flag & TOUPCAM_FLAG_AUTOFOCUSER)) continue;
        const QString id = sdkText(device.id);
        const QString name = sdkText(device.displayname);
        result.append(QVariantMap{{"id", id}, {"name", name + QStringLiteral(" [%1]").arg(id.right(12))},
                                  {"model", model ? sdkText(model->name) : QString{}}});
    }
    return result;
}

void ToupcamFocuser::scan()
{
    QString error;
    if (!loadSdk(error)) { emit scanFinished({}, error); return; }
    emit scanFinished(enumerate(), {});
}

void ToupcamFocuser::open(const QString &id)
{
    QString error;
    if (!loadSdk(error)) { emit connectionFinished(false, {}, error); return; }
    if (m_handle) { emit connectionFinished(true, {}, QStringLiteral("电调焦已经打开，请先断开。")); return; }
    QVariantMap information;
    for (const auto &candidate : enumerate()) {
        if (candidate.toMap().value("id").toString() == id) { information = candidate.toMap(); break; }
    }
    if (id.isEmpty() || information.isEmpty()) {
        emit connectionFinished(false, {}, QStringLiteral("所选电调焦已不可用，请刷新设备列表。"));
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
        emit connectionFinished(false, {}, QStringLiteral("电调焦打开失败。请检查 USB 连接与驱动，并确认设备未被其他软件占用。"));
        return;
    }
    m_id = id;
    m_missingScans = 0;
    // Connect sequence per the NINA driver: direction, max step, position,
    // temperature and step size; no defaults are pushed.
    int value = 0;
    if (aaf(kAafGetDirection, 0, &value)) m_reversed = value != 0;
    if (aaf(kAafGetMaxStep, 0, &value) && value > 0) m_maxStep = value;
    if (aaf(kAafGetStepSize, 0, &value)) m_stepSize = value;
    char serial[32]{};
    if (SUCCEEDED(m_serial(m_handle, serial)))
        information.insert("serial", QString::fromLatin1(serial, int(std::find(serial, serial + 32, '\0') - serial)));
    information.insert("sdkVersion", sdkText(m_version()));
    const UsbIdentity usb = usbIdentityForDeviceId(id);
    information.insert(QStringLiteral("vendorId"), usb.vendorId);
    information.insert(QStringLiteral("productId"), usb.productId);
    information.insert(QStringLiteral("deviceId"), usb.instanceId);
    information.insert("maxStep", m_maxStep);
    information.insert("reversed", m_reversed);
    information.insert("stepSize", m_stepSize);
    refreshState();
    information.insert("position", m_position);
    information.insert("moving", m_moving);
    information.insert("temperature", m_temperature);
    m_presenceTimer->start();
    emit connectionFinished(true, information, {});
}

void ToupcamFocuser::release()
{
    m_presenceTimer->stop();
    m_pollTimer->stop();
    if (m_handle) { m_close(m_handle); m_handle = nullptr; }
    m_id.clear();
    m_missingScans = 0;
    m_position = -1;
    m_moving = false;
    m_maxStep = 0;
    m_reversed = false;
    m_temperature = 0.0;
    m_stepSize = 0;
}

void ToupcamFocuser::close()
{
    release();
    emit connectionFinished(false, {}, {});
}

void ToupcamFocuser::moveTo(int position)
{
    if (!m_handle) return;
    if (m_maxStep > 0 && (position < 0 || position > m_maxStep)) {
        emit stateChanged({{"error", QStringLiteral("调焦位置超出行程范围")}});
        return;
    }
    if (!aaf(kAafSetPosition, position, nullptr)) {
        emit stateChanged({{"error", QStringLiteral("调焦移动指令被设备拒绝")}});
        return;
    }
    m_moving = true;
    reportPosition();
    m_pollTimer->start();
}

void ToupcamFocuser::halt()
{
    if (!m_handle) return;
    if (!aaf(kAafHalt, 0, nullptr)) {
        emit stateChanged({{"error", QStringLiteral("调焦停止指令被设备拒绝，请重试停止")}});
        return;
    }
    m_pollTimer->start();
    refreshState();
}

void ToupcamFocuser::setReversed(bool reversed)
{
    if (!m_handle) return;
    if (aaf(kAafSetDirection, reversed ? 1 : 0, nullptr)) {
        m_reversed = reversed;
        reportPosition();
    }
}

void ToupcamFocuser::setMaxStep(int maxStep)
{
    if (!m_handle || maxStep <= 0) return;
    if (aaf(kAafSetMaxStep, maxStep, nullptr)) {
        m_maxStep = maxStep;
        reportPosition();
    } else {
        emit stateChanged({{"error", QStringLiteral("设置最大行程被设备拒绝")}});
    }
}

void ToupcamFocuser::setZero()
{
    if (!m_handle) return;
    if (aaf(kAafSetZero, 0, nullptr)) {
        // The counter re-anchors at the current physical spot.
        int value = 0;
        if (aaf(kAafGetPosition, 0, &value)) m_position = value;
        m_moving = false;
        m_pollTimer->stop();
        reportPosition();
    } else {
        emit stateChanged({{"error", QStringLiteral("位置清零被设备拒绝")}});
    }
}

void ToupcamFocuser::setCurrentPosition(int position)
{
    if (!m_handle || position < 0) return;
    if (!aaf(kAafSetZero, position, nullptr)) {
        emit stateChanged({{"error", QStringLiteral("设备不支持将当前位置设为指定值，仅支持清零")}});
        return;
    }
    int value = 0;
    if (aaf(kAafGetPosition, 0, &value)) m_position = value;
    m_moving = false;
    m_pollTimer->stop();
    reportPosition();
}

void ToupcamFocuser::refreshState()
{
    if (!m_handle) { m_pollTimer->stop(); return; }
    int value = 0;
    bool changed = false;
    if (aaf(kAafGetPosition, 0, &value) && value != m_position) { m_position = value; changed = true; }
    if (aaf(kAafIsMoving, 0, &value)) {
        const bool moving = value != 0;
        if (moving != m_moving) { m_moving = moving; changed = true; }
    }
    if (aaf(kAafGetTemp, 0, &value)) {
        const double temperature = value / 10.0;
        if (std::abs(temperature - m_temperature) >= 0.2) { m_temperature = temperature; changed = true; }
    }
    if (!m_moving) m_pollTimer->stop();
    if (changed) reportPosition();
}

void ToupcamFocuser::reportPosition()
{
    emit stateChanged({{"position", m_position}, {"moving", m_moving}, {"maxStep", m_maxStep},
                       {"reversed", m_reversed}, {"temperature", m_temperature}, {"stepSize", m_stepSize}});
}

void ToupcamFocuser::checkPresence()
{
    if (!m_handle) return;
    const auto focusers = enumerate();
    for (const auto &focuser : focusers) {
        if (focuser.toMap().value("id").toString() == m_id) { m_missingScans = 0; return; }
    }
    // Two missed scans avoid marking a transient discovery gap as a disconnect.
    if (++m_missingScans < 2) return;
    release();
    emit connectionLost(QStringLiteral("电调焦连接已丢失，请检查设备后刷新并重新连接。"));
    emit scanFinished(focusers, {});
}
