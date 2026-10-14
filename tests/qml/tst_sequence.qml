import QtQuick
import QtTest
import "../../qml/pages"

TestCase {
    name: "SequenceWorkspace"
    when: windowShown
    visible: true
    width: 1440; height: 900
    QtObject {
        id: fakeSequence
        property var plan: ({name:"测试计划",target:"M31",directory:"D:/frames",ra:"0.7",dec:"41.2"})
        property var rows: []
        property string state: "Idle"
        property string phase: ""
        property bool active: false
        property int currentRow: -1
        property int saved: 0
        property int exposed: 0
        property int failed: 0
        property int total: 2
        property real progress: 0
        property bool needsCentering: !!plan.centerBeforeCapture
        property bool needsAutofocus: !!plan.focusBeforeCapture
        property string preparationStatus: "正在指向"
        function addRow() { rows = [{enabled:true,type:"LIGHT",filter:-1,exposure:2,gain:100,count:2,saved:0}] }
        function setHeader(key,value) { const copy = Object.assign({},plan); copy[key]=value; plan=copy }
        function updateRow(index,key,value) { const next = rows.slice(); next[index] = Object.assign({},next[index]); next[index][key]=value; rows=next }
        function removeRow(index) { const next=rows.slice(); next.splice(index,1); rows=next }
        function start() { active=true; state="Running"; phase="Exposing" }
        function pause() { state="Pausing" }
        function stop() { active=false; state="Idle" }
        function newPlan() { rows=[] }
    }
    QtObject {
        id: fakeAutoFocus
        property var settings: ({step:80,points:9,exposure:3,frames:1,backlash:0})
        property bool busy: false
        property bool succeeded: false
        property string status: "未开始"
        property var samples: []
        property var fitCurve: []
        property int bestPosition: -1
        property real bestHfr: NaN
        property real verifiedHfr: NaN
        property real rSquared: NaN
        property int starCount: 0
        property int cancelCount: 0
        function setSetting(key,value) { const next=Object.assign({},settings); next[key]=value; settings=next }
        function start() { busy=true; return true }
        function cancel() { cancelCount++; busy=false }
    }
    QtObject {
        id: fakeDevices
        property var devices: [{role:1,connected:true},{role:4,connected:true}]
        property var filterWheelChannels: []
        property var filterFocusSettings: ({enabled:false,reference:1,editable:true,baselineValid:false})
    }
    Component { id: page; SequenceWorkspace { width: 1180; height: 680; controller: fakeSequence } }
    function init() {
        fakeSequence.rows=[]; fakeSequence.state="Idle"; fakeSequence.phase=""; fakeSequence.active=false
        fakeSequence.plan={name:"测试计划",target:"M31",directory:"D:/frames",ra:"0.7",dec:"41.2"}
        fakeSequence.preparationStatus="正在指向"
        fakeAutoFocus.busy=false; fakeAutoFocus.cancelCount=0
        fakeAutoFocus.settings={step:80,points:9,exposure:3,frames:1,backlash:0}
        fakeDevices.filterFocusSettings={enabled:false,reference:1,editable:true,baselineValid:false}
    }
    function test_filterChangeFocusOptionAndOffsetPriorityHint() {
        const ui=createTemporaryObject(page,this,{autoFocusController:fakeAutoFocus,deviceManager:fakeDevices})
        verify(waitForRendering(ui))
        const toggle=findChild(ui,"sequenceFilterFocusEnabled")
        verify(toggle !== null)
        verify(!toggle.checked)
        mouseClick(toggle,toggle.width/2,toggle.height/2)
        compare(fakeSequence.plan.focusAfterFilterChange,true)
        verify(!fakeSequence.plan.focusBeforeCapture)
        fakeDevices.filterFocusSettings=Object.assign({},fakeDevices.filterFocusSettings,{enabled:true})
        const hint=findChild(ui,"sequenceFilterFocusHint")
        verify(hint !== null)
        verify(hint.text.indexOf("偏移")>=0)
        verify(hint.text.indexOf("代替")>=0)
        fakeSequence.active=true; fakeSequence.state="Running"; fakeSequence.phase="FocusOffset"
        fakeSequence.preparationStatus="正在应用滤镜焦点偏移"
        compare(ui.phaseLabel,fakeSequence.preparationStatus)
        verify(!toggle.enabled)
        fakeSequence.state="Stopping"
        verify(findChild(ui,"sequenceStop").enabled)
    }
    function test_autofocusOptionReusesSettingsAndLocksDuringSequence() {
        const ui=createTemporaryObject(page,this,{autoFocusController:fakeAutoFocus,deviceManager:fakeDevices})
        verify(ui !== null)
        verify(waitForRendering(ui))
        const toggle=findChild(ui,"sequenceFocusEnabled")
        verify(toggle !== null)
        verify(!toggle.checked, "Legacy plans without focusBeforeCapture must not enable movement")
        mouseClick(toggle,toggle.width/2,toggle.height/2)
        compare(fakeSequence.plan.focusBeforeCapture,true)
        mouseClick(findChild(ui,"sequenceAutoFocusSettings"))
        const popup=findChild(ui,"sequenceAutoFocusPopup")
        tryCompare(popup,"opened",true)
        const step=findChild(ui,"autoFocusStep")
        verify(step.enabled)
        verify(findChild(ui,"autoFocusStart").enabled)
        step.text="120"; step.editingFinished()
        compare(fakeAutoFocus.settings.step,120)
        fakeSequence.active=true; fakeSequence.state="Running"; fakeSequence.phase="Autofocus"
        fakeAutoFocus.busy=true
        verify(!toggle.enabled)
        verify(!step.enabled)
        verify(!findChild(ui,"autoFocusStart").enabled)
        const stop=findChild(ui,"autoFocusStop")
        verify(stop.enabled)
        mouseClick(stop)
        compare(fakeAutoFocus.cancelCount,1)
        popup.close()
        verify(findChild(ui,"sequenceAutoFocusSettings").enabled)
    }
    function test_autofocusPreparationPauseRetryAndStopStates() {
        const ui=createTemporaryObject(page,this)
        fakeSequence.addRow()
        fakeSequence.setHeader("focusBeforeCapture",true)
        fakeSequence.active=true; fakeSequence.state="Running"
        for (const phase of ["FocusFilter","FocusGain","Autofocus"]) {
            fakeSequence.phase=phase
            fakeSequence.preparationStatus="对焦准备："+phase
            compare(ui.phaseLabel,fakeSequence.preparationStatus)
        }
        fakeSequence.state="Pausing"
        compare(ui.stateLabel,"正在停止对焦")
        fakeSequence.state="Stopping"
        const stop=findChild(ui,"sequenceStop")
        verify(stop.enabled)
        compare(stop.text,"重试停止")
        fakeSequence.state="Paused"
        const start=findChild(ui,"sequenceStart")
        compare(start.text,"重试对焦")
        verify(start.enabled)
        start.clicked()
        compare(fakeSequence.state,"Running")
    }
    function test_centeringRequiresMovementConfirmation() {
        const ui=createTemporaryObject(page,this)
        fakeSequence.addRow()
        verify(waitForRendering(ui))
        const toggle=findChild(ui,"sequenceCenterEnabled")
        verify(toggle !== null)
        mouseClick(toggle,toggle.width/2,toggle.height/2)
        compare(fakeSequence.plan.centerBeforeCapture,true)
        const exposure=findChild(ui,"sequenceCenterExposure")
        verify(exposure.visible)
        exposure.forceActiveFocus(); exposure.text="3.5"
        ui.commitAndStart()
        compare(fakeSequence.plan.centerExposure,3.5)
        compare(fakeSequence.state,"Idle")
        const dialog=findChild(ui,"sequenceCenterDialog")
        tryCompare(dialog,"opened",true)
        findChild(dialog,"sequenceCenterCancel").clicked()
        compare(fakeSequence.state,"Idle")
        ui.commitAndStart()
        tryCompare(dialog,"opened",true)
        findChild(dialog,"sequenceCenterConfirm").clicked()
        compare(fakeSequence.state,"Running")
        verify(!toggle.enabled)
        verify(!exposure.enabled)
    }
    function test_addAndRunControls() {
        const ui=createTemporaryObject(page,this)
        verify(ui !== null); verify(waitForRendering(ui))
        findChild(ui,"sequenceAddRow").clicked()
        tryCompare(findChild(ui,"sequenceRows"),"count",1)
        findChild(ui,"sequenceStart").clicked()
        compare(fakeSequence.state,"Running")
        verify(!findChild(ui,"sequenceAddRow").enabled)
        verify(!findChild(ui,"sequenceTarget").enabled)
        verify(!findChild(ui,"sequenceStart").enabled)
        findChild(ui,"sequencePause").clicked()
        compare(fakeSequence.state,"Pausing")
        verify(!findChild(ui,"sequencePause").enabled)
        findChild(ui,"sequenceStop").clicked()
        verify(findChild(ui,"sequenceAddRow").enabled)
    }
    function test_newPlanConfirmation() {
        const ui=createTemporaryObject(page,this)
        fakeSequence.addRow()
        verify(waitForRendering(ui))
        const trigger=findChild(ui,"sequenceNewPlan")
        verify(trigger !== null)
        trigger.clicked()
        const dialog=findChild(ui,"sequenceNewDialog")
        tryCompare(dialog,"opened",true)
        const cancel=findChild(dialog,"sequenceNewCancel")
        const confirm=findChild(dialog,"sequenceNewConfirm")
        verify(cancel !== null && confirm !== null)
        compare(cancel.text,"取消")
        compare(confirm.text,"新建计划")
        verify(confirm.primary)
        verify(cancel.background.color.r < 0.3)
        cancel.clicked()
        tryCompare(dialog,"visible",false)
        compare(fakeSequence.rows.length,1)
        trigger.clicked()
        tryCompare(dialog,"opened",true)
        confirm.clicked()
        tryCompare(dialog,"visible",false)
        compare(fakeSequence.rows.length,0)
    }
    function test_selectedTaskOwnsEditor() {
        const ui=createTemporaryObject(page,this)
        fakeSequence.rows=[{enabled:true,type:"LIGHT",filter:-1,exposure:2,gain:100,count:2,saved:0},
                           {enabled:true,type:"DARK",filter:-1,exposure:8,gain:200,count:3,saved:0}]
        verify(waitForRendering(ui))
        const second=findChild(ui,"sequenceTask1")
        verify(second !== null,"Task summary must be selectable")
        second.clicked()
        compare(ui.selectedIndex,1)
        const editor=findChild(ui,"sequenceExposure1")
        verify(editor !== null)
        editor.forceActiveFocus(); editor.text="9.5"
        ui.commitAndStart()
        compare(fakeSequence.rows[1].exposure,9.5)
        compare(fakeSequence.rows[0].exposure,2)
        verify(!editor.enabled)
    }
    function test_switchPreservesDraftAndDeletion() {
        const ui=createTemporaryObject(page,this)
        fakeSequence.rows=[{enabled:true,type:"LIGHT",filter:-1,exposure:2,gain:100,count:2,saved:0},
                           {enabled:true,type:"DARK",filter:-1,exposure:8,gain:200,count:3,saved:0}]
        verify(waitForRendering(ui))
        const field=findChild(ui,"sequenceExposure0")
        field.forceActiveFocus(); field.text="3.5"
        fakeSequence.updateRow(0,"saved",1)
        compare(field.text,"3.5")
        ui.selectTask(1)
        compare(fakeSequence.rows[0].exposure,3.5)
        compare(field.text,"8")
        field.forceActiveFocus(); field.text="0"
        ui.selectTask(0)
        compare(ui.selectedIndex,1)
        ui.deleteTask()
        compare(ui.selectedIndex,0)
        compare(field.text,"3.5")
        ui.deleteTask()
        compare(ui.selectedIndex,-1)
        verify(!field.enabled)
    }
    function test_deleteClearsInvalidDraftWithEqualValues() {
        const ui=createTemporaryObject(page,this)
        fakeSequence.addRow()
        fakeSequence.rows=[fakeSequence.rows[0],Object.assign({},fakeSequence.rows[0])]
        verify(waitForRendering(ui))
        const field=findChild(ui,"sequenceExposure0")
        field.forceActiveFocus(); field.text="0"
        ui.deleteTask()
        compare(ui.selectedIndex,0)
        compare(field.text,"2")
        verify(field.acceptableInput)
        ui.commitAndStart()
        compare(fakeSequence.state,"Running")
    }
    function test_invalidExposureDoesNotStart() {
        const ui=createTemporaryObject(page,this)
        fakeSequence.addRow()
        tryCompare(findChild(ui,"sequenceRows"),"count",1)
        const field=findChild(ui,"sequenceExposure0")
        verify(field !== null)
        field.forceActiveFocus(); field.text="0"
        compare(field.acceptableInput,false)
        ui.commitAndStart()
        compare(fakeSequence.state,"Idle")
    }
    function test_editIsCommittedBeforeStart() {
        const ui=createTemporaryObject(page,this)
        fakeSequence.addRow()
        tryCompare(findChild(ui,"sequenceRows"),"count",1)
        const field=findChild(ui,"sequenceExposure0")
        field.forceActiveFocus(); field.text="3.5"
        ui.commitAndStart()
        compare(fakeSequence.rows[0].exposure,3.5)
        compare(fakeSequence.state,"Running")
    }
}
