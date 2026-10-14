#include "ToupcamFilterWheel.h"
#include "UsbIdentity.h"
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <algorithm>

namespace {
#ifdef Q_OS_WIN
QString sdkText(const wchar_t *value) { return QString::fromWCharArray(value); }
#else
QString sdkText(const char *value) { return QString::fromUtf8(value); }
#endif
bool looksLikeWheel(const QString &displayname)
{
    const auto upper = displayname.toUpper();
    return upper.contains(QStringLiteral("EFW")) || upper.contains(QStringLiteral("FILTER"));
}
}

ToupcamFilterWheel::ToupcamFilterWheel()
    : m_presenceTimer(new QTimer(this))
    , m_pollTimer(new QTimer(this))
{
    m_library.setParent(this);
    m_presenceTimer->setInterval(1500);
    connect(m_presenceTimer, &QTimer::timeout, this, &ToupcamFilterWheel::checkPresence);
    // The SDK offers no motion event; completion is found by polling option 0x49.
    m_pollTimer->setInterval(200);
    connect(m_pollTimer, &QTimer::timeout, this, &ToupcamFilterWheel::pollMovement);
}

ToupcamFilterWheel::~ToupcamFilterWheel() { release(); }

bool ToupcamFilterWheel::loadSdk(QString &error)
{
    if (m_enum) return true;
#ifdef Q_OS_WIN
    const auto file = QStringLiteral("toupcam.dll");
#else
    const auto file = QStringLiteral("libtoupcam.so");
#endif
    m_library.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath(file));
    if (!m_library.load()) {
        error = QStringLiteral("无法加载滤镜轮 SDK：%1\n%2").arg(m_library.fileName(), m_library.errorString());
        return false;
    }
    const auto enumerateFunction = reinterpret_cast<decltype(m_enum)>(m_library.resolve("Toupcam_EnumV2"));
    m_open = reinterpret_cast<decltype(m_open)>(m_library.resolve("Toupcam_Open"));
    m_close = reinterpret_cast<decltype(m_close)>(m_library.resolve("Toupcam_Close"));
    m_putOption = reinterpret_cast<decltype(m_putOption)>(m_library.resolve("Toupcam_put_Option"));
    m_getOption = reinterpret_cast<decltype(m_getOption)>(m_library.resolve("Toupcam_get_Option"));
    m_version = reinterpret_cast<decltype(m_version)>(m_library.resolve("Toupcam_Version"));
    m_serial = reinterpret_cast<decltype(m_serial)>(m_library.resolve("Toupcam_get_SerialNumber"));
    if (!enumerateFunction || !m_open || !m_close || !m_putOption || !m_getOption || !m_version || !m_serial) {
        error = QStringLiteral("滤镜轮 SDK 缺少必要接口，请使用项目内配套的 DLL/SO。");
        m_library.unload();
        return false;
    }
    m_enum = enumerateFunction;
    return true;
}

QVariantList ToupcamFilterWheel::enumerate()
{
    ToupcamDeviceV2 devices[TOUPCAM_MAX]{};
    const unsigned count = (std::min)(m_enum(devices), unsigned(TOUPCAM_MAX));
    QVariantList result;
    for (unsigned i = 0; i < count; ++i) {
        const auto &device = devices[i];
        const auto *model = device.model;
        const QString name = sdkText(device.displayname);
        if (!(model && (model->flag & TOUPCAM_FLAG_FILTERWHEEL)) && !looksLikeWheel(name)) continue;
        const QString id = sdkText(device.id);
        result.append(QVariantMap{{"id", id}, {"name", name + QStringLiteral(" [%1]").arg(id.right(12))},
                                  {"model", model ? sdkText(model->name) : QString{}}});
    }
    return result;
}

void ToupcamFilterWheel::scan()
{
    QString error;
    if (!loadSdk(error)) { emit scanFinished({}, error); return; }
    emit scanFinished(enumerate(), {});
}

bool ToupcamFilterWheel::refreshCapability(QVariantMap &information, QString &error)
{
    int slotCount = 0;
    // Each connection starts with seven slots; users can change this after connecting.
    qInfo() << "[FilterWheel] initialize slots=" << 7;
    if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_FILTERWHEEL_SLOT, 7))
        || FAILED(m_getOption(m_handle, TOUPCAM_OPTION_FILTERWHEEL_SLOT, &slotCount))) {
        error = QStringLiteral("滤镜轮孔数初始化失败，请重新连接设备。");
        return false;
    }
    if (slotCount != 7) {
        error = QStringLiteral("滤镜轮孔数初始化未生效：期望 7，回读 %1").arg(slotCount);
        return false;
    }
    m_slots = slotCount;
    int position = 0;
    if (FAILED(m_getOption(m_handle, TOUPCAM_OPTION_FILTERWHEEL_POSITION, &position))) {
        error = QStringLiteral("滤镜轮位置读取失败，请重新连接设备。");
        return false;
    }
    if (position >= 0) {
        m_position = position;
    } else if (position == -1) {
        m_moving = true;
        m_motionElapsed.start();
        m_pollTimer->start();
    } else {
        // Position unknown (-2): only a calibration run brings the wheel back to a known slot.
        if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_FILTERWHEEL_POSITION, -1))) {
            error = QStringLiteral("滤镜轮初始校准失败，请重新连接设备。");
            return false;
        }
        m_position = -1;
        m_moving = true;
        m_calibrating = true;
        m_motionElapsed.start();
        m_pollTimer->start();
    }
    information.insert(QStringLiteral("slots"), m_slots);
    information.insert(QStringLiteral("position"), m_position);
    information.insert(QStringLiteral("moving"), m_moving);
    information.insert(QStringLiteral("calibrating"), m_calibrating);
    return true;
}

void ToupcamFilterWheel::open(const QString &id)
{
    QString error;
    if (!loadSdk(error)) { emit connectionFinished(false, {}, error); return; }
    if (m_handle) { emit connectionFinished(true, {}, QStringLiteral("滤镜轮已经打开，请先断开。")); return; }
    QVariantMap information;
    for (const auto &candidate : enumerate()) {
        if (candidate.toMap().value("id").toString() == id) { information = candidate.toMap(); break; }
    }
    if (id.isEmpty() || information.isEmpty()) {
        emit connectionFinished(false, {}, QStringLiteral("所选滤镜轮已不可用，请刷新设备列表。"));
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
        emit connectionFinished(false, {}, QStringLiteral("滤镜轮打开失败。请检查 USB 连接与驱动，并确认设备未被其他软件占用。"));
        return;
    }
    m_id = id;
    m_missingScans = 0;
    char serial[32]{};
    if (SUCCEEDED(m_serial(m_handle, serial)))
        information.insert("serial", QString::fromLatin1(serial, int(std::find(serial, serial + 32, '\0') - serial)));
    if (!refreshCapability(information, error)) {
        release();
        emit connectionFinished(false, {}, error);
        return;
    }
    information.insert(QStringLiteral("sdkVersion"), sdkText(m_version()));
    const UsbIdentity usb = usbIdentityForDeviceId(id);
    information.insert(QStringLiteral("vendorId"), usb.vendorId);
    information.insert(QStringLiteral("productId"), usb.productId);
    information.insert(QStringLiteral("deviceId"), usb.instanceId);
    m_presenceTimer->start();
    emit connectionFinished(true, information, {});
}

void ToupcamFilterWheel::release()
{
    m_presenceTimer->stop();
    m_pollTimer->stop();
    if (m_handle) { m_close(m_handle); m_handle = nullptr; }
    m_id.clear();
    m_missingScans = 0;
    m_slots = 0;
    m_position = -1;
    m_moving = false;
    m_calibrating = false;
    m_targetPosition = -1;
    m_motionElapsed.invalidate();
}

void ToupcamFilterWheel::close()
{
    release();
    emit connectionFinished(false, {}, {});
}

void ToupcamFilterWheel::moveTo(int position, bool bidirectional)
{
    if (!m_handle || position < 0 || position >= m_slots) return;
    const int value = (position & 0xff) | ((bidirectional ? 1 : 0) << 8);
    qInfo() << "[FilterWheel] move slot=" << position+1 << "sdkPosition=" << position
            << "command=" << value << "slots=" << m_slots << "autoDirection=" << bidirectional;
    if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_FILTERWHEEL_POSITION, value))) {
        m_pollTimer->stop();
        m_moving = false;
        m_calibrating = false;
        handleIoFailure(QStringLiteral("切换滤镜"));
        return;
    }
    m_targetPosition = position;
    m_lastReadPosition = -999;
    m_motionElapsed.start();
    m_moving = true;
    reportState();
    m_pollTimer->start();
}

void ToupcamFilterWheel::calibrate()
{
    if (!m_handle) return;
    qInfo() << "[FilterWheel] calibrate slots=" << m_slots;
    if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_FILTERWHEEL_POSITION, -1))) {
        m_pollTimer->stop();
        m_moving = false;
        m_calibrating = false;
        handleIoFailure(QStringLiteral("校准滤镜轮"));
        return;
    }
    m_position = -1;
    m_targetPosition = -1;
    m_lastReadPosition = -999;
    m_motionElapsed.start();
    m_moving = true;
    m_calibrating = true;
    reportState();
    m_pollTimer->start();
}

void ToupcamFilterWheel::setSlotCount(int slotCount)
{
    if (!m_handle || (slotCount != 5 && slotCount != 7 && slotCount != 8)) return;
    qInfo() << "[FilterWheel] set slots=" << slotCount;
    if (FAILED(m_putOption(m_handle, TOUPCAM_OPTION_FILTERWHEEL_SLOT, slotCount))) {
        handleIoFailure(QStringLiteral("设置轮孔数量"));
        return;
    }
    int value = 0;
    if (FAILED(m_getOption(m_handle, TOUPCAM_OPTION_FILTERWHEEL_SLOT, &value))) {
        handleIoFailure(QStringLiteral("读取轮孔数量"));
        return;
    }
    m_slots = value;
    if (m_position >= m_slots) {
        // The current slot no longer exists on the smaller wheel; recalibrate to a known state.
        calibrate();
        return;
    }
    reportState();
}

void ToupcamFilterWheel::checkPresence()
{
    if (!m_handle) return;
    const auto wheels = enumerate();
    for (const auto &wheel : wheels) {
        if (wheel.toMap().value("id").toString() == m_id) { m_missingScans = 0; return; }
    }
    // Two missed scans avoid marking a transient discovery gap as a disconnect.
    if (++m_missingScans < 2) return;
    release();
    emit connectionLost(QStringLiteral("滤镜轮连接已丢失，请检查设备后刷新并重新连接。"));
    emit scanFinished(wheels, {});
}

void ToupcamFilterWheel::pollMovement()
{
    if (!m_handle || !m_moving) { m_pollTimer->stop(); return; }
    int value = 0;
    if (FAILED(m_getOption(m_handle, TOUPCAM_OPTION_FILTERWHEEL_POSITION, &value))) {
        m_pollTimer->stop();
        m_moving = false;
        m_calibrating = false;
        handleIoFailure(QStringLiteral("读取滤镜轮位置"));
        return;
    }
    if (value != m_lastReadPosition) {
        qInfo() << "[FilterWheel] read sdkPosition=" << value << "target=" << m_targetPosition
                << "slots=" << m_slots << "calibrating=" << m_calibrating;
        m_lastReadPosition = value;
    }
    if (value >= m_slots || value < -1) {
        handleIoFailure(QStringLiteral("滤镜轮返回非法位置 %1（孔数 %2）").arg(value).arg(m_slots));
        return;
    }
    // A previous position can be returned before motion starts. Do not treat it as arrival.
    if (value == -1 || (m_targetPosition >= 0 && value != m_targetPosition)) {
        if (m_motionElapsed.isValid() && m_motionElapsed.elapsed() >= 60000)
            handleIoFailure(m_targetPosition >= 0
                ? QStringLiteral("滤镜轮到位超时，目标孔 %1，SDK 回读 %2").arg(m_targetPosition+1).arg(value)
                : QStringLiteral("滤镜轮校准等待超时，SDK 回读 %1").arg(value));
        return;
    }
    m_position = value;
    m_moving = false;
    m_calibrating = false;
    m_targetPosition = -1;
    m_motionElapsed.invalidate();
    m_pollTimer->stop();
    reportState();
}

void ToupcamFilterWheel::reportState()
{
    emit stateChanged(QVariantMap{{"slots", m_slots}, {"position", m_position},
                                  {"moving", m_moving}, {"calibrating", m_calibrating}});
}

void ToupcamFilterWheel::handleIoFailure(const QString &action)
{
    // A failed option exchange latches the wheel off until the user reconnects.
    release();
    emit connectionLost(QStringLiteral("%1失败，滤镜轮连接已断开，请重新连接。").arg(action));
}
