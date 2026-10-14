import QtQuick
import QtTest
import "../../qml/shell"
import "../../qml/controls"
import "../../qml/pages"

TestCase {
    name: "FigmaDesktop"
    when: windowShown
    width: 1440
    height: 900

    function test_collapsedParametersOpensSelectedDeviceSettings() {
        const host = createTemporaryObject(desktopWindowComponent, this)
        const ui = host.ui
        const spy = createTemporaryObject(spyComponent, this, {target: ui, signalName: "settingsRequested"})
        verify(waitForRendering(ui))
        findChild(ui, "collapseInspectorButton").click()
        tryCompare(ui, "inspectorCollapsed", true)
        const button = findChild(ui, "collapsedParametersButton")
        verify(button !== null)
        verify(waitForRendering(button))
        for (const role of [1, 0, 2, 3, 4]) {
            ui.selectedDevice = role
            spy.clear()
            mouseClick(button)
            compare(ui.inspectorCollapsed, true)
            verify(!findChild(ui, "deviceInspector").visible)
            compare(spy.count, 1)
            compare(spy.signalArguments[0][0], role)
        }
        spy.clear()
        mouseClick(findChild(ui, "restoreInspectorButton"))
        tryCompare(ui, "inspectorCollapsed", false)
        compare(spy.count, 0)
    }

    QtObject {
        id: manager
        function device(role) { return devices[role] }
        property var devices: [
            {title: "赤道仪", selectedName: "", connected: false},
            {title: "主相机", selectedName: "", connected: false},
            {title: "导星相机", selectedName: "", connected: false},
            {title: "滤镜轮", selectedName: "", connected: false},
            {title: "电调焦", selectedName: "", connected: false}
        ]
    }

    function test_deviceSectionsAndSettingsRouting() {
        const host = createTemporaryObject(desktopWindowComponent, this)
        const ui = host.ui
        const inspector = findChild(ui, "deviceInspector")
        verify(waitForRendering(ui))
        const spy = createTemporaryObject(spyComponent, this, { target: ui, signalName: "settingsRequested" })
        const roles = [1, 3, 4, 0, 2]
        for (const role of roles) {
            const header = findChild(inspector, "deviceSectionToggle" + role)
            verify(header !== null, "All five devices must be directly accessible")
            findChild(inspector, "deviceSettings" + role).clicked()
            compare(spy.signalArguments[spy.count - 1][0], role)
        }
        compare(findChild(inspector, "deviceSection1").expanded, true)
        findChild(inspector, "deviceSectionToggle3").click()
        compare(findChild(inspector, "deviceSection3").expanded, true)
        compare(findChild(inspector, "deviceSection1").expanded, true)
        findChild(inspector, "deviceSectionToggle3").click()
        compare(findChild(inspector, "deviceSection3").expanded, false)
        ui.immersive = true
        compare(inspector.visible, false)
    }

    function test_previewReclaimsSidebarSpace() {
        const ui = createTemporaryObject(desktopComponent, this)
        const image = findChild(ui, "samplePreview")
        verify(waitForRendering(ui))
        verify(image.mapToItem(ui, 0, 0).x < 80, "Preview should start immediately after the navigation rail")
    }

    function test_connectedSectionsKeepSeparateControlsAndGuideNavigation() {
        const original = manager.devices
        manager.devices = original.map(device => Object.assign({}, device, {connected: true}))
        const host = createTemporaryObject(desktopWindowComponent, this)
        const inspector = findChild(host.ui, "deviceInspector")
        verify(waitForRendering(host.ui))
        const main = findChild(inspector, "deviceSectionToggle1")
        main.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(findChild(inspector, "deviceSection1").expanded, false)
        for (const role of [3, 4, 0, 2])
            findChild(inspector, "deviceSectionToggle" + role).click()
        const guideControls = findChild(inspector, "cameraControls2")
        verify(guideControls.visible)
        compare(findChild(guideControls, "fieldEditor").text, "2")
        const pageSpy = createTemporaryObject(spyComponent, this, {target: host.ui, signalName: "workspaceRequested"})
        findChild(inspector, "openGuideWorkspace").click()
        compare(pageSpy.signalArguments[0][0], 1)
        manager.devices = original
        compare(findChild(inspector, "deviceSection4").expanded, true)
        verify(!findChild(inspector, "cameraControls2").visible)
    }

    function test_disconnectedSectionAndCameraEdits() {
        const host = createTemporaryObject(desktopWindowComponent, this)
        const inspector = findChild(host.ui, "deviceInspector")
        verify(waitForRendering(host.ui))
        const connections = createTemporaryObject(spyComponent, this, {target: host.ui, signalName: "connectionsRequested"})
        mouseClick(findChild(inspector, "deviceConnect1"))
        compare(connections.count, 1)
        verify(!findChild(inspector, "cameraControls1").visible)
        const before = manager.devices
        const connected = JSON.parse(JSON.stringify(before))
        connected[1].connected = true
        manager.devices = connected
        tryCompare(findChild(inspector, "cameraControls1"), "visible", true)
        const loop = findChild(inspector, "loopCaptureToggle")
        loop.click()
        compare(inspector.loopCapture, true)
        compare(findChild(inspector, "exposureButton").text, "开始循环曝光")
        findChild(inspector, "deviceSectionToggle1").click()
        findChild(inspector, "deviceSectionToggle1").click()
        manager.devices = before
        tryCompare(findChild(inspector, "cameraControls1"), "visible", false)
    }

    function test_topConnectionEntryOpensManager() {
        const host = createTemporaryObject(topBarComponent, this)
        const bar = host.bar
        verify(waitForRendering(bar))
        const button = findChild(bar, "deviceConnectionsButton")
        const plan = findChild(bar, "planButton")
        verify(button.height >= 36, "Connection entry needs a usable click target")
        verify(plan !== null)
        verify(button.x + button.width <= plan.x)
        verify(button.x > bar.width / 2)
        compare(button.text, "1/5")
        const spy = createTemporaryObject(spyComponent, this, {target: bar, signalName: "deviceConnectionsRequested"})
        mouseClick(button, button.width / 2, button.height / 2)
        compare(spy.count, 1)
    }

    function test_previewToolbar() {
        const preview = createTemporaryObject(previewComponent, this)
        verify(preview !== null)
        compare(preview.crosshairVisible, true)
        findChild(preview, "crosshairButton").clicked()
        compare(preview.crosshairVisible, false)
        findChild(preview, "roiButton").clicked()
        compare(preview.roiVisible, true)
        findChild(preview, "actualSizeButton").clicked()
        compare(preview.fitImage, false)
        compare(preview.zoom, 1)
        findChild(preview, "fitButton").clicked()
        compare(preview.fitImage, true)
    }

    function test_iconButtonLabelsHaveEnoughSpace() {
        const host = createTemporaryObject(topBarComponent, this)
        verify(waitForRendering(host.bar))
        const connection = findChild(host.bar, "deviceConnectionsButton")
        verify(connection.availableWidth >= connection.implicitContentWidth,
               "Device count must fit beside the larger connection icon")
        const desktop = createTemporaryObject(desktopWindowComponent, this)
        verify(waitForRendering(desktop.ui))
        const histogram = findChild(desktop.ui, "histogramToolbarButton")
        compare(histogram.text, "直方图")
        verify(histogram.availableWidth >= histogram.implicitContentWidth,
               "Direct icon sources must reserve space beside the label")
    }


    function test_numericFieldRejectsInvalidValues() {
        const field = createTemporaryObject(fieldComponent, this)
        const editor = findChild(field, "fieldEditor")
        editor.text = "-1"
        compare(editor.acceptableInput, false)
        editor.editingFinished()
        compare(editor.text, "120")
        editor.text = "45"
        compare(editor.acceptableInput, true)
    }

    function test_inspectorCollapsePreservesEditsAndExpandsPreview() {
        const host = createTemporaryObject(desktopWindowComponent, this)
        const ui = host.ui
        const inspector = findChild(ui, "deviceInspector")
        const button = findChild(ui, "collapseInspectorButton")
        verify(button !== null, "Inspector must have a collapse action")
        const image = findChild(ui, "samplePreview")
        verify(waitForRendering(ui))
        tryVerify(() => image.width > 400)
        const before = image.width
        ui.selectedDevice = 3
        button.clicked()
        tryCompare(ui, "inspectorCollapsed", true)
        tryVerify(() => image.width > before + 200)
        findChild(ui, "restoreInspectorButton").clicked()
        tryCompare(ui, "inspectorCollapsed", false)
        compare(ui.selectedDevice, 3)
        tryVerify(() => Math.abs(image.width - before) < 2)
    }

    function test_guideAndHistogramCanCollapseIndependently() {
        const host = createTemporaryObject(desktopWindowComponent, this)
        const ui = host.ui
        const guide = findChild(ui, "guideMonitor")
        const histogram = findChild(ui, "histogramPanel")
        verify(guide !== null, "Guiding monitor must be below the preview")
        verify(histogram !== null, "Histogram must remain available alongside device parameters")
        verify(waitForRendering(ui))
        const image = findChild(ui, "samplePreview")
        const guideButton = findChild(ui, "guideToolbarButton")
        const histogramButton = findChild(ui, "histogramToolbarButton")
        verify(guideButton !== null, "Guiding curve needs an independent toolbar action")
        compare(guide.visible, false)
        compare(histogram.visible, false)
        const fullHeight = image.height
        mouseClick(guideButton)
        tryVerify(() => guide.visible && guide.height >= 120)
        tryVerify(() => image.height < fullHeight - 100)
        compare(guideButton.checked, true)
        compare(histogramButton.checked, false)
        const guideOnlyWidth = guide.width
        mouseClick(histogramButton)
        tryVerify(() => histogram.visible && guide.width < guideOnlyWidth - 200)
        const guidePos = guide.mapToItem(ui, 0, 0)
        const histPos = histogram.mapToItem(ui, 0, 0)
        verify(histPos.x >= guidePos.x + guide.width)
        verify(Math.abs(histPos.y - guidePos.y) < 2, "Both charts share the bottom row")
        verify(Math.abs(histogram.height - guide.height) < 2)
        ui.immersive = true
        tryCompare(histogram, "visible", false)
        ui.immersive = false
        tryCompare(histogram, "visible", true)
        ui.workspace = 2
        tryCompare(guide, "visible", false)
        ui.workspace = 0
        tryCompare(guide, "visible", true)
        histogram.logarithmic = true
        findChild(ui, "collapseInspectorButton").clicked()
        tryCompare(ui, "inspectorCollapsed", true)
        compare(histogram.visible, true, "Histogram no longer belongs to the device inspector")
        findChild(ui, "restoreInspectorButton").clicked()
        tryCompare(ui, "inspectorCollapsed", false)
        mouseClick(guideButton)
        tryCompare(guide, "visible", false)
        tryVerify(() => Math.abs(histogram.width - guideOnlyWidth) < 2)
        compare(histogramButton.checked, true)
        findChild(ui, "histogramToggleButton").clicked()
        tryCompare(histogram, "visible", false)
        tryVerify(() => Math.abs(image.height - fullHeight) < 2)
        compare(histogramButton.checked, false)
        mouseClick(histogramButton)
        compare(histogram.logarithmic, true)
        mouseClick(histogramButton)
        mouseClick(guideButton)
        findChild(ui, "guideToggleButton").clicked()
        tryCompare(guide, "visible", false)
        compare(guide.statusText, "未连接导星相机")
    }

    function test_splittersResizeWithinBounds() {
        const host = createTemporaryObject(desktopWindowComponent, this)
        const ui = host.ui
        const pane = findChild(ui, "inspectorPane")
        const guide = findChild(ui, "guideMonitor")
        verify(waitForRendering(ui))
        const guideButton = findChild(ui, "guideToolbarButton")
        verify(guideButton !== null)
        mouseClick(guideButton)
        tryVerify(() => pane.width >= 280 && guide.height >= 120)
        const widthBefore = pane.width
        const heightBefore = guide.height
        const sideHandle = findChild(ui, "inspectorResizeHandle")
        mouseDrag(sideHandle, 2, sideHandle.height / 2, -45, 0)
        tryVerify(() => pane.width > widthBefore + 20)
        verify(pane.width <= 380)
        const bottomHandle = findChild(ui, "guideResizeHandle")
        mouseDrag(bottomHandle, bottomHandle.width / 2, 2, 0, -35)
        tryVerify(() => guide.height > heightBefore + 15)
        verify(guide.height <= 280)
        const resizedHeight = guide.height
        mouseClick(guideButton)
        tryCompare(guide, "visible", false)
        mouseClick(guideButton)
        tryVerify(() => Math.abs(guide.height - resizedHeight) < 2)
    }

    Component { id: desktopComponent; DesktopWorkspace { width: 1440; height: 832; deviceManager: manager } }
    Component {
        id: topBarComponent
        Window {
            width: 1440; height: 100; visible: true
            property alias bar: barItem
            TopBar { id: barItem; width: 1440; height: 44; deviceSummary: "设备 1/5" }
        }
    }
    Component { id: spyComponent; SignalSpy { } }
    Component {
        id: desktopWindowComponent
        Window {
            width: 1440; height: 900; visible: true
            property alias ui: workspaceItem
            DesktopWorkspace { id: workspaceItem; anchors.fill: parent; deviceManager: manager }
        }
    }
    Component { id: previewComponent; CaptureWorkspace { width: 800; height: 600 } }
    Component { id: fieldComponent; DesktopField { width: 250; editable: true; value: "120"; minimum: 0.001; maximum: 3600 } }
}
