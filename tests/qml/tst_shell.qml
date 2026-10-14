import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/shell"
import "../../qml/pages" as Pages

TestCase {
    name: "AsterShell"
    when: windowShown
    width: 400
    height: 300

    function test_toolbarsKeepStableControlCounts() {
        const left = createTemporaryObject(leftComponent, this)
        const right = createTemporaryObject(rightComponent, this)
        verify(left !== null)
        verify(right !== null)
        compare(left.toolCount, 4)
        compare(right.toolCount, 5)
        compare(left.implicitWidth, 68)
        compare(left.implicitHeight, 174)
        compare(right.implicitWidth, 60)
        compare(right.implicitHeight, 218)
    }

    function test_topBarUsesEdgeGeometry() {
        const bar = createTemporaryObject(topBarComponent, this)
        verify(bar !== null)
        compare(bar.implicitHeight, 44)
        compare(bar.radius, 0)
    }

    function test_topBarOwnsCustomCaptionGeometry() {
        const bar = createTemporaryObject(topBarComponent, this, { width: 1440 })
        verify(bar !== null)

        const mark = findChild(bar, "brandMark")
        const brand = findChild(bar, "brandText")
        const controls = findChild(bar, "windowControls")
        verify(mark !== null)
        verify(brand !== null)
        verify(controls !== null)
        compare(mark.width, 24)
        compare(mark.height, 24)
        compare(mark.mapToItem(bar, 0, 0).x, 12)
        compare(brand.text, "STELLAMORA")
        compare(controls.width, 138)
        compare(controls.height, 44)
    }

    function test_windowControlsForwardCaptionActions() {
        const host = createTemporaryObject(windowControlsWindowComponent, this)
        verify(host !== null)
        tryVerify(() => host.visible)
        verify(waitForRendering(host.controls))
        const controls = host.controls

        const minimize = findChild(controls, "minimizeButton")
        const maximize = findChild(controls, "maximizeButton")
        const close = findChild(controls, "closeButton")
        verify(minimize !== null)
        verify(maximize !== null)
        verify(close !== null)
        compare(minimize.width, 46)
        compare(maximize.width, 46)
        compare(close.width, 46)

        const minimizeSpy = createTemporaryObject(signalSpyComponent, this, {
            target: controls,
            signalName: "minimizeRequested"
        })
        const maximizeSpy = createTemporaryObject(signalSpyComponent, this, {
            target: controls,
            signalName: "toggleMaximizeRequested"
        })
        const closeSpy = createTemporaryObject(signalSpyComponent, this, {
            target: controls,
            signalName: "closeRequested"
        })
        verify(minimizeSpy !== null)
        verify(maximizeSpy !== null)
        verify(closeSpy !== null)

        minimize.clicked()
        maximize.clicked()
        close.clicked()
        compare(minimizeSpy.count, 1)
        compare(maximizeSpy.count, 1)
        compare(closeSpy.count, 1)

        compare(maximize.kind, "maximize")
        controls.maximized = true
        compare(maximize.kind, "restore")
    }

    function test_customCaptionRestoresAfterMaximize() {
        const host = createTemporaryObject(customCaptionWindowComponent, this)
        verify(host !== null)
        tryVerify(() => host.visible)
        verify(waitForRendering(host.topBar))

        const maximize = findChild(host.topBar, "maximizeButton")
        verify(maximize !== null)

        mouseClick(maximize, maximize.width / 2, maximize.height / 2)
        tryCompare(host, "visibility", Window.Maximized)
        compare(maximize.kind, "restore")

        mouseClick(maximize, maximize.width / 2, maximize.height / 2)
        tryCompare(host, "visibility", Window.Windowed)
        compare(maximize.kind, "maximize")
    }

    function test_resizeHandlesExposeEightRegions() {
        const handles = createTemporaryObject(resizeHandlesComponent, this, {
            width: 400,
            height: 300
        })
        verify(handles !== null)
        compare(handles.handleCount, 8)
    }

    function test_captureHudCanHideForImmersivePreview() {
        const workspace = createTemporaryObject(captureWorkspaceComponent, this)
        verify(workspace !== null)
        const hud = findChild(workspace, "captureHud")
        verify(hud !== null)
        compare(workspace.hudVisible, true)

        workspace.hudVisible = false

        tryCompare(hud, "visible", false, 300)
        compare(hud.visible, false)
    }

    function test_capturePreviewEmitsTapFromCentralImage() {
        const host = createTemporaryObject(captureWorkspaceWindowComponent, this)
        verify(host !== null)
        tryVerify(() => host.visible)
        verify(waitForRendering(host.workspace))

        const tapArea = findChild(host.workspace, "samplePreview")
        verify(tapArea !== null)
        const spy = createTemporaryObject(signalSpyComponent, this, {
            target: host.workspace,
            signalName: "previewTapped"
        })
        verify(spy !== null)

        mouseDoubleClickSequence(tapArea, tapArea.width / 2, tapArea.height / 2)

        compare(spy.count, 1)
    }

    Component { id: leftComponent; LeftAnalysisBar { } }
    Component { id: rightComponent; RightCaptureBar { } }
    Component { id: topBarComponent; TopBar { } }
    Component { id: resizeHandlesComponent; WindowResizeHandles { } }
    Component {
        id: windowControlsWindowComponent
        Window {
            width: 138
            height: 44
            visible: true
            property alias controls: controlsItem
            WindowControls { id: controlsItem }
        }
    }
    Component {
        id: customCaptionWindowComponent
        Window {
            id: captionWindow
            width: 1280
            height: 720
            visible: true
            flags: Qt.Window | Qt.FramelessWindowHint
            property alias topBar: topBarItem

            TopBar {
                id: topBarItem
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 44
                windowMaximized: captionWindow.visibility === Window.Maximized
                onToggleMaximizeRequested: {
                    if (captionWindow.visibility === Window.Maximized)
                        captionWindow.showNormal()
                    else
                        captionWindow.showMaximized()
                }
            }

            WindowResizeHandles {
                anchors.fill: parent
                hostWindow: captionWindow
                enabled: captionWindow.visibility === Window.Windowed
                z: 100
            }
        }
    }
    Component {
        id: captureWorkspaceComponent
        Pages.CaptureWorkspace { width: 800; height: 600 }
    }
    Component {
        id: captureWorkspaceWindowComponent
        Window {
            width: 800
            height: 600
            visible: true
            property alias workspace: workspaceItem
            Pages.CaptureWorkspace { id: workspaceItem; anchors.fill: parent }
        }
    }
    Component { id: signalSpyComponent; SignalSpy { } }
}
