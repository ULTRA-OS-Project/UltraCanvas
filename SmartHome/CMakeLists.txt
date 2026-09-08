# SmartHome/CMakeLists.txt
# Smart Home — devices, rooms, scenes and automations for the connected home,
# behind one API that hides the protocol differences.
#
# Two targets, matching the two layers described in STATUS.md:
#   SmartHome    — the engine and the public facade (no UI dependency)
#   SmartHomeUI  — the dashboard widgets, needs the UltraCanvas UI library
#
# The five protocol backends are each behind their own option and every one is
# OFF by default, because none of them can build yet: Matter needs the
# connectedhomeip SDK and Zigbee needs the Silicon Labs EZSP / TI ZNP stacks,
# and neither is vendored. Their headers compile; their .cpp files do not.
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
option(ULTRACANVAS_SMARTHOME_KNX    "Build the KNX backend" OFF)

set(_sh_backends "")
foreach(_sh_proto Matter Thread Zigbee ZWave KNX)
    string(TOUPPER ${_sh_proto} _sh_upper)
    if(ULTRACANVAS_SMARTHOME_${_sh_upper})
        list(APPEND SMARTHOME_SOURCES protocols/${_sh_proto}/${_sh_proto}Protocol.cpp)
        list(APPEND _sh_backends ${_sh_proto})
    endif()
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
    target_include_directories(SmartHome PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${_sh_proto})
endforeach()

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

# The dashboard and its two card widgets have bodies; the rest of ui/ is still
# declarations, so listing them here would fail to link. Add each .cpp as it
# gains an implementation.
add_library(SmartHomeUI STATIC
    ui/UltraCanvasSmartHomeDeviceCard.cpp
    ui/UltraCanvasSmartHomePanel.cpp
    ui/UltraCanvasSmartHomeDeviceControl.cpp
    ui/UltraCanvasSmartHomeDialogs.cpp)
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

    add_test(NAME SmartHomeFacade COMMAND SmartHomeFacadeTest)
    add_test(NAME SmartHomeWidgets COMMAND SmartHomeWidgetTest)
    message(STATUS "  [✓] SmartHome tests - ENABLED")
endif()
