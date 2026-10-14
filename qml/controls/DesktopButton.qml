import QtQuick
import QtQuick.Controls
import "../theme"

Button {
    id: control
    property string glyph: ""
    property bool primary: false
    implicitWidth: Math.max(30, Math.ceil(implicitContentWidth) + leftPadding + rightPadding)
    implicitHeight: 27
    leftPadding: 8
    rightPadding: 8
    topPadding: 4
    bottomPadding: 4
    spacing: 5
    font.family: AsterTheme.uiFont
    font.pixelSize: 11
    icon.source: glyph ? "../assets/icons/" + glyph + ".svg" : ""
    icon.width: 14
    icon.height: 14
    icon.color: !enabled ? AsterTheme.textDisabled : primary ? AsterTheme.accentText : checked ? AsterTheme.accent : AsterTheme.textSecondary
    palette.buttonText: !enabled ? AsterTheme.textDisabled : primary ? AsterTheme.accentText : checked ? AsterTheme.accent : AsterTheme.textSecondary
    background: Rectangle {
        radius: 3
        color: !control.enabled ? (control.primary ? AsterTheme.canvas : "transparent")
             : control.down ? (control.primary ? AsterTheme.accentPressed : AsterTheme.surfaceHover)
             : control.primary ? AsterTheme.accent
             : control.checked ? AsterTheme.accentSurface
             : control.hovered ? AsterTheme.surfaceHover : "transparent"
        border.width: control.checked || control.primary || control.activeFocus ? 1 : 0
        border.color: AsterTheme.accent
    }
}
