import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"
import "../theme"

ColumnLayout {
    id: root
    property var controller: null
    property bool ready: false
    property bool targetValid: false
    property string targetName: ""
    property real targetRaHours: 0
    property real targetDecDeg: 0
    property real exposureSeconds: 2
    property real fovHeight: 0
    property bool showOnlySlew: false
    property bool onlySlewReady: false
    property bool onlySlewMode: false
    readonly property bool active: controller && controller.busy
    signal onlySlewRequested()
    spacing: 6
    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        DesktopButton {
            objectName: "startCentering"
            Layout.fillWidth: true
            text: root.onlySlewMode ? "仅指向" : "指向并居中"
            enabled: root.targetValid && !root.active && (root.onlySlewMode ? root.onlySlewReady : root.controller && root.ready)
            onClicked: {
                if (root.onlySlewMode) { root.onlySlewRequested(); return }
                confirm.ra=root.targetRaHours; confirm.dec=root.targetDecDeg; confirm.name=root.targetName
                confirm.fov=root.fovHeight
                exposure.text=String(root.exposureSeconds)
                confirm.open()
            }
            ToolTip.visible: hovered
            ToolTip.text: root.onlySlewMode ? "仅移动至所选目标，不拍摄、不解析" : "移动至所选目标，再拍摄解析校正；不调用赤道仪坐标同步"
        }
        DesktopButton {
            visible: root.showOnlySlew; text: "▾"
            enabled: root.targetValid && root.onlySlewReady && !root.active
            onClicked: actions.open()
            DesktopMenu {
                id: actions
                y: parent.height+2
                DesktopMenuItem { objectName: "onlySlewMode"; text: "仅指向"; checkable: true; checked: root.onlySlewMode; onTriggered: root.onlySlewMode=true }
                DesktopMenuItem { objectName: "centerMode"; text: "指向并居中"; checkable: true; checked: !root.onlySlewMode; onTriggered: root.onlySlewMode=false }
            }
        }
        DesktopButton {
            objectName: "stopCentering"; text: "停止"
            visible: root.active; enabled: root.active
            onClicked: root.controller.cancel()
        }
    }
    Text {
        Layout.fillWidth: true; visible: root.controller && root.controller.status.length>0
        color: root.controller && root.controller.succeeded ? "#61c99b" : AsterTheme.textSecondary
        font.pixelSize: 11; wrapMode: Text.Wrap
        text: root.controller ? root.controller.targetName+" · "+root.controller.status
            +(root.active && isFinite(root.controller.separationArcmin) ? "\n当前偏差 "+root.controller.separationArcmin.toFixed(2)+"′" : "") : ""
    }
    Dialog {
        id: confirm
        objectName: "confirmCentering"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(400, parent ? parent.width-24 : 400)
        modal: true
        property real ra: 0
        property real dec: 0
        property real fov: 0
        property string name: ""
        background: Rectangle { color: AsterTheme.surface; border.color: AsterTheme.stroke; radius: 4 }
        header: Text { text: "确认指向并居中"; color: AsterTheme.textPrimary; font.pixelSize: 12; font.bold: true; padding: 12 }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: AsterTheme.textPrimary; font.pixelSize: 11
                text: confirm.name+" · J2000\nRA "+confirm.ra.toFixed(6)+"h  Dec "+confirm.dec.toFixed(5)+"°"
                    +"\n将移动赤道仪并重新拍照验证。不调用 Sync，不自动解除停放。请确认站点、时间和设备周围安全。"
            }
            RowLayout {
                Text { text: "曝光 / 秒"; color: AsterTheme.textSecondary; font.pixelSize: 11 }
                TextField {
                    id: exposure; Layout.fillWidth: true; text: "2"; font.pixelSize: 11; color: AsterTheme.textPrimary
                    validator: DoubleValidator { bottom: 0.000001; top: 3600; locale: "C"; notation: DoubleValidator.StandardNotation }
                    background: Rectangle { color: AsterTheme.canvas; border.color: AsterTheme.stroke; radius: 3 }
                }
            }
            DesktopButton { text: (advanced.visible ? "▾ " : "▸ ")+"高级设置"; onClicked: advanced.visible=!advanced.visible }
            GridLayout {
                id: advanced; visible: false; columns: 2; Layout.fillWidth: true
                Text { text: "容差 / 角分"; color: AsterTheme.textSecondary; font.pixelSize: 11 }
                TextField {
                    id: tolerance; text: "1"; Layout.fillWidth: true; font.pixelSize: 11; color: AsterTheme.textPrimary
                    validator: DoubleValidator { bottom: 0.1; top: 10; locale: "C"; notation: DoubleValidator.StandardNotation }
                    background: Rectangle { color: AsterTheme.canvas; border.color: AsterTheme.stroke; radius: 3 }
                }
                Text { text: "最多校正 / 次"; color: AsterTheme.textSecondary; font.pixelSize: 11 }
                TextField {
                    id: limit; text: "3"; Layout.fillWidth: true; font.pixelSize: 11; color: AsterTheme.textPrimary
                    validator: IntValidator { bottom: 1; top: 3 }
                    background: Rectangle { color: AsterTheme.canvas; border.color: AsterTheme.stroke; radius: 3 }
                }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "每次校正后重新拍摄验证 · 不旋转相机"; font.pixelSize: 10; color: AsterTheme.textMuted }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            DesktopButton { text: "取消"; onClicked: confirm.close() }
            DesktopButton {
                objectName: "acceptCentering"; text: "确认移动"; primary: true
                enabled: root.ready && !root.active && exposure.acceptableInput && tolerance.acceptableInput && limit.acceptableInput
                onClicked: {
                    confirm.close()
                    root.controller.start(confirm.ra,confirm.dec,confirm.name,Number(exposure.text),confirm.fov,Number(tolerance.text),Number(limit.text))
                }
            }
        }
    }
}
