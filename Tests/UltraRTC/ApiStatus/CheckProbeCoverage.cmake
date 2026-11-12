# Tests/UltraRTC/ApiStatus/CheckProbeCoverage.cmake
# Run as a test (cmake -DHEADER=... -DPROBE_DIR=... -P this): every
# UltraRtc_* function declared in UltraRTCCore.h has exactly one
# ULTRARTC_PROBE, and every probe names a function the header declares. A
# status report that silently lacks a function would read as "all working".

file(READ "${HEADER}" header)
# Declarations only: comments also name functions.
string(REGEX REPLACE "//[^\n]*" "" header "${header}")
string(REGEX MATCHALL "UltraRtc_[A-Za-z0-9]+[ \t]*\\(" declared "${header}")
set(functions "")
foreach(match IN LISTS declared)
    string(REGEX REPLACE "[ \t]*\\($" "" name "${match}")
    list(APPEND functions "${name}")
endforeach()
list(REMOVE_DUPLICATES functions)

file(GLOB probe_files "${PROBE_DIR}/Probe*.cpp")
set(probed "")
foreach(probe_file IN LISTS probe_files)
    file(READ "${probe_file}" source)
    string(REGEX MATCHALL "ULTRARTC_PROBE\\(\"[A-Za-z]+\", *UltraRtc_[A-Za-z0-9]+\\)" probes "${source}")
    foreach(match IN LISTS probes)
        string(REGEX REPLACE ".*(UltraRtc_[A-Za-z0-9]+)\\)$" "\\1" name "${match}")
        list(APPEND probed "${name}")
    endforeach()
endforeach()

set(problems "")
foreach(name IN LISTS functions)
    list(FIND probed "${name}" at)
    if(at EQUAL -1)
        string(APPEND problems "  ${name} is declared in UltraRTCCore.h but has no probe\n")
    endif()
endforeach()
foreach(name IN LISTS probed)
    list(FIND functions "${name}" at)
    if(at EQUAL -1)
        string(APPEND problems "  ${name} has a probe but UltraRTCCore.h does not declare it\n")
    endif()
endforeach()
set(unique "${probed}")
list(REMOVE_DUPLICATES unique)
list(LENGTH probed probe_count)
list(LENGTH unique unique_count)
if(NOT probe_count EQUAL unique_count)
    string(APPEND problems "  a function has more than one probe\n")
endif()

list(LENGTH functions function_count)
if(function_count EQUAL 0)
    message(FATAL_ERROR "UltraRtcApiStatus coverage: no UltraRtc_* function found in ${HEADER}")
endif()
if(problems)
    message(FATAL_ERROR "UltraRtcApiStatus coverage:\n${problems}"
                        "Add the probe to Tests/UltraRTC/ApiStatus/Probe<Area>.cpp.")
endif()
message(STATUS "UltraRtcApiStatus coverage: ${function_count} functions, ${probe_count} probes")
