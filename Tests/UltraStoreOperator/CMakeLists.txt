# Tests/UltraStoreOperator/CMakeLists.txt
# Builds the UltraStoreOperatorCoreTests binary. Enabled by
# ULTRACANVAS_BUILD_ULTRASTOREOPERATOR_TESTS.

cmake_minimum_required(VERSION 3.16)

add_executable(UltraStoreOperatorCoreTests
    test_main.cpp
    test_operator.cpp
)
target_compile_features(UltraStoreOperatorCoreTests PRIVATE cxx_std_20)
target_include_directories(UltraStoreOperatorCoreTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../UltraStore
    ${ULTRACANVAS_INCLUDE_DIRS})
target_link_libraries(UltraStoreOperatorCoreTests PRIVATE UltraStoreOperatorCore)

include(CTest)
add_test(NAME UltraStoreOperatorCore COMMAND UltraStoreOperatorCoreTests)
