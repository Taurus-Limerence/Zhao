import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"
import "../theme"

Rectangle {
    id: root
    property bool expanded: true
    property bool logarithmic: false
    property var bins: []
    readonly property real pixelCount: bins.reduce((sum, count) => sum + count, 0)
    signal toggleRequested()
    color: AsterTheme.surface
    clip: true
    onBinsChanged: plot.requestPaint()
    onLogarithmicChanged: plot.requestPaint()
    Rectangle { width: parent.width; height: 1; color: AsterTheme.stroke }
    RowLayout {
        id: heading
        width: parent.width
        height: 30
        spacing: 4
        DesktopButton {
            objectName: "histogramToggleButton"
            text: root.expanded ? "⌄  直方图" : "›  直方图"
            onClicked: root.toggleRequested()
            Accessible.name: root.expanded ? "收起直方图" : "展开直方图"
        }
        Item { Layout.fillWidth: true }
        DesktopButton {
            text: "Log"
            checked: root.logarithmic
            enabled: root.pixelCount > 0
            visible: root.expanded
            onClicked: root.logarithmic = !root.logarithmic
            ToolTip.visible: hovered
            ToolTip.text: "纵轴对数显示"
            Layout.rightMargin: 8
        }
    }
    Canvas {
        id: plot
        objectName: "histogramPlot"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.bottom: sourceLabel.top
        anchors.margins: 10
        visible: root.expanded
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const h = height - 16
            if (h < 1 || width < 2) return
            ctx.strokeStyle = AsterTheme.stroke
            ctx.lineWidth = 1
            for (let i = 1; i <= 3; ++i) {
                ctx.beginPath(); ctx.moveTo(0, h * i / 3); ctx.lineTo(width, h * i / 3); ctx.stroke()
            }
            if (root.bins.length === 256 && root.pixelCount > 0) {
                const peak = Math.max(1, ...root.bins)
                const scale = root.logarithmic ? Math.log(1 + peak) : peak
                ctx.beginPath(); ctx.moveTo(0, h)
                for (let i = 0; i < 256; ++i) {
                    const count = root.logarithmic ? Math.log(1 + root.bins[i]) : root.bins[i]
                    ctx.lineTo(i * (width - 1) / 255, h - count / scale * (h - 4))
                }
                ctx.lineTo(width, h); ctx.closePath()
                ctx.fillStyle = AsterTheme.accentSurface; ctx.fill()
                ctx.strokeStyle = AsterTheme.accent; ctx.stroke()
            }
            ctx.fillStyle = AsterTheme.textMuted
            ctx.font = '9px "' + AsterTheme.monoFont + '"'
            ctx.fillText("0", 0, height - 2)
            ctx.fillText("128", width / 2 - 10, height - 2)
            ctx.fillText("255", width - 22, height - 2)
        }
        Text { anchors.centerIn: parent; visible: root.pixelCount === 0; text: "暂无图像数据"; color: AsterTheme.textMuted; font.pixelSize: 11 }
    }
    Text {
        id: sourceLabel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        visible: root.expanded
        text: root.pixelCount > 0 ? "JPEG · 8-bit 灰度 · 非 RAW" : "等待图像"
        color: AsterTheme.textMuted
        font.pixelSize: 10
        elide: Text.ElideRight
    }
}
