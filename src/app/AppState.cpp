#include "AppState.h"

#include <QGuiApplication>
#include <QScreen>

AppState::AppState(QObject *parent)
    : QObject(parent)
{
    // 系统主屏切换时通知 QML 侧重新落屏/定位。
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged,
            this, &AppState::notificationScreenChanged);
}

QScreen *AppState::notificationScreen() const
{
    return QGuiApplication::primaryScreen();
}

QRect AppState::notificationArea() const
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect();
}

AppState::Workspace AppState::workspace() const
{
    return m_workspace;
}

void AppState::setWorkspace(Workspace value)
{
    if (m_workspace == value)
        return;
    m_workspace = value;
    emit workspaceChanged();
}

AppState::CaptureMode AppState::captureMode() const
{
    return m_captureMode;
}

void AppState::setCaptureMode(CaptureMode value)
{
    if (m_captureMode == value)
        return;
    m_captureMode = value;
    emit captureModeChanged();
}

AppState::Overlay AppState::overlay() const
{
    return m_overlay;
}

AppState::QuickTask AppState::quickTask() const
{
    return m_quickTask;
}

void AppState::setQuickTask(QuickTask value)
{
    if (m_quickTask == value)
        return;
    m_quickTask = value;
    emit quickTaskChanged();
}

AppState::SettingsDevice AppState::settingsDevice() const
{
    return m_settingsDevice;
}

bool AppState::notificationVisible() const
{
    return m_notificationVisible;
}

AppState::NotificationSeverity AppState::notificationSeverity() const
{
    return m_notificationSeverity;
}

QString AppState::notificationTitle() const
{
    return m_notificationTitle;
}

QString AppState::notificationMessage() const
{
    return m_notificationMessage;
}

void AppState::openOverlay(Overlay value)
{
    if (m_overlay == value)
        return;
    m_overlay = value;
    emit overlayChanged();
}

void AppState::closeOverlay()
{
    openOverlay(NoOverlay);
}

void AppState::openDeviceSettings(SettingsDevice value)
{
    if (m_settingsDevice != value) {
        m_settingsDevice = value;
        emit settingsDeviceChanged();
    }
    openOverlay(DeviceSettings);
}

void AppState::showNotification(NotificationSeverity severity,
                                const QString &title,
                                const QString &message)
{
    if (message.trimmed().isEmpty()) return;
    if (m_notificationVisible && m_notificationSeverity == severity && m_notificationMessage == message)
        return;
    m_notificationSeverity = severity;
    m_notificationTitle = title;
    m_notificationMessage = message;
    m_notificationVisible = true;
    emit notificationChanged();
}

void AppState::dismissNotification()
{
    if (!m_notificationVisible)
        return;
    m_notificationVisible = false;
    emit notificationChanged();
}
