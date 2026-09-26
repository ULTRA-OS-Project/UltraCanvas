# Tests/VectorFormatsTests.cmake
# VectorFormatsPluginTest (every vector converter, and detailed.cdr through
# the patched libcdr) and CDRWriterTest. Included by Tests/CMakeLists.txt
# under BUILD_TESTS, and by the top-level CMakeLists.txt on its own under
# ULTRACANVAS_BUILD_VECTOR_FORMAT_TESTS - so platforms that build no full
# test suite (Windows CI) still run the CorelDRAW path on real files.
# Paths are relative to this file, so either includer works.

set(_VFT_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_VFT_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../UltraCanvas/include")

# ===== VECTOR FORMATS PLUGIN TEST =====
# The whole converter matrix through the graphics plugin registry: saving
# all ten formats via SaveGraphicsFile, loading the formats with readers
# via LoadGraphicsFile, round-trip structure checks, and the supported-
# format inventory's per-extension load/save capabilities.
if(TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building VectorFormatsPluginTest...")
    add_executable(VectorFormatsPluginTest
        ${_VFT_DIR}/VectorFormatsPluginTest.cpp
    )
    target_include_directories(VectorFormatsPluginTest PRIVATE ${_VFT_INCLUDE_DIR})
    target_compile_features(VectorFormatsPluginTest PRIVATE cxx_std_20)
    target_compile_definitions(VectorFormatsPluginTest PRIVATE
        VECTOR_SAMPLES_DIR="${_VFT_DIR}/../media/vector"
    )
    target_link_libraries(VectorFormatsPluginTest PRIVATE UltraCanvasVectorPlugin UltraCanvas)
    set_target_properties(VectorFormatsPluginTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME VectorFormatsPluginTest COMMAND VectorFormatsPluginTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: VectorFormatsPluginTest")
else()
    message(STATUS "  VectorFormatsPluginTest skipped (UltraCanvasVectorPlugin target not present)")
endif()

# Round-trips a VectorDocument through the Vector plugin's CDR writer and the
# CDR plugin (libcdr): the file must parse, carry the right page, and render
# the objects at their places. Needs both plugins.
if(TARGET UltraCanvasCDRPlugin AND TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building CDRWriterTest...")
    add_executable(CDRWriterTest
        ${_VFT_DIR}/CDRWriterTest.cpp
    )
    target_include_directories(CDRWriterTest PRIVATE ${_VFT_INCLUDE_DIR})
    target_compile_features(CDRWriterTest PRIVATE cxx_std_20)
    target_link_libraries(CDRWriterTest PRIVATE
        UltraCanvasVectorPlugin UltraCanvasCDRPlugin UltraCanvas)
    # Exported symbols, so the crash backtrace this test prints can name the
    # frames in our own painter and not just the ones in libcdr's shared
    # object. Costs nothing but a slightly larger dynamic symbol table.
    set_target_properties(CDRWriterTest PROPERTIES ENABLE_EXPORTS ON)
    if(NOT WIN32)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(CDRWRITER_CAIRO cairo)
            if(CDRWRITER_CAIRO_FOUND)
                target_compile_definitions(CDRWriterTest PRIVATE CDRWRITER_HAVE_CAIRO=1)
                target_include_directories(CDRWriterTest PRIVATE ${CDRWRITER_CAIRO_INCLUDE_DIRS})
                target_link_libraries(CDRWriterTest PRIVATE ${CDRWRITER_CAIRO_LIBRARIES})
            endif()
        endif()
    endif()
    set_target_properties(CDRWriterTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME CDRWriterTest COMMAND CDRWriterTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: CDRWriterTest")
else()
    message(STATUS "  CDRWriterTest skipped (needs UltraCanvasCDRPlugin and UltraCanvasVectorPlugin)")
endif()
