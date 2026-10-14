import QtQuick
import QtTest
import "../../qml/panels"

TestCase {
    name: "PhotoSolvePanel"
    when: windowShown
    width: 300; height: 600
    visible: true
    Component { id: panelComponent; PhotoSolvePanel { width: 280 } }
    Component {
        id: solverComponent
        QtObject {
            property bool busy: false
            property bool currentAvailable: false
            property string currentLabel: "RAW 2026-10-13"
            property string status: "未解析"
            property string executable: "astap_cli.exe"
            property var result: ({})
            property real requestedFov: -1
            property real requestedExposure: -1
            function captureAndSolve(seconds,fov) { requestedExposure=seconds; requestedFov=fov; busy=true }
            function cancel() { busy=false }
        }
    }
    function test_currentPhotoControlsAndIndependentResult() {
        const solver=createTemporaryObject(solverComponent,this)
        const panel=createTemporaryObject(panelComponent,this,{solver:solver,fovHeight:1.5,captureMode:true})
        const start=findChild(panel,"captureAndSolvePhoto")
        verify(!start.enabled)
        panel.cameraConnected=true
        verify(start.enabled)
        start.clicked()
        compare(solver.requestedFov,1.5)
        compare(solver.requestedExposure,2)
        verify(findChild(panel,"solveCurrentPhoto")===null)
        verify(!start.enabled)
        verify(findChild(panel,"cancelPhotoSolve").enabled)
        solver.busy=false
        solver.result={raHours:0,decDeg:0,widthDeg:1,heightDeg:0.5,pixelScale:2,
            positionAngle:0,source:"test.fits",solvedAt:"2026-10-13",corners:[[0,0],[1,0],[1,1],[0,1]]}
        verify(findChild(panel,"solvedPhotoDetails").visible)
        panel.targetValid=true; panel.targetRaHours=0; panel.targetDecDeg=1
        verify(Math.abs(panel.separationArcmin-60)<0.00001)
        compare(solver.result.raHours,0)
        panel.targetRaHours=23.999; panel.targetDecDeg=0
        verify(Math.abs(panel.separationArcmin-0.9)<0.00001)
        solver.result={}
        verify(!findChild(panel,"solvedPhotoDetails").visible)
    }
    function test_skyPanelKeepsImportOnly() {
        const solver=createTemporaryObject(solverComponent,this)
        const panel=createTemporaryObject(panelComponent,this,{solver:solver})
        verify(findChild(panel,"importSolvePhoto").visible)
        verify(!findChild(panel,"captureAndSolvePhoto").visible)
        verify(findChild(panel,"solveCurrentPhoto")===null)
    }
}
