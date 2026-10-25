# cmake/UltraCanvasTdlib.cmake
# TDLib's JSON library (libtdjson) for the Telegram provider
# (Plugins/UltraIM/telegram), fetched prebuilt at configure time and put
# where the provider looks first at run time: <build>/Plugins/UltraIM/tdlib/
# (libtdjson.so / libtdjson.dylib / tdjson.dll, with the OpenSSL and zlib
# libraries the macOS and Windows builds of it need beside it). The packaging
# scripts ship that folder next to the provider modules. Nothing of TDLib is
# linked: the provider dlopens it, so a build without it still builds and
# signs in wherever a libtdjson is found (ULTRAIM_TDJSON_PATH, the system's).
#
# Where the library comes from, in order:
#   1. ULTRAIM_TDJSON_FILE - a libtdjson of your own (an offline build, a TDLib
#      you built): copied as is, under the platform's name.
#   2. The `tdjson` wheel on PyPI, pinned to one version and checked against
#      the SHA-256 below: a plain shared library inside a zip, built by its
#      maintainers for Linux x86_64 and aarch64 (manylinux2014, OpenSSL and
#      zlib linked in), macOS arm64 and Windows x64 - the same library CI's
#      TelegramTdlibTests have always run against. No wheel exists for macOS
#      Intel or Windows ARM; those builds say so and rely on a libtdjson found
#      at run time.
# ULTRAIM_FETCH_TDLIB=OFF skips the download.
#
# Sets (cache, INTERNAL):
#   ULTRAIM_TDLIB_LIBRARY  the shipped library's full path, "" when none
#   ULTRAIM_TDLIB_STATUS   one line for the configure summary
#
# Version: 1.0.0
# Last Modified: 2026-10-25
# Author: UltraCanvas Framework / ULTRA OS

set(ULTRAIM_TDLIB_VERSION "1.8.69")
option(ULTRAIM_FETCH_TDLIB "Telegram provider: download TDLib's prebuilt libtdjson (the tdjson wheel) at configure time" ON)
set(ULTRAIM_TDJSON_FILE "" CACHE FILEPATH "A libtdjson to ship with the Telegram provider instead of the downloaded one")
set(ULTRAIM_TDLIB_DIR "${CMAKE_BINARY_DIR}/Plugins/UltraIM/tdlib")
set(ULTRAIM_TDLIB_LIBRARY "" CACHE INTERNAL "The libtdjson shipped with the Telegram provider")
set(ULTRAIM_TDLIB_STATUS "" CACHE INTERNAL "How the Telegram provider's TDLib was resolved")

# The wheel for this platform (tdjson 1.8.69, the cp313 build - the library
# inside does not depend on the Python version), its SHA-256, the library's
# name in it and what ships beside it.
set(_uc_td_cpu "${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(GET CMAKE_OSX_ARCHITECTURES 0 _uc_td_cpu)
endif()
string(TOLOWER "${_uc_td_cpu}" _uc_td_cpu)
if(_uc_td_cpu MATCHES "^(x86_64|amd64)$")
    set(_uc_td_cpu "x86_64")
elseif(_uc_td_cpu MATCHES "^(aarch64|arm64)$")
    set(_uc_td_cpu "aarch64")
endif()

set(_uc_td_url "")
set(_uc_td_sha256 "")
set(_uc_td_name "")          # the library's name once shipped
set(_uc_td_glob "")          # the library inside the unpacked wheel
set(_uc_td_sidecars "")      # libraries shipped beside it, as globs
set(_uc_td_patterns "")      # what to unpack
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND _uc_td_cpu STREQUAL "x86_64")
    set(_uc_td_url "https://files.pythonhosted.org/packages/e5/28/7e293c1e31e8d378eda9c3a0c20b443d52be83d4574289128cb776f4705b/tdjson-1.8.69-cp313-cp313-manylinux2014_x86_64.manylinux_2_17_x86_64.whl")
    set(_uc_td_sha256 "5971fe43c41173f75bb63c9bf8bc54c7a0e8013fa0c41c66cf086967e16ddf86")
    set(_uc_td_name "libtdjson.so")
    set(_uc_td_glob "tdjson.libs/libtdjson-*.so*")
    set(_uc_td_patterns "tdjson.libs/*")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND _uc_td_cpu STREQUAL "aarch64")
    set(_uc_td_url "https://files.pythonhosted.org/packages/fc/a2/e1382351fd6f527d943e63302f67b64ea931061d63290b8b70a7ff6e7628/tdjson-1.8.69-cp313-cp313-manylinux2014_aarch64.manylinux_2_17_aarch64.whl")
    set(_uc_td_sha256 "2fc1d4f1d6abbbad5f783173371005064f5206caca733929d6a78f770e4ad4e6")
    set(_uc_td_name "libtdjson.so")
    set(_uc_td_glob "tdjson.libs/libtdjson-*.so*")
    set(_uc_td_patterns "tdjson.libs/*")
elseif(APPLE AND _uc_td_cpu STREQUAL "aarch64")
    set(_uc_td_url "https://files.pythonhosted.org/packages/b8/82/7b3cc7d5fd9e1842de385dfecb40750847ee26a61ba3f5253a2c8eaad21e/tdjson-1.8.69-cp313-cp313-macosx_11_0_arm64.whl")
    set(_uc_td_sha256 "a3899c3f58aadb8feeea68b469845508f59aaa739f68bcb481f7beaac6b6803b")
    set(_uc_td_name "libtdjson.dylib")
    set(_uc_td_glob "tdjson/.dylibs/libtdjson.*.dylib")
    # Referenced as @loader_path/<name>: same folder, same names.
    set(_uc_td_sidecars "tdjson/.dylibs/libssl.*.dylib" "tdjson/.dylibs/libcrypto.*.dylib")
    set(_uc_td_patterns "tdjson/.dylibs/*")
elseif(WIN32 AND _uc_td_cpu STREQUAL "x86_64")
    set(_uc_td_url "https://files.pythonhosted.org/packages/8c/a2/cf0f907f932f7b6b2ee0fc974443c667ed2f24269824f1969b1595f8b9dc/tdjson-1.8.69-cp313-cp313-win_amd64.whl")
    set(_uc_td_sha256 "f25ed16c406795a50738b3a6bbdc5c0975c26bb6998f0af1f4cb7795a1c541ea")
    set(_uc_td_name "tdjson.dll")
    set(_uc_td_glob "tdjson.libs/tdjson-*.dll")
    # Imported under these (hashed) names by tdjson.dll: same folder, same
    # names. The provider puts that folder on the DLL search path to load it.
    set(_uc_td_sidecars "tdjson.libs/libcrypto-*.dll" "tdjson.libs/libssl-*.dll" "tdjson.libs/zlib1-*.dll"
                        "tdjson.libs/msvcp140-*.dll")
    set(_uc_td_patterns "tdjson.libs/*")
endif()

if(ULTRAIM_TDJSON_FILE)
    if(NOT EXISTS "${ULTRAIM_TDJSON_FILE}")
        message(FATAL_ERROR "ULTRAIM_TDJSON_FILE names no file: ${ULTRAIM_TDJSON_FILE}")
    endif()
    if(NOT _uc_td_name)
        if(WIN32)
            set(_uc_td_name "tdjson.dll")
        elseif(APPLE)
            set(_uc_td_name "libtdjson.dylib")
        else()
            set(_uc_td_name "libtdjson.so")
        endif()
    endif()
    file(MAKE_DIRECTORY "${ULTRAIM_TDLIB_DIR}")
    configure_file("${ULTRAIM_TDJSON_FILE}" "${ULTRAIM_TDLIB_DIR}/${_uc_td_name}" COPYONLY)
    set(ULTRAIM_TDLIB_LIBRARY "${ULTRAIM_TDLIB_DIR}/${_uc_td_name}" CACHE INTERNAL "")
    set(ULTRAIM_TDLIB_STATUS "ULTRAIM_TDJSON_FILE, copied to Plugins/UltraIM/tdlib" CACHE INTERNAL "")
elseif(NOT ULTRAIM_FETCH_TDLIB)
    set(ULTRAIM_TDLIB_LIBRARY "" CACHE INTERNAL "")
    set(ULTRAIM_TDLIB_STATUS "not fetched (ULTRAIM_FETCH_TDLIB is OFF); found at run time or not at all" CACHE INTERNAL "")
elseif(NOT _uc_td_url)
    set(ULTRAIM_TDLIB_LIBRARY "" CACHE INTERNAL "")
    set(ULTRAIM_TDLIB_STATUS "no prebuilt libtdjson for ${CMAKE_SYSTEM_NAME} ${_uc_td_cpu}; found at run time or not at all" CACHE INTERNAL "")
elseif(CMAKE_VERSION VERSION_LESS 3.18)
    set(ULTRAIM_TDLIB_LIBRARY "" CACHE INTERNAL "")
    set(ULTRAIM_TDLIB_STATUS "not fetched (unpacking the wheel needs CMake 3.18); found at run time or not at all" CACHE INTERNAL "")
else()
    set(_uc_td_target "${ULTRAIM_TDLIB_DIR}/${_uc_td_name}")
    set(_uc_td_stamp "${ULTRAIM_TDLIB_DIR}/.tdjson-${_uc_td_sha256}")
    if(EXISTS "${_uc_td_target}" AND EXISTS "${_uc_td_stamp}")
        set(ULTRAIM_TDLIB_LIBRARY "${_uc_td_target}" CACHE INTERNAL "")
        set(ULTRAIM_TDLIB_STATUS "TDLib ${ULTRAIM_TDLIB_VERSION} in Plugins/UltraIM/tdlib (already fetched)" CACHE INTERNAL "")
    else()
        set(_uc_td_deps "${CMAKE_BINARY_DIR}/_deps/tdjson")
        get_filename_component(_uc_td_wheel_name "${_uc_td_url}" NAME)
        set(_uc_td_wheel "${_uc_td_deps}/${_uc_td_wheel_name}")
        set(_uc_td_ok TRUE)
        set(_uc_td_why "")
        if(NOT EXISTS "${_uc_td_wheel}")
            message(STATUS "  Fetching TDLib ${ULTRAIM_TDLIB_VERSION} for the Telegram provider (${_uc_td_wheel_name})")
            file(MAKE_DIRECTORY "${_uc_td_deps}")
            file(DOWNLOAD "${_uc_td_url}" "${_uc_td_wheel}.part"
                 EXPECTED_HASH SHA256=${_uc_td_sha256}
                 TLS_VERIFY ON
                 INACTIVITY_TIMEOUT 60
                 STATUS _uc_td_download)
            list(GET _uc_td_download 0 _uc_td_code)
            if(_uc_td_code EQUAL 0)
                file(RENAME "${_uc_td_wheel}.part" "${_uc_td_wheel}")
            else()
                list(GET _uc_td_download 1 _uc_td_why)
                file(REMOVE "${_uc_td_wheel}.part")
                set(_uc_td_ok FALSE)
            endif()
        endif()
        if(_uc_td_ok)
            # A wheel is a zip; the extractor reads the format from the content.
            set(_uc_td_unpacked "${_uc_td_deps}/unpacked")
            file(REMOVE_RECURSE "${_uc_td_unpacked}")
            file(ARCHIVE_EXTRACT INPUT "${_uc_td_wheel}" DESTINATION "${_uc_td_unpacked}" PATTERNS ${_uc_td_patterns})
            file(GLOB _uc_td_found LIST_DIRECTORIES false "${_uc_td_unpacked}/${_uc_td_glob}")
            list(LENGTH _uc_td_found _uc_td_count)
            if(NOT _uc_td_count EQUAL 1)
                set(_uc_td_ok FALSE)
                set(_uc_td_why "the wheel holds no ${_uc_td_glob} (${_uc_td_count} matches)")
            endif()
        endif()
        if(_uc_td_ok)
            file(REMOVE_RECURSE "${ULTRAIM_TDLIB_DIR}")
            file(MAKE_DIRECTORY "${ULTRAIM_TDLIB_DIR}")
            list(GET _uc_td_found 0 _uc_td_lib)
            configure_file("${_uc_td_lib}" "${_uc_td_target}" COPYONLY)
            foreach(_uc_td_side ${_uc_td_sidecars})
                file(GLOB _uc_td_side_files LIST_DIRECTORIES false "${_uc_td_unpacked}/${_uc_td_side}")
                foreach(_uc_td_side_file ${_uc_td_side_files})
                    get_filename_component(_uc_td_side_name "${_uc_td_side_file}" NAME)
                    configure_file("${_uc_td_side_file}" "${ULTRAIM_TDLIB_DIR}/${_uc_td_side_name}" COPYONLY)
                endforeach()
            endforeach()
            file(WRITE "${ULTRAIM_TDLIB_DIR}/README.txt"
"TDLib ${ULTRAIM_TDLIB_VERSION} (https://github.com/tdlib/td, Boost Software License 1.0), the
Telegram client library ChatCube's Telegram provider loads at run time, as
built for the tdjson Python package (https://pypi.org/project/tdjson/) and
taken from its wheel by cmake/UltraCanvasTdlib.cmake. On macOS and Windows
the OpenSSL (Apache License 2.0 / OpenSSL License) and zlib (zlib License)
libraries it needs ship beside it. A libtdjson of your own goes in
ULTRAIM_TDJSON_PATH instead.
")
            file(WRITE "${_uc_td_stamp}" "${_uc_td_url}\n")
            file(REMOVE_RECURSE "${_uc_td_unpacked}")
            set(ULTRAIM_TDLIB_LIBRARY "${_uc_td_target}" CACHE INTERNAL "")
            set(ULTRAIM_TDLIB_STATUS "TDLib ${ULTRAIM_TDLIB_VERSION} fetched into Plugins/UltraIM/tdlib" CACHE INTERNAL "")
        else()
            set(ULTRAIM_TDLIB_LIBRARY "" CACHE INTERNAL "")
            set(ULTRAIM_TDLIB_STATUS "could not fetch TDLib ${ULTRAIM_TDLIB_VERSION} (${_uc_td_why}); found at run time or not at all - set ULTRAIM_TDJSON_FILE for an offline build" CACHE INTERNAL "")
            message(WARNING "The Telegram provider's TDLib could not be fetched: ${_uc_td_why}. ChatCube signs in to Telegram only where a libtdjson is found at run time (ULTRAIM_TDJSON_PATH); ULTRAIM_TDJSON_FILE ships one of your own, ULTRAIM_FETCH_TDLIB=OFF silences this.")
        endif()
    endif()
endif()
