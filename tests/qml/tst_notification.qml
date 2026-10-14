import QtQuick
import QtTest
import "../../qml/panels"

TestCase {
    name: "DesktopNotifications"
    when: windowShown

    Component {
        id: stateComponent
        QtObject {
            property bool notificationVisible: false
            property int notificationSeverity: 0
            property string notificationTitle: ""
            property string notificationMessage: ""
            signal notificationChanged()
            function dismissNotification() {
                notificationVisible = false
                notificationChanged()
            }
            function show(title, message, severity) {
                notificationTitle = title
                notificationMessage = message
                notificationSeverity = severity
                notificationVisible = true
                notificationChanged()
            }
        }
    }

    Component {
        id: windowComponent
        DesktopNotificationWindow { }
    }

    function test_stacksAndAutoDismisses() {
        const state = createTemporaryObject(stateComponent, this)
        const window = createTemporaryObject(windowComponent, this,
                                             { appState: state, dismissInterval: 150 })
        compare(window.cardsModel.count, 0)
        // 连续两条通知：堆叠共存而不是互相顶替
        state.show("设备已连接", "主相机 · G3M678C", 0)
        state.show("设备已断开", "电调焦 · AAF Pro", 1)
        tryCompare(window.cardsModel, "count", 2)
        // 每张卡片独立计时，150ms 后全部自动消失
        tryCompare(window.cardsModel, "count", 0)
    }

    function test_capsStackSize() {
        const state = createTemporaryObject(stateComponent, this)
        const window = createTemporaryObject(windowComponent, this,
                                             { appState: state, dismissInterval: 60000 })
        for (let i = 0; i < 6; ++i)
            state.show("标题" + i, "内容" + i, 0)
        tryCompare(window.cardsModel, "count", window.maxCards)
    }

    function test_closeAllButtonClearsStack() {
        const state = createTemporaryObject(stateComponent, this)
        const window = createTemporaryObject(windowComponent, this,
                                             { appState: state, dismissInterval: 60000 })
        const button = findChild(window, "notificationCloseAllButton")
        verify(button !== null)
        for (let i = 0; i < 3; ++i)
            state.show("标题" + i, "内容" + i, 0)
        tryCompare(window.cardsModel, "count", 3)
        // 两张及以上按钮可见，点击全部清空
        tryCompare(button, "visible", true)
        button.clicked()
        tryCompare(window.cardsModel, "count", 0)
        compare(state.notificationVisible, false)
        // 单卡时按钮隐藏，卡片自己的 ✕/计时关闭即可
        state.show("单张", "内容", 0)
        tryCompare(window.cardsModel, "count", 1)
        tryCompare(button, "visible", false)
    }
}
