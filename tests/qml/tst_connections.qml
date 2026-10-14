import QtQuick
import QtTest
import "../../qml/panels"

TestCase {
    name: "DesktopConnections"
    when: windowShown
    width: 900; height: 600
    Component { id: noticeSpy; SignalSpy { signalName: "noticeRequested" } }
    function test_emptyScanNotifiesOnceWithoutInlineError() {
        const manager = createTemporaryObject(managerComponent, this)
        const host = createTemporaryObject(dialogComponent, this)
        host.dialog.deviceManager = manager
        const spy = createTemporaryObject(noticeSpy, this, {target: host.dialog})
        manager.scanning = true
        manager.devicesChanged()
        manager.scanning = false
        manager.devicesChanged()
        compare(spy.count, 1)
        verify(spy.signalArguments[0][0].indexOf("未发现主相机") >= 0)
        manager.devicesChanged()
        compare(spy.count, 1)
        compare(findChild(host.dialog, "connectionFeedback").text, "")
    }

    function test_darkDropdownAndKeyboardSelection() {
        const manager = createTemporaryObject(managerComponent, this)
        const host = createTemporaryObject(dialogComponent, this)
        host.dialog.deviceManager = manager
        const selector = findChild(host.dialog, "connectionSelector0")
        selector.model = [{id: "a", name: "AM5N"}, {id: "b", name: "ToupTek Harmonic Mount"}]
        verify(waitForRendering(host.dialog))
        mouseClick(selector)
        tryCompare(selector.popup, "opened", true)
        const background = selector.popup.background.color
        const option = selector.popup.contentItem.itemAtIndex(0)
        verify(option !== null)
        compare(option.contentItem.text, "AM5N")
        verify(background.r < 0.2 && background.g < 0.2 && background.b < 0.2, "Expanded list must keep the dark theme")
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Return)
        tryCompare(selector.popup, "visible", false)
        verify(selector.currentIndex >= 0)
    }

    function test_refreshShowsCameraPluggedWhilePopupOpen() {
        const manager = createTemporaryObject(managerComponent, this)
        const host = createTemporaryObject(dialogComponent, this)
        const dialog = host.dialog
        dialog.deviceManager = manager
        verify(waitForRendering(dialog))
        const selector = findChild(dialog, "connectionSelector1")
        verify(selector !== null)
        compare(selector.count, 0)

        // 相机在弹窗打开后插入：点击刷新应当立即出现在下拉列表中，
        // 无需关闭再打开弹窗（回归：快照绑定缺少刷新依赖）。
        manager.cameraCandidates = [{id: "cam-a", name: "G3M678C"}]
        mouseClick(findChild(dialog, "refreshDevicesButton"))
        tryCompare(selector, "count", 1)
        compare(selector.model[0].name, "G3M678C")
    }

    function test_connectionActionsAndContainedFeedback() {
        const manager = createTemporaryObject(managerComponent, this)
        const host = createTemporaryObject(dialogComponent, this)
        const dialog = host.dialog
        dialog.deviceManager = manager
        verify(waitForRendering(dialog))
        let action = findChild(dialog, "connectDevice0")
        verify(action !== null, "Each row needs an explicit connection action")
        verify(!action.enabled, "No connection without a selected model")
        action = findChild(dialog, "connectDevice1")
        mouseClick(action)
        // 状态列已移除：连接状态经操作按钮文本体现
        tryCompare(findChild(dialog, "connectDevice1"), "text", "断开")
        verify(waitForRendering(dialog))
        mouseClick(findChild(dialog, "connectDevice1"))
        tryCompare(findChild(dialog, "connectDevice1"), "text", "连接")
        verify(waitForRendering(dialog))
        mouseClick(findChild(dialog, "connectSelectedDevices"))
        compare(manager.calls, [[1, true], [1, false], [1, true], [4, true]])
        const feedback = findChild(dialog, "connectionFeedback")
        compare(feedback.text, "", "Operation errors must not occupy the connection panel")
        const point = feedback.mapToItem(dialog, 0, 0)
        verify(point.y >= 0 && point.y + feedback.height <= dialog.height)
        mouseClick(findChild(dialog, "disconnectAllDevices"))
        tryCompare(findChild(dialog, "connectDevice1"), "text", "连接")
        verify(!findChild(dialog, "connectDevice0").enabled, "Mount stays unselected")
    }

    Component {
        id: dialogComponent
        Window {
            width: 900; height: 600; visible: true
            property alias dialog: connectionDialog
            DeviceConnectionsPopup {
                id: connectionDialog
                anchors.centerIn: parent
                width: 640; height: 410
            }
        }
    }
    // Standalone qmltestrunner does not register the application's C++ singleton.
    Component {
        id: managerComponent
        QtObject {
            property var devices: [
                {role: 0, title: "赤道仪", selectedId: "", selectedName: "", connected: false, busy: false, error: "", metrics: {}},
                {role: 1, title: "主相机", selectedId: "camera", selectedName: "SkyEye 62AM", connected: false, busy: false, error: "", metrics: {}},
                {role: 2, title: "导星相机", selectedId: "", selectedName: "", connected: false, busy: false, error: "", metrics: {}},
                {role: 3, title: "滤镜轮", selectedId: "", selectedName: "", connected: false, busy: false, error: "", metrics: {}},
                {role: 4, title: "电调焦", selectedId: "focuser", selectedName: "AAF Pro", connected: false, busy: false, error: "", metrics: {}}
            ]
            property var calls: []
            property string lastError: ""
            property bool scanning: false
            property var cameraCandidates: []
            signal candidatesChanged()
            function candidatesForRole(role) { return role === 1 ? cameraCandidates : [] }
            function refreshDevices() { scanning = true; scanning = false; candidatesChanged() }
            function selectDevice(role, id) { return true }
            function setConnected(role, connected) {
                calls = calls.concat([[role, connected]])
                if (role === 4 && connected) { lastError = "测试连接失败"; return false }
                const next = JSON.parse(JSON.stringify(devices))
                next[role].connected = connected
                devices = next
                lastError = ""
                return true
            }
        }
    }
}
