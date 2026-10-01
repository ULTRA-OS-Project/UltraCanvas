# Tests/PackageConsumer/CMakeLists.txt
# The smallest application that builds against an INSTALLED UltraCanvas - the
# out-of-tree route in Docs/GettingStarted.md, step 4.
#
# It is deliberately not part of the framework's own build: it is a separate
# project, configured with CMAKE_PREFIX_PATH pointing at a `cmake --install`
# prefix, and everything it knows about the framework comes from
# find_package(UltraCanvas). No include directories, no pkg-config, no plugin
# lists, no add_subdirectory - if one of those turns out to be needed here, the
# package is incomplete and that is the bug. CI runs this on Linux:
#
#     cmake --install build --prefix "$PWD/install-prefix"
#     cmake -S Tests/PackageConsumer -B build-package-consumer \
#           -DCMAKE_PREFIX_PATH="$PWD/install-prefix"
#     cmake --build build-package-consumer
#     ./build-package-consumer/PackageConsumer
#
# Version: 1.0.0
# Last Modified: 2026-10-01
# Author: UltraCanvas Framework

cmake_minimum_required(VERSION 3.16)
project(UltraCanvasPackageConsumer LANGUAGES CXX)

find_package(UltraCanvas CONFIG REQUIRED)
message(STATUS "UltraCanvas ${ULTRACANVAS_VERSION} from ${UltraCanvas_DIR}")

message(STATUS "Plug-ins in the package: ${ULTRACANVAS_PLUGIN_TARGETS}")

add_executable(PackageConsumer main.cpp)
target_link_libraries(PackageConsumer PRIVATE UltraCanvas::UltraCanvas)
# The format plug-ins and the registrar that announces them to FileLoader -
# the two lines an application adds when it opens files.
target_link_libraries(PackageConsumer PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
if(TARGET UltraCanvas::UltraCanvasAllFormats)
    target_link_libraries(PackageConsumer PRIVATE UltraCanvas::UltraCanvasAllFormats)
    target_compile_definitions(PackageConsumer PRIVATE PACKAGE_CONSUMER_HAS_ALL_FORMATS=1)
endif()
