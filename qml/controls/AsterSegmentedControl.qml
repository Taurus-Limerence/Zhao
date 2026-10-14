import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Rectangle {
    id: root
    property var model: []
    property int currentIndex: 0
    property color normalTextColor: AsterTheme.textMuted
    property color selectedTextColor: AsterTheme.textSelected
    property color disabledTextColor: AsterTheme.textDisabled
    signal activated(int index)
    implicitWidth: 210
    implicitHeight: 34
    radius: AsterTheme.radiusSmall
    color: AsterTheme.canvasRaised
    border.width: 1
    border.color: AsterTheme.stroke

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Repeater {
            model: root.model
            delegate: Button {
                required property int index
                required property var modelData
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 0
                onClicked: {
                    root.currentIndex = index
                    root.activated(index)
                }
                contentItem: Text {
                    text: modelData
                    color: !enabled ? root.disabledTextColor
                         : index === root.currentIndex ? root.selectedTextColor
                         : root.normalTextColor
                    font.family: AsterTheme.uiFont
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: index === root.currentIndex ? AsterTheme.accentSurface : "transparent"
                    border.width: index === root.currentIndex ? 1 : 0
                    border.color: AsterTheme.accent
                    radius: AsterTheme.radiusSmall
                }
            }
        }
    }
}
