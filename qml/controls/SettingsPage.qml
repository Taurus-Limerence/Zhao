import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

ScrollView {
    id: root
    property var sections: []
    // Optional content appended below the last section (e.g. the filter-wheel channel table).
    property Component footer: null
    // Live camera telemetry map; rows of type "telemetry" read their value here.
    property var telemetrySource: null
    readonly property var parameterSections: sections.map(section => ({
        title: section.title === "设备连接" ? "设备信息" : section.title,
        visible: section.visible,
        rows: section.title === "设备连接"
              ? section.rows.filter(row => row.label !== "设备" && row.label !== "连接")
              : section.rows
    })).filter(section => section.rows.length > 0 && section.visible !== false)
    clip: true
    contentWidth: availableWidth
    ScrollBar.vertical.policy: ScrollBar.AsNeeded
    ScrollBar.vertical.active: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    ColumnLayout {
        width: root.availableWidth - 12
        spacing: AsterTheme.space3
        Repeater {
            // Keep editors alive while telemetry replaces the section data.
            model: root.parameterSections.length
            delegate: ColumnLayout {
                id: sectionDelegate
                required property int index
                readonly property var modelData: root.parameterSections[index] || { rows: [] }
                Layout.fillWidth: true
                spacing: AsterTheme.space2

                Text {
                    visible: modelData.title && modelData.title.length > 0
                    text: modelData.title || ""
                    color: AsterTheme.deviceTitleText
                    font.family: AsterTheme.uiFont
                    font.pixelSize: 12
                    font.bold: true
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Repeater {
                        model: sectionDelegate.modelData.rows.length
                        delegate: AsterSettingRow {
                            required property int index
                            rowData: sectionDelegate.modelData.rows[index] || {}
                            telemetrySource: root.telemetrySource
                            compact: true
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: AsterTheme.stroke }
            }
        }
        Loader {
            active: root.footer !== null
            sourceComponent: root.footer
            Layout.fillWidth: true
        }
    }
}
