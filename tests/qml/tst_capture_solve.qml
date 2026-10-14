import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/pages"

TestCase {
    name: "CaptureSolveEntry"
    when: windowShown
    Component {
        id: hostComponent
        Window {
            width: 1050; height: 760; visible: true
            property alias page: page
            CaptureWorkspace { id: page; anchors.fill: parent }
        }
    }
    Component {
        id: managerComponent
        QtObject {
            signal devicesChanged()
            property int previewCounter: 0
            function device(role) { return {connected:true} }
        }
    }
    Component {
        id: solverComponent
        QtObject {
            property bool busy: false
            property string status: "未解析"
            property string executable: "D:/AStap/astap/astap_cli.exe"
            property var result: ({})
            property int captures: 0
            function captureAndSolve(seconds,fov) { captures++; busy=true }
            function cancel() { busy=false }
        }
    }
    Component { id: spyComponent; SignalSpy {} }
    function test_mainEntryCapturesAndRequestsSkyOnlyExplicitly() {
        const host=createTemporaryObject(hostComponent,this)
        const solver=createTemporaryObject(solverComponent,this)
        host.page.deviceManager=createTemporaryObject(managerComponent,this)
        host.page.plateSolver=solver
        const spy=createTemporaryObject(spyComponent,this,{target:host.page,signalName:"inspectPhotoRequested"})
        findChild(host.page,"openPhotoSolvePanel").clicked()
        const popup=findChild(host.page,"captureSolvePopup")
        tryCompare(popup,"opened",true)
        const start=findChild(host.page,"captureAndSolvePhoto")
        verify(start.enabled); start.clicked()
        compare(solver.captures,1); compare(spy.count,0)
        solver.busy=false
        solver.status="解析成功 · J2000"
        solver.result={raHours:20.6975,decDeg:45.323,widthDeg:7.358,heightDeg:4.911,
            positionAngle:83.75,source:"主相机 RAW",solvedAt:"2026-10-13",corners:[[1,1],[2,1],[2,2],[1,2]]}
        compare(spy.count,0)
        verify(waitForRendering(host.page))
        findChild(host.page,"inspectSolvedPhoto").clicked()
        compare(spy.count,1)
        tryCompare(popup,"opened",false)
    }
}
