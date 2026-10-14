cmake_minimum_required(VERSION 3.24)

project(StellaMora VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Test Concurrent)

qt_standard_project_setup(REQUIRES 6.9)

enable_testing()
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_GENERATOR STREQUAL "Ninja")
    include(cmake/MsvcUtf8Dependencies.cmake)
    add_test(NAME msvc_header_dependencies COMMAND ${CMAKE_COMMAND}
        -DNINJA=${CMAKE_MAKE_PROGRAM} -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/verify_msvc_dependencies.cmake)
endif()
include(cmake/Toupcam.cmake)

qt_add_executable(tst_focusmetrics tests/cpp/tst_focusmetrics.cpp src/focus/FocusMetrics.cpp)
target_include_directories(tst_focusmetrics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_focusmetrics PRIVATE Qt6::Core Qt6::Test)
if(MSVC)
    target_compile_options(tst_focusmetrics PRIVATE /utf-8)
endif()
add_test(NAME focusmetrics COMMAND tst_focusmetrics)
set_tests_properties(focusmetrics PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_autofocus tests/cpp/tst_autofocus.cpp)
target_link_libraries(tst_autofocus PRIVATE stellamora_camera Qt6::Test)
if(MSVC)
    target_compile_options(tst_autofocus PRIVATE /utf-8)
endif()
add_test(NAME autofocus COMMAND tst_autofocus)
set_tests_properties(autofocus PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_platesolver tests/cpp/tst_platesolver.cpp src/imaging/FitsWriter.cpp src/astro/PlateSolver.cpp src/astro/PlateSolver.h)
target_include_directories(tst_platesolver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_platesolver PRIVATE Qt6::Gui Qt6::Test Qt6::Concurrent Qt6::Qml)
target_compile_definitions(tst_platesolver PRIVATE STELLAMORA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(tst_platesolver PRIVATE /utf-8)
endif()
add_test(NAME platesolver COMMAND tst_platesolver)
qt_add_executable(tst_centering tests/cpp/tst_centering.cpp)
target_link_libraries(tst_centering PRIVATE stellamora_camera Qt6::Test Qt6::Concurrent)
if(MSVC)
    target_compile_options(tst_centering PRIVATE /utf-8)
endif()
add_test(NAME centering COMMAND tst_centering)
set_tests_properties(platesolver PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_appstate
    tests/cpp/tst_appstate.cpp
    src/app/AppState.cpp
    src/app/AppState.h
)

target_include_directories(tst_appstate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_appstate PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME appstate COMMAND tst_appstate)

qt_add_executable(tst_devicemanager
    tests/cpp/tst_devicemanager.cpp
)

target_include_directories(tst_devicemanager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_devicemanager PRIVATE stellamora_camera Qt6::Test)

add_test(NAME devicemanager COMMAND tst_devicemanager)
qt_add_executable(tst_sequence tests/cpp/tst_sequence.cpp)
target_link_libraries(tst_sequence PRIVATE stellamora_camera Qt6::Test)
if(MSVC)
    target_compile_options(tst_sequence PRIVATE /utf-8)
endif()
add_test(NAME sequence COMMAND tst_sequence)

qt_add_executable(tst_sequence_autofocus tests/cpp/tst_sequence_autofocus.cpp)
target_link_libraries(tst_sequence_autofocus PRIVATE stellamora_camera Qt6::Test)
if(MSVC)
    target_compile_options(tst_sequence_autofocus PRIVATE /utf-8)
endif()
add_test(NAME sequence_autofocus COMMAND tst_sequence_autofocus)
set_tests_properties(sequence_autofocus PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(tst_mount tests/cpp/tst_mount.cpp)
target_link_libraries(tst_mount PRIVATE stellamora_camera Qt6::Test)
add_test(NAME mount COMMAND tst_mount)

qt_add_executable(tst_filterwheel
    tests/cpp/tst_filterwheel.cpp
)

target_include_directories(tst_filterwheel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/toupcam/include)
target_link_libraries(tst_filterwheel PRIVATE stellamora_camera Qt6::Test)

add_test(NAME filterwheel COMMAND tst_filterwheel)
qt_add_executable(tst_rawcapture tests/cpp/tst_rawcapture.cpp)
target_include_directories(tst_rawcapture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/toupcam/include)
target_link_libraries(tst_rawcapture PRIVATE stellamora_camera Qt6::Test)
add_test(NAME rawcapture COMMAND tst_rawcapture)

qt_add_executable(tst_previewhistogram tests/cpp/tst_previewhistogram.cpp)
target_include_directories(tst_previewhistogram PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_previewhistogram PRIVATE Qt6::Gui Qt6::Test)
add_test(NAME previewhistogram COMMAND tst_previewhistogram)

qt_add_executable(tst_previewframe tests/cpp/tst_previewframe.cpp src/imaging/FitsWriter.cpp src/imaging/StillSaveQueue.cpp)
target_include_directories(tst_previewframe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_previewframe PRIVATE Qt6::Gui Qt6::Test)
add_test(NAME previewframe COMMAND tst_previewframe)

qt_add_executable(tst_astro tests/cpp/tst_astro.cpp src/astro/AstroPlanner.h)

qt_add_executable(ascom_smoke tools/ascom_smoke.cpp)
target_include_directories(ascom_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ascom_smoke PRIVATE stellamora_camera Qt6::Core)

qt_add_executable(focuser_smoke tools/focuser_smoke.cpp)
target_include_directories(focuser_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${TOUPCAM_ROOT}/include)
target_link_libraries(focuser_smoke PRIVATE stellamora_camera Qt6::Core)

qt_add_executable(tst_focuser tests/cpp/tst_focuser.cpp)
target_include_directories(tst_focuser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_focuser PRIVATE stellamora_camera Qt6::Test)
add_test(NAME focuser COMMAND tst_focuser)

qt_add_executable(guide_smoke tools/guide_smoke.cpp)
target_include_directories(guide_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${TOUPCAM_ROOT}/include)
target_link_libraries(guide_smoke PRIVATE stellamora_camera Qt6::Core)

qt_add_executable(tst_stardetection tests/cpp/tst_stardetection.cpp)
target_include_directories(tst_stardetection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_stardetection PRIVATE Qt6::Core Qt6::Test)
add_test(NAME stardetection COMMAND tst_stardetection)

qt_add_executable(tst_guideprocessor tests/cpp/tst_guideprocessor.cpp src/guide/GuideProcessor.h)
target_include_directories(tst_guideprocessor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_guideprocessor PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
add_test(NAME guideprocessor COMMAND tst_guideprocessor)

qt_add_executable(tst_guidealgorithm tests/cpp/tst_guidealgorithm.cpp src/guide/GuideProcessor.h)
target_include_directories(tst_guidealgorithm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_guidealgorithm PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
add_test(NAME guidealgorithm COMMAND tst_guidealgorithm)
target_include_directories(tst_astro PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_astro PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
add_test(NAME astro COMMAND tst_astro)

qt_add_executable(tst_cameraparams
    tests/cpp/tst_cameraparams.cpp
)

target_include_directories(tst_cameraparams PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_cameraparams PRIVATE Qt6::Core Qt6::Test)

add_test(NAME cameraparams COMMAND tst_cameraparams)

qt_add_executable(StellaMora
    src/main.cpp
    src/app/AppState.cpp
    src/app/AppState.h
    src/astro/AstroPlanner.h
    src/guide/GuideProcessor.h
)

if(NOT WIN32)
    set_target_properties(StellaMora PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
else()
    target_sources(StellaMora PRIVATE resources/StellaMora.rc)
endif()

set_source_files_properties(
    qml/theme/AsterTheme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

set(STELLAMORA_QML_FILES
    qml/controls/DesktopButton.qml
    qml/controls/DesktopField.qml
    qml/shell/DesktopWorkspace.qml
    qml/panels/GuideMonitorPanel.qml
    qml/panels/HistogramPanel.qml
    qml/panels/DeviceInspector.qml
    qml/Main.qml
    qml/theme/AsterTheme.qml
    qml/controls/AsterButton.qml
    qml/controls/AsterIconButton.qml
    qml/controls/AsterSwitch.qml
    qml/controls/AsterSegmentedControl.qml
    qml/controls/AsterGroup.qml
    qml/controls/AsterSettingRow.qml
    qml/controls/SettingsPage.qml
    qml/shell/TopBar.qml
    qml/shell/WindowCaptionButton.qml
    qml/shell/WindowControls.qml
    qml/shell/WindowResizeHandles.qml
    qml/shell/LeftAnalysisBar.qml
    qml/shell/RightCaptureBar.qml
    qml/shell/StatusBar.qml
    qml/pages/CaptureWorkspace.qml
    qml/pages/GuideWorkspace.qml
    qml/pages/SequenceWorkspace.qml
    qml/pages/SkyMapWorkspace.qml
    qml/panels/DeviceConnectionsPopup.qml
    qml/panels/PhotoSolvePanel.qml
    qml/panels/AutoFocusPanel.qml
    qml/panels/CenteringControls.qml
    qml/controls/DesktopComboBox.qml
    qml/controls/DesktopMenu.qml
    qml/controls/DesktopMenuItem.qml
    qml/panels/DeviceSettingsDrawer.qml
    qml/panels/MountManualControl.qml
    qml/panels/NotificationToast.qml
    qml/panels/DesktopNotificationWindow.qml
    qml/settings/MountSettings.qml
    qml/settings/MainCameraSettings.qml
    qml/settings/GuideCameraSettings.qml
    qml/settings/FilterWheelSettings.qml
    qml/settings/FilterWheelChannelTable.qml
    qml/settings/FocuserSettings.qml
)

# Survey imagery layer needs Qt WebEngine; kits without the component keep
# building (and the planner falls back to the pure-QML canvas).
find_package(Qt6 COMPONENTS WebEngineQuick QUIET)
if(TARGET Qt6::WebEngineQuick)
    message(STATUS "StellaMora Aladin layer: ENABLED")
    set(STELLAMORA_ALADIN ON)
    list(APPEND STELLAMORA_QML_FILES qml/pages/AladinSkyView.qml)
else()
    set(STELLAMORA_ALADIN OFF)
endif()

qt_add_qml_module(StellaMora
    URI StellaMora
    VERSION 1.0
    QML_FILES ${STELLAMORA_QML_FILES}
    RESOURCES
        qml/assets/images/preview.jpg
        qml/assets/aladin.html
        qml/assets/aladin-lite.mjs
        qml/assets/icons/aperture.svg
        qml/assets/icons/bell.svg
        qml/assets/icons/camera.svg
        qml/assets/icons/chart.svg
        qml/assets/icons/chevron-down.svg
        qml/assets/icons/chevron-left.svg
        qml/assets/icons/chevron-right.svg
        qml/assets/icons/chevron.svg
        qml/assets/icons/close.svg
        qml/assets/icons/crosshair-ui.svg
        qml/assets/icons/crosshair.svg
        qml/assets/icons/device.svg
        qml/assets/icons/film.svg
        qml/assets/icons/filter.svg
        qml/assets/icons/fisheye.svg
        qml/assets/icons/fit.svg
        qml/assets/icons/focus.svg
        qml/assets/icons/focuser.svg
        qml/assets/icons/grid.svg
        qml/assets/icons/guide-camera.svg
        qml/assets/icons/guide.svg
        qml/assets/icons/histogram.svg
        qml/assets/icons/image.svg
        qml/assets/icons/list.svg
        qml/assets/icons/map-ui.svg
        qml/assets/icons/map.svg
        qml/assets/icons/mount.svg
        qml/assets/icons/roi.svg
        qml/assets/icons/settings-ui.svg
        qml/assets/icons/star.svg
        qml/assets/icons/stellamora-app.svg
        qml/assets/icons/stellamora-mark.svg
        qml/assets/icons/stretch.svg
)

add_test(
    NAME qml_module_contract
    COMMAND ${CMAKE_COMMAND}
        -DQMLDIR_PATH=${CMAKE_CURRENT_BINARY_DIR}/StellaMora/qmldir
        -DMAIN_QML_PATH=${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/verify_qml_module.cmake
)

add_test(
    NAME custom_titlebar_contract
    COMMAND ${CMAKE_COMMAND}
        -DMAIN_QML_PATH=${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml
        -DTOP_BAR_PATH=${CMAKE_CURRENT_SOURCE_DIR}/qml/shell/TopBar.qml
        -DTHEME_PATH=${CMAKE_CURRENT_SOURCE_DIR}/qml/theme/AsterTheme.qml
        -DCMAKE_PATH=${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/verify_custom_titlebar.cmake
)

add_test(
    NAME app_icon_contract
    COMMAND ${CMAKE_COMMAND}
        -DROOT_PATH=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/verify_app_icon.cmake
)

add_test(
    NAME theme_palette_contract
    COMMAND ${CMAKE_COMMAND}
        -DTHEME_PATH=${CMAKE_CURRENT_SOURCE_DIR}/qml/theme/AsterTheme.qml
        -DBUTTON_PATH=${CMAKE_CURRENT_SOURCE_DIR}/qml/controls/AsterButton.qml
        -DICON_BUTTON_PATH=${CMAKE_CURRENT_SOURCE_DIR}/qml/controls/AsterIconButton.qml
        -DSEGMENTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/qml/controls/AsterSegmentedControl.qml
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/verify_theme_palette.cmake
)

add_test(
    NAME project_identity_contract
    COMMAND ${CMAKE_COMMAND}
        -DROOT_PATH=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/verify_project_identity.cmake
)

add_test(
    NAME qml_components
    COMMAND $<TARGET_FILE:Qt6::qmltestrunner>
        -input ${CMAKE_CURRENT_SOURCE_DIR}/tests/qml
)
set_tests_properties(qml_components PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
    TIMEOUT 60
)

target_include_directories(StellaMora PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# C++ tests resolve Qt DLLs from the build tree; prepend the Qt bin dir so
# they run without a developer PATH containing the Qt kit.
foreach(test_target IN ITEMS appstate devicemanager filterwheel sequence centering
        previewhistogram previewframe astro cameraparams
        mount focuser stardetection guideprocessor guidealgorithm)
    set_tests_properties(${test_target} PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endforeach()

add_test(NAME render_sequence COMMAND StellaMora --sequence
    --screenshot ${CMAKE_BINARY_DIR}/artifacts/sequence.png --size 1280x720)
set_tests_properties(render_sequence PROPERTIES
    ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 10)

if(STELLAMORA_ALADIN)
    target_link_libraries(StellaMora PRIVATE Qt6::WebEngineQuick)
    target_compile_definitions(StellaMora PRIVATE STELLAMORA_ALADIN)
endif()

target_link_libraries(StellaMora PRIVATE
    Qt6::Concurrent
    stellamora_camera
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

add_custom_command(TARGET StellaMora POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TOUPCAM_RUNTIME}" "$<TARGET_FILE_DIR:StellaMora>"
    VERBATIM
)
install(TARGETS StellaMora RUNTIME DESTINATION bin)
install(FILES "${TOUPCAM_RUNTIME}" DESTINATION bin)
if(MSVC)
    target_compile_options(tst_devicemanager PRIVATE /utf-8)
    target_compile_options(tst_filterwheel PRIVATE /utf-8)
endif()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/artifacts)
if(WIN32)
    set(STELLAMORA_HEADLESS_PLATFORM windows)
else()
    set(STELLAMORA_HEADLESS_PLATFORM offscreen)
endif()

foreach(render_size IN ITEMS 1280x720 1440x900 1920x1080)
    add_test(
        NAME render_${render_size}
        COMMAND StellaMora
            --screenshot ${CMAKE_BINARY_DIR}/artifacts/${render_size}.png
            --size ${render_size}
    )
    set_tests_properties(render_${render_size} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=${STELLAMORA_HEADLESS_PLATFORM};QT_QUICK_BACKEND=software"
        TIMEOUT 8
    )
endforeach()
