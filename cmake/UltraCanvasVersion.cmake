# cmake/UltraCanvasVersion.cmake
# Single source of truth for version numbers: the first line of each CHANGELOG.
#
# The packaging scripts (build-demoapp-appimage.sh, package-win.sh,
# package-macos.sh) already parse the changelog directly, which is why the
# version in a built artefact's file name is always right. Everything compiled
# *into* the binaries used to carry a hand-maintained copy of the same number,
# refreshed only when someone remembered to run a script — so the demo app's
# info window drifted (it showed 0.3.21 against a 0.3.31 changelog).
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
#   LADYBIRD_VERSION            e.g. "0.1.0"   (Docs/Ladybird/CHANGELOG.md)
#   LADYBIRD_VERSION_DOT4       e.g. "0.1.0.0"
#   LADYBIRD_VERSION_COMMA4     e.g. "0,1,0,0"
#
# and one <APP>_VERSION triple per application that keeps its own changelog:
#
#   ANCHORPOINT_VERSION        (Docs/AnchorPoint/CHANGELOG.md)
#   EMAILCLEANER_VERSION       (Docs/EmailCleaner/CHANGELOG.md)
#   ULTRAAI_VERSION            (Docs/UltraAI/CHANGELOG.md)
#   ULTRAAUTHENTICATOR_VERSION (Docs/UltraAuthenticator/CHANGELOG.md)
#   ULTRAFILER_VERSION         (Docs/UltraFiler/CHANGELOG.md)
#   ULTRAMAIL_VERSION          (Docs/UltraMail/CHANGELOG.md)
#   ULTRASOCIAL_VERSION        (Docs/UltraSocial/CHANGELOG.md)
#   ULTRAVIEWER_VERSION        (Docs/UltraViewer/CHANGELOG.md)
#   ULTRAWIN_VERSION           (Docs/Modules/UltraWin/CHANGELOG.md)
#
# Each of those also gets _DOT4 / _COMMA4 variants. An application with its own
# changelog versions itself: it does not move when the framework releases, and
# a change to it is described in its own file. Several have no consumer in the
# build yet — like LADYBIRD_VERSION — and are set anyway, so that when one needs
# a version it takes it from the same place everything else does rather than
# growing a second copy of the number.
#
# DemoApp is deliberately NOT in this list. It is the framework's own showcase:
# its artefacts are named UCDemo-<ULTRACANVAS_VERSION> by the packaging scripts
# and by CI, so it versions with the framework by design, and giving it a second
# number would be exactly the duplication this module exists to prevent.
#
# The Ladybird port is built from its own tree, outside this repository, so
# nothing here consumes LADYBIRD_VERSION yet; it is set so that tree gets the
# port's version from the same place everything else does simply by including
# this module, instead of keeping a second copy of the number.
#
# Expected first line of a changelog: `#### YYYY-MM-DD *x.y.z*`

include_guard(GLOBAL)

get_filename_component(_ULTRACANVAS_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)



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

# Declare one product: sets <PREFIX>_CHANGELOG_FILE, _VERSION, _VERSION_DOT4
# and _VERSION_COMMA4, and remembers the file so the configure step re-runs when
# it gains an entry. A macro rather than a table of "PREFIX;path" rows, because
# CMake flattens a list element that contains a semicolon and the rows would
# come apart. Adding an application is one line below plus its changelog.
macro(_ultracanvas_declare_product PREFIX RELATIVE)
    set(${PREFIX}_CHANGELOG_FILE "${_ULTRACANVAS_REPO_ROOT}/${RELATIVE}")
    _ultracanvas_version_from_changelog("${${PREFIX}_CHANGELOG_FILE}" ${PREFIX}_VERSION)
    _ultracanvas_version_variants("${${PREFIX}_VERSION}"
        ${PREFIX}_VERSION_DOT4 ${PREFIX}_VERSION_COMMA4)
    list(APPEND _ULTRACANVAS_ALL_CHANGELOGS "${${PREFIX}_CHANGELOG_FILE}")
endmacro()

set(_ULTRACANVAS_ALL_CHANGELOGS "")

# The framework itself, and the products that version with it (DemoApp).
_ultracanvas_declare_product(ULTRACANVAS         "Docs/UltraCanvas/CHANGELOG.md")

# Applications that keep their own changelog and version themselves.
_ultracanvas_declare_product(ULTRATEXTER         "Docs/Texter/CHANGELOG.md")
_ultracanvas_declare_product(ULTRACLEANER        "Docs/UltraCleaner/CHANGELOG.md")
_ultracanvas_declare_product(LADYBIRD            "Docs/Ladybird/CHANGELOG.md")
_ultracanvas_declare_product(ANCHORPOINT         "Docs/AnchorPoint/CHANGELOG.md")
_ultracanvas_declare_product(EMAILCLEANER        "Docs/EmailCleaner/CHANGELOG.md")
_ultracanvas_declare_product(ULTRAAI             "Docs/UltraAI/CHANGELOG.md")
_ultracanvas_declare_product(ULTRAAUTHENTICATOR  "Docs/UltraAuthenticator/CHANGELOG.md")
_ultracanvas_declare_product(ULTRAFILER          "Docs/UltraFiler/CHANGELOG.md")
_ultracanvas_declare_product(ULTRAMAIL           "Docs/UltraMail/CHANGELOG.md")
_ultracanvas_declare_product(ULTRASOCIAL         "Docs/UltraSocial/CHANGELOG.md")
_ultracanvas_declare_product(ULTRAVIEWER         "Docs/UltraViewer/CHANGELOG.md")
_ultracanvas_declare_product(ULTRAWIN            "Docs/Modules/UltraWin/CHANGELOG.md")

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
    CMAKE_CONFIGURE_DEPENDS ${_ULTRACANVAS_ALL_CHANGELOGS})

# Nothing downstream holds a literal version any more. The two Windows resource
# pairs that used to (UltraTexter.rc/.manifest, UltraFiler.rc/.manifest) are
# templates now, written into the build tree by
# ultracanvas_add_windows_resources() in cmake/UltraCanvasWinResources.cmake
# from the values above — so there is no staleness left to warn about and no
# set-version.sh to remember to run.
