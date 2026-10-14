import QtQuick
import QtTest
import "../../qml/panels"

TestCase {
    name: "AsterPanels"
    when: windowShown

    function test_devicePanelsExposeFiveStableRoles() {
        const connections = createTemporaryObject(connectionsComponent, this)
        const settings = createTemporaryObject(settingsComponent, this)
        verify(connections !== null)
        verify(settings !== null)
        compare(connections.roleCount, 5)
        compare(settings.roleCount, 5)
    }

    function test_settingsNavigationAndResize() {
        const host = createTemporaryObject(settingsHost, this)
        const settings = host.settings
        verify(waitForRendering(settings))
        const nav = findChild(settings, "deviceSettingsNavigation")
        verify(nav !== null, "Desktop settings need a vertical device list")
        const first = findChild(settings, "deviceSettingsTab0")
        const last = findChild(settings, "deviceSettingsTab4")
        verify(last.y > first.y)
        const spy = createTemporaryObject(spyComponent, this, {target: settings, signalName: "selectedDeviceRequested"})
        mouseClick(last)
        compare(spy.signalArguments[0][0], 4)
        compare(settings.selectedDevice, 4)
        compare(findChild(settings, "deviceSettingsTitle").text, "电调焦")
        mouseClick(settings, settings.width - 8, 70)
        compare(host.backgroundClicks, 0, "Clicks inside the dialog must not reach the scrim")
        const connectionSpy = createTemporaryObject(spyComponent, this, {target: settings, signalName: "connectionsRequested"})
        mouseClick(findChild(settings, "settingsConnectionsButton"))
        compare(connectionSpy.count, 1)
        const grip = findChild(settings, "settingsResizeHandle")
        verify(grip !== null)
        const before = settings.width
        mouseDrag(grip, 7, 7, 40, 20)
        tryVerify(() => settings.width > before + 20)
        verify(settings.width <= host.width - 64)
        const closeSpy = createTemporaryObject(spyComponent, this, {target: settings, signalName: "closeRequested"})
        mouseClick(findChild(settings, "settingsCloseButton"))
        compare(closeSpy.count, 1)
    }

    Component { id: connectionsComponent; DeviceConnectionsPopup { } }
    Component { id: settingsComponent; DeviceSettingsDrawer { } }
    Component { id: spyComponent; SignalSpy {} }
    Component {
        id: settingsHost
        Window {
            id: settingsWindow
            width: 1180; height: 720; visible: true
            property int backgroundClicks: 0
            property alias settings: dialog
            MouseArea { anchors.fill: parent; onClicked: settingsWindow.backgroundClicks++ }
            DeviceSettingsDrawer {
                id: dialog
                anchors.centerIn: parent
                width: Math.min(preferredDialogWidth, parent.width - 64)
                height: Math.min(preferredDialogHeight, parent.height - 64)
                onSelectedDeviceRequested: device => selectedDevice = device
            }
        }
    }
}
