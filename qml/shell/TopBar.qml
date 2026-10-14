import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"
import "../theme"

Rectangle {
    id: root
    property string workspaceTitle: "主控"
    property string targetName: "未选择目标"
    property var selectedTarget: null
    property string deviceSummary: "设备 0/5"
    property bool windowMaximized: false
    signal deviceConnectionsRequested()
    signal processingRequested()
    signal planRequested()
    signal moveRequested()
    signal minimizeRequested()
    signal toggleMaximizeRequested()
    signal closeRequested()
    // Minute-carry-safe sexagesimal rendering for the live target readout.
    function coordinateLine(isRa) {
        if (!selectedTarget) return ""
        const value = isRa ? selectedTarget.raHours : selectedTarget.decDeg
        if (value === undefined || value === null) return ""
        if (isRa) {
            const total = Math.round(value * 3600)
            const h = Math.floor(total / 3600) % 24
            const m = Math.floor(total / 60) % 60
            const sec = total % 60
            return "RA   " + h + "h " + String(m).padStart(2, "0") + "m " + String(sec).padStart(2, "0") + "s"
        }
        const sign = value >= 0 ? "+" : "-"
        const total = Math.round(Math.abs(value) * 3600)
        const d = Math.floor(total / 3600)
        const m = Math.floor(total / 60) % 60
        const sec = total % 60
        return "DEC  " + sign + d + "° " + String(m).padStart(2, "0") + "′ " + String(sec).padStart(2, "0") + "″"
    }

    color: AsterTheme.surface
    implicitHeight: 44
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: AsterTheme.stroke }

    RowLayout {
        objectName: "topBarContent"
        anchors.fill: parent
        anchors.leftMargin: 12
        spacing: 12
        Image {
            objectName: "brandMark"
            source: "../assets/icons/stellamora-mark.svg"
            sourceSize: Qt.size(24, 24)
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
        }
        Text {
            objectName: "brandText"
            text: "STELLAMORA"
            color: AsterTheme.textPrimary
            font.family: AsterTheme.uiFont
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.letterSpacing: 1.3
        }
        Rectangle { width: 1; Layout.fillHeight: true; color: AsterTheme.stroke }
        Text { text: "工作区"; color: AsterTheme.textMuted; font.pixelSize: 10 }
        Item {
            objectName: "workspaceBadge"
            implicitWidth: workspaceText.implicitWidth + 8
            Layout.fillHeight: true
            Text { id: workspaceText; anchors.centerIn: parent; text: root.workspaceTitle; color: AsterTheme.accent; font.family: AsterTheme.uiFont; font.pixelSize: 11 }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: AsterTheme.accent }
        }
        Rectangle { width: 1; Layout.fillHeight: true; color: AsterTheme.stroke }
        Column {
            spacing: 3
            Text { text: "目标天体"; color: AsterTheme.textMuted; font.pixelSize: 9 }
            Text { text: root.targetName; color: AsterTheme.textPrimary; font.pixelSize: 11; font.weight: Font.DemiBold }
        }
        Column {
            objectName: "topBarTargetCoordinates"
            visible: root.width >= 1380 && root.selectedTarget
            spacing: 3
            Text { text: root.coordinateLine(true); color: AsterTheme.textSecondary; font.family: AsterTheme.monoFont; font.pixelSize: 9 }
            Text { text: root.coordinateLine(false); color: AsterTheme.textSecondary; font.family: AsterTheme.monoFont; font.pixelSize: 9 }
        }
        Text { visible: root.width >= 1300; text: "天气安全  ·  未接入"; color: AsterTheme.textMuted; font.pixelSize: 10 }
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 40
            Layout.fillHeight: true
            DragHandler { target: null; acceptedButtons: Qt.LeftButton; onActiveChanged: { if (active) root.moveRequested() } }
            TapHandler { acceptedButtons: Qt.LeftButton; onDoubleTapped: root.toggleMaximizeRequested() }
        }
        DesktopButton {
            objectName: "deviceConnectionsButton"
            glyph: "device"
            text: root.deviceSummary.replace(/^设备\s*/, "")
            Layout.preferredHeight: 36
            icon.width: 19
            icon.height: 19
            Accessible.name: "设备连接管理"
            ToolTip.visible: hovered
            ToolTip.text: "设备连接管理 · " + root.deviceSummary
            onClicked: root.deviceConnectionsRequested()
        }
        DesktopButton { objectName: "planButton"; glyph: "list"; text: "序列"; onClicked: root.planRequested() }
        DesktopButton { glyph: "bell"; ToolTip.visible: hovered; ToolTip.text: "通知 / 处理工具"; onClicked: root.processingRequested() }
        Text {
            text: Qt.formatTime(new Date(), "hh:mm:ss")
            color: AsterTheme.textSecondary
            font.family: AsterTheme.monoFont
            font.pixelSize: 10
            Timer { interval: 1000; running: true; repeat: true; onTriggered: parent.text = Qt.formatTime(new Date(), "hh:mm:ss") }
        }
        Rectangle { width: 1; height: 20; color: AsterTheme.stroke }
        WindowControls {
            Layout.preferredWidth: 138
            Layout.preferredHeight: 44
            maximized: root.windowMaximized
            onMinimizeRequested: root.minimizeRequested()
            onToggleMaximizeRequested: root.toggleMaximizeRequested()
            onCloseRequested: root.closeRequested()
        }
    }
}
