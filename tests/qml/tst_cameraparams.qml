import QtQuick
import QtTest
import "../../qml/panels"
import "../../qml/settings"
import "../../qml/controls"

TestCase {
    name: "CameraParams"
    when: windowShown
    width: 900; height: 600

    readonly property var connectedCameraMetrics: {
        "exposureUs": 12000000, "exposureMinUs": 100, "exposureMaxUs": 3600000000,
        "gain": 180, "gainMin": 100, "gainMax": 10000,
        "offset": 20, "offsetMax": 496, "canBlackLevel": true,
        "binning": 1,
        "canTec": true, "tecEnabled": true, "targetTemp": -10, "tempMin": -50, "tempMax": 40,
        "canTemp": true, "currentTemp": -9.5, "tecPower": 62,
        "canFan": true, "fanSpeed": -1, "fanMax": 100,
        "canHeat": true, "heatLevel": 25, "heatMax": 100,
        "canGainMode": true, "gainMode": 1,
        "canLowNoise": true, "lowNoise": false
    }

    function makeDevices(metrics) {
        return [
            {role: 0, title: "赤道仪", selectedId: "", selectedName: "", connected: false, busy: false, error: "", metrics: {}},
            {role: 1, title: "主相机", selectedId: "cam", selectedName: "SkyEye 62AM", connected: true, busy: false, error: "", metrics: metrics},
            {role: 2, title: "导星相机", selectedId: "", selectedName: "", connected: false, busy: false, error: "", metrics: {}},
            {role: 3, title: "滤镜轮", selectedId: "", selectedName: "", connected: false, busy: false, error: "", metrics: {}},
            {role: 4, title: "电调焦", selectedId: "", selectedName: "", connected: false, busy: false, error: "", metrics: {}}
        ]
    }

    Component {
        id: managerComponent
        QtObject {
            property var devices: []
            property var exposureUpdates: []
            property var gainUpdates: []
            property var offsetUpdates: []
            property var binningUpdates: []
            property var coolerUpdates: []
            property var targetTempUpdates: []
            property var fanUpdates: []
            property var heatUpdates: []
            property var gainModeUpdates: []
            property var lowNoiseUpdates: []
            property string lastError: ""
            function candidatesForRole(role) { return [] }
            function selectDevice(role, id) { return true }
            function setConnected(role, connected) { return true }
            function setCameraExposure(seconds) { exposureUpdates = exposureUpdates.concat([seconds]); return true }
            function setCameraGain(gain) { gainUpdates = gainUpdates.concat([gain]); return true }
            function setCameraOffset(offset) { offsetUpdates = offsetUpdates.concat([offset]); return true }
            function setCameraBinning(factor) { binningUpdates = binningUpdates.concat([factor]); return true }
            function setCameraCoolerEnabled(enabled) { coolerUpdates = coolerUpdates.concat([enabled]); return true }
            function setCameraTargetTemperature(celsius) { targetTempUpdates = targetTempUpdates.concat([celsius]); return true }
            function setCameraFanSpeed(speed) { fanUpdates = fanUpdates.concat([speed]); return true }
            function setCameraHeatLevel(level) { heatUpdates = heatUpdates.concat([level]); return true }
            function setCameraGainMode(mode) { gainModeUpdates = gainModeUpdates.concat([mode]); return true }
            function setCameraLowNoise(enabled) { lowNoiseUpdates = lowNoiseUpdates.concat([enabled]); return true }
            function startCameraPreview() { previewRequests = previewRequests.concat(["start"]); return true }
            function stopCameraPreview() { previewRequests = previewRequests.concat(["stop"]); return true }
            function captureSingleExposure() { previewRequests = previewRequests.concat(["single"]); return true }
            property var previewRequests: []
            property var cameraTelemetry: {"currentTemp": -9.5, "tecPower": 62}
        }
    }

    Component {
        id: inspectorComponent
        Window {
            width: 400; height: 700; visible: true
            property alias inspector: inspectorPanel
            DeviceInspector {
                id: inspectorPanel
                anchors.fill: parent
                deviceManager: null
            }
        }
    }

    Component {
        id: settingsComponent
        MainCameraSettings { }
    }

    Component {
        id: telemetryRowComponent
        AsterSettingRow { }
    }

    function test_telemetryRowRendersLiveValue() {
        const manager = createTemporaryObject(managerComponent, this)
        const row = createTemporaryObject(telemetryRowComponent, this, {
            rowData: {label: "实际温度", type: "telemetry", key: "currentTemp",
                      format: value => Number(value).toFixed(1) + " °C"},
            telemetrySource: Qt.binding(() => manager.cameraTelemetry)
        })
        verify(waitForRendering(row))
        const loader = findChild(row, "settingRowEditor")
        verify(loader !== null && loader.item !== null)
        tryCompare(loader.item, "text", "-9.5 °C")
        manager.cameraTelemetry = {"currentTemp": -11.2, "tecPower": 58}
        tryCompare(loader.item, "text", "-11.2 °C")
        manager.cameraTelemetry = {}
        tryCompare(loader.item, "text", "—")
    }

    function test_inspectorShowsLiveCameraValues() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = makeDevices(connectedCameraMetrics)
        const host = createTemporaryObject(inspectorComponent, this)
        host.inspector.deviceManager = manager
        verify(waitForRendering(host.inspector))
        const section = findChild(host.inspector, "deviceSection1")
        verify(section !== null)
        section.expanded = true

        tryCompare(findChild(host.inspector, "cameraExposureField"), "value", "12")
        tryCompare(findChild(host.inspector, "cameraGainField"), "value", "180")
        tryCompare(findChild(host.inspector, "cameraOffsetField"), "value", "20")
        tryCompare(findChild(host.inspector, "cameraBinningCombo"), "currentIndex", 0)
        tryCompare(findChild(host.inspector, "cameraCoolerField"), "value", "-10")
        tryCompare(findChild(host.inspector, "cameraTempText"), "text", "实际温度 -9.5 °C    功率 62%")
    }

    function test_inspectorEditsDriveManager() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = makeDevices(connectedCameraMetrics)
        const host = createTemporaryObject(inspectorComponent, this)
        host.inspector.deviceManager = manager
        verify(waitForRendering(host.inspector))
        findChild(host.inspector, "deviceSection1").expanded = true

        findChild(host.inspector, "cameraExposureField").valueEdited("30")
        tryCompare(manager, "exposureUpdates", [30])
        findChild(host.inspector, "cameraGainField").valueEdited("200")
        tryCompare(manager, "gainUpdates", [200])
        findChild(host.inspector, "cameraOffsetField").valueEdited("42")
        tryCompare(manager, "offsetUpdates", [42])
        findChild(host.inspector, "cameraCoolerField").valueEdited("-15")
        tryCompare(manager, "targetTempUpdates", [-15])

        const binning = findChild(host.inspector, "cameraBinningCombo")
        verify(binning !== null, "Binning must be selectable through a dropdown")
        binning.activated(2)
        tryCompare(manager, "binningUpdates", [3])
    }

    function test_inspectorExposureButtonDrivesPreview() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = makeDevices(connectedCameraMetrics)
        const host = createTemporaryObject(inspectorComponent, this)
        host.inspector.deviceManager = manager
        verify(waitForRendering(host.inspector))
        const section = findChild(host.inspector, "deviceSection1")
        section.expanded = true

        const button = findChild(host.inspector, "exposureButton")
        tryCompare(button, "text", "开始曝光")
        button.clicked()
        tryCompare(manager, "previewRequests", ["single"])

        host.inspector.loopCapture = true
        tryCompare(button, "text", "开始循环曝光")
        button.clicked()
        tryCompare(manager, "previewRequests", ["single", "start"])

        host.inspector.loopCapture = false
        const stopButton = findChild(host.inspector, "stopExposureButton")
        verify(stopButton !== null)
        verify(!stopButton.enabled, "Stop stays disabled while the preview is idle")
    }

    function test_inspectorHidesUnsupportedControls() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = makeDevices({"exposureUs": 5000000, "gain": 120})
        const host = createTemporaryObject(inspectorComponent, this)
        host.inspector.deviceManager = manager
        verify(waitForRendering(host.inspector))
        findChild(host.inspector, "deviceSection1").expanded = true

        tryCompare(findChild(host.inspector, "cameraExposureField"), "value", "5")
        verify(!findChild(host.inspector, "cameraOffsetField").visible,
               "Offset stays hidden when the camera lacks black-level support")
        verify(!findChild(host.inspector, "cameraCoolerField").visible,
               "Cooler target stays hidden without TEC support")
        verify(!findChild(host.inspector, "cameraTempText").visible,
               "Temperature readout stays hidden without temperature support")
    }

    function test_settingsRowsDriveManager() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = makeDevices(connectedCameraMetrics)
        const settings = createTemporaryObject(settingsComponent, this, {deviceManager: manager})
        verify(waitForRendering(settings))
        const exposure = settings.sections.filter(section => section.title === "曝光参数")[0]
        verify(exposure !== undefined, "Settings page keeps the 曝光参数 group")

        const gainMode = exposure.rows.find(row => row.label === "增益模式")
        compare(gainMode.currentIndex, 0, "HCG (SDK value 1) is the first display option")
        gainMode.onActivated(2)
        tryCompare(manager, "gainModeUpdates", [2])

        const gain = exposure.rows.find(row => row.label === "增益")
        compare(gain.value, 180)
        gain.onEdited("300")
        tryCompare(manager, "gainUpdates", [300])

        const exposureRow = exposure.rows.find(row => row.label === "曝光时间 (s)")
        compare(exposureRow.value, "12")
        exposureRow.onEdited("0.5")
        tryCompare(manager, "exposureUpdates", [0.5])

        const binning = exposure.rows.find(row => row.label === "BIN")
        compare(binning.currentIndex, 0)
        binning.onActivated(1)
        tryCompare(manager, "binningUpdates", [2])

        const offset = exposure.rows.find(row => row.label === "偏移设置")
        compare(offset.value, 20)
        offset.onEdited("35")
        tryCompare(manager, "offsetUpdates", [35])

        const tec = settings.sections.filter(section => section.title === "TEC 制冷")[0]
        verify(tec !== undefined, "TEC group is present for a cooled camera")
        const cooler = tec.rows.find(row => row.label === "TEC")
        compare(cooler.checked, true)
        cooler.onToggled(false)
        tryCompare(manager, "coolerUpdates", [false])
        const target = tec.rows.find(row => row.label === "目标温度 (°C)")
        compare(target.value, -10)
        target.onEdited("-15")
        tryCompare(manager, "targetTempUpdates", [-15])
        compare(tec.rows.find(row => row.label === "实际温度").type, "telemetry")
        compare(tec.rows.find(row => row.label === "制冷功率").key, "tecPower")
        const fan = tec.rows.find(row => row.label === "风扇")
        compare(fan.checked, true)
        fan.onToggled(false)
        tryCompare(manager, "fanUpdates", [0])

        const heat = settings.sections.filter(section => section.title === "加热")[0]
        const dew = heat.rows.find(row => row.label === "防结露 (%)")
        compare(dew.value, 25)
        dew.onEdited("30")
        tryCompare(manager, "heatUpdates", [30])
    }

    function test_settingsHideUnsupportedSections() {
        const manager = createTemporaryObject(managerComponent, this)
        const metrics = {"exposureUs": 5000000, "gain": 120}
        manager.devices = makeDevices(metrics).map(device =>
            device.role === 1 ? Object.assign({}, device, {connected: false}) : device)
        const settings = createTemporaryObject(settingsComponent, this, {deviceManager: manager})
        verify(waitForRendering(settings))
        verify(settings.parameterSections.filter(section => section.title === "TEC 制冷")[0] === undefined,
               "TEC group is hidden without cooling capability")
        verify(settings.parameterSections.filter(section => section.title === "加热")[0] === undefined,
               "Heater group is hidden without heater capability")
        const exposure = settings.sections.filter(section => section.title === "曝光参数")[0]
        verify(exposure !== undefined)
        compare(exposure.rows.find(row => row.label === "增益模式").visible, false)
        compare(exposure.rows.find(row => row.label === "偏移设置").visible, false)
        compare(exposure.rows.find(row => row.label === "低噪声模式").visible, false)
        verify(!exposure.rows.find(row => row.label === "增益").enabled,
               "Parameter rows stay disabled while the camera is disconnected")
    }

    function test_settingsWithoutManagerStaysInactive() {
        const settings = createTemporaryObject(settingsComponent, this)
        verify(waitForRendering(settings))
        const exposure = settings.sections.filter(section => section.title === "曝光参数")[0]
        verify(exposure !== undefined)
        verify(!exposure.rows.find(row => row.label === "增益").enabled)
        verify(!exposure.rows.find(row => row.label === "曝光时间 (s)").enabled)
        // 分辨率行已是下拉：无连接时无选项且禁用，不再有静态 value 文本
        const resolution = exposure.rows.find(row => row.label === "分辨率")
        verify(resolution !== undefined)
        verify(!resolution.enabled)
        compare(resolution.options.length, 0)
    }
}
