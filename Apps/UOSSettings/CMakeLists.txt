# Apps/UOSSettings/CMakeLists.txt
# UOS-Settings — the ULTRA OS settings application: settings that belong to
# the system rather than to one application (first page: where the file
# dialogs of all applications open).
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UOS-Settings - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()

# The Desktop page reads and writes UltraDesktop's settings file through the
# desktop's own settings code (no UI in it), so the two programs cannot
# disagree about the file - whether or not UltraDesktop itself is built.
set(UOSSETTINGS_DESKTOP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../UltraDesktop/ui)
add_executable(UOSSettings
    main.cpp
    ui/UOSSettingsWindow.cpp
    ${UOSSETTINGS_DESKTOP_DIR}/UltraDesktopSettings.cpp
)
# The program is called UOS-Settings; the CMake target cannot carry the hyphen
# in every generator, so only the output name does.
set_target_properties(UOSSettings PROPERTIES
    OUTPUT_NAME "UOS-Settings"
    ENABLE_EXPORTS ON)

target_include_directories(UOSSettings PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${UOSSETTINGS_DESKTOP_DIR})
target_link_libraries(UOSSettings PRIVATE ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UOSSettings PRIVATE fmt::fmt)
endif()
target_compile_features(UOSSettings PRIVATE cxx_std_20)

# UOS-Settings versions itself: the number comes from the first line of
# Docs/UOSSettings/CHANGELOG.md through cmake/UltraCanvasVersion.cmake.
target_compile_definitions(UOSSettings PRIVATE
    UOSSETTINGS_VERSION="${UOSSETTINGS_VERSION}"
    UCAPP_ICON_PATH="media/appicon/UOS-Settings.png"
    $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

ultracanvas_embed_windows_manifest(UOSSettings)
ultracanvas_embed_app_icon(UOSSettings
    "${CMAKE_SOURCE_DIR}/media/appicon/UOS-Settings.png")

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS UOSSettings RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UOS-Settings.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UOS-Settings.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UOS-Settings.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [✓] UOS-Settings - ENABLED (target: UOSSettings)")
message(STATUS "      Version ${UOSSETTINGS_VERSION} (Docs/UOSSettings/CHANGELOG.md)")
