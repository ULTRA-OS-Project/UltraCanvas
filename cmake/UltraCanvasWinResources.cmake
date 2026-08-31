# cmake/UltraCanvasWinResources.cmake
# Generate an application's Windows resource script and manifest from the
# version in its changelog, instead of keeping the number written out in them.
#
# windres and rc.exe read a .rc and the manifest it names from disk, so those
# two files used to hold literal version numbers that a separate script
# (set-version.sh) had to be remembered and run to refresh. It was not, and both
# UltraTexter's files (1.40 against a 1.41 changelog) and UltraFiler's (0.8.0
# with no changelog at all, plus a third copy hard-coded in CMakeLists.txt) had
# drifted. A configure-time warning told you about it and still left the fixing
# to a person.
#
# They are templates now. `configure_file` writes them into the build tree on
# every configure, from the same <PREFIX>_VERSION the rest of the build uses, so
# there is nothing left to keep in sync and nothing left to run.
#
# Generation is deliberately NOT guarded by WIN32: the substitution then runs on
# every platform, so a broken template is caught by any configure rather than
# only by a Windows build. Only the compiling half is Windows-only.

include_guard(GLOBAL)

# For _ultracanvas_version_variants() and the <PREFIX>_VERSION values.
include("${CMAKE_CURRENT_LIST_DIR}/UltraCanvasVersion.cmake")

# ultracanvas_add_windows_resources(<target> <dir> <name> <version>)
#
#   <dir>      directory holding the templates (the app's source directory;
#              named explicitly because UltraFiler is built from the top-level
#              CMakeLists while its templates live under Apps/UltraFiler)
#   <name>     base name of the pair, e.g. "UltraTexter" — expects
#              <dir>/<name>.rc.in and <dir>/<name>.manifest.in
#   <version>  dotted version, e.g. "1.41". The template can use
#              @UCRES_VERSION@        the version as written  ("1.41")
#              @UCRES_VERSION_DOT4@   padded to four parts    ("1.41.0.0")
#              @UCRES_VERSION_COMMA4@ padded, comma separated ("1,41,0,0")
#
# The generated .rc is compiled into <target> on Windows and ignored elsewhere.
function(ultracanvas_add_windows_resources TARGET DIR NAME VERSION)
    set(_in_rc "${DIR}/${NAME}.rc.in")
    set(_in_manifest "${DIR}/${NAME}.manifest.in")
    foreach(_template "${_in_rc}" "${_in_manifest}")
        if(NOT EXISTS "${_template}")
            message(FATAL_ERROR
                "ultracanvas_add_windows_resources(${TARGET}): missing ${_template}")
        endif()
    endforeach()

    _ultracanvas_version_variants("${VERSION}" _dot4 _comma4)
    set(UCRES_VERSION "${VERSION}")
    set(UCRES_VERSION_DOT4 "${_dot4}")
    set(UCRES_VERSION_COMMA4 "${_comma4}")

    # Both land in one directory so the .rc's `RT_MANIFEST "<name>.manifest"`
    # resolves next to it, the way it did in the source tree.
    set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_winres")
    file(MAKE_DIRECTORY "${_out_dir}")
    set(_out_rc "${_out_dir}/${NAME}.rc")

    # @ONLY: the .rc is C-preprocessed and the manifest is XML; neither should
    # have ${...} touched.
    configure_file("${_in_rc}" "${_out_rc}" @ONLY)
    configure_file("${_in_manifest}" "${_out_dir}/${NAME}.manifest" @ONLY)

    if(WIN32)
        target_sources(${TARGET} PRIVATE "${_out_rc}")
        set_source_files_properties("${_out_rc}" PROPERTIES
            COMPILE_FLAGS "-I${_out_dir}")
    endif()

    set(${TARGET}_WINRES_DIR "${_out_dir}" PARENT_SCOPE)
endfunction()
