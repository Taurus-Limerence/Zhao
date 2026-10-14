import QtQuick
import QtTest
import "../../qml/panels"
TestCase {
    name: "FocusInspector"
    when: windowShown
    width: 310; height: 850
    visible: true
    QtObject {
        id: manager
        property var calls: []
        property string lastError: ""
        property var devices: [{}, {}, {}, {}, {connected:true, selectedId:"test", selectedName:"AAF",
            metrics:{position:5000, maxStep:65000, temperature:33.1, reversed:true, moving:false}}]
        function moveFocuser(position) { calls = calls.concat([position]); return true }
        function haltFocuser() { calls = calls.concat(["stop"]); return true }
    }
    DeviceInspector {
        id: inspector
        anchors.fill: parent
        deviceManager: manager
    }
    function test_coarseFineAndReference() {
        const coarse = findChild(inspector, "focusCoarseOut")
        verify(coarse !== null)
        // Expand the section without connecting hardware.
        const header = findChild(inspector, "deviceSectionToggle4")
        verify(header !== null)
        mouseClick(header)
        wait(100)
        verify(coarse.visible)
        verify(coarse.enabled)
        grabImage(inspector).save("D:/VisualStudio/Projects/StellaMora/out/agent-work/focus-panel.png")
        mouseClick(coarse)
        compare(manager.calls[0], 5050)
        mouseClick(findChild(inspector, "focusFineIn"))
        compare(manager.calls[1], 4990)
        mouseClick(findChild(inspector, "focusSetReference"))
        compare(manager.calls.length, 2)
        compare(findChild(inspector, "deviceSection4").focusReference, 5000)
        tryCompare(findChild(inspector, "focusReturnReference"), "enabled", true)
        wait(50)
        mouseClick(findChild(inspector, "focusReturnReference"))
        compare(manager.calls[2], 5000)
        mouseClick(findChild(inspector, "focusHaltButton"))
        compare(manager.calls[3], "stop")
        manager.devices = [{}, {}, {}, {}, {connected:true, selectedId:"test", selectedName:"AAF",
            metrics:{position:64990, maxStep:65000, temperature:33.1, reversed:true, moving:false}}]
        mouseClick(coarse)
        compare(manager.calls[4], 65000)
        manager.devices = [{}, {}, {}, {}, {connected:false, selectedId:"test", metrics:{}}]
        verify(!findChild(inspector, "focusReturnReference").enabled)
    }
}
