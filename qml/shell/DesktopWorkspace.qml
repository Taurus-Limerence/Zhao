import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../controls"
import "../pages"
import "../panels"

RowLayout {
    id: root
    required property var deviceManager
    property var planner: null
    property var guider: null
    property var sequenceController: null
    property var plateSolver: null
    property var centeringController: null
    property var autoFocusController: null
    property int workspace: 0
    property int selectedDevice: 1
    property bool immersive: false
    property bool inspectorCollapsed: false
    property bool guideExpanded: false
    property bool histogramExpanded: false
    property real savedInspectorWidth: 310
    property var histogramBins: []
    // 星图页懒加载：首次进入（workspace === 2）才实例化，避免每次启动都拉起
    // WebEngine/Chromium；之后驻留内存以保留巡天视图的浏览位置。
    property bool skyMapVisited: false
    property bool inspectPhotoPending: false
    onWorkspaceChanged: if (workspace === 2) skyMapVisited = true

    function toggleInspector() {
        if (!inspectorCollapsed) savedInspectorWidth = inspectorPane.width
        inspectorCollapsed = !inspectorCollapsed
        inspectorPane.SplitView.preferredWidth = inspectorCollapsed ? 32 : savedInspectorWidth
    }

    function toggleGuide() {
        guideExpanded = !guideExpanded
    }

    function toggleHistogram() {
        histogramExpanded = !histogramExpanded
    }
    signal workspaceRequested(int workspace)
    signal noticeRequested(string message)
    signal connectionsRequested()
    signal settingsRequested(int device)
    signal unavailableRequested(string feature)
    signal immersiveRequested()
    spacing: 0

    Rectangle {
        Layout.preferredWidth: 68
        Layout.fillHeight: true
        visible: !root.immersive
        color: AsterTheme.surface
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: AsterTheme.stroke }
        Column {
            width: parent.width
            y: 8
            Repeater {
                model: [
                    {label: "主控", icon: "grid", page: 0},
                    {label: "星图", icon: "map-ui", page: 2},
                    {label: "序列", icon: "list", page: 3},
                    {label: "导星", icon: "crosshair-ui", page: 1},
                    {label: "全天空", icon: "fisheye", page: -1},
                    {label: "时序影像", icon: "film", page: -1}
                ]
                delegate: Button {
                    required property var modelData
                    objectName: "nav_" + modelData.icon
                    width: 68; height: 62
                    padding: 0
                    hoverEnabled: true
                    icon.source: "../assets/icons/" + modelData.icon + ".svg"
                    icon.width: 19; icon.height: 19
                    icon.color: root.workspace === modelData.page ? AsterTheme.accent : AsterTheme.textSecondary
                    display: AbstractButton.TextUnderIcon
                    text: modelData.label
                    font.family: AsterTheme.uiFont
                    font.pixelSize: 10
                    spacing: 7
                    palette.buttonText: root.workspace === modelData.page ? AsterTheme.accent : AsterTheme.textSecondary
                    background: Rectangle {
                        color: parent.hovered ? AsterTheme.surfaceHover : "transparent"
                        Rectangle {
                            width: 3; height: 36; anchors.verticalCenter: parent.verticalCenter
                            color: AsterTheme.accent
                            visible: root.workspace === modelData.page
                        }
                    }
                    onClicked: modelData.page >= 0 ? root.workspaceRequested(modelData.page) : root.unavailableRequested(modelData.label)
                }
            }
        }
        DesktopButton {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 12
            height: 54; width: 66
            text: "设置"; glyph: "settings-ui"
            display: AbstractButton.TextUnderIcon
            onClicked: root.connectionsRequested()
        }
    }

    SplitView {
        id: horizontalSplit
        Layout.fillWidth: true
        Layout.fillHeight: true
        orientation: Qt.Horizontal
        handle: Rectangle {
            objectName: "inspectorResizeHandle"
            implicitWidth: root.inspectorCollapsed || root.immersive ? 0 : 5
            color: SplitHandle.pressed || SplitHandle.hovered ? AsterTheme.accent : AsterTheme.stroke
        }

        SplitView {
            id: guideSplit
            orientation: Qt.Vertical
            SplitView.fillWidth: true
            SplitView.minimumWidth: 400
            handle: Rectangle {
                objectName: "guideResizeHandle"
                implicitHeight: 5
                color: SplitHandle.pressed || SplitHandle.hovered ? AsterTheme.accent : AsterTheme.stroke
            }
            Item {
                SplitView.fillHeight: true
                SplitView.minimumHeight: 240
                clip: true
                CaptureWorkspace {
                    anchors.fill: parent
                    visible: root.workspace === 0
                    deviceManager: root.deviceManager
                    plateSolver: root.plateSolver
                    centeringController: root.centeringController
                    autoFocusController: root.autoFocusController
                    sequenceActive: !!root.sequenceController && root.sequenceController.active
                    onNoticeRequested: message => root.noticeRequested(message)
                    planner: root.planner
                    onInspectPhotoRequested: { root.inspectPhotoPending = true; root.workspaceRequested(2) }
                    hudVisible: !root.immersive
                    guideShown: root.guideExpanded
                    histogramShown: root.histogramExpanded
                    onGuideRequested: root.toggleGuide()
                    onHistogramRequested: root.toggleHistogram()
                    onPreviewTapped: root.immersiveRequested()
                }
                GuideWorkspace {
                    anchors.fill: parent
                    visible: root.workspace === 1
                    deviceManager: root.deviceManager
                    guider: root.guider
                    onConnectionsRequested: root.connectionsRequested()
                    onNoticeRequested: message => root.noticeRequested(message)
                }
                SequenceWorkspace {
                    anchors.fill: parent
                    visible: root.workspace === 3
                    controller: root.sequenceController
                    autoFocusController: root.autoFocusController
                    deviceManager: root.deviceManager
                    planner: root.planner
                    onConnectionsRequested: root.connectionsRequested()
                    onNoticeRequested: message => root.noticeRequested(message)
                }
                Loader {
                    anchors.fill: parent
                    visible: root.workspace === 2
                    active: root.skyMapVisited
                    sourceComponent: SkyMapWorkspace {
                        anchors.fill: parent
                        planner: root.planner
                        deviceManager: root.deviceManager
                        plateSolver: root.plateSolver
                        centeringController: root.centeringController
                        inspectPhotoPending: root.inspectPhotoPending
                        onPhotoInspected: root.inspectPhotoPending = false
                        onNoticeRequested: message => root.noticeRequested(message)
                    }
                }
            }
            RowLayout {
                objectName: "monitorArea"
                spacing: 0
                visible: !root.immersive && root.workspace === 0 && (root.guideExpanded || root.histogramExpanded)
                SplitView.preferredHeight: 170
                SplitView.minimumHeight: 120
                SplitView.maximumHeight: Math.max(120, Math.min(280, guideSplit.height * 0.45))
                GuideMonitorPanel {
                    objectName: "guideMonitor"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.guideExpanded
                    statusText: root.deviceManager && root.deviceManager.devices.length > 2 && root.deviceManager.devices[2].connected
                                ? "等待导星数据" : "未连接导星相机"
                    onToggleRequested: root.toggleGuide()
                }
                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    visible: root.guideExpanded && root.histogramExpanded
                    color: AsterTheme.stroke
                }
                HistogramPanel {
                    objectName: "histogramPanel"
                    Layout.fillWidth: !root.guideExpanded
                    Layout.preferredWidth: 320
                    Layout.fillHeight: true
                    visible: root.histogramExpanded
                    bins: root.histogramBins
                    onToggleRequested: root.toggleHistogram()
                }
            }
        }

        Item {
            id: inspectorPane
            objectName: "inspectorPane"
            // Sky and guide pages host their own dedicated panels; the
            // generic all-devices inspector only crowds them out. Layout
            // memory is per-workspace: hiding it here never touches the
            // capture page's expand/collapse state.
            visible: !root.immersive && root.workspace === 0
            SplitView.preferredWidth: 310
            SplitView.minimumWidth: root.inspectorCollapsed ? 32 : 280
            SplitView.maximumWidth: root.inspectorCollapsed ? 32 : 380
            clip: true

            DeviceInspector {
                objectName: "deviceInspector"
                anchors.fill: parent
                visible: !root.inspectorCollapsed
                deviceManager: root.deviceManager
                selectedDevice: root.selectedDevice
                onSelectedDeviceRequested: device => root.selectedDevice = device
                onCollapseRequested: root.toggleInspector()
                onConnectionsRequested: root.connectionsRequested()
                onSettingsRequested: device => root.settingsRequested(device)
                onGuideRequested: root.workspaceRequested(1)
                // Device errors already reach the global notification handler.
            }
            Rectangle {
                anchors.fill: parent
                visible: root.inspectorCollapsed
                color: AsterTheme.surface
                Column {
                    width: parent.width
                    spacing: 8
                    DesktopButton {
                        objectName: "restoreInspectorButton"
                        width: 32; height: 36
                        text: "‹"
                        font.pixelSize: 20
                        Accessible.name: "展开设备参数"
                        ToolTip.visible: hovered
                        ToolTip.text: "展开设备参数"
                        onClicked: root.toggleInspector()
                    }
                    DesktopButton {
                        objectName: "collapsedParametersButton"
                        width: 32; height: 32
                        glyph: "settings-ui"
                        Accessible.name: "设备参数"
                        ToolTip.visible: hovered
                        ToolTip.text: "设备参数"
                        onClicked: root.settingsRequested(root.selectedDevice)
                    }
                }
            }
        }
    }
}
