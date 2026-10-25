// OS/MSWindows/AppEntry/UltraCanvasWindowsLibraryCheck.cpp
// Refuses to start a program against a framework DLL from another build.
// Version: 1.1.0 - every framework DLL that says which build it is, not only
//                  libUltraCanvas.dll: libVideoFX.dll, and VirtualFS as a DLL
// Version: 1.0.0
// Last Modified: 2026-10-25
// Author: UltraCanvas Framework
//
// A program is compiled against the framework's headers - the size of every
// class, the place of every member, the order of every virtual function - and
// runs against whatever DLL Windows finds first, which is the one beside the
// .exe. A folder that collects programs from different packages ends up with
// one copy of each DLL, from whichever package was copied last. While every
// function a program names is still in that DLL the loader says nothing, and
// the program crashes at its first call whose classes differ: "ChatCube
// crashed ... ChatCube.exe+0x1463D", which says nothing about why.
//
// This runs first in the program - before main(), before the program's own
// static objects - and, for each framework DLL the program has loaded,
// compares the build the program was compiled for with the one the DLL
// reports (a plain C function, so it can be asked whatever the classes look
// like): libUltraCanvas.dll its version, the others a build id
// (cmake/UltraCanvasLibraryBuildId.cmake). On a difference it says which DLL
// was loaded, from where, and what the program needs, and exits with
// 0xC0000139 (STATUS_ENTRYPOINT_NOT_FOUND, "a DLL of the wrong version").
//
// NOT compiled on its own. The build writes a wrapper per build tree
// (UltraCanvas/CMakeLists.txt: generated/UltraCanvasLibraryCheck.cpp) that
// defines ULTRACANVAS_LIBRARY_CHECKS - one
// ULTRACANVAS_LIBRARY_CHECK(dll, function, name, expected) per library - and
// includes this file, and every executable that links the framework on
// Windows compiles that wrapper (an INTERFACE source of the core). The
// versions are in that one small file only: a changelog edit recompiles it,
// not the program.

#ifndef ULTRACANVAS_LIBRARY_CHECKS
#error "Compile UltraCanvasLibraryCheck.cpp from the build directory, which lists the libraries"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace {

    // ULTRACANVAS_NO_ERROR_DIALOG=1: a child process or a test says it on
    // stderr instead of blocking on a box nobody clicks.
    bool DialogsSuppressed() {
        wchar_t value[8] = L"";
        const DWORD n = GetEnvironmentVariableW(L"ULTRACANVAS_NO_ERROR_DIALOG", value, 8);
        return n > 0 && n < 8 && value[0] != L'0';
    }

    struct LibraryCheck {
        const wchar_t* dll;        // "libUltraCanvas.dll"
        const char* function;      // what it answers with its build
        const wchar_t* name;       // "UltraCanvas"
        const char* expected;      // the build this program was compiled for
    };

    const LibraryCheck kLibraryChecks[] = {
#define ULTRACANVAS_LIBRARY_CHECK(dll, function, name, expected) { dll, function, name, expected },
        ULTRACANVAS_LIBRARY_CHECKS
#undef ULTRACANVAS_LIBRARY_CHECK
    };

    void RefuseMismatchedLibrary(const LibraryCheck& check, HMODULE library, const char* found) {
        wchar_t program[MAX_PATH * 2] = L"";
        GetModuleFileNameW(nullptr, program, MAX_PATH * 2);
        const wchar_t* programName = program;
        for (const wchar_t* c = program; *c; ++c)
            if (*c == L'\\' || *c == L'/') programName = c + 1;
        wchar_t libraryPath[MAX_PATH * 2] = L"";
        GetModuleFileNameW(library, libraryPath, MAX_PATH * 2);

        // "version 0.9.380", or what a library from before this check is.
        wchar_t foundVersion[96] = L"an older version, which does not report its number";
        if (found) {
            wchar_t number[80] = L"";
            MultiByteToWideChar(CP_UTF8, 0, found, -1, number, 80);
            number[79] = L'\0';
            std::swprintf(foundVersion, 96, L"version %ls", number);
        }
        wchar_t expected[80] = L"";
        MultiByteToWideChar(CP_UTF8, 0, check.expected, -1, expected, 80);
        expected[79] = L'\0';

        static wchar_t message[2048];
        std::swprintf(message, sizeof(message) / sizeof(message[0]),
                      L"%ls cannot start: the %ls library it loaded is from another build.\n\n"
                      L"Loaded: %ls (%ls)\n"
                      L"Needed: version %ls\n\n"
                      L"The folder holds DLLs that came with a different program or version. Keep each "
                      L"program together with the DLLs it was shipped with - unpack every package into a "
                      L"folder of its own, or replace this folder's DLLs with the ones from %ls's package.",
                      programName, check.name, libraryPath, foundVersion, expected, programName);
        if (DialogsSuppressed()) {
            std::fwprintf(stderr, L"%ls\n", message);
        } else {
            MessageBoxW(nullptr, message, programName, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        }
        ExitProcess(0xC0000139u);   // STATUS_ENTRYPOINT_NOT_FOUND: a DLL of the wrong version
    }

    void CheckLibraryVersions() {
        for (const LibraryCheck& check : kLibraryChecks) {
            HMODULE library = GetModuleHandleW(check.dll);
            if (!library) continue;   // a library this program does not load
            using BuildFn = const char* (*)();
            const auto build = reinterpret_cast<BuildFn>(
                    reinterpret_cast<void (*)()>(GetProcAddress(library, check.function)));
            const char* found = build ? build() : nullptr;
            if (found && std::strcmp(found, check.expected) == 0) continue;
            RefuseMismatchedLibrary(check, library, found);
        }
    }

} // namespace

// First of the program's initialisers: its own static objects may already
// call into the DLL. Priority 101 is the earliest a program may use.
#if defined(__GNUC__)
__attribute__((constructor(101))) static void UltraCanvasLibraryCheckAtStart() {
    CheckLibraryVersions();
}
#else
#pragma init_seg(lib)
static const int ultraCanvasLibraryCheckAtStart = (CheckLibraryVersions(), 0);
#endif
