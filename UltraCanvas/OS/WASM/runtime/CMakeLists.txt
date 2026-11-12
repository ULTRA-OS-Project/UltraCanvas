# OS/WASM/runtime/CMakeLists.txt
# The UltraCanvas runtime for browsers (Docs/UltraCanvas/UltraCanvasWasmRuntime.md):
# UltraCanvas linked once into an Emscripten main module that exports the
# whole C++ API, which UltraCanvas apps - side modules of their own code
# only - are loaded into. The build leaves in its binary directory:
#
#   ultracanvas-runtime.{js,wasm,data}   the runtime
#   ultracanvas-extension/               the browser extension that installs
#                                        it once (load it unpacked, or zip it)
#   site/                                what a site serves: the loader, two
#                                        pages, the sample app hello-app.wasm,
#                                        and the runtime under runtime/ for
#                                        visitors without the extension
#
# Emscripten only, single-threaded and position-independent: configure with
# ULTRACANVAS_WASM_THREADS=OFF and ULTRACANVAS_WASM_DYNAMIC=ON against a
# sysroot built with WASM_THREADS=0 WASM_PIC=1. From the repository root,
# or standalone from this directory:
#
#   emcmake cmake -S UltraCanvas/OS/WASM/runtime -B build-ucr -G Ninja \
#       -DCMAKE_BUILD_TYPE=Release -DULTRACANVAS_WASM_THREADS=OFF -DULTRACANVAS_WASM_DYNAMIC=ON
#   cmake --build build-ucr
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS
cmake_minimum_required(VERSION 3.16)

set(UCR_STANDALONE OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(UCR_STANDALONE ON)
    project(UltraCanvasWasmRuntime C CXX)
endif()

set(UCR_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)
if(UCR_STANDALONE)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "The UltraCanvas runtime is a browser build: configure it under emcmake")
    endif()
    set(ULTRACANVAS_WASM_THREADS OFF CACHE BOOL "WebAssembly: build with -pthread (Web Workers)" FORCE)
    set(ULTRACANVAS_WASM_DYNAMIC ON CACHE BOOL "WebAssembly: build -fPIC, and the UltraCanvas runtime" FORCE)
    # Every object in the main module position-independent, the library's too.
    add_compile_options(-fPIC -fwasm-exceptions)
    include(${UCR_REPO_ROOT}/cmake/UltraCanvasVersion.cmake)
    add_subdirectory(${UCR_REPO_ROOT}/UltraCanvas UltraCanvas)
endif()
include(${UCR_REPO_ROOT}/cmake/UltraCanvasWasmApp.cmake)

add_executable(ultracanvas-runtime UltraCanvasWasmRuntime.cpp)
set_target_properties(ultracanvas-runtime PROPERTIES SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(ultracanvas-runtime PRIVATE UltraCanvas)
# The whole library, not only what this small program calls: an app may
# use any of it. MAIN_MODULE=1 exports every symbol, the C and C++ standard
# libraries' included. The C++ exception personality is exported by name,
# since nothing in the main module refers to it by itself.
target_link_options(ultracanvas-runtime PRIVATE
    -sMAIN_MODULE=1
    "SHELL:-Wl,--whole-archive $<TARGET_FILE:UltraCanvas> -Wl,--no-whole-archive"
    -Wl,--export-if-defined=__gxx_wasm_personality_v0
    "SHELL:--preload-file ${UCR_REPO_ROOT}/media/fonts@/share/media/fonts")

# The sample app, a side module.
ultracanvas_wasm_app(hello-app examples/hello-app.cpp)

configure_file(extension/manifest.json.in ${CMAKE_CURRENT_BINARY_DIR}/manifest.json @ONLY)

set(_ucr_ext ${CMAKE_CURRENT_BINARY_DIR}/ultracanvas-extension)
set(_ucr_site ${CMAKE_CURRENT_BINARY_DIR}/site)
set(_ucr_runtime_files
    ${CMAKE_CURRENT_BINARY_DIR}/ultracanvas-runtime.js
    ${CMAKE_CURRENT_BINARY_DIR}/ultracanvas-runtime.wasm
    ${CMAKE_CURRENT_BINARY_DIR}/ultracanvas-runtime.data)
add_custom_target(ultracanvas-runtime-package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_ucr_ext}/runtime ${_ucr_site}/runtime
    COMMAND ${CMAKE_COMMAND} -E copy ${_ucr_runtime_files} ${_ucr_ext}/runtime/
    COMMAND ${CMAKE_COMMAND} -E copy ${_ucr_runtime_files} ${_ucr_site}/runtime/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/manifest.json ${_ucr_ext}/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/extension/ultracanvas-announce.js ${_ucr_ext}/
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/site/ultracanvas-loader.js
        ${CMAKE_CURRENT_SOURCE_DIR}/site/index.html
        ${CMAKE_CURRENT_SOURCE_DIR}/site/extension-only.html
        ${CMAKE_CURRENT_BINARY_DIR}/hello-app.wasm
        ${_ucr_site}/
    DEPENDS ultracanvas-runtime hello-app
    COMMENT "Assembling the UltraCanvas extension and the sample site")

message(STATUS "  [✓] UltraCanvas runtime - ENABLED (target: ultracanvas-runtime, extension and site in ${CMAKE_CURRENT_BINARY_DIR})")
