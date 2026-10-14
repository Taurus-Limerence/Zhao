import QtQuick
import QtQuick.Controls
import "../theme"

ComboBox {
    id: control
    implicitHeight: 28
    leftPadding: 8
    rightPadding: 26
    font.family: AsterTheme.uiFont
    font.pixelSize: 11
    palette.text: AsterTheme.textPrimary
    palette.buttonText: AsterTheme.textPrimary
    palette.base: AsterTheme.canvasRaised
    palette.window: AsterTheme.canvasRaised
    palette.highlight: AsterTheme.accentSurface
    palette.highlightedText: AsterTheme.textPrimary
    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? AsterTheme.textPrimary : AsterTheme.textDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Image {
        source: "../assets/icons/chevron-down.svg"
        width: 12; height: 12
        anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
        opacity: control.enabled ? 1 : 0.4
    }
    background: Rectangle {
        color: AsterTheme.canvas; radius: 3
        border.color: control.activeFocus ? AsterTheme.accent : AsterTheme.strokeStrong
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        width: control.width; height: 28
        highlighted: control.highlightedIndex===index
        contentItem: Text {
            text: control.textAt(option.index); font: control.font
            color: AsterTheme.textPrimary; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
        background: Rectangle { color: option.highlighted ? AsterTheme.accentSurface : option.hovered ? AsterTheme.surfaceHover : AsterTheme.canvasRaised }
    }
    popup: Popup {
        popupType: Popup.Item
        y: control.height+2; width: control.width; padding: 1
        implicitHeight: Math.min(contentItem.implicitHeight+2,280)
        contentItem: ListView {
            clip: true; implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle { color: AsterTheme.canvasRaised; border.color: AsterTheme.strokeStrong; radius: 3 }
    }
}
