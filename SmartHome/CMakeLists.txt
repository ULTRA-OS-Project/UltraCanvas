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
option(ULTRACANVAS_SMARTHOME_THREAD "Build the Thread backend (needs a BUILT OpenThread, not just headers)" OFF)
option(ULTRACANVAS_SMARTHOME_ZIGBEE "Build the Zigbee backend (NO transport implemented — see SmartHome/STATUS.md)" OFF)
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
        # connectedhomeip does not install either. Its GN build is packaged
        # by scripts/package-chip.sh into one archive plus a link list; the
        # recipe is in Docs/Dependencies.md. Three directories are needed:
        # the checkout (headers), its GN output (generated headers), and the
        # package directory that script wrote.
        set(CHIP_SOURCE_DIR "" CACHE PATH "connectedhomeip checkout")
        set(CHIP_BUILD_DIR "" CACHE PATH "connectedhomeip GN output directory (out/host)")
        set(CHIP_PACKAGE_DIR "" CACHE PATH "directory written by SmartHome/scripts/package-chip.sh")
        if(NOT CHIP_SOURCE_DIR OR NOT EXISTS "${CHIP_SOURCE_DIR}/src/controller/CHIPDeviceController.h")
            list(APPEND _sh_missing "CHIP_SOURCE_DIR (a connectedhomeip checkout)")
        endif()
        if(NOT CHIP_BUILD_DIR OR NOT EXISTS "${CHIP_BUILD_DIR}/gen/include/matter/tracing/build_config.h")
            list(APPEND _sh_missing "CHIP_BUILD_DIR (a chip-tool GN build; generated headers)")
        endif()
        if(NOT CHIP_PACKAGE_DIR OR NOT EXISTS "${CHIP_PACKAGE_DIR}/link.txt")
            list(APPEND _sh_missing "CHIP_PACKAGE_DIR (run SmartHome/scripts/package-chip.sh)")
        endif()
        # mbedTLS comes from inside that build too (chip_crypto="mbedtls").
    elseif(_sh_proto STREQUAL "Thread")
        # OpenThread does not install: `cmake --install` puts one binary in
        # bin/ and nothing else. A host links against the build tree, the way
        # OpenThread's own ot-cli does. So this wants two directories — the
        # source checkout (public headers, and the POSIX platform header that
        # lives under src/) and the build made from it with OT_PLATFORM=posix.
        # The recipe that produced a working build is in Docs/Dependencies.md.
        set(OPENTHREAD_SOURCE_DIR "" CACHE PATH "OpenThread source checkout")
        set(OPENTHREAD_BUILD_DIR "" CACHE PATH "OpenThread build tree (OT_PLATFORM=posix)")
        if(NOT OPENTHREAD_SOURCE_DIR OR NOT EXISTS "${OPENTHREAD_SOURCE_DIR}/include/openthread/instance.h")
            list(APPEND _sh_missing "OPENTHREAD_SOURCE_DIR (an OpenThread checkout)")
        endif()
        if(NOT OPENTHREAD_BUILD_DIR OR NOT EXISTS "${OPENTHREAD_BUILD_DIR}/src/posix/platform/libopenthread-posix.a")
            list(APPEND _sh_missing "OPENTHREAD_BUILD_DIR (an OpenThread build with OT_PLATFORM=posix)")
        endif()
        # mbedTLS comes from inside that build (OpenThread vendors it), so the
        # system package is not needed here.
    elseif(_sh_proto STREQUAL "Zigbee")
        # EZSP only. The backend also carries TI Z-Stack (ZNP) branches, but
        # every one of them is `return false; // Not implemented`, so offering
        # the choice would only invite someone to pick the half that does
        # nothing. Those branches stay inert: nothing defines
        # ULTRACANVAS_WITH_ZSTACK.
        # The ASH framing and EZSP frame layers are in ezsp/ in this repository,
        # so this backend needs no vendor library. What it does need is a serial
        # NCP at run time.
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
    if(_sh_proto STREQUAL "Zigbee")
        list(APPEND SMARTHOME_SOURCES
             protocols/Zigbee/ezsp/AshCodec.cpp
             protocols/Zigbee/ezsp/EzspFrame.cpp
             protocols/Zigbee/ezsp/AshTransport.cpp)
    endif()
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
    if(_sh_proto STREQUAL "Matter")
        # Selects the real connectedhomeip code; without it the backend
        # compiles into a shell, as with the other gating macros.
        target_compile_definitions(SmartHome PRIVATE ULTRACANVAS_WITH_MATTER=1 CHIP_HAVE_CONFIG_H=1)
        # The include set and project config are chip-tool's, because the
        # packaged objects were compiled with them and the config headers
        # shape the SDK's structures.
        target_include_directories(SmartHome PRIVATE
            ${CHIP_SOURCE_DIR}/examples/chip-tool/include
            ${CHIP_SOURCE_DIR}/config/standalone
            ${CHIP_SOURCE_DIR}/src
            ${CHIP_SOURCE_DIR}/src/include
            ${CHIP_SOURCE_DIR}/zzz_generated/app-common
            ${CHIP_SOURCE_DIR}/src/tracing/perfetto/include
            ${CHIP_SOURCE_DIR}/third_party/nlassert/repo/include
            ${CHIP_SOURCE_DIR}/third_party/nlio/repo/include
            ${CHIP_SOURCE_DIR}/third_party/nlfaultinjection/include
            ${CHIP_SOURCE_DIR}/third_party/mbedtls/repo/include
            ${CHIP_SOURCE_DIR}/third_party/inipp/repo/inipp
            ${CHIP_SOURCE_DIR}/third_party/perfetto/repo/sdk
            ${CHIP_BUILD_DIR}/gen/include)
        # chip-tool's file-backed storage is example code, not library code.
        target_sources(SmartHome PRIVATE ${CHIP_SOURCE_DIR}/src/controller/ExamplePersistentStorage.cpp)
        set_source_files_properties(${CHIP_SOURCE_DIR}/src/controller/ExamplePersistentStorage.cpp
            PROPERTIES COMPILE_OPTIONS "-w")
        # The packaged link list, as one group: the SDK's archives refer to
        # one another in both directions and a single pass would not do.
        file(STRINGS "${CHIP_PACKAGE_DIR}/link.txt" _chip_link)
        target_link_libraries(SmartHome PUBLIC -Wl,--start-group ${_chip_link} -Wl,--end-group)
        message(STATUS "  [✓] Matter backend against connectedhomeip at ${CHIP_BUILD_DIR}")
    endif()
    if(_sh_proto STREQUAL "Thread")
        # Selects the real OpenThread code; without it the backend compiles
        # into a shell, as with the other two gating macros.
        target_compile_definitions(SmartHome PRIVATE ULTRACANVAS_WITH_OPENTHREAD=1)
        target_include_directories(SmartHome PRIVATE
            ${OPENTHREAD_SOURCE_DIR}/include                     # public API
            ${OPENTHREAD_SOURCE_DIR}/src/posix/platform/include  # openthread-system.h
            ${OPENTHREAD_SOURCE_DIR}/src)                        # lib/spinel/coprocessor_type.h
        # The archives, in the order OpenThread's own posix host binary links
        # them. The repeats are deliberate: these are static archives with
        # mutual references, and a single pass needs the second mention.
        set(_ot_link_order
            src/posix/platform/libopenthread-posix.a
            src/core/libopenthread-ftd.a
            src/posix/platform/libopenthread-posix.a
            src/lib/hdlc/libopenthread-hdlc.a
            src/lib/spinel/libopenthread-radio-spinel.a
            src/lib/spinel/libopenthread-spinel-rcp.a
            src/core/libopenthread-ftd.a
            third_party/tcplp/libtcplp-ftd.a
            src/core/libopenthread-ftd.a
            third_party/tcplp/libtcplp-ftd.a
            third_party/mbedtls/repo/library/libmbedtls.a
            third_party/mbedtls/repo/library/libmbedx509.a
            third_party/mbedtls/repo/library/libmbedcrypto.a
            third_party/mbedtls/repo/3rdparty/everest/libeverest.a
            third_party/mbedtls/repo/3rdparty/p256-m/libp256m.a
            src/lib/platform/libopenthread-platform.a
            src/lib/url/libopenthread-url.a)
        set(_ot_libs "")
        foreach(_ot_lib ${_ot_link_order})
            if(NOT EXISTS "${OPENTHREAD_BUILD_DIR}/${_ot_lib}")
                message(FATAL_ERROR "OpenThread build is missing ${_ot_lib}; "
                        "build it with OT_PLATFORM=posix and the options in Docs/Dependencies.md")
            endif()
            list(APPEND _ot_libs "${OPENTHREAD_BUILD_DIR}/${_ot_lib}")
        endforeach()
        target_link_libraries(SmartHome PUBLIC ${_ot_libs} util rt)
        message(STATUS "  [✓] Thread backend against OpenThread at ${OPENTHREAD_BUILD_DIR}")
    endif()
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
    endif()
    target_include_directories(SmartHome PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${_sh_proto})
endforeach()

# Matter and Thread carry their PKI in their own SDKs, and both SDK builds
# vendor the mbedTLS they were compiled against (OpenThread's third_party,
# connectedhomeip's chip_crypto="mbedtls"). Those archives are on the link
# lines above; the system mbedTLS package is deliberately NOT added on top —
# two copies of one crypto library in a binary is a bug, not a belt and
# braces. The find_package above stays for the configure summary only.

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

    # Pure framing, so it builds whether or not the Zigbee backend is on.
    add_executable(SmartHomeAshCodecTest tests/AshCodecTest.cpp
                   protocols/Zigbee/ezsp/AshCodec.cpp)
    target_include_directories(SmartHomeAshCodecTest PRIVATE
                               ${CMAKE_CURRENT_SOURCE_DIR}/protocols/Zigbee/ezsp)
    add_test(NAME SmartHomeAshCodec COMMAND SmartHomeAshCodecTest)

    # Likewise: EZSP, APS, ZDO and ZCL are byte layouts, testable anywhere.
    add_executable(SmartHomeEzspFrameTest tests/EzspFrameTest.cpp
                   protocols/Zigbee/ezsp/EzspFrame.cpp)
    target_include_directories(SmartHomeEzspFrameTest PRIVATE
                               ${CMAKE_CURRENT_SOURCE_DIR}/protocols/Zigbee/ezsp)
    add_test(NAME SmartHomeEzspFrame COMMAND SmartHomeEzspFrameTest)

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

    # Likewise for Thread: proves the OpenThread link is real (the version
    # string in GetHardwareInfo can only come from libopenthread-ftd).
    if("Thread" IN_LIST _sh_backends)
        add_executable(SmartHomeThreadLinkTest tests/ThreadLinkTest.cpp)
        target_link_libraries(SmartHomeThreadLinkTest PRIVATE SmartHome)
        target_include_directories(SmartHomeThreadLinkTest PRIVATE
                                   ${CMAKE_CURRENT_SOURCE_DIR}/protocols/Thread)
        add_test(NAME SmartHomeThreadLink COMMAND SmartHomeThreadLinkTest)
    endif()

    # And Matter: proves the connectedhomeip link is real.
    if("Matter" IN_LIST _sh_backends)
        add_executable(SmartHomeMatterLinkTest tests/MatterLinkTest.cpp)
        target_link_libraries(SmartHomeMatterLinkTest PRIVATE SmartHome)
        target_include_directories(SmartHomeMatterLinkTest PRIVATE
                                   ${CMAKE_CURRENT_SOURCE_DIR}/protocols/Matter)
        add_test(NAME SmartHomeMatterLink COMMAND SmartHomeMatterLinkTest)
    endif()

    add_test(NAME SmartHomeFacade COMMAND SmartHomeFacadeTest)
    add_test(NAME SmartHomeWidgets COMMAND SmartHomeWidgetTest)
    message(STATUS "  [✓] SmartHome tests - ENABLED")
endif()
