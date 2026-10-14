#include <QtTest>

#include "app/AppState.h"

class AppStateTest final : public QObject
{
    Q_OBJECT

private slots:
    void defaultsMatchTheObservationWorkspace();
    void openingDeviceSettingsSelectsDeviceAndSingleOverlay();
    void closingOverlayPreservesWorkspace();
    void notificationCanBePresentedAndDismissed();
    void repeatedVisibleNotificationIsCoalesced();
};

void AppStateTest::defaultsMatchTheObservationWorkspace()
{
    AppState state;

    QCOMPARE(state.workspace(), AppState::Capture);
    QCOMPARE(state.captureMode(), AppState::Single);
    QCOMPARE(state.overlay(), AppState::NoOverlay);
    QCOMPARE(state.quickTask(), AppState::NoQuickTask);
    QVERIFY(!state.notificationVisible());
}

void AppStateTest::openingDeviceSettingsSelectsDeviceAndSingleOverlay()
{
    AppState state;
    state.openOverlay(AppState::DeviceConnections);

    state.openDeviceSettings(AppState::GuideCamera);

    QCOMPARE(state.overlay(), AppState::DeviceSettings);
    QCOMPARE(state.settingsDevice(), AppState::GuideCamera);
}

void AppStateTest::closingOverlayPreservesWorkspace()
{
    AppState state;
    state.setWorkspace(AppState::Guide);
    state.openOverlay(AppState::DeviceConnections);

    state.closeOverlay();

    QCOMPARE(state.overlay(), AppState::NoOverlay);
    QCOMPARE(state.workspace(), AppState::Guide);
}

void AppStateTest::notificationCanBePresentedAndDismissed()
{
    AppState state;

    state.showNotification(AppState::Warning,
                           QStringLiteral("未选择设备"),
                           QStringLiteral("请先选择设备型号"));

    QVERIFY(state.notificationVisible());
    QCOMPARE(state.notificationSeverity(), AppState::Warning);
    QCOMPARE(state.notificationTitle(), QStringLiteral("未选择设备"));
    QCOMPARE(state.notificationMessage(), QStringLiteral("请先选择设备型号"));

    state.dismissNotification();
    QVERIFY(!state.notificationVisible());
}

void AppStateTest::repeatedVisibleNotificationIsCoalesced()
{
    AppState state;
    QSignalSpy changed(&state, &AppState::notificationChanged);
    state.showNotification(AppState::Warning, "设备操作失败", "相机未连接");
    state.showNotification(AppState::Warning, "提示", "相机未连接");
    QCOMPARE(changed.count(), 1);
    state.dismissNotification();
    state.showNotification(AppState::Warning, "设备操作失败", "相机未连接");
    QCOMPARE(changed.count(), 3);
}

QTEST_MAIN(AppStateTest)
#include "tst_appstate.moc"
