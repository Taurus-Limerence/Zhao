import QtQuick
import QtQuick.Layouts
import "../controls"
import "../theme"

Rectangle {
    id: root
    readonly property int toolCount: 5
    signal quickPanelRequested()
    signal shutterRequested()
    implicitWidth: 60
    implicitHeight: 218
    radius: AsterTheme.radiusMedium
    color: "#080d11"
    border.width: 1
    border.color: AsterTheme.stroke

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 0

        AsterIconButton { Layout.fillWidth: true; Layout.preferredHeight: 39; iconSource: "../assets/icons/aperture.svg"; label: "增益" }
        AsterIconButton { Layout.fillWidth: true; Layout.preferredHeight: 39; iconSource: "../assets/icons/crosshair.svg"; label: "曝光" }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            Rectangle {
                anchors.centerIn: parent
                width: 48
                height: 48
                radius: 24
                color: shutterMouse.pressed ? AsterTheme.accentSurface : AsterTheme.surface
                border.width: 2
                border.color: AsterTheme.textSecondary
                Rectangle { anchors.centerIn: parent; width: 36; height: 36; radius: 18; color: "transparent"; border.width: 1; border.color: AsterTheme.strokeStrong }
                Image { anchors.centerIn: parent; source: "../assets/icons/aperture.svg"; width: 20; height: 20 }
                MouseArea { id: shutterMouse; anchors.fill: parent; onClicked: root.shutterRequested() }
            }
        }

        AsterIconButton { Layout.fillWidth: true; Layout.preferredHeight: 39; iconSource: "../assets/icons/map.svg"; label: "解析" }
        AsterIconButton { Layout.fillWidth: true; Layout.preferredHeight: 39; iconSource: "../assets/icons/image.svg"; label: "图像" }
    }

    AsterButton {
        width: 24
        height: 44
        anchors.right: parent.left
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        text: "‹"
        onClicked: root.quickPanelRequested()
    }
}
