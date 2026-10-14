import QtQuick
import QtTest
import "../../qml/panels"
TestCase {
    name: "CenteringControls"
    when: windowShown
    width: 600; height: 650
    visible: true
    Component { id: controls; CenteringControls { width: 300 } }
    Component { id: spyComponent; SignalSpy {} }
    Component {
        id: service
        QtObject {
            property bool busy: false
            property bool succeeded: false
            property string status: ""
            property string targetName: ""
            property real separationArcmin: NaN
            property int corrections: 0
            property var requests: []
            property int stops: 0
            function start(ra,dec,name,seconds,fov,tolerance,limit) { requests.push([ra,dec,name,seconds,fov,tolerance,limit]); busy=true }
            function cancel() { stops++; busy=false }
        }
    }
    function test_confirmationSnapshotsTargetAndStop() {
        const controller=createTemporaryObject(service,this)
        const panel=createTemporaryObject(controls,this,{controller:controller,ready:true,targetName:"M31",targetValid:true,targetRaHours:0.7,targetDecDeg:41,fovHeight:1.5})
        findChild(panel,"startCentering").clicked()
        compare(controller.requests.length,0)
        const confirm=findChild(panel,"confirmCentering")
        tryCompare(confirm,"opened",true)
        panel.targetName="Other"; panel.targetRaHours=12
        findChild(panel,"acceptCentering").clicked()
        compare(controller.requests.length,1)
        compare(controller.requests[0][0],0.7)
        compare(controller.requests[0][2],"M31")
        compare(controller.requests[0][3],2)
        compare(controller.requests[0][5],1)
        compare(controller.requests[0][6],3)
        verify(!findChild(panel,"startCentering").enabled)
        findChild(panel,"stopCentering").clicked()
        compare(controller.stops,1)
        panel.targetValid=false
        verify(!findChild(panel,"startCentering").enabled)
    }
    function test_onlySlewSelectsModeWithoutExecuting() {
        const controller=createTemporaryObject(service,this)
        const panel=createTemporaryObject(controls,this,{controller:controller,ready:false,onlySlewReady:true,showOnlySlew:true,targetValid:true,targetName:"M31"})
        const spy=createTemporaryObject(spyComponent,this,{target:panel,signalName:"onlySlewRequested"})
        const action=findChild(panel,"onlySlewMode")
        verify(action!==null)
        action.menu.open()
        tryCompare(action.menu,"opened",true)
        verify(action.menu.width>=160)
        action.menu.close()
        action.triggered()
        compare(spy.count,0)
        const button=findChild(panel,"startCentering")
        compare(button.text,"仅指向")
        verify(button.enabled)
        button.clicked()
        compare(spy.count,1)
        compare(controller.requests.length,0)
        findChild(panel,"centerMode").triggered()
        compare(button.text,"指向并居中")
        verify(!button.enabled)
    }
}
