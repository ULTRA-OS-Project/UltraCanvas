# Apps/UltraStoreOperator/CMakeLists.txt
# UltraStoreOperator - the operator's window and command line on the
# marketplace service (Server/UltraStoreMarket): the merchants, the review
# and the publish, the reports, the notices, the terms and the policy, the
# audit log (Docs/UltraStoreOperator/README.md).
#
# Two targets:
#   UltraStoreOperatorCore  a static library: the client (Operator.h) on the
#                           admin backend's core (its HTTP call and JSON) and
#                           the store's core. Opens no window, so the
#                           command line and the tests run headless.
#   ultrastoreop            the window when started plainly, the commands
#                           when given one.
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT TARGET UltraStoreAdminCore)
    message(STATUS "  [−] UltraStoreOperator - SKIPPED (UltraStoreAdminCore not built)")
    return()
endif()

add_library(UltraStoreOperatorCore STATIC
    core/Operator.cpp
)
set_target_properties(UltraStoreOperatorCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultrastoreop_core")
target_compile_features(UltraStoreOperatorCore PUBLIC cxx_std_20)
target_include_directories(UltraStoreOperatorCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_include_directories(UltraStoreOperatorCore PRIVATE ${ULTRACANVAS_INCLUDE_DIRS})
target_link_libraries(UltraStoreOperatorCore PUBLIC UltraStoreAdminCore)
message(STATUS "  [✓] UltraStoreOperator core - ENABLED")

# The program: the window (ui/) when started plainly, the commands (cli/)
# when given one. The window's look is the admin backend's theme
# (Apps/UltraStoreAdmin/ui/AdminTheme.h) with the operator tier's marks.
add_executable(ultrastoreop
    ui/main.cpp
    ui/OperatorWindow.cpp
    ui/OperatorPages.cpp
    cli/Commands.cpp
)
ultracanvas_single_instance(ultrastoreop)
target_include_directories(ultrastoreop PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/cli
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../UltraStoreAdmin/ui)
target_link_libraries(ultrastoreop PRIVATE UltraStoreOperatorCore ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(ultrastoreop PRIVATE fmt::fmt)
endif()
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(ultrastoreop PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
endif()
target_compile_features(ultrastoreop PRIVATE cxx_std_20)
# The version comes from the first line of Docs/UltraStoreOperator/CHANGELOG.md
# through cmake/UltraCanvasVersion.cmake - never a literal of its own.
target_compile_definitions(ultrastoreop PRIVATE
    ULTRASTOREOPERATOR_VERSION="${ULTRASTOREOPERATOR_VERSION}"
    UCAPP_ICON_PATH="media/appicon/UltraStoreOperator.png")
ultracanvas_embed_app_icon(ultrastoreop "${CMAKE_SOURCE_DIR}/media/appicon/UltraStoreOperator.png")
# A GUI program on Windows with ultrastoreop.com beside it, so a prompt
# waits for `ultrastoreop publish` and sees its exit code.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/UltraCanvasWindowsGuiApp.cmake")
ultracanvas_windows_console_companion(ultrastoreop)
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS ultrastoreop RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraStoreOperator.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraStoreOperator.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraStoreOperator.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()
message(STATUS "  [✓] ultrastoreop (window and command line) - ENABLED")
