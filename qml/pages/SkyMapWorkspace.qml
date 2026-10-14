import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"
import "../panels"
import "../theme"

// Observing planner workspace: pick a target, check its altitude for the
// configured site, and frame it with the camera field of view. The FOV box is
// the planned capture extent — never a live telescope pointing.
Rectangle {
    id: root
    property var planner: null
    property var deviceManager: null
    property var plateSolver: null
    property var centeringController: null
    property bool inspectPhotoPending: false
    signal photoInspected()
    function inspectPendingPhoto() {
        const view = aladinLoader.item
        if (!inspectPhotoPending || !view || view.state !== "ready" || !plateSolver || !plateSolver.result.corners) return
        view.inspectPhoto(plateSolver.result)
        photoInspected()
    }
    onInspectPhotoPendingChanged: Qt.callLater(inspectPendingPhoto)
    readonly property var solvedCorners: plateSolver && plateSolver.result.corners ? plateSolver.result.corners : []
    function syncSolvedFootprint() {
        if (aladinViewReady) aladinLoader.item.setSolvedFootprint(photoSolve.footprintVisible ? solvedCorners : [])
    }
    onSolvedCornersChanged: syncSolvedFootprint()
    property var mountDevice: ({})
    property var cameraDevice: ({})
    property bool siteExpanded: false
    property string lastSiteMismatch: ""
    readonly property var cameraMetrics: cameraDevice.metrics || ({})
    readonly property bool cameraDimensionsAvailable: cameraDevice.connected === true
        && Number(cameraMetrics.sensorWidthMm) > 0 && Number(cameraMetrics.sensorWidthMm) <= 100
        && Number(cameraMetrics.sensorHeightMm) > 0 && Number(cameraMetrics.sensorHeightMm) <= 100
    function readCameraDimensions() {
        if (!planner || !cameraDimensionsAvailable) return
        planner.sensorWidthMm = Number(cameraMetrics.sensorWidthMm)
        planner.sensorHeightMm = Number(cameraMetrics.sensorHeightMm)
        noticeRequested("已读取主相机全幅尺寸；焦距与旋转角仍需手动设置。此框为规划视场，不是解析结果。")
    }
    function siteText(value, longitude) {
        if (typeof value !== "number" || !isFinite(value)) return "—"
        const seconds = Math.round(Math.abs(value) * 3600)
        return (longitude ? (value < 0 ? "W" : "E") : (value < 0 ? "S" : "N"))
            + Math.floor(seconds / 3600) + "°" + String(Math.floor(seconds / 60) % 60).padStart(2, "0")
            + "′" + String(seconds % 60).padStart(2, "0") + "″"
    }
    function checkSite() {
        if (!planner || !mountDevice.connected) { lastSiteMismatch = ""; return }
        const lon = mountMetrics.siteLongitude, lat = mountMetrics.siteLatitude
        if (typeof lon !== "number" || typeof lat !== "number" || !isFinite(lon) || !isFinite(lat)) return
        if (Math.abs(lon-planner.longitude) < 0.01 && Math.abs(lat-planner.latitude) < 0.01) { lastSiteMismatch = ""; return }
        const key = [lon.toFixed(2), lat.toFixed(2), planner.longitude.toFixed(2), planner.latitude.toFixed(2)].join(",")
        if (key !== lastSiteMismatch) {
            lastSiteMismatch = key
            noticeRequested("星图观测站与赤道仪站点不一致，请核对位置；不会自动覆盖任何一方。")
        }
    }
    Connections {
        target: root.planner
        ignoreUnknownSignals: true
        function onSiteChanged() { root.checkSite() }
    }
    readonly property var mountMetrics: mountDevice.metrics || ({})
    readonly property bool mountPointValid: mountDevice.connected === true
        && mountMetrics.skyRa !== undefined && mountMetrics.skyRa !== null
        && mountMetrics.skyDec !== undefined && mountMetrics.skyDec !== null
        && isFinite(Number(mountMetrics.skyRa)) && isFinite(Number(mountMetrics.skyDec))
        && Number(mountMetrics.skyRa) >= 0 && Number(mountMetrics.skyRa) < 24
        && Number(mountMetrics.skyDec) >= -90 && Number(mountMetrics.skyDec) <= 90
    property bool initialViewResolved: false
    function returnToMount() {
        const view = aladinLoader.item
        if (!view || view.state !== "ready" || !mountPointValid) return
        view.inspectMount(Number(mountMetrics.skyRa), Number(mountMetrics.skyDec))
    }
    readonly property bool mountGotoBusy: mountMetrics.gotoState === "Pending" || mountMetrics.gotoState === "Slewing"
        || mountMetrics.gotoState === "StopFailed"
    function refreshMount() {
        mountDevice = deviceManager ? deviceManager.device(0) : ({})
        cameraDevice = deviceManager ? deviceManager.device(1) : ({})
        checkSite()
        if (aladinViewReady) aladinLoader.item.setMountPosition(mountPointValid,
            Number(mountMetrics.skyRa || 0), Number(mountMetrics.skyDec || 0))
    }
    onDeviceManagerChanged: refreshMount()
    Connections {
        target: root.deviceManager
        function onDevicesChanged() { root.refreshMount() }
    }
    Dialog {
        id: gotoConfirm
        objectName: "gotoMountConfirm"
        property real ra: 0
        property real dec: 0
        width: Math.min(440, root.width - 24)
        anchors.centerIn: parent
        modal: true
        title: "确认指向目标"
        background: Rectangle { color: AsterTheme.surface; border.color: AsterTheme.stroke; radius: 4 }
        header: Text {
            text: gotoConfirm.title
            color: AsterTheme.textPrimary
            font.pixelSize: 12
            font.bold: true
            padding: 12
        }
        contentItem: Text {
            wrapMode: Text.Wrap
            color: AsterTheme.textPrimary
            font.pixelSize: 12
            text: "将移动赤道仪至 J2000：\nRA " + gotoConfirm.ra.toFixed(6) + "h  Dec " + gotoConfirm.dec.toFixed(5)
                + "°\n请确认站点、电脑时间正确，设备周围无障碍。\n将开启跟踪并指向；不会自动解除停放。"
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            DesktopButton { text: "取消"; onClicked: gotoConfirm.close() }
            DesktopButton {
                objectName: "confirmMountGotoButton"
                text: "确认指向"; primary: true
                onClicked: { gotoConfirm.close(); root.deviceManager.slewMountToJ2000(gotoConfirm.ra, gotoConfirm.dec) }
            }
        }
    }
    signal noticeRequested(string message)
    // Set from main.cpp when the build links Qt WebEngine; qmltestrunner and
    // WebEngine-less kits keep the pure-QML fallback canvas.
    property bool aladinEnabled: (typeof aladinAvailable !== "undefined") && aladinAvailable === true
    readonly property var selected: planner ? planner.selectedTarget : ({})
    // Recomputed on target/site change and on every clock tick so altitude and
    // azimuth track the passing night instead of freezing at selection time.
    property int clockTick: 0
    readonly property var horizontal: root.computeHorizontal()
    // The sky scale (px per degree) belongs to the map; the camera field of
    // view is measured against it. Changing the focal length must visibly
    // resize the frame instead of being cancelled by an auto-fitted canvas.
    property real zoom: 1.0
    readonly property real pixelsPerDegree: 12 * zoom
    readonly property real fovWidthDeg: planner ? planner.fovWidthDeg : 0
    readonly property real fovHeightDeg: planner ? planner.fovHeightDeg : 0
    readonly property bool aladinViewReady: root.aladinEnabled && aladinLoader.item
                                            && aladinLoader.item.state === "ready"
    readonly property bool frameShown: frameVisible && !browsing
    // Toolbar-owned display switches; both canvases follow the same state.
    property bool frameVisible: true
    property bool gridVisible: false
    // "framing" is the small-field capture planner; "browsing" is a wide
    // stereographic sky for picking where to shoot.
    property string skyMode: "framing"
    readonly property bool browsing: skyMode === "browse"
    // Whole-sky view rotation (browsing aid) — separate from the camera
    // position angle that rotates the capture frame.
    property real viewRotation: 0
    // The right panel collapses so the sky can use the full width; the
    // state lives here so each visit remembers its own layout.
    property bool targetPanelCollapsed: false
    // True after the latest non-empty query matched nothing; drives the
    // no-result hint independent of popup internals.
    property bool searchEmpty: false
    readonly property var surveySources: [
        {label: "DSS2 彩色", hips: "P/DSS2/color"},
        {label: "DSS2 红光", hips: "P/DSS2/red"},
        {label: "2MASS 彩色", hips: "P/2MASS/color"}
    ]
    property int surveySourceIndex: 0

    // The framing center follows the user's drag in the survey view; the
    // catalogue position is the fallback before any drag.
    readonly property real centerRaHours: {
        if (planner && planner.actualCenterRaHours >= 0) return planner.actualCenterRaHours
        return selected.raHours !== undefined ? selected.raHours : 0
    }
    readonly property real centerDecDeg: {
        if (planner && planner.actualCenterDecDeg >= -90) return planner.actualCenterDecDeg
        return selected.decDeg !== undefined ? selected.decDeg : 0
    }

    function setSkyMode(mode) {
        if (skyMode === mode) return
        skyMode = mode
        if (aladinViewReady) aladinLoader.item.setMode(mode, 60)
        if (!browsing) fitFieldOfView()
        else zoom = Math.max(0.2, zoom * 0.3)
    }

    // Survey switch: applies the selected HiPS source to the imagery layer.
    function setSurveySource(index) {
        surveySourceIndex = index
        if (aladinViewReady) aladinLoader.item.setSurveySource(surveySources[index].hips)
    }

    // The QML bridge flattens the C++ nested list into 8 numbers; regroup
    // into [ra,dec] pairs regardless of which side changed shape.
    function normalizeCorners(raw) {
        // Qt list conversions may arrive as array-likes (length + indexing
        // without being a JS Array), so never gate on Array.isArray().
        if (!raw || raw.length === undefined || raw.length === 0) return []
        if (raw[0] !== null && typeof raw[0] === "object") return raw
        if (raw.length === 8) {
            const pairs = []
            for (let i = 0; i < 4; ++i) pairs.push([raw[2 * i], raw[2 * i + 1]])
            return pairs
        }
        return []
    }

    // Accepts sexagesimal ("0h42m +41°16'") or decimal hours/degrees pairs;
    // returns {raHours, decDeg} or null.
    function parseCoordinate(text) {
        const t = text.trim()
        const sexa = t.match(/^([0-9]+(?:\.[0-9]+)?)\s*h(?:\s*([0-9]+(?:\.[0-9]+)?)\s*m?)?(?:\s*([0-9]+(?:\.[0-9]+)?)\s*s?)?[\s,]+([+-])?([0-9]+(?:\.[0-9]+)?)\s*(?:d|°)(?:\s*([0-9]+(?:\.[0-9]+)?)\s*m?['′]?)?(?:\s*([0-9]+(?:\.[0-9]+)?)\s*s?["″]?)?$/i)
        if (sexa) {
            const ra = Number(sexa[1]) + Number(sexa[2] || 0) / 60 + Number(sexa[3] || 0) / 3600
            const decSign = sexa[4] === "-" ? -1 : 1
            const dec = decSign * (Number(sexa[5]) + Number(sexa[6] || 0) / 60 + Number(sexa[7] || 0) / 3600)
            if (ra >= 0 && ra < 24 && dec >= -90 && dec <= 90) return {raHours: ra, decDeg: dec}
        }
        const decimal = t.match(/^([0-9]+(?:\.[0-9]+)?)\s*[, ]\s*(-?[0-9]+(?:\.[0-9]+)?)$/)
        if (decimal) {
            const ra = Number(decimal[1])
            const dec = Number(decimal[2])
            if (ra >= 0 && ra < 24 && dec >= -90 && dec <= 90) return {raHours: ra, decDeg: dec}
        }
        return null
    }
    readonly property bool selectedSaved: {
        const list = planner ? planner.savedTargets : []
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === selected.id) return true
        }
        return false
    }

    readonly property var altitudeCurveData: {
        void clockTick
        if (!selected.id || !planner) return []
        return planner.nightProfile(selected.raHours, selected.decDeg)
    }
    readonly property var nightWindowData: {
        void clockTick
        return planner ? planner.nightWindow() : ({})
    }
    readonly property var captureWindowData: {
        void clockTick
        if (!selected.id || !planner) return ({})
        return planner.captureWindow(selected.raHours, selected.decDeg, 30)
    }

    function fmtIso(iso) {
        if (!iso) return "—"
        const d = new Date(iso)
        return isNaN(d.getTime()) ? "—" : Qt.formatDateTime(d, "HH:mm")
    }

    function computeHorizontal() {
        void clockTick
        if (!selected.id || !planner) return ({})
        if (typeof planner.horizontalCoordinatesOfSelected === "function")
            return planner.horizontalCoordinatesOfSelected()
        return planner.horizontalCoordinates(selected.id)
    }
    function fitFieldOfView() {
        if (fovHeightDeg <= 0.01) return
        // The survey view owns its own zoom once ready; the QML scale only
        // drives the offline fallback canvas.
        if (aladinViewReady) {
            // Rotated frame extent on the canvas axes, then the horizontal
            // FoV that fits BOTH axes given the window aspect ratio.
            const angle = (planner ? planner.positionAngle : 0) * Math.PI / 180
            const rotW = Math.abs(fovWidthDeg * Math.cos(angle)) + Math.abs(fovHeightDeg * Math.sin(angle))
            const rotH = Math.abs(fovWidthDeg * Math.sin(angle)) + Math.abs(fovHeightDeg * Math.cos(angle))
            const aspect = skyCanvas.width / Math.max(skyCanvas.height, 1)
            aladinLoader.item.fitSurvey(Math.max(rotW, rotH * aspect) * 1.2)
            return
        }
        zoom = Math.max(0.2, Math.min(20, skyCanvas.height * 0.6 / (fovHeightDeg * 12)))
    }

    function raText(raHours) {
        if (typeof raHours !== "number" || !isFinite(raHours) || raHours < 0 || raHours >= 24) return "—"
        const seconds = Math.round(raHours * 3600) % 86400
        return String(Math.floor(seconds / 3600)).padStart(2, "0") + "h "
            + String(Math.floor(seconds / 60) % 60).padStart(2, "0") + "m "
            + String(seconds % 60).padStart(2, "0") + "s"
    }
    function decText(decDeg) {
        if (typeof decDeg !== "number" || !isFinite(decDeg) || Math.abs(decDeg) > 90) return "—"
        const sign = decDeg >= 0 ? "+" : "-"
        const seconds = Math.round(Math.abs(decDeg) * 3600)
        return sign + String(Math.floor(seconds / 3600)).padStart(2, "0") + "° "
            + String(Math.floor(seconds / 60) % 60).padStart(2, "0") + "′ "
            + String(seconds % 60).padStart(2, "0") + "″"
    }

    Timer {
        interval: 30000
        running: root.visible
        repeat: true
        onTriggered: root.clockTick += 1
    }

    color: AsterTheme.canvas

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            spacing: 8
            Text { text: "拍摄规划"; color: AsterTheme.textPrimary; font.pixelSize: 13; font.bold: true }
            Item { Layout.preferredWidth: 8 }

            TextField {
                id: searchInput
                objectName: "targetSearchInput"
                Layout.preferredWidth: 240
                placeholderText: "搜索目标：M31 / 仙女 / 猎户…"
                color: AsterTheme.textPrimary
                placeholderTextColor: AsterTheme.textMuted
                font.pixelSize: 11
                selectByMouse: true
                background: Rectangle {
                    radius: AsterTheme.radiusSmall
                    color: AsterTheme.surface
                    border.width: 1
                    border.color: searchInput.activeFocus ? AsterTheme.accent : AsterTheme.stroke
                }
                onTextChanged: resultsTimer.restart()
                onActiveFocusChanged: if (activeFocus) searchInput.forceActiveFocus()
                Timer {
                    id: resultsTimer
                    interval: 150
                    onTriggered: {
                        root.searchEmpty = searchInput.text.trim().length > 0
                            && root.planner
                            && root.planner.searchTargets(searchInput.text).length === 0
                        searchResults.model = root.planner ? root.planner.searchTargets(searchInput.text) : []
                        searchResults.currentIndex = searchResults.count > 0 ? 0 : -1
                    }
                }
                Keys.onDownPressed: if (searchResults.count > 0) searchResults.currentIndex = Math.min(searchResults.currentIndex + 1, searchResults.count - 1)
                Keys.onUpPressed: if (searchResults.count > 0) searchResults.currentIndex = Math.max(searchResults.currentIndex - 1, 0)
                Keys.onReturnPressed: root.acceptSearch()
            }

            Popup {
                id: resultsPopup
                parent: searchInput
                x: 0
                y: searchInput.height + 2
                width: searchInput.width
                padding: 1
                visible: searchInput.text.length > 0 && (searchResults.count > 0 || root.searchEmpty)
                background: Rectangle {
                    color: AsterTheme.canvasRaised
                    border.color: AsterTheme.strokeStrong
                    radius: AsterTheme.radiusSmall
                }
                contentItem: Column {
                    Text {
                        id: noResultText
                        objectName: "searchNoResult"
                        width: resultsPopup.width - 2
                        visible: searchInput.text.length > 0 && searchResults.count === 0
                        text: "没有匹配的目标，试试 M31 / 仙女 / 猎户"
                        color: AsterTheme.textMuted
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        padding: 10
                    }
                    ListView {
                        id: searchResults
                        objectName: "targetSearchResults"
                        clip: true
                        implicitHeight: Math.min(contentHeight, 260)
                        width: resultsPopup.width - 2
                        model: []
                        delegate: ItemDelegate {
                            id: resultRow
                            required property var modelData
                            width: ListView.view.width
                            height: 34
                            highlighted: ListView.isCurrentItem
                            contentItem: RowLayout {
                                spacing: 6
                                Text { text: resultRow.modelData.id; color: AsterTheme.deviceTitleText; font.pixelSize: 11; font.bold: true; Layout.preferredWidth: 58 }
                                Text { text: resultRow.modelData.name; color: AsterTheme.textPrimary; font.pixelSize: 11; Layout.fillWidth: true }
                                Text { text: resultRow.modelData.constellation; color: AsterTheme.textMuted; font.pixelSize: 10 }
                            }
                            background: Rectangle {
                                color: resultRow.highlighted ? AsterTheme.accentSurface
                                     : resultRow.hovered ? AsterTheme.surfaceHover : "transparent"
                            }
                            onClicked: root.selectFrom(resultRow)
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }
            Text {
                objectName: "targetCoordinates"
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: AsterTheme.textPrimary
                font.pixelSize: 11
                text: root.selected.id ? root.selected.id + " · " + (root.selected.name || "") + "  |  RA "
                    + root.raText(root.selected.raHours) + "  Dec " + root.decText(root.selected.decDeg) + "  J2000"
                    : "目标坐标 —"
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: AsterTheme.stroke }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                id: skyCanvas
                objectName: "skyCanvas"
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#070b0e"
                clip: true

                // Survey imagery layer (Aladin Lite in Qt WebEngine). Hidden
                // until the page reports ready so a network failure always
                // leaves the planned crosshair and FOV box visible below.
                Loader {
                    id: aladinLoader
                    objectName: "aladinLoader"
                    anchors.fill: parent
                    active: root.aladinEnabled
                    source: active ? "../pages/AladinSkyView.qml" : ""
                    onLoaded: skyCanvas.syncAladin()
                }
                Connections {
                    target: aladinLoader.item
                    ignoreUnknownSignals: true
                    function onStateChanged() { skyCanvas.syncAladin(true) }
                    function onNoticeRequested(message) { root.noticeRequested(message) }
                }
                Connections {
                    target: aladinLoader.item || null
                    ignoreUnknownSignals: true
                    function onReadyChanged() { skyCanvas.syncAladin() }
                }
                Connections {
                    target: root.planner || null
                    ignoreUnknownSignals: true
                    function onSelectedTargetChanged() { skyCanvas.syncAladin(true) }
                    function onInstrumentChanged() { skyCanvas.syncAladin(false) }
                    function onObservationTimeChanged() { root.clockTick += 1 }
                    function onFollowClockChanged() { root.clockTick += 1 }
                }
                Connections {
                    // Survey view dragging re-anchors the planned center; RA
                    // and Dec land together so the frame never tears.
                    target: aladinLoader.item || null
                    ignoreUnknownSignals: true
                    function onCenterChanged(raDeg, decDeg) {
                        if (root.aladinViewReady && !aladinLoader.item.inspectingPhoto
                                && !aladinLoader.item.inspectingMount && root.selected.id && planner)
                            planner.setActualCenter(raDeg / 15.0, decDeg)
                    }
                    function onSurveyChanged(hipsId) {
                        // Follow what is actually displayed after a failed switch.
                        for (let i = 0; i < root.surveySources.length; ++i) {
                            if (root.surveySources[i].hips === hipsId) {
                                root.surveySourceIndex = i
                                return
                            }
                        }
                    }
                }
                Connections {
                    target: root.planner || null
                    ignoreUnknownSignals: true
                    function onActualCenterChanged() { skyCanvas.syncAladin(false) }
                }
                Connections {
                    // Display switches reach the survey view without touching
                    // either the browsing state or the footprint geometry.
                    target: root
                    function onFrameVisibleChanged() {
                        if (aladinViewReady) aladinLoader.item.setFrameVisible(frameVisible)
                    }
                    function onGridVisibleChanged() {
                        if (aladinViewReady) aladinLoader.item.setGridVisible(gridVisible)
                    }
                }
                Text {
                    anchors.top: parent.top; anchors.left: parent.left
                    anchors.margins: 10
                    text: {
                        if (!root.aladinEnabled)
                            return "巡天影像未启用：本构建缺少 Qt WebEngine 组件"
                        return aladinLoader.item && aladinLoader.item.state !== "error" ? aladinLoader.item.statusMessage : ""
                    }
                    visible: !root.aladinEnabled
                             || (aladinLoader.item && aladinLoader.item.state !== "ready")
                    color: AsterTheme.textMuted
                    font.pixelSize: 10
                }

                // Pushes the planned frame into the survey layer; the QML
                // crosshair/FOV pair stays as the offline fallback.
                function syncAladin(recenter) {
                    const view = aladinLoader.item
                    if (!view) return
                    // Keep the browser alive during initialization and expose retry on failure.
                    view.visible = true
                    root.refreshMount()
                    root.syncSolvedFootprint()
                    if (root.inspectPhotoPending && view.state === "ready") {
                        root.initialViewResolved = true
                        root.inspectPendingPhoto()
                        return
                    }
                    if (view.state === "ready" && !root.initialViewResolved) {
                        root.initialViewResolved = true
                        if (!root.selected.id) root.returnToMount()
                    }
                    if (view.state !== "ready" || !root.selected.id || root.fovHeightDeg <= 0) return
                    const corners = root.normalizeCorners(root.planner.footprintCorners(
                        root.centerRaHours, root.centerDecDeg,
                        root.fovWidthDeg, root.fovHeightDeg, root.planner.positionAngle))
                    view.setFrameVisible(root.frameShown)
                    view.setGridVisible(root.gridVisible)
                    if (recenter)
                        view.gotoTarget(root.centerRaHours, root.centerDecDeg,
                                        corners, Math.max(root.fovWidthDeg, root.fovHeightDeg))
                    else
                        view.updateFrame(corners)
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    enabled: !root.aladinEnabled || !aladinLoader.item || aladinLoader.item.state !== "ready"
                    onWheel: wheel => {
                        const factor = wheel.angleDelta.y > 0 ? 1.15 : 1 / 1.15
                        root.zoom = Math.max(0.2, Math.min(20, root.zoom * factor))
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: !root.selected.id && !root.aladinEnabled
                    text: "搜索目标，或直接输入坐标（0h42m +41°16′ 或 0.712, 41.269）"
                    color: AsterTheme.textMuted
                    font.pixelSize: 13
                }

                // Target crosshair at the planned center.
                Item {
                    anchors.centerIn: parent
                    visible: !!root.selected.id && (!aladinLoader.item || aladinLoader.item.state !== "ready")
                    width: 1; height: 1
                    Rectangle { width: 26; height: 1; color: AsterTheme.accent; anchors.centerIn: parent }
                    Rectangle { width: 1; height: 26; color: AsterTheme.accent; anchors.centerIn: parent }
                    Text {
                        anchors.top: parent.verticalCenter
                        anchors.topMargin: 18
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.selected.id + " · " + root.selected.name
                        color: AsterTheme.textPrimary
                        font.pixelSize: 12
                    }
                }

                // The planned field of view, rotatable through the position angle.
                Rectangle {
                    id: fovBox
                    objectName: "fovBox"
                    width: root.fovWidthDeg * root.pixelsPerDegree
                    height: root.fovHeightDeg * root.pixelsPerDegree
                    anchors.centerIn: parent
                    color: "transparent"
                    border.color: AsterTheme.accent
                    border.width: 1
                    opacity: root.selected.id && root.frameShown
                             && (!aladinLoader.item || aladinLoader.item.state !== "ready") ? 1 : 0
                    rotation: root.planner ? root.planner.positionAngle : 0
                    Text {
                        anchors.bottom: parent.bottom; anchors.right: parent.right
                        anchors.margins: 4
                        text: root.fovWidthDeg.toFixed(2) + "° × " + root.fovHeightDeg.toFixed(2) + "°"
                        color: AsterTheme.accent
                        font.pixelSize: 10
                    }
                }

                RowLayout {
                    anchors.top: parent.top; anchors.right: parent.right
                    anchors.margins: 10
                    spacing: 6
                    Text {
                        visible: !root.aladinViewReady
                        text: root.zoom.toFixed(1) + "× · " + (24 / root.zoom).toFixed(0) + " px/°"
                        color: AsterTheme.textMuted
                        font.pixelSize: 10
                    }
                    DesktopButton {
                        objectName: "returnToMountButton"
                        visible: root.aladinViewReady
                        text: "回到赤道仪"
                        enabled: root.aladinViewReady && root.mountPointValid
                        ToolTip.visible: hovered
                        ToolTip.text: "仅定位星图视角，不移动赤道仪或改变拍摄目标"
                        onClicked: root.returnToMount()
                    }
                    DesktopButton {
                        objectName: "recenterButton"
                        visible: root.aladinViewReady
                        text: "回到目标"
                        // resetActualCenter already announces the catalogue
                        // position through selectedTargetChanged; an explicit
                        // sync here would recenter a second time.
                        onClicked: if (planner) planner.resetActualCenter()
                    }
                    DesktopButton {
                        objectName: "fitFovButton"
                        text: "适合视场"
                        onClicked: root.fitFieldOfView()
                    }
                    DesktopButton {
                        objectName: "modeFramingButton"
                        text: "构图"
                        checked: !root.browsing
                        onClicked: root.setSkyMode("framing")
                    }
                    DesktopButton {
                        objectName: "modeBrowseButton"
                        text: "浏览"
                        checked: root.browsing
                        onClicked: root.setSkyMode("browse")
                    }
                    Rectangle { width: 1; height: 20; color: AsterTheme.stroke }
                    DesktopButton {
                        objectName: "frameToggleButton"
                        text: "视场框"
                        visible: !root.browsing
                        checked: root.frameVisible
                        onClicked: root.frameVisible = !root.frameVisible
                    }
                    DesktopButton {
                        objectName: "gridToggleButton"
                        text: "网格"
                        visible: root.aladinViewReady
                        checked: root.gridVisible
                        onClicked: root.gridVisible = !root.gridVisible
                    }
                    DesktopComboBox {
                        objectName: "surveySourceCombo"
                        visible: root.aladinViewReady
                        Layout.preferredWidth: 118
                        model: root.surveySources.map(source => source.label)
                        currentIndex: root.surveySourceIndex
                        onActivated: index => root.setSurveySource(index)
                        font.pixelSize: 11
                    }
                    DesktopButton {
                        objectName: "moreToolsButton"
                        visible: skyCanvas.width < 720
                        implicitWidth: 28
                        glyph: toolMenu.visible ? "chevron" : "settings-ui"
                        onClicked: root.toolMenu.popup(this, 0, height + 2)
                    }
                }
                DesktopMenu {
                    id: toolMenu
                    objectName: "toolMenu"
                    // Narrow canvases fold the secondary display switches here.
                    DesktopButton { text: "视场框"; checked: root.frameVisible; onClicked: { root.frameVisible = !root.frameVisible } }
                    DesktopButton { text: "网格"; checked: root.gridVisible; onClicked: { root.gridVisible = !root.gridVisible } }
                }

                Rectangle {
                    objectName: "skyInfoBar"
                    anchors.left: parent.left; anchors.bottom: parent.bottom
                    anchors.leftMargin: 10; anchors.bottomMargin: 10
                    width: Math.min(infoText.implicitWidth + 16, parent.width - 200)
                    height: 24; radius: 3
                    color: "#80070b0e"
                    visible: infoText.text.length > 0
                    Text {
                        id: infoText
                        anchors.centerIn: parent
                        width: parent.width - 12
                        text: {
                            if (!root.selected.id) return ""
                            const az = root.horizontal.azimuth
                            const dirs = ["北", "东北", "东", "东南", "南", "西南", "西", "西北"]
                            const dir = dirs[Math.round(az / 45) % 8]
                            const position = root.horizontal.aboveHorizon
                                ? "高度 " + root.horizontal.altitude.toFixed(1) + "° · 方位 " + az.toFixed(1) + "° (" + dir + ")"
                                : "当前在地平线下 · 高度 " + root.horizontal.altitude.toFixed(1) + "°"
                            const busy = root.aladinViewReady && aladinLoader.item && aladinLoader.item.imageryBusy
                            const source = root.aladinViewReady
                                ? (busy ? " · 影像加载中…" : " · 巡天参考影像（非实时）") : ""
                            return position + source
                        }
                        color: root.horizontal.aboveHorizon ? AsterTheme.statusOk : AsterTheme.textMuted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }

            Rectangle {
                objectName: "skyTargetPanel"
                Layout.preferredWidth: root.targetPanelCollapsed ? 28 : 264
                Layout.fillHeight: true
                color: AsterTheme.surface
                // Collapsed rail: a header button keeps the expand affordance.
                DesktopButton {
                    visible: root.targetPanelCollapsed
                    anchors.centerIn: parent
                    rotation: 0
                    implicitWidth: 24
                    glyph: "chevron-left"
                    Accessible.name: "展开目标面板"
                    ToolTip.visible: hovered
                    ToolTip.text: "展开目标与构图"
                    onClicked: root.targetPanelCollapsed = false
                }
                ScrollView {
                    id: sideScroll
                    anchors.fill: parent
                    visible: !root.targetPanelCollapsed
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: sideScroll.availableWidth - 24
                        x: 12
                        y: 12
                        spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; text: "目标与构图"; color: AsterTheme.deviceTitleText; font.pixelSize: 12; font.bold: true }
                        DesktopButton {
                            objectName: "targetPanelCollapseButton"
                            implicitWidth: 24
                            glyph: "chevron-right"
                            Accessible.name: "收起目标面板"
                            ToolTip.visible: hovered
                            ToolTip.text: "收起，把空间留给星图"
                            onClicked: root.targetPanelCollapsed = true
                        }
                    }
                    PhotoSolvePanel {
                        id: photoSolve
                        Layout.fillWidth: true
                        solver: root.plateSolver
                        centeringController: root.centeringController
                        fovHeight: root.fovHeightDeg
                        targetValid: !!root.selected.id
                        targetRaHours: root.selected.raHours || 0
                        targetDecDeg: root.selected.decDeg || 0
                        mapAvailable: root.aladinViewReady
                        onFootprintVisibleChanged: root.syncSolvedFootprint()
                        onInspectRequested: if (root.aladinViewReady) aladinLoader.item.inspectPhoto(root.plateSolver.result)
                    }
                    DesktopButton {
                        objectName: "siteExpandButton"
                        Layout.fillWidth: true
                        text: (root.siteExpanded ? "▾ " : "▸ ") + "观测站：" + root.siteText(root.planner ? root.planner.longitude : undefined, true)
                            + " / " + root.siteText(root.planner ? root.planner.latitude : undefined, false)
                        onClicked: root.siteExpanded = !root.siteExpanded
                    }
                    DesktopField {
                        objectName: "siteLongitude"
                        visible: root.siteExpanded
                        Layout.fillWidth: true
                        label: "经度（东正西负）"; editable: true
                        value: root.planner ? root.planner.longitude.toFixed(4) : ""
                        minimum: -180; maximum: 180
                        onValueEdited: value => { if (root.planner) { root.planner.longitude = Number(value); root.checkSite() } }
                    }
                    DesktopField {
                        objectName: "siteLatitude"
                        visible: root.siteExpanded
                        Layout.fillWidth: true
                        label: "纬度（北正南负）"; editable: true
                        value: root.planner ? root.planner.latitude.toFixed(4) : ""
                        minimum: -89.9; maximum: 89.9
                        onValueEdited: value => { if (root.planner) { root.planner.latitude = Number(value); root.checkSite() } }
                    }
                    Text { text: "观测时间"; color: AsterTheme.deviceTitleText; font.pixelSize: 12; font.bold: true }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: "观测模式"; color: AsterTheme.textMuted; font.pixelSize: 11 }
                        // Plain buttons: the Switch's toggled delivery proved
                        // unreliable through this stack, while onClicked is
                        // the path every working control here uses.
                        DesktopButton {
                            objectName: "followClockOnButton"
                            text: "实时"
                            checked: root.planner ? root.planner.followClock : true
                            onClicked: if (root.planner) root.planner.followClock = true
                        }
                        DesktopButton {
                            objectName: "followClockOffButton"
                            text: "手动"
                            checked: root.planner ? !root.planner.followClock : false
                            onClicked: if (root.planner) root.planner.followClock = false
                        }
                        Item { Layout.fillWidth: true }
                        DesktopButton {
                            objectName: "timeNowButton"
                            visible: root.planner && !root.planner.followClock
                            text: "现在"
                            onClicked: if (root.planner) root.planner.followClock = true
                        }
                    }
                    // Always visible (dimmed while following the clock): a
                    // hidden row depended on a cross-object binding that the
                    // runtime failed to refresh; enabled-state cannot go stale.
                    RowLayout {
                        objectName: "observationTimeRow"
                        Layout.fillWidth: true
                        opacity: root.planner && !root.planner.followClock ? 1 : 0.35
                        spacing: 4
                        DesktopField {
                            objectName: "observationTimeField"
                            Layout.fillWidth: true
                            label: "时间"; editable: true
                            enabled: root.planner && !root.planner.followClock
                            // formatDateTime renders invalid dates as "...":
                            // read the epoch directly and guard it.
                            value: {
                                if (!root.planner) return ""
                                const ms = root.planner.observationTimeMs
                                if (ms === undefined || ms === null || isNaN(ms)) return "—"
                                return Qt.formatDateTime(new Date(ms), "MM-dd HH:mm")
                            }
                            onValueEdited: value => {
                                if (!root.planner || root.planner.followClock) return
                                const parsed = Date.fromLocaleString(Qt.locale(), value, "MM-dd HH:mm")
                                if (!isNaN(parsed.getTime())) root.planner.observationTime = parsed
                            }
                        }
                        // ±1h works in ANY state: pressing one while the
                        // clock is followed auto-pins the time (now ± 1h) —
                        // no need to hunt for the switch first.
                        DesktopButton {
                            text: "-1h"
                            Layout.preferredWidth: 44
                            onClicked: {
                                if (!root.planner) return
                                if (root.planner.followClock) root.planner.followClock = false
                                root.planner.observationTime =
                                    new Date(root.planner.observationTime.getTime() - 3600000)
                            }
                        }
                        DesktopButton {
                            text: "+1h"
                            Layout.preferredWidth: 44
                            onClicked: {
                                if (!root.planner) return
                                if (root.planner.followClock) root.planner.followClock = false
                                root.planner.observationTime =
                                    new Date(root.planner.observationTime.getTime() + 3600000)
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: AsterTheme.stroke }
                    Text { text: "目标信息"; color: AsterTheme.deviceTitleText; font.pixelSize: 12; font.bold: true }
                    Text {
                        objectName: "plannerTargetTitle"
                        Layout.fillWidth: true
                        visible: !!root.selected.id
                        text: root.selected.id + " · " + root.selected.name
                        color: AsterTheme.textPrimary; font.pixelSize: 12; wrapMode: Text.Wrap
                    }
                    // Empty state: a single guidance line instead of a
                    // column of placeholder dashes.
                    Rectangle {
                        objectName: "skyEmptyTargetHint"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        visible: !root.selected.id
                        radius: AsterTheme.radiusSmall
                        color: AsterTheme.canvasRaised
                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 12
                            text: "搜索或点击星图选择目标"
                            color: AsterTheme.textMuted
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                    DesktopField { visible: !!root.selected.id; label: "星座"; value: root.selected.constellation || "—" }
                    DesktopField { visible: !!root.selected.id; label: "类型"; value: root.selected.type || "—" }
                    DesktopField { visible: !!root.selected.id; label: "视星等"; value: root.selected.magnitude || "—" }
                    DesktopField { visible: !!root.selected.id; label: "视尺寸"; value: root.selected.sizeArcmin ? root.selected.sizeArcmin + "′" : "—" }
                    DesktopField {
                        visible: !!root.selected.id
                        label: "当前高度"
                        value: root.decText(root.horizontal.altitude)
                    }
                    DesktopField {
                        visible: !!root.selected.id
                        label: "最高高度"
                        value: root.decText(root.horizontal.maxAltitude)
                    }
                    Text {
                        objectName: "nightWindowText"
                        Layout.fillWidth: true
                        visible: root.nightWindowData.startISO !== undefined
                        text: "天文夜（本地时） " + root.fmtIso(root.nightWindowData.startISO)
                              + " – " + root.fmtIso(root.nightWindowData.endISO)
                        color: AsterTheme.textSecondary
                        font.pixelSize: 11
                    }
                    Text {
                        objectName: "captureWindowText"
                        Layout.fillWidth: true
                        visible: !!root.selected.id
                        text: root.captureWindowData.startISO !== undefined
                            ? "最佳拍摄 " + root.fmtIso(root.captureWindowData.startISO)
                              + " – " + root.fmtIso(root.captureWindowData.endISO)
                              + " · 最高 " + root.captureWindowData.maxAltitude.toFixed(0) + "°"
                            : "本夜无 ≥30° 的拍摄窗口"
                        color: root.captureWindowData.startISO !== undefined ? AsterTheme.statusOk : AsterTheme.danger
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    Text {
                        objectName: "visibilitySummary"
                        Layout.fillWidth: true
                        visible: !!root.selected.id
                        text: {
                            if (root.horizontal.alwaysUp) return "拱极目标：整夜在地平线上"
                            if (root.horizontal.neverUp) return "本地不可见：始终在地平线下"
                            return "可观测目标：每日过中天时最高"
                        }
                        color: root.horizontal.neverUp ? AsterTheme.danger : AsterTheme.textSecondary
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !!root.selected.id
                        text: "整夜高度曲线"
                        color: AsterTheme.deviceTitleText
                        font.pixelSize: 11
                        font.bold: true
                    }
                    Canvas {
                        id: altitudeCurve
                        objectName: "altitudeCurve"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 110
                        visible: !!root.selected.id
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.fillStyle = "#0a1015"
                            ctx.fillRect(0, 0, width, height)
                            const data = root.altitudeCurveData
                            if (!data || data.length < 2) return
                            const px = i => 4 + i / (data.length - 1) * (width - 10)
                            const py = alt => height - 8 - Math.max(0, Math.min(90, alt)) / 90 * (height - 18)
                            // Astronomical night shading.
                            ctx.fillStyle = "#10222e"
                            for (let i = 0; i < data.length - 1; ++i) {
                                if (data[i].sunAltitude <= -18)
                                    ctx.fillRect(px(i), 5, px(i + 1) - px(i) + 0.5, height - 13)
                            }
                            // Horizon and the 30° imaging threshold.
                            ctx.strokeStyle = "#2a3238"
                            ctx.beginPath(); ctx.moveTo(4, py(0)); ctx.lineTo(width - 6, py(0)); ctx.stroke()
                            ctx.strokeStyle = "#3a4249"
                            ctx.setLineDash([3, 3])
                            ctx.beginPath(); ctx.moveTo(4, py(30)); ctx.lineTo(width - 6, py(30)); ctx.stroke()
                            ctx.setLineDash([])
                            // Target altitude curve.
                            ctx.strokeStyle = "#2f9def"
                            ctx.lineWidth = 1.5
                            ctx.beginPath()
                            for (let i = 0; i < data.length; ++i) {
                                const cx = px(i), cy = py(data[i].altitude)
                                i ? ctx.lineTo(cx, cy) : ctx.moveTo(cx, cy)
                            }
                            ctx.stroke()
                            ctx.fillStyle = "#818b94"
                            ctx.font = "9px sans-serif"
                            ctx.fillText("30°", 6, py(30) - 3)
                            ctx.fillText("地平线", 6, py(0) - 3)
                        }
                    }
                    Connections {
                        target: root
                        function onAltitudeCurveDataChanged() { altitudeCurve.requestPaint() }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: AsterTheme.stroke }
                    Text { text: "构图参数"; visible: !root.browsing; color: AsterTheme.deviceTitleText; font.pixelSize: 12; font.bold: true }
                    DesktopField {
                        objectName: "focalLengthField"
                        visible: !root.browsing
                        Layout.fillWidth: true
                        label: "焦距"; editable: true; unit: "mm"
                        value: root.planner ? root.planner.focalLengthMm : ""
                        minimum: 10; maximum: 10000
                        onValueEdited: value => { if (root.planner) root.planner.focalLengthMm = Number(value) }
                    }
                    DesktopButton {
                        objectName: "readCameraDimensions"
                        visible: !root.browsing
                        Layout.fillWidth: true
                        text: "读取主相机尺寸"
                        enabled: root.cameraDimensionsAvailable && !!root.planner
                        onClicked: root.readCameraDimensions()
                    }
                    Text {
                        visible: !root.browsing
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: AsterTheme.textMuted; font.pixelSize: 11
                        text: root.cameraDimensionsAvailable ? "全幅尺寸可读取，也可手动编辑；不随设备刷新覆盖。"
                            : "主相机未连接或未提供有效尺寸，请手动填写。"
                    }
                    DesktopField {
                        objectName: "sensorWidthField"
                        visible: !root.browsing
                        Layout.fillWidth: true
                        label: "传感器宽"; editable: true; unit: "mm"
                        value: root.planner ? root.planner.sensorWidthMm : ""
                        minimum: 1; maximum: 100
                        onValueEdited: value => { if (root.planner) root.planner.sensorWidthMm = Number(value) }
                    }
                    DesktopField {
                        objectName: "sensorHeightField"
                        visible: !root.browsing
                        Layout.fillWidth: true
                        label: "传感器高"; editable: true; unit: "mm"
                        value: root.planner ? root.planner.sensorHeightMm : ""
                        minimum: 1; maximum: 100
                        onValueEdited: value => { if (root.planner) root.planner.sensorHeightMm = Number(value) }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: !root.browsing
                        spacing: 6
                        Text { text: "旋转角"; color: AsterTheme.textMuted; font.pixelSize: 11 }
                        DesktopField {
                            objectName: "positionAngleField"
                            Layout.fillWidth: true
                            editable: true; unit: "°"
                            value: root.planner ? Math.round(root.planner.positionAngle) : 0
                            minimum: 0; maximum: 360
                            onValueEdited: value => { if (root.planner) root.planner.positionAngle = Number(value) }
                        }
                    }
                    Slider {
                        id: angleSlider
                        objectName: "positionAngleSlider"
                        Layout.fillWidth: true
                        visible: !root.browsing
                        from: 0; to: 360; stepSize: 1
                        value: root.planner ? root.planner.positionAngle : 0
                        onMoved: if (root.planner) root.planner.positionAngle = value
                    }
                    Text { text: "视角旋转 " + root.viewRotation.toFixed(0) + "°（仅浏览）"
                           visible: root.browsing; color: AsterTheme.textMuted; font.pixelSize: 11 }
                    Slider {
                        objectName: "viewRotationSlider"
                        Layout.fillWidth: true
                        visible: root.browsing
                        from: 0; to: 360; stepSize: 1
                        value: root.viewRotation
                        onMoved: {
                            root.viewRotation = value
                            if (root.aladinViewReady) aladinLoader.item.setViewRotation(value)
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: AsterTheme.stroke }
                    DesktopButton {
                        objectName: "saveTargetButton"
                        Layout.fillWidth: true
                        text: root.selectedSaved ? "更新当前构图" : "设为拍摄目标"
                        primary: true
                        enabled: !!root.selected.id
                        onClicked: if (root.planner) root.planner.saveSelectedTarget()
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: !root.centeringController || (root.mountGotoBusy && !root.centeringController.busy)
                        DesktopButton {
                            objectName: "gotoMountButton"
                            Layout.fillWidth: true
                            text: "仅指向当前构图"
                            visible: !root.centeringController
                            enabled: !!root.selected.id && root.mountDevice.connected === true && !root.mountGotoBusy
                            onClicked: {
                                gotoConfirm.ra = root.centerRaHours
                                gotoConfirm.dec = root.centerDecDeg
                                gotoConfirm.open()
                            }
                        }
                        DesktopButton {
                            objectName: "stopMountGotoButton"
                            text: "停止"
                            visible: !root.centeringController || root.mountGotoBusy
                            enabled: root.mountDevice.connected === true
                            onClicked: {
                                if (root.centeringController && root.centeringController.busy) root.centeringController.cancel()
                                else root.deviceManager.mountAbortSlew()
                            }
                        }
                    }
                    CenteringControls {
                        Layout.fillWidth: true
                        visible: !!root.centeringController
                        controller: root.centeringController
                        ready: root.mountDevice.connected===true && root.cameraDevice.connected===true && !root.mountGotoBusy && root.plateSolver && !root.plateSolver.busy
                        targetValid: !!root.selected.id
                        targetName: root.selected.id ? root.selected.id+" · "+(root.selected.name || "") : ""
                        targetRaHours: root.selected.id ? root.selected.raHours : 0
                        targetDecDeg: root.selected.id ? root.selected.decDeg : 0
                        fovHeight: root.fovHeightDeg
                        showOnlySlew: true
                        onlySlewReady: root.mountDevice.connected===true && !root.mountGotoBusy
                        onOnlySlewRequested: {
                            gotoConfirm.ra=root.selected.raHours
                            gotoConfirm.dec=root.selected.decDeg
                            gotoConfirm.open()
                        }
                    }
                    Text {
                        objectName: "framingCoordinates"
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: AsterTheme.textMuted; font.pixelSize: 11
                        text: "构图中心 J2000\nRA " + (root.selected.id ? root.raText(root.centerRaHours) : "—")
                            + "  Dec " + (root.selected.id ? root.decText(root.centerDecDeg) : "—")
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: AsterTheme.textMuted
                        font.pixelSize: 11
                        text: !root.mountDevice.connected ? "赤道仪未连接" :
                            ({Pending:"正在发送指向", Slewing:"正在指向", Arrived:"驱动坐标已到位（未解析验证）",
                              Stopped:"指向已停止", Failed:"指向失败，请查看通知", StopFailed:"停止失败，请检查驱动并重试停止"}[root.mountMetrics.gotoState]
                            || "赤道仪已连接")
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: "#f0b85b"
                        font.pixelSize: 11
                        text: root.mountPointValid ? "◎ 赤道仪 J2000  " + Number(root.mountMetrics.skyRa).toFixed(5)
                            + "h / " + Number(root.mountMetrics.skyDec).toFixed(4) + "°"
                            : "实际指向暂无有效回读"
                    }
                    Text { visible: root.planner && root.planner.savedTargets.length > 0
                           text: "已存构图（点击恢复）"; color: AsterTheme.textMuted; font.pixelSize: 10 }
                    Repeater {
                        model: root.planner ? root.planner.savedTargets : []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 4
                            DesktopButton {
                                objectName: "applyTarget" + modelData.id
                                Layout.fillWidth: true
                                text: modelData.id + " · " + modelData.name
                                onClicked: if (root.planner) root.planner.applySavedTarget(modelData.id)
                            }
                            DesktopButton {
                                objectName: "removeTarget" + modelData.id
                                implicitWidth: 24
                                glyph: "close"
                                onClicked: if (root.planner) root.planner.removeSavedTarget(modelData.id)
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    }
                }
                Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: AsterTheme.stroke }
            }
        }
    }

    function selectFrom(row) {
        if (!row || !row.modelData || !planner) return
        planner.selectTarget(row.modelData.id)
        searchInput.text = ""
        searchResults.model = []
        searchEmpty = false
    }

    // Return in the search field: highlighted row first, then raw coordinates.
    function acceptSearch() {
        if (searchResults.currentIndex >= 0 && searchResults.currentIndex < searchResults.count) {
            selectFrom(searchResults.itemAtIndex(searchResults.currentIndex))
            return
        }
        if (searchResults.count > 0) {
            selectFrom(searchResults.itemAtIndex(0))
            return
        }
        const coordinate = parseCoordinate(searchInput.text)
        if (coordinate && planner) {
            planner.selectCoordinate(coordinate.raHours, coordinate.decDeg)
            searchInput.text = ""
            searchResults.model = []
            searchEmpty = false
        }
    }
}
