if(NOT DEFINED ROOT_PATH)
    message(FATAL_ERROR "ROOT_PATH is required")
endif()

set(required_pairs
    "CMakeLists.txt@@project(StellaMora"
    "CMakeLists.txt@@qt_add_executable(StellaMora"
    "CMakeLists.txt@@qt_add_qml_module(StellaMora"
    "CMakeLists.txt@@URI StellaMora"
    "src/main.cpp@@StellaMora.State"
    "src/main.cpp@@engine.loadFromModule(\"StellaMora\", \"Main\")"
    "qml/Main.qml@@import StellaMora.State 1.0"
    "qml/Main.qml@@title: \"StellaMora\""
    "src/main.cpp@@QGuiApplication::setApplicationName(QStringLiteral(\"StellaMora\"))"
    "qml/controls/qmldir@@module StellaMora.Controls"
    "qml/theme/qmldir@@module StellaMora.Theme"
)

foreach(pair IN LISTS required_pairs)
    string(REPLACE "@@" ";" fields "${pair}")
    list(GET fields 0 relative_path)
    list(GET fields 1 required_text)
    file(READ "${ROOT_PATH}/${relative_path}" contents)
    string(FIND "${contents}" "${required_text}" match_index)
    if(match_index EQUAL -1)
        message(FATAL_ERROR "Missing StellaMora identity in ${relative_path}: ${required_text}")
    endif()
endforeach()
