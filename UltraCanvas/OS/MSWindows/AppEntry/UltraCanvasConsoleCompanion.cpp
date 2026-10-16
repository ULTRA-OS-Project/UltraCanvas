// OS/MSWindows/AppEntry/UltraCanvasConsoleCompanion.cpp
// The console half of an application that is a GUI program on Windows.
// Version: 1.0.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework
//
// Built as <App>.com beside <App>.exe by ultracanvas_windows_console_companion()
// (cmake/UltraCanvasWindowsGuiApp.cmake), for the applications whose command
// line prints - UltraDesktop --windows, UltraCleaner --scan, ultrafibu's
// commands. The .exe is a GUI program, so the Start menu, Explorer and the
// sign-in start it without a console window. Typed at a prompt, cmd and
// PowerShell find the .com first (PATHEXT lists .COM before .EXE), and this
// program starts the .exe with the same command line, waits for it and
// returns its exit code - so the prompt waits and a script sees the result,
// as it would for a console program. The same pattern as devenv.com.
//
// How the .exe reaches the console:
//   - All three standard handles a console (typed at a prompt): it is given
//     none, and attaches to this process's console before main()
//     (AttachParentConsole(), UltraCanvasWindowsAttachConsole.cpp) - output,
//     input and Ctrl+C all work as in a console program.
//   - Any of them redirected (`> file`, a pipe, a script capturing it): it
//     is handed this process's three handles and writes to them directly.
//
// Nothing of the framework is linked in, and the C++ runtime statically, so
// it needs no DLL and starts wherever the .exe does.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

#include <initializer_list>
#include <string>

namespace {

    HANDLE g_program = nullptr;
    bool g_sharesConsole = false;

    bool IsConsole(HANDLE handle) {
        DWORD mode = 0;
        return handle && handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode) != 0;
    }

    // Ctrl+C reaches every process attached to the console. Sharing it, the
    // program hears it too and decides for itself; given handles instead, it
    // has no console to hear it on, so it is ended here. This process keeps
    // waiting either way, so the prompt gets the program's exit code.
    BOOL WINAPI OnConsoleControl(DWORD) {
        if (!g_sharesConsole && g_program) TerminateProcess(g_program, STATUS_CONTROL_C_EXIT);
        return TRUE;
    }

    void Complain(const std::wstring& text) {
        HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
        if (!err || err == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        if (IsConsole(err)) {
            WriteConsoleW(err, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
            return;
        }
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                              nullptr, nullptr);
        if (bytes <= 0) return;
        std::string utf8(static_cast<size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), bytes, nullptr,
                            nullptr);
        WriteFile(err, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }

    std::wstring SystemMessage(DWORD error) {
        wchar_t* buffer = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
            error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        std::wstring text = length && buffer ? std::wstring(buffer, length) : L"error " + std::to_wstring(error);
        if (buffer) LocalFree(buffer);
        while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ')) text.pop_back();
        return text;
    }

    // This file's own path, its extension swapped for .exe.
    std::wstring ProgramBeside() {
        std::wstring self(MAX_PATH, L'\0');
        for (;;) {
            const DWORD length = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
            if (length == 0) return {};
            if (length < self.size()) {
                self.resize(length);
                break;
            }
            self.resize(self.size() * 2);
        }
        const size_t slash = self.find_last_of(L"\\/");
        const size_t dot = self.find_last_of(L'.');
        if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) self.resize(dot);
        return self + L".exe";
    }

    // The arguments as typed: the command line with its first token - this
    // program, quoted or not - taken off, the rest untouched.
    std::wstring ArgumentsAsTyped() {
        const wchar_t* line = GetCommandLineW();
        if (*line == L'"') {
            ++line;
            while (*line && *line != L'"') ++line;
            if (*line == L'"') ++line;
        } else {
            while (*line && *line != L' ' && *line != L'\t') ++line;
        }
        return line;
    }

} // namespace

int main() {
    const std::wstring program = ProgramBeside();
    std::wstring commandLine = L"\"" + program + L"\"" + ArgumentsAsTyped();

    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    g_sharesConsole = IsConsole(input) && IsConsole(output) && IsConsole(error);

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    BOOL inherit = FALSE;
    if (!g_sharesConsole) {
        for (HANDLE handle : {input, output, error}) {
            if (handle && handle != INVALID_HANDLE_VALUE) {
                SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
            }
        }
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input;
        startup.hStdOutput = output;
        startup.hStdError = error;
        inherit = TRUE;
    }

    SetConsoleCtrlHandler(OnConsoleControl, TRUE);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(program.c_str(), commandLine.data(), nullptr, nullptr, inherit, 0, nullptr, nullptr,
                        &startup, &process)) {
        const DWORD failure = GetLastError();
        Complain(L"Cannot start " + program + L": " + SystemMessage(failure) + L"\r\n");
        return 9009;   // what cmd answers for a program it cannot find
    }
    CloseHandle(process.hThread);
    g_program = process.hProcess;

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    g_program = nullptr;
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}
