# Qt compiles MSVC sources as UTF-8; CMake's cached localized prefix may use the ANSI code page.
set(_showincludes_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/StellaMoraShowIncludes")
file(MAKE_DIRECTORY "${_showincludes_dir}")
file(WRITE "${_showincludes_dir}/probe.h" "\n")
file(WRITE "${_showincludes_dir}/probe.cpp" "#include \"probe.h\"\n")
execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /utf-8 /EP probe.cpp
    WORKING_DIRECTORY "${_showincludes_dir}"
    RESULT_VARIABLE _showincludes_result
    OUTPUT_VARIABLE _showincludes_output ERROR_VARIABLE _showincludes_output
    ENCODING UTF-8
)
if(NOT _showincludes_result EQUAL 0 OR NOT _showincludes_output MATCHES
        "(^|\n)([^:\n][^:\n]+:[^:\n]*[^: \n][^: \n]:?[ \t]+)([A-Za-z]:\\\\|\\./|\\.\\\\|/)")
    message(FATAL_ERROR "Cannot detect the MSVC UTF-8 include prefix: ${_showincludes_output}")
endif()
set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}")
unset(_showincludes_dir)
unset(_showincludes_result)
unset(_showincludes_output)
