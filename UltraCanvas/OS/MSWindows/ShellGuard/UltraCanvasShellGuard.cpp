// OS/MSWindows/ShellGuard/UltraCanvasShellGuard.cpp
// UltraCanvasShellGuard - what an account's sign-in starts when an
// UltraCanvas desktop runs instead of Explorer
// (UltraCanvasDesktopShell::ReplaceSystemDesktop with
// DesktopReplacement::Instead writes it into the account's shell setting):
//
//   "...\UltraCanvasShellGuard.exe" "...\UltraDesktop.exe" --shell
//
// Its one job is that the screen is never left empty. Between the desktop
// and the sign-in, it
//   - starts the desktop (the rest of its command line, verbatim);
//   - starts it again when it crashes, and starts Explorer for the session
//     when it fails three times within a minute or cannot start at all;
//   - starts Explorer when the desktop exits on its own - the user switched
//     back to the Windows desktop, or closed it; not when the session is
//     ending, which closes the desktop too - and, should the desktop
//     hang, when the account's shell setting stops naming the guard (what
//     "UltraDesktop --restore-desktop" from Task Manager's "Run new task"
//     does), after five seconds' grace;
//   - does for the session what Explorer would have: tells the sign-in
//     screen the desktop is up (ShellDesktopSwitchEvent), hides minimized
//     windows instead of parking them at the screen's corner (ARW_HIDE), and
//     starts the programs that start with Windows - the Run and RunOnce keys
//     and the Startup folders, except those switched off in Task Manager.
//
// It is a program of its own, not a mode of the desktop, so a desktop that
// cannot even load - a DLL missing after a half-finished update - still gets
// its fallback; it needs nothing but Windows itself (the C++ runtime is
// linked in). Started any other way - by hand, by a test, while Explorer is
// the desktop - it only starts and restarts the program: no Explorer, no
// startup programs, no session settings.
//
// What it did goes to %LOCALAPPDATA%\UltraCanvas\ShellGuard.log, so "why did
// I get the Windows desktop?" has an answer.
//
// Exit codes: 0 the desktop exited normally (or Explorer took over), 1 it
// could not be started, 2 no program was named, 3 it failed three times in a
// minute.
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif

#include "UltraCanvasDataFolder.h"   // UltraCanvasLocalDataFolderRoot
#include "UltraCanvasPathUtf8.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>   // SHGetKnownFolderPath

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <string>
#include <vector>

namespace {

    namespace fs = std::filesystem;

    constexpr wchar_t kWinlogonKey[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kRunOnceKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
    constexpr wchar_t kPolicyRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run";
    constexpr wchar_t kApprovedKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved";
    // The value that starts a desktop beside Explorer: never also started here.
    constexpr wchar_t kBesideRunValue[] = L"UltraCanvas Desktop";

    constexpr int kFailuresAllowed = 3;
    constexpr ULONGLONG kFailureWindowMs = 60 * 1000;
    constexpr DWORD kRestartDelayMs = 1000;
    constexpr DWORD kGraceMs = 5000;

    // ===== THE LOG =====

    std::wstring g_logPath;

    void Log(const std::string& line) {
        if (g_logPath.empty()) return;
        HANDLE file = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        SYSTEMTIME now;
        GetLocalTime(&now);
        char stamp[48];
        std::snprintf(stamp, sizeof stamp, "%04u-%02u-%02u %02u:%02u:%02u ", now.wYear, now.wMonth, now.wDay,
                      now.wHour, now.wMinute, now.wSecond);
        const std::string text = stamp + line + "\r\n";
        DWORD written = 0;
        WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        CloseHandle(file);
    }

    std::string Utf8(const std::wstring& wide) {
        return UltraCanvas::PathUtf8Detail::Utf16ToUtf8(wide);
    }

    // %LOCALAPPDATA%\UltraCanvas\ShellGuard.log, the previous run's kept as
    // ShellGuard.old.log once it passes 256 KB.
    void OpenLog() {
        std::error_code ec;
        const fs::path root = UltraCanvas::UltraCanvasLocalDataFolderRoot();
        if (root.empty()) return;
        const fs::path dir = root / L"UltraCanvas";
        fs::create_directories(dir, ec);
        const fs::path log = dir / L"ShellGuard.log";
        if (fs::exists(log, ec) && fs::file_size(log, ec) > 256 * 1024) {
            fs::rename(log, dir / L"ShellGuard.old.log", ec);
        }
        g_logPath = log.native();
    }

    // ===== COMMAND LINES =====

    // The rest of a command line after its first word: the desktop and its
    // arguments, exactly as the shell setting quoted them.
    std::wstring AfterFirstWord(const wchar_t* commandLine) {
        const wchar_t* at = commandLine;
        while (*at == L' ' || *at == L'\t') ++at;
        if (*at == L'"') {
            ++at;
            while (*at && *at != L'"') ++at;
            if (*at == L'"') ++at;
        } else {
            while (*at && *at != L' ' && *at != L'\t') ++at;
        }
        while (*at == L' ' || *at == L'\t') ++at;
        return at;
    }

    std::wstring FirstWord(const std::wstring& command) {
        size_t at = command.find_first_not_of(L" \t");
        if (at == std::wstring::npos) return {};
        if (command[at] == L'"') {
            const size_t end = command.find(L'"', at + 1);
            return command.substr(at + 1, end == std::wstring::npos ? std::wstring::npos : end - at - 1);
        }
        const size_t end = command.find_first_of(L" \t", at);
        return command.substr(at, end == std::wstring::npos ? std::wstring::npos : end - at);
    }

    std::wstring FolderOf(const std::wstring& path) {
        const size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
    }

    // The folder of a program that exists - "" for a first word that is
    // only part of an unquoted path with spaces, which the command still
    // runs from (CreateProcess tries the longer readings itself).
    std::wstring FolderOfExistingFile(const std::wstring& path) {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return {};
        return FolderOf(path);
    }

    // A path as the file system knows it: full, long names, so that two
    // spellings of one file compare equal.
    std::wstring CanonicalPath(const std::wstring& path) {
        wchar_t full[MAX_PATH * 4];
        DWORD n = GetFullPathNameW(path.c_str(), MAX_PATH * 4, full, nullptr);
        std::wstring result = (n > 0 && n < MAX_PATH * 4) ? std::wstring(full, n) : path;
        wchar_t longer[MAX_PATH * 4];
        n = GetLongPathNameW(result.c_str(), longer, MAX_PATH * 4);
        if (n > 0 && n < MAX_PATH * 4) result.assign(longer, n);
        return result;
    }

    bool SamePath(const std::wstring& a, const std::wstring& b) {
        return CompareStringOrdinal(CanonicalPath(a).c_str(), -1, CanonicalPath(b).c_str(), -1, TRUE) == CSTR_EQUAL;
    }

    std::wstring ThisProgram() {
        wchar_t self[MAX_PATH * 4];
        const DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH * 4);
        return (n > 0 && n < MAX_PATH * 4) ? std::wstring(self, n) : std::wstring();
    }

    // ===== THE REGISTRY =====

    bool ReadText(HKEY root, const wchar_t* subKey, const wchar_t* name, std::wstring& out) {
        DWORD size = 0;
        if (RegGetValueW(root, subKey, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, nullptr,
                         &size) != ERROR_SUCCESS)
            return false;
        std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
        if (RegGetValueW(root, subKey, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr,
                         buffer.data(), &size) != ERROR_SUCCESS)
            return false;
        buffer.resize(wcsnlen(buffer.c_str(), buffer.size()));
        out = buffer;
        return true;
    }

    // Whether the account's shell setting starts this very program - the
    // sign-in started it as the session's shell, and the user has not
    // switched back since.
    bool ShellSettingNamesUs(const std::wstring& self) {
        std::wstring shell;
        if (!ReadText(HKEY_CURRENT_USER, kWinlogonKey, L"Shell", shell)) return false;
        const std::wstring program = FirstWord(shell);
        return !program.empty() && SamePath(program, self);
    }

    // The desktop is still to start at sign-in beside Explorer: the user
    // chose that instead, which this session's desktop outlives.
    bool StartsBesideExplorer() {
        DWORD size = 0;
        return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kBesideRunValue, RRF_RT_REG_SZ, nullptr, nullptr, &size) ==
               ERROR_SUCCESS;
    }

    bool ExplorerTaskbarRunning() {
        return FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
    }

    struct NamedCommand {
        std::wstring name;
        std::wstring command;
    };

    std::vector<NamedCommand> ReadCommands(HKEY root, const wchar_t* subKey, REGSAM view = 0) {
        std::vector<NamedCommand> commands;
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, subKey, 0, KEY_READ | view, &key) != ERROR_SUCCESS) return commands;
        for (DWORD index = 0;; ++index) {
            wchar_t name[512];
            DWORD nameLength = static_cast<DWORD>(sizeof(name) / sizeof(name[0]));
            DWORD type = 0;
            DWORD bytes = 0;
            const LSTATUS status = RegEnumValueW(key, index, name, &nameLength, nullptr, &type, nullptr, &bytes);
            if (status == ERROR_NO_MORE_ITEMS) break;
            if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) continue;
            std::wstring value(bytes / sizeof(wchar_t) + 1, L'\0');
            nameLength = static_cast<DWORD>(sizeof(name) / sizeof(name[0]));
            DWORD size = bytes;
            if (RegEnumValueW(key, index, name, &nameLength, nullptr, &type, reinterpret_cast<BYTE*>(value.data()),
                              &size) != ERROR_SUCCESS)
                continue;
            value.resize(wcsnlen(value.c_str(), value.size()));
            if (value.empty()) continue;
            commands.push_back({std::wstring(name, nameLength), value});
        }
        RegCloseKey(key);
        return commands;
    }

    // Task Manager's "Disabled": a value named after the entry under
    // StartupApproved\<list>, in the user's hive or the machine's, whose
    // first byte is odd.
    bool SwitchedOffInTaskManager(const wchar_t* list, const std::wstring& name) {
        const std::wstring subKey = std::wstring(kApprovedKey) + L"\\" + list;
        for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
            BYTE data[16] = {};
            DWORD size = sizeof(data);
            const LSTATUS status = RegGetValueW(root, subKey.c_str(), name.c_str(), RRF_RT_REG_BINARY, nullptr, data,
                                                &size);
            if ((status == ERROR_SUCCESS || status == ERROR_MORE_DATA) && size > 0 && (data[0] & 1)) return true;
        }
        return false;
    }

    // ===== STARTING THINGS =====

    std::wstring Expanded(const std::wstring& text) {
        const DWORD needed = ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
        if (needed == 0) return text;
        std::wstring buffer(needed, L'\0');
        const DWORD n = ExpandEnvironmentStringsW(text.c_str(), buffer.data(), needed);
        if (n == 0 || n > needed) return text;
        buffer.resize(n - 1);
        return buffer;
    }

    // A startup entry's command: a program and its arguments, or - when it
    // names a document, a script, a shortcut - whatever opens that.
    void StartCommand(const std::wstring& rawCommand, const std::string& what) {
        std::wstring command = Expanded(rawCommand);
        std::vector<wchar_t> buffer(command.begin(), command.end());
        buffer.push_back(L'\0');
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process = {};
        const std::wstring folder = FolderOfExistingFile(FirstWord(command));
        if (CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, 0, nullptr,
                           folder.empty() ? nullptr : folder.c_str(), &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            Log("started " + what);
            return;
        }
        const DWORD failure = GetLastError();
        const std::wstring file = FirstWord(command);
        std::wstring parameters = AfterFirstWord(command.c_str());
        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.lpFile = file.c_str();
        info.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
        info.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&info)) {
            Log("opened " + what);
        } else {
            Log("could not start " + what + " (error " + std::to_string(failure) + ")");
        }
    }

    void OpenStartupFile(const fs::path& file) {
        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        const std::wstring path = file.native();
        info.lpFile = path.c_str();
        info.nShow = SW_SHOWNORMAL;
        const std::string what = UltraCanvas::PathToUtf8(file.filename());
        if (ShellExecuteExW(&info)) Log("opened " + what + " from the Startup folder");
        else Log("could not open " + what + " from the Startup folder (error " + std::to_string(GetLastError()) + ")");
    }

    void RunList(HKEY root, const wchar_t* subKey, const wchar_t* approvalList, const char* where, REGSAM view = 0) {
        for (const NamedCommand& entry : ReadCommands(root, subKey, view)) {
            if (_wcsicmp(entry.name.c_str(), kBesideRunValue) == 0) continue;
            const std::string what = "\"" + Utf8(entry.name) + "\" (" + where + ")";
            if (approvalList && SwitchedOffInTaskManager(approvalList, entry.name)) {
                Log("left " + what + ": switched off in Task Manager");
                continue;
            }
            StartCommand(entry.command, what);
        }
    }

    // RunOnce: each entry removed as it is started - before, unless its name
    // starts with "!", which asks for after - so it runs once even if the
    // start fails.
    void RunOnceList() {
        for (const NamedCommand& entry : ReadCommands(HKEY_CURRENT_USER, kRunOnceKey)) {
            const bool removeAfter = !entry.name.empty() && entry.name[0] == L'!';
            if (!removeAfter) RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunOnceKey, entry.name.c_str());
            StartCommand(entry.command, "\"" + Utf8(entry.name) + "\" (RunOnce)");
            if (removeAfter) RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunOnceKey, entry.name.c_str());
        }
    }

    void RunStartupFolder(REFKNOWNFOLDERID folderId) {
        PWSTR raw = nullptr;
        if (FAILED(SHGetKnownFolderPath(folderId, 0, nullptr, &raw)) || !raw) return;
        const fs::path folder(raw);
        CoTaskMemFree(raw);
        std::error_code ec;
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const fs::path file = it->path();
            const DWORD attributes = GetFileAttributesW(file.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                continue;   // desktop.ini and its kind
            if (SwitchedOffInTaskManager(L"StartupFolder", file.filename().native())) {
                Log("left " + UltraCanvas::PathToUtf8(file.filename()) + ": switched off in Task Manager");
                continue;
            }
            OpenStartupFile(file);
        }
    }

    // What Explorer starts at sign-in, in the order it does.
    void RunStartupPrograms() {
        Log("starting the programs that start with Windows");
        RunList(HKEY_LOCAL_MACHINE, kPolicyRunKey, nullptr, "policy, this computer");
        RunList(HKEY_CURRENT_USER, kPolicyRunKey, nullptr, "policy, this account");
        RunOnceList();
        RunList(HKEY_LOCAL_MACHINE, kRunKey, L"Run", "Run, this computer", KEY_WOW64_64KEY);
        BOOL wow = FALSE;
#if defined(_WIN64) || defined(_M_ARM64)
        wow = TRUE;   // a 64-bit program: the 32-bit list is a view of its own
#else
        IsWow64Process(GetCurrentProcess(), &wow);
#endif
        if (wow) RunList(HKEY_LOCAL_MACHINE, kRunKey, L"Run32", "Run, 32-bit, this computer", KEY_WOW64_32KEY);
        RunList(HKEY_CURRENT_USER, kRunKey, L"Run", "Run, this account");
        RunStartupFolder(FOLDERID_CommonStartup);
        RunStartupFolder(FOLDERID_Startup);
    }

    // The sign-in screen waits for the shell to say it is up before it gives
    // way to the desktop; Explorer says it with these events.
    void SignalShellReady() {
        for (const wchar_t* name : {L"ShellDesktopSwitchEvent", L"msgina: ShellReadyEvent",
                                    L"Global\\msgina: ShellReadyEvent"}) {
            HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
            if (!event) continue;
            SetEvent(event);
            CloseHandle(event);
        }
    }

    // Minimized windows hidden, as Explorer has them, instead of shrunk to
    // title bars at the screen's bottom-left corner - which is Windows'
    // behaviour without a shell. For this session only.
    void HideMinimizedWindows() {
        MINIMIZEDMETRICS metrics = {};
        metrics.cbSize = sizeof(metrics);
        if (!SystemParametersInfoW(SPI_GETMINIMIZEDMETRICS, sizeof(metrics), &metrics, 0)) return;
        metrics.iArrange |= ARW_HIDE;
        SystemParametersInfoW(SPI_SETMINIMIZEDMETRICS, sizeof(metrics), &metrics, 0);
    }

    // Explorer, as the session's shell: started with nothing to open while
    // no other desktop runs, it brings up its taskbar and desktop.
    void StartExplorer(const std::string& why) {
        Log("starting Explorer: " + why);
        wchar_t windows[MAX_PATH] = {};
        const UINT n = GetWindowsDirectoryW(windows, MAX_PATH);
        std::wstring explorer = (n > 0 && n < MAX_PATH) ? std::wstring(windows, n) + L"\\explorer.exe"
                                                         : std::wstring(L"explorer.exe");
        std::vector<wchar_t> command(explorer.begin(), explorer.end());
        command.push_back(L'\0');
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process = {};
        if (!CreateProcessW(explorer.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                            &process)) {
            Log("Explorer could not be started (error " + std::to_string(GetLastError()) + ")");
            return;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        for (int i = 0; i < 200 && !ExplorerTaskbarRunning(); ++i) Sleep(100);
        Log(ExplorerTaskbarRunning() ? "Explorer's taskbar is up" : "Explorer started; its taskbar did not show yet");
    }

    // ===== THE DESKTOP =====

    struct Running {
        HANDLE process = nullptr;
        DWORD error = 0;
    };

    Running StartDesktop(const std::wstring& program, const std::wstring& commandLine) {
        Running running;
        std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
        buffer.push_back(L'\0');
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process = {};
        const std::wstring folder = FolderOf(program);
        if (!CreateProcessW(program.c_str(), buffer.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            folder.empty() ? nullptr : folder.c_str(), &startup, &process)) {
            running.error = GetLastError();
            return running;
        }
        CloseHandle(process.hThread);
        running.process = process.hProcess;
        return running;
    }

    // Waits for the desktop to exit. While it runs, a shell setting that
    // stops naming the guard - with no start beside Explorer either - means
    // the user switched back: a desktop that does not close by itself within
    // the grace period is ended.
    DWORD WaitForDesktop(HANDLE process, bool sessionShell, const std::wstring& self) {
        HKEY winlogon = nullptr;
        HANDLE changed = nullptr;
        if (sessionShell && RegOpenKeyExW(HKEY_CURRENT_USER, kWinlogonKey, 0, KEY_NOTIFY | KEY_READ, &winlogon) ==
                                ERROR_SUCCESS) {
            changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (changed) RegNotifyChangeKeyValue(winlogon, FALSE, REG_NOTIFY_CHANGE_LAST_SET, changed, TRUE);
        }
        ULONGLONG deadline = 0;
        // Switched back before the watch was armed - while the desktop was
        // starting - is switched back all the same.
        if (changed && !ShellSettingNamesUs(self) && !StartsBesideExplorer()) {
            Log("the shell setting no longer names the guard: the desktop has five seconds to close");
            deadline = GetTickCount64() + kGraceMs;
        }
        for (;;) {
            HANDLE handles[2] = {process, changed};
            const DWORD count = changed ? 2 : 1;
            DWORD timeout = INFINITE;
            if (deadline != 0) {
                const ULONGLONG now = GetTickCount64();
                timeout = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
            }
            const DWORD result = WaitForMultipleObjects(count, handles, FALSE, timeout);
            if (result == WAIT_OBJECT_0) break;
            if (result == WAIT_OBJECT_0 + 1) {
                RegNotifyChangeKeyValue(winlogon, FALSE, REG_NOTIFY_CHANGE_LAST_SET, changed, TRUE);
                if (deadline == 0 && !ShellSettingNamesUs(self) && !StartsBesideExplorer()) {
                    Log("the shell setting no longer names the guard: the desktop has five seconds to close");
                    deadline = GetTickCount64() + kGraceMs;
                }
                continue;
            }
            if (result == WAIT_TIMEOUT && deadline != 0) {
                Log("the desktop did not close: ending it");
                TerminateProcess(process, 0);
                WaitForSingleObject(process, 5000);
                break;
            }
            break;   // the wait failed: treat as gone
        }
        if (changed) CloseHandle(changed);
        if (winlogon) RegCloseKey(winlogon);
        DWORD code = 0;
        if (!GetExitCodeProcess(process, &code)) code = 1;
        return code;
    }

    // 0, 1 - or an NTSTATUS such as 0xC0000005, the access violation of
    // a crash, which reads better in hex.
    std::string ExitCodeText(DWORD code) {
        if (code < 0x10000) return std::to_string(code);
        char text[16];
        std::snprintf(text, sizeof text, "0x%08lX", static_cast<unsigned long>(code));
        return text;
    }

    void Tell(const std::wstring& text) {
        MessageBoxW(nullptr, text.c_str(), L"UltraCanvas", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
    }

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    OpenLog();
    const std::wstring self = ThisProgram();
    const std::wstring commandLine = AfterFirstWord(GetCommandLineW());
    const std::wstring program = FirstWord(commandLine);

    // The session's shell: the sign-in started the guard through the
    // account's shell setting, and no other desktop runs.
    const bool sessionShell = ShellSettingNamesUs(self) && !ExplorerTaskbarRunning();
    Log(std::string(sessionShell ? "started as this session's shell" : "started (not as the session's shell)") +
        ": " + Utf8(commandLine));

    if (program.empty()) {
        Log("no desktop program was named");
        if (sessionShell) {
            SignalShellReady();
            StartExplorer("the shell setting names no desktop");
        }
        return sessionShell ? 0 : 2;
    }
    if (sessionShell) HideMinimizedWindows();

    std::vector<ULONGLONG> failures;
    bool startedOnce = false;
    for (;;) {
        Running running = StartDesktop(program, commandLine);
        if (!running.process) {
            Log("the desktop could not be started (error " + std::to_string(running.error) + ")");
            if (!sessionShell) return 1;
            SignalShellReady();
            StartExplorer("the desktop could not be started");
            Tell(L"UltraDesktop could not be started, so the Windows desktop has been started for this session.\n\n"
                 L"Reinstalling UltraCanvas usually helps; UOS-Settings switches back to the Windows desktop for "
                 L"good.");
            return 0;
        }

        if (sessionShell && !startedOnce) {
            // Up and waiting for input: the sign-in screen can give way.
            WaitForInputIdle(running.process, 15000);
            SignalShellReady();
            // The programs that start with Windows mostly put an icon in the
            // notification area: they start once the desktop hosts it.
            for (int i = 0; i < 100 && !FindWindowW(L"Shell_TrayWnd", nullptr); ++i) {
                if (WaitForSingleObject(running.process, 100) == WAIT_OBJECT_0) break;
            }
            RunStartupPrograms();
        }
        startedOnce = true;

        const DWORD code = WaitForDesktop(running.process, sessionShell, self);
        CloseHandle(running.process);
        Log("the desktop exited with code " + ExitCodeText(code));
        // Signing out, restarting, shutting down: the desktop was asked to
        // close with everything else, and nothing is to start again.
        if (GetSystemMetrics(SM_SHUTTINGDOWN)) {
            Log("the session is ending");
            return 0;
        }

        if (code == 0) {
            if (!sessionShell) return 0;
            StartExplorer(ShellSettingNamesUs(self) ? "the desktop was closed"
                                                    : "the user switched back to the Windows desktop");
            return 0;
        }

        // A crash, or a desktop that gave up: again - unless that is now
        // the third time within a minute.
        const ULONGLONG now = GetTickCount64();
        failures.push_back(now);
        while (!failures.empty() && now - failures.front() > kFailureWindowMs) failures.erase(failures.begin());
        if (static_cast<int>(failures.size()) >= kFailuresAllowed) {
            Log("the desktop failed " + std::to_string(failures.size()) + " times within a minute");
            if (!sessionShell) return 3;
            StartExplorer("the desktop failed three times within a minute");
            Tell(L"UltraDesktop stopped unexpectedly three times, so the Windows desktop has been started for this "
                 L"session.\n\nUltraDesktop is tried again at the next sign-in; UOS-Settings switches back to the "
                 L"Windows desktop for good.");
            return 0;
        }
        Sleep(kRestartDelayMs);
        Log("starting the desktop again");
    }
}
