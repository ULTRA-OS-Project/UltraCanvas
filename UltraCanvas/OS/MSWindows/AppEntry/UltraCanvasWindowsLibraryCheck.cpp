// OS/MSWindows/AppEntry/UltraCanvasWindowsLibraryCheck.cpp
// Refuses to start a program against a framework DLL of another version.
// Version: 1.0.0
// Last Modified: 2026-10-25
// Author: UltraCanvas Framework
//
// A program is compiled against the framework's headers - the size of every
// class, the place of every member, the order of every virtual function - and
// runs against whatever libUltraCanvas.dll Windows finds first, which is the
// one beside the .exe. A folder that collects programs from different builds
// ends up with one DLL for all of them, from whichever was copied last. While
// every function a program names is still in that DLL the loader says
// nothing, and the program crashes at its first call whose classes differ:
// "ChatCube crashed ... ChatCube.exe+0x1463D", which says nothing about why.
//
// This runs first in the program - before main(), before the program's own
// static objects - and compares the version the program was built for with
// the one the loaded DLL reports (UltraCanvas_LibraryVersion, plain C, so it
// can be asked whatever the classes look like). On a difference it says which
// DLL was loaded, from where, and what the program needs, and exits with
// 0xC0000139 (STATUS_ENTRYPOINT_NOT_FOUND, "a DLL of the wrong version").
//
// NOT compiled on its own. The build writes a wrapper per build tree
// (UltraCanvas/CMakeLists.txt: UltraCanvasLibraryCheck.cpp in the binary
// directory) that defines ULTRACANVAS_EXPECTED_VERSION and
// ULTRACANVAS_CORE_DLL_NAME and includes this file, and every executable that
// links the shared core on Windows compiles that wrapper (an INTERFACE source
// of the core). The version is in that one small file only: a changelog edit
// recompiles it, not the program.

#ifndef ULTRACANVAS_EXPECTED_VERSION
#error "Compile UltraCanvasLibraryCheck.cpp from the build directory, which defines the version"
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

    void RefuseMismatchedLibrary(HMODULE library, const char* found) {
        wchar_t program[MAX_PATH * 2] = L"";
        GetModuleFileNameW(nullptr, program, MAX_PATH * 2);
        const wchar_t* programName = program;
        for (const wchar_t* c = program; *c; ++c)
            if (*c == L'\\' || *c == L'/') programName = c + 1;
        wchar_t libraryPath[MAX_PATH * 2] = L"";
        GetModuleFileNameW(library, libraryPath, MAX_PATH * 2);

        // "version 0.9.380", or what a library from before this check is.
        wchar_t foundVersion[80] = L"an older version, which does not report its number";
        if (found) {
            wchar_t number[64] = L"";
            MultiByteToWideChar(CP_UTF8, 0, found, -1, number, 64);
            number[63] = L'\0';
            std::swprintf(foundVersion, 80, L"version %ls", number);
        }
        wchar_t expected[64] = L"";
        MultiByteToWideChar(CP_UTF8, 0, ULTRACANVAS_EXPECTED_VERSION, -1, expected, 64);
        expected[63] = L'\0';

        static wchar_t message[2048];
        std::swprintf(message, sizeof(message) / sizeof(message[0]),
                      L"%ls cannot start: the UltraCanvas library it loaded is from another build.\n\n"
                      L"Loaded: %ls (%ls)\n"
                      L"Needed: version %ls\n\n"
                      L"The folder holds DLLs that came with a different program or version. Keep each "
                      L"program together with the DLLs it was shipped with - unpack every package into a "
                      L"folder of its own, or replace this folder's DLLs with the ones from %ls's package.",
                      programName, libraryPath, foundVersion, expected,
                      programName);
        if (DialogsSuppressed()) {
            std::fwprintf(stderr, L"%ls\n", message);
        } else {
            MessageBoxW(nullptr, message, programName, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        }
        ExitProcess(0xC0000139u);   // STATUS_ENTRYPOINT_NOT_FOUND: a DLL of the wrong version
    }

    void CheckLibraryVersion() {
        HMODULE library = GetModuleHandleW(ULTRACANVAS_CORE_DLL_NAME);
        if (!library) return;   // a program that does not load the shared core
        using VersionFn = const char* (*)();
        const auto version = reinterpret_cast<VersionFn>(
                reinterpret_cast<void (*)()>(GetProcAddress(library, "UltraCanvas_LibraryVersion")));
        const char* found = version ? version() : nullptr;
        if (found && std::strcmp(found, ULTRACANVAS_EXPECTED_VERSION) == 0) return;
        RefuseMismatchedLibrary(library, found);
    }

} // namespace

// First of the program's initialisers: its own static objects may already
// call into the DLL. Priority 101 is the earliest a program may use.
#if defined(__GNUC__)
__attribute__((constructor(101))) static void UltraCanvasLibraryCheckAtStart() {
    CheckLibraryVersion();
}
#else
#pragma init_seg(lib)
static const int ultraCanvasLibraryCheckAtStart = (CheckLibraryVersion(), 0);
#endif
