import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import StellaMora.State 1.0
import "theme"
import "controls"
import "shell"
import "pages"
import "panels"

ApplicationWindow {
    id: root
    width: 1440
    height: 900
    minimumWidth: 1180
    minimumHeight: 680
    visible: true
    color: AsterTheme.canvas
    title: "StellaMora"
    flags: Qt.Window | Qt.FramelessWindowHint
    topPadding: 0
    leftPadding: 0
    rightPadding: 0
    property bool chromeVisible: true
    Binding {
        target: DeviceManager
        property: "stillTargetName"
        value: AstroPlanner.selectedTarget.id || ""
    }
    property var staticHistogramBins: []
    // Live histogram from the camera preview once frames arrive; the bundled
    // sample image histogram serves as the initial display.
    readonly property var previewHistogramBins: DeviceManager.previewHistogram.length > 0
                                                ? DeviceManager.previewHistogram : staticHistogramBins

    function workspaceName() {
        if (AppState.workspace === AppState.Sequence)
            return "序列"
        if (AppState.workspace === AppState.Guide)
            return "导星"
        if (AppState.workspace === AppState.SkyMap)
            return "星体图"
        return "主控"
    }

    function toggleChrome() {
        if (AppState.workspace === AppState.Capture
                && AppState.overlay === AppState.NoOverlay)
            chromeVisible = !chromeVisible
    }

    Connections {
        target: AppState
        function onWorkspaceChanged() {
            if (AppState.workspace !== AppState.Capture)
                root.chromeVisible = true
        }
        function onOverlayChanged() {
            if (AppState.overlay !== AppState.NoOverlay)
                root.chromeVisible = true
        }
    }

    Connections {
        target: DeviceManager
        // 1 Hz 的赤道仪状态轮询同样走 devicesChanged：快照对比后无变化
        // 不弹提示，只有真实的连接/断开/掉线转换才通知。
        function onDevicesChanged() { root.notifyDeviceChanges() }
    }

    // 设备连接状态变化 → 右下角提示（NINA 同款行为）。
    property var connectedSnapshot: ({})
    function notifyDeviceChanges() {
        const next = {}
        for (const device of DeviceManager.devices) {
            // 断开时 selectedName 已被清空，名称要取自上一次快照
            const name = (device.selectedName || "").replace(/\s*\[[^\]]*\]$/, "") || device.title
            next[device.role] = {connected: device.connected, name: name, error: device.error || ""}
            const previous = connectedSnapshot[device.role]
            if (previous && previous.connected !== device.connected && !device.error)
                AppState.showNotification(device.connected ? AppState.Information : AppState.Warning,
                                          device.connected ? "设备已连接" : "设备已断开",
                                          device.title + " · " + (device.connected ? name : previous.name)
                                          // 掉线时带上驱动报告的原因（如"赤道仪连接已丢失且
                                          // 无法恢复（ASCOM 驱动报告未连接）"），完整告警走弹窗。
                                          + (!device.connected && device.error ? "：" + device.error : ""))
            // Device errors are routed once through DeviceManager.lastErrorChanged.
        }
        connectedSnapshot = next
    }

    // Scan once at startup so device lists are ready before the connections dialog opens.
    Component.onCompleted: {
        notifyDeviceChanges()
        DeviceManager.refreshDevices()
    }

    Item {
        id: stage
        anchors.fill: parent
        clip: true

        DesktopWorkspace {
            anchors.fill: parent
            anchors.topMargin: root.chromeVisible ? 44 : 0
            anchors.bottomMargin: root.chromeVisible ? 24 : 0
            deviceManager: DeviceManager
            planner: AstroPlanner
            guider: GuideProcessor
            sequenceController: SequenceController
            plateSolver: PlateSolver
            centeringController: CenteringController
            autoFocusController: AutoFocusController
            histogramBins: root.previewHistogramBins
            workspace: AppState.workspace
            immersive: !root.chromeVisible
            onWorkspaceRequested: workspace => AppState.workspace = workspace
            onConnectionsRequested: AppState.openOverlay(AppState.DeviceConnections)
            onSettingsRequested: device => AppState.openDeviceSettings(device)
            onUnavailableRequested: feature => AppState.showNotification(AppState.Information, "提示", feature.length > 15 ? feature : feature + "功能尚未接入，当前保留入口。")
            onNoticeRequested: message => AppState.showNotification(AppState.Warning, "操作提示", message)
            onImmersiveRequested: root.toggleChrome()
        }

        Item {
            id: chromeLayer
            objectName: "chromeLayer"
            anchors.fill: parent
            visible: root.chromeVisible
            TopBar {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 44
                windowMaximized: root.visibility === Window.Maximized
                workspaceTitle: root.workspaceName()
                deviceSummary: DeviceManager.statusSummary
                targetName: AstroPlanner.selectedTarget.id
                            ? AstroPlanner.selectedTarget.id + " · " + AstroPlanner.selectedTarget.name
                            : "未选择目标"
                selectedTarget: AstroPlanner.selectedTarget.id ? AstroPlanner.selectedTarget : null
                onMoveRequested: root.startSystemMove()
                onMinimizeRequested: root.showMinimized()
                onToggleMaximizeRequested: {
                    if (root.visibility === Window.Maximized)
                        root.showNormal()
                    else
                        root.showMaximized()
                }
                onCloseRequested: root.close()
                onDeviceConnectionsRequested: AppState.openOverlay(AppState.DeviceConnections)
                onProcessingRequested: AppState.showNotification(AppState.Information, "通知", "暂无设备事件。")
                onPlanRequested: AppState.workspace = AppState.Sequence
            }
            StatusBar {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 24
                deviceSummary: DeviceManager.statusSummary
            }
        }
        DesktopNotificationWindow {
            appState: AppState
        }

        WindowResizeHandles {
            anchors.fill: parent
            hostWindow: root
            enabled: root.visibility === Window.Windowed
            z: 100
        }

        Rectangle {
            anchors.fill: parent
            visible: AppState.overlay !== AppState.NoOverlay
            color: AsterTheme.scrim
            z: 20
            MouseArea { anchors.fill: parent; onClicked: AppState.closeOverlay() }
        }

        Loader {
            id: deviceConnectionsLoader
            anchors.centerIn: parent
            width: Math.min(640, parent.width - 64)
            height: 410
            active: AppState.overlay === AppState.DeviceConnections
            visible: active
            z: 21
            sourceComponent: DeviceConnectionsPopup {
                anchors.fill: parent
                deviceManager: DeviceManager
                onCloseRequested: AppState.closeOverlay()
                onNoticeRequested: message => AppState.showNotification(AppState.Warning, "设备搜索", message)
            }
        }

        Loader {
            id: deviceSettingsLoader
            anchors.centerIn: parent
            width: Math.min(item ? item.preferredDialogWidth : 860, parent.width - 64)
            height: Math.min(item ? item.preferredDialogHeight : 620, parent.height - 64)
            active: AppState.overlay === AppState.DeviceSettings
            visible: active
            z: 21
            sourceComponent: DeviceSettingsDrawer {
                anchors.fill: parent
                selectedDevice: AppState.settingsDevice
                deviceManager: DeviceManager
                autoFocusController: AutoFocusController
                planner: AstroPlanner
                onConnectionsRequested: AppState.openOverlay(AppState.DeviceConnections)
                onCloseRequested: AppState.closeOverlay()
                onSelectedDeviceRequested: device => AppState.openDeviceSettings(device)
            }
        }
    }

    Shortcut { sequence: "F11"; onActivated: root.toggleChrome() }

    Shortcut {
        sequence: "Escape"
        onActivated: { AppState.closeOverlay(); root.chromeVisible = true }
    }
}
