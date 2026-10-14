import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/settings"

TestCase {
    id: testCase
    name: "AutoFocusPanel"
    when: windowShown
    width: 420; height: 700
    visible: true

    Component {
        id: controllerComponent
        QtObject {
            property var settings: ({step: 80, points: 9, exposure: 3.0, frames: 1, backlash: 0})
            property bool busy: false
            property bool succeeded: false
            property string status: "未开始"
            property var samples: []
            property var fitCurve: []
            property int bestPosition: -1
            property real bestHfr: NaN
            property real verifiedHfr: NaN
            property real rSquared: NaN
            property int starCount: 0
            property var startedSettings: ({})
            property bool startAccepted: true
            property int cancelCount: 0
            function setSetting(key, value) {
                const next = Object.assign({}, settings)
                next[key] = value
                settings = next
            }
            function start() {
                startedSettings = Object.assign({}, settings)
                if (startAccepted) busy = true
                else status = "相机正忙"
                return startAccepted
            }
            function cancel() { cancelCount++; busy = false; status = "已停止" }
        }
    }
    Component { id: settingsComponent; FocuserSettings { width: 400; height: 640 } }
    Component { id: spyComponent; SignalSpy {} }
    Component { id: hostComponent; Window { width: 1050; height: 760; visible: true } }
    Component {
        id: managerComponent
        QtObject {
            signal devicesChanged()
            property int previewCounter: 0
            property bool cameraConnected: true
            property bool focuserConnected: true
            function device(role) {
                if (role === 1) return {connected: cameraConnected}
                if (role === 4) return {connected: focuserConnected}
                return {connected: false}
            }
        }
    }

    function panelWith(controller, properties) {
        const component = Qt.createComponent("../../qml/panels/AutoFocusPanel.qml")
        compare(component.status, Component.Ready, component.errorString())
        const options = Object.assign({controller: controller, width: 380,
                                          cameraConnected: true, focuserConnected: true}, properties || {})
        return createTemporaryObject(component, testCase, options)
    }

    function test_startCommitsFocusedParameters() {
        const controller = createTemporaryObject(controllerComponent, testCase)
        const panel = panelWith(controller)
        const step = findChild(panel, "autoFocusStep")
        const exposure = findChild(panel, "autoFocusExposure")
        const start = findChild(panel, "autoFocusStart")
        verify(step !== null && start !== null)
        step.text = "120"
        exposure.text = "4.5"
        findChild(panel, "autoFocusPoints").text = "11"
        exposure.forceActiveFocus()
        verify(start.enabled)
        mouseClick(start)
        compare(controller.startedSettings.step, 120)
        compare(controller.startedSettings.exposure, 4.5)
        compare(controller.startedSettings.points, 11)
        verify(controller.busy)
    }

    function test_invalidParametersBlockStart_data() {
        return [
            {tag: "empty-step", name: "autoFocusStep", text: ""},
            {tag: "zero-step", name: "autoFocusStep", text: "0"},
            {tag: "fractional-step", name: "autoFocusStep", text: "1.5"},
            {tag: "large-step", name: "autoFocusStep", text: "100001"},
            {tag: "even-points", name: "autoFocusPoints", text: "8"},
            {tag: "too-few-points", name: "autoFocusPoints", text: "3"},
            {tag: "too-many-points", name: "autoFocusPoints", text: "17"},
            {tag: "zero-exposure", name: "autoFocusExposure", text: "0"},
            {tag: "long-exposure", name: "autoFocusExposure", text: "3601"},
            {tag: "zero-frames", name: "autoFocusFrames", text: "0"},
            {tag: "too-many-frames", name: "autoFocusFrames", text: "4"},
            {tag: "negative-backlash", name: "autoFocusBacklash", text: "-1"},
            {tag: "large-backlash", name: "autoFocusBacklash", text: "100001"}
        ]
    }
    function test_invalidParametersBlockStart(data) {
        const controller = createTemporaryObject(controllerComponent, testCase)
        const panel = panelWith(controller)
        findChild(panel, "autoFocusAdvanced").clicked()
        const field = findChild(panel, data.name)
        verify(field !== null)
        field.text = data.text
        verify(!findChild(panel, "autoFocusStart").enabled)
        verify(findChild(panel, "autoFocusValidation").visible)
        compare(controller.startedSettings.step, undefined)
    }

    function test_busyLocksParametersButStopWorksAfterDisconnect() {
        const controller = createTemporaryObject(controllerComponent, testCase)
        const panel = panelWith(controller)
        controller.busy = true
        panel.cameraConnected = false
        panel.focuserConnected = false
        verify(!findChild(panel, "autoFocusStep").enabled)
        verify(!findChild(panel, "autoFocusExposure").enabled)
        verify(!findChild(panel, "autoFocusPoints").enabled)
        verify(!findChild(panel, "autoFocusFrames").enabled)
        verify(!findChild(panel, "autoFocusBacklash").enabled)
        verify(!findChild(panel, "autoFocusStart").enabled)
        const stop = findChild(panel, "autoFocusStop")
        verify(stop.enabled && stop.visible)
        mouseClick(stop)
        compare(controller.cancelCount, 1)
        verify(!controller.busy)
    }

    function test_sequenceOwnershipLocksEditsAndManualStartButAllowsStop() {
        const controller = createTemporaryObject(controllerComponent, testCase)
        const panel = panelWith(controller, {controlsLocked: true})
        verify(panel !== null)
        for (const name of ["autoFocusStep", "autoFocusExposure", "autoFocusPoints", "autoFocusFrames", "autoFocusBacklash"])
            verify(!findChild(panel, name).enabled)
        verify(!findChild(panel, "autoFocusStart").enabled)
        const step = findChild(panel, "autoFocusStep")
        step.text = "120"
        step.editingFinished()
        panel.start()
        compare(controller.settings.step, 80)
        compare(controller.startedSettings.step, undefined)
        controller.busy = true
        const stop = findChild(panel, "autoFocusStop")
        verify(stop.enabled && stop.visible)
        mouseClick(stop)
        compare(controller.cancelCount, 1)
    }

    function test_readyRequiresBothDevicesAndController() {
        const controller = createTemporaryObject(controllerComponent, testCase)
        const panel = panelWith(controller, {cameraConnected: false})
        const start = findChild(panel, "autoFocusStart")
        verify(!start.enabled)
        panel.cameraConnected = true
        verify(start.enabled)
        panel.focuserConnected = false
        verify(!start.enabled)
        panel.controller = null
        verify(!start.enabled)
    }

    function test_editsPersistAndFollowExternalSettings() {
        const controller = createTemporaryObject(controllerComponent, testCase)
        const panel = panelWith(controller)
        const step = findChild(panel, "autoFocusStep")
        step.text = "140"
        step.editingFinished()
        compare(controller.settings.step, 140)
        controller.setSetting("step", 160)
        compare(step.text, "160")
        verify(!findChild(panel, "autoFocusFrames").visible)
        mouseClick(findChild(panel, "autoFocusAdvanced"))
        verify(findChild(panel, "autoFocusFrames").visible)
    }

    function test_resultsAndPlotUseControllerData() {
        const controller = createTemporaryObject(controllerComponent, testCase)
        const panel = panelWith(controller)
        const empty = findChild(panel, "autoFocusPlotEmpty")
        verify(empty.visible)
        controller.busy = true
        controller.status = "采样 3 / 9"
        compare(findChild(panel, "autoFocusStatus").text, "采样 3 / 9")
        controller.samples = [{position: 1000, hfr: 4.0, stars: 30}, {position: 1160, hfr: 2.0, stars: 36}]
        controller.fitCurve = [{position: 1000, hfr: 3.8}, {position: 1160, hfr: 2.1}, {position: 1320, hfr: 4.2}]
        const plot = findChild(panel, "autoFocusPlot")
        compare(plot.samples.length, 2)
        compare(plot.fitCurve.length, 3)
        verify(!empty.visible)
        controller.bestPosition = 1152
        controller.bestHfr = 2.12
        controller.verifiedHfr = 2.18
        controller.rSquared = 0.97
        controller.starCount = 36
        controller.busy = false
        controller.succeeded = true
        controller.status = "自动对焦完成"
        const results = findChild(panel, "autoFocusResults")
        verify(results.text.indexOf("1152") >= 0)
        verify(results.text.indexOf("2.12") >= 0)
        verify(results.text.indexOf("2.18") >= 0)
        verify(results.text.indexOf("0.970") >= 0)
        compare(findChild(panel, "autoFocusStatus").text, "自动对焦完成")
    }

    function test_backendRefreshPreservesFocusedEdit() {
        const controller = createTemporaryObject(controllerComponent, testCase)
        const panel = panelWith(controller)
        const step = findChild(panel, "autoFocusStep")
        step.forceActiveFocus()
        step.selectAll()
        keyClick(Qt.Key_1)
        keyClick(Qt.Key_2)
        controller.setSetting("step", 180)
        compare(step.text, "12")
        keyClick(Qt.Key_0)
        mouseClick(findChild(panel, "autoFocusStart"))
        compare(controller.startedSettings.step, 120)
    }

    function test_rejectedStartShowsNotice() {
        const controller = createTemporaryObject(controllerComponent, testCase, {startAccepted: false})
        const panel = panelWith(controller)
        const spy = createTemporaryObject(spyComponent, testCase, {target: panel, signalName: "noticeRequested"})
        mouseClick(findChild(panel, "autoFocusStart"))
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "相机正忙")
    }

    function test_focuserSettingsUseLiveAutofocusParameters() {
        const controller = createTemporaryObject(controllerComponent, testCase)
        const settings = createTemporaryObject(settingsComponent, testCase, {autoFocusController: controller})
        const group = settings.sections.filter(section => section.title === "自动对焦")[0]
        const step = group.rows.filter(row => row.label === "步长")[0]
        compare(step.type, "field")
        step.onEdited("120")
        compare(controller.settings.step, 120)
        controller.busy = true
        const busyGroup = settings.sections.filter(section => section.title === "自动对焦")[0]
        verify(!busyGroup.rows.filter(row => row.label === "步长")[0].enabled)
        const motion = findChild(settings, "focuserMoveButton")
        verify(motion !== null, "Existing manual motion controls must remain")
    }

    function test_captureWorkspaceEntryUsesConnectedDevicesAndController() {
        const host = createTemporaryObject(hostComponent, testCase)
        const controller = createTemporaryObject(controllerComponent, testCase)
        const manager = createTemporaryObject(managerComponent, testCase)
        const component = Qt.createComponent("../../qml/pages/CaptureWorkspace.qml")
        compare(component.status, Component.Ready, component.errorString())
        const workspace = createTemporaryObject(component, host.contentItem, {
            width: host.width, height: host.height, deviceManager: manager, autoFocusController: controller
        })
        verify(waitForRendering(workspace))
        mouseClick(findChild(workspace, "openAutoFocusPanel"))
        const popup = findChild(workspace, "captureAutoFocusPopup")
        tryCompare(popup, "opened", true)
        const step = findChild(workspace, "autoFocusStep")
        const exposure = findChild(workspace, "autoFocusExposure")
        const start = findChild(workspace, "autoFocusStart")
        compare(workspace.sequenceActive, false)
        workspace.sequenceActive = true
        for (const name of ["autoFocusStep", "autoFocusExposure", "autoFocusPoints", "autoFocusFrames", "autoFocusBacklash"])
            verify(!findChild(workspace, name).enabled)
        verify(!start.enabled)
        workspace.sequenceActive = false
        verify(step.enabled && exposure.enabled && start.enabled)
        step.text = "160"
        step.editingFinished()
        compare(controller.settings.step, 160)
        exposure.forceActiveFocus()
        exposure.text = "4.5"
        verify(start.enabled)
        mouseClick(start)
        compare(controller.startedSettings.step, 160)
        compare(controller.startedSettings.exposure, 4.5)
        verify(controller.busy)
        workspace.sequenceActive = true
        manager.focuserConnected = false
        manager.devicesChanged()
        const stop = findChild(workspace, "autoFocusStop")
        verify(stop.enabled && stop.visible)
        mouseClick(stop)
        compare(controller.cancelCount, 1)
        verify(!start.enabled)
        manager.focuserConnected = true
        manager.cameraConnected = false
        manager.devicesChanged()
        verify(!start.enabled)
        manager.cameraConnected = true
        manager.devicesChanged()
        verify(!start.enabled)
        workspace.sequenceActive = false
        verify(start.enabled)
        verify(step.enabled && exposure.enabled)
        mouseClick(findChild(workspace, "openPhotoSolvePanel"))
        tryCompare(popup, "opened", false)
    }
}
