# Apps/UltraCanvasStart/CMakeLists.txt
# UltraCanvasStart — sets a machine up for UltraCanvas development: finds out
# what is installed, installs what is missing, fetches the prebuilt SDK,
# writes a project skeleton and explains how to work with an AI assistant.
#
# Two targets:
#   UltraCanvasStartEngine  a static library: detection, the dependency table,
#                           the checks, the plan, the scaffold. Links the
#                           UltraCanvas core for RunProcessCaptured and the
#                           hardware snapshot (and UltraNet for the SDK
#                           download when it was built) but opens no window,
#                           so the CLI modes and the test suite use it headless.
#   UltraCanvasStart        the GUI application on top of it.
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraCanvasStart - SKIPPED (UltraCanvas core library not built)")
    return()
endif()

add_library(UltraCanvasStartEngine STATIC
    engine/StartTypes.cpp
    engine/StartSystem.cpp
    engine/StartPackages.cpp
    engine/StartChecks.cpp
    engine/StartPlan.cpp
    engine/StartSdk.cpp
    engine/StartProject.cpp
    engine/StartAi.cpp
    engine/StartRunner.cpp
)
set_target_properties(UltraCanvasStartEngine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultracanvasstart_engine")
target_compile_features(UltraCanvasStartEngine PUBLIC cxx_std_20)
target_include_directories(UltraCanvasStartEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine)
target_include_directories(UltraCanvasStartEngine PRIVATE ${ULTRACANVAS_INCLUDE_DIRS})
target_link_libraries(UltraCanvasStartEngine PUBLIC ${ULTRACANVAS_LIBRARY})

# The SDK download needs UltraNet. With a SHARED core UltraNet is absorbed
# into libultracanvas and only its headers are needed; with a static core the
# target is linked itself (the same logic UltraSocialEngine uses).
if(TARGET UltraNet)
    get_target_property(_ucstart_core_type ${ULTRACANVAS_LIBRARY} TYPE)
    if(_ucstart_core_type STREQUAL "SHARED_LIBRARY")
        target_include_directories(UltraCanvasStartEngine PRIVATE
            $<TARGET_PROPERTY:UltraNet,INTERFACE_INCLUDE_DIRECTORIES>)
    else()
        target_link_libraries(UltraCanvasStartEngine PUBLIC UltraNet)
    endif()
    target_compile_definitions(UltraCanvasStartEngine PRIVATE ULTRACANVASSTART_HAS_NET=1)
    message(STATUS "  [✓] UltraCanvasStart engine - ENABLED, with SDK download (UltraNet)")
else()
    message(STATUS "  [✓] UltraCanvasStart engine - ENABLED, without SDK download (no UltraNet)")
endif()

# ── UltraCanvasStart GUI application ─────────────────────────────────────────
add_executable(UltraCanvasStart
    main.cpp
    ui/UltraCanvasStartWindow.cpp
)
set_target_properties(UltraCanvasStart PROPERTIES ENABLE_EXPORTS ON)
target_include_directories(UltraCanvasStart PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(UltraCanvasStart PRIVATE UltraCanvasStartEngine ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UltraCanvasStart PRIVATE fmt::fmt)
endif()
target_compile_features(UltraCanvasStart PRIVATE cxx_std_20)

# The version comes from the first line of Docs/UltraCanvasStart/CHANGELOG.md
# through cmake/UltraCanvasVersion.cmake - never a literal of its own.
target_compile_definitions(UltraCanvasStart PRIVATE
    ULTRACANVASSTART_VERSION="${ULTRACANVASSTART_VERSION}"
    UCAPP_ICON_PATH="media/appicon/UltraCanvasStart.png"
    $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

ultracanvas_embed_windows_manifest(UltraCanvasStart)
ultracanvas_embed_app_icon(UltraCanvasStart
    "${CMAKE_SOURCE_DIR}/media/appicon/UltraCanvasStart.png")

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS UltraCanvasStart RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraCanvasStart.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraCanvasStart.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraCanvasStart.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [✓] UltraCanvasStart GUI app - ENABLED (target: UltraCanvasStart)")
message(STATUS "      Version ${ULTRACANVASSTART_VERSION} (Docs/UltraCanvasStart/CHANGELOG.md)")
