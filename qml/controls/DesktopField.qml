import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

RowLayout {
    id: root
    property string label: ""
    property string value: ""
    property string unit: ""
    property bool editable: false
    property real minimum: 0
    property real maximum: 100000
    signal valueEdited(string value)
    // Follow backend value updates unless the user is editing this field.
    onValueChanged: if (editor.activeFocus === false) editor.text = value
    implicitHeight: 36
    Layout.minimumHeight: 36
    spacing: 8

    Text {
        Layout.fillWidth: true
        text: root.label
        color: AsterTheme.textSecondary
        font.family: AsterTheme.uiFont
        font.pixelSize: 11
    }
    TextField {
        id: editor
        objectName: "fieldEditor"
        Layout.preferredWidth: 100
        Layout.preferredHeight: 25
        visible: root.editable
        // The text is synced imperatively instead of being bound to root.value:
        // live metric refreshes must not wipe text the user is still typing.
        Component.onCompleted: editor.text = root.value
        onActiveFocusChanged: if (!activeFocus) editor.text = root.value
        color: AsterTheme.textPrimary
        font.family: AsterTheme.monoFont
        font.pixelSize: 11
        horizontalAlignment: Text.AlignRight
        selectByMouse: true
        validator: DoubleValidator { bottom: root.minimum; top: root.maximum; decimals: 3; locale: "C" }
        onEditingFinished: {
            if (acceptableInput) root.valueEdited(text)
            else editor.text = root.value
        }
        background: Rectangle {
            color: AsterTheme.canvas
            radius: 3
            border.color: parent.activeFocus ? AsterTheme.accent : AsterTheme.strokeStrong
        }
    }
    Text {
        visible: !root.editable
        text: root.value
        color: AsterTheme.textPrimary
        font.family: AsterTheme.monoFont
        font.pixelSize: 11
    }
    Text {
        Layout.preferredWidth: 18
        text: root.unit
        color: AsterTheme.textMuted
        font.pixelSize: 10
    }
}
