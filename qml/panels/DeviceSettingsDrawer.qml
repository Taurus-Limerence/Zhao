import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"
import "../settings"
import "../theme"

Rectangle {
    id: root
    readonly property int roleCount: 5
    property int selectedDevice: 0
    property var deviceManager: null
    property var autoFocusController: null
    property var planner: null
    property real preferredDialogWidth: 860
    property real preferredDialogHeight: 620
    signal closeRequested()
    signal selectedDeviceRequested(int device)
    signal connectionsRequested()
    color: AsterTheme.canvas
    border.color: AsterTheme.strokeStrong
    radius: AsterTheme.radiusMedium

    function titleForDevice(device) {
        return ["赤道仪", "主相机", "导星相机", "滤镜轮", "电调焦"][device]
    }

    MouseArea { anchors.fill: parent; onWheel: wheel => wheel.accepted = true }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            Layout.leftMargin: 16
            Layout.rightMargin: 8
            Text { text: "设备设置"; font.family: AsterTheme.uiFont; font.pixelSize: 14; color: AsterTheme.textPrimary; Layout.fillWidth: true }
            DesktopButton {
                objectName: "settingsConnectionsButton"
                glyph: "device"
                text: "连接管理"
                onClicked: root.connectionsRequested()
            }
            DesktopButton {
                objectName: "settingsCloseButton"
                icon.source: "../assets/icons/close.svg"
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                Accessible.name: "关闭设备设置"
                ToolTip.visible: hovered
                ToolTip.text: "关闭"
                onClicked: root.closeRequested()
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: AsterTheme.stroke }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Rectangle {
                Layout.preferredWidth: 144
                Layout.fillHeight: true
                color: AsterTheme.surface
                Column {
                    objectName: "deviceSettingsNavigation"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 8
                    spacing: 4
                    Repeater {
                        model: ["mount", "camera", "guide-camera", "filter", "focuser"]
                        delegate: DesktopButton {
                            required property int index
                            required property string modelData
                            objectName: "deviceSettingsTab" + index
                            width: parent.width
                            height: 36
                            text: root.titleForDevice(index)
                            icon.source: "../assets/icons/" + modelData + ".svg"
                            checked: root.selectedDevice === index
                            onClicked: root.selectedDeviceRequested(index)
                        }
                    }
                }
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: AsterTheme.stroke }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 18
                spacing: 8
                Text {
                    objectName: "deviceSettingsTitle"
                    text: root.titleForDevice(root.selectedDevice)
                    color: AsterTheme.deviceTitleText
                    font.family: AsterTheme.uiFont
                    font.pixelSize: 16
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: AsterTheme.stroke; Layout.topMargin: 4; Layout.bottomMargin: 4 }
                Loader {
                    objectName: "settingsPageLoader"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    sourceComponent: root.selectedDevice === 0 ? mountSettings
                                   : root.selectedDevice === 1 ? mainCameraSettings
                                   : root.selectedDevice === 2 ? guideCameraSettings
                                   : root.selectedDevice === 3 ? filterWheelSettings
                                   : focuserSettings
                }
            }
        }
    }
    MouseArea {
        id: resizeGrip
        objectName: "settingsResizeHandle"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 18; height: 18
        cursorShape: Qt.SizeFDiagCursor
        property point startPosition
        property size startSize
        onPressed: mouse => {
            startPosition = mapToItem(null, mouse.x, mouse.y)
            startSize = Qt.size(root.width, root.height)
        }
        onPositionChanged: mouse => {
            if (!pressed) return
            const p = mapToItem(null, mouse.x, mouse.y)
            root.preferredDialogWidth = Math.max(720, startSize.width + (p.x - startPosition.x) * 2)
            root.preferredDialogHeight = Math.max(460, startSize.height + (p.y - startPosition.y) * 2)
        }
        Text { anchors.centerIn: parent; text: "◢"; color: AsterTheme.textMuted; font.pixelSize: 10 }
    }
    Component { id: mountSettings; MountSettings { deviceManager: root.deviceManager; planner: root.planner } }
    Component { id: mainCameraSettings; MainCameraSettings { deviceManager: root.deviceManager } }
    Component { id: guideCameraSettings; GuideCameraSettings { } }
    Component { id: filterWheelSettings; FilterWheelSettings { deviceManager: root.deviceManager } }
    Component { id: focuserSettings; FocuserSettings { deviceManager: root.deviceManager; autoFocusController: root.autoFocusController } }
}
