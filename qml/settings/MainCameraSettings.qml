import QtQuick
import QtQuick.Dialogs
import "../controls"

SettingsPage {
    id: root
    property var deviceManager: null
    telemetrySource: deviceManager ? deviceManager.cameraTelemetry : null

    readonly property var camera: {
        const devices = deviceManager ? deviceManager.devices : []
        for (let i = 0; i < devices.length; ++i) if (devices[i].role === 1) return devices[i]
        return {}
    }
    readonly property var metrics: camera.metrics || {}
    readonly property bool interactive: !!deviceManager && !!camera.connected
    // UI display order [HCG, LCG, HDR] maps to the SDK conversion-gain values.
    readonly property var gainModeValues: [1, 0, 2]
    readonly property int gainModeIndex: {
        const index = gainModeValues.indexOf(metrics.gainMode !== undefined ? metrics.gainMode : 1)
        return index >= 0 ? index : 0
    }
    readonly property var binningOptions: ["1 × 1", "2 × 2", "3 × 3", "4 × 4"]
    readonly property int binningIndex: metrics.binning !== undefined ? Math.min(Math.max(metrics.binning, 1), 4) - 1 : 0
    // 分辨率表由相机连接信息带回（model->res，eSize 索引可选）
    readonly property var resolutionOptions: metrics.resolutions !== undefined ? metrics.resolutions : []
    readonly property int resolutionIndex: metrics.resolutionIndex !== undefined ? metrics.resolutionIndex : 0

    function exposureSecondsText() {
        return metrics.exposureUs !== undefined ? parseFloat((metrics.exposureUs / 1000000).toFixed(3)).toString() : "—"
    }

    sections: [
        { title: "设备连接", rows: [
            { label: "主镜焦距", value: "480 mm" }
        ]},
        { title: "曝光参数", rows: [
            { label: "增益模式", type: "segment", options: ["HCG", "LCG", "HDR"],
              currentIndex: gainModeIndex, visible: metrics.canGainMode === true, enabled: interactive,
              onActivated: index => deviceManager.setCameraGainMode(gainModeValues[index]) },
            { label: "增益", type: "field",
              value: metrics.gain !== undefined ? metrics.gain : "—",
              minimum: metrics.gainMin !== undefined ? metrics.gainMin : 100,
              maximum: metrics.gainMax !== undefined ? metrics.gainMax : 10000,
              enabled: interactive,
              onEdited: text => deviceManager.setCameraGain(Math.round(Number(text))) },
            { label: "曝光时间 (s)", type: "field",
              value: exposureSecondsText(),
              minimum: metrics.exposureMinUs !== undefined ? Math.max(metrics.exposureMinUs / 1000000, 0.001) : 0.001,
              maximum: metrics.exposureMaxUs !== undefined ? metrics.exposureMaxUs / 1000000 : 3600,
              enabled: interactive,
              onEdited: text => deviceManager.setCameraExposure(Number(text)) },
            { label: "分辨率", type: "select", options: resolutionOptions,
              currentIndex: resolutionIndex,
              enabled: interactive && resolutionOptions.length > 1,
              onActivated: index => root.deviceManager.setCameraResolution(1, index) },
            { label: "BIN", type: "select", options: binningOptions,
              currentIndex: binningIndex, enabled: interactive,
              onActivated: index => deviceManager.setCameraBinning(index + 1) },
            { label: "视频格式", type: "segment", options: ["MP4", "MP4+SER"], enabled: false },
            { label: "低噪声模式", type: "toggle", checked: metrics.lowNoise === true,
              visible: metrics.canLowNoise === true, enabled: interactive,
              onToggled: checked => deviceManager.setCameraLowNoise(checked) },
            { label: "偏移设置", type: "field",
              value: metrics.offset !== undefined ? metrics.offset : "—",
              minimum: 0, maximum: metrics.offsetMax !== undefined ? metrics.offsetMax : 31,
              visible: metrics.canBlackLevel === true, enabled: interactive,
              onEdited: text => deviceManager.setCameraOffset(Math.round(Number(text))) }
        ]},
        { title: "TEC 制冷", visible: metrics.canTec === true || metrics.canTemp === true, rows: [
            { label: "TEC", type: "toggle", checked: metrics.tecEnabled === true,
              visible: metrics.canTec === true, enabled: interactive,
              onToggled: checked => deviceManager.setCameraCoolerEnabled(checked) },
            { label: "目标温度 (°C)", type: "field",
              value: metrics.targetTemp !== undefined ? metrics.targetTemp : "—",
              minimum: metrics.tempMin !== undefined ? metrics.tempMin : -50,
              maximum: metrics.tempMax !== undefined ? metrics.tempMax : 40,
              visible: metrics.canTec === true, enabled: interactive,
              onEdited: text => deviceManager.setCameraTargetTemperature(Number(text)) },
            { label: "实际温度", type: "telemetry", key: "currentTemp",
              format: value => Number(value).toFixed(1) + " °C", visible: metrics.canTemp === true },
            { label: "制冷功率", type: "telemetry", key: "tecPower",
              format: value => Math.round(value) + "%", visible: metrics.canTemp === true },
            { label: "风扇", type: "toggle",
              checked: metrics.fanSpeed === undefined ? true : metrics.fanSpeed !== 0,
              visible: metrics.canFan === true, enabled: interactive,
              onToggled: checked => deviceManager.setCameraFanSpeed(checked ? -1 : 0) }
        ]},
        { title: "加热", visible: metrics.canHeat === true, rows: [
            { label: metrics.heatMax === 100 ? "防结露 (%)" : "防结露 (级)", type: "field",
              value: metrics.heatLevel !== undefined ? metrics.heatLevel : "—",
              minimum: 0, maximum: metrics.heatMax !== undefined ? metrics.heatMax : 100,
              enabled: interactive,
              onEdited: text => deviceManager.setCameraHeatLevel(Math.round(Number(text))) }
        ]},
        { title: "采集行为", rows: [
            { label: "自动重新拍摄", type: "toggle", checked: true, enabled: false },
            { label: "单帧自动保存", type: "toggle",
              checked: deviceManager ? deviceManager.autoSaveStills : true,
              enabled: !!deviceManager,
              onToggled: checked => deviceManager.autoSaveStills = checked },
            { label: "单帧格式", type: "select", options: ["FITS（含拍摄参数头）"],
              currentIndex: 0, enabled: false },
            { label: "图像类型", type: "select", options: ["LIGHT", "DARK", "FLAT", "BIAS"],
              currentIndex: deviceManager ? ["LIGHT", "DARK", "FLAT", "BIAS"].indexOf(deviceManager.stillImageType) : 0,
              enabled: !!deviceManager,
              onActivated: index => deviceManager.stillImageType = ["LIGHT", "DARK", "FLAT", "BIAS"][index] },
            { label: "保存目录", type: "button", value: "选择目录…",
              onClicked: () => saveDirectoryDialog.open() },
            { label: "当前保存位置", value: deviceManager ? deviceManager.stillSaveDirectory : "—", wide: true },
            { label: "恢复默认目录", type: "button", value: "恢复默认",
              onClicked: () => deviceManager.stillSaveDirectory = "" }
        ]},
        { title: "", rows: [{ label: "高级设置", type: "button", value: "打开 ›" }] }
    ]

    FolderDialog {
        id: saveDirectoryDialog
        title: "选择图像保存目录"
        onAccepted: {
            const path = selectedFolder.toString()
            root.deviceManager.stillSaveDirectory = path.startsWith("file:///")
                ? decodeURIComponent(path.substring(8)) : path
        }
    }
}
