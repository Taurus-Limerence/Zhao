import QtQuick
import QtQuick.Controls
import "../theme"

Button {
    id: control
    property url iconSource
    property string label
    property bool showLabel: true
    property bool selected: false
    property bool backgroundVisible: true
    property int iconSize: 19
    property real iconRotation: 0
    property real backgroundRadius: AsterTheme.radiusSmall
    property color idleBackgroundColor: "transparent"
    property color normalTextColor: AsterTheme.textSecondary
    property color selectedTextColor: AsterTheme.textSelected
    property color disabledTextColor: AsterTheme.textDisabled
    implicitWidth: 44
    implicitHeight: showLabel && label.length > 0 ? 54 : 40
    padding: 0
    hoverEnabled: true

    contentItem: Item {
        implicitWidth: contentColumn.implicitWidth
        implicitHeight: contentColumn.implicitHeight

        Column {
            id: contentColumn
            spacing: 3
            anchors.centerIn: parent
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: control.iconSource
                width: control.iconSize
                height: control.iconSize
                rotation: control.iconRotation
                sourceSize: Qt.size(control.iconSize, control.iconSize)
                opacity: control.enabled ? 1 : 0.4
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: control.showLabel && control.label.length > 0
                text: control.label
                color: !control.enabled ? control.disabledTextColor
                     : control.selected ? control.selectedTextColor
                     : control.normalTextColor
                font.family: AsterTheme.uiFont
                font.pixelSize: 11
            }
        }
    }

    background: Rectangle {
        radius: control.backgroundRadius
        color: !control.backgroundVisible ? "transparent"
             : control.selected ? AsterTheme.accentSurface
             : control.hovered ? AsterTheme.surfaceHover
             : control.idleBackgroundColor
        border.width: control.backgroundVisible && control.activeFocus ? 1 : 0
        border.color: AsterTheme.accent
    }
}
