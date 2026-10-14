# cmake/UltraCanvasWasmSysroot.cmake
# Put the wasm sysroot on CMAKE_FIND_ROOT_PATH under Emscripten.
#
# The sysroot that UltraCanvas/OS/WASM/build-wasm-sysroot.sh cross-compiles
# (cairo, pango, glib, libvips, zlib, ...) is handed to a build only through
# PKG_CONFIG_PATH (UltraCanvas/OS/WASM/README.md), which serves every
# pkg_check_modules() in the tree. A find_package() or find_library() is
# different: the Emscripten toolchain sets CMAKE_FIND_ROOT_PATH_MODE_* to ONLY
# and CMAKE_FIND_ROOT_PATH to Emscripten's own sysroot, so find_package(ZLIB)
# - the Models plugin's FBX and .blend readers, VirtualFS, the core's
# VirtualFS bridge - looked straight past the zlib the sysroot holds, and
# the FBX reader was off in every WebAssembly build. The .pc files know
# where they were installed: take the prefix from zlib's and add it to the
# search root.
#
# Included from the root CMakeLists.txt (before VirtualFS is added) and from
# UltraCanvas/CMakeLists.txt (for a build that adds the library on its own,
# like OS/WASM/demo); the guard makes the second include a no-op.
if(EMSCRIPTEN)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(_uc_wasm_sysroot_zlib QUIET zlib)
        if(_uc_wasm_sysroot_zlib_FOUND)
            pkg_get_variable(_uc_wasm_sysroot_prefix zlib prefix)
            if(_uc_wasm_sysroot_prefix AND NOT "${_uc_wasm_sysroot_prefix}" IN_LIST CMAKE_FIND_ROOT_PATH)
                list(APPEND CMAKE_FIND_ROOT_PATH "${_uc_wasm_sysroot_prefix}")
                message(STATUS "WebAssembly: wasm sysroot ${_uc_wasm_sysroot_prefix} added to CMAKE_FIND_ROOT_PATH")
            endif()
            unset(_uc_wasm_sysroot_prefix)
        endif()
    endif()
endif()
