# Tests/VectorFormatsTests.cmake
# VectorFormatsPluginTest (every vector converter, and detailed.cdr through
# the patched libcdr), ODGConverterTest, VectorFillRuleTest, EMFPlusTest, CDRWriterTest and
# CDRPluginRenderTest. Included by Tests/CMakeLists.txt
# under BUILD_TESTS, and by the top-level CMakeLists.txt on its own under
# ULTRACANVAS_BUILD_VECTOR_FORMAT_TESTS - so platforms that build no full
# test suite (Windows CI) still run the CorelDRAW path on real files.
# Paths are relative to this file, so either includer works.

set(_VFT_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_VFT_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../UltraCanvas/include")
# Where the test executables go. On Windows beside the core DLL
# (libUltraCanvas.dll, at the build root like the apps): an executable finds
# its DLLs in its own directory, and build/bin is on no search path - the
# tests failed to start there with STATUS_DLL_NOT_FOUND (0xc0000135).
if(WIN32)
    set(_VFT_BIN_DIR "$<TARGET_FILE_DIR:UltraCanvas>")
else()
    set(_VFT_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
endif()

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
        RUNTIME_OUTPUT_DIRECTORY ${_VFT_BIN_DIR}
    )
    add_test(NAME VectorFormatsPluginTest COMMAND VectorFormatsPluginTest
             WORKING_DIRECTORY ${_VFT_BIN_DIR})
    message(STATUS "    Test registered: VectorFormatsPluginTest")
else()
    message(STATUS "  VectorFormatsPluginTest skipped (UltraCanvasVectorPlugin target not present)")
endif()

# VectorExportFixtures: not a test on its own - the drawings, and the
# manifest of what must be where, that scripts/check_libreoffice_render.py
# has LibreOffice draw (.github/workflows/vector-libreoffice.yml).
if(TARGET UltraCanvasVectorPlugin)
    add_executable(VectorExportFixtures
        ${_VFT_DIR}/VectorExportFixtures.cpp
    )
    target_include_directories(VectorExportFixtures PRIVATE ${_VFT_INCLUDE_DIR})
    target_compile_features(VectorExportFixtures PRIVATE cxx_std_20)
    target_link_libraries(VectorExportFixtures PRIVATE UltraCanvasVectorPlugin UltraCanvas)
    set_target_properties(VectorExportFixtures PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_VFT_BIN_DIR}
    )
endif()

# ===== OPENDOCUMENT DRAWING =====
# ODGConverterTest: the .odg / .fodg writer and reader round trip, LibreOffice
# Draw's own sample files (media/vector/ODG) and hand-written custom-shape
# geometry. Run with --render <file> <out.ppm> to draw any vector file.
if(TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building ODGConverterTest...")
    add_executable(ODGConverterTest
        ${_VFT_DIR}/ODGConverterTest.cpp
    )
    target_include_directories(ODGConverterTest PRIVATE ${_VFT_INCLUDE_DIR})
    target_compile_features(ODGConverterTest PRIVATE cxx_std_20)
    target_compile_definitions(ODGConverterTest PRIVATE
        VECTOR_SAMPLES_DIR="${_VFT_DIR}/../media/vector"
    )
    target_link_libraries(ODGConverterTest PRIVATE UltraCanvasVectorPlugin UltraCanvas)
    set_target_properties(ODGConverterTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_VFT_BIN_DIR}
    )
    add_test(NAME ODGConverterTest COMMAND ODGConverterTest
             WORKING_DIRECTORY ${_VFT_BIN_DIR})
    message(STATUS "    Test registered: ODGConverterTest")
endif()

# ===== FILL RULE =====
# VectorFillRuleTest: a shape's even-odd / non-zero fill through every writer
# with a reader (EMF, WMF, DXF, XAR, CDR, ODG, SVG), drawn after the round
# trip, and the non-zero-as-even-odd subpath helper.
if(TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building VectorFillRuleTest...")
    add_executable(VectorFillRuleTest
        ${_VFT_DIR}/VectorFillRuleTest.cpp
    )
    target_include_directories(VectorFillRuleTest PRIVATE ${_VFT_INCLUDE_DIR})
    target_compile_features(VectorFillRuleTest PRIVATE cxx_std_20)
    target_link_libraries(VectorFillRuleTest PRIVATE UltraCanvasVectorPlugin UltraCanvas)
    set_target_properties(VectorFillRuleTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_VFT_BIN_DIR}
    )
    add_test(NAME VectorFillRuleTest COMMAND VectorFillRuleTest
             WORKING_DIRECTORY ${_VFT_BIN_DIR})
    message(STATUS "    Test registered: VectorFillRuleTest")
endif()

# EMFPlusTest: EMF+ records in the EMF reader (alpha kept, the GDI copy of a
# dual file not drawn twice, a fallback to it for what is not read), and the
# EMF a WMF carries.
if(TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building EMFPlusTest...")
    add_executable(EMFPlusTest
        ${_VFT_DIR}/EMFPlusTest.cpp
    )
    target_include_directories(EMFPlusTest PRIVATE ${_VFT_INCLUDE_DIR})
    target_compile_features(EMFPlusTest PRIVATE cxx_std_20)
    target_link_libraries(EMFPlusTest PRIVATE UltraCanvasVectorPlugin UltraCanvas)
    set_target_properties(EMFPlusTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_VFT_BIN_DIR}
    )
    add_test(NAME EMFPlusTest COMMAND EMFPlusTest
             WORKING_DIRECTORY ${_VFT_BIN_DIR})
    message(STATUS "    Test registered: EMFPlusTest")
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
        RUNTIME_OUTPUT_DIRECTORY ${_VFT_BIN_DIR}
    )
    add_test(NAME CDRWriterTest COMMAND CDRWriterTest
             WORKING_DIRECTORY ${_VFT_BIN_DIR})
    message(STATUS "    Test registered: CDRWriterTest")
else()
    message(STATUS "  CDRWriterTest skipped (needs UltraCanvasCDRPlugin and UltraCanvasVectorPlugin)")
endif()

# Draws every page of media/vector/CDR through the CDR plugin after its parse,
# as the DemoApp does: the recorded draw commands must not reach back into the
# painter that recorded them, which is gone by then (detailed.cdr's gradient
# fills crashed the DemoApp's CDR page that way).
if(TARGET UltraCanvasCDRPlugin)
    message(STATUS "  Building CDRPluginRenderTest...")
    add_executable(CDRPluginRenderTest
        ${_VFT_DIR}/CDRPluginRenderTest.cpp
    )
    target_include_directories(CDRPluginRenderTest PRIVATE ${_VFT_INCLUDE_DIR})
    target_compile_features(CDRPluginRenderTest PRIVATE cxx_std_20)
    target_compile_definitions(CDRPluginRenderTest PRIVATE
        CDRRENDER_SAMPLES_DIR="${_VFT_DIR}/../media/vector/CDR"
    )
    target_link_libraries(CDRPluginRenderTest PRIVATE UltraCanvasCDRPlugin UltraCanvas)
    # Exported symbols, so the crash backtrace names our painter's frames.
    set_target_properties(CDRPluginRenderTest PROPERTIES ENABLE_EXPORTS ON)
    if(NOT WIN32)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(CDRRENDER_CAIRO cairo)
            if(CDRRENDER_CAIRO_FOUND)
                target_compile_definitions(CDRPluginRenderTest PRIVATE CDRRENDER_HAVE_CAIRO=1)
                target_include_directories(CDRPluginRenderTest PRIVATE ${CDRRENDER_CAIRO_INCLUDE_DIRS})
                target_link_libraries(CDRPluginRenderTest PRIVATE ${CDRRENDER_CAIRO_LIBRARIES})
            endif()
        endif()
    endif()
    set_target_properties(CDRPluginRenderTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_VFT_BIN_DIR}
    )
    add_test(NAME CDRPluginRenderTest COMMAND CDRPluginRenderTest
             WORKING_DIRECTORY ${_VFT_BIN_DIR})
    message(STATUS "    Test registered: CDRPluginRenderTest")
else()
    message(STATUS "  CDRPluginRenderTest skipped (needs UltraCanvasCDRPlugin)")
endif()

# VectorFileSizeTest: a drawing written in each format the Vector plugin
# writes and reads, rasterized from the file - its page is the natural size
# (InspectVectorFile, GetFileInfo), and RasterizeVectorFile scales it.
if(TARGET UltraCanvasVectorPlugin)
    message(STATUS "  Building VectorFileSizeTest...")
    add_executable(VectorFileSizeTest
        ${_VFT_DIR}/VectorFileSizeTest.cpp
    )
    target_include_directories(VectorFileSizeTest PRIVATE ${_VFT_INCLUDE_DIR})
    target_compile_features(VectorFileSizeTest PRIVATE cxx_std_20)
    target_link_libraries(VectorFileSizeTest PRIVATE UltraCanvasVectorPlugin UltraCanvas)
    set_target_properties(VectorFileSizeTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_VFT_BIN_DIR}
    )
    add_test(NAME VectorFileSizeTest COMMAND VectorFileSizeTest
             WORKING_DIRECTORY ${_VFT_BIN_DIR})
    message(STATUS "    Test registered: VectorFileSizeTest")
endif()
