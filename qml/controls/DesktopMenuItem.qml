import QtQuick
import QtQuick.Controls
import "../theme"

MenuItem {
    id: control
    implicitHeight: 29
    font.family: AsterTheme.uiFont
    font.pixelSize: 11
    contentItem: Text {
        text: (control.checked ? "✓  " : "    ")+control.text
        font: control.font; color: control.enabled ? AsterTheme.textPrimary : AsterTheme.textDisabled
        verticalAlignment: Text.AlignVCenter
    }
    indicator: Item {}
    background: Rectangle { color: control.highlighted || control.hovered ? AsterTheme.accentSurface : AsterTheme.canvasRaised }
}
