# OS/WASM/demo — browser validation apps for the WASM backend.
#
#   wasm-demo          a window, pango-rendered labels and a button
#   ultraos-frontpage  the https://ultraos.eu front page out of UltraCanvas
#                      elements: navigation, hero, cards, status, newsletter
#
#   source /path/to/emsdk/emsdk_env.sh
#   export PKG_CONFIG_PATH=/path/to/wasm-sysroot/lib/pkgconfig:/path/to/wasm-sysroot/share/pkgconfig
#   export PKG_CONFIG_LIBDIR=$PKG_CONFIG_PATH   # see OS/WASM/README.md
#   emcmake cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   python3 serve.py 8080   # from build/, then open wasm-demo.html or
#                           # ultraos-frontpage.html
#
# The same sources build as desktop applications, to look at a page before
# the wasm pipeline runs: configure the repository root with
# -DBUILD_WASM_DEMOS=ON and run build/ultraos-frontpage. This file serves
# both: standalone under emcmake (it adds the library itself) and as a
# subdirectory of the root CMakeLists.txt (the library is already there).
cmake_minimum_required(VERSION 3.16)

set(UC_WASM_DEMO_STANDALONE OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(UC_WASM_DEMO_STANDALONE ON)
    project(UltraCanvasWasmDemo C CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# <repo>/UltraCanvas (library) and <repo> (bundled media)
set(UC_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(UC_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

if(UC_WASM_DEMO_STANDALONE)
    add_subdirectory(${UC_LIB_DIR} UltraCanvas)
endif()

# uc_wasm_demo(<target> <source> [preload dirs/files as src@dst ...])
# Under Emscripten the program is a .html page with the bundled fonts (and
# whatever else the page needs) preloaded into the virtual FS; natively it is
# an ordinary executable that finds the same files through GetResourcesDir().
function(uc_wasm_demo target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE UltraCanvas)
    if(EMSCRIPTEN)
        set_target_properties(${target} PROPERTIES SUFFIX ".html")
        # OS/WASM/UltraCanvasWASMSupport.h (the History API) is not on the
        # library's public include path; see UltraCanvasWebAssembly.md.
        target_include_directories(${target} PRIVATE ${UC_LIB_DIR})
        # GetResourcesDir() resolves to ./share/ (cwd is / in MEMFS), so the
        # bundled fonts must be preloaded at /share/media/fonts.
        target_link_options(${target} PRIVATE
            "SHELL:--preload-file ${UC_REPO_ROOT}/media/fonts@/share/media/fonts")
        foreach(preload IN LISTS ARGN)
            target_link_options(${target} PRIVATE
                "SHELL:--preload-file ${UC_REPO_ROOT}/${preload}")
        endforeach()
    endif()
endfunction()

uc_wasm_demo(wasm-demo wasm-demo.cpp)
uc_wasm_demo(ultraos-frontpage ultraos-frontpage.cpp
    "media/images/ultraos@/share/media/images/ultraos")
