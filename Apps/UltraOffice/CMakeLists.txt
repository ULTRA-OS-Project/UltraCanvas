# Apps/UltraOffice/CMakeLists.txt
# UltraOffice - documents, spreadsheets and presentations in one window, on
# the framework's elements: UltraCanvasRichTextEdit, UltraCanvasSpreadsheet
# and UltraCanvasMediaViewer (Docs/UltraOffice/README.md).
#
# Version: 0.5.0 - UltraOfficePanels.cpp (the tool panels built from the tool table)
# Version: 0.4.0 - UltraOfficeTools.cpp (the tool table, the toolbars built from it)
# Version: 0.3.0 - UltraOfficeSettings.cpp (the settings' model), UltraOfficeSettingsDialog.cpp (the Settings window)
# Version: 0.2.0 - UltraOfficeTemplates.cpp (new from template); the shipped templates are media/templates/office,
#                  which every package copies with the rest of media/
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraOffice - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()

add_executable(UltraOffice
    main.cpp
    UltraOfficeWindow.cpp
    UltraOfficeHost.cpp
    UltraOfficeTools.cpp
    UltraOfficePanels.cpp
    UltraOfficeTemplates.cpp
    UltraOfficeSettings.cpp
    UltraOfficeSettingsDialog.cpp
    UltraOfficeDocumentHost.cpp
    UltraOfficeSpreadsheetHost.cpp
    UltraOfficePresentationHost.cpp
)
# One copy per user (UltraCanvasSingleInstance.h).
ultracanvas_single_instance(UltraOffice)
set_target_properties(UltraOffice PROPERTIES ENABLE_EXPORTS ON)

target_include_directories(UltraOffice PRIVATE ${ULTRACANVAS_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(UltraOffice PRIVATE ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UltraOffice PRIVATE fmt::fmt)
endif()
target_compile_features(UltraOffice PRIVATE cxx_std_20)

# UltraOffice versions itself: the number comes from the first line of
# Docs/UltraOffice/CHANGELOG.md through cmake/UltraCanvasVersion.cmake.
target_compile_definitions(UltraOffice PRIVATE
    ULTRAOFFICE_VERSION="${ULTRAOFFICE_VERSION}"
    UCAPP_ICON_PATH="media/appicon/UltraOffice.png"
    $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

ultracanvas_embed_windows_manifest(UltraOffice)
ultracanvas_embed_app_icon(UltraOffice "${CMAKE_SOURCE_DIR}/media/appicon/UltraOffice.png")

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS UltraOffice RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraOffice.desktop DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraOffice.png DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraOffice.svg DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [✓] UltraOffice - ENABLED (target: UltraOffice)")
message(STATUS "      Version ${ULTRAOFFICE_VERSION} (Docs/UltraOffice/CHANGELOG.md)")
