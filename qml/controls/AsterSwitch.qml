import QtQuick
import QtQuick.Controls
import "../theme"

Switch {
    id: control
    implicitWidth: 42
    implicitHeight: 24
    padding: 0

    indicator: Rectangle {
        implicitWidth: 42
        implicitHeight: 24
        radius: 12
        color: control.checked ? AsterTheme.accent : "#303941"
        border.width: control.activeFocus ? 1 : 0
        border.color: "#d7efff"

        Rectangle {
            width: 18
            height: 18
            radius: 9
            y: 3
            x: control.checked ? parent.width - width - 3 : 3
            color: control.enabled ? "#eef3f6" : "#88939a"
            Behavior on x { NumberAnimation { duration: 120 } }
        }
    }

    contentItem: Item { }
}
