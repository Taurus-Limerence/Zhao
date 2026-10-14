file(READ "${MAIN_QML_PATH}" main_qml)
file(READ "${TOP_BAR_PATH}" top_bar_qml)
file(READ "${THEME_PATH}" theme_qml)
file(READ "${CMAKE_PATH}" cmake_text)

foreach(required_token IN ITEMS
        "flags: Qt.Window | Qt.FramelessWindowHint"
        "onMoveRequested: root.startSystemMove()"
        "onMinimizeRequested: root.showMinimized()"
        "root.visibility === Window.Maximized"
        "root.showNormal()"
        "root.showMaximized()"
        "onCloseRequested: root.close()"
        "WindowResizeHandles"
        "hostWindow: root")
    string(FIND "${main_qml}" "${required_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR "Main.qml is missing custom-title-bar token: ${required_token}")
    endif()
endforeach()

foreach(forbidden_token IN ITEMS
        "Qt.ExpandedClientAreaHint"
        "Qt.NoTitleBarBackgroundHint"
        "titleBarLeftInset"
        "titleBarRightInset"
        "? 118 : 0"
        "? 150 : 0")
    string(FIND "${main_qml}${top_bar_qml}" "${forbidden_token}" token_position)
    if(NOT token_position EQUAL -1)
        message(FATAL_ERROR "Custom title bar still contains native-caption token: ${forbidden_token}")
    endif()
endforeach()

foreach(required_token IN ITEMS
        "objectName: \"brandMark\""
        "objectName: \"brandText\""
        "source: \"../assets/icons/stellamora-mark.svg\""
        "WindowControls"
        "signal moveRequested()"
        "signal minimizeRequested()"
        "signal toggleMaximizeRequested()"
        "signal closeRequested()")
    string(FIND "${top_bar_qml}" "${required_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR "TopBar.qml is missing custom-caption token: ${required_token}")
    endif()
endforeach()

foreach(required_token IN ITEMS
        "readonly property color dangerSurface"
        "readonly property color dangerPressed")
    string(FIND "${theme_qml}" "${required_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR "AsterTheme.qml is missing caption token: ${required_token}")
    endif()
endforeach()

foreach(required_token IN ITEMS
        "qml/shell/WindowCaptionButton.qml"
        "qml/shell/WindowControls.qml"
        "qml/shell/WindowResizeHandles.qml"
        "qml/assets/icons/stellamora-mark.svg")
    string(FIND "${cmake_text}" "${required_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR "CMakeLists.txt is missing custom-title-bar source: ${required_token}")
    endif()
endforeach()
