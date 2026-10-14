import QtQuick
import QtTest
import "../../qml/panels"

TestCase {
    name: "MountControl"
    when: windowShown
    width: 320; height: 360
    visible: true
    Component { id: controlComponent; MountManualControl { width: 290 } }
    Component {
        id: managerComponent
        QtObject {
            property var calls: []
            property bool mountManualMoving: false
            property bool mountGuiding: false
            property string lastError: ""
            function startMountManualMove(axis, rate) { calls = calls.concat([[axis, rate]]); mountManualMoving = true; return true }
            function keepMountManualMoveAlive() {}
            function stopMountManualMove() { calls = calls.concat(["stop"]); mountManualMoving = false; return true }
            function mountAbortSlew() { return stopMountManualMove() }
            function setMountTracking(on) { return true }
        }
    }
    function test_holdReleaseAndHideStop() {
        const manager = createTemporaryObject(managerComponent, this)
        const control = createTemporaryObject(controlComponent, this, {deviceManager: manager,
            connected: true, metrics: {canManualMove: true, slewing: false, atPark: false}})
        const north = findChild(control, "mountMoveNorth")
        const rate = findChild(control, "mountManualRate")
        compare(rate.rates[rate.currentIndex], 720)
        compare(rate.rates[rate.rates.length - 1], 1440)
        mousePress(rate, 31, 156)
        compare(rate.rates[rate.currentIndex], 0.25)
        mouseMove(rate, 6, 88)
        compare(rate.rates[rate.currentIndex], 8)
        mouseRelease(rate, 6, 88)
        mouseClick(rate, 31, 20)
        compare(rate.rates[rate.currentIndex], 1440)
        compare(manager.calls.length, 0)
        mousePress(north)
        compare(manager.calls.length, 1)
        compare(manager.calls[0][0], 1)
        compare(manager.calls[0][1], 1440)
        verify(!rate.enabled)
        mouseRelease(north)
        compare(manager.calls[1], "stop")
        mousePress(north)
        control.visible = false
        compare(manager.calls[3], "stop")
        mouseRelease(north)
        control.visible = true
        manager.mountGuiding = true
        verify(!north.enabled)
        verify(findChild(control, "mountManualStop").enabled)
        manager.mountGuiding = false
        control.metrics = {canManualMove: true, slewing: true, atPark: false}
        verify(!north.enabled)
        control.metrics = {canManualMove: true, slewing: false, atPark: true}
        verify(!north.enabled)
    }
}
