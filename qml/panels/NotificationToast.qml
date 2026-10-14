import QtQuick
import QtQuick.Layouts
import "../controls"
import "../theme"

// 单张通知卡片：由 DesktopNotificationWindow 堆叠承载。severity 对应
// AppState.NotificationSeverity（0 信息 / 1 警告 / 2 错误）；每张卡片独立
// 计时自动消失，关闭信号交由宿主把它从堆叠中移除。
Rectangle {
    id: root
    required property string title
    required property string message
    required property int severity
    property int dismissInterval: 5000
    signal closed()

    width: 340
    implicitHeight: 76
    radius: AsterTheme.radiusMedium
    color: AsterTheme.surfaceRaised
    border.width: 1
    border.color: root.severity === 2 ? AsterTheme.danger
                : root.severity === 1 ? AsterTheme.warning
                : AsterTheme.strokeStrong

    RowLayout {
        anchors.fill: parent
        anchors.margins: AsterTheme.space3
        spacing: AsterTheme.space3
        Rectangle {
            width: 30; height: 30; radius: 15; color: "transparent"; border.width: 1; border.color: root.border.color
            Text { anchors.centerIn: parent; text: root.severity === 2 ? "!" : "i"; color: root.border.color; font.bold: true }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Text { text: root.title; color: AsterTheme.textPrimary; font.pixelSize: 13; font.bold: true }
            Text {
                Layout.fillWidth: true
                text: root.message; color: AsterTheme.textSecondary; font.pixelSize: 11
                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
            }
        }
        AsterIconButton { width: 32; height: 32; iconSource: "../assets/icons/close.svg"; onClicked: root.closed() }
    }

    Timer {
        interval: root.dismissInterval
        running: true
        onTriggered: root.closed()
    }
}
