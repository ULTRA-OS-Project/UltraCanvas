# Apps/AnchorPoint/CMakeLists.txt
# AnchorPoint - peer-to-peer file sharing (no server file storage).
# Version: 0.1.0
#
# M1 ships the dependency-free transfer core + a headless CLI (anchorpoint).
# The UltraCanvas GUI target (M2) is added once its sources land; it links the
# UltraCanvas core the same way Apps/Texter does.
cmake_minimum_required(VERSION 3.16)

project(AnchorPoint
        VERSION 0.1.0
        DESCRIPTION "AnchorPoint - peer-to-peer file sharing"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- Transfer core (no external dependencies) -----------------------------
# Raw-socket backend today; swappable for an UltraNet backend later. Kept as a
# static lib so both the CLI and the future GUI link the exact same code.
add_library(anchorpoint_core STATIC
        net/RawSocketTransport.cpp
        net/Protocol.cpp)
target_include_directories(anchorpoint_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/net)
target_compile_features(anchorpoint_core PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(anchorpoint_core PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(anchorpoint_core PUBLIC Threads::Threads)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(anchorpoint_core PRIVATE -Wall -Wextra)
endif()

# ---- Headless CLI ---------------------------------------------------------
add_executable(anchorpoint cli/main.cpp)
target_link_libraries(anchorpoint PRIVATE anchorpoint_core)
set_target_properties(anchorpoint PROPERTIES OUTPUT_NAME "anchorpoint")

install(TARGETS anchorpoint RUNTIME DESTINATION bin)

# ---- GUI target (M2) ------------------------------------------------------
# Enabled once Apps/AnchorPoint/gui sources exist and UltraCanvas is available.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gui/AnchorPointApp.cpp")
    message(STATUS "AnchorPoint: GUI sources found - configure UltraCanvas link here (see Apps/Texter).")
endif()

message(STATUS "AnchorPoint ${PROJECT_VERSION}: building core + CLI (GUI: M2).")
