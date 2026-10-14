import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../controls"

Rectangle {
    id: root
    required property var deviceManager
    property int selectedDevice: 1
    readonly property var devices: deviceManager ? deviceManager.devices : []
    readonly property var cameraTelemetry: deviceManager && deviceManager.cameraTelemetry ? deviceManager.cameraTelemetry : ({})
    property bool loopCapture: false
    signal selectedDeviceRequested(int device)
    signal collapseRequested()
    signal connectionsRequested()
    signal settingsRequested(int device)
    signal guideRequested()
    signal noticeRequested(string message)
    color: AsterTheme.surface

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 8
            Layout.preferredHeight: 40
            Text {
                Layout.fillWidth: true
                text: "设备控制"
                color: AsterTheme.textPrimary
                font.family: AsterTheme.uiFont
                font.pixelSize: 12
            }
            DesktopButton {
                objectName: "collapseInspectorButton"
                implicitWidth: 28
                glyph: "chevron-right"
                Accessible.name: "收起设备控制"
                ToolTip.visible: hovered
                ToolTip.text: "收起设备控制"
                onClicked: root.collapseRequested()
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: AsterTheme.stroke }
        ScrollView {
            id: scroll
            objectName: "deviceSectionsScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Column {
                width: scroll.availableWidth
                Repeater {
                    model: [
                        {role: 1, title: "主相机", icon: "camera"},
                        {role: 3, title: "滤镜轮", icon: "filter"},
                        {role: 4, title: "电调焦", icon: "focuser"},
                        {role: 0, title: "赤道仪", icon: "mount"},
                        {role: 2, title: "导星相机", icon: "guide-camera"}
                    ]
                    delegate: Column {
                        id: section
                        required property var modelData
                        readonly property int role: modelData.role
                        readonly property var device: root.devices.length > role ? root.devices[role] : ({})
                        readonly property var wheelMetrics: device.metrics || {}
                        readonly property var focuserMetrics: device.metrics || {}
                        readonly property var mountMetrics: device.metrics || {}
                        readonly property string mountRaText: {
                            const ra = mountMetrics.ra
                            if (ra === undefined || ra === null) return "—"
                            const h = Math.floor(ra)
                            const m = Math.floor((ra - h) * 60)
                            const sec = Math.round(((ra - h) * 60 - m) * 60)
                            return h + "h " + String(m).padStart(2, "0") + "m " + String(sec).padStart(2, "0") + "s"
                        }
                        readonly property string mountDecText: {
                            const dec = mountMetrics.dec
                            if (dec === undefined || dec === null) return "—"
                            const sign = dec >= 0 ? "+" : "-"
                            const abs = Math.abs(dec)
                            const d = Math.floor(abs)
                            const m = Math.floor((abs - d) * 60)
                            const sec = Math.round(((abs - d) * 60 - m) * 60)
                            return sign + d + "° " + String(m).padStart(2, "0") + "′ " + String(sec).padStart(2, "0") + "″"
                        }
                        readonly property string mountStatusText: {
                            if (mountMetrics.atPark) return "已停放"
                            if (mountMetrics.slewing) return "转动中"
                            if (mountMetrics.tracking) return "跟踪中"
                            return mountMetrics.ra !== undefined ? "已连接·未跟踪" : "等待数据"
                        }
                        property int focusTarget: 0
                        readonly property string focusPositionText: {
                            const position = focuserMetrics.position
                            if (position === undefined || position === null) return "—"
                            return focuserMetrics.moving ? position + "（移动中）" : String(position)
                        }
                        readonly property string focusTemperatureText: {
                            const temperature = focuserMetrics.temperature
                            return temperature === undefined || temperature === null ? "—" : temperature.toFixed(1) + " °C"
                        }
                        readonly property var cameraMetrics: device.metrics || {}
                        function exposureText() {
                            return cameraMetrics.exposureUs !== undefined
                                ? parseFloat((cameraMetrics.exposureUs / 1000000).toFixed(3)).toString() : "—"
                        }
                        readonly property int wheelSlots: wheelMetrics.slots > 0 ? wheelMetrics.slots : 0
                        readonly property bool wheelBusy: !!wheelMetrics.moving || !!wheelMetrics.calibrating
                        readonly property string wheelPositionText: wheelMetrics.moving ? "移动中…"
                            : wheelMetrics.calibrating ? "校准中…"
                            : wheelMetrics.position >= 0 ? slotLabel(wheelMetrics.position + 1) : "—"
                        function channelSuffix(slot) {
                            const channels = root.deviceManager && root.deviceManager.filterWheelChannels
                                ? root.deviceManager.filterWheelChannels : []
                            for (let i = 0; i < channels.length; ++i)
                                if (channels[i].slot === slot) return String(channels[i].suffix || "")
                            return ""
                        }
                        function slotLabel(slot) {
                            const suffix = channelSuffix(slot)
                            return suffix ? slot + " · " + suffix : "#" + slot
                        }
                        readonly property var wheelTargetOptions: {
                            const options = []
                            const total = Math.max(wheelSlots, 1)
                            for (let i = 1; i <= total; ++i) options.push(slotLabel(i))
                            return options
                        }
                        readonly property bool connected: !!device.connected
                        property bool expanded: role === 1
                        property string guideExposure: "2"
                        property string guideGain: "68"
                        property int focusCoarseStep: 50
                        property int focusFineStep: 10
                        property int focusReference: -1
                        readonly property string focusDeviceId: device.selectedId || ""
                        onFocusDeviceIdChanged: focusReference = -1
                        onConnectedChanged: if (!connected) focusReference = -1
                        function focusMoveBy(delta) {
                            const position = focuserMetrics.position
                            const limit = focuserMetrics.maxStep
                            if (!connected || focuserMetrics.moving || typeof position !== "number"
                                || typeof limit !== "number" || !isFinite(position) || !isFinite(limit)) return
                            root.deviceManager.moveFocuser(Math.max(0, Math.min(limit, position + delta)))
                            if (root.deviceManager.lastError) root.noticeRequested(root.deviceManager.lastError)
                        }
                        property int wheelTarget: 1
                        objectName: "deviceSection" + role
                        width: parent.width
                        spacing: 0

                        RowLayout {
                            width: parent.width
                            height: 42
                            spacing: 0
                            Button {
                                id: sectionToggle
                                objectName: "deviceSectionToggle" + section.role
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                padding: 0
                                hoverEnabled: true
                                Accessible.name: (section.expanded ? "收起" : "展开") + section.modelData.title
                                onClicked: {
                                    section.expanded = !section.expanded
                                    root.selectedDeviceRequested(section.role)
                                }
                                background: Rectangle { color: sectionToggle.hovered ? AsterTheme.surfaceHover : AsterTheme.surface }
                                contentItem: RowLayout {
                                    spacing: 8
                                    Image {
                                        Layout.leftMargin: 10
                                        Layout.preferredWidth: 12; Layout.preferredHeight: 12
                                        source: section.expanded ? "../assets/icons/chevron-down.svg" : "../assets/icons/chevron-right.svg"
                                    }
                                    Image {
                                        Layout.preferredWidth: 16; Layout.preferredHeight: 16
                                        source: "../assets/icons/" + section.modelData.icon + ".svg"
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: section.modelData.title
                                        color: AsterTheme.textPrimary
                                        font.family: AsterTheme.uiFont
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        Layout.rightMargin: 4
                                        text: section.device.busy ? "处理中…" : section.device.error ? "连接异常" : section.connected ? "已连接" : "未连接"
                                        color: section.connected ? AsterTheme.accent : AsterTheme.textSecondary
                                        font.family: AsterTheme.uiFont
                                        font.pixelSize: 10
                                    }
                                }
                            }
                            DesktopButton {
                                objectName: "deviceSettings" + section.role
                                Layout.preferredWidth: 32
                                Layout.rightMargin: 4
                                glyph: "settings-ui"
                                Accessible.name: section.modelData.title + "完整设置"
                                ToolTip.visible: hovered
                                ToolTip.text: section.modelData.title + "完整设置"
                                onClicked: {
                                    root.selectedDeviceRequested(section.role)
                                    root.settingsRequested(section.role)
                                }
                            }
                        }

                        ColumnLayout {
                            width: parent.width - 24
                            x: 12
                            visible: section.expanded
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.bottomMargin: 10
                                visible: !section.connected
                                Text {
                                    Layout.fillWidth: true
                                    text: "连接后显示常用控制"
                                    color: AsterTheme.textSecondary
                                    font.family: AsterTheme.uiFont
                                    font.pixelSize: 11
                                }
                                DesktopButton {
                                    objectName: "deviceConnect" + section.role
                                    text: "连接设备"
                                    glyph: "device"
                                    onClicked: {
                                        root.selectedDeviceRequested(section.role)
                                        root.connectionsRequested()
                                    }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.bottomMargin: 12
                                visible: section.connected
                                spacing: 4
                                Text {
                                    Layout.fillWidth: true
                                    text: section.device.selectedName || "设备"
                                    color: AsterTheme.textSecondary
                                    font.family: AsterTheme.uiFont
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                                ColumnLayout {
                                    objectName: "cameraControls" + section.role
                                    Layout.fillWidth: true
                                    visible: section.role === 1 || section.role === 2
                                    spacing: 0
                                    DesktopField {
                                        objectName: section.role === 1 ? "cameraExposureField" : ""
                                        implicitHeight: 30; Layout.minimumHeight: 30
                                        Layout.fillWidth: true
                                        label: "曝光"; unit: "秒"; editable: true
                                        value: section.role === 1 ? section.exposureText() : section.guideExposure
                                        minimum: 0.001
                                        maximum: section.cameraMetrics.exposureMaxUs !== undefined ? section.cameraMetrics.exposureMaxUs / 1000000 : 3600
                                        onValueEdited: value => {
                                            if (section.role !== 1) { section.guideExposure = value; return }
                                            root.deviceManager.setCameraExposure(Number(value))
                                            if (root.deviceManager.lastError) root.noticeRequested(root.deviceManager.lastError)
                                        }
                                    }
                                    DesktopField {
                                        objectName: section.role === 1 ? "cameraGainField" : ""
                                        implicitHeight: 30; Layout.minimumHeight: 30
                                        Layout.fillWidth: true
                                        label: "增益"; editable: true
                                        value: section.role === 1 ? (section.cameraMetrics.gain !== undefined ? section.cameraMetrics.gain : "—") : section.guideGain
                                        minimum: section.cameraMetrics.gainMin !== undefined ? section.cameraMetrics.gainMin : 100
                                        maximum: section.cameraMetrics.gainMax !== undefined ? section.cameraMetrics.gainMax : 1000
                                        onValueEdited: value => {
                                            if (section.role !== 1) { section.guideGain = value; return }
                                            root.deviceManager.setCameraGain(Math.round(Number(value)))
                                            if (root.deviceManager.lastError) root.noticeRequested(root.deviceManager.lastError)
                                        }
                                    }
                                    DesktopField {
                                        objectName: "cameraOffsetField"
                                        implicitHeight: 30; Layout.minimumHeight: 30; Layout.fillWidth: true
                                        visible: section.role === 1 && section.cameraMetrics.canBlackLevel === true
                                        label: "偏置"; editable: true
                                        value: section.cameraMetrics.offset !== undefined ? section.cameraMetrics.offset : "—"
                                        minimum: 0
                                        maximum: section.cameraMetrics.offsetMax !== undefined ? section.cameraMetrics.offsetMax : 31
                                        onValueEdited: value => {
                                            root.deviceManager.setCameraOffset(Math.round(Number(value)))
                                            if (root.deviceManager.lastError) root.noticeRequested(root.deviceManager.lastError)
                                        }
                                    }
                                    RowLayout {
                                        objectName: "cameraBinningRow"
                                        Layout.fillWidth: true
                                        implicitHeight: 30
                                        visible: section.role === 1
                                        Text {
                                            Layout.fillWidth: true
                                            text: "合并像素"
                                            color: AsterTheme.textSecondary
                                            font.family: AsterTheme.uiFont
                                            font.pixelSize: 11
                                        }
                                        DesktopComboBox {
                                            id: binningCombo
                                            objectName: "cameraBinningCombo"
                                            Layout.preferredWidth: 100
                                            implicitHeight: 25
                                            model: ["1 × 1", "2 × 2", "3 × 3", "4 × 4"]
                                            currentIndex: section.cameraMetrics.binning !== undefined ? Math.min(Math.max(section.cameraMetrics.binning, 1), 4) - 1 : 0
                                            onActivated: index => {
                                                root.deviceManager.setCameraBinning(index + 1)
                                                if (root.deviceManager.lastError) root.noticeRequested(root.deviceManager.lastError)
                                            }
                                            Accessible.name: "合并像素"
                                            contentItem: Text {
                                                leftPadding: 8
                                                rightPadding: 20
                                                text: binningCombo.displayText
                                                color: AsterTheme.textPrimary
                                                font.family: AsterTheme.uiFont
                                                font.pixelSize: 11
                                                verticalAlignment: Text.AlignVCenter
                                                elide: Text.ElideRight
                                            }
                                            indicator: Image {
                                                x: binningCombo.width - 18
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: 12; height: 12
                                                source: "../assets/icons/chevron-down.svg"
                                            }
                                            background: Rectangle {
                                                radius: 3
                                                color: AsterTheme.canvas
                                                border.color: binningCombo.activeFocus ? AsterTheme.accent : AsterTheme.strokeStrong
                                            }
                                        }
                                        Item { Layout.preferredWidth: 18 }
                                    }
                                    DesktopField {
                                        implicitHeight: 30; Layout.minimumHeight: 30; Layout.fillWidth: true; visible: section.role === 1; label: "当前滤镜"; value: "—" }
                                    Rectangle { Layout.fillWidth: true; visible: section.role === 1; implicitHeight: 1; Layout.topMargin: 8; Layout.bottomMargin: 6; color: AsterTheme.stroke }
                                    DesktopField {
                                        objectName: "cameraCoolerField"
                                        implicitHeight: 30; Layout.minimumHeight: 30; Layout.fillWidth: true
                                        visible: section.role === 1 && section.cameraMetrics.canTec === true
                                        label: "制冷目标"; editable: true; unit: "°C"
                                        value: section.cameraMetrics.targetTemp !== undefined ? section.cameraMetrics.targetTemp : "—"
                                        minimum: section.cameraMetrics.tempMin !== undefined ? section.cameraMetrics.tempMin : -50
                                        maximum: section.cameraMetrics.tempMax !== undefined ? section.cameraMetrics.tempMax : 40
                                        onValueEdited: value => {
                                            root.deviceManager.setCameraTargetTemperature(Number(value))
                                            if (root.deviceManager.lastError) root.noticeRequested(root.deviceManager.lastError)
                                        }
                                    }
                                    Text {
                                        objectName: "cameraTempText"
                                        visible: section.role === 1 && section.cameraMetrics.canTemp === true
                                        text: "实际温度 " + (root.cameraTelemetry.currentTemp !== undefined ? Number(root.cameraTelemetry.currentTemp).toFixed(1) : "—")
                                              + " °C    功率 " + (root.cameraTelemetry.tecPower !== undefined ? Math.round(root.cameraTelemetry.tecPower) + "%" : "—")
                                        color: AsterTheme.textSecondary
                                        font.family: AsterTheme.uiFont
                                        font.pixelSize: 11
                                        Layout.bottomMargin: 10
                                    }
                                    RowLayout {
                                        visible: section.role === 1
                                        Layout.fillWidth: true
                                        DesktopButton {
                                            objectName: section.role === 1 ? "exposureButton" : ""
                                            Layout.fillWidth: true
                                            // NINA-style semantics: a plain click takes one
                                            // exposure; looping requires the checkbox.
                                            text: section.cameraMetrics.previewing ? "停止曝光"
                                                  : root.loopCapture ? "开始循环曝光" : "开始曝光"
                                            primary: true
                                            enabled: section.connected
                                            onClicked: {
                                                if (section.cameraMetrics.previewing) root.deviceManager.stopCameraPreview()
                                                else if (root.loopCapture) root.deviceManager.startCameraPreview()
                                                else root.deviceManager.captureSingleExposure()
                                                if (root.deviceManager.lastError) root.noticeRequested(root.deviceManager.lastError)
                                            }
                                        }
                                        DesktopButton {
                                            objectName: section.role === 1 ? "stopExposureButton" : ""
                                            text: "停止"
                                            enabled: section.role === 1 && !!section.cameraMetrics.previewing
                                            onClicked: {
                                                root.deviceManager.stopCameraPreview()
                                                if (root.deviceManager.lastError) root.noticeRequested(root.deviceManager.lastError)
                                            }
                                        }
                                    }
                                    CheckBox {
                                        id: loopToggle
                                        objectName: section.role === 1 ? "loopCaptureToggle" : ""
                                        visible: section.role === 1
                                        text: "循环曝光"
                                        checked: root.loopCapture
                                        onToggled: root.loopCapture = checked
                                        font.family: AsterTheme.uiFont
                                        font.pixelSize: 11
                                        palette.windowText: AsterTheme.textSecondary
                                        palette.base: AsterTheme.canvas
                                        palette.highlight: AsterTheme.accent
                                        padding: 0
                                        implicitHeight: 26
                                        indicator: Rectangle {
                                            implicitWidth: 14; implicitHeight: 14
                                            x: loopToggle.leftPadding
                                            y: (loopToggle.height - height) / 2
                                            radius: 2
                                            color: loopToggle.checked ? AsterTheme.accent : AsterTheme.canvas
                                            border.color: loopToggle.activeFocus ? AsterTheme.accent : AsterTheme.strokeStrong
                                            Text { anchors.centerIn: parent; text: "✓"; visible: loopToggle.checked; color: AsterTheme.accentText; font.family: AsterTheme.uiFont; font.pixelSize: 12 }
                                        }
                                    }
                                    DesktopField {
                                        implicitHeight: 30; Layout.minimumHeight: 30; Layout.fillWidth: true; visible: section.role === 2; label: "导星状态"; value: "等待数据" }
                                    DesktopButton { objectName: section.role === 2 ? "openGuideWorkspace" : ""; visible: section.role === 2; text: "打开导星页"; onClicked: root.guideRequested() }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: section.role === 0 || section.role === 3 || section.role === 4
                                    spacing: 0
                                    DesktopField {
                                        objectName: "wheelPositionField"
                                        implicitHeight: 30; Layout.minimumHeight: 30; Layout.fillWidth: true; label: section.role === 3 ? "当前滤镜" : "当前位置"; value: section.role === 3 ? section.wheelPositionText : section.role === 4 ? section.focusPositionText : "—" }
                                    DesktopField {
                                        objectName: "focusTemperatureField"
                                        implicitHeight: 30; Layout.minimumHeight: 30; Layout.fillWidth: true; visible: section.role === 4; label: "温度"; value: section.focusTemperatureText }
                                    DesktopField {
                                        objectName: "focusMaxStepField"
                                        implicitHeight: 30; Layout.minimumHeight: 30; Layout.fillWidth: true; visible: section.role === 4
                                        label: "行程上限"; editable: true; value: section.focuserMetrics.maxStep !== undefined ? String(section.focuserMetrics.maxStep) : "—"
                                        minimum: 1; maximum: 1000000
                                        onValueEdited: value => {
                                            const maxStep = Math.round(Number(value))
                                            if (maxStep > 0) root.deviceManager.setFocuserMaxStep(maxStep)
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        visible: section.role === 4
                                        spacing: 4
                                        DesktopField {
                                            objectName: "focusTargetField"
                                            implicitHeight: 30; Layout.minimumHeight: 30
                                            Layout.fillWidth: true
                                            label: "目标位置"; editable: true
                                            value: String(section.focusTarget)
                                            minimum: 0; maximum: Math.max(section.focuserMetrics.maxStep !== undefined ? section.focuserMetrics.maxStep : 0, 1)
                                            onValueEdited: value => section.focusTarget = Math.round(Number(value))
                                        }
                                        DesktopButton {
                                            objectName: "focusGoToButton"
                                            implicitHeight: 30
                                            text: "转到"
                                            enabled: section.connected && !section.focuserMetrics.moving
                                            onClicked: root.deviceManager.moveFocuser(section.focusTarget)
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        visible: section.role === 4
                                    DesktopButton {
                                        objectName: "focusZeroButton"
                                        Layout.fillWidth: true
                                        implicitHeight: 30
                                        visible: section.role === 4
                                        text: "设为零位"
                                        enabled: section.connected && !section.focuserMetrics.moving && section.focuserMetrics.position > 0
                                        onClicked: { section.focusReference = -1; root.deviceManager.resetFocuserPosition() }
                                    }
                                    DesktopButton {
                                        objectName: "focusSetCurrentButton"
                                        Layout.fillWidth: true
                                        implicitHeight: 30
                                        visible: section.role === 4
                                        text: "当前位置设为 " + section.focusTarget
                                        enabled: section.connected && !section.focuserMetrics.moving && section.focusTarget >= 0
                                        onClicked: { section.focusReference = -1; root.deviceManager.setCurrentFocuserPosition(section.focusTarget) }
                                    }
                                    }
                                    // NINA-style mount info block: prominent readouts.
                                    Rectangle {
                                        objectName: "mountInfoBlock"
                                        Layout.fillWidth: true
                                        visible: section.role === 0 && section.connected
                                        Layout.preferredHeight: 96
                                        radius: AsterTheme.radiusSmall
                                        color: AsterTheme.canvas
                                        border.color: AsterTheme.stroke
                                        GridLayout {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            columns: 2
                                            columnSpacing: 12
                                            rowSpacing: 4
                                            Text { text: "赤经"; color: AsterTheme.textMuted; font.pixelSize: 10 }
                                            Text { text: section.mountRaText; color: AsterTheme.textPrimary; font.family: AsterTheme.monoFont; font.pixelSize: 13; font.bold: true }
                                            Text { text: "赤纬"; color: AsterTheme.textMuted; font.pixelSize: 10 }
                                            Text { text: section.mountDecText; color: AsterTheme.textPrimary; font.family: AsterTheme.monoFont; font.pixelSize: 13; font.bold: true }
                                            Text { text: "状态"; color: AsterTheme.textMuted; font.pixelSize: 10 }
                                            Text { text: section.mountStatusText; color: section.mountMetrics.tracking ? AsterTheme.statusOk : AsterTheme.textSecondary; font.pixelSize: 11 }
                                        }
                                    }
                                    DesktopField {
                                        objectName: "mountAltField"
                                        implicitHeight: 30; Layout.minimumHeight: 30; Layout.fillWidth: true; visible: section.role === 0 && section.connected
                                        label: "地平高度"; value: section.mountMetrics.altitude !== undefined ? section.mountMetrics.altitude.toFixed(1) + "°" : "—" }
                                    DesktopField {
                                        objectName: "mountSiderealField"
                                        implicitHeight: 30; Layout.minimumHeight: 30; Layout.fillWidth: true; visible: section.role === 0 && section.connected
                                        label: "恒星时"; value: {
                                            const st = section.mountMetrics.siderealTime
                                            if (st === undefined || st === null) return "—"
                                            const h = Math.floor(st)
                                            const m = Math.round((st - h) * 60)
                                            return h + "h " + String(m).padStart(2, "0") + "m"
                                        } }
                                    DesktopField {
                                        objectName: "mountSiteField"
                                        implicitHeight: 30; Layout.minimumHeight: 30; Layout.fillWidth: true; visible: section.role === 0 && section.connected
                                        label: "站点"; value: {
                                            const lat = section.mountMetrics.siteLatitude
                                            const lon = section.mountMetrics.siteLongitude
                                            if (lat === undefined) return "—"
                                            return lat.toFixed(2) + "° / " + (lon !== undefined ? lon.toFixed(2) : "—") + "°"
                                        } }
                                    MountManualControl {
                                        Layout.fillWidth: true
                                        visible: section.role === 0 && section.connected
                                        deviceManager: root.deviceManager
                                        connected: section.connected
                                        metrics: section.mountMetrics
                                        onNoticeRequested: message => root.noticeRequested(message)
                                    }
                                    RowLayout {
                                        objectName: section.role === 3 ? "wheelTargetRow" : ""
                                        Layout.fillWidth: true
                                        implicitHeight: 30
                                        visible: section.role === 3
                                        Text {
                                            Layout.fillWidth: true
                                            text: "目标滤镜"
                                            color: AsterTheme.textSecondary
                                            font.family: AsterTheme.uiFont
                                            font.pixelSize: 11
                                        }
                                        ComboBox {
                                            id: wheelTargetCombo
                                            objectName: section.role === 3 ? "wheelTargetCombo" : ""
                                            Layout.preferredWidth: 100
                                            implicitHeight: 25
                                            enabled: section.connected && !section.wheelBusy
                                            model: section.wheelTargetOptions
                                            currentIndex: Math.min(Math.max(section.wheelTarget - 1, 0), Math.max(section.wheelSlots, 1) - 1)
                                            onActivated: index => section.wheelTarget = index + 1
                                            Accessible.name: "目标滤镜"
                                            contentItem: Text {
                                                leftPadding: 8
                                                rightPadding: 20
                                                text: wheelTargetCombo.displayText
                                                color: wheelTargetCombo.enabled ? AsterTheme.textPrimary : AsterTheme.textDisabled
                                                font.family: AsterTheme.uiFont
                                                font.pixelSize: 11
                                                verticalAlignment: Text.AlignVCenter
                                                elide: Text.ElideRight
                                            }
                                            indicator: Image {
                                                x: wheelTargetCombo.width - 18
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: 12; height: 12
                                                source: "../assets/icons/chevron-down.svg"
                                                opacity: wheelTargetCombo.enabled ? 1 : 0.4
                                            }
                                            background: Rectangle {
                                                radius: 3
                                                color: AsterTheme.canvas
                                                border.color: wheelTargetCombo.activeFocus ? AsterTheme.accent : AsterTheme.strokeStrong
                                            }
                                            delegate: ItemDelegate {
                                                id: wheelTargetOption
                                                width: wheelTargetCombo.width
                                                height: 28
                                                required property int index
                                                contentItem: Text {
                                                    text: wheelTargetCombo.textAt(wheelTargetOption.index)
                                                    color: AsterTheme.textPrimary
                                                    font.family: AsterTheme.uiFont
                                                    font.pixelSize: 11
                                                    verticalAlignment: Text.AlignVCenter
                                                }
                                                background: Rectangle {
                                                    color: wheelTargetOption.highlighted ? AsterTheme.accentSurface
                                                         : wheelTargetOption.hovered ? AsterTheme.surfaceHover : AsterTheme.canvasRaised
                                                }
                                                highlighted: wheelTargetCombo.highlightedIndex === wheelTargetOption.index
                                            }
                                            popup: Popup {
                                                y: wheelTargetCombo.height + 2
                                                width: wheelTargetCombo.width
                                                padding: 1
                                                implicitHeight: Math.min(contentItem.implicitHeight + 2, 220)
                                                contentItem: ListView {
                                                    clip: true
                                                    implicitHeight: contentHeight
                                                    model: wheelTargetCombo.popup.visible ? wheelTargetCombo.delegateModel : null
                                                    currentIndex: wheelTargetCombo.highlightedIndex
                                                }
                                                background: Rectangle {
                                                    color: AsterTheme.canvasRaised
                                                    border.color: AsterTheme.strokeStrong
                                                    radius: AsterTheme.radiusSmall
                                                }
                                            }
                                        }
                                        Item { Layout.preferredWidth: 18 }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        visible: section.role === 4
                                        DesktopButton {
                                            objectName: section.role === 4 ? "focusSetReference" : ""
                                            Layout.fillWidth: true
                                            text: "设置零点"
                                            enabled: section.connected && !section.focuserMetrics.moving
                                                && typeof section.focuserMetrics.position === "number"
                                            ToolTip.visible: hovered
                                            ToolTip.text: "记录当前位置为本次连接的参考点，不改变设备坐标"
                                            onClicked: section.focusReference = section.focuserMetrics.position
                                        }
                                        DesktopButton {
                                            objectName: section.role === 4 ? "focusReturnReference" : ""
                                            Layout.fillWidth: true
                                            text: "回到零点"
                                            enabled: section.connected && !section.focuserMetrics.moving && section.focusReference >= 0
                                                && section.focusReference <= section.focuserMetrics.maxStep
                                            ToolTip.visible: hovered
                                            ToolTip.text: section.focusReference >= 0 ? "返回参考位置 " + section.focusReference : "请先设置参考点"
                                            onClicked: root.deviceManager.moveFocuser(section.focusReference)
                                        }
                                        DesktopButton {
                                            objectName: section.role === 4 ? "focusHaltButton" : ""
                                            text: "停止"
                                            enabled: section.connected
                                            onClicked: root.deviceManager.haltFocuser()
                                        }
                                    }
                                    Repeater {
                                        model: section.role === 4 ? 2 : 0
                                        delegate: RowLayout {
                                            id: focusAdjust
                                            required property int index
                                            readonly property bool coarse: index === 0
                                            readonly property int step: coarse ? section.focusCoarseStep : section.focusFineStep
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Text { text: focusAdjust.coarse ? "粗调" : "细调"; color: AsterTheme.textSecondary; font.pixelSize: 11 }
                                            TextField {
                                                objectName: focusAdjust.coarse ? "focusCoarseStep" : "focusFineStep"
                                                Layout.preferredWidth: 56; Layout.preferredHeight: 25
                                                text: String(focusAdjust.step)
                                                color: AsterTheme.textPrimary
                                                font.family: AsterTheme.monoFont; font.pixelSize: 11
                                                horizontalAlignment: Text.AlignRight
                                                selectByMouse: true
                                                validator: IntValidator { bottom: 1; top: 60000 }
                                                onEditingFinished: {
                                                    if (acceptableInput) {
                                                        if (focusAdjust.coarse) section.focusCoarseStep = Number(text)
                                                        else section.focusFineStep = Number(text)
                                                    }
                                                    text = String(focusAdjust.step)
                                                }
                                                background: Rectangle { color: AsterTheme.canvas; radius: 3; border.color: parent.activeFocus ? AsterTheme.accent : AsterTheme.strokeStrong }
                                            }
                                            Text { text: "步"; color: AsterTheme.textMuted; font.pixelSize: 10 }
                                            Item { Layout.fillWidth: true }
                                            DesktopButton {
                                                objectName: focusAdjust.coarse ? "focusCoarseIn" : "focusFineIn"
                                                text: "−"
                                                Accessible.name: (focusAdjust.coarse ? "粗调" : "细调") + "向内"
                                                enabled: section.connected && !section.focuserMetrics.moving && section.focuserMetrics.position > 0
                                                onClicked: section.focusMoveBy(-focusAdjust.step)
                                            }
                                            DesktopButton {
                                                objectName: focusAdjust.coarse ? "focusCoarseOut" : "focusFineOut"
                                                text: "+"
                                                Accessible.name: (focusAdjust.coarse ? "粗调" : "细调") + "向外"
                                                enabled: section.connected && !section.focuserMetrics.moving && section.focuserMetrics.position < section.focuserMetrics.maxStep
                                                onClicked: section.focusMoveBy(focusAdjust.step)
                                            }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        visible: section.role === 4
                                        CheckBox {
                                            id: focusReverseToggle
                                            objectName: "focusReverseToggle"
                                            checked: !!section.focuserMetrics.reversed
                                            enabled: section.connected && !section.focuserMetrics.moving
                                            text: "方向反转"
                                            onToggled: root.deviceManager.setFocuserReversed(checked)
                                            font.pixelSize: 11
                                            padding: 0
                                            implicitHeight: 22
                                            spacing: 5
                                            contentItem: Text {
                                                text: focusReverseToggle.text
                                                leftPadding: 19
                                                color: AsterTheme.textSecondary
                                                font.pixelSize: 11
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                            indicator: Rectangle {
                                                width: 13; height: 13
                                                y: (focusReverseToggle.height - height) / 2
                                                radius: 1
                                                color: focusReverseToggle.checked ? AsterTheme.accent : AsterTheme.canvas
                                                border.color: focusReverseToggle.activeFocus ? AsterTheme.accent : AsterTheme.strokeStrong
                                                Text { anchors.centerIn: parent; text: "✓"; visible: focusReverseToggle.checked; color: AsterTheme.accentText; font.pixelSize: 11 }
                                            }
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.topMargin: 6
                                        visible: section.role === 3
                                        Repeater {
                                            model: section.role === 0 ? [section.mountMetrics.tracking ? "停止跟踪" : "开始跟踪", section.mountMetrics.atPark ? "唤醒" : "停放", "停止"] : section.role === 3 ? ["切换滤镜", "校准"] : ["向内", "向外", "停止"]
                                            delegate: DesktopButton {
                                                required property string modelData
                                                required property int index
                                                readonly property bool mountAction: section.role === 0
                                                readonly property bool filterAction: section.role === 3
                                                readonly property bool focusAction: section.role === 4
                                                objectName: filterAction ? (index === 0 ? "filterSwitchButton" : "filterCalibrateButton")
                                                         : ""
                                                Layout.fillWidth: true
                                                text: modelData
                                                enabled: filterAction ? (section.connected && !section.wheelBusy)
                                                     : focusAction ? (section.connected && (index === 2 || !section.focuserMetrics.moving))
                                                     : false
                                                onClicked: {
                                                    if (filterAction) {
                                                        const target = Math.min(Math.max(section.wheelTarget, 1), Math.max(section.wheelSlots, 1)) - 1
                                                        if (index === 0) root.deviceManager.moveFilterWheel(target)
                                                        else root.deviceManager.calibrateFilterWheel()
                                                        return
                                                    }
                                                    if (!focusAction) return
                                                    const position = section.focuserMetrics.position
                                                    const step = section.focusFineStep
                                                    if (index === 0) root.deviceManager.moveFocuser(Math.max(0, position - step))
                                                    else if (index === 1) root.deviceManager.moveFocuser(position + step)
                                                    else root.deviceManager.haltFocuser()
                                                    if (root.deviceManager.lastError) root.noticeRequested(root.deviceManager.lastError)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        Rectangle { width: parent.width; height: 1; color: AsterTheme.stroke }
                    }
                }
            }
        }
    }
}
