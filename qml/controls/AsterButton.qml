import QtQuick
import QtQuick.Controls
import "../theme"

Button {
    id: control
    property bool primary: false
    property bool danger: false
    property color normalTextColor: AsterTheme.textPrimary
    property color disabledTextColor: AsterTheme.textDisabled
    implicitHeight: 36
    implicitWidth: 96
    padding: 0
    hoverEnabled: true

    contentItem: Text {
        text: control.text
        color: !control.enabled ? control.disabledTextColor
              : control.primary ? AsterTheme.accentText
              : control.danger ? AsterTheme.danger
              : control.normalTextColor
        font.family: AsterTheme.uiFont
        font.pixelSize: 13
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: AsterTheme.radiusSmall
        color: !control.enabled ? AsterTheme.surface
             : control.down ? (control.primary ? AsterTheme.accentPressed : AsterTheme.surfaceHover)
             : control.hovered ? (control.primary ? AsterTheme.accentPressed : AsterTheme.surfaceHover)
             : control.primary ? AsterTheme.accent
             : AsterTheme.canvasRaised
        border.width: 1
        border.color: control.primary ? AsterTheme.accent
                    : control.danger ? AsterTheme.danger
                    : control.activeFocus ? AsterTheme.accent
                    : AsterTheme.stroke
    }
}
