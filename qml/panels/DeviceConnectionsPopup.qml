import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"
import "../theme"

Rectangle {
    id: root
    readonly property int roleCount: 5
    property var deviceManager
    readonly property var devices: deviceManager ? deviceManager.devices : []
    signal noticeRequested(string message)
    property bool scanWasRunning: false
    // 候选列表修订号：仅在扫描结果更新（candidatesChanged）时递增，作为下拉框
    // model 绑定的重取触发。不能用 scanning——赤道仪连接后 1 Hz 的状态轮询
    // 同走 devicesChanged 通知，会把下拉框每秒重置一次。
    property int candidatesRevision: 0
    // 设备行结构快照：赤道仪坐标等高频 metrics 更新只刷新数值，不重建行，
    // 否则打开中的下拉框会被销毁。仅结构字段变化时才更新。
    property var deviceRows: []
    function syncDeviceRows() {
        // 直接读 manager 而非 devices 绑定：onDeviceManagerChanged 触发时
        // 中间绑定可能尚未重算。
        const source = deviceManager ? deviceManager.devices : []
        const next = []
        for (const device of source)
            next.push({role: device.role, title: device.title, selectedId: device.selectedId,
                       selectedName: device.selectedName, connected: device.connected,
                       busy: device.busy, error: device.error, supported: device.supported})
        if (JSON.stringify(next) === JSON.stringify(deviceRows)) return
        deviceRows = next
    }
    signal closeRequested()
    // 连接后的设备身份与系统驱动：USB 设备按 NINA 格式显示 Vendor/Product/
    // 设备 ID（VID/PID 由 Windows 实例路径解析，相机/滤镜轮/电调焦共用
    // Toupcam SDK、赤道仪走 ASCOM，驱动各显示一次）。
    readonly property var connectedInfoRows: {
        const rows = []
        for (const device of devices) {
            if (!device.connected) continue
            const metrics = device.metrics || {}
            if (device.role === 0) {
                rows.push({label: device.title, value: metrics.progId || device.selectedName || "—"})
                continue
            }
            // NINA 同款格式：Vendor ID: xxxx, Product ID: xxxx, <角色>ID: 尾段
            const idLabel = device.role === 1 || device.role === 2 ? "Camera ID"
                          : device.role === 3 ? "Filterwheel ID" : "Focuser ID"
            const parts = []
            if (metrics.vendorId) parts.push("Vendor ID: " + metrics.vendorId)
            if (metrics.productId) parts.push("Product ID: " + metrics.productId)
            const deviceId = metrics.deviceId || device.selectedId
            if (deviceId) parts.push(idLabel + ": " + deviceId)
            // 设备名去掉 "[id 尾段]" 后缀——Camera ID 已单独显示，避免重复。
            const displayName = (device.selectedName || "—").replace(/\s*\[[^\]]*\]$/, "")
            rows.push({label: device.title,
                       value: displayName + (parts.length > 0 ? " · " + parts.join(", ") : "")})
        }
        // 驱动行跟随实际连接：ToupTek SDK 覆盖相机/导星/滤镜轮/电调焦（版本取自
        // 任一已连的 Toupcam 设备），ASCOM 覆盖赤道仪；只显示已连接的一侧，
        // 两侧都有连接才同时显示。
        let touptekSdk = ""
        for (const device of devices) {
            if (device.connected && device.role >= 1 && device.role <= 4) {
                const version = (device.metrics || {}).sdkVersion
                if (version) { touptekSdk = version; break }
            }
        }
        const mount = devices.length > 0 ? devices[0] : {}
        const mountMetrics = mount.connected ? (mount.metrics || {}) : {}
        const driverParts = []
        if (touptekSdk) driverParts.push("ToupTek SDK " + touptekSdk)
        // 平台版本（注册表，如 7.1）与驱动自身版本是两个数字；驱动显示 NINA 同款
        // 的 ProductVersion 完整四段号（ProgId→CLSID→DLL 版本资源解析）。
        if (mountMetrics.platformVersion)
            driverParts.push("ASCOM Platform " + mountMetrics.platformVersion)
        if (mountMetrics.driverProductVersion) {
            // NINA 显示 "ASCOM TMount 6.5.…"：短名取自 ProgId 的中段
            // （ASCOM.<短名>.<设备类型>），Name 属性报的是完整 ProgId。
            const driverName = (mountMetrics.progId || "").replace(/^ASCOM\./i, "").replace(/\.[^.]+$/, "")
                             || mountMetrics.name || ""
            driverParts.push((driverName ? "ASCOM " + driverName + " " : "ASCOM ")
                             + mountMetrics.driverProductVersion)
        } else if (mountMetrics.driverVersion)
            driverParts.push((mountMetrics.name ? mountMetrics.name + " " : "") + "驱动 "
                             + mountMetrics.driverVersion)
        if (driverParts.length > 0)
            rows.push({label: "驱动", value: driverParts.join(" · ")})
        return rows
    }
    readonly property bool infoRowsShown: !(deviceManager && deviceManager.scanning)
        && connectedInfoRows.length > 0
    implicitWidth: 640
    implicitHeight: 410
    radius: AsterTheme.radiusMedium
    color: AsterTheme.surface
    border.color: AsterTheme.strokeStrong

    onDeviceManagerChanged: syncDeviceRows()
    Component.onCompleted: {
        syncDeviceRows()
        if (deviceManager && typeof deviceManager.refreshDevices === "function") deviceManager.refreshDevices()
    }
    Connections {
        target: root.deviceManager || null
        ignoreUnknownSignals: true
        function onDevicesChanged() {
            root.syncDeviceRows()
            if (root.scanWasRunning && !root.deviceManager.scanning
                && root.deviceManager.candidatesForRole(1).length === 0 && !root.deviceManager.lastError)
                root.noticeRequested("未发现主相机。请检查设备连接及访问权限后刷新。")
            root.scanWasRunning = !!root.deviceManager.scanning
        }
        function onCandidatesChanged() { ++root.candidatesRevision }
    }

    function iconForRole(role) {
        return role === 0 ? "../assets/icons/mount.svg"
             : role === 1 ? "../assets/icons/camera.svg"
             : role === 2 ? "../assets/icons/guide-camera.svg"
             : role === 3 ? "../assets/icons/filter.svg"
             : "../assets/icons/focuser.svg"
    }

    function changeConnections(connected, role) {
        // Snapshot the rows: DeviceManager emits a new list after each change.
        const targets = devices.filter(device => role >= 0 ? device.role === role
            : connected ? device.selectedId.length > 0 && !device.connected : device.connected)
        for (const device of targets)
            deviceManager.setConnected(device.role, connected)
    }

    function channelSuffix(slot) {
        const channels = deviceManager && deviceManager.filterWheelChannels ? deviceManager.filterWheelChannels : []
        for (let i = 0; i < channels.length; ++i)
            if (channels[i].slot === slot) return String(channels[i].suffix || "")
        return ""
    }

    function feedbackText() {
        if (deviceManager && deviceManager.scanning) return "正在搜索主相机…"
        return ""
    }

    MouseArea { anchors.fill: parent; onWheel: wheel => wheel.accepted = true }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Text { text: "设备连接管理"; color: AsterTheme.textPrimary; font.pixelSize: 15; font.bold: true }
            Text {
                Layout.fillWidth: true
                text: "已连接 " + root.devices.filter(device => device.connected).length + " / 5"
                color: AsterTheme.textSecondary
                font.pixelSize: 12
            }
            DesktopButton {
                objectName: "refreshDevicesButton"
                text: root.deviceManager && root.deviceManager.scanning ? "搜索中…" : "刷新设备"
                enabled: !!root.deviceManager && !root.deviceManager.scanning && !root.devices.some(device => device.busy)
                onClicked: {
                    root.deviceManager.refreshDevices()
                }
            }
            AsterIconButton {
                width: 30; height: 30
                iconSource: "../assets/icons/close.svg"
                Accessible.name: "关闭设备连接管理"
                onClicked: root.closeRequested()
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: AsterTheme.stroke }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.preferredHeight: 32
            spacing: 16
            Text { Layout.preferredWidth: 120; text: "设备类型"; color: AsterTheme.textSecondary; font.pixelSize: 12 }
            Text { Layout.fillWidth: true; text: "设备"; color: AsterTheme.textSecondary; font.pixelSize: 12 }
            Text { Layout.preferredWidth: 72; text: "操作"; horizontalAlignment: Text.AlignHCenter; color: AsterTheme.textSecondary; font.pixelSize: 12 }
        }
        Repeater {
            model: root.deviceRows
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                color: modelData.role % 2 === 0 ? AsterTheme.canvasRaised : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 16
                    RowLayout {
                        Layout.preferredWidth: 120
                        Layout.minimumWidth: 120
                        Layout.maximumWidth: 120
                        spacing: 10
                        Image { source: root.iconForRole(modelData.role); Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                        // 状态列已移除：已连接经设备名着色体现，失败经右下角通知提示
                        Text {
                            Layout.fillWidth: true
                            text: modelData.title
                            color: modelData.connected ? AsterTheme.accent : AsterTheme.textPrimary
                            font.pixelSize: 13
                        }
                    }
                    ComboBox {
                        id: selector
                        objectName: "connectionSelector" + modelData.role
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        implicitHeight: 30
                        enabled: modelData.supported !== false && !modelData.connected && !modelData.busy
                                  && !!root.deviceManager && !root.deviceManager.scanning
                        // 候选列表是 candidatesForRole() 的 C++ 快照调用，QML 无法
                        // 跟踪其内部状态：读取修订号（只在扫描结果更新时递增）
                        // 触发重取，新插入的设备无需重开弹窗即可见。
                        model: {
                            const revision = root.candidatesRevision
                            const manager = root.deviceManager
                            return manager ? manager.candidatesForRole(modelData.role) : []
                        }
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: indexOfValue(modelData.selectedId)
                        // 收起状态去掉名称里的 [id 尾段]；完整 ID 保留在下拉
                        // 列表里用于区分同型号设备
                        displayText: root.deviceManager && root.deviceManager.scanning ? "搜索中…"
                            : (modelData.selectedName || "").replace(/\s*\[[^\]]*\]$/, "")
                              || (modelData.supported === false ? "驱动尚未接入" : "请选择设备")
                        Accessible.name: modelData.title + "型号"
                        delegate: ItemDelegate {
                            id: optionDelegate
                            width: selector.width - 2
                            height: 34
                            // 转义设备 ID 里的 &：ItemDelegate 按助记符解析会告警并吞字符
                            text: selector.textAt(index).replace(/&/g, "&&")
                            highlighted: selector.highlightedIndex === index
                            hoverEnabled: true
                            contentItem: Text {
                                text: optionDelegate.text
                                color: AsterTheme.textPrimary
                                font.family: AsterTheme.uiFont
                                font.pixelSize: 12
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            background: Rectangle {
                                color: optionDelegate.highlighted ? AsterTheme.accentSurface
                                     : optionDelegate.hovered ? AsterTheme.surfaceHover : AsterTheme.canvasRaised
                            }
                        }
                        popup: Popup {
                            y: selector.height + 2
                            width: selector.width
                            padding: 1
                            implicitHeight: Math.min(contentItem.implicitHeight + 2, 240)
                            contentItem: ListView {
                                clip: true
                                implicitHeight: contentHeight
                                model: selector.popup.visible ? selector.delegateModel : null
                                currentIndex: selector.highlightedIndex
                                ScrollIndicator.vertical: ScrollIndicator { }
                            }
                            background: Rectangle {
                                color: AsterTheme.canvasRaised
                                border.color: AsterTheme.strokeStrong
                                radius: AsterTheme.radiusSmall
                            }
                        }
                        // 编译单元里信号处理器解析不到外层 root（id 引用），
                        // 但 modelData 等上下文属性可以：经属性绑定（绑定上下文
                        // 解析正常）捕获 root，处理器内只用捕获值。命名避开
                        // ComboBox 内建的 popup 属性。
                        readonly property var host: root
                        onActivated: function(index) {
                            host.deviceManager.selectDevice(modelData.role, currentValue)
                        }
                        contentItem: Text {
                            leftPadding: 8
                            rightPadding: 26
                            text: selector.displayText
                            color: AsterTheme.textPrimary
                            font.pixelSize: 12
                            font.family: AsterTheme.uiFont
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        indicator: Image {
                            x: selector.width - 22
                            anchors.verticalCenter: parent.verticalCenter
                            width: 14; height: 14
                            source: "../assets/icons/chevron-down.svg"
                            opacity: selector.enabled ? 1 : 0.4
                        }
                        background: Rectangle {
                            radius: 3
                            color: AsterTheme.canvas
                            border.color: selector.activeFocus ? AsterTheme.accent : AsterTheme.stroke
                        }
                    }
                    DesktopButton {
                        objectName: "connectDevice" + modelData.role
                        Layout.preferredWidth: 72
                        implicitHeight: 30
                        // 忙态文本"…"弥补移除状态列后的"连接中…/断开中…"过程反馈
                        text: modelData.busy ? "…" : modelData.connected ? "断开" : "连接"
                        enabled: modelData.supported !== false && modelData.selectedId.length > 0 && !modelData.busy
                                  && !!root.deviceManager && !root.deviceManager.scanning
                        Accessible.name: text + modelData.title
                        onClicked: root.changeConnections(!modelData.connected, modelData.role)
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: AsterTheme.stroke }
            }
        }
        // 连接后的设备信息区：标签列 + 内容列；无连接/出错/扫描中回退为提示文字。
        ColumnLayout {
            objectName: "connectionInfoRows"
            visible: root.infoRowsShown
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 48
            spacing: 2
            Repeater {
                model: root.connectedInfoRows
                RowLayout {
                    id: infoRow
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 10
                    Text {
                        Layout.preferredWidth: 56
                        text: infoRow.modelData.label
                        color: AsterTheme.textSecondary
                        font.family: AsterTheme.uiFont
                        font.pixelSize: 11
                    }
                    Text {
                        Layout.fillWidth: true
                        text: infoRow.modelData.value
                        color: AsterTheme.textPrimary
                        font.family: AsterTheme.uiFont
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }
        Text {
            objectName: "connectionFeedback"
            visible: !root.infoRowsShown
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 48
            text: root.feedbackText()
            color: AsterTheme.textSecondary
            font.pixelSize: 12
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: AsterTheme.stroke }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            Item { Layout.fillWidth: true }
            DesktopButton {
                objectName: "disconnectAllDevices"
                text: "全部断开"
                enabled: !!root.deviceManager && !root.devices.some(device => device.busy) && !root.deviceManager.scanning && root.devices.some(device => device.connected)
                implicitHeight: 30
                onClicked: root.changeConnections(false, -1)
            }
            DesktopButton {
                objectName: "connectSelectedDevices"
                text: "连接已选设备"
                primary: true
                implicitHeight: 30
                enabled: !!root.deviceManager && !root.devices.some(device => device.busy) && !root.deviceManager.scanning && root.devices.some(device => device.selectedId.length > 0 && !device.connected && device.supported !== false)
                onClicked: root.changeConnections(true, -1)
            }
        }
    }
}
