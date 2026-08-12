# OS/WASM/demo — minimal browser validation app for the WASM backend.
#
#   source /path/to/emsdk/emsdk_env.sh
#   export EM_PKG_CONFIG_PATH=/path/to/wasm-sysroot/lib/pkgconfig
#   emcmake cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   python3 serve.py 8080   # from build/, then open wasm-demo.html
cmake_minimum_required(VERSION 3.16)
project(UltraCanvasWasmDemo C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# <repo>/UltraCanvas (library) and <repo> (bundled media/fonts)
set(UC_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(UC_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

add_subdirectory(${UC_LIB_DIR} UltraCanvas)

add_executable(wasm-demo wasm-demo.cpp)
target_link_libraries(wasm-demo PRIVATE UltraCanvas)
set_target_properties(wasm-demo PROPERTIES SUFFIX ".html")
target_link_options(wasm-demo PRIVATE
    # GetResourcesDir() resolves to ./share/ (cwd is / in MEMFS), so the
    # bundled fonts must be preloaded at /share/media/fonts.
    "SHELL:--preload-file ${UC_REPO_ROOT}/media/fonts@/share/media/fonts"
)
