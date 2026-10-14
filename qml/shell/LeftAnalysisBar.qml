import QtQuick
import QtQuick.Layouts
import "../controls"
import "../theme"

Rectangle {
    id: root
    readonly property int toolCount: 4
    property int currentIndex: 2
    signal toolSelected(int index)
    implicitWidth: 68
    implicitHeight: 174
    radius: AsterTheme.radiusMedium
    color: "#080d11"
    border.width: 1
    border.color: AsterTheme.stroke

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 0
        Repeater {
            model: [
                { label: "导星曲线", icon: "../assets/icons/chart.svg" },
                { label: "直方图", icon: "../assets/icons/histogram.svg" },
                { label: "十字线", icon: "../assets/icons/crosshair.svg" },
                { label: "星点检测", icon: "../assets/icons/focus.svg" }
            ]
            delegate: AsterIconButton {
                required property int index
                required property var modelData
                Layout.fillWidth: true
                Layout.fillHeight: true
                iconSource: modelData.icon
                label: modelData.label
                selected: root.currentIndex === index
                onClicked: {
                    root.currentIndex = index
                    root.toolSelected(index)
                }
            }
        }
    }
}
