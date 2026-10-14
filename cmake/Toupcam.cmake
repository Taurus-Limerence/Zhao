# Runtime loading permits the application to explain missing SDK/dependency errors.
# Only the checked-in SDK copy is used; no developer-machine SDK paths.
set(TOUPCAM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/toupcam")
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|X86_64)$")
    message(FATAL_ERROR "The bundled Toupcam SDK supports x64 only; select an x64 toolchain.")
endif()
if(WIN32)
    set(TOUPCAM_RUNTIME "${TOUPCAM_ROOT}/windows/x64/toupcam.dll")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(TOUPCAM_RUNTIME "${TOUPCAM_ROOT}/linux/x64/libtoupcam.so")
else()
    message(FATAL_ERROR "Toupcam backend currently supports Windows and Linux only.")
endif()
if(NOT EXISTS "${TOUPCAM_RUNTIME}" OR NOT EXISTS "${TOUPCAM_ROOT}/include/toupcam.h")
    message(FATAL_ERROR "Project-local Toupcam SDK files are missing: ${TOUPCAM_ROOT}")
endif()
add_library(stellamora_camera STATIC
    src/focus/AutoFocusController.cpp
    src/focus/AutoFocusController.h
    src/focus/FocusMetrics.cpp
    src/focus/FocusMetrics.h
    src/astro/CenteringController.cpp
    src/astro/CenteringController.h
    src/astro/PlateSolver.cpp
    src/astro/PlateSolver.h
    src/sequence/SequenceController.h
    src/sequence/SequenceController.cpp
    src/devices/CameraBackend.h
    src/imaging/FitsWriter.h
    src/imaging/FitsWriter.cpp
    src/imaging/StillSaveQueue.h
    src/imaging/StillSaveQueue.cpp
    src/devices/UsbIdentity.h
    src/devices/UsbIdentity.cpp
    src/devices/ToupcamCamera.h
    src/devices/ToupcamCamera.cpp
    src/devices/FilterWheelBackend.h
    src/devices/ToupcamFilterWheel.h
    src/devices/ToupcamFilterWheel.cpp
    src/devices/FocuserBackend.h
    src/devices/ToupcamFocuser.h
    src/devices/ToupcamFocuser.cpp
    src/devices/AscomDispatch.h
    src/devices/AscomDispatch.cpp
    src/devices/MountBackend.h
    src/devices/AscomMount.h
    src/devices/AscomMount.cpp
    src/devices/DeviceManager.h
    src/devices/DeviceManager.cpp
)
target_include_directories(stellamora_camera PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" PRIVATE "${TOUPCAM_ROOT}/include")
target_compile_definitions(stellamora_camera PRIVATE TOUPCAM_NOIMPORTS)
target_link_libraries(stellamora_camera PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent)
if(WIN32)
    # ASCOM COM interop needs the OLE libraries; driver version resources need version.dll.
    target_link_libraries(stellamora_camera PUBLIC ole32 oleaut32 version)
endif()
if(MSVC)
    target_compile_options(stellamora_camera PRIVATE /utf-8)
endif()
