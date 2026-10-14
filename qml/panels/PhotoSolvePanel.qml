import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../controls"
import "../theme"

ColumnLayout {
    id: root
    property var solver: null
    property var centeringController: null
    property string targetName: ""
    property bool mountConnected: false
    readonly property bool centeringBusy: centeringController && centeringController.busy
    property bool captureMode: false
    property bool cameraConnected: false
    property real fovHeight: 0
    property bool targetValid: false
    property real targetRaHours: 0
    property real targetDecDeg: 0
    property bool footprintVisible: true
    property bool mapAvailable: false
    readonly property var solution: solver ? solver.result : ({})
    readonly property bool solved: solution.corners !== undefined && solution.corners.length === 4
    readonly property real separationArcmin: {
        if (!solved || !targetValid) return NaN
        const r=Math.PI/180
        const d1=targetDecDeg*r, d2=solution.decDeg*r
        const a=Math.pow(Math.sin((d2-d1)/2),2)
            +Math.cos(d1)*Math.cos(d2)*Math.pow(Math.sin((solution.raHours-targetRaHours)*15*r/2),2)
        return 2*Math.asin(Math.sqrt(Math.max(0,Math.min(1,a))))/r*60
    }
    signal inspectRequested()
    function coordinate(value, hours) {
        const sec=Math.round(Math.abs(value)*3600)
        return (hours ? "" : value<0 ? "−" : "+")
            +String(Math.floor(sec/3600)%(hours?24:360)).padStart(2,"0")+(hours?"h ":"° ")
            +String(Math.floor(sec/60)%60).padStart(2,"0")+(hours?"m ":"′ ")
            +String(sec%60).padStart(2,"0")+(hours?"s":"″")
    }
    spacing: 6
    RowLayout {
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; text:"照片解析"; color:AsterTheme.deviceTitleText; font.pixelSize:12; font.bold:true }
        DesktopButton { text:"ASTAP 设置"; enabled:root.solver && !root.solver.busy && !root.centeringBusy; onClicked:solverDialog.open() }
    }
    Text {
        Layout.fillWidth:true; elide:Text.ElideMiddle; font.pixelSize:10; color:AsterTheme.textMuted
        text:root.solver && root.solver.executable ? root.solver.executable : "先选择 ASTAP 程序，需已安装星表"
        ToolTip.visible:pathHover.hovered; ToolTip.text:text
        HoverHandler { id:pathHover }
    }
    RowLayout {
        visible: root.captureMode
        Layout.fillWidth: true
        Text { text: "解析曝光 / 秒"; color: AsterTheme.textSecondary; font.pixelSize: 11 }
        TextField {
            id: solveExposure
            objectName: "solveExposureSeconds"
            Layout.fillWidth: true
            text: "2"
            enabled: (!root.solver || !root.solver.busy) && !root.centeringBusy
            validator: DoubleValidator { bottom: 0.000001; top: 3600; locale: "C"; notation: DoubleValidator.StandardNotation }
            color: AsterTheme.textPrimary; font.pixelSize: 11
            background: Rectangle { color: AsterTheme.canvas; border.color: AsterTheme.stroke; radius: 3 }
        }
    }
    RowLayout {
        Layout.fillWidth:true
        DesktopButton { objectName:"importSolvePhoto"; visible:!root.captureMode; Layout.fillWidth:true; text:"导入 FITS"; enabled:root.solver && !root.solver.busy && !root.centeringBusy; onClicked:photoDialog.open() }
        DesktopButton {
            objectName:"captureAndSolvePhoto"; visible:root.captureMode; Layout.fillWidth:true; text:"拍摄并解析"; primary:true
            enabled:root.solver && !root.solver.busy && !root.centeringBusy && root.cameraConnected && solveExposure.acceptableInput && Number(solveExposure.text)>0
            onClicked:root.solver.captureAndSolve(Number(solveExposure.text),root.fovHeight)
            ToolTip.visible:hovered; ToolTip.text:"重新拍摄一张 RAW 后解析；使用当前增益和滤镜，不保存到拍摄目录"
        }
    }
    RowLayout {
        Layout.fillWidth:true
        Text { Layout.fillWidth:true; text:root.solver ? root.solver.status : "未解析"; color:root.solved?"#61c99b":AsterTheme.textMuted; font.pixelSize:11 }
        DesktopButton { objectName:"cancelPhotoSolve"; text:"取消"; visible:root.solver && root.solver.busy && !root.centeringBusy; enabled:visible; onClicked:root.solver.cancel() }
    }
    Text {
        objectName:"solvedPhotoDetails"; Layout.fillWidth:true; visible:root.solved
        color:AsterTheme.textPrimary; font.pixelSize:11; wrapMode:Text.Wrap; lineHeight:1.3
        text:!root.solved?"": "RA  "+root.coordinate(root.solution.raHours,true)
            +"\nDec  "+root.coordinate(root.solution.decDeg,false)
            +"\n视场  "+root.solution.widthDeg.toFixed(3)+"° × "+root.solution.heightDeg.toFixed(3)+"°"
            +"\n图像 +Y 方位角  "+root.solution.positionAngle.toFixed(2)+"°"
            +"\n距所选目标  "+(isFinite(root.separationArcmin)?root.separationArcmin.toFixed(2)+"′":"—")
    }
    Text {
        Layout.fillWidth:true; visible:root.solved; elide:Text.ElideMiddle; color:AsterTheme.textMuted; font.pixelSize:10
        text:root.solved ? root.solution.source : ""
        ToolTip.visible:sourceHover.hovered; ToolTip.text:text
        HoverHandler { id:sourceHover }
    }
    Text { Layout.fillWidth:true; visible:root.solved; text:root.solved ? "解析于 "+root.solution.solvedAt : ""; color:AsterTheme.textMuted; font.pixelSize:10 }
    RowLayout {
        visible:root.solved; Layout.fillWidth:true
        DesktopButton { objectName:"toggleSolvedFootprint"; visible:!root.captureMode; Layout.fillWidth:true; text:"绿色：照片视场"; checkable:true; checked:root.footprintVisible; onClicked:root.footprintVisible=checked }
        DesktopButton { objectName:"inspectSolvedPhoto"; text:root.captureMode?"在星图查看":"查看照片位置"; enabled:root.captureMode || root.mapAvailable; onClicked:root.inspectRequested() }
    }
    Text { Layout.fillWidth:true; text:"单独解析不移动/同步赤道仪"; font.pixelSize:10; color:AsterTheme.textMuted }
    Text { Layout.fillWidth:true; visible:root.captureMode; text:"使用当前增益和滤镜 · 拍摄结束恢复原曝光"; font.pixelSize:10; color:AsterTheme.textMuted; wrapMode:Text.Wrap }
    CenteringControls {
        Layout.fillWidth: true
        visible: root.captureMode && !!root.centeringController
        controller: root.centeringController
        ready: root.cameraConnected && root.mountConnected && root.solver && !root.solver.busy
        targetValid: root.targetValid
        targetName: root.targetName
        targetRaHours: root.targetRaHours; targetDecDeg: root.targetDecDeg
        exposureSeconds: Number(solveExposure.text); fovHeight: root.fovHeight
    }
    Rectangle { Layout.fillWidth:true; height:1; color:AsterTheme.stroke }
    FileDialog {
        id:photoDialog; title:"导入星空 FITS 进行解析"; nameFilters:["FITS 图像 (*.fits *.fit *.fts)"]
        onAccepted:if(root.solver) root.solver.solveFile(selectedFile)
    }
    FileDialog {
        id:solverDialog; title:"选择 ASTAP（推荐 astap_cli.exe）"; nameFilters:["ASTAP 程序 (*.exe)","所有文件 (*)"]
        onAccepted:if(root.solver) root.solver.executable=selectedFile.toString()
    }
}
