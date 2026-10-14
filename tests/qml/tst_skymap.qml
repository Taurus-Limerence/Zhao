import QtQuick
import QtTest
import "../../qml/pages"

TestCase {
    name: "SkyMapPlanner"
    when: windowShown
    width: 1200; height: 720

    Component {
        id: plannerComponent
        QtObject {
            property var selectedTarget: ({})
            property var savedTargets: []
            property double actualCenterRaHours: -1
            property double actualCenterDecDeg: -91
            property bool followClock: true
            property var lastToggleValue: undefined
            property var observationTime: new Date(2026, 8, 18, 14, 0, 0)
            function nightProfile(raHours, decDeg) {
                return [{offsetHours: 0, altitude: -20, sunAltitude: 30},
                        {offsetHours: 9.5, altitude: 40, sunAltitude: -18},
                        {offsetHours: 12, altitude: 79, sunAltitude: -40},
                        {offsetHours: 16.5, altitude: 45, sunAltitude: -18},
                        {offsetHours: 24, altitude: -20, sunAltitude: 30}]
            }
            function nightWindow() {
                return {startISO: "2026-09-18T21:00:00", endISO: "2026-09-19T04:30:00",
                        startOffsetHours: 9.5, endOffsetHours: 16.5}
            }
            function captureWindow(raHours, decDeg, minAltitude) {
                return {startISO: "2026-09-18T22:00:00", endISO: "2026-09-19T03:00:00", maxAltitude: 79}
            }
            // FOV derives from the instrument, like the real planner does.
            property double fovWidthDeg: 2 * Math.atan(sensorWidthMm / (2 * focalLengthMm)) * 180 / Math.PI
            property double fovHeightDeg: 2 * Math.atan(sensorHeightMm / (2 * focalLengthMm)) * 180 / Math.PI
            property double positionAngle: 0
            property double latitude: 30.65
            property double longitude: 104.07
            property double focalLengthMm: 550
            property double sensorWidthMm: 23.5
            property double sensorHeightMm: 15.7
            property int saveCalls: 0
            property string lastSelectedId: ""
            property string appliedId: ""
            property int resetCalls: 0
            property bool compositionSaved: false
            function searchTargets(query) {
                const q = query.trim()
                if (q === "") return []
                if (q.indexOf("M31") >= 0 || q.indexOf("仙女") >= 0)
                    return [{id: "M31", name: "仙女座星系", constellation: "仙女座", type: "星系",
                             raHours: 0.7123, decDeg: 41.269, magnitude: 3.4, sizeArcmin: 190}]
                return []
            }
            function horizontalCoordinates(id) {
                return {altitude: 34.2, azimuth: 86.5, aboveHorizon: true,
                        maxAltitude: 78.3, alwaysUp: false, neverUp: false, lstHours: 22.5}
            }
            function horizontalCoordinatesOfSelected() {
                if (!selectedTarget.id) return {}
                return {altitude: 34.2, azimuth: 86.5, aboveHorizon: true,
                        maxAltitude: 78.3, alwaysUp: false, neverUp: false, lstHours: 22.5}
            }
            function selectCoordinate(raHours, decDeg) {
                lastSelectedId = "coord"
                selectedTarget = {id: "自定义坐标", name: "坐标目标", constellation: "—", type: "坐标",
                                  raHours: raHours, decDeg: decDeg}
            }
            function selectTarget(id) {
                lastSelectedId = id
                const hits = searchTargets(id)
                selectedTarget = hits.length > 0 ? hits[0] : {}
            }
            function saveSelectedTarget() {
                saveCalls += 1
                compositionSaved = true
                savedTargets = [{id: "M31", name: "仙女座星系"}]
            }
            function applySavedTarget(id) { appliedId = id }
            function resetActualCenter() { resetCalls += 1 }
            function setActualCenter(ra, dec) { actualCenterRaHours=ra; actualCenterDecDeg=dec }
            function footprintCorners(ra, dec, width, height, angle) { return [[10,40],[11,40],[11,41],[10,41]] }
            function removeSavedTarget(id) { savedTargets = [] }
        }
    }

    Component {
        id: workspaceComponent
        Window {
            width: 1200; height: 720; visible: true
            property alias workspace: plannerPage
            SkyMapWorkspace {
                id: plannerPage
                anchors.fill: parent
                planner: null
            }
        }
    }
    // Narrow host exercises the folded toolbar path.
    Component {
        id: narrowComponent
        Window {
            width: 560; height: 600; visible: true
            property alias workspace: narrowPage
            SkyMapWorkspace {
                id: narrowPage
                anchors.fill: parent
                planner: null
            }
        }
    }

    function test_searchPopulatesResults() {
        const planner = createTemporaryObject(plannerComponent, this)
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner
        const input = findChild(host.workspace, "targetSearchInput")
        verify(input !== null)
        input.text = "M31"
        const results = findChild(host.workspace, "targetSearchResults")
        tryCompare(results, "count", 1)
        // Mouse selection through the popup row (keyboard Up/Down/Return is
        // wired in the field; offscreen focus quirks keep this on the click path).
        const row = results.itemAtIndex(0)
        verify(row !== null)
        row.clicked()
        compare(planner.lastSelectedId, "M31")
        compare(findChild(host.workspace, "targetSearchResults").count, 0)
        input.text = "不存在的目标xyz"
        tryCompare(host.workspace, "searchEmpty", true)
        input.text = ""
    }
    function test_searchAlignmentAndCompactSiteFields() {
        const planner = createTemporaryObject(plannerComponent, this)
        const host = createTemporaryObject(workspaceComponent, this)
        host.width = 1920
        host.workspace.planner = planner
        const input = findChild(host.workspace, "targetSearchInput")
        input.text = "M31"
        const results = findChild(host.workspace, "targetSearchResults")
        tryCompare(results, "count", 1)
        wait(50)
        const p = input.mapToItem(host.contentItem, 0, 0)
        const r = results.mapToItem(host.contentItem, 0, 0)
        verify(Math.abs(r.x - p.x - 1) < 2, "Search results must align with the input")
        compare(results.width, input.width - 2)
        verify(findChild(host.workspace, "targetCoordinates") !== null)
    }

    Component {
        id: mountManagerComponent
        QtObject {
            signal devicesChanged()
            property var state: ({connected: true, metrics: {skyRa: 3.2, skyDec: 21.5}})
            property var cameraState: ({connected:false, metrics:{}})
            property var sent: []
            function device(role) { return role === 1 ? cameraState : state }
            function slewMountToJ2000(ra, dec) { sent = [ra, dec]; return true }
            function mountAbortSlew() { return true }
        }
    }
    Component {
        id: photoSolverComponent
        QtObject {
            property bool busy: false
            property bool currentAvailable: true
            property string currentLabel: "主相机 RAW"
            property string status: "解析成功 · J2000"
            property string executable: "D:/ASTAP/astap_cli.exe"
            property var result: ({})
        }
    }
    Component {
        id: skyViewDouble
        Item {
            property string state: "loading"
            property string statusMessage: ""
            property bool inspectingPhoto: false
            property bool inspectingMount: false
            property var visits: []
            signal centerChanged(real raDeg, real decDeg)
            function inspectMount(ra, dec) { inspectingMount=true; visits=visits.concat([[ra,dec]]) }
            function inspectPhoto(result) { inspectingPhoto=true; inspectingMount=false; visits=visits.concat([[result.raHours,result.decDeg]]) }
            function setMountPosition(valid, ra, dec) {}
            function setSolvedFootprint(corners) {}
            function setFrameVisible(value) {}
            function setGridVisible(value) {}
            function updateFrame(corners) {}
            function gotoTarget(ra, dec, corners, fov) { inspectingMount=false; visits=visits.concat([[ra,dec]]) }
        }
    }
    function installSkyDouble(page) {
        const loader=findChild(page,"aladinLoader")
        loader.source=""
        loader.sourceComponent=skyViewDouble
        page.aladinEnabled=true
        return loader.item
    }
    function test_initialMountViewOnceAndManualReturnPreservesPlan() {
        const planner=createTemporaryObject(plannerComponent,this)
        const manager=createTemporaryObject(mountManagerComponent,this)
        const host=createTemporaryObject(workspaceComponent,this)
        host.workspace.planner=planner
        host.workspace.deviceManager=manager
        const view=installSkyDouble(host.workspace)
        view.state="ready"
        compare(view.visits.length,1)
        compare(view.visits[0][0],3.2)
        compare(view.visits[0][1],21.5)
        manager.state={connected:true,metrics:{skyRa:4,skyDec:22}}
        manager.devicesChanged()
        host.workspace.visible=false; host.workspace.visible=true
        compare(view.visits.length,1)
        planner.selectTarget("M31")
        planner.actualCenterRaHours=0.72; planner.actualCenterDecDeg=41.3
        const button=findChild(host.workspace,"returnToMountButton")
        verify(button.enabled)
        button.clicked()
        compare(view.visits[view.visits.length-1][0],4)
        view.centerChanged(60,22)
        compare(planner.actualCenterRaHours,0.72)
        compare(planner.actualCenterDecDeg,41.3)
        compare(planner.selectedTarget.id,"M31")
        compare(manager.sent.length,0)
        manager.state={connected:false,metrics:{}}
        manager.devicesChanged()
        verify(!button.enabled)
    }
    function test_initialTargetWinsAndLateConnectionDoesNotJump() {
        const planner=createTemporaryObject(plannerComponent,this)
        planner.selectTarget("M31")
        const manager=createTemporaryObject(mountManagerComponent,this)
        const host=createTemporaryObject(workspaceComponent,this)
        host.workspace.planner=planner; host.workspace.deviceManager=manager
        const view=installSkyDouble(host.workspace)
        view.state="ready"
        compare(view.visits.length,1)
        compare(view.visits[0][0],0.7123)
        verify(!view.inspectingMount)
        const other=createTemporaryObject(workspaceComponent,this)
        manager.state={connected:false,metrics:{}}
        other.workspace.deviceManager=manager
        const otherView=installSkyDouble(other.workspace)
        otherView.state="ready"
        manager.state={connected:true,metrics:{skyRa:4,skyDec:22}}
        manager.devicesChanged()
        compare(otherView.visits.length,0)
        manager.state={connected:true,metrics:{skyRa:25,skyDec:91}}
        manager.devicesChanged()
        verify(!findChild(other.workspace,"returnToMountButton").enabled)
    }
    function test_mainPhotoNavigationWinsOverSelectedTarget() {
        const planner=createTemporaryObject(plannerComponent,this)
        planner.selectTarget("M31")
        const solver=createTemporaryObject(photoSolverComponent,this)
        solver.result={raHours:20.5,decDeg:45,widthDeg:2,heightDeg:1,positionAngle:0,
            source:"new RAW",solvedAt:"2026-10-13",corners:[[1,1],[2,1],[2,2],[1,2]]}
        const host=createTemporaryObject(workspaceComponent,this)
        host.workspace.planner=planner; host.workspace.plateSolver=solver
        host.workspace.inspectPhotoPending=true
        const view=installSkyDouble(host.workspace)
        view.state="ready"
        compare(view.visits.length,1)
        compare(view.visits[0][0],20.5)
        verify(view.inspectingPhoto)
        compare(planner.selectedTarget.id,"M31")
    }
    function test_photoSolutionDoesNotChangeTargetOrMoveMount() {
        const planner=createTemporaryObject(plannerComponent,this)
        planner.selectTarget("M31")
        planner.actualCenterRaHours=0.72; planner.actualCenterDecDeg=41.3
        const manager=createTemporaryObject(mountManagerComponent,this)
        const solver=createTemporaryObject(photoSolverComponent,this)
        const host=createTemporaryObject(workspaceComponent,this)
        host.workspace.planner=planner
        host.workspace.deviceManager=manager
        host.workspace.plateSolver=solver
        solver.result={raHours:0.713,decDeg:41.27,widthDeg:1.2,heightDeg:0.7,positionAngle:15,
            source:"M31_LIGHT_0001.fits",solvedAt:"2026-10-13 21:00:00",pixelScale:1.7,
            corners:[[10,41],[11,41],[11,42],[10,42]]}
        compare(host.workspace.solvedCorners.length,4)
        compare(planner.actualCenterRaHours,0.72)
        compare(planner.actualCenterDecDeg,41.3)
        compare(planner.selectedTarget.id,"M31")
        compare(manager.sent.length,0)
        const toggle=findChild(host.workspace,"toggleSolvedFootprint")
        toggle.checked=false; toggle.clicked()
        compare(manager.sent.length,0)
        compare(planner.sensorWidthMm,23.5)
    }
    function test_cameraDimensionsOnlyAppliedOnRequest() {
        const planner = createTemporaryObject(plannerComponent, this)
        const manager = createTemporaryObject(mountManagerComponent, this)
        manager.cameraState = {connected:true, metrics:{sensorWidthMm:7.68, sensorHeightMm:4.32}}
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner
        host.workspace.deviceManager = manager
        compare(planner.sensorWidthMm, 23.5)
        const button = findChild(host.workspace, "readCameraDimensions")
        verify(button !== null)
        button.clicked()
        compare(planner.sensorWidthMm, 7.68)
        compare(planner.sensorHeightMm, 4.32)
        compare(planner.focalLengthMm, 550)
        planner.sensorWidthMm = 10
        manager.devicesChanged()
        compare(planner.sensorWidthMm, 10)
        manager.cameraState = {connected:false, metrics:{}}
        manager.devicesChanged()
        verify(!button.enabled)
    }
    function test_stationIsCollapsedAndTargetDoesNotFollowFraming() {
        const planner = createTemporaryObject(plannerComponent, this)
        planner.selectTarget("M31")
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner
        const target = findChild(host.workspace, "targetCoordinates")
        const before = target.text
        planner.actualCenterRaHours = 4
        planner.actualCenterDecDeg = 20
        compare(target.text, before)
        verify(findChild(host.workspace, "framingCoordinates").text.indexOf("04h 00m 00s") >= 0)
        verify(!findChild(host.workspace, "siteLongitude").visible)
        findChild(host.workspace, "siteExpandButton").clicked()
        verify(findChild(host.workspace, "siteLongitude").visible)
    }
    function test_mountUsesFramingCenterAndDoesNotMovePlanner() {
        const planner = createTemporaryObject(plannerComponent, this)
        planner.selectTarget("M31")
        planner.actualCenterRaHours = 1.25
        planner.actualCenterDecDeg = 40.5
        const manager = createTemporaryObject(mountManagerComponent, this)
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner
        host.workspace.deviceManager = manager
        compare(host.workspace.mountPointValid, true)
        findChild(host.workspace, "gotoMountButton").clicked()
        compare(manager.sent.length, 0) // Confirmation is required before dispatch.
        findChild(host.workspace, "confirmMountGotoButton").clicked()
        compare(manager.sent[0], 1.25)
        compare(manager.sent[1], 40.5)
        manager.state = {connected:true, metrics:{skyRa:4, skyDec:22, gotoState:"Slewing"}}
        manager.devicesChanged()
        compare(findChild(host.workspace, "gotoMountButton").enabled, false)
        compare(planner.actualCenterRaHours, 1.25)
        compare(planner.actualCenterDecDeg, 40.5)
        manager.state = {connected:false, metrics:{}}
        manager.devicesChanged()
        compare(host.workspace.mountPointValid, false)
    }

    function test_displayTogglesAndAngleInput() {
        const planner = createTemporaryObject(plannerComponent, this)
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner
        verify(waitForRendering(host.workspace))
        const frameToggle = findChild(host.workspace, "frameToggleButton")
        const gridToggle = findChild(host.workspace, "gridToggleButton")
        verify(frameToggle !== null && gridToggle !== null)
        compare(host.workspace.frameVisible, true)
        frameToggle.clicked()
        compare(host.workspace.frameVisible, false)
        compare(findChild(host.workspace, "fovBox").opacity, 0, "Fallback frame follows the toggle")
        gridToggle.clicked()
        compare(host.workspace.gridVisible, true)

        const angleField = findChild(host.workspace, "positionAngleField")
        verify(angleField !== null, "Position angle accepts direct input")
        const editor = findChild(angleField, "fieldEditor")
        verify(editor !== null)
        editor.text = "45"
        editor.editingFinished()
        compare(planner.positionAngle, 45)

        // Minute carry: 0.7123h = 42.738m must render 42m, never 60m overflow.
        compare(host.workspace.raText(0.7123), "00h 42m 44s")
        compare(host.workspace.raText(23.999), "23h 59m 56s")
        compare(host.workspace.raText(23.999999), "00h 00m 00s")
        compare(host.workspace.decText(-5.5), "-05° 30′ 00″")
        compare(host.workspace.decText(41.269), "+41° 16′ 08″")
        compare(host.workspace.decText(12.999999), "+13° 00′ 00″")
        compare(host.workspace.decText(undefined), "—")
    }

    function test_selectTargetShowsInfoAndFov() {
        const planner = createTemporaryObject(plannerComponent, this)
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner
        verify(waitForRendering(host.workspace))
        const title = findChild(host.workspace, "plannerTargetTitle")
        // Empty state: the title row hides and a single hint shows instead.
        verify(!title.visible)
        verify(findChild(host.workspace, "skyEmptyTargetHint").visible)
        // Without a target the panel must not advertise observability.
        const summary = findChild(host.workspace, "visibilitySummary")
        verify(summary !== null)
        verify(!summary.visible, "Empty state keeps visibility wording hidden")

        host.workspace.selectFrom({modelData: {id: "M31"}})
        compare(planner.lastSelectedId, "M31")
        tryCompare(title, "text", "M31 · 仙女座星系")
        tryCompare(summary, "visible", true)
        const fov = findChild(host.workspace, "fovBox")
        verify(fov !== null)
        compare(fov.opacity, 1)
    }

    function test_fovBoxFollowsInstrument() {
        const planner = createTemporaryObject(plannerComponent, this)
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner
        planner.positionAngle = 45
        const fov = findChild(host.workspace, "fovBox")
        tryCompare(fov, "rotation", 45)
        const slider = findChild(host.workspace, "positionAngleSlider")
        verify(slider !== null)
        // The planned extent is the capture frame, never a live pointing.
        verify(findChild(host.workspace, "skyCanvas") !== null)
    }

    function test_saveTargetRoutesToPlanner() {
        const planner = createTemporaryObject(plannerComponent, this)
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner
        const button = findChild(host.workspace, "saveTargetButton")
        verify(button !== null)
        verify(!button.enabled, "Save needs a selected target")
        host.workspace.selectFrom({modelData: {id: "M31"}})
        verify(button.enabled)
        compare(button.text, "设为拍摄目标")
        button.clicked()
        tryCompare(planner, "saveCalls", 1)
        // Once the composition is stored the same target offers an update.
        tryCompare(button, "text", "更新当前构图")
    }

    function test_savedRowRestoresComposition() {
        const planner = createTemporaryObject(plannerComponent, this)
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner
        host.workspace.selectFrom({modelData: {id: "M31"}})
        const button = findChild(host.workspace, "saveTargetButton")
        button.clicked()
        const row = findChild(host.workspace, "applyTargetM31")
        verify(row !== null, "Saved composition must be listed for restore")
        row.clicked()
        compare(planner.appliedId, "M31")
    }

    function test_fitButtonTiesZoomToFieldOfView() {
        const planner = createTemporaryObject(plannerComponent, this)
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner
        host.workspace.selectFrom({modelData: {id: "M31"}})
        verify(waitForRendering(host.workspace))
        verify(findChild(host.workspace, "skyCanvas").height > 100, "Canvas must be laid out before fitting")
        const fov = findChild(host.workspace, "fovBox")
        const beforeHeight = fov.height
        const beforeFov = host.workspace.fovHeightDeg
        // Doubling the focal length halves the field (atan geometry allows a
        // small nonlinearity); the frame must shrink on a fixed sky scale
        // instead of being auto-cancelled.
        planner.focalLengthMm = planner.focalLengthMm * 2
        const afterFov = host.workspace.fovHeightDeg
        verify(afterFov < beforeFov && afterFov > beforeFov * 0.45)
        verify(findChild(host.workspace, "fovBox").height < beforeHeight)
        const fit = findChild(host.workspace, "fitFovButton")
        verify(fit !== null)
        fit.clicked()
        verify(host.workspace.zoom > 1, "Fitting a small field zooms the sky in")
    }

    function test_coordinateParsingAndSelection() {
        const planner = createTemporaryObject(plannerComponent, this)
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner

        // Sexagesimal with mixed units, negative declination.
        const sexa = host.workspace.parseCoordinate("0h42m +41°16'")
        verify(sexa !== null)
        verify(Math.abs(sexa.raHours - (0 + 42 / 60)) < 1e-6)
        verify(Math.abs(sexa.decDeg - (41 + 16 / 60)) < 1e-6)
        const negative = host.workspace.parseCoordinate("5h 34m, -5° 27'")
        verify(negative !== null)
        verify(negative.decDeg < 0)
        // Decimal hours/degrees pair.
        const decimal = host.workspace.parseCoordinate("0.712, 41.269")
        verify(decimal !== null)
        compare(decimal.raHours, 0.712)
        // Garbage stays null.
        compare(host.workspace.parseCoordinate("不是坐标"), null)
        compare(host.workspace.parseCoordinate("99 41"), null)

        // Enter on a coordinate string creates the target through the planner.
        const input = findChild(host.workspace, "targetSearchInput")
        input.text = "0h42m +41°16'"
        const results = findChild(host.workspace, "targetSearchResults")
        tryCompare(results, "count", 0, 1000)
        host.workspace.acceptSearch()
        compare(planner.lastSelectedId, "coord")
        compare(planner.selectedTarget.id, "自定义坐标")
        compare(planner.selectedTarget.raHours, 0 + 42 / 60)
    }

    function test_recenterResetsDraggedCenter() {
        const planner = createTemporaryObject(plannerComponent, this)
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner
        const button = findChild(host.workspace, "recenterButton")
        verify(button !== null)
        // 回到目标 goes back to the searched-for object: the button must
        // reset the dragged center rather than re-apply it.
        planner.resetCalls = 0
        button.clicked()
        compare(planner.resetCalls, 1)
    }

    function test_dualModeSwitchHidesFramingTools() {
        const planner = createTemporaryObject(plannerComponent, this)
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner
        verify(waitForRendering(host.workspace))
        const framingButton = findChild(host.workspace, "modeFramingButton")
        const browseButton = findChild(host.workspace, "modeBrowseButton")
        verify(framingButton !== null && browseButton !== null)

        compare(host.workspace.skyMode, "framing")
        compare(findChild(host.workspace, "fovBox").opacity, 0, "No target, no frame")
        host.workspace.selectFrom({modelData: {id: "M31"}})
        compare(findChild(host.workspace, "fovBox").opacity, 1)

        // Browsing mode: framing geometry is a distraction, hide it.
        browseButton.clicked()
        compare(host.workspace.skyMode, "browse")
        compare(host.workspace.browsing, true)
        compare(findChild(host.workspace, "fovBox").opacity, 0, "FOV box hidden while browsing")
        verify(!findChild(host.workspace, "frameToggleButton").visible)
        verify(!findChild(host.workspace, "positionAngleSlider").visible)
        verify(findChild(host.workspace, "viewRotationSlider") !== null, "Browse mode offers sky view rotation")

        // Back to framing: the capture frame returns.
        framingButton.clicked()
        compare(host.workspace.skyMode, "framing")
        compare(findChild(host.workspace, "fovBox").opacity, 1)
    }

    function test_observationTimeControlsAndCurve() {
        const planner = createTemporaryObject(plannerComponent, this)
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner
        host.workspace.selectFrom({modelData: {id: "M31"}})
        verify(waitForRendering(host.workspace))

        const curve = findChild(host.workspace, "altitudeCurve")
        verify(curve !== null, "Night altitude curve must exist")
        verify(findChild(host.workspace, "captureWindowText") !== null)
        verify(findChild(host.workspace, "nightWindowText") !== null)

        // Following the clock: manual field hidden.
        compare(planner.followClock, true)
        const row = findChild(host.workspace, "observationTimeRow")
        verify(row !== null)
        // Always visible, gated by enabled state instead of visibility
        // (cross-object visible bindings proved unreliable to refresh).
        compare(row.opacity < 1, true, "Dimmed while following the clock")
        const liveButton = findChild(host.workspace, "followClockOnButton")
        const manualButton = findChild(host.workspace, "followClockOffButton")
        verify(liveButton !== null && manualButton !== null)
        verify(liveButton.checked, "Live mode on by default")
        // Manual mode pins the observation time.
        manualButton.clicked()
        compare(planner.followClock, false, "Manual mode unpins from the clock")
        verify(findChild(host.workspace, "timeNowButton").visible)
        liveButton.clicked()
        compare(planner.followClock, true, "Live mode follows the clock")
    }

    function test_targetPanelCollapsesAndSimplifiesEmptyState() {
        const planner = createTemporaryObject(plannerComponent, this)
        const host = createTemporaryObject(workspaceComponent, this)
        host.workspace.planner = planner
        verify(waitForRendering(host.workspace))
        const panel = findChild(host.workspace, "skyTargetPanel")
        verify(panel !== null)
        const collapseButton = findChild(host.workspace, "targetPanelCollapseButton")
        verify(collapseButton !== null)

        // Empty state: one hint line, no detail rows.
        const hint = findChild(host.workspace, "skyEmptyTargetHint")
        verify(hint !== null)
        verify(hint.visible, "Hint shows when no target is selected")
        const title = findChild(host.workspace, "plannerTargetTitle")
        verify(!title.visible)

        // Collapse: the panel rail shrinks and the hint area hides.
        const expandedWidth = panel.width
        collapseButton.clicked()
        tryCompare(panel, "width", 28)
        verify(host.workspace.targetPanelCollapsed === true)
        // The capture page's inspector state is separate memory; nothing
        // here may touch it (implicitly verified by the Desktop tests).

        // Select a target after expanding: hint gone, details visible.
        const expandButton = findChild(host.workspace, "skyExpandButton") || null
        // Re-expand via the property (rail button uses glyph only).
        host.workspace.targetPanelCollapsed = false
        tryCompare(panel, "width", expandedWidth)
        host.workspace.selectFrom({modelData: {id: "M31"}})
        tryCompare(title, "visible", true)
        verify(!hint.visible)
        verify(expandedWidth > 200)
    }

    function test_normalizeCornersHandlesBridgeFlattening() {
        const host = createTemporaryObject(workspaceComponent, this)
        const nested = host.workspace.normalizeCorners([[1, 2], [3, 4], [5, 6], [7, 8]])
        compare(nested.length, 4, "Nested pairs pass through")
        const flat = host.workspace.normalizeCorners([1, 2, 3, 4, 5, 6, 7, 8])
        compare(flat.length, 4, "Flattened bridge payload is regrouped")
        compare(flat[0][0], 1)
        compare(flat[3][1], 8)
        compare(host.workspace.normalizeCorners([]).length, 0)
        compare(host.workspace.normalizeCorners([1, 2, 3]).length, 0, "Malformed payload stays empty")
    }
}
