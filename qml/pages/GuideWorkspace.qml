import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"
import "../theme"

// Guiding workbench: central camera view, bottom drift chart with a drag
// splitter, right-side guide-specific panel (the generic all-devices
// inspector stays hidden on this page). Phase two: image analysis and
// virtual pulses only — the output chip says so and real guiding stays
// disabled until a pulse sink (ST-4 or mount) is linked.
Rectangle {
    id: root
    property var deviceManager: null
    property var guider: null
    readonly property bool loopActive: guideConnected && !!guideMetrics.previewing
    signal noticeRequested(string message)
    property real chartHeight: 200
    property real imageZoom: 0          // 0 = fit pane; >0 = preview-pixel scale
    property bool editRaParameters: true
    signal connectionsRequested()
    readonly property var guideDevice: {
        const devices = deviceManager ? deviceManager.devices : []
        return devices.length > 2 ? devices[2] : {}
    }
    readonly property bool guideConnected: !!guideDevice.connected
    readonly property var guideMetrics: guideDevice.metrics || {}
    readonly property bool hasImage: guideConnected && deviceManager && deviceManager.guidePreviewCounter > 0
    readonly property double frameScale: guideImageItem.width / Math.max(guider ? guider.frameWidth : 0, 1)
    readonly property double lockX: guider ? guider.starX - guider.errorX : 0
    readonly property double lockY: guider ? guider.starY - guider.errorY : 0

    function report(ok) {
        if (!ok && !deviceManager.lastError) noticeRequested("操作未完成，请检查设备状态")
    }
    onGuideConnectedChanged: {
        if (!guideConnected && guider) { guider.stopGuiding(); guider.clearStar() }
    }

    function stateText() {
        if (!guideConnected) return "相机未连接"
        if (!hasImage) return "相机已连接 · 点击循环曝光"
        if (guider && guider.guiding) return "模拟导星中"
        if (guider && guider.locked) return "已锁定 · 测量偏移"
        if (loopActive) return "循环曝光 · 未选星"
        return "有图像 · 循环已停"
    }

    color: "#11171c"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Compact toolbar in working order ────────────────────────────
        RowLayout {
            objectName: "guideToolbar"
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.preferredHeight: 40
            spacing: 8
            DesktopButton {
                objectName: "guideLoopButton"
                text: root.loopActive ? "停止曝光" : "循环曝光"
                primary: root.guideConnected && !root.loopActive
                glyph: "guide-camera"
                enabled: root.guideConnected
                onClicked: {
                    if (root.loopActive) {
                        if (guider) guider.stopGuiding()
                        root.report(deviceManager.stopGuideLoop())
                    } else root.report(deviceManager.startGuideLoop())
                }
            }
            DesktopButton {
                objectName: "guideAutoSelectButton"
                text: "自动选星"
                enabled: root.hasImage
                onClicked: if (guider) {
                    guider.stopGuiding()
                    if (!guider.autoSelect()) root.noticeRequested("未找到有效星点，请检查曝光、对焦和星场")
                }
            }
            DesktopButton {
                objectName: "guideCalibrateButton"
                text: "高级参数"
                checked: guideAdvancedBody.visible
                onClicked: guideAdvancedBody.visible = !guideAdvancedBody.visible
            }
            DesktopButton {
                objectName: "startGuidingButton"
                text: "开始导星"
                primary: true
                enabled: guider && guider.calibrated && guider.locked && guider.frameCount > 0 && guider.outputLinked
                onClicked: if (guider) guider.startGuiding()
            }
            DesktopButton {
                objectName: "stopGuidingButton"
                text: "停止"
                enabled: (guider && guider.guiding) || root.loopActive
                onClicked: {
                    // One stop ends everything: analysis and exposure. The
                    // loop stop is unconditional — a stale loopActive flag
                    // must never keep the shutter running.
                    if (guider) guider.stopGuiding()
                    if (deviceManager && typeof deviceManager.stopGuideLoop === "function")
                        deviceManager.stopGuideLoop()
                }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                objectName: "guideStateChip"
                radius: 3
                Layout.preferredHeight: 22
                Layout.preferredWidth: stateChipText.implicitWidth + 14
                color: guider && guider.locked ? "#12321f" : "#1b242b"
                border.color: guider && guider.locked ? AsterTheme.statusOk : AsterTheme.stroke
                Text {
                    id: stateChipText
                    anchors.centerIn: parent
                    text: root.stateText()
                    color: guider && guider.locked ? AsterTheme.statusOk : AsterTheme.textSecondary
                    font.pixelSize: 11
                }
            }
            Text {
                objectName: "guideOutputStatus"
                text: guider && guider.outputLinked ? "输出已接入" : "输出未接入"
                color: guider && guider.outputLinked ? AsterTheme.statusOk : AsterTheme.danger
                font.pixelSize: 11
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: AsterTheme.stroke }

        // ── Body: image column + right panel ────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    id: guideImagePane
                    objectName: "guideImagePane"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#0a1014"
                    border.color: AsterTheme.stroke
                    clip: true

                    Flickable {
                        id: imageFlick
                        anchors.fill: parent
                        anchors.margins: 4
                        contentWidth: Math.max(width, guideImageItem.width)
                        contentHeight: Math.max(height, guideImageItem.height)
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        Image {
                            id: guideImageItem
                            objectName: "guidePreviewImage"
                            anchors.centerIn: parent
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            source: root.hasImage
                                    ? "image://live/guide?" + deviceManager.guidePreviewCounter : ""
                            // Fit sizes to the pane; zoom keeps preview
                            // pixels (the 720-wide display stream — sensor
                            // 1:1 belongs to the raw-view pipeline).
                            width: root.imageZoom > 0 ? implicitWidth * root.imageZoom
                                   : Math.min(imageFlick.width, imageFlick.height
                                              * (guider && guider.frameWidth > 0
                                                 ? guider.frameWidth / guider.frameHeight : 4 / 3))
                            height: width / (guider && guider.frameWidth > 0
                                             ? guider.frameWidth / guider.frameHeight : 4 / 3)

                            MouseArea {
                                anchors.fill: parent
                                onClicked: mouse => {
                                    if (!root.hasImage || !guider || guider.frameCount === 0) return
                                    guider.stopGuiding()
                                    if (!guider.selectStarAt(mouse.x / root.frameScale, mouse.y / root.frameScale))
                                        root.noticeRequested("此处未找到有效星点")
                                }
                            }

                            // Search window around the lock position.
                            Rectangle {
                                visible: guider && guider.state === "locked"
                                x: root.lockX * root.frameScale - 15 * root.frameScale
                                y: root.lockY * root.frameScale - 15 * root.frameScale
                                width: 30 * root.frameScale
                                height: 30 * root.frameScale
                                color: "transparent"
                                border.color: "#3d5a6e"
                                border.width: 1
                            }
                            // Lock reference (fixed) — green.
                            Item {
                                visible: guider && guider.state === "locked"
                                x: root.lockX * root.frameScale - 9
                                y: root.lockY * root.frameScale - 9
                                width: 18; height: 18
                                Rectangle { width: parent.width; height: 1; color: "#62d08a"; anchors.centerIn: parent }
                                Rectangle { width: 1; height: parent.height; color: "#62d08a"; anchors.centerIn: parent }
                            }
                            // Current star position — accent.
                            Item {
                                visible: guider && guider.state === "locked"
                                x: (guider ? guider.starX : 0) * root.frameScale - 9
                                y: (guider ? guider.starY : 0) * root.frameScale - 9
                                width: 18; height: 18
                                Rectangle { width: parent.width; height: 1; color: AsterTheme.accent; anchors.centerIn: parent }
                                Rectangle { width: 1; height: parent.height; color: AsterTheme.accent; anchors.centerIn: parent }
                            }
                        }
                    }

                    RowLayout {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 8
                        spacing: 4
                        DesktopButton {
                            text: "适合"
                            implicitWidth: 42
                            onClicked: root.imageZoom = 0
                        }
                        DesktopButton {
                            text: "预览 1:1"
                            implicitWidth: 70
                            onClicked: root.imageZoom = 1
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: !root.hasImage
                        text: !root.guideConnected ? "连接导星相机后开始图像分析" : root.loopActive ? "正在曝光，等待图像…" : "相机已连接 · 点击「循环曝光」开始取帧"
                        color: AsterTheme.textPrimary
                        font.pixelSize: 13
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.NoButton
                        onWheel: wheel => {
                            if (!root.hasImage) return
                            const factor = wheel.angleDelta.y > 0 ? 1.2 : 1 / 1.2
                            const current = root.imageZoom === 0 ? guideImageItem.width / Math.max(1, guideImageItem.implicitWidth) : root.imageZoom
                            root.imageZoom = Math.min(8, Math.max(0.05, current * factor))
                        }
                    }
                }

                // Draggable splitter over the chart.
                Rectangle {
                    objectName: "guideChartSplitter"
                    Layout.fillWidth: true
                    height: 6
                    color: AsterTheme.canvas
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.SizeVerCursor
                        property real dragOffset
                        property real startHeight
                        onPressed: mouse => { dragOffset = mapToItem(root, 0, mouse.y).y; startHeight = root.chartHeight }
                        onPositionChanged: mouse => {
                            if (!pressed) return
                            root.chartHeight = Math.max(120,
                                Math.min(root.height * 0.6, startHeight - (mapToItem(root, 0, mouse.y).y - dragOffset)))
                        }
                    }
                }

                Rectangle {
                    id: guideChartPane
                    objectName: "guideChartPane"
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.chartHeight
                    color: "#0a1014"
                    border.color: AsterTheme.stroke

                    Canvas {
                        id: driftCurve
                        objectName: "guideDriftCurve"
                        anchors.fill: parent
                        anchors.margins: 10
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            const points = guider ? guider.curve : []
                            const top = 16
                            ctx.fillStyle = "#818b94"
                            ctx.font = "10px sans-serif"
                            ctx.fillText("dX/dY 偏移（像素）· 最近 " + points.length + " 帧 · 断点为丢星", 4, top - 6)
                            ctx.strokeStyle = "#2a3238"
                            ctx.beginPath(); ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke()
                            if (points.length < 2) return
                            let peak = 2.0
                            for (const point of points)
                                peak = Math.max(peak, Math.abs(point.dx), Math.abs(point.dy))
                            ctx.fillStyle = "#818b94"
                            ctx.fillText("±" + peak.toFixed(1), 4, top + 6)
                            const px = i => 4 + i / (points.length - 1) * (width - 8)
                            const py = v => top + (height - top - 6) / 2 - v / peak * (height - top - 6) / 2
                            for (const [color, key, legend] of [["#62a8d2", "dx", "dX"], ["#d2a86a", "dy", "dY"]]) {
                                ctx.strokeStyle = color
                                ctx.lineWidth = 1.4
                                ctx.beginPath()
                                let previousFrame = -1
                                for (let i = 0; i < points.length; ++i) {
                                    // Frame-index gaps from star loss break the
                                    // line instead of bridging with fake data.
                                    const gap = previousFrame >= 0 && points[i].f - previousFrame > 1
                                    if (i === 0 || gap) ctx.moveTo(px(i), py(points[i][key]))
                                    else ctx.lineTo(px(i), py(points[i][key]))
                                    previousFrame = points[i].f
                                }
                                ctx.stroke()
                                ctx.fillStyle = color
                                ctx.fillText(legend, key === "dx" ? width - 44 : width - 22, top - 6)
                            }
                        }
                        Connections {
                            target: guider
                            ignoreUnknownSignals: true
                            function onStarChanged() { driftCurve.requestPaint() }
                        }
                    }
                }
            }

            // ── Guide-specific right panel ──────────────────────────────
            Rectangle {
                id: guideInspector
                objectName: "guideInspector"
                Layout.preferredWidth: 300
                Layout.minimumWidth: 280
                Layout.maximumWidth: 320
                Layout.fillHeight: true
                color: AsterTheme.surface
                border.color: AsterTheme.stroke

                ScrollView {
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ColumnLayout {
                        width: guideInspector.width - 20
                        x: 10
                        y: 10
                        spacing: 8

                        // 相机
                        Text { text: "相机"; color: AsterTheme.deviceTitleText; font.pixelSize: 11; font.bold: true }
                        Text {
                            Layout.fillWidth: true; elide: Text.ElideRight
                            text: root.guideConnected ? (guideDevice.selectedName || "导星相机") : "未连接"
                            color: AsterTheme.textPrimary; font.pixelSize: 12
                        }
                        DesktopButton {
                            objectName: "guideConnectButton"
                            Layout.fillWidth: true
                            text: "设备连接管理"; glyph: "device"
                            onClicked: root.connectionsRequested()
                        }
                        DesktopField {
                            objectName: "guideExposureField"
                            label: "曝光"; editable: true; unit: "秒"
                            Layout.fillWidth: true; enabled: root.guideConnected
                            value: root.guideConnected && root.guideMetrics.exposureUs !== undefined
                                   ? (root.guideMetrics.exposureUs / 1000000).toString() : "—"
                            minimum: Math.max(0.001, (root.guideMetrics.exposureMinUs || 1000) / 1000000)
                            maximum: Math.min(3600, (root.guideMetrics.exposureMaxUs || 3600000000) / 1000000)
                            onValueEdited: value => { if (root.deviceManager) root.report(root.deviceManager.setGuideExposure(Number(value))) }
                        }
                        DesktopField {
                            objectName: "guideGainField"
                            label: "增益"; editable: true
                            Layout.fillWidth: true; enabled: root.guideConnected
                            value: root.guideConnected && root.guideMetrics.gain !== undefined
                                   ? String(root.guideMetrics.gain) : "—"
                            minimum: root.guideMetrics.gainMin || 0; maximum: root.guideMetrics.gainMax || 10000
                            onValueEdited: value => { if (root.deviceManager) root.report(root.deviceManager.setGuideGain(Math.round(Number(value)))) }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: AsterTheme.stroke }

                        // 星点
                        Text { text: "星点"; color: AsterTheme.deviceTitleText; font.pixelSize: 11; font.bold: true }
                        Rectangle {
                            objectName: "guideStarZoom"
                            Layout.fillWidth: true; Layout.preferredHeight: 110
                            color: AsterTheme.canvas; clip: true
                            Image {
                                id: starCrop
                                objectName: "guideStarCropImage"
                                visible: root.hasImage && !!guider && guider.locked
                                source: visible ? guideImageItem.source : ""
                                cache: false; smooth: false
                                width: implicitWidth * 4; height: implicitHeight * 4
                                x: parent.width / 2 - (guider ? guider.starX : 0) / Math.max(1, guider ? guider.frameWidth : 0) * width
                                y: parent.height / 2 - (guider ? guider.starY : 0) / Math.max(1, guider ? guider.frameHeight : 0) * height
                            }
                            Text {
                                anchors.centerIn: parent; visible: !starCrop.visible
                                text: "选星后显示局部放大"; color: AsterTheme.textSecondary; font.pixelSize: 11
                            }
                        }
                        GridLayout {
                            Layout.fillWidth: true; columns: 2; columnSpacing: 12; rowSpacing: 8
                            Repeater {
                                model: [
                                    ["SNR", guider && guider.locked ? guider.snr.toFixed(1) : "—"],
                                    ["HFD", guider && guider.locked ? guider.hfd.toFixed(2) + " px" : "—"],
                                    ["dX", guider && guider.locked ? guider.errorX.toFixed(2) + " px" : "—"],
                                    ["dY", guider && guider.locked ? guider.errorY.toFixed(2) + " px" : "—"]
                                ]
                                delegate: Text {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData[0] + "   " + modelData[1]
                                    color: AsterTheme.textPrimary; font.pixelSize: 12
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: AsterTheme.stroke }

                        // 输出与校准
                        Text { text: "输出与校准"; color: AsterTheme.deviceTitleText; font.pixelSize: 11; font.bold: true }
                        Text {
                            Layout.fillWidth: true
                            text: guider && guider.calibrated ? "校准：手动参数 · 未实测" : "校准：未校准"
                            color: AsterTheme.warning
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                        }
                        DesktopButton {
                            Layout.fillWidth: true
                            text: "自动校准（需输出接入）"
                            enabled: false
                        }
                        DesktopButton {
                            objectName: "guideClearStarButton"
                            Layout.fillWidth: true
                            text: "清除星点"
                            enabled: guider && guider.locked
                            onClicked: { guider.stopGuiding(); guider.clearStar() }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: AsterTheme.stroke }

                        // 导星参数
                        Text { text: "导星参数"; color: AsterTheme.deviceTitleText; font.pixelSize: 11; font.bold: true }
                        ComboBox {
                            id: algorithmCombo
                            objectName: "guideAlgorithmCombo"
                            Layout.fillWidth: true
                            implicitHeight: 30
                            enabled: !!guider
                            model: ["滞回", "直通"]
                            currentIndex: guider && guider.algorithm === "identity" ? 1 : 0
                            onActivated: index => { if (guider) guider.algorithm = index === 1 ? "identity" : "hysteresis" }
                            font.pixelSize: 11
                            contentItem: Text {
                                leftPadding: 10; rightPadding: 24
                                text: algorithmCombo.displayText
                                color: AsterTheme.textPrimary; font: algorithmCombo.font
                                verticalAlignment: Text.AlignVCenter
                            }
                            indicator: Text {
                                x: algorithmCombo.width - 22; anchors.verticalCenter: parent.verticalCenter
                                text: "⌄"; color: AsterTheme.textSecondary; font.pixelSize: 16
                            }
                            background: Rectangle {
                                color: AsterTheme.canvas; radius: 3
                                border.color: algorithmCombo.activeFocus ? AsterTheme.accent : AsterTheme.strokeStrong
                            }
                            delegate: ItemDelegate {
                                required property int index
                                required property var modelData
                                width: algorithmCombo.width
                                contentItem: Text { text: modelData; color: AsterTheme.textPrimary; font.pixelSize: 12 }
                                highlighted: algorithmCombo.highlightedIndex === index
                                background: Rectangle { color: parent.highlighted ? AsterTheme.accentSurface : AsterTheme.surface }
                            }
                            popup: Popup {
                                y: algorithmCombo.height + 2; width: algorithmCombo.width; padding: 1
                                implicitHeight: contentItem.implicitHeight + 2
                                contentItem: ListView {
                                    clip: true; implicitHeight: contentHeight
                                    model: algorithmCombo.popup.visible ? algorithmCombo.delegateModel : null
                                    currentIndex: algorithmCombo.highlightedIndex
                                }
                                background: Rectangle { color: AsterTheme.surface; border.color: AsterTheme.strokeStrong; radius: 3 }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            DesktopButton {
                                objectName: "guideRaParametersButton"
                                Layout.fillWidth: true
                                text: "RA 赤经"; checked: root.editRaParameters
                                onClicked: root.editRaParameters = true
                            }
                            DesktopButton {
                                objectName: "guideDecParametersButton"
                                Layout.fillWidth: true
                                text: "Dec 赤纬"; checked: !root.editRaParameters
                                onClicked: root.editRaParameters = false
                            }
                        }
                        DesktopField {
                            id: hysteresisField
                            objectName: "hysteresisField"
                            Layout.fillWidth: true
                            label: "滞回"; editable: true; unit: "%"
                            enabled: !!guider && guider.algorithm !== "identity"
                            value: guider ? String(root.editRaParameters ? guider.raHysteresisPercent : guider.decHysteresisPercent) : "10"
                            minimum: 0; maximum: 90
                            onValueEdited: value => {
                                if (!guider) return
                                if (root.editRaParameters) guider.raHysteresisPercent = Math.round(Number(value))
                                else guider.decHysteresisPercent = Math.round(Number(value))
                            }
                        }
                        DesktopField {
                            id: aggressionField
                            objectName: "aggressionField"
                            Layout.fillWidth: true
                            label: "修正强度"; editable: true; unit: "%"
                            enabled: !!guider && guider.algorithm !== "identity"
                            value: guider ? String(root.editRaParameters ? guider.raAggressionPercent : guider.decAggressionPercent) : "70"
                            minimum: 5; maximum: 100
                            onValueEdited: value => {
                                if (!guider) return
                                if (root.editRaParameters) guider.raAggressionPercent = Math.round(Number(value))
                                else guider.decAggressionPercent = Math.round(Number(value))
                            }
                        }
                        DesktopField {
                            objectName: "guideMinMoveField"
                            Layout.fillWidth: true
                            label: "最小修正量"; editable: true; unit: "px"
                            enabled: !!guider && guider.algorithm !== "identity"
                            value: guider ? Number(root.editRaParameters ? guider.raMinMovePx : guider.decMinMovePx).toFixed(3) : "0.200"
                            minimum: 0; maximum: 20
                            onValueEdited: value => {
                                if (!guider) return
                                if (root.editRaParameters) guider.raMinMovePx = Number(value)
                                else guider.decMinMovePx = Number(value)
                            }
                        }
                        DesktopField { label: "RMS RA"; value: guider && guider.guiding ? guider.rmsRa.toFixed(2) + " 角秒" : "—" }
                        DesktopField { label: "RMS Dec"; value: guider && guider.guiding ? guider.rmsDec.toFixed(2) + " 角秒" : "—" }
                        Text {
                            objectName: "guidePulseText"
                            Layout.fillWidth: true
                            visible: guider && guider.guiding
                            text: guider ? ("虚拟脉冲: " + (guider.pulseLog || "—")) : ""
                            color: AsterTheme.textSecondary
                            font.pixelSize: 10
                            font.family: AsterTheme.monoFont
                        }

                        // 高级设置：手动校准参数（默认折叠）。
                        Rectangle { Layout.fillWidth: true; height: 1; color: AsterTheme.stroke }
                        ColumnLayout {
                            id: guideAdvancedBody
                            objectName: "guideAdvancedBody"
                            visible: false
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "手动校准参数 · 未实测"; color: AsterTheme.warning; font.pixelSize: 10 }
                            DesktopField {
                                id: calScaleField
                                objectName: "calImageScaleField"
                                Layout.fillWidth: true
                                label: "像素比例"; editable: true; unit: "角秒/px"; value: "0.75"
                                minimum: 0.05; maximum: 20
                                onValueEdited: value => calScaleField.value = value
                            }
                            DesktopField {
                                id: calRateField
                                objectName: "calGuideRateField"
                                Layout.fillWidth: true
                                label: "导星速度"; editable: true; unit: "×"; value: "0.5"
                                minimum: 0.05; maximum: 1
                                onValueEdited: value => calRateField.value = value
                            }
                            DesktopField {
                                id: calDecField
                                objectName: "calDeclinationField"
                                Layout.fillWidth: true
                                label: "赤纬"; editable: true; unit: "°"; value: "0"
                                minimum: -89; maximum: 89
                                onValueEdited: value => calDecField.value = value
                            }
                            DesktopField {
                                id: calAngleField
                                objectName: "calCameraAngleField"
                                Layout.fillWidth: true
                                label: "相机角度"; editable: true; unit: "°"; value: "0"
                                minimum: -180; maximum: 180
                                onValueEdited: value => calAngleField.value = value
                            }
                            DesktopButton {
                                objectName: "manualCalibrateButton"
                                Layout.fillWidth: true
                                text: "应用手动参数"
                                onClicked: if (guider) guider.manualCalibrate(
                                    Number(calScaleField.value), Number(calRateField.value),
                                    Number(calDecField.value), Number(calAngleField.value))
                            }
                            DesktopButton {
                                objectName: "clearCalibrationButton"
                                Layout.fillWidth: true
                                text: "清除校准"
                                enabled: guider && guider.calibrated
                                onClicked: guider.clearCalibration()
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }
}
