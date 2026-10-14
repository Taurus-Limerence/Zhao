import QtQuick
import QtTest
import "../../qml/settings"
import "../../qml/controls"

TestCase {
    name: "StillSettings"
    when: windowShown
    width: 800; height: 600
    Component {
        id: managerComponent
        QtObject {
            property var devices: []
            property var cameraTelemetry: ({})
            property bool autoSaveStills: true
            property string stillSaveDirectory: "D:/captures"
            property string stillImageType: "LIGHT"
        }
    }
    Component { id: pageComponent; MainCameraSettings { width: 700; height: 550 } }
    Component { id: rowComponent; AsterSettingRow { width: 400; height: 42 } }
    function row(page, label) {
        for (const section of page.sections)
            for (const item of section.rows)
                if (item.label === label) return item
        fail("Missing row: " + label)
    }
    function test_restoreDirectoryUsesWritableProperty() {
        const manager = createTemporaryObject(managerComponent, this)
        const page = createTemporaryObject(pageComponent, this, {deviceManager: manager})
        row(page, "恢复默认目录").onClicked()
        compare(manager.stillSaveDirectory, "")
    }
    function test_autoSaveUsesWritableProperty() {
        const manager = createTemporaryObject(managerComponent, this)
        const page = createTemporaryObject(pageComponent, this, {deviceManager: manager})
        row(page, "单帧自动保存").onToggled(false)
        compare(manager.autoSaveStills, false)
    }
    function test_selectionPassesExplicitIndex() {
        let selected = -1
        const control = createTemporaryObject(rowComponent, this, {
            rowData: {type: "select", options: ["LIGHT", "DARK"], onActivated: function(index) { selected = index }}
        })
        const loader = findChild(control, "settingRowEditor")
        verify(loader && loader.item)
        failOnWarning(/.*Injection of parameters.*/)
        loader.item.activated(1)
        compare(selected, 1)
    }
}
