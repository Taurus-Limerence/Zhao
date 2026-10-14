import QtQuick
import QtQuick.Layouts
import "../controls"
import "../theme"

// Mount settings: dynamic — reads live ASCOM data from DeviceManager
// and routes control back through it. Placeholder groups stay until
// those modules exist.
SettingsPage {
    id: root
    property var deviceManager: null
    property var planner: null
    property date localNow: new Date()
    property string syncStatus: ""
    readonly property bool canSync: connected && mount.selectedId === "ASCOM.TMount.Mount"
        && !mount.busy && metrics.slewing === false && deviceManager
        && !deviceManager.mountGuiding && !deviceManager.mountManualMoving
    Timer { interval: 1000; running: root.visible; repeat: true; onTriggered: root.localNow = new Date() }

    readonly property var mount: {
        const devices = deviceManager ? deviceManager.devices : []
        for (let i = 0; i < devices.length; ++i) if (devices[i].role === 0) return devices[i]
        return {}
    }
    readonly property var metrics: mount.metrics || {}
    readonly property bool connected: !!mount.connected
    // Verified in TMount 6.5: ASCOM FindHome sends :MH# (return), not :MFH# (search).
    readonly property bool canReturnHome: connected && mount.selectedId === "ASCOM.TMount.Mount"
                                          && !!metrics.canFindHome && !metrics.atPark && !metrics.slewing
    readonly property double ra: metrics.ra !== undefined ? metrics.ra : -1
    readonly property double dec: metrics.dec !== undefined ? metrics.dec : -999

    readonly property int guideRateIndex: {
        const rate = metrics.guideRateRa
        if (!connected || typeof rate !== "number" || !isFinite(rate) || rate < 0.099999 || rate > 0.900001)
            return -1
        return Math.round(rate * 10) - 1
    }

    function raText() {
        return hoursText(ra)
    }
    function hoursText(value) {
        if (typeof value !== "number" || !isFinite(value) || value < 0) return "—"
        const seconds = Math.round(value * 3600) % 86400
        return String(Math.floor(seconds / 3600)).padStart(2, "0") + "h "
            + String(Math.floor(seconds / 60) % 60).padStart(2, "0") + "m "
            + String(seconds % 60).padStart(2, "0") + "s"
    }
    function degreesText(value) {
        const seconds = Math.round(Math.abs(value) * 3600)
        return Math.floor(seconds / 3600) + "° "
            + String(Math.floor(seconds / 60) % 60).padStart(2, "0") + "′ "
            + String(seconds % 60).padStart(2, "0") + "″"
    }
    function coordinateText(value, longitude) {
        if (typeof value !== "number" || !isFinite(value) || Math.abs(value) > (longitude ? 180 : 90)) return "—"
        return (longitude ? (value < 0 ? "W " : "E ") : (value < 0 ? "S " : "N ")) + degreesText(value)
    }
    function horizontalText(value, azimuth) {
        if (typeof value !== "number" || !isFinite(value)
            || (azimuth ? value < 0 || value > 360 : Math.abs(value) > 90)) return "—"
        let seconds = Math.round(Math.abs(value) * 3600)
        if (azimuth) seconds %= 360 * 3600
        return (azimuth ? "" : value < 0 ? "-" : "+")
            + String(Math.floor(seconds / 3600)).padStart(azimuth ? 3 : 2, "0") + "° "
            + String(Math.floor(seconds / 60) % 60).padStart(2, "0") + "′ "
            + String(seconds % 60).padStart(2, "0") + "″"
    }
    function decText() {
        if (dec < -900) return "—"
        const sign = dec >= 0 ? "+" : "-"
        return sign + degreesText(dec)
    }
    function siderealText() {
        return hoursText(metrics.siderealTime)
    }
    function syncObserver(timeOnly) {
        if (!canSync || (!timeOnly && !planner)) return
        const ok = deviceManager.syncMountObserver(timeOnly, planner ? planner.longitude : 0, planner ? planner.latitude : 0)
        syncStatus = ok ? (timeOnly ? "电脑时间已同步，回读核验通过" : "经纬度已同步，回读核验通过")
                        : ""
    }
    function localTimeText() {
        return Qt.formatDateTime(localNow, "yyyy-MM-dd HH:mm:ss t")
    }

    sections: [
        { title: "设备连接", rows: [
            { label: "设备", type: "select",
              options: mount.selectedName ? [mount.selectedName] : [],
              wide: true },
            { label: "连接", type: "toggle",
              checked: connected,
              enabled: !!deviceManager && !!mount.selectedId,
              onToggled: checked => deviceManager.setConnected(0, checked) }
        ]},
        { title: "星体库", rows: [
            { label: "星体库", type: "button", value: "选择目标 ›" }
        ]},
        { title: "赤道仪信息 · 驱动回读", rows: [
            { label: "RA", value: raText() },
            { label: "Dec", value: decText() },
            { label: "经度 LON", value: coordinateText(metrics.siteLongitude, true) },
            { label: "纬度 LAT", value: coordinateText(metrics.siteLatitude, false) },
            { label: "高度角", value: horizontalText(metrics.altitude, false) },
            { label: "方位角", value: horizontalText(metrics.azimuth, true) },
            { label: "恒星时", value: siderealText() },
            { label: "设备时间（UTC）", value: metrics.utcTimeText || "—", wide: true }
        ]},
        { title: "本地信息 · 与星图共用观测站", rows: [
            { label: "经度（°，东正西负）", type: "field", value: planner ? planner.longitude.toFixed(4) : "",
              minimum: -180, maximum: 180, decimals: 6, enabled: !!planner,
              onEdited: value => { planner.longitude = Number(value); syncStatus = "本地位置已保存，尚未同步到赤道仪" } },
            { label: "纬度（°，北正南负）", type: "field", value: planner ? planner.latitude.toFixed(4) : "",
              minimum: -90, maximum: 90, decimals: 6, enabled: !!planner,
              onEdited: value => { planner.latitude = Number(value); syncStatus = "本地位置已保存，尚未同步到赤道仪" } },
            { label: "电脑时间／时区", value: localTimeText(), wide: true },
            { label: "仅同步经纬度", type: "button", value: "同步位置到赤道仪", enabled: canSync && !!planner,
              onClicked: () => syncObserver(false) },
            { label: "使用当前电脑时间，非星图模拟时间", type: "button", value: "同步电脑时间", enabled: canSync,
              onClicked: () => syncObserver(true) },
            { label: "", component: syncMessage, wide: true, editorWidth: Math.max(190, root.width - 36) }
        ]},
        { title: "零位", rows: [
            { label: "零位", component: homeActions, editorWidth: 330 },
            { label: "寻找零位", component: searchHomeAction },
            { label: "寻找／设置零位尚未通过当前 ASCOM 接口开放，请在驱动窗口操作", value: " ", editorWidth: 1 }
        ]},
        { title: "停放位", rows: [
            { label: "停放位", component: parkActions, editorWidth: 330 }
        ]},
        { title: "跟踪", rows: [
            { label: "跟踪", type: "toggle",
              checked: !!metrics.tracking,
              enabled: connected && !metrics.atPark,
              onToggled: checked => deviceManager.setMountTracking(checked) },
            { label: "跟踪速率", type: "segment",
              options: ["恒星", "月亮", "太阳"],
              currentIndex: metrics.trackingRate !== undefined ? metrics.trackingRate : 0,
              enabled: connected,
              onActivated: index => deviceManager.setMountTrackingRate(index) },
            { label: "导星速率（倍恒星速）", type: "select",
              options: ["0.1×", "0.2×", "0.3×", "0.4×", "0.5×", "0.6×", "0.7×", "0.8×", "0.9×"],
              currentIndex: guideRateIndex,
              enabled: connected && !!metrics.canSetGuideRates,
              onActivated: index => {
                  if (!connected || !metrics.canSetGuideRates || index < 0 || index > 8) return
                  const rate = (index + 1) / 10
                  deviceManager.setMountGuideRates(rate, rate)
              } }
        ]},
        { title: "解析与指向", rows: [
            { label: "指向后居中", type: "segment", options: ["关闭", "打开", "询问"], currentIndex: 1 },
            { label: "解析增益", type: "segment", options: ["低", "中", "高"], currentIndex: 1 },
            { label: "解析曝光时间", value: "4.0 s" }
        ]},
        { title: "中天翻转", rows: [
            { label: "中天翻转", type: "toggle", checked: true },
            { label: "过中天前停止跟踪", value: "5 min" },
            { label: "过中天后开始翻转", value: "2 min" },
            { label: "翻转后赤纬反向输出", type: "toggle", checked: false },
            { label: "翻转后重新校准导星", type: "toggle", checked: true }
        ]},
        { title: "", rows: [
            { label: "停止运动", type: "button", value: "立即停止",
              enabled: connected,
              onClicked: () => deviceManager.mountAbortSlew() },
            { label: "高级设置", type: "button", value: "打开 ›" }
        ]}
    ]

    footer: controlFooter
    Component {
        id: syncMessage
        Text {
            text: root.syncStatus || "本地修改与星图共用；同步需手动点击，不修改设备时区。"
            color: AsterTheme.textSecondary
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }
    }

    Component {
        id: homeActions
        RowLayout {
            spacing: 8
            DesktopButton {
                objectName: "mountSetHome"
                Layout.fillWidth: true
                text: "设为零位"
                enabled: false
            }
            AsterButton {
                objectName: "mountGoHome"
                Layout.preferredWidth: 190
                Layout.fillHeight: true
                text: "回到零位"
                enabled: root.canReturnHome
                onClicked: if (enabled) root.deviceManager.mountFindHome()
            }
        }
    }
    Component {
        id: searchHomeAction
        AsterButton {
            objectName: "mountSearchHome"
            text: "寻找零位"
            enabled: false
        }
    }
    Component {
        id: parkActions
        RowLayout {
            spacing: 8
            DesktopButton {
                objectName: "mountSetPark"
                Layout.fillWidth: true
                text: "设为停放位"
                enabled: root.connected && !!root.metrics.canSetPark && !root.metrics.slewing && !root.metrics.atPark
                onClicked: if (enabled) root.deviceManager.mountSetCurrentPark()
            }
            AsterButton {
                objectName: "mountPark"
                Layout.preferredWidth: 190
                Layout.fillHeight: true
                text: root.metrics.atPark ? "解除停放" : "停放赤道仪"
                enabled: root.connected && !root.metrics.slewing
                         && (root.metrics.atPark ? !!root.metrics.canUnpark : !!root.metrics.canPark)
                onClicked: {
                    if (!enabled) return
                    if (root.metrics.atPark) root.deviceManager.mountUnpark()
                    else root.deviceManager.mountPark()
                }
            }
        }
    }

    Component {
        id: controlFooter
        ColumnLayout {
            spacing: AsterTheme.space2
            Text {
                text: "驱动配置"
                color: AsterTheme.deviceTitleText
                font.family: AsterTheme.uiFont
                font.pixelSize: 12
                font.bold: true
            }
            DesktopButton {
                objectName: "mountSetupButton"
                Layout.fillWidth: true
                text: "驱动设置（串口/参数）…"
                enabled: !!root.deviceManager && !!root.mount.selectedId && !root.connected
                onClicked: root.deviceManager.openMountSetup()
            }
            Text {
                Layout.fillWidth: true
                visible: !root.connected
                text: "断开状态下可打开驱动自带设置界面配置串口等参数"
                color: AsterTheme.textMuted
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }
        }
    }
}
