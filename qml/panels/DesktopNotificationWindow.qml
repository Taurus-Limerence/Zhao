import QtQuick
import QtQuick.Window
import "../controls"
import "../theme"

// NINA 式主屏右下角通知堆叠（NotificationHostWindow.xaml.cs 的对等实现，
// NINA 默认 PrimaryScreenWorkAreaProvider——锚定主屏工作区，不跟随主窗口）：
// 独立置顶无边框窗口，不进任务栏。落屏显式取 AppState.notificationScreen
// （C++ 暴露的 QGuiApplication::primaryScreen）——多屏下隐式默认落屏不可靠
// （真机实测：删掉 screen 指定后窗口完全不显示）。transientParent 必须显式
// 置空——Qt 会对在主窗口对象树里创建的窗口自动指派 transientParent，
// transient 子窗口被居中/相对定位并产生无效几何（真机日志已证实）。
Window {
    id: root
    property var appState
    property int maxCards: 4
    property int dismissInterval: 5000
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"
    visible: cards.count > 0
    transientParent: null
    screen: appState && appState.notificationScreen ? appState.notificationScreen : null
    width: 340
    height: Math.max(column.implicitHeight, 76)

    // 坐标直接取 AppState.notificationArea（C++ 提供的主屏工作区）：
    // Screen 附加属性的 desktopAvailableWidth 是整个虚拟桌面宽度（多屏拼接，
    // 实测 3840），QScreen.availableGeometry 从 QML 访问返回 undefined——
    // 两者都不能用，主屏工作区只能由 C++ 暴露。全局坐标落在主屏范围内，
    // 操作系统自然把窗口显示在主屏。
    readonly property rect area: appState && appState.notificationArea
                                  ? appState.notificationArea : Qt.rect(0, 0, 0, 0)
    x: area.width > 0 ? area.x + area.width - width - 15 : 0
    y: area.height > 0 ? area.y + area.height - height - 10 : 0

    // 堆叠模型暴露给测试/调试；卡片经 appState 通道进入。
    readonly property alias cardsModel: cards
    ListModel { id: cards }

    Column {
        id: column
        width: parent.width
        spacing: 8
        // 两张及以上才显示"全部关闭"——单卡用卡片自身的 ✕ 即可。按钮带实底
        // 药丸背景：窗口本身透明，无底色的灰字浮在桌面上无法辨认。
        Item {
            width: parent.width
            height: visible ? 30 : 0
            visible: cards.count >= 2
            Rectangle {
                anchors.right: parent.right
                width: closeAllButton.width + 20
                height: 30
                radius: AsterTheme.radiusMedium
                color: AsterTheme.surfaceRaised
                border.width: 1
                border.color: AsterTheme.strokeStrong
                DesktopButton {
                    id: closeAllButton
                    objectName: "notificationCloseAllButton"
                    anchors.centerIn: parent
                    text: "全部关闭"
                    palette.buttonText: AsterTheme.textPrimary
                    onClicked: root.clearAll()
                }
            }
        }
        // 新卡片追加在末尾（列表底部、贴近屏幕角），旧卡片被推向上方
        Repeater {
            model: cards
            // title/message/severity 是卡片组件自身的 required 属性，会自动
            // 从 ListModel 角色注入；index 需在实例处声明用于移除。
            delegate: NotificationToast {
                id: card
                required property int index
                // 编译后的 delegate 信号处理器解析不到外层 id（真机 root 报
                // ReferenceError），但绑定上下文可以——经属性捕获后使用，
                // 与弹窗 onActivated 的修法一致。
                readonly property var stackModel: cards
                readonly property var dismissSettled: root.dismissIfSettled
                width: root.width
                dismissInterval: root.dismissInterval
                onClosed: {
                    stackModel.remove(index)
                    dismissSettled()
                }
            }
        }
    }

    function clearAll() {
        cards.clear()
        dismissIfSettled()
    }
    function dismissIfSettled() {
        if (cards.count === 0 && appState && typeof appState.dismissNotification === "function"
            && appState.notificationVisible)
            appState.dismissNotification()
    }

    Connections {
        target: root.appState
        ignoreUnknownSignals: true
        // dismissNotification 也发 notificationChanged：仅在可见（即真正的
        // showNotification 调用）时采集，避免把清除动作当成新通知。
        function onNotificationChanged() {
            if (!root.appState || !root.appState.notificationVisible) return
            for (let i = 0; i < cards.count; ++i) {
                const card = cards.get(i)
                if (card.message === root.appState.notificationMessage
                    && card.severity === root.appState.notificationSeverity) return
            }
            cards.append({title: root.appState.notificationTitle,
                          message: root.appState.notificationMessage,
                          severity: root.appState.notificationSeverity})
            while (cards.count > root.maxCards) cards.remove(0)
        }
    }
}
