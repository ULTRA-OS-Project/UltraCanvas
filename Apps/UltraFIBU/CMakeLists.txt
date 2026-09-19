# Apps/UltraFIBU/CMakeLists.txt
# UltraFIBU - German double-entry accounting (Finanzbuchhaltung). Phase A1
# builds the headless engine (calendar, fiscal year, master data, store) and a
# command-line front end on top of it; phase A2 adds the documents, the journal,
# Storno, Festschreibung and the hash chain over the journal. The German UI
# follows on UltraCanvasListView with its sorting proxy.
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT TARGET UltraDatabase)
    message(STATUS "  [−] UltraFIBU engine - SKIPPED (UltraDatabase not built)")
    return()
endif()

set(ULTRAFIBU_ENGINE_SOURCES
    engine/UltraFIBUDate.cpp
    engine/UltraFIBUGeschaeftsjahr.cpp
    engine/UltraFIBUTypes.cpp
    engine/UltraFIBUUstIdNr.cpp
    engine/UltraFIBUUstIdNrOnline.cpp
    engine/UltraFIBUKontenrahmen.cpp
    engine/UltraFIBUBeleg.cpp
    engine/UltraFIBUBuchung.cpp
    engine/UltraFIBUDatev.cpp
    engine/UltraFIBUBank.cpp
    engine/UltraFIBUStore.cpp
)

add_library(UltraFIBUEngine STATIC ${ULTRAFIBU_ENGINE_SOURCES})
set_target_properties(UltraFIBUEngine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultrafibu_engine")
target_compile_features(UltraFIBUEngine PUBLIC cxx_std_20)
target_include_directories(UltraFIBUEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine)

# Persistence, the journal hash chain and password hashing. UltraCrypt is
# optional: without libsodium it refuses every operation, and the store then
# refuses to set a password instead of storing one badly.
target_link_libraries(UltraFIBUEngine PUBLIC UltraDatabase)
if(TARGET UltraCrypt)
    target_link_libraries(UltraFIBUEngine PUBLIC UltraCrypt)
endif()

# CAMT.053 is XML, and tinyxml2 is already a REQUIRED core dependency
# (UltraCanvas/CMakeLists.txt pkg_check_modules TINYXML2) - so the bank reader
# needs no new dependency, only the include path named explicitly rather than
# inherited. Explicitly, because the core library is what re-exports it and the
# engine is built to stay usable without a rendering stack.
if(TINYXML2_FOUND)
    target_include_directories(UltraFIBUEngine PRIVATE ${TINYXML2_INCLUDE_DIRS})
    target_link_libraries(UltraFIBUEngine PUBLIC ${TINYXML2_LIBRARIES})
    if(TINYXML2_LIBRARY_DIRS)
        target_link_directories(UltraFIBUEngine PUBLIC ${TINYXML2_LIBRARY_DIRS})
    endif()
endif()

# UltraCanvasJSON (the VIES / BZSt responses) lives in the core library, so it
# has to follow this archive on the link line - the same reasoning as
# UltraCloud/CMakeLists.txt. With a SHARED core the entry collapses to a no-op.
if(ULTRACANVAS_LIBRARY AND TARGET ${ULTRACANVAS_LIBRARY})
    target_link_libraries(UltraFIBUEngine PUBLIC ${ULTRACANVAS_LIBRARY})
endif()

message(STATUS "  [✓] UltraFIBU engine - ENABLED (target: UltraFIBUEngine)")

# ── UltraFIBUReport: the printed invoice ───────────────────────────────────
# There is no PDF writer in here. The framework's PDFVectorConverter writes the
# file; this target only builds the VectorDocument it consumes.
#
# It compiles the two sources that writer needs directly, rather than linking
# the Vector plugin, for one specific reason: the plugin links the UltraCanvas
# core library, and that would drag the whole rendering stack - pango, vips, a
# display - into a target whose entire point is that an invoice can be produced
# on a headless server. Both files were checked and reference nothing outside
# VectorStorage, so this is a real separation and not a hopeful one. It is the
# same pattern Tests/UltraFIBU already uses for the engine sources.
set(ULTRAFIBU_REPORT_SOURCES
    report/UltraFIBURechnungPdf.cpp
    ${CMAKE_SOURCE_DIR}/UltraCanvas/core/DataFormats/UltraCanvasVectorStorage.cpp
    ${CMAKE_SOURCE_DIR}/UltraCanvas/Plugins/Vector/UltraCanvasPDFVectorConverter.cpp
)

add_library(UltraFIBUReport STATIC ${ULTRAFIBU_REPORT_SOURCES})
set_target_properties(UltraFIBUReport PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultrafibu_report")
target_compile_features(UltraFIBUReport PUBLIC cxx_std_20)
target_include_directories(UltraFIBUReport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/report
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include
    ${CMAKE_SOURCE_DIR}/UltraCanvas/Plugins/Vector)
target_link_libraries(UltraFIBUReport PUBLIC UltraFIBUEngine)

message(STATUS "  [✓] UltraFIBU Rechnungsdruck - ENABLED (target: UltraFIBUReport)")

# ── The shipped data files ───────────────────────────────────────────────────
# The chart of accounts and the tax keys are data, not code. They are copied
# next to the binaries so a build tree behaves like an installation; the
# ULTRAFIBU_DATA_DIR environment variable overrides the location.
set(ULTRAFIBU_DATA_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/data/SKR03.csv
    ${CMAKE_CURRENT_SOURCE_DIR}/data/Steuerschluessel.csv
    ${CMAKE_CURRENT_SOURCE_DIR}/data/DATEV-Buchungsstapel-v700.csv
    ${CMAKE_CURRENT_SOURCE_DIR}/data/DATEV-Sachkontenbeschriftungen-v700.csv
)
add_custom_target(UltraFIBUData ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ULTRAFIBU_DATA_FILES}
            ${CMAKE_BINARY_DIR}/bin/data
    COMMENT "UltraFIBU: Kontenrahmen und Steuerschlüssel nach bin/data kopieren"
    VERBATIM)
add_dependencies(UltraFIBUEngine UltraFIBUData)

# ── ultrafibu: the command-line front end ────────────────────────────────────
# Everything the engine can do, without waiting for the UI: create a company
# and a fiscal year with any start date, import a chart of accounts, keep
# customers and suppliers with their VAT numbers, check a VAT number, list the
# filing deadlines, freeze a period.
add_executable(ultrafibu cli/main.cpp)
target_compile_features(ultrafibu PRIVATE cxx_std_20)
target_link_libraries(ultrafibu PRIVATE UltraFIBUEngine UltraFIBUReport)
# The version comes from the first line of Docs/UltraFIBU/CHANGELOG.md through
# cmake/UltraCanvasVersion.cmake - never a literal in the source. Attached to
# this one source rather than to the target's interface, so a changelog edit
# does not rebuild anything else.
if(ULTRAFIBU_VERSION)
    set_source_files_properties(cli/main.cpp PROPERTIES
        COMPILE_DEFINITIONS "ULTRAFIBU_CLI_VERSION=\"${ULTRAFIBU_VERSION}\"")
endif()
set_target_properties(ultrafibu PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

message(STATUS "  [✓] UltraFIBU CLI - ENABLED (target: ultrafibu)")

# ── ultrafibu-ui: the German screens ────────────────────────────────────
# Belege, Journal, Summen und Salden and the partners, each a sortable and
# filterable table over UltraCanvasListView and its sort/filter proxy.
#
# Needs the UI library, so unlike the engine and the CLI it is skipped where
# that cannot be built - a headless CI runner, a machine without pango or
# vips. The engine stays reachable there through `ultrafibu`.
if(ULTRACANVAS_LIBRARY AND TARGET ${ULTRACANVAS_LIBRARY})
    add_executable(ultrafibu-ui
        ui/main.cpp
        ui/UltraFIBUApp.cpp
        ui/UltraFIBUTabelle.cpp
    )
    target_compile_features(ultrafibu-ui PRIVATE cxx_std_20)
    target_include_directories(ultrafibu-ui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ui)
    target_link_libraries(ultrafibu-ui PRIVATE
        UltraFIBUEngine UltraFIBUReport ${ULTRACANVAS_LIBRARY})
    if(ULTRAFIBU_VERSION)
        set_source_files_properties(ui/main.cpp PROPERTIES
            COMPILE_DEFINITIONS "ULTRAFIBU_UI_VERSION=\"${ULTRAFIBU_VERSION}\"")
    endif()
    set_target_properties(ultrafibu-ui PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "  [✓] UltraFIBU Oberfläche - ENABLED (target: ultrafibu-ui)")
else()
    message(STATUS "  [−] UltraFIBU Oberfläche - SKIPPED (UltraCanvas UI library not built)")
endif()
