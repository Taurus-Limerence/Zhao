import QtQuick
import QtQuick.Layouts
import "../controls"
import "../theme"

SettingsPage {
    id: root
    property var deviceManager: null

    readonly property var wheel: {
        const devices = deviceManager ? deviceManager.devices : []
        for (let i = 0; i < devices.length; ++i) if (devices[i].role === 3) return devices[i]
        return {}
    }
    readonly property var metrics: wheel.metrics || {}
    readonly property int slotCount: metrics.slots > 0 ? metrics.slots : 7
    readonly property int position: metrics.position >= 0 ? metrics.position : 0
    readonly property bool wheelBusy: !!metrics.moving || !!metrics.calibrating
    readonly property bool wheelConnected: !!wheel.connected
    readonly property bool bidirectional: metrics.bidirectional === true
    readonly property bool interactive: !!deviceManager && wheelConnected && !wheelBusy
    readonly property var slotCountOptions: [5, 7, 8]
    readonly property var focusSettings: deviceManager && deviceManager.filterFocusSettings ? deviceManager.filterFocusSettings : ({enabled:false, reference:1, editable:false})

    function channelSuffixAt(slot) {
        const channels = deviceManager && deviceManager.filterWheelChannels ? deviceManager.filterWheelChannels : []
        for (let i = 0; i < channels.length; ++i)
            if (channels[i].slot === slot) return String(channels[i].suffix || "")
        return ""
    }

    function slotLabel(slot) {
        const suffix = channelSuffixAt(slot)
        return suffix ? slot + " · " + suffix : "#" + slot
    }

    function positionOptions() {
        const options = []
        for (let i = 1; i <= slotCount; ++i) options.push(slotLabel(i))
        return options
    }

    sections: [
        { title: "设备连接", rows: [
            { label: "设备", type: "select", options: wheel.selectedName ? [wheel.selectedName] : [], wide: true },
            { label: "连接", type: "toggle", checked: wheelConnected }
        ]},
        { title: "滤镜轮", rows: [
            { label: "当前位置", type: "select",
              options: positionOptions(),
              currentIndex: Math.min(position, slotCount - 1),
              enabled: interactive,
              onActivated: index => deviceManager.moveFilterWheel(index) },
            { label: "校准", type: "button",
              value: metrics.calibrating ? "校准中…" : metrics.moving ? "移动中…" : "开始校准",
              enabled: interactive,
              onClicked: () => deviceManager.calibrateFilterWheel() },
            { label: "双向旋转", type: "toggle",
              checked: bidirectional,
              enabled: !!deviceManager && wheelConnected,
              onToggled: checked => deviceManager.setFilterWheelBidirectional(checked) },
            { label: "轮孔数量", type: "select",
              options: ["5 孔", "7 孔", "8 孔"],
              currentIndex: Math.max(0, slotCountOptions.indexOf(slotCount)),
              enabled: interactive,
              onActivated: index => deviceManager.setFilterWheelSlots(slotCountOptions[index]) }
        ]},
        { title: "", rows: [{ label: "高级设置", type: "button", value: "打开 ›" }] }
    ]

    footer: channelFooter

    Component {
        id: channelFooter
        ColumnLayout {
            spacing: AsterTheme.space2
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "通道设置"
                    color: AsterTheme.deviceTitleText
                    font.family: AsterTheme.uiFont
                    font.pixelSize: 12
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: "启用焦点偏移"
                    color: AsterTheme.deviceBodyText
                    font.family: AsterTheme.uiFont
                    font.pixelSize: 12
                }
                AsterSwitch {
                    objectName: "filterFocusEnabled"
                    Accessible.name: "启用焦点偏移"
                    checked: !!root.focusSettings.enabled
                    enabled: !!root.deviceManager && !!root.focusSettings.editable
                    onToggled: {
                        root.deviceManager.setFilterFocusEnabled(checked)
                        checked = Qt.binding(function() { return !!root.focusSettings.enabled })
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !!root.focusSettings.enabled
                spacing: AsterTheme.space3
                Text {
                    text: "基准滤镜"
                    color: AsterTheme.deviceBodyText
                    font.family: AsterTheme.uiFont
                    font.pixelSize: 12
                }
                DesktopComboBox {
                    objectName: "filterFocusReference"
                    Accessible.name: "焦点偏移基准滤镜"
                    Layout.preferredWidth: 125
                    model: root.positionOptions()
                    enabled: !!root.deviceManager && !!root.focusSettings.editable
                    currentIndex: root.focusSettings.reference - 1
                    onActivated: function(index) {
                        root.deviceManager.setFilterFocusReference(index + 1)
                        currentIndex = Qt.binding(function() { return root.focusSettings.reference - 1 })
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: "基准为 0 · 偏移以实测步数填写"
                    color: AsterTheme.deviceMutedText
                    font.family: AsterTheme.uiFont
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }
            Text {
                Layout.fillWidth: true
                visible: !!root.focusSettings.enabled
                text: "仅计划换镜时应用；先完成一次自动对焦建立基准。切换基准前，请先填写该滤镜的偏移。"
                color: AsterTheme.deviceMutedText
                font.family: AsterTheme.uiFont
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
            FilterWheelChannelTable {
                objectName: "filterWheelChannelTable"
                deviceManager: root.deviceManager
                Layout.fillWidth: true
            }
        }
    }
}
