# cmake/UltraCanvasVersion.cmake
# Single source of truth for version numbers: the first line of each CHANGELOG.
#
# The packaging scripts (build-demoapp-appimage.sh, package-win.sh,
# package-macos.sh) already parse the changelog directly, which is why the
# version in a built artefact's file name is always right. Everything compiled
# *into* the binaries used to carry a hand-maintained copy of the same number,
# refreshed only when someone remembered to run set-version.sh — so the demo
# app's info window drifted (it showed 0.3.21 against a 0.3.31 changelog).
#
# Including this module makes CMake read the same first changelog line the
# packaging scripts read, so the displayed version cannot disagree with the
# file name of the build it came from.
#
# Sets, in the including scope:
#   ULTRACANVAS_VERSION        e.g. "0.3.31"   (Docs/UltraCanvas/CHANGELOG.md)
#   ULTRACANVAS_VERSION_DOT4   e.g. "0.3.31.0"
#   ULTRACANVAS_VERSION_COMMA4 e.g. "0,3,31,0"
#   ULTRATEXTER_VERSION        e.g. "1.40"     (Docs/Texter/CHANGELOG.md)
#   ULTRATEXTER_VERSION_DOT4   e.g. "1.40.0.0"
#   ULTRATEXTER_VERSION_COMMA4 e.g. "1,40,0,0"
#   ULTRACLEANER_VERSION        e.g. "0.50"    (Docs/UltraCleaner/CHANGELOG.md)
#   ULTRACLEANER_VERSION_DOT4   e.g. "0.50.0.0"
#   ULTRACLEANER_VERSION_COMMA4 e.g. "0,50,0,0"
#   ULTRAFILER_VERSION          e.g. "0.9.0"   (Docs/UltraFiler/CHANGELOG.md)
#   ULTRAFILER_VERSION_DOT4     e.g. "0.9.0.0"
#   ULTRAFILER_VERSION_COMMA4   e.g. "0,9,0,0"
#
# Expected first line of a changelog: `#### YYYY-MM-DD *x.y.z*`

include_guard(GLOBAL)

get_filename_component(_ULTRACANVAS_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(ULTRACANVAS_CHANGELOG_FILE "${_ULTRACANVAS_REPO_ROOT}/Docs/UltraCanvas/CHANGELOG.md")
set(ULTRATEXTER_CHANGELOG_FILE "${_ULTRACANVAS_REPO_ROOT}/Docs/Texter/CHANGELOG.md")
set(ULTRACLEANER_CHANGELOG_FILE "${_ULTRACANVAS_REPO_ROOT}/Docs/UltraCleaner/CHANGELOG.md")
set(ULTRAFILER_CHANGELOG_FILE "${_ULTRACANVAS_REPO_ROOT}/Docs/UltraFiler/CHANGELOG.md")

# Pad a dotted version to exactly four components and emit the dotted and
# comma-separated forms Windows resource scripts want.
function(_ultracanvas_version_variants VERSION OUT_DOT4 OUT_COMMA4)
    string(REPLACE "." ";" _parts "${VERSION}")
    list(LENGTH _parts _count)
    while(_count LESS 4)
        list(APPEND _parts "0")
        math(EXPR _count "${_count} + 1")
    endwhile()
    list(SUBLIST _parts 0 4 _parts)
    string(REPLACE ";" "." _dot4 "${_parts}")
    string(REPLACE ";" "," _comma4 "${_parts}")
    set(${OUT_DOT4} "${_dot4}" PARENT_SCOPE)
    set(${OUT_COMMA4} "${_comma4}" PARENT_SCOPE)
endfunction()

# Read `#### YYYY-MM-DD *x.y.z*` from the first line of CHANGELOG.
function(_ultracanvas_version_from_changelog CHANGELOG OUT_VAR)
    if(NOT EXISTS "${CHANGELOG}")
        message(FATAL_ERROR "UltraCanvas version: changelog not found at ${CHANGELOG}")
    endif()

    file(STRINGS "${CHANGELOG}" _first_line LIMIT_COUNT 1)
    string(REGEX MATCH "^#### [0-9-]+ \\*([0-9]+(\\.[0-9]+)*)\\*" _matched "${_first_line}")
    if(NOT _matched)
        message(FATAL_ERROR
            "UltraCanvas version: could not parse a version from the first line of\n"
            "  ${CHANGELOG}\n"
            "  got:      ${_first_line}\n"
            "  expected: '#### YYYY-MM-DD *x.y.z*'")
    endif()

    set(${OUT_VAR} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

_ultracanvas_version_from_changelog("${ULTRACANVAS_CHANGELOG_FILE}" ULTRACANVAS_VERSION)
_ultracanvas_version_from_changelog("${ULTRATEXTER_CHANGELOG_FILE}" ULTRATEXTER_VERSION)
_ultracanvas_version_from_changelog("${ULTRACLEANER_CHANGELOG_FILE}" ULTRACLEANER_VERSION)
_ultracanvas_version_from_changelog("${ULTRAFILER_CHANGELOG_FILE}" ULTRAFILER_VERSION)

_ultracanvas_version_variants("${ULTRACANVAS_VERSION}"
    ULTRACANVAS_VERSION_DOT4 ULTRACANVAS_VERSION_COMMA4)
_ultracanvas_version_variants("${ULTRATEXTER_VERSION}"
    ULTRATEXTER_VERSION_DOT4 ULTRATEXTER_VERSION_COMMA4)
_ultracanvas_version_variants("${ULTRACLEANER_VERSION}"
    ULTRACLEANER_VERSION_DOT4 ULTRACLEANER_VERSION_COMMA4)
_ultracanvas_version_variants("${ULTRAFILER_VERSION}"
    ULTRAFILER_VERSION_DOT4 ULTRAFILER_VERSION_COMMA4)

# Re-run the configure step when a changelog gains a new entry, so an
# already-configured build tree picks the new version up instead of baking in
# whatever was current when it was first configured. Honoured by the Makefile
# and Ninja generators on the top-level directory.
if(CMAKE_SOURCE_DIR)
    set(_uc_configure_depends_dir "${CMAKE_SOURCE_DIR}")
else()
    set(_uc_configure_depends_dir "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
set_property(DIRECTORY "${_uc_configure_depends_dir}" APPEND PROPERTY
    CMAKE_CONFIGURE_DEPENDS
        "${ULTRACANVAS_CHANGELOG_FILE}"
        "${ULTRATEXTER_CHANGELOG_FILE}"
        "${ULTRACLEANER_CHANGELOG_FILE}"
        "${ULTRAFILER_CHANGELOG_FILE}")

# The Windows resource script and manifest still hold literal version numbers
# (they are compiled by windres/rc.exe from files on disk, not generated), and
# set-version.sh writes them. Warn — on every platform, so a Linux or macOS
# configure catches it too — when they have fallen behind the changelog.
function(_ultracanvas_warn_if_version_stale FILE PATTERN EXPECTED WHAT)
    if(NOT EXISTS "${FILE}")
        return()
    endif()
    file(STRINGS "${FILE}" _hits REGEX "${PATTERN}")
    foreach(_line IN LISTS _hits)
        string(REGEX MATCH "${PATTERN}" _ignored "${_line}")
        if(NOT CMAKE_MATCH_1 STREQUAL "${EXPECTED}")
            message(WARNING
                "${WHAT} is ${CMAKE_MATCH_1} but the changelog says ${EXPECTED}.\n"
                "  ${FILE}\n"
                "  Run ./set-version.sh to refresh the Windows resource files.")
        endif()
    endforeach()
endfunction()

_ultracanvas_warn_if_version_stale(
    "${_ULTRACANVAS_REPO_ROOT}/Apps/Texter/UltraTexter.rc"
    "^ +FILEVERSION +([0-9,]+)" "${ULTRATEXTER_VERSION_COMMA4}"
    "UltraTexter.rc FILEVERSION")
_ultracanvas_warn_if_version_stale(
    "${_ULTRACANVAS_REPO_ROOT}/Apps/Texter/UltraTexter.manifest"
    "^        version=\"([0-9.]+)\"" "${ULTRATEXTER_VERSION_DOT4}"
    "UltraTexter.manifest assembly version")
_ultracanvas_warn_if_version_stale(
    "${_ULTRACANVAS_REPO_ROOT}/Apps/UltraFiler/UltraFiler.rc"
    "^ +FILEVERSION +([0-9,]+)" "${ULTRAFILER_VERSION_COMMA4}"
    "UltraFiler.rc FILEVERSION")
_ultracanvas_warn_if_version_stale(
    "${_ULTRACANVAS_REPO_ROOT}/Apps/UltraFiler/UltraFiler.manifest"
    "^        version=\"([0-9.]+)\"" "${ULTRAFILER_VERSION_DOT4}"
    "UltraFiler.manifest assembly version")
