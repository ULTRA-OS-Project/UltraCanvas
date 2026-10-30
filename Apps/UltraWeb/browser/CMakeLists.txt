# Apps/UltraWeb/browser/CMakeLists.txt
# The UltraWeb runtime for ordinary browsers (Docs/UltraWeb/UltraWebInTheBrowser.md):
# UltraCanvas compiled with Emscripten, running UltraWeb apps - the same
# app.wasm the UltraWeb browser runs - on the browser's WebAssembly engine.
# Emscripten only. The build leaves three things in its binary directory:
#
#   ultraweb-runtime.{js,wasm,data}   the runtime
#   ultraweb-extension/               the browser extension that installs it
#                                     once (load it unpacked, or zip it)
#   site/                             what a site serves: the loader, a page,
#                                     the sample app hello.wasm, and the
#                                     runtime under runtime/ for visitors
#                                     without the extension
#
# Built from the repository root under emcmake (BUILD_ULTRAWEB_RUNTIME), or
# standalone from this directory, as UltraCanvas/OS/WASM/demo is:
#
#   source /path/to/emsdk/emsdk_env.sh
#   export PKG_CONFIG_PATH=/path/to/wasm-sysroot/lib/pkgconfig:/path/to/wasm-sysroot/share/pkgconfig
#   export PKG_CONFIG_LIBDIR=$PKG_CONFIG_PATH
#   emcmake cmake -S Apps/UltraWeb/browser -B build-uwr -DCMAKE_BUILD_TYPE=Release \
#       -DULTRACANVAS_WASM_THREADS=OFF
#   cmake --build build-uwr
#
# Single-threaded (ULTRACANVAS_WASM_THREADS=OFF, against a WASM_THREADS=0
# sysroot) is the build to ship: a page cannot start Web Workers from an
# extension's address, and a site without cross-origin isolation headers
# cannot run a threaded one. A threaded build compiles, for the CI's sake.
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS
cmake_minimum_required(VERSION 3.16)

set(UW_RUNTIME_STANDALONE OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(UW_RUNTIME_STANDALONE ON)
    project(UltraWebRuntime C CXX)
endif()

set(UW_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
if(UW_RUNTIME_STANDALONE)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    include(${UW_REPO_ROOT}/cmake/UltraCanvasVersion.cmake)
    add_subdirectory(${UW_REPO_ROOT}/UltraCanvas UltraCanvas)
endif()

if(NOT EMSCRIPTEN)
    message(STATUS "  [−] UltraWeb runtime - SKIPPED (browser builds only, under emcmake)")
    return()
endif()

add_executable(ultraweb-runtime
    UltraWebRuntime.cpp
    ../host/UltraWebFetch.cpp
    ../host/UltraWebGuest.cpp
    ../host/UltraWebStorage.cpp
)
# In this directory, whatever the including project sets for programs (the
# repository root puts them all in its build root): the steps below copy
# the runtime from here.
set_target_properties(ultraweb-runtime PROPERTIES SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
target_compile_features(ultraweb-runtime PRIVATE cxx_std_20)
# UltraWeb/guest/ultraweb.h is the element ABI; OS/WASM/UltraCanvasWASMSupport.h
# (IndexedDB) is reached through the library's own directory.
target_include_directories(ultraweb-runtime PRIVATE
    ${UW_REPO_ROOT}/UltraWeb/guest
    ${UW_REPO_ROOT}/UltraCanvas)
target_link_libraries(ultraweb-runtime PRIVATE WasmHost UltraCanvas)
# IndexedDB for the apps' storage, and the bundled fonts at
# /share/media/fonts, where GetResourcesDir() looks in the browser.
target_link_options(ultraweb-runtime PRIVATE
    -lidbfs.js
    "SHELL:--preload-file ${UW_REPO_ROOT}/media/fonts@/share/media/fonts")

# The extension's manifest carries UltraWeb's version, from its changelog.
configure_file(extension/manifest.json.in ${CMAKE_CURRENT_BINARY_DIR}/manifest.json @ONLY)

set(_uw_ext ${CMAKE_CURRENT_BINARY_DIR}/ultraweb-extension)
set(_uw_site ${CMAKE_CURRENT_BINARY_DIR}/site)
set(_uw_runtime_files
    ${CMAKE_CURRENT_BINARY_DIR}/ultraweb-runtime.js
    ${CMAKE_CURRENT_BINARY_DIR}/ultraweb-runtime.wasm
    ${CMAKE_CURRENT_BINARY_DIR}/ultraweb-runtime.data)
add_custom_command(TARGET ultraweb-runtime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_uw_ext}/runtime ${_uw_site}/runtime
    COMMAND ${CMAKE_COMMAND} -E copy ${_uw_runtime_files} ${_uw_ext}/runtime/
    COMMAND ${CMAKE_COMMAND} -E copy ${_uw_runtime_files} ${_uw_site}/runtime/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/manifest.json ${_uw_ext}/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/extension/ultraweb-announce.js ${_uw_ext}/
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/site/ultraweb-loader.js
        ${CMAKE_CURRENT_SOURCE_DIR}/site/index.html
        ${CMAKE_CURRENT_SOURCE_DIR}/site/extension-only.html
        ${CMAKE_CURRENT_SOURCE_DIR}/site/hello.txt
        ${_uw_site}/
    COMMENT "Assembling the UltraWeb extension and the sample site")

# The sample app and the test guests: freestanding C, built by the clang
# inside emsdk with the plain WebAssembly target (no Emscripten, no libc).
set(_uw_clang "${EMSCRIPTEN_ROOT_PATH}/../bin/clang")
if(EXISTS "${_uw_clang}")
    add_custom_command(OUTPUT ${_uw_site}/hello.wasm
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_uw_site}
        COMMAND "${_uw_clang}" --target=wasm32 -O2 -ffreestanding -fno-builtin -nostdlib -Wl,--no-entry
                -I ${UW_REPO_ROOT}/UltraWeb/guest ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.c
                -o ${_uw_site}/hello.wasm
        DEPENDS examples/hello.c ${UW_REPO_ROOT}/UltraWeb/guest/ultraweb.h
        COMMENT "Building the sample UltraWeb app hello.wasm")
    add_custom_target(ultraweb-hello ALL DEPENDS ${_uw_site}/hello.wasm)

    # The browser engine of UltraCanvasWasmHost on its own, under Node:
    # node wasmhost-browser-test.js (test/wasmhost-browser-test.cpp).
    set(_uw_guest ${CMAKE_CURRENT_SOURCE_DIR}/test/wasmhost-guest.c)
    set(_uw_guest_flags --target=wasm32 -O2 -ffreestanding -fno-builtin -nostdlib -Wl,--no-entry)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wasmhost-guest.wasm ${CMAKE_CURRENT_BINARY_DIR}/wasmhost-guest-unknown.wasm
        COMMAND "${_uw_clang}" ${_uw_guest_flags} ${_uw_guest} -o ${CMAKE_CURRENT_BINARY_DIR}/wasmhost-guest.wasm
        COMMAND "${_uw_clang}" ${_uw_guest_flags} -DUNKNOWN_IMPORT ${_uw_guest} -o ${CMAKE_CURRENT_BINARY_DIR}/wasmhost-guest-unknown.wasm
        DEPENDS test/wasmhost-guest.c
        COMMENT "Building the WasmHost test guests")
    add_custom_target(wasmhost-browser-guests DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/wasmhost-guest.wasm)
    add_executable(wasmhost-browser-test test/wasmhost-browser-test.cpp)
    set_target_properties(wasmhost-browser-test PROPERTIES SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_features(wasmhost-browser-test PRIVATE cxx_std_20)
    target_link_libraries(wasmhost-browser-test PRIVATE WasmHost)
    add_dependencies(wasmhost-browser-test wasmhost-browser-guests)
    target_link_options(wasmhost-browser-test PRIVATE
        -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToNewUTF8,HEAPU8
        "SHELL:--embed-file ${CMAKE_CURRENT_BINARY_DIR}/wasmhost-guest.wasm@/wasmhost-guest.wasm"
        "SHELL:--embed-file ${CMAKE_CURRENT_BINARY_DIR}/wasmhost-guest-unknown.wasm@/wasmhost-guest-unknown.wasm")
else()
    message(STATUS "      no clang beside emcc (${_uw_clang}): the sample app hello.wasm is not built")
endif()

message(STATUS "  [✓] UltraWeb runtime - ENABLED (target: ultraweb-runtime, extension and site in ${CMAKE_CURRENT_BINARY_DIR})")
