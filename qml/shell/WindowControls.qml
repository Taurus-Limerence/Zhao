import QtQuick

Row {
    id: root
    objectName: "windowControls"
    property bool maximized: false
    signal minimizeRequested()
    signal toggleMaximizeRequested()
    signal closeRequested()

    width: 138
    height: 44
    spacing: 0

    WindowCaptionButton {
        objectName: "minimizeButton"
        width: 46
        height: 44
        kind: "minimize"
        accessibleName: qsTr("最小化")
        onClicked: root.minimizeRequested()
    }

    WindowCaptionButton {
        objectName: "maximizeButton"
        width: 46
        height: 44
        kind: root.maximized ? "restore" : "maximize"
        accessibleName: root.maximized ? qsTr("还原") : qsTr("最大化")
        onClicked: root.toggleMaximizeRequested()
    }

    WindowCaptionButton {
        objectName: "closeButton"
        width: 46
        height: 44
        kind: "close"
        accessibleName: qsTr("关闭")
        dangerous: true
        onClicked: root.closeRequested()
    }
}
