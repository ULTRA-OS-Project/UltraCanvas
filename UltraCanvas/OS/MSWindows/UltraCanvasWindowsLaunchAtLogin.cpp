// OS/MSWindows/UltraCanvasWindowsLaunchAtLogin.cpp
// UltraCanvasLaunchAtLogin on Windows: a value under
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run, the per-user startup
// list Explorer runs at sign-in and the Task Manager's Startup tab shows.
// That tab's on / off is kept apart, under Explorer\StartupApproved\Run: a
// binary value whose first byte is even when the entry is on (02) and odd
// when the user turned it off there (03). An entry turned off there reads as
// off here; turning it on here removes that value, which means "on".
// Version: 1.0.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework
#include "UltraCanvasLaunchAtLogin.h"
#include "UltraCanvasUtils.h"          // Utf8ToWide / WideToUtf8
#include "UltraLocalisation/UltraLocalisation.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef APPMODEL_ERROR_NO_PACKAGE
#define APPMODEL_ERROR_NO_PACKAGE 15700L   // winerror.h; older MinGW headers lack it
#endif

#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kApprovedKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

// A packaged (MSIX) program's HKCU writes go to the package's own hive,
// which Explorer never reads. GetCurrentPackageFullName is Windows 8 and
// later, so it is looked up rather than linked.
bool IsPackaged() {
    using GetPackageFn = LONG(WINAPI*)(UINT32*, PWSTR);
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    if (!kernel) return false;
    auto fn = reinterpret_cast<GetPackageFn>(
        reinterpret_cast<void*>(GetProcAddress(kernel, "GetCurrentPackageFullName")));
    if (!fn) return false;
    UINT32 length = 0;
    const LONG result = fn(&length, nullptr);
    return result != APPMODEL_ERROR_NO_PACKAGE;
}

std::wstring ThisProgram() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0) return {};
        if (n < buffer.size()) return std::wstring(buffer.data(), n);
        if (buffer.size() >= 32768) return {};
        buffer.resize(buffer.size() * 2);
    }
}

// One argument as CommandLineToArgvW reads it back: quoted when it has a
// space, a tab or a quote; a quote and the backslashes before it escaped.
std::wstring QuoteArgument(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            backslashes = 0;
        }
        if (c != L'\\') out += c;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

std::wstring CommandLine(const LaunchAtLoginEntry& entry) {
    const std::wstring program = entry.executable.empty() ? ThisProgram() : Utf8ToWide(entry.executable);
    if (program.empty()) return {};
    // The program is always quoted: an unquoted path with a space in it is
    // read as a shorter path plus arguments.
    std::wstring line = L"\"" + program + L"\"";
    for (const std::string& arg : entry.arguments) line += L" " + QuoteArgument(Utf8ToWide(arg));
    return line;
}

std::string SystemMessage(LONG code) {
    wchar_t* text = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, static_cast<DWORD>(code), 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    std::string message;
    if (n && text) {
        message = WideToUtf8(std::wstring(text, n));
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' '))
            message.pop_back();
    }
    if (text) LocalFree(text);
    return message.empty() ? std::to_string(code) : message;
}

// Whether the Task Manager's Startup tab turned this entry off.
bool TurnedOffInTaskManager(const std::wstring& name) {
    BYTE data[16] = {};
    DWORD size = sizeof(data);
    DWORD type = 0;
    const LONG r = RegGetValueW(HKEY_CURRENT_USER, kApprovedKey, name.c_str(), RRF_RT_REG_BINARY, &type, data, &size);
    return r == ERROR_SUCCESS && size >= 1 && (data[0] & 1) != 0;
}

} // namespace

namespace LaunchAtLoginBackend {

bool Available() {
    return !IsPackaged();
}

bool IsEnabled(const std::string& id) {
    const std::wstring name = Utf8ToWide(id);
    DWORD size = 0;
    const LONG r = RegGetValueW(HKEY_CURRENT_USER, kRunKey, name.c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                                nullptr, nullptr, &size);
    if (r != ERROR_SUCCESS) return false;
    return !TurnedOffInTaskManager(name);
}

bool Set(const LaunchAtLoginEntry& entry, bool enabled, std::string& error) {
    const std::wstring name = Utf8ToWide(entry.id);
    HKEY key = nullptr;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr,
                             &key, nullptr);
    if (r != ERROR_SUCCESS) {
        error = TrF("The startup list could not be changed: {0}", SystemMessage(r));
        return false;
    }
    if (enabled) {
        const std::wstring command = CommandLine(entry);
        if (command.empty()) {
            RegCloseKey(key);
            error = Tr("Where this program is installed could not be found.");
            return false;
        }
        r = RegSetValueExW(key, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                           static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(key, name.c_str());
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (r != ERROR_SUCCESS) {
        error = TrF("The startup list could not be changed: {0}", SystemMessage(r));
        return false;
    }
    // On here means on in the Task Manager too; off here leaves nothing behind.
    HKEY approved = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kApprovedKey, 0, KEY_SET_VALUE, &approved) == ERROR_SUCCESS) {
        RegDeleteValueW(approved, name.c_str());
        RegCloseKey(approved);
    }
    return true;
}

std::string Command(const LaunchAtLoginEntry& entry) {
    return WideToUtf8(CommandLine(entry));
}

} // namespace LaunchAtLoginBackend

} // namespace UltraCanvas
