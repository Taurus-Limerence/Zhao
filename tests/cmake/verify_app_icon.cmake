if(NOT DEFINED ROOT_PATH)
    message(FATAL_ERROR "ROOT_PATH is required")
endif()

set(svg_path "${ROOT_PATH}/qml/assets/icons/stellamora-app.svg")
set(ico_path "${ROOT_PATH}/resources/stellamora.ico")
set(rc_path "${ROOT_PATH}/resources/StellaMora.rc")

foreach(required_file IN ITEMS "${svg_path}" "${ico_path}" "${rc_path}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Application icon file is missing: ${required_file}")
    endif()
endforeach()

file(SIZE "${ico_path}" ico_size)
if(ico_size LESS 1024)
    message(FATAL_ERROR "Windows icon is unexpectedly small: ${ico_size} bytes")
endif()

file(READ "${ROOT_PATH}/CMakeLists.txt" cmake_contents)
file(READ "${ROOT_PATH}/src/main.cpp" main_contents)
file(READ "${rc_path}" rc_contents)

foreach(required_text IN ITEMS
        "qml/assets/icons/stellamora-app.svg"
        "resources/StellaMora.rc")
    string(FIND "${cmake_contents}" "${required_text}" match_index)
    if(match_index EQUAL -1)
        message(FATAL_ERROR "CMake icon wiring is missing: ${required_text}")
    endif()
endforeach()

string(FIND "${main_contents}" "QGuiApplication::setWindowIcon" match_index)
if(match_index EQUAL -1)
    message(FATAL_ERROR "Runtime window icon wiring is missing")
endif()

string(FIND "${rc_contents}" "stellamora.ico" match_index)
if(match_index EQUAL -1)
    message(FATAL_ERROR "Windows resource does not reference stellamora.ico")
endif()

