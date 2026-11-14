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
# UCR_FONTS=subset (the default) preloads Manrope and Inter alone, cut to
# Latin, Greek and Cyrillic with Inter's optical size pinned at 14 - 332 KB
# instead of the 4.6 MB of media/fonts - which needs fonttools (pyftsubset,
# `pipx install fonttools`); without it the two fonts go in whole, with a
# warning. UCR_FONTS=all preloads every bundled font, as before.
#
# Version: 0.2.0 - the fonts subset (UCR_FONTS); UltraLocalisation and UltraCrypt whole in the main module, for the apps
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
    -Wl,--export-if-defined=__gxx_wasm_personality_v0)
# The UI-free modules an app may call whole as well, not only what the
# library's own code pulls from their archives: the translation catalogues
# (UltraLocalisation::Load, an app that fetches its catalogues) and
# UltraCrypt with libsodium (the store's signature check). A module missing
# here fails an app's load with the symbol named.
foreach(_ucr_module uc-localisation uc-crypt)
    if(TARGET ${_ucr_module})
        target_link_options(ultracanvas-runtime PRIVATE
            "SHELL:-Wl,--whole-archive $<TARGET_FILE:${_ucr_module}> -Wl,--no-whole-archive")
        target_link_libraries(ultracanvas-runtime PRIVATE ${_ucr_module})
    endif()
endforeach()

# The fonts in the runtime's .data (UCR_FONTS above).
set(UCR_FONTS "subset" CACHE STRING "The fonts the runtime carries: subset (Manrope and Inter, Latin, Greek and Cyrillic) or all (every file of media/fonts)")
if(UCR_FONTS STREQUAL "all")
    target_link_options(ultracanvas-runtime PRIVATE
        "SHELL:--preload-file ${UCR_REPO_ROOT}/media/fonts@/share/media/fonts")
else()
    set(_ucr_fonts_dir ${CMAKE_CURRENT_BINARY_DIR}/fonts)
    set(_ucr_inter ${UCR_REPO_ROOT}/media/fonts/Inter-Variable.ttf)
    set(_ucr_manrope ${UCR_REPO_ROOT}/media/fonts/Manrope-Variable.ttf)
    # The characters of the catalog's languages: Latin with its extensions
    # (the EU's, Turkish, Vietnamese's marks come from the combining block),
    # Greek, Cyrillic (Bulgarian, Ukrainian, Russian, Serbian), the common
    # punctuation and symbols a store shows. Arabic, Hebrew and the CJK
    # scripts are not in these fonts at all.
    set(_ucr_unicodes "U+0020-007E,U+00A0-017F,U+0180-024F,U+02C6-02DC,U+0300-036F,U+0370-03FF,U+0400-045F,U+0490-0491,U+1E9E,U+2000-203A,U+2044,U+20AC,U+2122,U+2190-2199,U+2212,U+2215,U+FEFF,U+FFFD")
    set(_ucr_features "kern,liga,calt,ccmp,locl,mark,mkmk,case,frac,tnum")
    find_program(UCR_PYFTSUBSET pyftsubset)
    find_program(UCR_FONTTOOLS fonttools)
    if(UCR_PYFTSUBSET AND UCR_FONTTOOLS)
        # Inter's optical-size axis pinned at the text size (its weights stay
        # variable), which halves the file; Manrope has the weight axis only.
        add_custom_command(OUTPUT ${_ucr_fonts_dir}/Inter-Variable.ttf
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_ucr_fonts_dir}
            COMMAND ${UCR_FONTTOOLS} varLib.instancer -q -o ${CMAKE_CURRENT_BINARY_DIR}/Inter-opsz14.ttf ${_ucr_inter} opsz=14
            COMMAND ${UCR_PYFTSUBSET} ${CMAKE_CURRENT_BINARY_DIR}/Inter-opsz14.ttf --unicodes=${_ucr_unicodes}
                --layout-features=${_ucr_features} --output-file=${_ucr_fonts_dir}/Inter-Variable.ttf
            DEPENDS ${_ucr_inter}
            COMMENT "UltraCanvas runtime: Inter subset")
        add_custom_command(OUTPUT ${_ucr_fonts_dir}/Manrope-Variable.ttf
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_ucr_fonts_dir}
            COMMAND ${UCR_PYFTSUBSET} ${_ucr_manrope} --unicodes=${_ucr_unicodes}
                --layout-features=${_ucr_features} --output-file=${_ucr_fonts_dir}/Manrope-Variable.ttf
            DEPENDS ${_ucr_manrope}
            COMMENT "UltraCanvas runtime: Manrope subset")
    else()
        message(WARNING "UltraCanvas runtime: no pyftsubset / fonttools on PATH (pipx install fonttools): Manrope and Inter go into the runtime whole")
        add_custom_command(OUTPUT ${_ucr_fonts_dir}/Inter-Variable.ttf ${_ucr_fonts_dir}/Manrope-Variable.ttf
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_ucr_fonts_dir}
            COMMAND ${CMAKE_COMMAND} -E copy ${_ucr_inter} ${_ucr_manrope} ${_ucr_fonts_dir}/
            DEPENDS ${_ucr_inter} ${_ucr_manrope})
    endif()
    add_custom_target(ultracanvas-runtime-fonts DEPENDS ${_ucr_fonts_dir}/Inter-Variable.ttf ${_ucr_fonts_dir}/Manrope-Variable.ttf)
    add_dependencies(ultracanvas-runtime ultracanvas-runtime-fonts)
    set_property(TARGET ultracanvas-runtime APPEND PROPERTY LINK_DEPENDS ${_ucr_fonts_dir}/Inter-Variable.ttf ${_ucr_fonts_dir}/Manrope-Variable.ttf)
    target_link_options(ultracanvas-runtime PRIVATE
        "SHELL:--preload-file ${_ucr_fonts_dir}@/share/media/fonts")
endif()

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
