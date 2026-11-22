# Apps/UOSLocalisation/CMakeLists.txt
# UOS-Localisation - the ULTRA OS translation editor: the localisation editor
# (UltraCanvasLocalisationEditor) in a window of its own. It was the
# Translations page of UOS-Settings.
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UOS-Localisation - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()

add_executable(UOSLocalisation main.cpp)
# One copy per user (UltraCanvasSingleInstance.h).
ultracanvas_single_instance(UOSLocalisation)
# The program is called UOS-Localisation; the CMake target cannot carry the
# hyphen in every generator, so only the output name does.
set_target_properties(UOSLocalisation PROPERTIES
    OUTPUT_NAME "UOS-Localisation"
    ENABLE_EXPORTS ON)

target_include_directories(UOSLocalisation PRIVATE ${ULTRACANVAS_INCLUDE_DIRS})
target_link_libraries(UOSLocalisation PRIVATE ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UOSLocalisation PRIVATE fmt::fmt)
endif()
target_compile_features(UOSLocalisation PRIVATE cxx_std_20)

# UOS-Localisation versions itself: the number comes from the first line of
# Docs/UOSLocalisation/CHANGELOG.md through cmake/UltraCanvasVersion.cmake.
target_compile_definitions(UOSLocalisation PRIVATE
    UOSLOCALISATION_VERSION="${UOSLOCALISATION_VERSION}"
    UCAPP_ICON_PATH="media/appicon/UOS-Localisation.png"
    $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

ultracanvas_embed_windows_manifest(UOSLocalisation)
ultracanvas_embed_app_icon(UOSLocalisation
    "${CMAKE_SOURCE_DIR}/media/appicon/UOS-Localisation.png")

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS UOSLocalisation RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UOS-Localisation.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UOS-Localisation.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UOS-Localisation.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [✓] UOS-Localisation - ENABLED (target: UOSLocalisation)")
message(STATUS "      Version ${UOSLOCALISATION_VERSION} (Docs/UOSLocalisation/CHANGELOG.md)")
