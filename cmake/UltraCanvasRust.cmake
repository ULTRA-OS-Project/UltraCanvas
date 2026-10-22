# cmake/UltraCanvasRust.cmake
# Rust crates in the build through Corrosion: the toolchain pick on Windows
# and Corrosion itself, fetched once. Used by the Vectorizer plugin (VTracer)
# and the Matrix provider's Olm shim (vodozemac).
# Version: 0.1.0
# Last Modified: 2026-10-21
# Author: UltraCanvas Framework
include_guard(GLOBAL)

# ultracanvas_find_rust_toolchain(<ok-var> <why-var>)
#
# The Rust programs shipped in MSYS2's ".../usr/bin" tree are Cygwin/POSIX
# binaries: they use ':' as the PATH-list separator (Unix semantics). When
# cargo/rustc append their own "<target-dir>/release/deps" build directory to
# the dynamic-library path, the drive letter in a native path like
# "C:/msys64/..." is mistaken for a separator and the build aborts with:
#     error: failed to join paths from `$LD_LIBRARY_PATH` together
#     Caused by: path segment contains separator `:`
# This cannot be worked around via the environment (cargo appends that path
# itself, so even an empty LD_LIBRARY_PATH still yields a lone "C:/..." segment
# that fails to join). The cure is a *native* Windows Rust toolchain - rustup or
# the mingw-w64 package - which uses Windows path semantics and matches the
# MinGW C/C++ compiler CMake already uses. This steers Corrosion at one; when
# only the POSIX msys toolchain is present <ok-var> is FALSE and <why-var>
# holds actionable instructions, for the caller to fail with or to fall back on.
# Elsewhere (and when Rust_COMPILER is given) <ok-var> is TRUE.
function(ultracanvas_find_rust_toolchain ok_var why_var)
    set(${ok_var} TRUE PARENT_SCOPE)
    set(${why_var} "" PARENT_SCOPE)
    if(NOT WIN32 OR DEFINED Rust_COMPILER)
        return()
    endif()
    get_filename_component(_uc_cc_bin "${CMAKE_C_COMPILER}" DIRECTORY)  # e.g. .../mingw64/bin
    set(_uc_rust_hints
        "${_uc_cc_bin}"                  # mingw-w64-x86_64-rust: native gnu, next to the compiler
        "$ENV{USERPROFILE}/.cargo/bin"   # rustup (native Windows install)
        "$ENV{HOME}/.cargo/bin"          # rustup (installed from an MSYS2/MinGW HOME)
    )
    # Re-search every configure so installing the toolchain after a failed run
    # is picked up (find_program otherwise caches a NOTFOUND result).
    unset(_UC_NATIVE_RUSTC CACHE)
    unset(_UC_NATIVE_CARGO CACHE)
    find_program(_UC_NATIVE_RUSTC rustc HINTS ${_uc_rust_hints} NO_DEFAULT_PATH)
    find_program(_UC_NATIVE_CARGO cargo HINTS ${_uc_rust_hints} NO_DEFAULT_PATH)
    if(_UC_NATIVE_RUSTC AND _UC_NATIVE_CARGO)
        set(Rust_COMPILER "${_UC_NATIVE_RUSTC}" CACHE FILEPATH "rustc used by Corrosion" FORCE)
        set(Rust_CARGO    "${_UC_NATIVE_CARGO}" CACHE FILEPATH "cargo used by Corrosion" FORCE)
        message(STATUS "  Rust: native toolchain -> ${_UC_NATIVE_CARGO}")
    else()
        set(${ok_var} FALSE PARENT_SCOPE)
        string(CONCAT _uc_why
            "It must match the Clang (CLANG64) compiler, but only the MSYS2 POSIX rust (in .../usr/bin) "
            "appears to be installed. That toolchain aborts the build with 'failed to join paths from "
            "$LD_LIBRARY_PATH' because it treats the ':' in Windows paths as a path separator.\n"
            "Install a native toolchain, then re-run CMake:\n"
            "  * from the MSYS2 CLANG64 shell:  pacman -S mingw-w64-clang-x86_64-rust\n"
            "  * or via rustup:  rustup toolchain install stable-x86_64-pc-windows-gnullvm\n"
            "                    rustup default        stable-x86_64-pc-windows-gnullvm\n"
            "If it lives elsewhere, point CMake at it:  -DRust_COMPILER=<path-to-rustc.exe>")
        set(${why_var} "${_uc_why}" PARENT_SCOPE)
    endif()
endfunction()

# ultracanvas_fetch_corrosion() - makes corrosion_import_crate() available; a
# macro, so Corrosion's variables land in the caller's scope. Calling it again
# is a no-op.
macro(ultracanvas_fetch_corrosion)
    include(FetchContent)
    FetchContent_Declare(
        Corrosion
        GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
        # v0.6.0+ is required for Windows-on-ARM: earlier Corrosion does not know
        # the *-windows-gnullvm ABI (CLANGARM64's rust target) and fails configure
        # with "Unknown windows environment - Can't determine implib name".
        GIT_TAG        v0.6.1
    )
    FetchContent_MakeAvailable(Corrosion)
endmacro()
