import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Item {
    id: root
    required property var rowData
    property bool compact: false
    // Live camera readouts come from here (a QVariantMap refreshed by polling)
    // so telemetry rows can update without the row model being rebuilt.
    property var telemetrySource: null
    signal edited(string key, string value)   // 数字等取值行的编辑提交（key 形式，供 SettingsPage 转发）
    signal triggered(string key)              // 按钮行的点击（key 形式）
    visible: rowData.visible !== false
    enabled: rowData.enabled !== false
    onRowDataChanged: if (editor.item && editor.item.syncFromValue && !editor.item.activeFocus) editor.item.syncFromValue()
    Layout.fillWidth: true
    implicitHeight: compact ? 32 : 42

    RowLayout {
        anchors.fill: parent
        spacing: AsterTheme.space3

        Text {
            Layout.fillWidth: true
            text: root.rowData.label || ""
            color: root.enabled ? AsterTheme.deviceBodyText : AsterTheme.deviceDisabledText
            font.family: AsterTheme.uiFont
            font.pixelSize: root.compact ? 12 : 13
            elide: Text.ElideMiddle
        }

        Loader {
            id: editor
            objectName: "settingRowEditor"
            Layout.preferredWidth: root.rowData.editorWidth || (root.rowData.wide ? 250 : 190)
            Layout.preferredHeight: root.compact ? 28 : -1
            sourceComponent: root.rowData.component || (root.rowData.type === "toggle" ? toggleEditor
                           : root.rowData.type === "segment" ? segmentEditor
                           : root.rowData.type === "button" ? buttonEditor
                           : root.rowData.type === "select" ? selectEditor
                           : root.rowData.type === "field" ? fieldEditor
                           : root.rowData.type === "number" ? numberEditor
                           : root.rowData.type === "telemetry" ? telemetryEditor
                           : valueEditor)
        }
    }

    Component {
        id: valueEditor
        Rectangle {
            implicitHeight: root.compact ? 28 : 34
            radius: AsterTheme.radiusSmall
            color: root.compact ? "transparent" : AsterTheme.canvasRaised
            border.width: root.compact ? 0 : 1
            border.color: AsterTheme.stroke
            Text {
                anchors.fill: parent
                anchors.margins: root.compact ? 4 : 8
                text: root.rowData.value || "—"
                color: root.enabled ? AsterTheme.deviceMutedText : AsterTheme.deviceDisabledText
                font.family: AsterTheme.monoFont
                font.pixelSize: 12
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    Component {
        id: fieldEditor
        TextField {
            id: field
            implicitHeight: root.compact ? 28 : 34
            // Imperative sync: unrelated metric refreshes must not wipe text
            // the user is still typing (see also DesktopField).
            function syncFromValue() {
                field.text = root.rowData.value !== undefined && root.rowData.value !== null ? String(root.rowData.value) : ""
            }
            Component.onCompleted: syncFromValue()
            onActiveFocusChanged: if (!activeFocus) syncFromValue()
            color: root.enabled ? AsterTheme.deviceBodyText : AsterTheme.deviceDisabledText
            font.family: AsterTheme.monoFont
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            selectByMouse: true
            validator: DoubleValidator {
                bottom: root.rowData.minimum !== undefined ? root.rowData.minimum : -1000000000
                top: root.rowData.maximum !== undefined ? root.rowData.maximum : 1000000000
                decimals: root.rowData.decimals !== undefined ? root.rowData.decimals : 3
                locale: "C"
            }
            onEditingFinished: {
                if (acceptableInput) {
                    if (root.rowData.onEdited) root.rowData.onEdited(text)
                    if (root.rowData.key) root.edited(root.rowData.key, text)
                } else {
                    syncFromValue()
                }
            }
            background: Rectangle {
                radius: AsterTheme.radiusSmall
                color: AsterTheme.canvasRaised
                border.width: 1
                border.color: parent.activeFocus ? AsterTheme.accent : AsterTheme.stroke
            }
        }
    }

    Component {
        id: numberEditor
        RowLayout {
            spacing: 4
            TextField {
                id: numberField
                Layout.fillWidth: true
                implicitHeight: root.compact ? 28 : 34
                text: root.rowData.value !== undefined ? String(root.rowData.value) : ""
                color: root.enabled ? AsterTheme.deviceBodyText : AsterTheme.deviceDisabledText
                font.family: AsterTheme.monoFont
                font.pixelSize: 12
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                selectByMouse: true
                padding: 6
                rightPadding: 6
                validator: DoubleValidator {
                    bottom: root.rowData.min !== undefined ? root.rowData.min : 0
                    top: root.rowData.max !== undefined ? root.rowData.max : 100000
                    decimals: root.rowData.decimals !== undefined ? root.rowData.decimals : 1
                    locale: "C"
                    notation: DoubleValidator.StandardNotation
                }
                background: Rectangle {
                    radius: AsterTheme.radiusSmall
                    color: AsterTheme.canvasRaised
                    border.width: 1
                    border.color: numberField.activeFocus ? AsterTheme.accent : AsterTheme.stroke
                    Behavior on border.color { ColorAnimation { duration: 120 } }
                }
                onEditingFinished: {
                    if (acceptableInput && root.rowData.key)
                        root.edited(root.rowData.key, text)
                    else
                        text = root.rowData.value !== undefined ? String(root.rowData.value) : ""
                }
            }
            Text {
                visible: !!root.rowData.unit
                text: root.rowData.unit || ""
                color: AsterTheme.deviceMutedText
                font.family: AsterTheme.uiFont
                font.pixelSize: 11
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    Component {
        id: telemetryEditor
        Text {
            text: {
                const value = root.telemetrySource && root.telemetrySource[root.rowData.key] !== undefined
                              ? root.telemetrySource[root.rowData.key] : undefined
                if (value === undefined) return "—"
                return root.rowData.format ? root.rowData.format(value) : String(value)
            }
            color: root.enabled ? AsterTheme.deviceMutedText : AsterTheme.deviceDisabledText
            font.family: AsterTheme.monoFont
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    Component {
        id: toggleEditor
        Item {
            implicitHeight: 34
            AsterSwitch {
                id: toggleSwitch
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                checked: !!root.rowData.checked
                // Qt 6 Switch.toggled() is PARAMETERLESS — any function
                // parameter would be undefined. Read the switch's own
                // `checked` property (already at the new value when the
                // signal fires) instead.
                onToggled: {
                    if (root.rowData.onToggled) root.rowData.onToggled(toggleSwitch.checked)
                }
            }
        }
    }

    Component {
        id: segmentEditor
        AsterSegmentedControl {
            model: root.rowData.options || []
            currentIndex: root.rowData.currentIndex || 0
            normalTextColor: AsterTheme.deviceMutedText
            selectedTextColor: AsterTheme.deviceSelectedText
            disabledTextColor: AsterTheme.deviceDisabledText
            onActivated: index => {
                if (root.rowData.onActivated) root.rowData.onActivated(index)
            }
        }
    }

    Component {
        id: buttonEditor
        AsterButton {
            width: parent ? parent.width : implicitWidth
            text: root.rowData.value || "执行"
            primary: !!root.rowData.primary
            normalTextColor: AsterTheme.deviceBodyText
            disabledTextColor: AsterTheme.deviceDisabledText
            onClicked: {
                if (root.rowData.onClicked) root.rowData.onClicked()
                if (root.rowData.key) root.triggered(root.rowData.key)
            }
        }
    }

    Component {
        id: selectEditor
        DesktopComboBox {
            id: combo
            implicitHeight: 34
            model: root.rowData.options || []
            currentIndex: root.rowData.currentIndex || 0
            onActivated: function(index) {
                if (root.rowData.onActivated) root.rowData.onActivated(index)
            }
            contentItem: Text {
                leftPadding: 10
                text: combo.displayText
                color: combo.enabled ? AsterTheme.deviceBodyText : AsterTheme.deviceDisabledText
                font.family: AsterTheme.uiFont
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                radius: AsterTheme.radiusSmall
                color: AsterTheme.canvasRaised
                border.width: 1
                border.color: combo.activeFocus ? AsterTheme.accent : AsterTheme.stroke
            }
        }
    }
}
