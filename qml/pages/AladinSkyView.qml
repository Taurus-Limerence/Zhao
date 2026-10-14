import QtQuick
import QtQuick.Controls
import QtWebEngine

// Survey imagery layer around Aladin Lite v3 (LGPL-3.0, CDS). Reports its
// lifecycle through `state` ("loading" | "ready" | "error"); the parent keeps
// its own crosshair/FOV fallback visible until the survey is actually ready.
// Tiles accumulate in a persistent disk cache, so previously browsed regions
// keep working offline — an accumulation cache, not a managed offline archive.
Item {
    id: root
    property string state: "loading"
    property string statusMessage: "巡天参考影像加载中…（需联网）"
    signal retryRequested()
    property int loadGeneration: 0

    // WebEngineProfilePrototype (6.9+) is NOT assignable to WebEngineView.profile;
    // the deprecated element still works and keeps the persistent tile cache.
    WebEngineProfile {
        id: surveyProfile
        storageName: "StellaMoraSurvey"
        httpCacheType: WebEngineProfile.DiskHttpCache
        persistentCookiesPolicy: WebEngineProfile.NoPersistentCookies
        // Path injected from C++ (QStandardPaths); empty falls back to the
        // engine default location.
        persistentStoragePath: (typeof surveyCachePath !== "undefined" && surveyCachePath.length > 0)
                                   ? surveyCachePath + "/survey-cache" : ""
    }

    function gotoTarget(raHours, decDeg, corners, fovDeg) {
        if (state !== "ready") return
        inspectingPhoto = false
        inspectingMount = false
        // Any recenter invalidates in-flight center polls.
        ++viewGeneration
        // The page expects a JSON string it parses itself; double stringify
        // embeds it as a safely escaped literal.
        const json = JSON.stringify(JSON.stringify(corners))
        view.runJavaScript(
            "window.stellaGoto(" + (raHours * 15).toFixed(6) + "," + decDeg.toFixed(6)
            + "," + json + "," + fovDeg.toFixed(6) + ")")
    }

    property string lastSentMountState: ""
    property bool inspectingPhoto: false
    property bool inspectingMount: false
    function inspectMount(raHours, decDeg) {
        if (state !== "ready" || !isFinite(raHours) || !isFinite(decDeg)
                || raHours < 0 || raHours >= 24 || decDeg < -90 || decDeg > 90) return
        inspectingMount = true
        inspectingPhoto = false
        ++viewGeneration
        view.runJavaScript("window.stellaInspectMount(" + JSON.stringify([raHours * 15, decDeg]) + ")")
    }
    property string lastSentSolvedState: ""
    function setSolvedFootprint(corners) {
        if (state !== "ready") return
        const payload=JSON.stringify(corners)
        if (payload===lastSentSolvedState) return
        lastSentSolvedState=payload
        view.runJavaScript("window.stellaSolved("+payload+")")
    }
    function inspectPhoto(result) {
        if (state !== "ready" || !result.corners) return
        inspectingPhoto=true
        inspectingMount=false
        ++viewGeneration
        view.runJavaScript("window.stellaInspectPhoto("+JSON.stringify(result)+")")
    }
    function setMountPosition(valid, raHours, decDeg) {
        if (state !== "ready") return
        const payload = JSON.stringify(valid ? [raHours * 15, decDeg] : null)
        if (payload === lastSentMountState) return
        lastSentMountState = payload
        view.runJavaScript("window.stellaMount(" + payload + ")")
    }

    // Instrument/angle edits redraw the footprint in place: the browsing
    // position and zoom the user chose are never disturbed.
    // Change-only forwarding: every skipped round trip is one less engine
    // redraw request. Identical payloads (idle center polls) never reach it.
    property string lastSentFrameState: ""
    property string lastSentGridState: ""
    property string lastDrawnCorners: ""
    // Bumped on every recenter so stale center polls cannot apply an old sky
    // position to a newly selected target.
    property int viewGeneration: 0

    function updateFrame(corners) {
        if (state !== "ready") return
        const json = JSON.stringify(JSON.stringify(corners))
        if (json === lastDrawnCorners) return
        lastDrawnCorners = json
        view.runJavaScript("window.stellaFrame(" + json + ")")
    }

    function fitSurvey(fovDeg) {
        if (state !== "ready") return
        view.runJavaScript("window.stellaFit(" + fovDeg.toFixed(6) + ")")
    }

    function setGridVisible(visible) {
        if (state !== "ready") return
        const wanted = visible ? "1" : "0"
        if (lastSentGridState === wanted) return
        lastSentGridState = wanted
        view.runJavaScript("window.stellaGrid(" + (visible ? "true" : "false") + ")")
    }

    function setMode(mode, browseFovDeg) {
        if (state !== "ready") return
        view.runJavaScript("window.stellaMode(" + JSON.stringify(mode) + ","
                           + (browseFovDeg > 0 ? browseFovDeg : 60) + ")")
    }

    function setViewRotation(deg) {
        if (state !== "ready") return
        view.runJavaScript("window.stellaViewRotation(" + deg.toFixed(4) + ")")
    }

    function setSurveySource(hipsId) {
        if (state !== "ready") return
        view.runJavaScript("window.stellaSurvey(" + JSON.stringify(hipsId) + ")")
    }

    function setFrameVisible(visible) {
        if (state !== "ready") return
        const wanted = visible ? "1" : "0"
        if (lastSentFrameState === wanted) return
        lastSentFrameState = wanted
        view.runJavaScript("window.stellaFrameVisible(" + (visible ? "true" : "false") + ")")
    }

    // Engine-ready is not imagery-on-screen: tiles keep streaming for a while
    // after A.init resolves, so the busy state is polled separately.
    property bool imageryBusy: true
    // Live view center in degrees; NaN while unknown. Dragging the survey
    // view moves the planned framing center through these values.
    property double centerRaDeg: Number.NaN
    property double centerDecDeg: Number.NaN
    // The survey actually displayed; may lag the requested one after a
    // failed source switch.
    property string currentSurvey: "P/DSS2/color"
    // True once the engine reports a base layer — "imagery on screen".
    property bool imageryReady: false
    // Emitted once per poll with BOTH center halves together.
    signal centerChanged(double raDeg, double decDeg)
    signal surveyChanged(string hipsId)

    Timer {
        interval: 800
        running: root.state === "ready" && root.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            view.runJavaScript(
                "window.stellaStatus ? window.stellaStatus() : '{}'",
                function (result) {
                    if (!result) return
                    try {
                        const status = JSON.parse(result)
                        root.imageryReady = status.hasBase === true
                        if (status.surveyId && status.surveyId !== root.currentSurvey) {
                            root.currentSurvey = status.surveyId
                            root.surveyChanged(root.currentSurvey)
                        }
                    } catch (error) {}
                })
            const generation = root.viewGeneration
            view.runJavaScript(
                "JSON.stringify(window.stellaCenter ? window.stellaCenter() : null)",
                function (result) {
                    // A recenter since this request started makes the reply
                    // describe a position the user already left behind.
                    if (generation !== root.viewGeneration || !result) return
                    try {
                        const center = JSON.parse(result)
                        if (Array.isArray(center) && center.length === 2) {
                            root.centerRaDeg = Number(center[0])
                            root.centerDecDeg = Number(center[1])
                            root.centerChanged(root.centerRaDeg, root.centerDecDeg)
                        }
                    } catch (error) {}
                })
            view.runJavaScript(
                "window.stellaCurrentSurvey ? window.stellaCurrentSurvey() : null",
                function (result) {
                    if (typeof result === "string" && result.length > 0 && result !== root.currentSurvey) {
                        root.currentSurvey = result
                        root.surveyChanged(result)
                    }
                })
        }
    }

    function reload() {
        ++loadGeneration
        state = "loading"
        statusMessage = "巡天参考影像加载中…（需联网）"
        deadline.restart()
        view.reload()
    }

    Timer {
        id: pollTimer
        interval: 1500
        running: root.state === "loading"
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            if (root.state === "ready") return
            const generation = root.loadGeneration
            view.runJavaScript("JSON.stringify({state: window.stellaReady || 'loading', message: window.stellaMessage || ''})",
                               function (result) {
                if (generation !== root.loadGeneration || root.state !== "loading" || !result) return
                try {
                    const report = JSON.parse(result)
                    root.applyState(report.state, report.message)
                } catch (error) { console.warn("[SkyMap] Invalid bootstrap status", error) }
            })
        }
    }

    Timer {
        id: deadline
        interval: 45000
        running: true
        onTriggered: if (root.state === "loading") root.applyState("error", "巡天引擎加载超时，请检查网络后重试")
    }

    signal noticeRequested(string message)
    function applyState(result, message) {
        if (result === "ready" && state !== "ready") {
            state = "ready"
            deadline.stop()
            statusMessage = "巡天引擎已就绪 · 影像按需联网加载"
            resizeNudge.restart()
        } else if (result === "error" && state !== "error") {
            state = "error"
            deadline.stop()
            statusMessage = message || "巡天影像加载失败：请检查网络后重试"
            noticeRequested(statusMessage)
            console.warn("[SkyMap]", statusMessage)
        }
    }

    // Chromium resizes its render surface asynchronously; the page's
    // ResizeObserver may sample stale geometry. After our item settles we
    // force the page to recompute and re-render at the new size.
    Timer {
        id: resizeNudge
        interval: 150
        onTriggered: if (root.state === "ready" && root.visible && root.width > 0 && root.height > 0)
            view.runJavaScript("window.stellaNudgeResize ? window.stellaNudgeResize() : 0")
    }
    onWidthChanged: resizeNudge.restart()
    onHeightChanged: resizeNudge.restart()
    onVisibleChanged: if (visible) resizeNudge.restart()

    WebEngineView {
        id: view
        anchors.fill: parent
        url: "qrc:///qt/qml/StellaMora/qml/assets/aladin.html"
        profile: surveyProfile
        backgroundColor: "#070b0e"
        settings.localContentCanAccessRemoteUrls: true
        settings.localContentCanAccessFileUrls: false
        onLoadingChanged: function(request) {
            if (request.status === WebEngineView.LoadStartedStatus) {
                ++root.loadGeneration
                root.state = "loading"
                root.imageryReady = false
                // The fresh page knows nothing; re-send all cached state.
                lastSentFrameState = ""
                lastSentGridState = ""
                lastSentMountState = ""
                lastSentSolvedState = ""
                inspectingPhoto = false
                inspectingMount = false
                lastDrawnCorners = ""
                root.statusMessage = "正在加载巡天页面…"
                deadline.restart()
            }
            if (request.status === WebEngineView.LoadFailedStatus)
                root.applyState("error", "页面加载失败：" + request.errorString + " (" + request.errorCode + ")")
        }
        onJavaScriptConsoleMessage: function(level, message, lineNumber, sourceID) {
            console.log("[SkyMap JS]", sourceID, lineNumber, message)
        }
    }

    // The CDS attribution lives in the parent's bottom info bar; this view
    // stays clean so Aladin's own logo (bottom-right) remains visible.
    Button {
        z: 10
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        visible: root.state === "error"
        text: "重试巡天影像"
        onClicked: root.reload()
    }
}
