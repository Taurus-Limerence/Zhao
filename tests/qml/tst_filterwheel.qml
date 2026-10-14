import QtQuick
import QtTest
import "../../qml/panels"
import "../../qml/settings"

TestCase {
    name: "FilterWheel"
    when: windowShown
    visible: true
    width: 900; height: 600

    readonly property var connectedWheelMetrics: {
        "slots": 7, "position": 2, "moving": false, "calibrating": false, "bidirectional": true
    }

    function makeDevices(metrics) {
        return [
            {role: 0, title: "赤道仪", selectedId: "", selectedName: "", connected: false, busy: false, error: "", metrics: {}},
            {role: 1, title: "主相机", selectedId: "", selectedName: "", connected: false, busy: false, error: "", metrics: {}},
            {role: 2, title: "导星相机", selectedId: "", selectedName: "", connected: false, busy: false, error: "", metrics: {}},
            {role: 3, title: "滤镜轮", selectedId: "wheel", selectedName: "EFW-7", connected: true, busy: false, error: "", metrics: metrics},
            {role: 4, title: "电调焦", selectedId: "", selectedName: "", connected: false, busy: false, error: "", metrics: {}}
        ]
    }

    Component {
        id: managerComponent
        QtObject {
            property var devices: []
            property var moves: []
            property var slotChanges: []
            property var calibrations: 0
            property var bidirectionalChanges: []
            property var channelUpdates: []
            property var filterFocusSettings: ({enabled:false, reference:1, editable:true, baselineValid:false})
            property var offsetUpdates: []
            function setFilterFocusEnabled(value) {
                filterFocusSettings = Object.assign({}, filterFocusSettings, {enabled:value})
                return true
            }
            function setFilterFocusReference(slot) {
                filterFocusSettings = Object.assign({}, filterFocusSettings, {reference:slot})
                return true
            }
            function setFilterWheelFocusOffset(slot, value) {
                offsetUpdates = offsetUpdates.concat([[slot, value]])
                const next = filterWheelChannels.map(channel => channel.slot === slot
                    ? Object.assign({}, channel, {focusOffset:value}) : channel)
                filterWheelChannels = next
                return true
            }
            property var filterWheelChannels: [
                {slot: 1, suffix: "L", gain: 120, exposure: 120},
                {slot: 2, suffix: "R", gain: 120, exposure: 180}
            ]
            property string lastError: ""
            function candidatesForRole(role) { return [] }
            function selectDevice(role, id) { return true }
            function setConnected(role, connected) { return true }
            function moveFilterWheel(position) {
                if (!(position >= 0 && position < (devices[3].metrics.slots || 0))) {
                    lastError = "滤镜位置超出范围"
                    return false
                }
                moves = moves.concat([position])
                setWheelMetrics({position: position, moving: false})
                lastError = ""
                return true
            }
            function calibrateFilterWheel() {
                calibrations = calibrations + 1
                return true
            }
            function setFilterWheelSlots(slotCount) {
                slotChanges = slotChanges.concat([slotCount])
                setWheelMetrics({slots: slotCount})
                return true
            }
            function setFilterWheelBidirectional(bidirectional) {
                bidirectionalChanges = bidirectionalChanges.concat([bidirectional])
                return true
            }
            function setFilterWheelChannel(slot, suffix, gain, exposure) {
                channelUpdates = channelUpdates.concat([[slot, suffix, gain, exposure]])
                // Mirror the real manager: the store updates on save so the
                // table fields resync to the persisted values.
                const next = JSON.parse(JSON.stringify(filterWheelChannels))
                let replaced = false
                for (let i = 0; i < next.length; ++i) {
                    if (next[i].slot === slot) { next[i] = {slot: slot, suffix: suffix, gain: gain, exposure: exposure}; replaced = true }
                }
                if (!replaced) next.push({slot: slot, suffix: suffix, gain: gain, exposure: exposure})
                filterWheelChannels = next
                return true
            }
            function setWheelMetrics(patch) {
                const next = JSON.parse(JSON.stringify(devices))
                for (const key in patch) next[3].metrics[key] = patch[key]
                devices = next
            }
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
        FilterWheelSettings { width: 760; height: 580 }
    }

    function test_inspectorMovesAndCalibrates() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = makeDevices(connectedWheelMetrics)
        const host = createTemporaryObject(inspectorComponent, this)
        host.inspector.deviceManager = manager
        verify(waitForRendering(host.inspector))
        const section = findChild(host.inspector, "deviceSection3")
        verify(section !== null)
        section.expanded = true

        tryCompare(section, "wheelPositionText", "#3")
        const switchButton = findChild(host.inspector, "filterSwitchButton")
        const calibrateButton = findChild(host.inspector, "filterCalibrateButton")
        verify(switchButton !== null && calibrateButton !== null)
        verify(switchButton.enabled, "Buttons enabled once the wheel is connected")
        verify(calibrateButton.enabled)

        const target = findChild(host.inspector, "wheelTargetCombo")
        verify(target !== null, "Target filter must be selectable through a dropdown")
        tryCompare(target, "count", 7)
        target.activated(4)
        compare(section.wheelTarget, 5)
        switchButton.clicked()
        tryCompare(manager, "moves", [4])
        tryCompare(section, "wheelPositionText", "#5")

        target.activated(0)
        switchButton.clicked()
        tryCompare(manager, "moves", [4, 0])
        tryCompare(section, "wheelPositionText", "1 · L")

        calibrateButton.clicked()
        tryCompare(manager, "calibrations", 1)
    }

    function test_inspectorBlocksWhileMoving() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = makeDevices({"slots": 7, "position": 1, "moving": true, "calibrating": false, "bidirectional": true})
        const host = createTemporaryObject(inspectorComponent, this)
        host.inspector.deviceManager = manager
        verify(waitForRendering(host.inspector))
        const section = findChild(host.inspector, "deviceSection3")
        section.expanded = true
        tryCompare(section, "wheelPositionText", "移动中…")
        verify(!findChild(host.inspector, "filterSwitchButton").enabled)
        verify(!findChild(host.inspector, "filterCalibrateButton").enabled)
        verify(!findChild(host.inspector, "wheelTargetCombo").enabled)
    }

    function test_settingsRowsDriveManager() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = makeDevices(connectedWheelMetrics)
        const settings = createTemporaryObject(settingsComponent, this, {deviceManager: manager})
        verify(waitForRendering(settings))
        const wheel = settings.sections.filter(section => section.title === "滤镜轮")[0]
        verify(wheel !== undefined, "Settings page keeps the 滤镜轮 group")

        const position = wheel.rows[0]
        compare(position.options, ["1 · L", "2 · R", "#3", "#4", "#5", "#6", "#7"])
        compare(position.currentIndex, 2)
        verify(position.enabled)
        position.onActivated(4)
        tryCompare(manager, "moves", [4])

        const calibrate = wheel.rows[1]
        compare(calibrate.value, "开始校准")
        calibrate.onClicked()
        tryCompare(manager, "calibrations", 1)

        const bidirectional = wheel.rows[2]
        compare(bidirectional.checked, true)
        bidirectional.onToggled(false)
        tryCompare(manager, "bidirectionalChanges", [false])

        const slotCount = wheel.rows[3]
        compare(slotCount.options, ["5 孔", "7 孔", "8 孔"])
        compare(slotCount.currentIndex, 1)
        slotCount.onActivated(0)
        tryCompare(manager, "slotChanges", [5])
    }

    function test_settingsReflectCalibrationAndDisabledState() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = makeDevices({"slots": 5, "position": 3, "moving": false, "calibrating": true, "bidirectional": false})
        const settings = createTemporaryObject(settingsComponent, this, {deviceManager: manager})
        verify(waitForRendering(settings))
        const wheel = settings.sections.filter(section => section.title === "滤镜轮")[0]
        compare(wheel.rows[0].options, ["1 · L", "2 · R", "#3", "#4", "#5"])
        verify(!wheel.rows[0].enabled, "Rows disabled while calibrating")
        compare(wheel.rows[1].value, "校准中…")
        verify(!wheel.rows[1].enabled)
        compare(wheel.rows[2].checked, false)
        compare(wheel.rows[3].currentIndex, 0)
    }

    function test_settingsWithoutManagerStaysInactive() {
        const settings = createTemporaryObject(settingsComponent, this)
        verify(waitForRendering(settings))
        const wheel = settings.sections.filter(section => section.title === "滤镜轮")[0]
        verify(wheel !== undefined)
        verify(!wheel.rows[0].enabled)
        verify(!wheel.rows[1].enabled)
        compare(wheel.rows[0].options, ["#1", "#2", "#3", "#4", "#5", "#6", "#7"])
    }

    function test_channelTableRowsFollowSlotCount() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = makeDevices(connectedWheelMetrics)
        const settings = createTemporaryObject(settingsComponent, this, {deviceManager: manager})
        verify(waitForRendering(settings))
        const table = findChild(settings, "filterWheelChannelTable")
        verify(table !== null, "Channel table replaces the placeholder rows")
        compare(table.slotCount, 7)
        verify(findChild(table, "channelSuffix7") !== null)
        verify(findChild(table, "channelGain7") !== null)
        verify(findChild(table, "channelExposure7") !== null)
        verify(findChild(table, "channelSuffix8") === null, "Rows must match the wheel slot count exactly")

        manager.setWheelMetrics({slots: 5})
        tryCompare(table, "slotCount", 5)
        verify(findChild(table, "channelSuffix5") !== null)
        verify(findChild(table, "channelSuffix6") === null)
        // Switching back restores 7 rows; stored values were never dropped.
        manager.setWheelMetrics({slots: 7})
        tryCompare(table, "slotCount", 7)
        verify(findChild(table, "channelSuffix7") !== null)
    }

    function test_channelTableEditsSave() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = makeDevices(connectedWheelMetrics)
        const settings = createTemporaryObject(settingsComponent, this, {deviceManager: manager})
        verify(waitForRendering(settings))
        const table = findChild(settings, "filterWheelChannelTable")
        const suffix = findChild(table, "channelSuffix1")
        const gain = findChild(table, "channelGain1")
        const exposure = findChild(table, "channelExposure1")
        compare(suffix.text, "L")
        compare(gain.text, "120")
        compare(exposure.text, "120")

        suffix.text = "Luminance"
        suffix.editingFinished()
        tryCompare(manager, "channelUpdates", [[1, "Luminance", 120, 120]])

        gain.text = "300"
        gain.editingFinished()
        tryCompare(manager, "channelUpdates", [[1, "Luminance", 120, 120], [1, "Luminance", 300, 120]])
    }

    function test_channelTableHighlightsCurrentSlot() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = makeDevices({"slots": 7, "position": 3, "moving": false, "calibrating": false, "bidirectional": true})
        const settings = createTemporaryObject(settingsComponent, this, {deviceManager: manager})
        verify(waitForRendering(settings))
        const table = findChild(settings, "filterWheelChannelTable")
        compare(table.currentPosition, 3)
    }

    function test_channelTableWithoutManagerUsesDefaults() {
        const settings = createTemporaryObject(settingsComponent, this)
        verify(waitForRendering(settings))
        const table = findChild(settings, "filterWheelChannelTable")
        verify(table !== null)
        const suffix = findChild(table, "channelSuffix1")
        verify(suffix !== null)
        compare(suffix.text, "")
    }

    function test_focusOffsetsHiddenUntilEnabledAndRetainedWhenDisabled() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = makeDevices(connectedWheelMetrics)
        manager.filterWheelChannels = [{slot:1,suffix:"L",gain:100,exposure:3,focusOffset:0},
                                       {slot:2,suffix:"R",gain:100,exposure:3,focusOffset:-80}]
        const settings = createTemporaryObject(settingsComponent, this, {deviceManager:manager})
        verify(waitForRendering(settings))
        const toggle = findChild(settings, "filterFocusEnabled")
        verify(toggle !== null)
        const reference = findChild(settings, "filterFocusReference")
        const offset = findChild(settings, "channelFocusOffset2")
        verify(reference !== null && offset !== null)
        verify(!reference.visible && !offset.visible)
        toggle.checked = true; toggle.toggled()
        verify(reference.visible && offset.visible)
        compare(offset.text, "-80")
        compare(findChild(settings, "channelFocusOffset1").text, "0")
        verify(!findChild(settings, "channelFocusOffset1").enabled)
        toggle.checked = false; toggle.toggled()
        verify(!offset.visible)
        toggle.checked = true; toggle.toggled()
        compare(offset.text, "-80")
        compare(manager.moves.length, 0)
    }

    function test_focusOffsetEditsDistinguishUnsetFromZeroAndLock() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = makeDevices(connectedWheelMetrics)
        manager.setFilterFocusEnabled(true)
        const settings = createTemporaryObject(settingsComponent, this, {deviceManager:manager})
        verify(waitForRendering(settings))
        const offset = findChild(settings, "channelFocusOffset2")
        verify(offset !== null)
        compare(offset.text, "")
        offset.forceActiveFocus()
        verify(offset.activeFocus)
        keyClick(Qt.Key_Minus)
        keyClick(Qt.Key_1)
        keyClick(Qt.Key_2)
        keyClick(Qt.Key_0)
        keyClick(Qt.Key_Return)
        compare(manager.offsetUpdates[0], [2,-120])
        offset.selectAll()
        keyClick(Qt.Key_0)
        keyClick(Qt.Key_Return)
        compare(manager.offsetUpdates[1], [2,0])
        offset.selectAll()
        keyClick(Qt.Key_Backspace)
        compare(offset.text, "")
        keyClick(Qt.Key_Return)
        compare(manager.offsetUpdates[2], [2,null])
        keyClick(Qt.Key_0)
        keyClick(Qt.Key_Enter)
        compare(manager.offsetUpdates[3], [2,0])
        offset.selectAll()
        keyClick(Qt.Key_Backspace)
        keyClick(Qt.Key_Enter)
        compare(manager.offsetUpdates[4], [2,null])
        for (const invalid of ["abc", "-", "1000001", "-1000001"]) {
            offset.text = invalid
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Enter)
            compare(manager.offsetUpdates.length, 5)
        }
        findChild(settings, "filterFocusReference").activated(1)
        compare(manager.filterFocusSettings.reference, 2)
        verify(!offset.enabled)
        manager.filterFocusSettings = Object.assign({},manager.filterFocusSettings,{editable:false})
        verify(!findChild(settings, "filterFocusEnabled").enabled)
        verify(!findChild(settings, "filterFocusReference").enabled)
        verify(!findChild(settings, "channelFocusOffset3").enabled)
        compare(manager.moves.length, 0)
    }
}
