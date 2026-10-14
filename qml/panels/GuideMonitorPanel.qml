import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"
import "../theme"

Rectangle {
    id: root
    property bool expanded: true
    property string statusText: "未连接导星相机"
    signal toggleRequested()
    color: AsterTheme.surface
    clip: true

    RowLayout {
        id: heading
        width: parent.width
        height: 26
        spacing: 12
        DesktopButton {
            objectName: "guideToggleButton"
            text: root.expanded ? "⌄  导星曲线" : "›  导星曲线"
            height: 26
            Accessible.name: root.expanded ? "收起导星曲线" : "展开导星曲线"
            onClicked: root.toggleRequested()
        }
        Text { text: "RA  —″"; color: AsterTheme.accent; font.family: AsterTheme.monoFont; font.pixelSize: 10 }
        Text { text: "Dec  —″"; color: AsterTheme.warning; font.family: AsterTheme.monoFont; font.pixelSize: 10 }
        Text { text: "总 RMS  —″"; color: AsterTheme.textSecondary; font.family: AsterTheme.monoFont; font.pixelSize: 10 }
        Text { text: "HFR  — px"; color: AsterTheme.textSecondary; font.family: AsterTheme.monoFont; font.pixelSize: 10; visible: root.width > 480 }
        Item { Layout.fillWidth: true }
        Text { text: root.statusText; color: AsterTheme.textMuted; font.pixelSize: 10; Layout.rightMargin: 12; visible: root.width > 660 }
    }
    Canvas {
        id: grid
        anchors.top: heading.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 10
        visible: root.expanded
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const left = 32, right = width - 8, bottom = height - 20
            if (bottom <= 0 || right <= left) return
            ctx.font = '10px "' + AsterTheme.monoFont + '"'
            for (let i = 0; i <= 4; ++i) {
                const y = 6 + (bottom - 6) * i / 4
                ctx.strokeStyle = i === 2 ? AsterTheme.strokeStrong : AsterTheme.stroke
                ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(right, y); ctx.stroke()
                ctx.fillStyle = AsterTheme.textMuted
                ctx.fillText(String(4 - i * 2), 6, y + 3)
            }
            for (let i = 0; i <= 4; ++i) {
                const x = left + (right - left) * i / 4
                ctx.strokeStyle = AsterTheme.stroke
                ctx.beginPath(); ctx.moveTo(x, 6); ctx.lineTo(x, bottom); ctx.stroke()
                ctx.fillStyle = AsterTheme.textMuted
                ctx.fillText(i === 4 ? "现在" : String(-120 + i * 30) + "s", Math.min(x, right - 24), height - 3)
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(300, parent.width - 40)
            height: 44
            color: AsterTheme.surface
            Column {
                anchors.centerIn: parent
                spacing: 5
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.statusText; color: AsterTheme.textSecondary; font.pixelSize: 11 }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "接入数据后显示 RA / Dec 曲线"; color: AsterTheme.textMuted; font.pixelSize: 10 }
            }
        }
    }
}
