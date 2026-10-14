import QtQuick
import QtTest
import "../../qml/pages"

TestCase {
    name: "GuideWorkbench"
    when: windowShown
    visible: true
    width: 1440; height: 900
    Component {
        id: managerComponent
        QtObject {
            property var devices: [{}, {}, {connected: true, selectedName: "G3M678C", metrics: {previewing: false, exposureUs: 2000000, gain: 68, gainMin: 0, gainMax: 500}}]
            property int guidePreviewCounter: 0
            property string lastError: ""
            property int stops: 0
            property real exposureWritten: 0
            property int gainWritten: -1
            function startGuideLoop() { return true }
            function stopGuideLoop() { stops++; return true }
            function setGuideExposure(value) { exposureWritten = value; return true }
            function setGuideGain(value) { gainWritten = value; return true }
        }
    }
    Component {
        id: processorComponent
        QtObject {
            property int frameCount: 0
            property real frameWidth: 640
            property real frameHeight: 480
            property string state: "idle"
            property bool locked: false
            property bool calibrated: false
            property bool guiding: false
            property bool outputLinked: false
            property real rmsRa: 0
            property real rmsDec: 0
            property int missCount: 0
            property real starX: 320
            property real starY: 240
            property real errorX: 2
            property real errorY: -1
            property real snr: 18
            property real hfd: 3
            property real mass: 1500
            property var curve: []
            property string algorithm: "hysteresis"
            property int raHysteresisPercent: 10
            property int decHysteresisPercent: 10
            property int raAggressionPercent: 70
            property int decAggressionPercent: 70
            property real raMinMovePx: 0.2
            property real decMinMovePx: 0.2
            property string pulseLog: ""
            property int stops: 0
            function stopGuiding() { stops++; guiding = false }
            function clearStar() { locked = false; state = "looping" }
            function autoSelect() { return false }
        }
    }
    Component { id: pageComponent; GuideWorkspace {} }
    Component { id: noticeSpy; SignalSpy { signalName: "noticeRequested" } }
    function test_failedStarSelectionRequestsToast() {
        const page = makePage(1372, 832)
        const spy = createTemporaryObject(noticeSpy, this, {target: page})
        findChild(page, "guideAutoSelectButton").clicked()
        compare(spy.count, 1)
        verify(spy.signalArguments[0][0].indexOf("未找到有效星点") >= 0)
    }
    function test_axisEditsDoNotOverwriteOtherAxis() {
        const page = makePage(1372, 832)
        const ra = findChild(page, "guideRaParametersButton")
        const dec = findChild(page, "guideDecParametersButton")
        verify(ra !== null && dec !== null)
        const aggression = findChild(page, "aggressionField")
        const minimum = findChild(page, "guideMinMoveField")
        aggression.valueEdited("80")
        compare(page.guider.raAggressionPercent, 80)
        compare(page.guider.decAggressionPercent, 70)
        dec.clicked()
        compare(aggression.value, "70")
        minimum.valueEdited("0.45")
        compare(page.guider.decMinMovePx, 0.45)
        compare(page.guider.raMinMovePx, 0.2)
        ra.clicked()
        compare(aggression.value, "80")
        page.guider.raAggressionPercent = 60
        compare(aggression.value, "60") // backend binding survives editing
    }
    function test_exposureStateFollowsBackendAndDisconnectHidesFrame() {
        const page = makePage(1372, 832)
        page.deviceManager.devices = [{}, {}, {connected: true, metrics: {previewing: true}}]
        compare(page.loopActive, true)
        page.deviceManager.devices = [{}, {}, {connected: true, metrics: {previewing: false}}]
        compare(page.loopActive, false)
        page.deviceManager.devices = [{}, {}, {connected: false, metrics: {}}]
        page.deviceManager.guidePreviewCounter = 1
        compare(page.hasImage, false)
        compare(findChild(page, "guideExposureField").enabled, false)
    }
    function test_manualFieldEditsPersistForApply() {
        const page = makePage(1372, 832)
        const field = findChild(page, "calImageScaleField")
        field.valueEdited("1.25")
        compare(field.value, "1.25")
    }
    function makePage(w, h) {
        const manager = createTemporaryObject(managerComponent, this)
        const processor = createTemporaryObject(processorComponent, this)
        return createTemporaryObject(pageComponent, this, {width: w, height: h, deviceManager: manager, guider: processor})
    }
    function test_layout_data() {
        return [{tag: "compact", w: 1212, h: 650}, {tag: "desktop", w: 1372, h: 832}]
    }
    function test_layout(data) {
        const page = makePage(data.w, data.h)
        verify(waitForRendering(page))
        const preview = findChild(page, "guideImagePane")
        const curve = findChild(page, "guideChartPane")
        const panel = findChild(page, "guideInspector")
        verify(preview !== null, "Guide image needs a dedicated pane")
        verify(curve !== null)
        verify(panel !== null)
        verify(preview.width > 600 && preview.height >= 260)
        verify(curve.mapToItem(page, 0, 0).y >= preview.mapToItem(page, 0, 0).y + preview.height)
        verify(panel.width >= 280 && panel.width <= 320)
        verify(panel.mapToItem(page, panel.width, 0).x <= page.width + 1)
        verify(!findChild(page, "guideAdvancedBody").visible)
        wait(100) // Canvas painting is asynchronous after the layout pass.
        grabImage(page).save(Qt.resolvedUrl("../../out/agent-work/guide-" + data.tag + ".png").toString().replace("file:///", ""))
    }
    function test_noHardwareOutputCannotStartRealGuiding() {
        const page = makePage(1372, 832)
        page.guider.calibrated = true; page.guider.locked = true; page.guider.frameCount = 1
        compare(findChild(page, "startGuidingButton").enabled, false)
        const status = findChild(page, "guideOutputStatus")
        verify(status !== null)
        verify(status.text.indexOf("未接入") >= 0)
    }
    function test_stopStopsBothAnalysisAndExposure() {
        const page = makePage(1372, 832)
        page.guider.guiding = true
        findChild(page, "stopGuidingButton").clicked()
        compare(page.guider.guiding, false)
        compare(page.deviceManager.stops, 1)
    }
    function test_cameraFieldsUseGuideBackend() {
        const page = makePage(1372, 832)
        const exposure = findChild(page, "guideExposureField")
        const gain = findChild(page, "guideGainField")
        verify(exposure !== null && gain !== null)
        exposure.valueEdited("1.5"); gain.valueEdited("80")
        compare(page.deviceManager.exposureWritten, 1.5)
        compare(page.deviceManager.gainWritten, 80)
    }
}
