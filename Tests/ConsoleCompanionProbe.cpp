// Tests/ConsoleCompanionProbe.cpp
// The GUI program ConsoleCompanionTest drives through its console companion,
// ConsoleCompanionProbe.com - both made by ultracanvas_windows_console_companion()
// as the applications' are, so the test exercises the real thing:
// the .exe is a GUI program, attached to the prompt's console before main()
// by UltraCanvasWindowsAttachConsole.cpp.
//
//   print <words>   prints them, and a line to stderr; exit code 7
//   echo            asks for a line, reads it from stdin and prints it back
//   wait            prints that it waits, then waits half a minute for a
//                   Ctrl+C, which it answers with "interrupted" and exit
//                   code 130 (its own handler: Windows' default one exits
//                   with STATUS_CONTROL_C_EXIT, Wine's with 0); 0 when none
//                   came
// Version: 1.0.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {
    BOOL WINAPI OnControlC(DWORD type) {
        if (type != CTRL_C_EVENT) return FALSE;
        std::printf("probe: interrupted\n");
        std::fflush(stdout);
        ExitProcess(130);
    }
} // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "print") {
        std::string words;
        for (int i = 2; i < argc; ++i) words += (i > 2 ? " " : "") + std::string(argv[i]);
        std::printf("probe: %s\n", words.c_str());
        std::fprintf(stderr, "probe: to stderr\n");
        std::fflush(stdout);
        std::fflush(stderr);
        return 7;
    }
    if (mode == "echo") {
        std::printf("probe: ready for a line\n");
        std::fflush(stdout);
        char line[256] = {};
        if (!std::fgets(line, sizeof(line), stdin)) return 3;
        line[std::strcspn(line, "\r\n")] = '\0';
        std::printf("probe: echo %s\n", line);
        std::fflush(stdout);
        return 0;
    }
    if (mode == "wait") {
        SetConsoleCtrlHandler(OnControlC, TRUE);
        std::printf("probe: waiting\n");
        std::fflush(stdout);
        Sleep(30000);
        std::printf("probe: not interrupted\n");
        std::fflush(stdout);
        return 0;
    }
    std::fprintf(stderr, "probe: print <words> | echo | wait\n");
    return 2;
}
