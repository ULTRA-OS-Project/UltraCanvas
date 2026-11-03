# Apps/SmartHomeCenter/CMakeLists.txt
# SmartHome Center — the home control window: rooms, devices, scenes, energy
# and activity, on the SmartHome module. With no radio enabled it opens the
# module's simulated demo home.
#
# The GUI application, over a small core with no window in it
# (SmartHomeCenterCore: the model, the demo home, the store path and the
# command-line actions) that SmartHomeCenterCliTest checks headless. It talks
# to the home through SmartHomeAPI alone (SmartHome/include), so it links the
# engine and nothing below it - and the module's widgets (SmartHomeUI), for
# the device icons and the network map, so a device looks the same here as
# in them.
#
# Version: 0.2.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] SmartHome Center app - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()
if(NOT TARGET SmartHome OR NOT TARGET SmartHomeUI)
    message(STATUS "  [−] SmartHome Center app - SKIPPED (SmartHome module or its widgets not built)")
    return()
endif()

# What the window and the command line share, with no UI types.
add_library(SmartHomeCenterCore STATIC
    ui/SmartHomeCenterCli.cpp
    ui/SmartHomeCenterDemo.cpp
    ui/SmartHomeCenterModel.cpp
    ui/SmartHomeCenterPaths.cpp
)
target_include_directories(SmartHomeCenterCore PUBLIC
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(SmartHomeCenterCore PUBLIC SmartHomeUI SmartHome ${ULTRACANVAS_LIBRARY})
target_compile_features(SmartHomeCenterCore PUBLIC cxx_std_20)

add_executable(SmartHomeCenter
    main.cpp
    ui/SmartHomeCenterControls.cpp
    ui/SmartHomeCenterDevicePage.cpp
    ui/SmartHomeCenterFirstRun.cpp
    ui/SmartHomeCenterRoomsPage.cpp
    ui/SmartHomeCenterScenesPage.cpp
    ui/SmartHomeCenterSettingsPage.cpp
    ui/SmartHomeCenterStyle.cpp
    ui/SmartHomeCenterWindow.cpp
)
# One copy per user (UltraCanvasSingleInstance.h): it owns the radios.
ultracanvas_single_instance(SmartHomeCenter)

# Export dynamic symbols so on-demand plugin modules resolve core symbols
# when the core is linked statically, as the other applications do.
set_target_properties(SmartHomeCenter PROPERTIES ENABLE_EXPORTS ON)

target_include_directories(SmartHomeCenter PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)

# Module before the core on the link line: GNU ld resolves left to right.
target_link_libraries(SmartHomeCenter PRIVATE SmartHomeCenterCore SmartHomeUI SmartHome ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(SmartHomeCenter PRIVATE fmt::fmt)
endif()
find_package(Threads REQUIRED)
target_link_libraries(SmartHomeCenter PRIVATE Threads::Threads)
target_compile_features(SmartHomeCenter PRIVATE cxx_std_20)

# SmartHome Center versions itself, independently of the framework: the
# number comes from the first line of Docs/SmartHomeCenter/CHANGELOG.md
# through cmake/UltraCanvasVersion.cmake — never a literal of its own.
target_compile_definitions(SmartHomeCenter PRIVATE
    SMARTHOMECENTER_VERSION="${SMARTHOMECENTER_VERSION}"
    $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

ultracanvas_embed_windows_manifest(SmartHomeCenter)

# One icon, everywhere the app is drawn: main.cpp names this same file for
# the window and taskbar; UCAPP_ICON_PATH is the core's fallback. The PNG is
# the 256 px render of media/appicon/SmartHomeCenter.svg.
target_compile_definitions(SmartHomeCenter PRIVATE
    UCAPP_ICON_PATH="media/appicon/SmartHomeCenter.png")
ultracanvas_embed_app_icon(SmartHomeCenter
    "${CMAKE_SOURCE_DIR}/media/appicon/SmartHomeCenter.png")

# The command line on the demo home, with no display: --list, --set,
# --scene, --history and --log through the functions main() calls.
if(BUILD_TESTS OR ULTRACANVAS_BUILD_SMARTHOME_TESTS)
    add_executable(SmartHomeCenterCliTest tests/SmartHomeCenterCliTest.cpp)
    target_link_libraries(SmartHomeCenterCliTest PRIVATE SmartHomeCenterCore)
    target_include_directories(SmartHomeCenterCliTest PRIVATE
        ${CMAKE_SOURCE_DIR}/SmartHome/protocols/Simulated)
    add_test(NAME SmartHomeCenterCli COMMAND SmartHomeCenterCliTest)
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS SmartHomeCenter RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/SmartHomeCenter.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/SmartHomeCenter.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/SmartHomeCenter.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [✓] SmartHome Center app - ENABLED (target: SmartHomeCenter)")
message(STATUS "      Version ${SMARTHOMECENTER_VERSION} (Docs/SmartHomeCenter/CHANGELOG.md)")
