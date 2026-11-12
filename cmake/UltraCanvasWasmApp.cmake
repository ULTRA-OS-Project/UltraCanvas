# cmake/UltraCanvasWasmApp.cmake
# ultracanvas_wasm_app(<target> <source>...)
#
# Builds an UltraCanvas application as a WebAssembly side module,
# <target>.wasm: the app's own code only, compiled against the UltraCanvas
# headers with the library's flags but not linked with it. It runs on the
# UltraCanvas runtime (UltraCanvas/OS/WASM/runtime), which a browser gets
# once from the UltraCanvas extension or from the site, and which calls the
# app's ordinary main() (Docs/UltraCanvas/UltraCanvasWasmRuntime.md).
#
# Needs an Emscripten build configured with ULTRACANVAS_WASM_DYNAMIC, so the
# app is compiled with the same flags as the runtime it will load into. An
# app built against one runtime version runs on that version: there is no
# stable binary interface across UltraCanvas releases yet.
#
# Author: UltraCanvas Framework / ULTRA OS

include_guard(GLOBAL)

function(ultracanvas_wasm_app target)
    if(NOT EMSCRIPTEN OR NOT ULTRACANVAS_WASM_DYNAMIC)
        message(FATAL_ERROR "ultracanvas_wasm_app(${target}): needs emcmake and ULTRACANVAS_WASM_DYNAMIC=ON")
    endif()
    add_executable(${target} ${ARGN})
    set_target_properties(${target} PROPERTIES
        SUFFIX ".wasm"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_features(${target} PRIVATE cxx_std_20)
    # The library's compile-side interface only: its link options make a
    # main module, and the app links nothing - every UltraCanvas symbol
    # comes from the runtime when the app is loaded.
    target_include_directories(${target} PRIVATE $<TARGET_PROPERTY:UltraCanvas,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(${target} PRIVATE $<TARGET_PROPERTY:UltraCanvas,INTERFACE_COMPILE_DEFINITIONS>)
    target_compile_options(${target} PRIVATE $<TARGET_PROPERTY:UltraCanvas,INTERFACE_COMPILE_OPTIONS> -fPIC)
    target_link_options(${target} PRIVATE -sSIDE_MODULE=1 -fwasm-exceptions)
endfunction()
