# Apps/UltraAIServerAdmin/CMakeLists.txt
# UltraAI ServerAdmin - the super administrator's tool for an UltraAI model
# server (Server/UltraAIModelServer): which models UltraAI clients may
# install, their files uploaded or hosted elsewhere, the topics and the
# models offered for each, the engines, the accounts and the audit log
# (Docs/UltraAIServerAdmin/README.md).
#
# Two targets:
#   UltraAIServerAdminCore  a static library: the admin API's client on
#                           UltraNet, UltraCrypt (the upload's hash) and
#                           UltraCanvasJSON. Opens no window, so the command
#                           line runs headless.
#   ultraai-serveradmin     the window when started plainly, the commands
#                           when given one.
#
# Version: 0.1.0
# Author: UltraAI Module

add_library(UltraAIServerAdminCore STATIC
    core/ModelServerClient.cpp
)
set_target_properties(UltraAIServerAdminCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultraaiserveradmin_core")
target_compile_features(UltraAIServerAdminCore PUBLIC cxx_std_20)
target_include_directories(UltraAIServerAdminCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${ULTRACANVAS_INCLUDE_DIRS})
target_link_libraries(UltraAIServerAdminCore PUBLIC ${ULTRACANVAS_LIBRARY} UltraNet UltraCrypt)

add_executable(ultraai-serveradmin
    ui/main.cpp
    ui/ServerAdminWindow.cpp
    cli/Commands.cpp
)
# One copy per user (UltraCanvasSingleInstance.h).
ultracanvas_single_instance(ultraai-serveradmin)
target_include_directories(ultraai-serveradmin PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/cli
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(ultraai-serveradmin PRIVATE UltraAIServerAdminCore ${ULTRACANVAS_LIBRARY})
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(ultraai-serveradmin PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
endif()
target_compile_features(ultraai-serveradmin PRIVATE cxx_std_20)
# The version comes from the first line of Docs/UltraAIServerAdmin/CHANGELOG.md
# through cmake/UltraCanvasVersion.cmake - never a literal of its own. The
# UltraAI icon: it is UltraAI's tool.
target_compile_definitions(ultraai-serveradmin PRIVATE
    ULTRAAISERVERADMIN_VERSION="${ULTRAAISERVERADMIN_VERSION}"
    UCAPP_ICON_PATH="media/appicon/UltraAI.png")
ultracanvas_embed_app_icon(ultraai-serveradmin "${CMAKE_SOURCE_DIR}/media/appicon/UltraAI.png")
# A GUI program on Windows with ultraai-serveradmin.com beside it, so a prompt
# waits for `ultraai-serveradmin publish` and sees its exit code.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/UltraCanvasWindowsGuiApp.cmake")
ultracanvas_windows_console_companion(ultraai-serveradmin)
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS ultraai-serveradmin RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraAIServerAdmin.desktop
            DESTINATION share/applications)
endif()
message(STATUS "  [✓] ultraai-serveradmin (window and command line) - ENABLED")
