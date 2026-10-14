execute_process(
    COMMAND "${NINJA}" -C "${BUILD_DIR}" -t deps CMakeFiles/StellaMora.dir/src/main.cpp.obj
    RESULT_VARIABLE result OUTPUT_VARIABLE dependencies ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect MSVC header dependencies: ${error}")
endif()
string(REPLACE "\\" "/" dependencies "${dependencies}")
foreach(header IN ITEMS src/devices/DeviceManager.h src/sequence/SequenceController.h)
    string(FIND "${dependencies}" "${header}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "main.cpp is missing the ${header} dependency; a header change can leave stale objects.\n${dependencies}")
    endif()
endforeach()
message(STATUS "main.cpp tracks DeviceManager and SequenceController headers")
