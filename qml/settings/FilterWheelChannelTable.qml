import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

ColumnLayout {
    id: root
    property var deviceManager: null

    readonly property var wheel: {
        const devices = deviceManager ? deviceManager.devices : []
        for (let i = 0; i < devices.length; ++i) if (devices[i].role === 3) return devices[i]
        return {}
    }
    readonly property var metrics: wheel.metrics || {}
    readonly property int slotCount: metrics.slots > 0 ? metrics.slots : 7
    readonly property int currentPosition: metrics.position >= 0 ? metrics.position : -1
    readonly property var focusSettings: deviceManager && deviceManager.filterFocusSettings ? deviceManager.filterFocusSettings : ({enabled:false, reference:1, editable:false})
    readonly property bool offsetsVisible: !!focusSettings.enabled
    readonly property bool editable: !!deviceManager && focusSettings.editable !== false

    function channelAt(slot) {
        const channels = deviceManager && deviceManager.filterWheelChannels ? deviceManager.filterWheelChannels : []
        for (let i = 0; i < channels.length; ++i) {
            const channel = channels[i]
            if (channel.slot === slot) return channel
        }
        return {slot: slot, suffix: "", gain: 100, exposure: 3.0}
    }

    spacing: 0
    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 28
        spacing: AsterTheme.space3
        Text { Layout.preferredWidth: 48; text: "孔位"; color: AsterTheme.deviceMutedText; font.family: AsterTheme.uiFont; font.pixelSize: 11 }
        Text { Layout.preferredWidth: root.offsetsVisible ? 120 : 150; text: "自定义后缀"; color: AsterTheme.deviceMutedText; font.family: AsterTheme.uiFont; font.pixelSize: 11 }
        Text { Layout.preferredWidth: root.offsetsVisible ? 80 : 110; text: "增益"; color: AsterTheme.deviceMutedText; font.family: AsterTheme.uiFont; font.pixelSize: 11 }
        Text { Layout.fillWidth: true; Layout.maximumWidth: 160; text: "曝光时间（秒）"; color: AsterTheme.deviceMutedText; font.family: AsterTheme.uiFont; font.pixelSize: 11 }
        Text { visible: root.offsetsVisible; Layout.preferredWidth: 110; text: "焦点偏移（步）"; color: AsterTheme.deviceMutedText; font.family: AsterTheme.uiFont; font.pixelSize: 11 }
    }
    Repeater {
        model: root.slotCount
        delegate: RowLayout {
            id: row
            required property int index
            readonly property int slot: index + 1
            readonly property bool current: index === root.currentPosition
            readonly property var channel: root.channelAt(slot)
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            spacing: AsterTheme.space3

            Rectangle {
                Layout.preferredWidth: 48
                Layout.fillHeight: true
                radius: AsterTheme.radiusSmall
                color: row.current ? AsterTheme.accentSurface : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: row.slot
                    color: row.current ? AsterTheme.deviceSelectedText : AsterTheme.deviceBodyText
                    font.family: AsterTheme.monoFont
                    font.pixelSize: 12
                    font.bold: row.current
                }
            }
            TextField {
                id: suffixInput
                objectName: "channelSuffix" + row.slot
                Layout.preferredWidth: root.offsetsVisible ? 120 : 150
                enabled: root.editable
                text: row.channel.suffix
                placeholderText: "如 L / R / G / B"
                color: AsterTheme.deviceBodyText
                placeholderTextColor: AsterTheme.deviceDisabledText
                font.family: AsterTheme.uiFont
                font.pixelSize: 12
                selectByMouse: true
                onEditingFinished: {
                    if (root.saveChannel(row.slot, text, gainInput.text, exposureInput.text)) text = row.channel.suffix
                }
                background: Rectangle {
                    radius: AsterTheme.radiusSmall
                    color: AsterTheme.canvasRaised
                    border.width: 1
                    border.color: parent.activeFocus ? AsterTheme.accent : AsterTheme.stroke
                }
            }
            TextField {
                id: gainInput
                objectName: "channelGain" + row.slot
                Layout.preferredWidth: root.offsetsVisible ? 80 : 110
                enabled: root.editable
                text: row.channel.gain
                color: AsterTheme.deviceBodyText
                font.family: AsterTheme.monoFont
                font.pixelSize: 12
                horizontalAlignment: TextInput.AlignRight
                selectByMouse: true
                validator: IntValidator { bottom: 0; top: 10000 }
                onEditingFinished: {
                    if (acceptableInput) {
                        if (root.saveChannel(row.slot, suffixInput.text, text, exposureInput.text)) text = row.channel.gain
                    } else {
                        text = row.channel.gain
                    }
                }
                background: Rectangle {
                    radius: AsterTheme.radiusSmall
                    color: AsterTheme.canvasRaised
                    border.width: 1
                    border.color: !parent.acceptableInput ? AsterTheme.danger
                               : parent.activeFocus ? AsterTheme.accent : AsterTheme.stroke
                }
            }
            TextField {
                id: exposureInput
                objectName: "channelExposure" + row.slot
                Layout.fillWidth: true
                Layout.maximumWidth: 160
                enabled: root.editable
                text: row.channel.exposure
                color: AsterTheme.deviceBodyText
                font.family: AsterTheme.monoFont
                font.pixelSize: 12
                horizontalAlignment: TextInput.AlignRight
                selectByMouse: true
                validator: DoubleValidator { bottom: 0.001; top: 3600; decimals: 3; locale: "C" }
                onEditingFinished: {
                    if (acceptableInput) {
                        if (root.saveChannel(row.slot, suffixInput.text, gainInput.text, text)) text = row.channel.exposure
                    } else {
                        text = row.channel.exposure
                    }
                }
                background: Rectangle {
                    radius: AsterTheme.radiusSmall
                    color: AsterTheme.canvasRaised
                    border.width: 1
                    border.color: !parent.acceptableInput ? AsterTheme.danger
                               : parent.activeFocus ? AsterTheme.accent : AsterTheme.stroke
                }
            }
            TextField {
                id: offsetInput
                objectName: "channelFocusOffset" + row.slot
                Accessible.name: "滤镜 " + row.slot + " 焦点偏移步数"
                readonly property bool reference: row.slot === root.focusSettings.reference
                readonly property string storedText: reference ? "0"
                    : row.channel.focusOffset === undefined || row.channel.focusOffset === null ? "" : String(row.channel.focusOffset)
                visible: root.offsetsVisible
                enabled: root.editable && !reference
                Layout.preferredWidth: 110
                text: storedText
                onStoredTextChanged: if (!activeFocus) text = storedText
                placeholderText: "未设置"
                placeholderTextColor: AsterTheme.deviceDisabledText
                color: enabled ? AsterTheme.deviceBodyText : AsterTheme.deviceMutedText
                font.family: AsterTheme.monoFont
                font.pixelSize: 12
                horizontalAlignment: TextInput.AlignRight
                selectByMouse: true
                validator: IntValidator { bottom: -1000000; top: 1000000; locale: "C" }
                // Empty input is Intermediate, so the validator suppresses editingFinished on Return.
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && text.trim() === "") {
                        editingFinished()
                        event.accepted = true
                    }
                }
                onEditingFinished: {
                    const value = text.trim()
                    if (enabled && (value === "" || acceptableInput))
                        root.deviceManager.setFilterWheelFocusOffset(row.slot, value === "" ? null : Number(value))
                    text = storedText
                }
                background: Rectangle {
                    radius: AsterTheme.radiusSmall
                    color: AsterTheme.canvasRaised
                    border.width: 1
                    border.color: offsetInput.activeFocus ? AsterTheme.accent : AsterTheme.stroke
                }
            }
        }
    }
    Rectangle { Layout.fillWidth: true; height: 1; color: AsterTheme.stroke }

    // Returns true when the manager accepted the row; invalid field input never
    // reaches here — the caller reverts it to the last stored value instead.
    function saveChannel(slot, suffix, gain, exposure) {
        if (!deviceManager || typeof deviceManager.setFilterWheelChannel !== "function") return false
        const ok = deviceManager.setFilterWheelChannel(slot, suffix, Math.round(Number(gain)), Number(exposure))
        return ok
    }
}
