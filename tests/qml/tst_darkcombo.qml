import QtQuick
import QtTest
import "../../qml/controls"
import "../../qml/theme"
TestCase {
    name: "DarkCombo"
    when: windowShown
    width: 400; height: 350
    Component { id: comboComponent; DesktopComboBox { width: 180; model: ["One", "Two", "Three"] } }
    function test_popupAndKeyboardSelection() {
        const combo=createTemporaryObject(comboComponent,this)
        combo.popup.open()
        tryCompare(combo.popup,"opened",true)
        compare(combo.popup.background.color,AsterTheme.canvasRaised)
        combo.forceActiveFocus()
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Return)
        tryCompare(combo.popup,"visible",false)
        compare(combo.currentIndex,1)
        compare(combo.displayText,"Two")
    }
}
