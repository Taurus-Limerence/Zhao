import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"
import "../theme"
import "../panels"

Rectangle {
    id: root
    property bool hudVisible: true
    property bool crosshairVisible: true
    property bool roiVisible: false
    property real zoom: 1
    property bool fitImage: true
    property var deviceManager: null
    property var plateSolver: null
    property var centeringController: null
    property var autoFocusController: null
    property bool sequenceActive: false
    property var planner: null
    property var mainCamera: deviceManager ? deviceManager.device(1) : ({})
    property var mountDevice: deviceManager ? deviceManager.device(0) : ({})
    property var focuserDevice: deviceManager ? deviceManager.device(4) : ({})
    signal noticeRequested(string message)
    Connections {
        target: root.deviceManager
        function onDevicesChanged() {
            root.mainCamera = root.deviceManager.device(1)
            root.mountDevice = root.deviceManager.device(0)
            root.focuserDevice = root.deviceManager.device(4)
        }
    }
    signal inspectPhotoRequested()
    // Live frames arrive from the camera worker through the "live" image
    // provider; the counter in the URL forces a reload per received frame.
    readonly property bool livePreview: !!deviceManager && deviceManager.previewCounter > 0
    readonly property string previewSource: livePreview
        ? "image://live/frame?" + deviceManager.previewCounter
        : "../assets/images/preview.jpg"
    signal previewTapped()
    signal histogramRequested()
    signal guideRequested()
    property bool guideShown: false
    property bool histogramShown: false
    color: AsterTheme.imageCanvas
    clip: true

    Flickable {
        id: viewport
        anchors.fill: parent
        anchors.topMargin: root.hudVisible ? 35 : 0
        contentWidth: Math.max(width, preview.width)
        contentHeight: Math.max(height, preview.height)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Image {
            id: preview
            objectName: "samplePreview"
            source: root.previewSource
            asynchronous: false
            width: root.fitImage ? viewport.width : sourceSize.width * root.zoom
            height: root.fitImage ? viewport.height : sourceSize.height * root.zoom
            x: Math.max(0, (viewport.width - width) / 2)
            y: Math.max(0, (viewport.height - height) / 2)
            fillMode: Image.PreserveAspectFit
            smooth: true
            MouseArea {
                objectName: "previewToggleArea"
                anchors.fill: parent
                onDoubleClicked: root.previewTapped()
            }
        }
        WheelHandler {
            target: null
            onWheel: event => {
                if (root.fitImage) {
                    root.zoom = Math.min(viewport.width / preview.sourceSize.width, viewport.height / preview.sourceSize.height)
                    root.fitImage = false
                }
                root.zoom = Math.max(0.2, Math.min(3, root.zoom * (event.angleDelta.y > 0 ? 1.1 : 0.9)))
                event.accepted = true
            }
        }
    }
    Item {
        anchors.fill: viewport
        visible: root.crosshairVisible
        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 1; color: AsterTheme.crosshair }
        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; height: parent.height; width: 1; color: AsterTheme.crosshair }
    }
    Rectangle {
        objectName: "roiOverlay"
        visible: root.roiVisible
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        width: root.width * 0.45
        height: root.height * 0.4
        color: "transparent"
        border.color: AsterTheme.accent
        Text { anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 6; text: "ROI · 预览区域"; color: AsterTheme.accent; font.pixelSize: 10 }
        DragHandler { }
    }
    Item {
        id: captureHud
        objectName: "captureHud"
        anchors.fill: parent
        visible: root.hudVisible
        Rectangle {
            width: parent.width; height: 35
            color: AsterTheme.canvas
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: AsterTheme.stroke }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8; anchors.rightMargin: 8
                spacing: 3
                DesktopButton {
                    objectName: "fitButton"
                    glyph: "fit"; text: "适合"; checked: root.fitImage
                    onClicked: { root.fitImage = true; viewport.contentX = 0; viewport.contentY = 0 }
                }
                DesktopButton {
                    objectName: "actualSizeButton"
                    text: "1:1"; checked: !root.fitImage && root.zoom === 1
                    onClicked: { root.fitImage = false; root.zoom = 1; viewport.contentX = Math.max(0, (preview.width - viewport.width) / 2); viewport.contentY = Math.max(0, (preview.height - viewport.height) / 2) }
                }
                Rectangle { width: 1; height: 16; color: AsterTheme.stroke }
                DesktopButton { glyph: "stretch"; text: root.width > 660 ? "自动拉伸" : ""; enabled: false; ToolTip.text: "需接入线性相机帧"; ToolTip.visible: hovered }
                DesktopButton { glyph: "star"; text: root.width > 800 ? "星点测量" : ""; enabled: false }
                DesktopButton { objectName: "crosshairButton"; glyph: "crosshair-ui"; text: root.width > 600 ? "十字线" : ""; checked: root.crosshairVisible; onClicked: root.crosshairVisible = !root.crosshairVisible }
                DesktopButton { objectName: "roiButton"; glyph: "roi"; text: root.width > 740 ? "ROI 区域" : ""; checked: root.roiVisible; onClicked: root.roiVisible = !root.roiVisible }
                DesktopButton {
                    objectName: "guideToolbarButton"
                    icon.source: "../assets/icons/guide.svg"
                    icon.width: 14; icon.height: 14
                    text: root.width > 700 ? "导星曲线" : ""
                    checked: root.guideShown
                    Accessible.name: "切换导星曲线"
                    ToolTip.visible: hovered
                    ToolTip.text: "显示 / 隐藏导星曲线"
                    onClicked: root.guideRequested()
                }
                DesktopButton {
                    objectName: "histogramToolbarButton"
                    icon.source: "../assets/icons/histogram.svg"
                    icon.width: 14; icon.height: 14
                    text: root.width > 700 ? "直方图" : ""
                    checked: root.histogramShown
                    Accessible.name: "切换直方图"
                    ToolTip.visible: hovered
                    ToolTip.text: "显示 / 收起直方图"
                    onClicked: root.histogramRequested()
                }
                Item { Layout.fillWidth: true }
                DesktopButton {
                    objectName: "openAutoFocusPanel"
                    text: root.autoFocusController && root.autoFocusController.busy ? "对焦中…" : "自动对焦"
                    checked: focusPopup.opened
                    onClicked: {
                        solvePopup.close()
                        focusPopup.opened ? focusPopup.close() : focusPopup.open()
                    }
                }
                DesktopButton {
                    objectName: "openPhotoSolvePanel"
                    text: "解析"
                    checked: solvePopup.opened
                    onClicked: {
                        focusPopup.close()
                        solvePopup.opened ? solvePopup.close() : solvePopup.open()
                    }
                }
                Text { text: root.fitImage ? "适合视窗" : Math.round(root.zoom * 100) + "%"; color: AsterTheme.textMuted; font.pixelSize: 9 }
            }
        }
        Rectangle {
            anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 12
            width: sampleLabel.implicitWidth + 16; height: 25
            color: AsterTheme.canvas
            border.color: AsterTheme.stroke
            Text { id: sampleLabel; anchors.centerIn: parent; text: "示例画面 · 未解析目标"; color: AsterTheme.textSecondary; font.pixelSize: 10 }
        }
        Text {
            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 16
            visible: root.width > 630
            text: "滚轮缩放 · 拖动平移 · 双击专注预览"
            color: AsterTheme.textMuted; font.pixelSize: 9
        }
    }
    Popup {
        id: focusPopup
        objectName: "captureAutoFocusPopup"
        x: Math.max(0, root.width-width-8); y: 40
        width: Math.min(420, root.width-16)
        height: Math.min(root.height-48, focusPanel.implicitHeight+24)
        padding: 12
        modal: false
        background: Rectangle { color: AsterTheme.surface; border.color: AsterTheme.stroke; radius: 4 }
        contentItem: ScrollView {
            clip: true
            AutoFocusPanel {
                id: focusPanel
                width: focusPopup.availableWidth
                controller: root.autoFocusController
                controlsLocked: root.sequenceActive
                cameraConnected: root.mainCamera.connected === true
                focuserConnected: root.focuserDevice.connected === true
                onNoticeRequested: message => root.noticeRequested(message)
            }
        }
    }
    Popup {
        id: solvePopup
        objectName: "captureSolvePopup"
        x: Math.max(0, root.width-width-8); y: 40
        width: Math.min(330,root.width-16)
        height: Math.min(root.height-48,solvePanel.implicitHeight+24)
        padding: 12
        modal: false
        background: Rectangle { color:AsterTheme.surface; border.color:AsterTheme.stroke; radius:4 }
        contentItem: ScrollView {
            clip:true
            PhotoSolvePanel {
                id:solvePanel
                width:solvePopup.availableWidth
                captureMode:true
                solver:root.plateSolver
                centeringController:root.centeringController
                mountConnected:root.mountDevice.connected===true
                targetName:targetValid ? root.planner.selectedTarget.id+" · "+(root.planner.selectedTarget.name || "") : ""
                cameraConnected:root.mainCamera.connected===true
                fovHeight:root.planner ? root.planner.fovHeightDeg : 0
                targetValid:root.planner && !!root.planner.selectedTarget.id
                targetRaHours:targetValid ? root.planner.selectedTarget.raHours : 0
                targetDecDeg:targetValid ? root.planner.selectedTarget.decDeg : 0
                onInspectRequested: { solvePopup.close(); root.inspectPhotoRequested() }
            }
        }
    }
}
