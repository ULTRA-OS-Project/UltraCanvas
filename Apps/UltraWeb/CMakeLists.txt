# Apps/UltraWeb/CMakeLists.txt
# UltraWeb - the ULTRA OS browser. This version runs WebAssembly apps that
# build their UI from UltraCanvas elements (Docs/UltraWeb/UltraWeb.md); the
# HTML reader and JavaScript follow Docs/UltraWeb/UltraWebProposal.md.
#
# Needs the UltraCanvas UI library, the WasmHost module (built everywhere;
# without wasmtime it reports that it cannot run apps) and UltraNet, which
# fetches apps from https:// addresses.
#
# Version: 0.2.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraWeb app - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()
if(NOT TARGET WasmHost)
    message(STATUS "  [−] UltraWeb app - SKIPPED (WasmHost module not built)")
    return()
endif()
if(NOT TARGET UltraNet)
    message(STATUS "  [−] UltraWeb app - SKIPPED (UltraNet not built: ULTRACANVAS_ENABLE_NET=OFF)")
    return()
endif()

add_executable(UltraWeb
    main.cpp
    host/UltraWebFetch.cpp
    host/UltraWebGuest.cpp
    host/UltraWebLoader.cpp
    host/UltraWebStorage.cpp
    ui/UltraWebWindow.cpp
)

# Export dynamic symbols so on-demand plugin modules resolve core symbols
# when the core is linked statically, as the other applications do.
set_target_properties(UltraWeb PROPERTIES ENABLE_EXPORTS ON)

# UltraWeb/guest/ultraweb.h is the element ABI both sides include.
target_include_directories(UltraWeb PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/UltraWeb/guest)

# UltraNet as UltraMail links it: the archive with a static core; with a
# shared one it is folded into libultracanvas and must not be linked twice.
set(_uw_core_shared FALSE)
get_target_property(_uw_core_type ${ULTRACANVAS_LIBRARY} TYPE)
if(_uw_core_type STREQUAL "SHARED_LIBRARY")
    set(_uw_core_shared TRUE)
endif()
if(_uw_core_shared)
    target_include_directories(UltraWeb PRIVATE $<TARGET_PROPERTY:UltraNet,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(UltraWeb PRIVATE WasmHost ${ULTRACANVAS_LIBRARY})
else()
    # Modules before the core on the link line: GNU ld resolves left to right.
    target_link_libraries(UltraWeb PRIVATE WasmHost UltraNet ${ULTRACANVAS_LIBRARY})
endif()
find_package(Threads REQUIRED)
target_link_libraries(UltraWeb PRIVATE Threads::Threads)
target_compile_features(UltraWeb PRIVATE cxx_std_20)

# UltraWeb versions itself, independently of the framework: the number comes
# from the first line of Docs/UltraWeb/CHANGELOG.md through
# cmake/UltraCanvasVersion.cmake - never a literal of its own.
target_compile_definitions(UltraWeb PRIVATE
    ULTRAWEB_VERSION="${ULTRAWEB_VERSION}"
    $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

# Embed the application manifest + version info (Windows only).
ultracanvas_embed_windows_manifest(UltraWeb)

# One icon, everywhere the app is drawn. main.cpp names this same file for the
# window and taskbar icon; UCAPP_ICON_PATH is the fallback the core applies
# when an app sets no icon of its own. The PNG is the 256 px render of
# media/appicon/UltraWeb.svg.
target_compile_definitions(UltraWeb PRIVATE
    UCAPP_ICON_PATH="media/appicon/UltraWeb.png")
ultracanvas_embed_app_icon(UltraWeb
    "${CMAKE_SOURCE_DIR}/media/appicon/UltraWeb.png")

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS UltraWeb RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraWeb.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraWeb.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraWeb.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [✓] UltraWeb app - ENABLED (target: UltraWeb)")
message(STATUS "      Version ${ULTRAWEB_VERSION} (Docs/UltraWeb/CHANGELOG.md)")
