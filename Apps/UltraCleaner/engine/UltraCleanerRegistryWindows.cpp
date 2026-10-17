// Apps/UltraCleaner/engine/UltraCleanerRegistryWindows.cpp
// The real registry and filesystem behind RegistryStore and FileProbe, on
// the Win32 W API. Everywhere else the factories return nullptr and the
// registry page is not offered.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerRegistry.h"

#if defined(_WIN32) || defined(_WIN64)

#include "UltraCanvasPathUtf8.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace UltraCleaner {
namespace {

using UltraCanvas::PathUtf8Detail::Utf16ToUtf8;
using UltraCanvas::PathUtf8Detail::Utf8ToUtf16;

std::wstring Wide(const std::string& utf8) { return Utf8ToUtf16<std::wstring>(utf8); }
std::string Narrow(const std::wstring& wide) { return Utf16ToUtf8(wide); }

HKEY RootOf(RegistryHive hive) {
    return hive == RegistryHive::CurrentUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
}

std::string DescribeError(LONG code) {
    if (code == ERROR_ACCESS_DENIED) {
        return "Access denied - entries under HKEY_LOCAL_MACHINE need "
               "UltraCleaner to run as administrator.";
    }
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code), 0, reinterpret_cast<LPWSTR>(&buffer), 0,
        nullptr);
    std::string text = length && buffer ? Narrow(std::wstring(buffer, length))
                                        : "error " + std::to_string(code);
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' ||
                             text.back() == ' ' || text.back() == '.')) {
        text.pop_back();
    }
    return text;
}

// Owns an open key.
class Key {
public:
    Key(RegistryHive hive, const std::string& path, REGSAM access) {
        status_ = RegOpenKeyExW(RootOf(hive), Wide(path).c_str(), 0,
                                access | KEY_WOW64_64KEY, &key_);
        if (status_ != ERROR_SUCCESS) key_ = nullptr;
    }
    ~Key() { if (key_) RegCloseKey(key_); }
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;

    HKEY Get() const { return key_; }
    LONG Status() const { return status_; }

private:
    HKEY key_ = nullptr;
    LONG status_ = ERROR_SUCCESS;
};

class Win32RegistryStore : public RegistryStore {
public:
    bool KeyExists(RegistryHive hive, const std::string& path) override {
        Key key(hive, path, KEY_QUERY_VALUE);
        return key.Get() != nullptr;
    }

    std::vector<std::string> SubKeys(RegistryHive hive, const std::string& path) override {
        std::vector<std::string> names;
        Key key(hive, path, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE);
        if (!key.Get()) return names;
        DWORD count = 0, maxName = 0;
        if (RegQueryInfoKeyW(key.Get(), nullptr, nullptr, nullptr, &count, &maxName,
                             nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr) != ERROR_SUCCESS) {
            return names;
        }
        std::vector<wchar_t> buffer(maxName + 2);
        for (DWORD index = 0;; ++index) {
            DWORD length = static_cast<DWORD>(buffer.size());
            const LONG status = RegEnumKeyExW(key.Get(), index, buffer.data(), &length,
                                              nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS) break;
            if (status == ERROR_MORE_DATA) {
                buffer.resize(buffer.size() * 2);
                --index;
                continue;
            }
            if (status != ERROR_SUCCESS) break;
            names.push_back(Narrow(std::wstring(buffer.data(), length)));
        }
        return names;
    }

    std::vector<RegistryValue> Values(RegistryHive hive, const std::string& path) override {
        std::vector<RegistryValue> values;
        Key key(hive, path, KEY_QUERY_VALUE);
        if (!key.Get()) return values;
        DWORD maxName = 0, maxData = 0;
        if (RegQueryInfoKeyW(key.Get(), nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr, nullptr, &maxName, &maxData, nullptr,
                             nullptr) != ERROR_SUCCESS) {
            return values;
        }
        std::vector<wchar_t> name(maxName + 2);
        std::vector<BYTE> data(maxData + 2);
        for (DWORD index = 0;; ++index) {
            DWORD nameLength = static_cast<DWORD>(name.size());
            DWORD dataLength = static_cast<DWORD>(data.size());
            DWORD type = 0;
            const LONG status = RegEnumValueW(key.Get(), index, name.data(), &nameLength,
                                              nullptr, &type, data.data(), &dataLength);
            if (status == ERROR_NO_MORE_ITEMS) break;
            if (status == ERROR_MORE_DATA) {
                // A value grew since the key was measured: measure again.
                name.resize(name.size() * 2);
                data.resize(std::max<size_t>(data.size() * 2, dataLength + 2));
                --index;
                continue;
            }
            if (status != ERROR_SUCCESS) break;
            RegistryValue value;
            value.name = Narrow(std::wstring(name.data(), nameLength));
            value.type = type;
            value.data.assign(data.begin(), data.begin() + dataLength);
            values.push_back(std::move(value));
        }
        return values;
    }

    bool DeleteValue(RegistryHive hive, const std::string& path,
                     const std::string& name, std::string* error) override {
        Key key(hive, path, KEY_SET_VALUE);
        if (!key.Get()) {
            if (error) *error = DescribeError(key.Status());
            return false;
        }
        const LONG status = RegDeleteValueW(key.Get(), Wide(name).c_str());
        if (status != ERROR_SUCCESS) {
            if (error) *error = DescribeError(status);
            return false;
        }
        return true;
    }

    bool DeleteKeyTree(RegistryHive hive, const std::string& path,
                       std::string* error) override {
        const size_t slash = path.find_last_of('\\');
        if (slash == std::string::npos || slash + 1 >= path.size()) {
            if (error) *error = "Refusing to remove a top-level key.";
            return false;
        }
        Key parent(hive, path.substr(0, slash),
                   DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE);
        if (!parent.Get()) {
            if (error) *error = DescribeError(parent.Status());
            return false;
        }
        const LONG status = RegDeleteTreeW(parent.Get(), Wide(path.substr(slash + 1)).c_str());
        if (status != ERROR_SUCCESS) {
            if (error) *error = DescribeError(status);
            return false;
        }
        return true;
    }
};

class Win32FileProbe : public FileProbe {
public:
    PathState Check(const std::string& path) override {
        if (path.size() < 3 || path[1] != ':' || path[2] != '\\') return PathState::Unknown;
        // Only a fixed disk that is mounted right now can prove a file gone.
        const std::wstring root = Wide(path.substr(0, 3));
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) return PathState::Unknown;
        if (GetFileAttributesW(root.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return PathState::Unknown;
        }
        std::wstring wide = Wide(path);
        if (wide.size() >= MAX_PATH - 12) wide = L"\\\\?\\" + wide;  // path-string-ok: wide string
        if (GetFileAttributesW(wide.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return PathState::Exists;
        }
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return PathState::Missing;
        }
        return PathState::Unknown;   // denied, sharing violation, bad name…
    }

    std::string ExpandEnvironment(const std::string& text) override {
        const std::wstring source = Wide(text);
        const DWORD needed = ExpandEnvironmentStringsW(source.c_str(), nullptr, 0);
        if (needed == 0) return text;
        std::wstring expanded(needed, L'\0');
        const DWORD written =
            ExpandEnvironmentStringsW(source.c_str(), expanded.data(), needed);
        if (written == 0 || written > needed) return text;
        expanded.resize(written - 1);
        return Narrow(expanded);
    }

    std::string WindowsDirectory() override {
        wchar_t buffer[MAX_PATH + 1];
        const UINT length = GetWindowsDirectoryW(buffer, MAX_PATH + 1);
        if (length == 0 || length > MAX_PATH) return "C:\\Windows";
        return Narrow(std::wstring(buffer, length));
    }
};

} // namespace

std::unique_ptr<RegistryStore> CreateSystemRegistryStore() {
    return std::make_unique<Win32RegistryStore>();
}

std::unique_ptr<FileProbe> CreateSystemFileProbe() {
    return std::make_unique<Win32FileProbe>();
}

bool RegistryCleaningAvailable() { return true; }

bool RestoreRegistryBackup(const std::string& path, std::string* error) {
    // reg.exe reads the UTF-16 .reg the backup writer produced, and merges
    // it exactly as double-clicking the file would.
    std::wstring file = Wide(path);
    for (auto& c : file) {
        if (c == L'/') c = L'\\';
    }
    wchar_t system[MAX_PATH + 1];
    const UINT length = GetSystemDirectoryW(system, MAX_PATH + 1);
    const std::wstring reg = (length && length <= MAX_PATH)
                                 ? std::wstring(system, length) + L"\\reg.exe"
                                 : std::wstring(L"reg.exe");
    std::wstring command = L"\"" + reg + L"\" import \"" + file + L"\"";

    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(reg.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        if (error) *error = "Could not start reg.exe: " +
                            DescribeError(static_cast<LONG>(GetLastError()));
        return false;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exitCode != 0) {
        if (error) *error = "reg.exe could not import the backup (exit code " +
                            std::to_string(exitCode) +
                            "). Entries under HKEY_LOCAL_MACHINE need "
                            "administrator rights.";
        return false;
    }
    return true;
}

} // namespace UltraCleaner

#else // not Windows

namespace UltraCleaner {

std::unique_ptr<RegistryStore> CreateSystemRegistryStore() { return nullptr; }
std::unique_ptr<FileProbe> CreateSystemFileProbe() { return nullptr; }
bool RegistryCleaningAvailable() { return false; }

bool RestoreRegistryBackup(const std::string&, std::string* error) {
    if (error) *error = "The Windows registry exists only on Windows.";
    return false;
}

} // namespace UltraCleaner

#endif
