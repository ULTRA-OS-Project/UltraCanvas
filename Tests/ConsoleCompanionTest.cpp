// Tests/ConsoleCompanionTest.cpp
// The console companion (OS/MSWindows/AppEntry/UltraCanvasConsoleCompanion.cpp)
// on real Windows, with ConsoleCompanionProbe.exe / .com built beside this
// test by ultracanvas_windows_console_companion():
//
//   - the .exe is a GUI program and the .com a console program;
//   - handed redirected handles, as a script capturing the output does, the
//     .com passes the arguments on, keeps stdout and stderr apart and
//     returns the program's exit code;
//   - on a console, as typed at a prompt: this test starts itself again in
//     a console of its own (hidden), runs the .com there, and through that
//     console types a line the program reads, reads the screen back for
//     what the program printed, and presses Ctrl+C on a program that waits -
//     which must end it, with the companion handing back its exit code.
//
// Windows Release builds only: the companion exists nowhere else.
// Version: 1.0.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const std::string& what) {
    ++g_checks;
    std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", what.c_str());
    std::fflush(stdout);
    if (!condition) ++g_failures;
}

std::wstring OwnPath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring FolderOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash);
}

// A file's first `limit` bytes, or all of it.
std::string ReadFileBytes(const std::wstring& path, DWORD limit = 1u << 20) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string bytes(limit, '\0');
    DWORD got = 0;
    if (!ReadFile(file, bytes.data(), limit, &got, nullptr)) got = 0;
    CloseHandle(file);
    bytes.resize(got);
    return bytes;
}

void WriteFileBytes(const std::wstring& path, const std::string& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
}

// The PE header's Subsystem field: 2 = Windows GUI, 3 = console.
int Subsystem(const std::wstring& path) {
    const std::string header = ReadFileBytes(path, 4096);
    if (header.size() < 0x40) return 0;
    uint32_t pe = 0;
    std::memcpy(&pe, header.data() + 0x3C, sizeof(pe));
    if (pe + 94 > header.size() || std::memcmp(header.data() + pe, "PE\0\0", 4) != 0) return 0;
    uint16_t subsystem = 0;
    std::memcpy(&subsystem, header.data() + pe + 92, sizeof(subsystem));
    return subsystem;
}

std::string ReadAll(HANDLE pipe) {
    std::string text;
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &got, nullptr) && got > 0) text.append(buffer, got);
    return text;
}

// ----- inside a console of its own (--inside-console) -----

std::vector<std::string> g_results;

void Note(bool condition, const std::string& what) {
    g_results.push_back(std::string(condition ? "PASS " : "FAIL ") + what);
}

// Everything the console shows.
std::wstring Screen() {
    HANDLE out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, 0, nullptr);
    if (out == INVALID_HANDLE_VALUE) return {};
    CONSOLE_SCREEN_BUFFER_INFO info = {};
    std::wstring text;
    if (GetConsoleScreenBufferInfo(out, &info)) {
        const DWORD cells = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwCursorPosition.Y + 1);
        text.resize(cells);
        DWORD read = 0;
        ReadConsoleOutputCharacterW(out, text.data(), cells, COORD{0, 0}, &read);
        text.resize(read);
    }
    CloseHandle(out);
    return text;
}

bool ScreenShowsWithin(const std::wstring& what, DWORD ms) {
    const ULONGLONG until = GetTickCount64() + ms;
    while (Screen().find(what) == std::wstring::npos) {
        if (GetTickCount64() >= until) return false;
        Sleep(50);
    }
    return true;
}

// Typed into the console, as a user's keys are.
void Type(const std::wstring& text) {
    HANDLE in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, 0, nullptr);
    if (in == INVALID_HANDLE_VALUE) return;
    std::vector<INPUT_RECORD> records;
    auto key = [&records](wchar_t c, WORD vk) {
        for (BOOL down : {TRUE, FALSE}) {
            INPUT_RECORD record = {};
            record.EventType = KEY_EVENT;
            record.Event.KeyEvent.bKeyDown = down;
            record.Event.KeyEvent.wRepeatCount = 1;
            record.Event.KeyEvent.wVirtualKeyCode = vk;
            record.Event.KeyEvent.uChar.UnicodeChar = c;
            records.push_back(record);
        }
    };
    for (wchar_t c : text) key(c, static_cast<WORD>(LOBYTE(VkKeyScanW(c))));
    key(L'\r', VK_RETURN);
    DWORD written = 0;
    WriteConsoleInputW(in, records.data(), static_cast<DWORD>(records.size()), &written);
    CloseHandle(in);
}

// The companion, started as a prompt starts a program: on this console,
// its standard handles this console's.
HANDLE StartCompanion(const std::wstring& companion, const std::wstring& arguments) {
    std::wstring command = L"\"" + companion + L"\" " + arguments;
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(companion.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                        &process)) {
        return nullptr;
    }
    CloseHandle(process.hThread);
    return process.hProcess;
}

DWORD WaitExit(HANDLE process, DWORD ms) {
    if (WaitForSingleObject(process, ms) != WAIT_OBJECT_0) {
        TerminateProcess(process, 0xDEAD);
        WaitForSingleObject(process, 5000);
        return 0xDEAD;
    }
    DWORD code = 0;
    GetExitCodeProcess(process, &code);
    return code;
}

BOOL WINAPI SurviveControlC(DWORD) { return TRUE; }

int InsideConsole(const std::wstring& companion, const std::wstring& resultFile) {
    // Ctrl+C reaches the programs started below - whatever this process
    // inherited - and this process outlives it.
    SetConsoleCtrlHandler(nullptr, FALSE);
    SetConsoleCtrlHandler(SurviveControlC, TRUE);

    HANDLE echo = StartCompanion(companion, L"echo");
    Note(echo != nullptr, "the companion starts on a console");
    if (echo) {
        const bool asked = ScreenShowsWithin(L"probe: ready for a line", 20000);
        Note(asked, "the program prints into the companion's console");
        if (asked) Type(L"typed at the console");
        const DWORD code = WaitExit(echo, 20000);
        CloseHandle(echo);
        Note(code == 0, "the program reads the typed line and exits (code " + std::to_string(code) + ")");
        Note(ScreenShowsWithin(L"probe: echo typed at the console", 2000), "and prints it back on the console");
    }

    HANDLE waiting = StartCompanion(companion, L"wait");
    if (waiting) {
        const bool started = ScreenShowsWithin(L"probe: waiting", 20000);
        Note(started, "a program that waits is waiting");
        if (started) GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        const DWORD code = WaitExit(waiting, 20000);
        CloseHandle(waiting);
        Note(code == 130, "Ctrl+C reaches the program, and the companion returns its exit code (" +
                              std::to_string(code) + ")");
        const std::wstring screen = Screen();
        Note(screen.find(L"probe: interrupted") != std::wstring::npos &&
                 screen.find(L"probe: not interrupted") == std::wstring::npos,
             "it was interrupted, not done waiting");
    }

    std::string report;
    int failures = 0;
    for (const std::string& line : g_results) {
        report += line + "\n";
        if (line.rfind("FAIL", 0) == 0) ++failures;
    }
    WriteFileBytes(resultFile, report);
    return failures;
}

} // namespace

int main() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc == 4 && std::wstring(argv[1]) == L"--inside-console") return InsideConsole(argv[2], argv[3]);

    const std::wstring folder = FolderOf(OwnPath());
    const std::wstring program = folder + L"\\ConsoleCompanionProbe.exe";
    const std::wstring companion = folder + L"\\ConsoleCompanionProbe.com";

    // ===== THE PAIR =====
    Check(Subsystem(program) == 2, "ConsoleCompanionProbe.exe is a GUI program");
    Check(Subsystem(companion) == 3, "ConsoleCompanionProbe.com is its console companion");

    // ===== REDIRECTED, AS A SCRIPT CAPTURING THE OUTPUT =====
    {
        SECURITY_ATTRIBUTES inheritable = {sizeof(inheritable), nullptr, TRUE};
        HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr;
        CreatePipe(&outRead, &outWrite, &inheritable, 0);
        CreatePipe(&errRead, &errWrite, &inheritable, 0);
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = outWrite;
        startup.hStdError = errWrite;
        std::wstring command = L"\"" + companion + L"\" print hello \"two words\"";
        PROCESS_INFORMATION process = {};
        const bool started = CreateProcessW(companion.c_str(), command.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE;
        CloseHandle(outWrite);
        CloseHandle(errWrite);
        Check(started, "the companion starts with redirected handles");
        if (started) {
            const std::string out = ReadAll(outRead);
            const std::string err = ReadAll(errRead);
            const DWORD code = WaitExit(process.hProcess, 30000);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            Check(out.find("probe: hello two words") != std::string::npos,
                  "the arguments reach the program, quoted words whole: " + out.substr(0, out.find('\n')));
            Check(err.find("probe: to stderr") != std::string::npos && out.find("to stderr") == std::string::npos,
                  "stderr stays stderr");
            Check(code == 7, "the program's exit code comes back (" + std::to_string(code) + ")");
        }
        CloseHandle(outRead);
        CloseHandle(errRead);
    }

    // ===== ON A CONSOLE, AS TYPED AT A PROMPT =====
    {
        wchar_t temp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temp);
        const std::wstring results = std::wstring(temp) + L"ConsoleCompanionTest-" +
                                     std::to_wstring(GetCurrentProcessId()) + L".txt";
        DeleteFileW(results.c_str());
        std::wstring command = L"\"" + OwnPath() + L"\" --inside-console \"" + companion + L"\" \"" + results + L"\"";
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process = {};
        const bool started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE,
                                            nullptr, nullptr, &startup, &process) != FALSE;
        Check(started, "a console of its own for the prompt's case");
        if (started) {
            const DWORD code = WaitExit(process.hProcess, 90000);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            const std::string report = ReadFileBytes(results);
            int lines = 0;
            size_t start = 0;
            while (start < report.size()) {
                size_t end = report.find('\n', start);
                if (end == std::string::npos) end = report.size();
                const std::string line = report.substr(start, end - start);
                start = end + 1;
                if (line.size() < 5) continue;
                ++lines;
                Check(line.rfind("PASS ", 0) == 0, "on a console: " + line.substr(5));
            }
            Check(lines > 0 && code != 0xDEAD, "the console's checks ran (" + std::to_string(lines) + ")");
            DeleteFileW(results.c_str());
        }
    }

    LocalFree(argv);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
