import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../theme"
import "../controls"
import "../panels"

Rectangle {
    id: root
    objectName: "sequenceWorkspace"
    property var controller: null
    property var autoFocusController: null
    property var deviceManager: null
    property var planner: null
    property int selectedIndex: -1
    readonly property bool compact: height < 760
    readonly property string pageFont: AsterTheme.uiFont
    readonly property var plan: controller ? controller.plan : ({})
    readonly property var rows: controller ? controller.rows : []
    readonly property var selected: selectedIndex >= 0 && selectedIndex < rows.length ? rows[selectedIndex] : ({})
    readonly property bool hasSelection: selectedIndex >= 0 && selectedIndex < rows.length
    readonly property bool locked: controller ? controller.active : false
    readonly property string runState: controller ? controller.state : "Idle"
    readonly property bool focusPreparation: !!controller && ["FocusFilter","FocusGain","Autofocus","FocusOffset"].indexOf(controller.phase)>=0
    readonly property bool filterOffsetsEnabled: !!deviceManager && !!deviceManager.filterFocusSettings && !!deviceManager.filterFocusSettings.enabled
    readonly property string stateLabel: (runState==="Pausing" || runState==="Stopping") && focusPreparation ? "正在停止对焦"
        : runState==="Pausing" && controller.phase==="Centering" ? "正在暂停居中" : ({Idle:"待开始",Running:"运行中",Pausing:"本帧结束后暂停",Paused:"已暂停",Stopping:"正在收尾",Completed:"已完成"})[runState] || runState
    readonly property string phaseLabel: controller ? (controller.phase==="Centering" || focusPreparation ? controller.preparationStatus : (({Filter:"等待滤镜到位",Exposing:"曝光中",Saving:"保存中"})[controller.phase] || "当前帧 —")) : "当前帧 —"
    readonly property var filterOptions: {
        const result=[{text:"当前位置",slot:-1}]
        const devices=deviceManager && deviceManager.devices ? deviceManager.devices : []
        const wheel=devices[3] || {}
        const channels=deviceManager && deviceManager.filterWheelChannels ? deviceManager.filterWheelChannels : []
        const count=wheel.metrics ? Number(wheel.metrics.slots || 0) : 0
        for(let i=0;i<count;++i) result.push({text:(i+1)+" · "+((channels[i] || {}).suffix || "滤镜"),slot:i})
        return result
    }
    signal connectionsRequested()
    signal noticeRequested(string message)
    color: AsterTheme.canvas
    focus: true
    onRowsChanged: {
        if (!rows.length) selectedIndex=-1
        else if (selectedIndex < 0) selectedIndex=0
        else if (selectedIndex >= rows.length) selectedIndex=rows.length-1
    }
    function filterName(slot) {
        const entry=filterOptions.find(option => option.slot===slot)
        return entry ? entry.text : "档位 "+(slot+1)
    }
    function duration(seconds) {
        if (seconds >= 3600) return (seconds/3600).toFixed(1).replace(/\.0$/,"")+" 小时"
        if (seconds >= 60) return (seconds/60).toFixed(1).replace(/\.0$/,"")+" 分钟"
        return Number(seconds.toFixed(3))+" 秒"
    }
    function coordinates() {
        if (plan.ra === "" || plan.ra === undefined || plan.dec === "" || plan.dec === undefined) return "RA —    Dec —"
        const ra=Number(plan.ra), dec=Number(plan.dec)
        if (!isFinite(ra) || !isFinite(dec)) return "RA —    Dec —"
        const r=Math.round(ra*3600)%86400, d=Math.round(Math.abs(dec)*3600)
        function pad(v) { return String(v).padStart(2,"0") }
        return "RA "+pad(Math.floor(r/3600))+"h "+pad(Math.floor(r/60)%60)+"m "+pad(r%60)+"s    Dec "
                +(dec<0?"−":"+")+pad(Math.floor(d/3600))+"° "+pad(Math.floor(d/60)%60)+"′ "+pad(d%60)+"″"
    }
    function commitEdits() {
        if (!locked && plan.centerBeforeCapture) {
            for (const field of [centerExposure,centerTolerance,centerCorrections]) {
                if (!field.acceptableInput) { noticeRequested("请输入有效的居中曝光、容差和校正次数"); field.forceActiveFocus(); return false }
            }
            for (const field of [centerExposure,centerTolerance,centerCorrections]) field.commit()
        }
        if (!hasSelection || locked) return true
        for (const field of [exposureInput,gainInput,countInput]) {
            if (!field.acceptableInput) {
                noticeRequested("请输入有效的曝光、增益和拍摄张数")
                field.forceActiveFocus()
                return false
            }
        }
        for (const field of [exposureInput,gainInput,countInput]) field.commit()
        return true
    }
    function selectTask(index) {
        if (!commitEdits()) return
        root.forceActiveFocus()
        selectedIndex=index
    }
    function commitAndStart() {
        if (!commitEdits()) return
        root.forceActiveFocus()
        if (controller) {
            if (controller.needsCentering) centerDialog.open()
            else controller.start()
        }
    }
    function addTask() {
        if (!controller || locked || !commitEdits()) return
        root.forceActiveFocus()
        controller.addRow()
        selectedIndex=rows.length-1
    }
    function deleteTask() {
        if (!controller || locked || !hasSelection) return
        root.forceActiveFocus()
        controller.removeRow(selectedIndex)
        for (const field of [exposureInput,gainInput,countInput]) field.text=field.sourceText
    }
    function importTarget() {
        if (!controller || !planner || locked) return
        const target=planner.selectedTarget
        controller.setHeader("target",target.id || target.name || "")
        controller.setHeader("ra",target.raHours !== undefined ? Number(target.raHours).toFixed(6) : "")
        controller.setHeader("dec",target.decDeg !== undefined ? Number(target.decDeg).toFixed(6) : "")
    }

    component Caption: Text {
        color: AsterTheme.textSecondary
        font.family: root.pageFont
        font.pixelSize: 12
        elide: Text.ElideRight
    }
    component Action: DesktopButton {
        font.family: root.pageFont
        implicitHeight: 28
        leftPadding: 12; rightPadding: 12
        font.pixelSize: 12
    }
    component Input: TextField {
        property string sourceText: ""
        implicitHeight: 30
        font.family: root.pageFont
        font.pixelSize: 12
        color: enabled ? AsterTheme.textPrimary : AsterTheme.textMuted
        placeholderTextColor: AsterTheme.textMuted
        selectByMouse: true
        padding: 8
        Component.onCompleted: text=sourceText
        onSourceTextChanged: if (!activeFocus) text=sourceText
        background: Rectangle {
            radius: 4; color: AsterTheme.canvas
            border.color: parent.activeFocus ? AsterTheme.accent : AsterTheme.strokeStrong
        }
    }
    component NumberInput: Input {
        property string fieldKey
        property int editingIndex: -1
        sourceText: root.hasSelection ? String(root.selected[fieldKey]) : ""
        enabled: root.hasSelection && !root.locked
        onActiveFocusChanged: if (activeFocus) editingIndex=root.selectedIndex
        function commit() {
            if (!acceptableInput || !root.hasSelection || root.locked) return
            const index=activeFocus && editingIndex >= 0 ? editingIndex : root.selectedIndex
            const value=Number(text)
            if (root.rows[index] && Number(root.rows[index][fieldKey]) !== value)
                root.controller.updateRow(index,fieldKey,value)
        }
        onEditingFinished: commit()
    }
    component Choice: DesktopComboBox {
        id: choice
        implicitHeight: 30
        font.pixelSize: 12
        font.family: root.pageFont
        leftPadding: 10; rightPadding: 28
        palette.text: AsterTheme.textPrimary
        palette.buttonText: AsterTheme.textPrimary
        palette.window: AsterTheme.surfaceRaised
        palette.base: AsterTheme.surfaceRaised
        palette.highlight: AsterTheme.accentSurface
        contentItem: Text { text: choice.displayText; color: choice.enabled ? AsterTheme.textPrimary : AsterTheme.textMuted; font: choice.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        indicator: Image { source: "../assets/icons/chevron-down.svg"; width: 14; height: 14; anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter }
        background: Rectangle { radius: 4; color: AsterTheme.canvas; border.color: choice.activeFocus ? AsterTheme.accent : AsterTheme.strokeStrong }
        delegate: ItemDelegate {
            required property int index
            width: choice.width; height: 28
            text: choice.textAt(index); font: choice.font
            palette.text: AsterTheme.textPrimary
            background: Rectangle { color: parent.hovered || choice.highlightedIndex===index ? AsterTheme.accentSurface : AsterTheme.surfaceRaised }
        }
    }
    component EnabledCheck: CheckBox {
        id: check
        implicitWidth: 32; implicitHeight: 32
        indicator: Rectangle {
            width: 20; height: 20; anchors.centerIn: parent; radius: 4
            color: check.checked ? AsterTheme.accent : AsterTheme.canvas
            border.color: check.activeFocus ? AsterTheme.textPrimary : check.checked ? AsterTheme.accent : AsterTheme.strokeStrong
            Text { anchors.centerIn: parent; text:"✓"; visible:check.checked; color:AsterTheme.accentText; font.pixelSize:14 }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16
        RowLayout {
            Layout.fillWidth: true; spacing: 16
            Caption { text:"拍摄计划"; font.pixelSize:16; font.bold:true; color:AsterTheme.textPrimary }
            Input { objectName:"sequencePlanName"; Layout.preferredWidth:250; sourceText:root.plan.name || ""; enabled:!root.locked; onEditingFinished: if(root.controller) root.controller.setHeader("name",text) }
            Item { Layout.fillWidth:true }
            Action { objectName:"sequenceNewPlan"; text:"新建"; enabled:!root.locked; onClicked:newDialog.open() }
            Action { text:"打开"; enabled:!root.locked; onClicked:openDialog.open() }
            Action { text:"保存计划"; onClicked: if(root.commitEdits()) { root.forceActiveFocus(); saveDialog.open() } }
        }
        Rectangle {
            Layout.fillWidth:true; Layout.preferredHeight:root.plan.centerBeforeCapture ? 228 : 184
            color:AsterTheme.surface; radius:6; border.color:AsterTheme.stroke
            RowLayout {
                anchors.left:parent.left; anchors.right:parent.right; anchors.top:parent.top; anchors.margins:12; height:84; spacing:12
                Rectangle {
                    Layout.preferredWidth:64; Layout.preferredHeight:64; radius:4; color:AsterTheme.canvas
                    Image { anchors.centerIn:parent; width:28; height:28; source:"../assets/icons/crosshair-ui.svg" }
                }
                ColumnLayout {
                    Layout.fillWidth:true; spacing:5
                    Input {
                        objectName:"sequenceTarget"; Layout.fillWidth:true; Layout.maximumWidth:360
                        sourceText:root.plan.target || ""; placeholderText:"选择拍摄目标"; enabled:!root.locked
                        font.pixelSize:14; font.bold:false
                        background: Rectangle { color:parent.activeFocus ? AsterTheme.canvas : "transparent"; radius:4; border.color:parent.activeFocus ? AsterTheme.accent : "transparent" }
                        onEditingFinished: if(root.controller) root.controller.setHeader("target",text)
                    }
                    Caption { Layout.fillWidth:true; text:root.coordinates(); font.family:AsterTheme.monoFont; font.pixelSize:12 }
                    Action { text:"从星图导入"; glyph:"map-ui"; enabled:!root.locked && !!root.planner && !!root.planner.selectedTarget.id; onClicked:root.importTarget() }
                }
                Rectangle { Layout.preferredWidth:1; Layout.fillHeight:true; color:AsterTheme.stroke }
                ColumnLayout {
                    Layout.preferredWidth:Math.max(260,root.width*0.28); spacing:6
                    Caption { text:"保存位置"; font.pixelSize:12 }
                    Input { objectName:"sequenceDirectory"; Layout.fillWidth:true; sourceText:root.plan.directory || ""; enabled:!root.locked; font.pixelSize:12; onEditingFinished: if(root.controller) root.controller.setHeader("directory",text) }
                    RowLayout {
                        Action { text:"选择目录"; enabled:!root.locked; onClicked:folderDialog.open() }
                        Caption { text:root.plan.centerBeforeCapture ? (root.plan.focusBeforeCapture ? "居中、对焦成功后开始任务" : "居中成功后开始任务") : "仅记录目标，不自动指向"; font.pixelSize:11; Layout.fillWidth:true }
                    }
                }
            }
            ColumnLayout {
            anchors.left:parent.left; anchors.right:parent.right; anchors.bottom:parent.bottom; anchors.margins:12; spacing:6
            RowLayout {
                EnabledCheck { objectName:"sequenceCenterEnabled"; checked:!!root.plan.centerBeforeCapture; enabled:!root.locked; onToggled:if(root.controller) root.controller.setHeader("centerBeforeCapture",checked) }
                Caption { text:"拍摄前指向并居中"; color:AsterTheme.textPrimary; font.pixelSize:12 }
                Caption { Layout.fillWidth:true; text:root.plan.centerBeforeCapture ? "仅 LIGHT · 未达标不曝光 · 不同步赤道仪模型" : "可选，关闭时直接拍摄"; font.pixelSize:11 }
            }
            RowLayout {
                visible:!!root.plan.centerBeforeCapture; spacing:8
                Caption { text:"解析曝光 / 秒"; font.pixelSize:11 }
                CenterInput { id:centerExposure; objectName:"sequenceCenterExposure"; fieldKey:"centerExposure"; fallback:2; validator:DoubleValidator { bottom:0.001; top:3600; decimals:3; locale:"C" } }
                Caption { text:"允许误差 / 角分"; font.pixelSize:11 }
                CenterInput { id:centerTolerance; objectName:"sequenceCenterTolerance"; fieldKey:"centerTolerance"; fallback:1; validator:DoubleValidator { bottom:0.1; top:10; decimals:2; locale:"C" } }
                Caption { text:"最多校正 / 次"; font.pixelSize:11 }
                CenterInput { id:centerCorrections; objectName:"sequenceCenterCorrections"; fieldKey:"centerCorrections"; fallback:3; validator:IntValidator { bottom:1; top:3 } }
                Item { Layout.fillWidth:true }
            }
            RowLayout {
                Layout.fillWidth:true
                EnabledCheck {
                    objectName:"sequenceFocusEnabled"; checked:!!root.plan.focusBeforeCapture; enabled:!root.locked
                    Accessible.name:"开始拍摄前自动对焦"
                    onToggled:if(root.controller) root.controller.setHeader("focusBeforeCapture",checked)
                }
                Caption { text:"开始拍摄前自动对焦"; color:AsterTheme.textPrimary; font.pixelSize:12 }
                EnabledCheck {
                    objectName:"sequenceFilterFocusEnabled"; checked:!!root.plan.focusAfterFilterChange; enabled:!root.locked
                    Accessible.name:"换镜后自动对焦"
                    Layout.leftMargin:8
                    onToggled:if(root.controller) root.controller.setHeader("focusAfterFilterChange",checked)
                }
                Caption { text:"换镜后自动对焦"; color:AsterTheme.textPrimary; font.pixelSize:12 }
                Caption {
                    objectName:"sequenceFilterFocusHint"; Layout.fillWidth:true; font.pixelSize:11
                    text:root.filterOffsetsEnabled ? "已启用焦点偏移，代替换镜对焦"
                        : root.plan.focusAfterFilterChange ? "仅换镜后的 LIGHT · 同滤镜不重复对焦" : "对焦参数共用 · 验证成功后拍摄"
                }
                Action { objectName:"sequenceAutoFocusSettings"; text:"参数 / 曲线"; enabled:!!root.autoFocusController; onClicked:focusPopup.open() }
            }
        }
        }
        RowLayout {
            Layout.fillWidth:true; Layout.fillHeight:true; spacing:12
            Rectangle {
                Layout.fillWidth:true; Layout.fillHeight:true
                color:AsterTheme.surface; radius:6; border.color:AsterTheme.stroke
                ColumnLayout {
                    anchors.fill:parent; anchors.margins:16; spacing:12
                    RowLayout {
                        Layout.fillWidth:true
                        Caption { text:"拍摄任务"; font.pixelSize:14; font.bold:true; color:AsterTheme.textPrimary }
                        Caption { text:root.rows.length+" 项 · "+(root.controller ? root.controller.total : 0)+" 帧"; font.pixelSize:12 }
                        Item { Layout.fillWidth:true }
                        Action { objectName:"sequenceAddRow"; text:"+ 添加任务"; checked:true; enabled:!root.locked && !!root.controller; onClicked:root.addTask() }
                    }
                    ListView {
                        id: tasks
                        objectName:"sequenceRows"
                        Layout.fillWidth:true; Layout.fillHeight:true
                        clip:true; spacing:6
                        model:root.rows
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ItemDelegate {
                            id: task
                            required property int index
                            required property var modelData
                            objectName:"sequenceTask"+index
                            width:tasks.width; height:64
                            padding:10
                            Accessible.name:"任务 "+(index+1)+" "+modelData.type+" "+root.filterName(modelData.filter)
                            onClicked:root.selectTask(index)
                            background: Rectangle {
                                radius:5
                                color:root.selectedIndex===task.index ? AsterTheme.accentSurface : task.hovered ? AsterTheme.surfaceHover : "transparent"
                                border.color:root.selectedIndex===task.index ? AsterTheme.accent : AsterTheme.stroke
                                Rectangle { width:3; height:parent.height-12; y:6; color:AsterTheme.accent; visible:root.selectedIndex===task.index }
                            }
                            contentItem: RowLayout {
                                spacing:10
                                EnabledCheck { enabled:!root.locked; checked:task.modelData.enabled; Accessible.name:"启用任务 "+(task.index+1); onToggled:root.controller.updateRow(task.index,"enabled",checked) }
                                Caption { text:String(task.index+1).padStart(2,"0"); font.family:AsterTheme.monoFont; Layout.preferredWidth:24 }
                                ColumnLayout {
                                    Layout.fillWidth:true; spacing:7
                                    RowLayout {
                                        Caption { text:root.filterName(task.modelData.filter); color:AsterTheme.textPrimary; font.pixelSize:12; font.bold:false; Layout.fillWidth:true }
                                        Rectangle {
                                            implicitWidth:typeText.implicitWidth+14; implicitHeight:23; radius:3; color:AsterTheme.surfaceRaised
                                            Caption { id:typeText; anchors.centerIn:parent; text:task.modelData.type; font.pixelSize:11 }
                                        }
                                    }
                                    Caption { Layout.fillWidth:true; text:task.modelData.exposure+" 秒 × "+task.modelData.count+" 张    增益 "+task.modelData.gain; font.pixelSize:12 }
                                }
                                ColumnLayout {
                                    Layout.preferredWidth:84; spacing:7
                                    Caption { text:task.modelData.saved+" / "+task.modelData.count; font.family:AsterTheme.monoFont; color:AsterTheme.textPrimary }
                                    Caption {
                                        Layout.fillWidth:true; font.pixelSize:11
                                        text:!task.modelData.enabled ? "已禁用" : root.locked && root.controller.currentRow===task.index ? root.phaseLabel : task.modelData.saved>=task.modelData.count ? "已完成" : root.selectedIndex===task.index ? "已选中" : "待执行"
                                        color:root.selectedIndex===task.index ? AsterTheme.accent : AsterTheme.textMuted
                                    }
                                }
                            }
                        }
                        Column {
                            anchors.centerIn:parent; spacing:12; visible:tasks.count===0
                            Caption { anchors.horizontalCenter:parent.horizontalCenter; text:"还没有拍摄任务"; font.pixelSize:14; color:AsterTheme.textPrimary }
                            Caption { anchors.horizontalCenter:parent.horizontalCenter; text:"添加任务，设置曝光与拍摄张数" }
                            Action { anchors.horizontalCenter:parent.horizontalCenter; text:"+ 添加第一项任务"; primary:true; onClicked:root.addTask() }
                        }
                    }
                    Caption { text:"选择任务，在右侧调整拍摄参数"; font.pixelSize:11; color:AsterTheme.textMuted }
                }
            }
            Rectangle {
                objectName:"sequenceEditorPanel"
                Layout.preferredWidth:Math.min(420,Math.max(340,root.width*0.34))
                Layout.fillHeight:true
                color:AsterTheme.surface; radius:6; border.color:AsterTheme.stroke
                ScrollView {
                    anchors.fill:parent; anchors.margins:12
                    contentWidth:availableWidth
                    ColumnLayout {
                        width:parent.width; spacing:root.compact ? 8 : 14
                        RowLayout {
                            Layout.fillWidth:true
                            Caption { text:root.hasSelection ? "任务 "+String(root.selectedIndex+1).padStart(2,"0") : "任务参数"; font.pixelSize:14; font.bold:true; color:AsterTheme.textPrimary; Layout.fillWidth:true }
                            Caption { text:root.selected.enabled ? "已启用" : "已禁用"; visible:root.hasSelection; font.pixelSize:12 }
                            EnabledCheck { visible:root.hasSelection; enabled:!root.locked; checked:!!root.selected.enabled; onToggled:root.controller.updateRow(root.selectedIndex,"enabled",checked) }
                        }
                        Caption { visible:!root.hasSelection; text:"选择左侧任务后编辑"; color:AsterTheme.textPrimary }
                        GridLayout {
                            Layout.fillWidth:true; columns:2; columnSpacing:14; rowSpacing:8
                            Caption { text:"图像类型"; font.pixelSize:12 }
                            Caption { text:"滤镜"; font.pixelSize:12 }
                            Choice { Layout.fillWidth:true; model:["LIGHT","DARK","FLAT","BIAS"]; currentIndex:root.hasSelection ? model.indexOf(root.selected.type) : -1; enabled:root.hasSelection && !root.locked; onActivated:index=>root.controller.updateRow(root.selectedIndex,"type",model[index]) }
                            Choice { Layout.fillWidth:true; model:root.filterOptions; textRole:"text"; currentIndex:root.hasSelection ? root.filterOptions.findIndex(option=>option.slot===root.selected.filter) : -1; displayText:currentIndex>=0 ? currentText : root.hasSelection ? root.filterName(root.selected.filter) : "—"; enabled:root.hasSelection && !root.locked; onActivated:index=>root.controller.updateRow(root.selectedIndex,"filter",root.filterOptions[index].slot) }
                            Caption { text:"曝光时间 / 秒"; font.pixelSize:12; Layout.topMargin:8 }
                            Caption { text:"拍摄张数"; font.pixelSize:12; Layout.topMargin:8 }
                            NumberInput { id:exposureInput; objectName:"sequenceExposure"+root.selectedIndex; fieldKey:"exposure"; Layout.fillWidth:true; validator:DoubleValidator {bottom:0.000001; top:4294.967295; decimals:6; locale:"C"} }
                            NumberInput { id:countInput; fieldKey:"count"; Layout.fillWidth:true; validator:IntValidator {bottom:1; top:100000} }
                            Caption { text:"增益"; font.pixelSize:12; Layout.topMargin:8; Layout.columnSpan:2 }
                            NumberInput { id:gainInput; fieldKey:"gain"; Layout.fillWidth:true; validator:IntValidator {bottom:0; top:65535} }
                            Item { Layout.fillWidth:true }
                        }
                        Rectangle { Layout.fillWidth:true; height:1; color:AsterTheme.stroke; Layout.topMargin:4 }
                        RowLayout { Layout.fillWidth:true; Caption { text:"本任务总曝光"; Layout.fillWidth:true } Caption { text:root.hasSelection ? root.duration(Number(root.selected.exposure)*Number(root.selected.count)) : "—"; color:AsterTheme.textPrimary } }
                        RowLayout { Layout.fillWidth:true; Caption { text:"输出格式"; Layout.fillWidth:true } Caption { text:"16 位 RAW / FITS"; color:AsterTheme.textPrimary } }
                        Caption { Layout.fillWidth:true; text:"DARK / BIAS 请自行遮光；FLAT 请准备平场光源。"; wrapMode:Text.Wrap; elide:Text.ElideNone; font.pixelSize:11; color:AsterTheme.textMuted }
                        RowLayout {
                            Layout.fillWidth:true
                            Caption { text:root.locked ? "运行期间参数已锁定" : "编辑完成后生效"; font.pixelSize:11; Layout.fillWidth:true }
                            Action { objectName:"sequenceDeleteRow"; text:"删除任务"; palette.buttonText:AsterTheme.danger; enabled:root.hasSelection && !root.locked; onClicked:root.deleteTask() }
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth:true; Layout.preferredHeight:64
            color:AsterTheme.surface; radius:6; border.color:AsterTheme.stroke
            RowLayout {
                anchors.fill:parent; anchors.margins:12; spacing:12
                ColumnLayout {
                    Layout.preferredWidth:240; spacing:6
                    Caption { text:root.stateLabel; color:root.locked ? AsterTheme.accent : AsterTheme.textPrimary; font.pixelSize:13; font.bold:true }
                    Caption { text:"已保存 "+(root.controller ? root.controller.saved+" / "+root.controller.total : "0 / 0")+" 帧 · 已曝光 "+(root.controller ? root.controller.exposed : 0)+" · 失败 "+(root.controller ? root.controller.failed : 0); font.pixelSize:11 }
                }
                ColumnLayout {
                    Layout.fillWidth:true; spacing:8
                    Caption { Layout.fillWidth:true; text:root.phaseLabel; font.pixelSize:12 }
                    ProgressBar {
                        id:frameProgress
                        Layout.fillWidth:true; value:root.controller ? root.controller.progress : 0
                        background:Rectangle { implicitHeight:5; radius:2; color:AsterTheme.stroke }
                        contentItem:Item { implicitHeight:5; Rectangle { width:parent.width*frameProgress.visualPosition; height:parent.height; radius:2; color:AsterTheme.accent } }
                    }
                }
                Action { objectName:"sequenceStart"; text:root.runState==="Paused" ? (root.controller.needsCentering ? "重试居中" : root.controller.needsAutofocus ? "重试对焦" : "继续拍摄") : "开始拍摄"; implicitHeight:30; implicitWidth:104; primary:true; enabled:!!root.controller && (!root.locked || root.runState==="Paused"); onClicked:root.commitAndStart() }
                Action { objectName:"sequencePause"; text:"暂停"; enabled:root.runState==="Running"; onClicked:root.controller.pause() }
                Action { objectName:"sequenceStop"; text:root.runState==="Stopping" ? "重试停止" : "停止"; enabled:root.locked && (root.runState!=="Stopping" || root.controller.phase==="Centering" || root.focusPreparation); onClicked:root.controller.stop() }
                Action { objectName:"sequenceReset"; text:"重置进度"; enabled:!!root.controller && !root.locked; onClicked:root.controller.resetProgress() }
            }
        }
    }
    Popup {
        id:focusPopup; objectName:"sequenceAutoFocusPopup"
        x:Math.max(8,root.width-width-16); y:Math.max(8,(root.height-height)/2)
        width:Math.min(420,root.width-32)
        height:Math.min(root.height-32,focusPanel.implicitHeight+24)
        padding:12; modal:false
        background:Rectangle { color:AsterTheme.surface; border.color:AsterTheme.stroke; radius:4 }
        contentItem:ScrollView {
            clip:true
            AutoFocusPanel {
                id:focusPanel
                width:focusPopup.availableWidth
                controller:root.autoFocusController
                controlsLocked:root.locked
                cameraConnected:!!root.deviceManager && root.deviceManager.devices.some(device=>device.role===1 && device.connected)
                focuserConnected:!!root.deviceManager && root.deviceManager.devices.some(device=>device.role===4 && device.connected)
                onNoticeRequested:message=>root.noticeRequested(message)
            }
        }
    }
    component CenterInput: Input {
        property string fieldKey
        property real fallback
        Layout.preferredWidth:68
        sourceText:String(root.plan[fieldKey] === undefined ? fallback : root.plan[fieldKey])
        enabled:!root.locked
        function commit() { if(root.controller && acceptableInput) root.controller.setHeader(fieldKey,Number(text)) }
        onEditingFinished:commit()
    }
    Dialog {
        id:centerDialog; objectName:"sequenceCenterDialog"
        anchors.centerIn:parent; title:"确认启动拍摄计划"; modal:true; width:Math.min(440,root.width-32); padding:16
        background:Rectangle { color:AsterTheme.surface; border.color:AsterTheme.stroke; radius:6 }
        header:Label { text:centerDialog.title; padding:16; bottomPadding:8; color:AsterTheme.textPrimary; font.family:AsterTheme.uiFont; font.pixelSize:14; font.bold:true }
        contentItem:Label {
            text:"将移动赤道仪至："+(root.plan.target || "未选择目标")+"\n"+root.coordinates()+"  J2000\n\n先拍摄解析并校正居中"+(root.plan.focusBeforeCapture ? "，再自动对焦，均成功后才开始任务。" : "，成功后才开始任务。")+"\n请确认站点、时间及运动空间；不会自动解除停放或修改同步模型。"
            wrapMode:Text.Wrap; color:AsterTheme.textPrimary; font.family:AsterTheme.uiFont; font.pixelSize:12
        }
        footer:Item {
            implicitHeight:52
            RowLayout {
                anchors.right:parent.right; anchors.bottom:parent.bottom; anchors.margins:16; spacing:8
                Action { id:cancelCenter; objectName:"sequenceCenterCancel"; text:"取消"; implicitWidth:72; checked:true; onClicked:centerDialog.reject() }
                Action { objectName:"sequenceCenterConfirm"; text:"指向居中并拍摄"; implicitWidth:130; primary:true; onClicked:centerDialog.accept() }
            }
        }
        onOpened:cancelCenter.forceActiveFocus()
        onAccepted:if(root.controller) root.controller.start()
    }
    FileDialog { id:openDialog; title:"打开拍摄计划"; nameFilters:["拍摄计划 (*.json)"]; onAccepted:if(root.controller) { root.forceActiveFocus(); root.controller.loadPlan(selectedFile) } }
    FileDialog { id:saveDialog; title:"保存拍摄计划"; fileMode:FileDialog.SaveFile; defaultSuffix:"json"; nameFilters:["拍摄计划 (*.json)"]; onAccepted:if(root.controller) root.controller.savePlan(selectedFile) }
    FolderDialog { id:folderDialog; title:"选择图像保存目录"; onAccepted:if(root.controller) root.controller.setHeader("directory",decodeURIComponent(selectedFolder.toString().replace(/^file:\/\//,"").replace(/^\/(?=[A-Za-z]:)/,""))) }
    Dialog {
        id:newDialog; objectName:"sequenceNewDialog"
        anchors.centerIn:parent; title:"新建计划"; modal:true
        width:Math.min(360,root.width-32); padding:16
        palette.window:AsterTheme.surface; palette.windowText:AsterTheme.textPrimary; palette.text:AsterTheme.textPrimary; palette.buttonText:AsterTheme.textPrimary
        background:Rectangle { color:AsterTheme.surface; border.color:AsterTheme.stroke; radius:6 }
        header:Label { text:newDialog.title; padding:16; bottomPadding:8; color:AsterTheme.textPrimary; font.family:AsterTheme.uiFont; font.pixelSize:14; font.bold:true }
        contentItem:Label { text:"新建后，当前未保存的计划修改将丢失。是否继续？"; wrapMode:Text.Wrap; color:AsterTheme.textPrimary; font.family:AsterTheme.uiFont; font.pixelSize:12 }
        footer:Item {
            implicitHeight:52
            RowLayout {
                anchors.right:parent.right; anchors.bottom:parent.bottom; anchors.margins:16; spacing:8
                Action { id:cancelNew; objectName:"sequenceNewCancel"; text:"取消"; implicitWidth:72; checked:true; onClicked:newDialog.reject() }
                Action { objectName:"sequenceNewConfirm"; text:"新建计划"; implicitWidth:88; primary:true; onClicked:newDialog.accept() }
            }
        }
        onOpened:cancelNew.forceActiveFocus()
        onAccepted:if(root.controller) { root.forceActiveFocus(); root.controller.newPlan() }
    }
}
