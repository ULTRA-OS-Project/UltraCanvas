# UltraCanvas/cmake/VendoredCurl.cmake
#
# Builds libcurl from the vendored sources in third_party/curl as a SHARED
# drop-in with WebSocket support. On Linux it stamps libcurl's public symbols
# with the system libcurl's symbol version node (e.g. CURL_OPENSSL_4) so the
# result is a superset that also satisfies other already-installed libraries
# (libvips -> libhdf5) which reference versioned curl symbols.
#
# Included only when ULTRACANVAS_USE_VENDORED_CURL is ON. On return, the target
# `libcurl_shared` (curl's own shared library target) is defined; callers link
# it via ${UC_CURL_LINK}.

# curl's CMake reads these as cache options; force them for our sub-build.
set(_uc_saved_build_shared "${BUILD_SHARED_LIBS}")
set(BUILD_CURL_EXE    OFF CACHE BOOL "" FORCE)  # no curl executable
set(BUILD_TESTING     OFF CACHE BOOL "" FORCE)  # no curl test suite
set(BUILD_SHARED_LIBS ON  CACHE BOOL "" FORCE)  # we want libcurl.so.4
set(BUILD_STATIC_LIBS OFF CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL   ON  CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL  ON  CACHE BOOL "" FORCE)  # match distro OpenSSL flavour
# CURL_DISABLE_WEBSOCKETS stays OFF (curl's default) => WebSockets enabled.

# SFTP for UltraStoreAdmin's upload: curl's own CMake looks for libssh2 (its
# default) and silently builds without SCP / SFTP when the development
# package is missing, so the outcome is checked below and said out loud.
set(CURL_USE_LIBSSH2  ON  CACHE BOOL "" FORCE)

# A curl_config.h inside the SOURCE tree is the output of an in-tree
# autotools ./configure, not part of curl. curl_setup.h includes
# "curl_config.h" with quotes, so the compiler takes the one beside it in
# lib/ before the one CMake generates into the build tree, and every option
# CMake decided (libssh2, and so SFTP) is lost while the library still links
# what CMake found. 2026-11-01: that file shipped with the vendored tree and
# UltraStoreAdmin's sftp:// upload was refused with 'Protocol "sftp" is
# disabled' although libssh2 was linked. Refuse to build on top of it.
set(_uc_curl_src "${CMAKE_CURRENT_LIST_DIR}/../third_party/curl")
if(EXISTS "${_uc_curl_src}/lib/curl_config.h")
    message(FATAL_ERROR
        "VendoredCurl: ${_uc_curl_src}/lib/curl_config.h exists. It is the output "
        "of an in-tree autotools configure and shadows the curl_config.h CMake "
        "generates, so the build silently loses the options CMake chose (SFTP "
        "among them). Delete it (git rm) and configure again.")
endif()

add_subdirectory(
    "${_uc_curl_src}"
    "${CMAKE_CURRENT_BINARY_DIR}/third_party/curl-build"
    EXCLUDE_FROM_ALL)

# What curl's CMake decided about SSH, read from its directory scope.
get_directory_property(_uc_curl_libssh2 DIRECTORY "${_uc_curl_src}" DEFINITION USE_LIBSSH2)
get_directory_property(_uc_curl_libssh  DIRECTORY "${_uc_curl_src}" DEFINITION USE_LIBSSH)
if(_uc_curl_libssh2)
    message(STATUS "  [i] Vendored libcurl: SSH backend = libssh2 (sftp:// available)")
elseif(_uc_curl_libssh)
    message(STATUS "  [i] Vendored libcurl: SSH backend = libssh (sftp:// available)")
else()
    message(WARNING
        "Vendored libcurl: no SSH backend - libssh2 was not found, so this libcurl "
        "has no sftp:// and UltraStoreAdmin's SFTP upload will be refused with "
        "'Protocol \"sftp\" is disabled'. Install libssh2's development package "
        "(Debian/Ubuntu: libssh2-1-dev) and configure again.")
    message(STATUS "  [-] Vendored libcurl: SSH backend = none (no sftp://)")
endif()

# Restore so we don't accidentally flip UltraCanvas's own libraries to shared.
set(BUILD_SHARED_LIBS "${_uc_saved_build_shared}" CACHE BOOL "" FORCE)

if(NOT TARGET libcurl_shared)
    message(FATAL_ERROR
        "VendoredCurl: expected target 'libcurl_shared' from third_party/curl "
        "(check BUILD_SHARED_LIBS handling in the vendored curl CMakeLists).")
endif()

# --- Linux: force the distro-compatible symbol version node -----------------
# curl's CMake build has no versioned-symbols feature, so we inject a linker
# version script. The node name must match what libhdf5/libvips reference; we
# detect it from the DISTRO libcurl (multiarch/system dirs only, never
# /usr/local) and fall back to CURL_OPENSSL_4 (Debian/Ubuntu + OpenSSL).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_uc_node "CURL_OPENSSL_4")
    file(GLOB _uc_distro_curls
        "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libcurl.so.4*"
        "/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libcurl.so.4*"
        "/usr/lib/libcurl.so.4*"
        "/usr/lib64/libcurl.so.4*")
    if(_uc_distro_curls)
        list(GET _uc_distro_curls 0 _uc_distro_curl)
        execute_process(COMMAND objdump -T "${_uc_distro_curl}"
                        OUTPUT_VARIABLE _uc_dump ERROR_QUIET)
        string(REGEX MATCH "CURL_[A-Z0-9_]+" _uc_match "${_uc_dump}")
        if(_uc_match)
            set(_uc_node "${_uc_match}")
        endif()
    endif()

    set(_uc_vers_map "${CMAKE_CURRENT_BINARY_DIR}/curl-vendored-${_uc_node}.map")
    file(WRITE "${_uc_vers_map}"
        "${_uc_node} {\n  global:\n    curl_*;\n  local:\n    *;\n};\n")
    target_link_options(libcurl_shared PRIVATE
        "-Wl,--version-script=${_uc_vers_map}")
    message(STATUS "  [i] Vendored libcurl: symbol version node = ${_uc_node}")

    # --- Make consumers link regardless of curl-vs-hdf5 order ------------------
    # libvips pulls in libhdf5, which carries a versioned NEEDED on the DISTRO
    # libcurl.so.4 (curl_*@${_uc_node}). Our vendored libcurl provides those same
    # versioned symbols (version script above), and hdf5 also has its own
    # DT_NEEDED on the system libcurl.so.4, so at load time every reference
    # resolves (hdf5 -> system libcurl.so.4, UltraNet -> vendored libcurl-d.so.4).
    # But GNU ld resolves shared-lib -> shared-lib symbols in a single FORWARD
    # pass: if the vendored curl appears BEFORE -lvips on a consumer's link line,
    # ld hasn't yet seen hdf5's requirement and fails with
    #   "undefined reference to curl_global_init@${_uc_node}".
    # UltraCanvas' own demos never trip this (they don't link vips); CoderBox
    # does. Propagate --allow-shlib-undefined to everything that links the
    # vendored curl so ld trusts a shared lib's own undefined symbols to resolve
    # at load time — order-independent. This only relaxes the check for pulled-in
    # shared libraries; undefined symbols in the consumer's OWN objects still err.
    target_link_options(libcurl_shared INTERFACE
        "-Wl,--allow-shlib-undefined")
endif()
