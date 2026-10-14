import QtQuick
import QtQuick.Layouts
import "../controls"
import "../theme"

// Focuser settings: the 电调焦位置 group is live (device metrics + target /
// travel / zero controls); connection management stays in the connections
// popup like every other device; autofocus shares the main panel's settings.
SettingsPage {
    id: root
    property var deviceManager: null
    property var autoFocusController: null
    readonly property var autoFocusSettings: autoFocusController ? autoFocusController.settings : ({})
    readonly property bool autoFocusEditable: !!autoFocusController && !autoFocusController.busy

    readonly property var focuser: {
        const devices = deviceManager ? deviceManager.devices : []
        for (let i = 0; i < devices.length; ++i) if (devices[i].role === 4) return devices[i]
        return {}
    }
    readonly property var metrics: focuser.metrics || {}
    readonly property int position: metrics.position !== undefined ? metrics.position : -1
    readonly property int maxStep: metrics.maxStep !== undefined ? metrics.maxStep : 0
    readonly property string positionText: position < 0 ? "—" : metrics.moving ? position + "（移动中）" : String(position)
    readonly property string temperatureText: metrics.temperature !== undefined ? metrics.temperature.toFixed(1) + " °C" : "—"
    property int focusTarget: 0

    sections: [
        { title: "设备连接", rows: [
            { label: "设备", type: "select", options: focuser.selectedName ? [focuser.selectedName] : [], wide: true },
            { label: "连接", type: "toggle", checked: !!focuser.connected }
        ]},
        { title: "电调焦位置", rows: [
            { label: "当前位置", value: positionText },
            { label: "温度", value: temperatureText },
            { label: "最大步数", value: maxStep > 0 ? String(maxStep) : "—" },
            { label: "反向", type: "toggle", checked: !!metrics.reversed,
              enabled: !!focuser.connected,
              onToggled: checked => deviceManager.setFocuserReversed(checked) }
        ]},
        { title: "自动对焦", rows: [
            { label: "步长", type: "field", enabled: autoFocusEditable,
              value: autoFocusSettings.step, minimum: 1, maximum: 100000, decimals: 0,
              onEdited: value => autoFocusController.setSetting("step", Number(value)) },
            { label: "采样点（奇数）", type: "select", enabled: autoFocusEditable,
              options: ["7", "9", "11", "13", "15"], currentIndex: (autoFocusSettings.points - 7) / 2,
              onActivated: index => autoFocusController.setSetting("points", 7 + 2 * index) },
            { label: "曝光 / 秒", type: "field", enabled: autoFocusEditable,
              value: autoFocusSettings.exposure, minimum: 0.001, maximum: 3600, decimals: 3,
              onEdited: value => autoFocusController.setSetting("exposure", Number(value)) },
            { label: "每点帧数", type: "select", enabled: autoFocusEditable,
              options: ["1", "2", "3"], currentIndex: autoFocusSettings.frames - 1,
              onActivated: index => autoFocusController.setSetting("frames", index + 1) },
            { label: "回差补偿 / 步", type: "field", enabled: autoFocusEditable,
              value: autoFocusSettings.backlash, minimum: 0, maximum: 100000, decimals: 0,
              onEdited: value => autoFocusController.setSetting("backlash", Number(value)) },
            { label: "增益 / 滤镜", value: "使用当前相机与滤镜设置" }
        ]},
        { title: "手动对焦", rows: [
            { label: "回差", type: "toggle", checked: false },
            { label: "回差量", value: "30 步" },
            { label: "粗调", value: "100 步" },
            { label: "精调", value: "10 步" },
            { label: "蜂鸣", type: "toggle", checked: false }
        ]},
        { title: "", rows: [{ label: "高级设置", type: "button", value: "打开 ›" }] }
    ]

    // Live controls below the parameter groups (the channel-table pattern).
    footer: controlFooter

    Component {
        id: controlFooter
        ColumnLayout {
            spacing: AsterTheme.space2
            Text {
                text: "运动控制"
                color: AsterTheme.deviceTitleText
                font.family: AsterTheme.uiFont
                font.pixelSize: 12
                font.bold: true
            }
            DesktopField {
                objectName: "focuserTargetField"
                Layout.fillWidth: true
                label: "目标位置"; editable: true; unit: "步"
                value: String(root.focusTarget)
                minimum: 0; maximum: Math.max(root.maxStep, 1)
                onValueEdited: value => root.focusTarget = Math.round(Number(value))
            }
            DesktopField {
                objectName: "focuserMaxStepField"
                Layout.fillWidth: true
                label: "最大步数"; editable: true; unit: "步"
                value: root.maxStep > 0 ? String(root.maxStep) : ""
                minimum: 1; maximum: 1000000
                onValueEdited: value => {
                    const maxStep = Math.round(Number(value))
                    if (maxStep > 0) root.deviceManager.setFocuserMaxStep(maxStep)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                DesktopButton {
                    objectName: "focuserMoveButton"
                    Layout.fillWidth: true
                    text: "移动到目标"
                    primary: true
                    enabled: !!root.focuser.connected && !root.metrics.moving
                    onClicked: root.deviceManager.moveFocuser(root.focusTarget)
                }
                DesktopButton {
                    objectName: "focuserZeroButton"
                    Layout.fillWidth: true
                    text: "设为零位"
                    enabled: !!root.focuser.connected && root.position > 0
                    onClicked: root.deviceManager.resetFocuserPosition()
                }
                DesktopButton {
                    objectName: "focuserSetCurrentButton"
                    Layout.fillWidth: true
                    text: "当前设为 " + root.focusTarget
                    enabled: !!root.focuser.connected
                    onClicked: root.deviceManager.setCurrentFocuserPosition(root.focusTarget)
                }
            }
        }
    }
}
