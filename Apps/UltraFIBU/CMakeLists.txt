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
    engine/UltraFIBUUstva.cpp
    engine/UltraFIBUBelegArchiv.cpp
    engine/UltraFIBUOss.cpp
    engine/UltraFIBUEinrichtung.cpp
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

# ── the commands, as a library ──────────────────────────────────────────────
# Everything the engine can do, without waiting for the UI: create a company
# and a fiscal year with any start date, import a chart of accounts, keep
# customers and suppliers with their VAT numbers, check a VAT number, list the
# filing deadlines, freeze a period. No main() here - see cli/UltraFIBUCli.h.
add_library(UltraFIBUCli STATIC cli/UltraFIBUCli.cpp)
set_target_properties(UltraFIBUCli PROPERTIES OUTPUT_NAME "ultrafibu_cli")
target_compile_features(UltraFIBUCli PUBLIC cxx_std_20)
target_include_directories(UltraFIBUCli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cli)
target_link_libraries(UltraFIBUCli PUBLIC UltraFIBUEngine UltraFIBUReport)
# The version comes from the first line of Docs/UltraFIBU/CHANGELOG.md through
# cmake/UltraCanvasVersion.cmake - never a literal in the source. Attached to
# this one source rather than to the target's interface, so a changelog edit
# does not rebuild anything else.
if(ULTRAFIBU_VERSION)
    set_source_files_properties(cli/UltraFIBUCli.cpp PROPERTIES
        COMPILE_DEFINITIONS "ULTRAFIBU_CLI_VERSION=\"${ULTRAFIBU_VERSION}\"")
endif()

# ── ultrafibu: one program, the commands and the window ─────────────────────
# `ultrafibu <Befehl> ...` runs a command and prints; `ultrafibu [<datei>]`
# opens the window. There used to be two programs, ultrafibu and ultrafibu-ui.
#
# The window needs the UI library, so where that cannot be built - a headless
# CI runner, a server, a machine without pango or vips - the same target is
# built from cli/main.cpp instead: the commands without the window. Either
# way the target and the binary are called ultrafibu, so a build command, a
# package list or a script never has to know which one it got.
if(ULTRACANVAS_LIBRARY AND TARGET ${ULTRACANVAS_LIBRARY})
    add_executable(ultrafibu
        ui/main.cpp
        ui/UltraFIBUApp.cpp
        ui/UltraFIBUTabelle.cpp
        ui/UltraFIBUBelegDialog.cpp
        ui/UltraFIBUStart.cpp
        ui/UltraFIBUKonsole.cpp
    )
    target_compile_features(ultrafibu PRIVATE cxx_std_20)
    target_include_directories(ultrafibu PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ui)
    target_link_libraries(ultrafibu PRIVATE UltraFIBUCli ${ULTRACANVAS_LIBRARY})
    set_target_properties(ultrafibu PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # The app icon, in one place for every consumer of it. ui/main.cpp asks for
    # this same file by name; UCAPP_ICON_PATH is the fallback the core applies
    # when an app sets no icon of its own, and the two agreeing is what keeps a
    # window from ever coming up unbranded.
    target_compile_definitions(ultrafibu PRIVATE
        UCAPP_ICON_PATH="media/appicon/UltraFIBU.png")

    # Embed the icon in the .exe (Windows only). Explorer and the Windows
    # taskbar read it off the binary itself - neither looks at _NET_WM_ICON or
    # at a desktop entry.
    #
    # Deliberately NOT a WIN32_EXECUTABLE, unlike the other apps' Release
    # builds: a GUI-subsystem program cannot print into the prompt it was
    # started from, and the commands are half of this one. The console a
    # double-click opens is closed again at start (ui/UltraFIBUKonsole.h).
    ultracanvas_embed_app_icon(ultrafibu
        "${CMAKE_SOURCE_DIR}/media/appicon/UltraFIBU.png")

    # The freedesktop shortcut and the icon it names. `Icon=UltraFIBU` is an
    # icon *name* resolved through the installed themes, so the file has to land
    # in one. There is no scalable/apps install here because UltraFIBU has no
    # SVG yet - unlike EmailCleaner and UltraFiler, which ship a PNG/SVG pair.
    if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
        install(TARGETS ultrafibu RUNTIME DESTINATION bin)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraFIBU.desktop
                DESTINATION share/applications)
        install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraFIBU.png
                DESTINATION share/icons/hicolor/256x256/apps)
        message(STATUS "  UltraFIBU desktop entry: share/applications/UltraFIBU.desktop")
    endif()

    message(STATUS "  [✓] UltraFIBU - ENABLED, Befehle und Fenster (target: ultrafibu)")
else()
    add_executable(ultrafibu cli/main.cpp)
    target_compile_features(ultrafibu PRIVATE cxx_std_20)
    target_link_libraries(ultrafibu PRIVATE UltraFIBUCli)
    set_target_properties(ultrafibu PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "  [✓] UltraFIBU - ENABLED, nur Befehle: UltraCanvas UI library not built (target: ultrafibu)")
endif()
