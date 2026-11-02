# cmake/UltraCanvasLibraryBuildId.cmake
# A framework DLL that programs are compiled against says which build it is,
# so a program can refuse one from another build before it crashes on it
# (OS/MSWindows/AppEntry/UltraCanvasWindowsLibraryCheck.cpp).
#
#   ultracanvas_library_build_id(<target> <name> <version> <public-include-dir> <c-function>)
#
# For a SHARED library <target>: the build id is "<version>+<12 hex>", the hex
# a hash of every header under <public-include-dir> - their names and contents,
# line endings normalised, so the same code checked out with CRLF or LF gives
# the same id. The headers are what a program compiled against: the size of
# every class, the order of every virtual function. A version that is not bumped
# for every change (VirtualFS stays 1.0.0, VideoFX's is bumped by hand) cannot
# tell two such builds apart; the hash does.
#
# The library gets a generated source defining
#     extern "C" const char* <c-function>()   // returns the build id
# and, on Windows, an entry in the global property ULTRACANVAS_LIBRARY_CHECKS
# that the core's CMakeLists turns into the check every program compiles.
# Call it after add_library and before the core is configured.
#
# ultracanvas_hash_headers(<out-var> <base-dir> [<subdir>...]) is that hash on
# its own - every header under <base-dir> (or under each <subdir> of it),
# named relative to <base-dir> - for a contract that is not a DLL's: the
# framework's element plug-ins carry the hash of the headers they were
# compiled against (UltraCanvasFrameworkHeadersId.h, UltraCanvas/CMakeLists.txt).
#
# The hash is taken when CMake configures. A header edited without a
# reconfigure leaves the id as it was - on both sides, the DLL and the
# programs, so nothing is refused that should not be; a packaging build
# configures afresh and always has the current one.

function(ultracanvas_hash_headers OUT_VAR BASE_DIR)
    set(_patterns "")
    if(ARGN)
        foreach(_sub IN LISTS ARGN)
            list(APPEND _patterns "${BASE_DIR}/${_sub}/*.h" "${BASE_DIR}/${_sub}/*.hpp" "${BASE_DIR}/${_sub}/*.hh")
        endforeach()
    else()
        set(_patterns "${BASE_DIR}/*.h" "${BASE_DIR}/*.hpp" "${BASE_DIR}/*.hh")
    endif()
    file(GLOB_RECURSE _headers RELATIVE "${BASE_DIR}" ${_patterns})
    list(SORT _headers)
    set(_all "")
    foreach(_header IN LISTS _headers)
        file(READ "${BASE_DIR}/${_header}" _text)
        string(REPLACE "\r\n" "\n" _text "${_text}")
        string(APPEND _all "${_header}\n${_text}\n")
    endforeach()
    string(SHA256 _hash "${_all}")
    set(${OUT_VAR} "${_hash}" PARENT_SCOPE)
endfunction()

function(ultracanvas_library_build_id TARGET_NAME DISPLAY_NAME VERSION INCLUDE_DIR C_FUNCTION)
    get_target_property(_type ${TARGET_NAME} TYPE)
    if(NOT _type STREQUAL "SHARED_LIBRARY")
        return()   # static: inside whatever links it, never a DLL of its own
    endif()

    ultracanvas_hash_headers(_hash "${INCLUDE_DIR}")
    string(SUBSTRING "${_hash}" 0 12 _short)
    set(_id "${VERSION}+${_short}")

    # Rewritten only when the id changes (configure_file leaves an identical
    # file alone), so the DLL relinks only then.
    set(_source "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}LibraryBuildId.cpp")
    file(WRITE "${_source}.tmp"
         "// Written by cmake/UltraCanvasLibraryBuildId.cmake: which build this library is.\n"
         "extern \"C\" const char* ${C_FUNCTION}() { return \"${_id}\"; }\n")
    configure_file("${_source}.tmp" "${_source}" COPYONLY)
    target_sources(${TARGET_NAME} PRIVATE "${_source}")

    if(WIN32)
        get_target_property(_output ${TARGET_NAME} OUTPUT_NAME)
        if(NOT _output)
            set(_output ${TARGET_NAME})
        endif()
        set(_dll "${CMAKE_SHARED_LIBRARY_PREFIX}${_output}${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set_property(GLOBAL APPEND PROPERTY ULTRACANVAS_LIBRARY_CHECKS
                     "${_dll}|${C_FUNCTION}|${DISPLAY_NAME}|${_id}")
    endif()
    message(STATUS "  ${DISPLAY_NAME} build id: ${_id}")
endfunction()
