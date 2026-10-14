import QtQuick
import QtQuick.Controls
import "../theme"

Button {
    id: control
    property string kind: "minimize"
    property string accessibleName
    property bool dangerous: false

    implicitWidth: 46
    implicitHeight: 44
    padding: 0
    hoverEnabled: true

    Accessible.name: control.accessibleName
    Accessible.role: Accessible.Button
    ToolTip.visible: control.hovered
    ToolTip.text: control.accessibleName

    background: Rectangle {
        color: control.pressed
             ? (control.dangerous ? AsterTheme.dangerPressed : AsterTheme.surfaceRaised)
             : control.hovered
               ? (control.dangerous ? AsterTheme.dangerSurface : AsterTheme.surfaceHover)
               : "transparent"
        border.width: control.activeFocus ? 1 : 0
        border.color: AsterTheme.accent
    }

    contentItem: Item {
        Rectangle {
            visible: control.kind === "minimize"
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 3
            width: 12
            height: 1
            color: AsterTheme.textPrimary
        }

        Rectangle {
            visible: control.kind === "maximize"
            anchors.centerIn: parent
            width: 11
            height: 10
            color: "transparent"
            border.width: 1
            border.color: AsterTheme.textPrimary
        }

        Item {
            visible: control.kind === "restore"
            anchors.centerIn: parent
            width: 13
            height: 12

            Rectangle { x: 3; y: 0; width: 10; height: 1; color: AsterTheme.textPrimary }
            Rectangle { x: 12; y: 0; width: 1; height: 9; color: AsterTheme.textPrimary }
            Rectangle { x: 3; y: 0; width: 1; height: 3; color: AsterTheme.textPrimary }
            Rectangle { x: 0; y: 3; width: 10; height: 1; color: AsterTheme.textPrimary }
            Rectangle { x: 9; y: 3; width: 1; height: 9; color: AsterTheme.textPrimary }
            Rectangle { x: 0; y: 11; width: 10; height: 1; color: AsterTheme.textPrimary }
            Rectangle { x: 0; y: 3; width: 1; height: 9; color: AsterTheme.textPrimary }
        }

        Item {
            visible: control.kind === "close"
            anchors.centerIn: parent
            width: 13
            height: 13

            Rectangle {
                anchors.centerIn: parent
                width: 14
                height: 1
                rotation: 45
                color: AsterTheme.textPrimary
            }
            Rectangle {
                anchors.centerIn: parent
                width: 14
                height: 1
                rotation: -45
                color: AsterTheme.textPrimary
            }
        }
    }
}
