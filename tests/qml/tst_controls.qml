import QtQuick
import QtTest
import "../../qml/controls"

TestCase {
    name: "AsterControls"
    when: windowShown
    width: 500
    height: 120
    visible: true

    function luminance(color) {
        function linear(value) { return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4) }
        return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b)
    }

    function test_primaryButtonTextRemainsReadable() {
        const button = createTemporaryObject(primaryComponent, this)
        const foreground = luminance(button.palette.buttonText)
        const background = luminance(button.background.color)
        verify((Math.max(foreground, background) + 0.05) / (Math.min(foreground, background) + 0.05) >= 4.5,
               "Primary button text must remain legible on its accent fill")
        button.down = true
        const pressed = luminance(button.background.color)
        verify((Math.max(foreground, pressed) + 0.05) / (Math.min(foreground, pressed) + 0.05) >= 4.5,
               "Pressed primary buttons must retain readable text")
    }

    Component { id: primaryComponent; DesktopButton { primary: true; text: "连接已选设备" } }

    function test_groupAndSwitchInstantiate() {
        const group = createTemporaryObject(groupComponent, this)
        verify(group !== null)
        const toggle = createTemporaryObject(switchComponent, this)
        verify(toggle !== null)
        compare(toggle.checked, false)
    }

    function test_iconButtonContentUsesWholeCell() {
        const button = createTemporaryObject(iconButtonComponent, this)
        verify(button !== null)
        compare(button.contentItem.width, button.availableWidth)
        compare(button.contentItem.height, button.availableHeight)
    }

    function test_iconButtonHidesVisualLabelWithoutClearingText() {
        const button = createTemporaryObject(implicitIconButtonComponent, this)
        verify(button !== null)
        compare(button.label, "主相机")
        compare(button.implicitHeight, 54)

        button.showLabel = false

        compare(button.label, "主相机")
        compare(button.implicitHeight, 40)
    }

    Component {
        id: groupComponent
        AsterGroup { width: 320; height: 120 }
    }

    function test_compactActionFitsItsRow() {
        const row = createTemporaryObject(compactRowComponent, this)
        verify(waitForRendering(row))
        const editor = findChild(row, "settingRowEditor")
        verify(editor.height <= row.height, "Compact buttons must not overlap adjacent rows")
    }
    Component {
        id: compactRowComponent
        AsterSettingRow { width: 460; compact: true; rowData: ({label: "移动", type: "button", value: "移动到目标"}) }
    }

    Component {
        id: switchComponent
        AsterSwitch { }
    }


    Component {
        id: toggleRowComponent
        AsterSettingRow { width: 460; height: 42 }
    }

    Component {
        id: iconButtonComponent
        AsterIconButton { width: 100; height: 68; label: "主相机" }
    }

    Component {
        id: implicitIconButtonComponent
        AsterIconButton { label: "主相机" }
    }
}
