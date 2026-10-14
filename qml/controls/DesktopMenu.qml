import QtQuick
import QtQuick.Controls
import "../theme"

Menu {
    implicitWidth: 200
    popupType: Popup.Item
    padding: 1
    font.family: AsterTheme.uiFont
    font.pixelSize: 11
    palette.window: AsterTheme.canvasRaised
    palette.base: AsterTheme.canvasRaised
    palette.text: AsterTheme.textPrimary
    palette.windowText: AsterTheme.textPrimary
    palette.buttonText: AsterTheme.textPrimary
    palette.highlight: AsterTheme.accentSurface
    palette.highlightedText: AsterTheme.textPrimary
    background: Rectangle { color: AsterTheme.canvasRaised; border.color: AsterTheme.strokeStrong; radius: 3 }
}
