# cmake/UltraCanvasMacOSDeps.cmake - where the macOS dependency libraries live.
#
# A local macOS build links Homebrew's libraries. CI does not: Homebrew builds
# its bottles for the macOS of the machine that built them, and dyld refuses a
# library built for a newer macOS than the one running, so apps bundling them
# start only on that macOS and newer. CI builds the same libraries with vcpkg
# for the oldest macOS the apps support (scripts/macos-deps.sh, MacOS/deps/)
# and passes the result as ULTRACANVAS_MACOS_DEPS_PREFIX.
#
# The two prefixes differ in one way the build cares about: Homebrew keeps a
# keg-only formula (icu4c, ...) under <prefix>/opt/<formula>, where a vcpkg
# prefix is flat. ultracanvas_macos_dep_prefix() hides that, so nothing else
# needs to ask `brew --prefix`.
#
# Included by the top-level CMakeLists.txt and by UltraCanvas/CMakeLists.txt
# (which also configures on its own).

include_guard(GLOBAL)

if(NOT APPLE)
    return()
endif()

set(_uc_default_macos_deps_prefix "")
if(NOT DEFINED CACHE{ULTRACANVAS_MACOS_DEPS_PREFIX})
    execute_process(COMMAND brew --prefix
        OUTPUT_VARIABLE _uc_default_macos_deps_prefix
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
set(ULTRACANVAS_MACOS_DEPS_PREFIX "${_uc_default_macos_deps_prefix}" CACHE PATH
    "Prefix holding the macOS dependency libraries (default: Homebrew's; scripts/macos-deps.sh builds one with vcpkg)")

if(EXISTS "${ULTRACANVAS_MACOS_DEPS_PREFIX}/Cellar")
    set(ULTRACANVAS_MACOS_DEPS_HOMEBREW TRUE)
else()
    set(ULTRACANVAS_MACOS_DEPS_HOMEBREW FALSE)
endif()

if(ULTRACANVAS_MACOS_DEPS_PREFIX AND NOT ULTRACANVAS_MACOS_DEPS_HOMEBREW)
    # find_package/find_library/find_path look here before anywhere else, and
    # pkg-config finds the prefix's .pc files first (CI also restricts
    # PKG_CONFIG_LIBDIR to them, so nothing falls back to Homebrew's).
    list(PREPEND CMAKE_PREFIX_PATH "${ULTRACANVAS_MACOS_DEPS_PREFIX}")
    set(ENV{PKG_CONFIG_PATH}
        "${ULTRACANVAS_MACOS_DEPS_PREFIX}/lib/pkgconfig:${ULTRACANVAS_MACOS_DEPS_PREFIX}/share/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    # vcpkg's dylibs are named @rpath/<file> (Homebrew's carry absolute
    # paths), and pkg-config hands them to the linker as -L/-l, from which
    # CMake adds no run path - so without this, every program in the build
    # tree, the tests included, stops at load with "Library not loaded:
    # @rpath/...". package-macos.sh deletes the entry again when it bundles
    # the libraries, so no shipped binary carries a build machine's path.
    add_link_options("LINKER:-rpath,${ULTRACANVAS_MACOS_DEPS_PREFIX}/lib")
    # UltraNet uses Apple's libcurl on macOS (the system's TLS and
    # certificates). vcpkg's prefix has a libcurl of its own - tesseract
    # depends on it - and with the prefix first on CMAKE_PREFIX_PATH,
    # find_package(CURL) would take that one, with its CURLConfig.cmake,
    # instead. Point FindCURL at the SDK's before anything asks for it.
    execute_process(COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE _uc_macos_sdk OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_uc_macos_sdk)
        set(CURL_NO_CURL_CMAKE ON CACHE BOOL "Use FindCURL's own search, not a CURLConfig.cmake")
        find_path(CURL_INCLUDE_DIR curl/curl.h PATHS "${_uc_macos_sdk}/usr/include" NO_DEFAULT_PATH)
        find_library(CURL_LIBRARY NAMES curl PATHS "${_uc_macos_sdk}/usr/lib" NO_DEFAULT_PATH)
    endif()
    message(STATUS "macOS dependencies: ${ULTRACANVAS_MACOS_DEPS_PREFIX}")
endif()

# ultracanvas_macos_dep_prefix(<formula> <out-var>) - the prefix the files of
# dependency <formula> (a Homebrew formula name) are under: the formula's keg
# in a Homebrew prefix, the prefix itself otherwise.
function(ultracanvas_macos_dep_prefix formula out_var)
    if(ULTRACANVAS_MACOS_DEPS_HOMEBREW)
        set(${out_var} "${ULTRACANVAS_MACOS_DEPS_PREFIX}/opt/${formula}" PARENT_SCOPE)
    else()
        set(${out_var} "${ULTRACANVAS_MACOS_DEPS_PREFIX}" PARENT_SCOPE)
    endif()
endfunction()
