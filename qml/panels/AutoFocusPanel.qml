import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls"
import "../theme"

ColumnLayout {
    id: root
    property var controller: null
    property bool cameraConnected: false
    property bool focuserConnected: false
    property bool controlsLocked: false
    readonly property bool active: !!controller && controller.busy
    readonly property var settings: controller ? controller.settings
        : ({step: 80, points: 9, exposure: 3.0, frames: 1, backlash: 0})
    readonly property bool parametersValid: step.valid && exposure.valid && points.valid && frames.valid && backlash.valid
    signal noticeRequested(string message)
    spacing: 8

    function formatted(value, digits) { return isFinite(value) ? Number(value).toFixed(digits) : "—" }
    function start() {
        if (!controller || active || controlsLocked || !parametersValid || !cameraConnected || !focuserConnected) return
        // Snapshot all editors before settings notifications can refresh them.
        const values = {step: Number(step.text), exposure: Number(exposure.text), points: Number(points.text),
                        frames: Number(frames.text), backlash: Number(backlash.text)}
        for (const key in values) controller.setSetting(key, values[key])
        if (!controller.start()) noticeRequested(controller.status)
    }

    component ParameterInput: TextField {
        id: input
        required property string settingKey
        required property string fieldLabel
        property real minimum: 0
        property real maximum: 100000
        property int decimals: 0
        readonly property var storedValue: root.settings[settingKey]
        readonly property bool valid: text.trim().length > 0 && acceptableInput
            && isFinite(Number(text)) && Number(text) >= minimum && Number(text) <= maximum
            && (decimals > 0 || Number.isInteger(Number(text)))
            && (settingKey !== "points" || Number(text) % 2 === 1)
        Layout.fillWidth: true
        Layout.preferredHeight: 27
        enabled: !!root.controller && !root.active && !root.controlsLocked
        text: String(storedValue)
        onStoredValueChanged: if (!activeFocus) text = String(storedValue)
        onEditingFinished: if (valid && root.controller && !root.active && !root.controlsLocked) root.controller.setSetting(settingKey, Number(text))
        Accessible.name: fieldLabel
        selectByMouse: true
        horizontalAlignment: Text.AlignRight
        color: enabled ? AsterTheme.textPrimary : AsterTheme.textDisabled
        font.family: AsterTheme.monoFont
        font.pixelSize: 11
        padding: 5
        validator: DoubleValidator {
            bottom: input.minimum; top: input.maximum; decimals: input.decimals
            locale: "C"; notation: DoubleValidator.StandardNotation
        }
        background: Rectangle {
            color: AsterTheme.canvas
            radius: 3
            border.color: !input.valid ? AsterTheme.danger : input.activeFocus ? AsterTheme.accent : AsterTheme.strokeStrong
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 3; columnSpacing: 8; rowSpacing: 4
        Text { text: "步长 / 步"; color: AsterTheme.textSecondary; font.pixelSize: 11 }
        Text { text: "曝光 / 秒"; color: AsterTheme.textSecondary; font.pixelSize: 11 }
        Text { text: "采样点 / 奇数"; color: AsterTheme.textSecondary; font.pixelSize: 11 }
        ParameterInput { id: step; objectName: "autoFocusStep"; settingKey: "step"; fieldLabel: "步长，步"; minimum: 1 }
        ParameterInput { id: exposure; objectName: "autoFocusExposure"; settingKey: "exposure"; fieldLabel: "曝光，秒"; minimum: 0.001; maximum: 3600; decimals: 3 }
        ParameterInput { id: points; objectName: "autoFocusPoints"; settingKey: "points"; fieldLabel: "采样点，7 至 15 的奇数"; minimum: 7; maximum: 15 }
    }
    DesktopButton {
        id: advancedButton
        objectName: "autoFocusAdvanced"
        text: (advanced.visible ? "▾ " : "▸ ") + "高级参数"
        onClicked: advanced.visible = !advanced.visible
    }
    GridLayout {
        id: advanced
        visible: false
        Layout.fillWidth: true
        columns: 2; columnSpacing: 8; rowSpacing: 4
        Text { text: "每点帧数 / 1–3"; color: AsterTheme.textSecondary; font.pixelSize: 11 }
        Text { text: "回差补偿 / 步"; color: AsterTheme.textSecondary; font.pixelSize: 11 }
        ParameterInput { id: frames; objectName: "autoFocusFrames"; settingKey: "frames"; fieldLabel: "每点帧数"; minimum: 1; maximum: 3 }
        ParameterInput { id: backlash; objectName: "autoFocusBacklash"; settingKey: "backlash"; fieldLabel: "回差补偿，步" }
    }
    Text {
        objectName: "autoFocusValidation"
        Layout.fillWidth: true
        visible: !root.parametersValid
        text: !step.valid ? "步长须为 1–100000 的整数"
            : !exposure.valid ? "曝光须为 0.001–3600 秒，最多 3 位小数"
            : !points.valid ? "采样点须为 7–15 的奇数"
            : !frames.valid ? "每点帧数须为 1–3 的整数" : "回差补偿须为 0–100000 的整数"
        color: AsterTheme.danger; font.pixelSize: 11; wrapMode: Text.Wrap
    }
    RowLayout {
        Layout.fillWidth: true
        DesktopButton {
            objectName: "autoFocusStart"
            Layout.fillWidth: true
            text: root.active ? "自动对焦中…" : "开始自动对焦"
            primary: true
            enabled: !!root.controller && !root.active && !root.controlsLocked && root.cameraConnected && root.focuserConnected && root.parametersValid
            onClicked: root.start()
        }
        DesktopButton {
            objectName: "autoFocusStop"
            text: "停止"
            visible: root.active
            enabled: root.active
            onClicked: root.controller.cancel()
        }
    }
    Text {
        Layout.fillWidth: true
        visible: !root.active && (!root.cameraConnected || !root.focuserConnected)
        text: "请先连接相机和电调焦器"
        color: AsterTheme.warning; font.pixelSize: 11; wrapMode: Text.Wrap
    }
    RowLayout {
        Layout.fillWidth: true
        Text {
            objectName: "autoFocusStatus"
            Layout.fillWidth: true
            text: root.controller ? root.controller.status : "未开始"
            color: root.active ? AsterTheme.accent : root.controller && root.controller.succeeded
                ? AsterTheme.textPrimary : AsterTheme.textSecondary
            font.family: AsterTheme.uiFont; font.pixelSize: 11; wrapMode: Text.Wrap
        }
        Text {
            text: "星点 " + (root.controller ? root.controller.starCount : 0)
            color: AsterTheme.textSecondary; font.pixelSize: 11
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 196
        color: AsterTheme.canvas
        border.color: AsterTheme.stroke
        radius: 3
        Canvas {
            id: plot
            objectName: "autoFocusPlot"
            anchors.fill: parent
            property var samples: root.controller ? root.controller.samples : []
            property var fitCurve: root.controller ? root.controller.fitCurve : []
            onSamplesChanged: requestPaint()
            onFitCurveChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Accessible.role: Accessible.Graphic
            Accessible.name: "自动对焦曲线：横轴调焦位置，纵轴 HFR；" + samples.length + " 个实测点"
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const data = samples.concat(fitCurve).filter(p => isFinite(p.position) && isFinite(p.hfr))
                const left = 42, right = width - 16, top = 28, bottom = height - 36
                let xmin = data.length ? Math.min.apply(null, data.map(p => p.position)) : 0
                let xmax = data.length ? Math.max.apply(null, data.map(p => p.position)) : 1
                if (xmin === xmax) { xmin -= 1; xmax += 1 }
                const ymin = data.length ? Math.max(0, Math.min.apply(null, data.map(p => p.hfr)) * 0.8) : 0
                const ymax = data.length ? Math.max(ymin + 1, Math.max.apply(null, data.map(p => p.hfr)) * 1.12) : 5
                function px(p) { return left + (p.position - xmin) / (xmax - xmin) * (right - left) }
                function py(p) { return bottom - (p.hfr - ymin) / (ymax - ymin) * (bottom - top) }
                ctx.font = "10px '" + AsterTheme.uiFont + "'"
                ctx.fillStyle = AsterTheme.textSecondary
                ctx.textAlign = "left"
                ctx.fillText("HFR / px", 9, 16)
                ctx.textAlign = "right"
                ctx.fillText("位置 / 步", right, height - 6)
                for (let i = 0; i < 3; ++i) {
                    const y = bottom - i * (bottom - top) / 2
                    ctx.strokeStyle = AsterTheme.stroke; ctx.lineWidth = 1
                    ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(right, y); ctx.stroke()
                    ctx.textAlign = "right"
                    ctx.fillText((ymin + i * (ymax - ymin) / 2).toFixed(1), left - 6, y + 3)
                    if (data.length) {
                        ctx.textAlign = i === 0 ? "left" : i === 2 ? "right" : "center"
                        ctx.fillText(String(Math.round(xmin + i * (xmax - xmin) / 2)), left + i * (right - left) / 2, bottom + 15)
                    }
                }
                ctx.save()
                ctx.beginPath(); ctx.rect(left - 4, top - 4, right - left + 8, bottom - top + 8); ctx.clip()
                ctx.strokeStyle = AsterTheme.warning; ctx.lineWidth = 1.5; ctx.beginPath()
                fitCurve.forEach((p, i) => { if (i === 0) ctx.moveTo(px(p), py(p)); else ctx.lineTo(px(p), py(p)) })
                ctx.stroke()
                ctx.fillStyle = AsterTheme.accent
                samples.forEach(p => { ctx.beginPath(); ctx.arc(px(p), py(p), 3, 0, 2 * Math.PI); ctx.fill() })
                ctx.restore()
            }
        }
        Text {
            objectName: "autoFocusPlotEmpty"
            anchors.centerIn: parent
            visible: plot.samples.length === 0
            text: root.active ? "等待第一组星点测量…" : "开始后显示实测 HFR 曲线"
            color: AsterTheme.textSecondary; font.pixelSize: 11
        }
        Row {
            anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
            spacing: 10
            Text { text: "● 实测"; color: AsterTheme.accent; font.pixelSize: 10 }
            Text { text: "— 拟合"; color: AsterTheme.warning; font.pixelSize: 10 }
        }
    }
    Text {
        objectName: "autoFocusResults"
        Layout.fillWidth: true
        color: AsterTheme.textPrimary; font.family: AsterTheme.uiFont; font.pixelSize: 11; lineHeight: 1.4
        wrapMode: Text.Wrap
        text: "最佳位置  " + (root.controller && root.controller.bestPosition >= 0 ? root.controller.bestPosition + " 步" : "—")
            + "    R²  " + root.formatted(root.controller ? root.controller.rSquared : NaN, 3)
            + "\n拟合 HFR  " + root.formatted(root.controller ? root.controller.bestHfr : NaN, 2)
            + "    验证 HFR  " + root.formatted(root.controller ? root.controller.verifiedHfr : NaN, 2)
    }
    Text {
        Layout.fillWidth: true
        text: "使用当前增益和滤镜 · 每点重新拍摄 RAW，不保存照片\n失败时尝试返回起点；点击停止仅中止，不返回"
        color: AsterTheme.textMuted; font.pixelSize: 11; wrapMode: Text.Wrap; lineHeight: 1.3
    }
}
