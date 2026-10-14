#pragma once

#include <QObject>
#include <QRect>
#include <QScreen>
#include <QString>

class AppState final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Workspace workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged)
    Q_PROPERTY(CaptureMode captureMode READ captureMode WRITE setCaptureMode NOTIFY captureModeChanged)
    Q_PROPERTY(Overlay overlay READ overlay NOTIFY overlayChanged)
    Q_PROPERTY(QuickTask quickTask READ quickTask WRITE setQuickTask NOTIFY quickTaskChanged)
    Q_PROPERTY(SettingsDevice settingsDevice READ settingsDevice NOTIFY settingsDeviceChanged)
    Q_PROPERTY(bool notificationVisible READ notificationVisible NOTIFY notificationChanged)
    Q_PROPERTY(NotificationSeverity notificationSeverity READ notificationSeverity NOTIFY notificationChanged)
    Q_PROPERTY(QString notificationTitle READ notificationTitle NOTIFY notificationChanged)
    Q_PROPERTY(QString notificationMessage READ notificationMessage NOTIFY notificationChanged)
    // 桌面通知窗口的落屏与工作区（NINA 默认 PrimaryScreenWorkAreaProvider——
    // 主屏工作区，不跟随主窗口）。QML 侧既无可靠主屏 API，QScreen 的
    // availableGeometry 也无法从 QML 访问（实测 undefined），故两者都经
    // C++ 显式暴露。
    Q_PROPERTY(QScreen *notificationScreen READ notificationScreen NOTIFY notificationScreenChanged)
    Q_PROPERTY(QRect notificationArea READ notificationArea NOTIFY notificationScreenChanged)

public:
    enum Workspace { Capture, Guide, SkyMap, Sequence };
    Q_ENUM(Workspace)

    enum CaptureMode { Single, Recording, Plan, Stacking };
    Q_ENUM(CaptureMode)

    enum Overlay { NoOverlay, DeviceConnections, DeviceSettings };
    Q_ENUM(Overlay)

    enum QuickTask { NoQuickTask, MountTask, FocusTask, SkyMapTask, GuideTask };
    Q_ENUM(QuickTask)

    enum SettingsDevice { Mount, MainCamera, GuideCamera, FilterWheel, Focuser };
    Q_ENUM(SettingsDevice)

    enum NotificationSeverity { Information, Warning, Error };
    Q_ENUM(NotificationSeverity)

    explicit AppState(QObject *parent = nullptr);

    Workspace workspace() const;
    void setWorkspace(Workspace value);

    CaptureMode captureMode() const;
    void setCaptureMode(CaptureMode value);

    Overlay overlay() const;
    QuickTask quickTask() const;
    void setQuickTask(QuickTask value);
    SettingsDevice settingsDevice() const;

    bool notificationVisible() const;
    NotificationSeverity notificationSeverity() const;
    QString notificationTitle() const;
    QString notificationMessage() const;
    QScreen *notificationScreen() const;
    QRect notificationArea() const;

    Q_INVOKABLE void openOverlay(Overlay value);
    Q_INVOKABLE void closeOverlay();
    Q_INVOKABLE void openDeviceSettings(SettingsDevice value);
    Q_INVOKABLE void showNotification(NotificationSeverity severity,
                                      const QString &title,
                                      const QString &message);
    Q_INVOKABLE void dismissNotification();

signals:
    void workspaceChanged();
    void captureModeChanged();
    void overlayChanged();
    void quickTaskChanged();
    void settingsDeviceChanged();
    void notificationChanged();
    void notificationScreenChanged();

private:
    Workspace m_workspace = Capture;
    CaptureMode m_captureMode = Single;
    Overlay m_overlay = NoOverlay;
    QuickTask m_quickTask = NoQuickTask;
    SettingsDevice m_settingsDevice = Mount;
    bool m_notificationVisible = false;
    NotificationSeverity m_notificationSeverity = Information;
    QString m_notificationTitle;
    QString m_notificationMessage;
};
