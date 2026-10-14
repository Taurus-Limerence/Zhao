import QtQuick
import QtQuick.Layouts
import "../theme"

Rectangle {
    id: root
    property string deviceSummary: "设备 0/5"
    color: AsterTheme.surface
    implicitHeight: 24
    Rectangle { width: parent.width; height: 1; color: AsterTheme.stroke }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 20
        Text { text: root.deviceSummary; color: AsterTheme.textSecondary; font.pixelSize: 10 }
        Rectangle { width: 1; height: 12; color: AsterTheme.strokeStrong }
        Text { text: "主相机  — °C     导星 RMS  —″     赤道仪  —"; color: AsterTheme.textMuted; font.pixelSize: 10 }
        Item { Layout.fillWidth: true }
        Text { text: "F11 专注预览"; color: AsterTheme.textMuted; font.pixelSize: 10 }
        Text { text: "设备遥测未接入"; color: AsterTheme.textSecondary; font.pixelSize: 10 }
    }
}
