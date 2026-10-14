import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"
import "../theme"

ColumnLayout {
    id: root
    required property var deviceManager
    property bool connected: false
    property var metrics: ({})
    property bool held: false
    property int activeDirection: -1
    signal noticeRequested(string message)
    readonly property bool available: connected && !!metrics.canManualMove && !metrics.atPark
        && !(deviceManager && deviceManager.mountGuiding) && (!metrics.slewing || held)
    spacing: 8

    function stopHeld() {
        if (!held) return
        held = false
        activeDirection = -1
        if (!deviceManager.stopMountManualMove() && deviceManager.lastError)
            noticeRequested(deviceManager.lastError)
    }
    onVisibleChanged: if (!visible) stopHeld()
    onAvailableChanged: if (!available) stopHeld()
    Component.onDestruction: stopHeld()
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state !== Qt.ApplicationActive) root.stopHeld() }
    }
    Timer {
        interval: 200
        repeat: true
        running: root.held && root.visible && root.available
        onTriggered: root.deviceManager.keepMountManualMoveAlive()
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: "跳转速率"; color: AsterTheme.textSecondary; font.pixelSize: 11 }
        Rectangle {
            implicitWidth: 62; implicitHeight: 28
            radius: 14
            color: AsterTheme.canvas
            border.color: AsterTheme.stroke
            Text {
                anchors.centerIn: parent
                text: rate.rates[rate.currentIndex] + "×"
                color: AsterTheme.textPrimary
                font.pixelSize: 12
            }
        }
        Item { Layout.fillWidth: true }
        DesktopButton {
            text: root.metrics.tracking ? "跟踪开" : "跟踪关"
            checked: !!root.metrics.tracking
            enabled: root.connected && !root.metrics.atPark && !root.held
            onClicked: {
                if (!root.deviceManager.setMountTracking(!root.metrics.tracking))
                    root.noticeRequested(root.deviceManager.lastError)
            }
        }
    }
    Item {
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: 244
        Layout.preferredHeight: 176
        Item {
            id: rate
            objectName: "mountManualRate"
            width: 60; height: 176
            readonly property var rates: [0.25, 0.5, 1, 2, 4, 8, 20, 60, 720, 1440]
            property int currentIndex: 8
            enabled: root.available && !root.held && !root.deviceManager.mountManualMoving
            opacity: enabled ? 1 : 0.4
            activeFocusOnTab: true
            Accessible.role: Accessible.Slider
            Accessible.name: "跳转速率 " + rates[currentIndex] + "倍恒星速"
            Accessible.description: "上快下慢，方向键切换档位；仅用于手动方向移动"
            Keys.onUpPressed: currentIndex = Math.min(9, currentIndex + 1)
            Keys.onDownPressed: currentIndex = Math.max(0, currentIndex - 1)
            function point(index) {
                const angle = (140 + index * 80 / 9) * Math.PI / 180
                return Qt.point(112 + 106 * Math.cos(angle), 88 + 106 * Math.sin(angle))
            }
            function selectAt(x, y) {
                let angle = Math.atan2(y - 88, x - 112) * 180 / Math.PI
                if (angle < 0) angle += 360
                currentIndex = Math.max(0, Math.min(9, Math.round((angle - 140) * 9 / 80)))
            }
            Canvas {
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.beginPath()
                    ctx.arc(112, 88, 106, 140 * Math.PI / 180, 220 * Math.PI / 180)
                    ctx.strokeStyle = AsterTheme.stroke
                    ctx.lineWidth = 3
                    ctx.stroke()
                }
            }
            Repeater {
                model: 10
                Rectangle {
                    required property int index
                    width: 6; height: 6; radius: 3
                    x: rate.point(index).x - width / 2
                    y: rate.point(index).y - height / 2
                    color: index <= rate.currentIndex ? AsterTheme.accent : AsterTheme.stroke
                }
            }
            Rectangle {
                width: 18; height: 18; radius: 9
                x: rate.point(rate.currentIndex).x - width / 2
                y: rate.point(rate.currentIndex).y - height / 2
                color: AsterTheme.textPrimary
                border.color: AsterTheme.accent
                border.width: rate.activeFocus ? 3 : 2
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                preventStealing: true
                onPressed: function(mouse) {
                    if (Math.abs(Math.hypot(mouse.x - 112, mouse.y - 88) - 106) > 18) {
                        mouse.accepted = false
                        return
                    }
                    rate.forceActiveFocus()
                    rate.selectAt(mouse.x, mouse.y)
                }
                onPositionChanged: function(mouse) { if (pressed) rate.selectAt(mouse.x, mouse.y) }
            }
        }
        Item {
            x: 60; width: 176; height: 176
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: AsterTheme.canvas
            border.color: AsterTheme.stroke
        }
        Repeater {
            model: [
                {name: "North", label: "北", arrow: "▲", axis: 1, sign: 1, x: 64, y: 6},
                {name: "West", label: "西", arrow: "◀", axis: 0, sign: 1, x: 6, y: 64},
                {name: "East", label: "东", arrow: "▶", axis: 0, sign: -1, x: 122, y: 64},
                {name: "South", label: "南", arrow: "▼", axis: 1, sign: -1, x: 64, y: 122}
            ]
            delegate: AsterButton {
                required property var modelData
                required property int index
                objectName: "mountMove" + modelData.name
                x: modelData.x; y: modelData.y; width: 48; height: 48
                text: modelData.arrow
                normalTextColor: AsterTheme.accent
                enabled: root.available && (!root.held || root.activeDirection === index)
                         && (!root.deviceManager.mountManualMoving || root.held)
                Accessible.name: "向" + modelData.label + "移动，按住移动松开停止"
                onPressed: {
                    if (root.held) return
                    root.activeDirection = index
                    root.held = true
                    if (!root.deviceManager.startMountManualMove(modelData.axis, rate.rates[rate.currentIndex] * modelData.sign)) {
                        root.held = false
                        root.activeDirection = -1
                        root.noticeRequested(root.deviceManager.lastError)
                    }
                }
                onReleased: root.stopHeld()
                onCanceled: root.stopHeld()
                onDownChanged: if (!down && root.activeDirection === index) root.stopHeld()
            }
        }
        AsterButton {
            objectName: "mountManualStop"
            x: 64; y: 64; width: 48; height: 48
            text: "停止"
            danger: true
            enabled: root.connected
            onClicked: {
                root.held = false
                root.activeDirection = -1
                if (!root.deviceManager.mountAbortSlew()) root.noticeRequested(root.deviceManager.lastError)
            }
        }
        }
    }
    Text {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        text: !root.metrics.canManualMove ? "当前驱动未开放已验证的手动控制"
            : root.deviceManager.mountGuiding ? "导星中 · 手动移动已锁定" : ""
        color: AsterTheme.textMuted
        font.pixelSize: 10
        wrapMode: Text.Wrap
    }
}
