# SmartHome/CMakeLists.txt
# Smart Home — devices, rooms, scenes and automations for the connected home,
# behind one API that hides the protocol differences.
#
# Two targets, matching the two layers described in STATUS.md:
#   SmartHome    — the engine and the public facade (no UI dependency)
#   SmartHomeUI  — the dashboard widgets, needs the UltraCanvas UI library
#
# The five protocol backends are each behind their own option, all OFF by
# default. KNX is the one that builds today: it speaks KNXnet/IP straight over
# sockets and needs no third party at all, so ULTRACANVAS_SMARTHOME_KNX=ON
# works out of the box. The other four each need a vendor SDK that is not
# vendored here, and CMake says which one is missing rather than letting the
# compiler do it.
# Turning an option on defines ULTRACANVAS_SMARTHOME_<NAME>, which is what
# core/SmartHomeProtocolRegistry.cpp keys off to register that backend's
# factory. With all of them off the module still builds and runs — it simply
# has no protocols until an application registers one of its own through
# SmartHomeAPI::RegisterProtocol.
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

# ── The engine and the public facade ─────────────────────────────────────────

set(SMARTHOME_SOURCES
    core/UltraCanvasSmartHomeManager.cpp
    core/SmartHomeProtocolRegistry.cpp
)

# ── Protocol backends: all OFF, none of them can build yet ───────────────────

option(ULTRACANVAS_SMARTHOME_MATTER "Build the Matter backend (needs the connectedhomeip SDK)" OFF)
option(ULTRACANVAS_SMARTHOME_THREAD "Build the Thread backend (needs OpenThread)" OFF)
option(ULTRACANVAS_SMARTHOME_ZIGBEE "Build the Zigbee backend (needs Silicon Labs EZSP or TI ZNP)" OFF)
option(ULTRACANVAS_SMARTHOME_ZWAVE  "Build the Z-Wave backend" OFF)
option(ULTRACANVAS_SMARTHOME_KNX    "Build the KNX backend (no third party; this one builds today)" OFF)

# ── Crypto backend for the protocol SDKs ─────────────────────────────────────
#
# Matter and Thread both need public-key crypto: X.509 and ECDSA on P-256 for
# Matter's device attestation, and the same primitives inside OpenThread's
# commissioner. Neither asks UltraCrypt for it — connectedhomeip carries its own
# DeviceAttestationCredsProvider — so the choice here is which backend those
# SDKs are built against.
#
# mbedTLS, for three reasons:
#   * It is what connectedhomeip and OpenThread default to, so both SDKs share
#     one stack instead of each bringing its own.
#   * ~1 MB static against OpenSSL's ~4-5 MB, which matters on the ARM and
#     RISC-V boards ULTRA OS targets.
#   * OpenSSL is only linked on Linux and Android today (Windows uses Schannel,
#     macOS SecureTransport), so it would be a new dependency on two of the
#     three desktop platforms. Docs/Modules/UltraCrypt/README.md §3 makes the
#     same point: "there is no free ride".
#
# This does NOT touch UltraCrypt. libsodium remains the framework's crypto
# backend for symmetric work; it has no P-256 and no X.509, so it was never a
# candidate for this job and the 2026-08-10 ruling is unaffected.
find_package(MbedTLS QUIET)
if(NOT MbedTLS_FOUND)
    find_path(MBEDTLS_INCLUDE_DIR mbedtls/version.h)
    find_library(MBEDTLS_CRYPTO_LIBRARY NAMES mbedcrypto)
    find_library(MBEDTLS_X509_LIBRARY NAMES mbedx509)
    find_library(MBEDTLS_TLS_LIBRARY NAMES mbedtls)
    if(MBEDTLS_INCLUDE_DIR AND MBEDTLS_CRYPTO_LIBRARY AND MBEDTLS_X509_LIBRARY)
        set(MbedTLS_FOUND TRUE)
    endif()
endif()

# ── Which backends can actually be built ─────────────────────────────────────
#
# Each backend needs its vendor SDK present. None is vendored here, so an
# option turned on without its SDK is a configure-time error naming what is
# missing, rather than a wall of missing-header output from the compiler.

set(_sh_backends "")
foreach(_sh_proto Matter Thread Zigbee ZWave KNX)
    string(TOUPPER ${_sh_proto} _sh_upper)
    if(NOT ULTRACANVAS_SMARTHOME_${_sh_upper})
        continue()
    endif()

    set(_sh_missing "")
    if(_sh_proto STREQUAL "Matter")
        find_path(CHIP_INCLUDE_DIR platform/CHIPDeviceLayer.h)
        if(NOT CHIP_INCLUDE_DIR)
            list(APPEND _sh_missing "connectedhomeip (Matter SDK)")
        endif()
        if(NOT MbedTLS_FOUND)
            list(APPEND _sh_missing "mbedTLS")
        endif()
    elseif(_sh_proto STREQUAL "Thread")
        find_path(OPENTHREAD_INCLUDE_DIR openthread/instance.h)
        if(NOT OPENTHREAD_INCLUDE_DIR)
            list(APPEND _sh_missing "OpenThread")
        endif()
        if(NOT MbedTLS_FOUND)
            list(APPEND _sh_missing "mbedTLS")
        endif()
    elseif(_sh_proto STREQUAL "Zigbee")
        # EZSP only. The backend also carries TI Z-Stack (ZNP) branches, but
        # every one of them is `return false; // Not implemented`, so offering
        # the choice would only invite someone to pick the half that does
        # nothing. Those branches stay inert: nothing defines
        # ULTRACANVAS_WITH_ZSTACK.
        find_path(EZSP_INCLUDE_DIR ezsp/ezsp.h)
        if(NOT EZSP_INCLUDE_DIR)
            list(APPEND _sh_missing "Silicon Labs EZSP (libezsp)")
        endif()
    elseif(_sh_proto STREQUAL "ZWave")
        find_path(OPENZWAVE_INCLUDE_DIR openzwave/Manager.h)
        if(NOT OPENZWAVE_INCLUDE_DIR)
            list(APPEND _sh_missing "OpenZWave (libopenzwave1.6-dev)")
        endif()
        # OpenZWave is LGPL 2.1 and its packages ship BOTH libopenzwave.a and
        # libopenzwave.so. A plain find_library or -lopenzwave can pick the
        # archive, which would pull LGPL code into this binary statically and
        # carry the relinking obligation with it. Ask for the shared object by
        # name so that cannot happen quietly, and refuse the static one.
        find_library(OPENZWAVE_SHARED_LIBRARY
                     NAMES libopenzwave.so libopenzwave.dylib openzwave.dll)
        if(NOT OPENZWAVE_SHARED_LIBRARY)
            find_library(OPENZWAVE_ANY_LIBRARY NAMES openzwave)
            if(OPENZWAVE_ANY_LIBRARY)
                list(APPEND _sh_missing
                     "a SHARED OpenZWave (found only ${OPENZWAVE_ANY_LIBRARY}; "
                     "OpenZWave is LGPL 2.1 and this project links it dynamically)")
            else()
                list(APPEND _sh_missing "OpenZWave (libopenzwave1.6-dev)")
            endif()
        endif()
    endif()
    # KNX speaks KNXnet/IP straight over sockets and needs nothing.

    if(_sh_missing)
        string(REPLACE ";" ", " _sh_missing_text "${_sh_missing}")
        message(FATAL_ERROR
            "ULTRACANVAS_SMARTHOME_${_sh_upper}=ON but ${_sh_missing_text} "
            "not found. Install it, or leave the option OFF. "
            "See SmartHome/STATUS.md.")
    endif()

    list(APPEND SMARTHOME_SOURCES protocols/${_sh_proto}/${_sh_proto}Protocol.cpp)
    list(APPEND _sh_backends ${_sh_proto})
endforeach()

add_library(SmartHome STATIC ${SMARTHOME_SOURCES})
set_target_properties(SmartHome PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "smarthome")
target_compile_features(SmartHome PUBLIC cxx_std_20)
target_include_directories(SmartHome PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../UltraCanvas/include)
target_include_directories(SmartHome PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core)

# One definition per enabled backend, and its headers on the private include
# path. SmartHomeProtocolRegistry.cpp reads these to decide what to register.
foreach(_sh_proto ${_sh_backends})
    string(TOUPPER ${_sh_proto} _sh_upper)
    target_compile_definitions(SmartHome PRIVATE ULTRACANVAS_SMARTHOME_${_sh_upper}=1)
    if(_sh_proto STREQUAL "ZWave")
        # Selects the real OpenZWave code. Without it the backend compiles into
        # a shell: the 162 OpenZWave calls sit behind this macro, the library is
        # never referenced, and the link succeeds while doing nothing.
        target_compile_definitions(SmartHome PRIVATE ULTRACANVAS_WITH_ZWAVE=1)
        target_include_directories(SmartHome PRIVATE ${OPENZWAVE_INCLUDE_DIR}
                                                     ${OPENZWAVE_INCLUDE_DIR}/openzwave)
        # The shared object by absolute path: naming the target "openzwave" here
        # would put -lopenzwave on the link line and let the linker choose the
        # archive again.
        target_link_libraries(SmartHome PUBLIC ${OPENZWAVE_SHARED_LIBRARY})
    endif()
    if(_sh_proto STREQUAL "Zigbee")
        # Selects the EZSP half of the backend's #ifdef pairs.
        target_compile_definitions(SmartHome PRIVATE ULTRACANVAS_WITH_EZSP=1)
        target_include_directories(SmartHome PRIVATE ${EZSP_INCLUDE_DIR})
    endif()
    target_include_directories(SmartHome PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${_sh_proto})
endforeach()

# Matter and Thread carry their PKI in their own SDKs; both are built against
# mbedTLS, so the link line needs it once for whichever of the two is enabled.
if("Matter" IN_LIST _sh_backends OR "Thread" IN_LIST _sh_backends)
    if(TARGET MbedTLS::mbedtls)
        target_link_libraries(SmartHome PUBLIC MbedTLS::mbedtls MbedTLS::mbedx509
                                               MbedTLS::mbedcrypto)
    else()
        target_include_directories(SmartHome PRIVATE ${MBEDTLS_INCLUDE_DIR})
        target_link_libraries(SmartHome PUBLIC ${MBEDTLS_TLS_LIBRARY}
                                               ${MBEDTLS_X509_LIBRARY}
                                               ${MBEDTLS_CRYPTO_LIBRARY})
    endif()
endif()

find_package(Threads REQUIRED)
target_link_libraries(SmartHome PUBLIC Threads::Threads)

if(_sh_backends)
    string(REPLACE ";" ", " _sh_list "${_sh_backends}")
    message(STATUS "  [✓] SmartHome - ENABLED (target: SmartHome, backends: ${_sh_list})")
else()
    message(STATUS "  [✓] SmartHome - ENABLED (target: SmartHome, no protocol backends)")
endif()

# ── The dashboard widgets ────────────────────────────────────────────────────

if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] SmartHomeUI - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()

# Every widget the headers declare now has an implementation.
add_library(SmartHomeUI STATIC
    ui/UltraCanvasSmartHomeDeviceCard.cpp
    ui/UltraCanvasSmartHomePanel.cpp
    ui/UltraCanvasSmartHomeDeviceControl.cpp
    ui/UltraCanvasSmartHomeDialogs.cpp
    ui/UltraCanvasSmartHomeEditors.cpp
    ui/UltraCanvasSmartHomeAdvanced.cpp)
set_target_properties(SmartHomeUI PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "smarthome_ui")
target_compile_features(SmartHomeUI PUBLIC cxx_std_20)
target_include_directories(SmartHomeUI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${ULTRACANVAS_INCLUDE_DIRS})
target_link_libraries(SmartHomeUI PUBLIC SmartHome ${ULTRACANVAS_LIBRARY})
message(STATUS "  [✓] SmartHomeUI - ENABLED (target: SmartHomeUI)")

# ── Tests ────────────────────────────────────────────────────────────────────

# The option and the top-level enable_testing() live in the root CMakeLists,
# where every other suite declares them.
if(ULTRACANVAS_BUILD_SMARTHOME_TESTS)
    add_executable(SmartHomeFacadeTest tests/FacadeTest.cpp)
    target_link_libraries(SmartHomeFacadeTest PRIVATE SmartHome)

    add_executable(SmartHomeWidgetTest tests/WidgetTest.cpp)
    target_link_libraries(SmartHomeWidgetTest PRIVATE SmartHomeUI)

    # Only meaningful when the backend is actually in the build; it is also the
    # runtime half of the LGPL dynamic-link guarantee.
    if("ZWave" IN_LIST _sh_backends)
        add_executable(SmartHomeZWaveLinkTest tests/ZWaveLinkTest.cpp)
        target_link_libraries(SmartHomeZWaveLinkTest PRIVATE SmartHome)
        target_include_directories(SmartHomeZWaveLinkTest PRIVATE
                                   ${CMAKE_CURRENT_SOURCE_DIR}/protocols/ZWave)
        add_test(NAME SmartHomeZWaveLink COMMAND SmartHomeZWaveLinkTest)
    endif()

    add_test(NAME SmartHomeFacade COMMAND SmartHomeFacadeTest)
    add_test(NAME SmartHomeWidgets COMMAND SmartHomeWidgetTest)
    message(STATUS "  [✓] SmartHome tests - ENABLED")
endif()
