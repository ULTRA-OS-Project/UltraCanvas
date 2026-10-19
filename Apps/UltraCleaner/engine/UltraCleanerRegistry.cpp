// Apps/UltraCleaner/engine/UltraCleanerRegistry.cpp
// The registry checks, the .reg backup writer and the cleaner. Everything
// here is platform-neutral; the Win32 store and probe are in
// UltraCleanerRegistryWindows.cpp.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerRegistry.h"

#include "UltraCleanerPaths.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / OpenFileUtf8 / UTF-16

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <optional>

using UltraCanvas::OpenFileUtf8;
using UltraCanvas::PathFromUtf8;

namespace UltraCleaner {
namespace {

// ===== TEXT HELPERS =====

char AsciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string Lower(std::string text) {
    for (char& c : text) c = AsciiLower(c);
    return text;
}

bool EqualsNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (AsciiLower(a[i]) != AsciiLower(b[i])) return false;
    }
    return true;
}

bool EndsWithNoCase(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           EqualsNoCase(text.substr(text.size() - suffix.size()), suffix);
}

bool StartsWithNoCase(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() &&
           EqualsNoCase(text.substr(0, prefix.size()), prefix);
}

std::string Trim(const std::string& text) {
    size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::string StripQuotes(std::string text) {
    text = Trim(text);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
    } else if (!text.empty() && text.front() == '"') {
        text = text.substr(1);   // an unterminated quote: take the rest
    }
    return Trim(text);
}

std::string LastSegment(const std::string& keyPath) {
    const size_t slash = keyPath.find_last_of('\\');
    return slash == std::string::npos ? keyPath : keyPath.substr(slash + 1);
}

std::string ParentKey(const std::string& keyPath) {
    const size_t slash = keyPath.find_last_of('\\');
    return slash == std::string::npos ? std::string() : keyPath.substr(0, slash);
}

std::string FileNameOf(const std::string& windowsPath) {
    const size_t slash = windowsPath.find_last_of("\\/");
    return slash == std::string::npos ? windowsPath : windowsPath.substr(slash + 1);
}

// The extensions CreateProcess, the shell and the registry's own servers
// name. A command whose first path ends in none of these is not pinned down.
const char* const kExecutableExtensions[] = {
    ".exe", ".com", ".bat", ".cmd", ".dll", ".ocx", ".cpl", ".scr", ".msc",
    ".vbs", ".vbe", ".js", ".jse", ".wsf", ".ps1", ".lnk", ".pif", ".hta",
    ".sys", ".drv", ".ax", ".chm", ".hlp"
};

bool HasExecutableExtension(const std::string& path) {
    for (const char* extension : kExecutableExtensions) {
        if (EndsWithNoCase(path, extension)) return true;
    }
    return false;
}

// "C:\x\app.exe,0" / "C:\x\app.exe,-101" → "C:\x\app.exe" (an icon index).
std::string StripIconIndex(const std::string& text) {
    const size_t comma = text.find_last_of(',');
    if (comma == std::string::npos || comma + 1 >= text.size()) return text;
    size_t i = comma + 1;
    if (text[i] == '-') ++i;
    if (i >= text.size()) return text;
    for (; i < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return text;
    }
    return Trim(text.substr(0, comma));
}

// Splits a command line into the program it starts and the arguments.
// Returns false when the program cannot be told apart from its arguments.
bool SplitCommand(const std::string& commandLine, std::string& program,
                  std::string& rest) {
    const std::string command = Trim(commandLine);
    if (command.empty()) return false;

    if (command.front() == '"') {
        const size_t close = command.find('"', 1);
        if (close == std::string::npos) {
            program = command.substr(1);
            rest.clear();
        } else {
            program = command.substr(1, close - 1);
            rest = command.substr(close + 1);
        }
        program = Trim(program);
        return !program.empty();
    }

    // A first word with no folder in it is a program looked up on PATH
    // ("regsvr32 /s x.dll", "cmd /c ..."): it ends at the first space.
    const size_t space = command.find(' ');
    const std::string firstWord = command.substr(0, space);
    if (firstWord.find_first_of("\\/:") == std::string::npos) {
        program = StripIconIndex(firstWord);
        rest = space == std::string::npos ? std::string() : command.substr(space);
        return !program.empty();
    }

    // Unquoted: the shortest prefix that ends at a space (or the end) and
    // names an executable — "C:\Program Files\My App\app.exe -x" stops at
    // "app.exe", which is the reading CreateProcess settles on too.
    for (size_t i = 0; i <= command.size(); ++i) {
        if (i < command.size() && command[i] != ' ') continue;
        const std::string candidate = StripIconIndex(command.substr(0, i));
        if (HasExecutableExtension(candidate)) {
            program = candidate;
            rest = i < command.size() ? command.substr(i) : std::string();
            return true;
        }
    }
    // No extension anywhere: only a command without spaces is unambiguous.
    if (command.find(' ') == std::string::npos) {
        program = StripIconIndex(command);
        rest.clear();
        return true;
    }
    return false;
}

// ===== VALUE LOOKUP =====

const RegistryValue* FindValue(const std::vector<RegistryValue>& values,
                               const std::string& name) {
    for (const auto& value : values) {
        if (EqualsNoCase(value.name, name)) return &value;
    }
    return nullptr;
}

std::string TextOf(const std::vector<RegistryValue>& values, const std::string& name) {
    const RegistryValue* value = FindValue(values, name);
    return value ? value->Text() : std::string();
}

// ===== THE CHECK TABLE =====
// Every key the cleaner may remove from, and what it looks for there. The
// guard in InsideAllowedRoot is built from this table and nothing else.

struct CheckRoot {
    RegistryIssueKind kind;
    RegistryHive hive;
    const char* path;
};

constexpr RegistryHive HKCU = RegistryHive::CurrentUser;
constexpr RegistryHive HKLM = RegistryHive::LocalMachine;

const CheckRoot kCheckRoots[] = {
    { RegistryIssueKind::ObsoleteUninstallEntry, HKLM, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall" },
    { RegistryIssueKind::ObsoleteUninstallEntry, HKLM, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall" },
    { RegistryIssueKind::ObsoleteUninstallEntry, HKCU, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall" },

    { RegistryIssueKind::MissingStartupProgram, HKCU, "Software\\Microsoft\\Windows\\CurrentVersion\\Run" },
    { RegistryIssueKind::MissingStartupProgram, HKCU, "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce" },
    { RegistryIssueKind::MissingStartupProgram, HKLM, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run" },
    { RegistryIssueKind::MissingStartupProgram, HKLM, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce" },
    { RegistryIssueKind::MissingStartupProgram, HKLM, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run" },
    { RegistryIssueKind::MissingStartupProgram, HKLM, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\RunOnce" },

    { RegistryIssueKind::MissingAppPath, HKCU, "Software\\Microsoft\\Windows\\CurrentVersion\\App Paths" },
    { RegistryIssueKind::MissingAppPath, HKLM, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths" },
    { RegistryIssueKind::MissingAppPath, HKLM, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\App Paths" },

    { RegistryIssueKind::MissingSharedDll, HKLM, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\SharedDLLs" },
    { RegistryIssueKind::MissingSharedDll, HKLM, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\SharedDLLs" },

    { RegistryIssueKind::StaleMuiCacheEntry, HKCU, "Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache" },

    { RegistryIssueKind::MissingHelpFile, HKLM, "SOFTWARE\\Microsoft\\Windows\\Help" },
    { RegistryIssueKind::MissingHelpFile, HKLM, "SOFTWARE\\Microsoft\\Windows\\HTML Help" },
    { RegistryIssueKind::MissingHelpFile, HKLM, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\Help" },
    { RegistryIssueKind::MissingHelpFile, HKLM, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\HTML Help" },

    { RegistryIssueKind::MissingInstallerFolder, HKLM, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\Folders" },

    { RegistryIssueKind::OrphanedComServer, HKCU, "Software\\Classes\\CLSID" },
};

// Kinds whose findings are subkeys of the root (removed whole); the rest are
// values of the root itself.
bool IsKeyKind(RegistryIssueKind kind) {
    return kind == RegistryIssueKind::ObsoleteUninstallEntry ||
           kind == RegistryIssueKind::MissingAppPath ||
           kind == RegistryIssueKind::OrphanedComServer;
}

// One entry's verdict.
enum class Verdict { Fine, Defective, Unjudged };

struct Examined {
    Verdict verdict = Verdict::Fine;
    std::string target;
    std::string label;
};

Examined Fine() { return {}; }
Examined Unjudged() { return { Verdict::Unjudged, {}, {} }; }
Examined FromState(PathState state, const std::string& target,
                   const std::string& label) {
    switch (state) {
        case PathState::Missing: return { Verdict::Defective, target, label };
        case PathState::Unknown: return Unjudged();
        case PathState::Exists:  break;
    }
    return Fine();
}

// ----- key kinds -----

Examined ExamineUninstallEntry(RegistryStore& store, FileProbe& probe,
                               RegistryHive hive, const std::string& keyPath) {
    const auto values = store.Values(hive, keyPath);
    // Windows' own components, and everything Windows Installer manages: an
    // MSI product's entry is one row of a database the installer keeps
    // elsewhere, and removing the row alone leaves that database lying.
    if (const auto* v = FindValue(values, "SystemComponent"); v && v->Dword() == 1) return Fine();
    if (const auto* v = FindValue(values, "WindowsInstaller"); v && v->Dword() == 1) return Fine();
    // An update or a patch belongs to its parent product's entry.
    if (FindValue(values, "ParentKeyName")) return Fine();

    const std::string uninstall = TextOf(values, "UninstallString");
    if (Trim(uninstall).empty()) return Fine();
    const std::string uninstaller = ExecutableOfCommand(uninstall);
    if (uninstaller.empty()) return Unjudged();

    std::string label = Trim(TextOf(values, "DisplayName"));
    if (label.empty()) label = LastSegment(keyPath);

    const PathState primary = JudgePath(probe, uninstaller);
    if (primary != PathState::Missing) return FromState(primary, uninstaller, label);

    // The uninstaller is gone. Anything else the entry names that is still
    // there means the software may be too — leave it.
    const std::string location = StripQuotes(TextOf(values, "InstallLocation"));
    if (!location.empty()) {
        const PathState state = JudgePath(probe, location);
        if (state == PathState::Exists) return Fine();
        // The install folder on a drive that is not here: cannot tell.
        if (state == PathState::Unknown) return Unjudged();
    }
    for (const char* name : { "QuietUninstallString", "DisplayIcon", "ModifyPath" }) {
        const std::string file = ExecutableOfCommand(TextOf(values, name));
        if (!file.empty() && JudgePath(probe, file) == PathState::Exists) return Fine();
    }
    return { Verdict::Defective, uninstaller, label };
}

Examined ExamineAppPath(RegistryStore& store, FileProbe& probe,
                        RegistryHive hive, const std::string& keyPath) {
    const auto values = store.Values(hive, keyPath);
    const RegistryValue* program = FindValue(values, "");
    if (!program || Trim(program->Text()).empty()) return Fine();
    const std::string file = ExecutableOfCommand(program->Text());
    if (file.empty()) return Unjudged();
    return FromState(JudgePath(probe, file), file, LastSegment(keyPath));
}

Examined ExamineComClass(RegistryStore& store, FileProbe& probe,
                         RegistryHive hive, const std::string& keyPath) {
    // A class is orphaned when every server it registers is gone. One that
    // registers none (a TreatAs, a ProgID alias) is not judged at all.
    std::string missing;
    bool anyServer = false;
    for (const char* server : { "InprocServer32", "LocalServer32" }) {
        const std::string serverKey = keyPath + "\\" + server;
        if (!store.KeyExists(hive, serverKey)) continue;
        const auto values = store.Values(hive, serverKey);
        const RegistryValue* path = FindValue(values, "");
        if (!path || Trim(path->Text()).empty()) return Unjudged();
        anyServer = true;
        const std::string file = ExecutableOfCommand(path->Text());
        if (file.empty()) return Unjudged();
        const PathState state = JudgePath(probe, file);
        if (state == PathState::Exists) return Fine();
        if (state == PathState::Unknown) return Unjudged();
        if (missing.empty()) missing = file;
    }
    if (!anyServer) return Fine();

    std::string label = Trim(TextOf(store.Values(hive, keyPath), ""));
    if (label.empty()) label = LastSegment(keyPath);
    return { Verdict::Defective, missing, label };
}

Examined ExamineKey(RegistryIssueKind kind, RegistryStore& store, FileProbe& probe,
                    RegistryHive hive, const std::string& keyPath) {
    switch (kind) {
        case RegistryIssueKind::ObsoleteUninstallEntry:
            return ExamineUninstallEntry(store, probe, hive, keyPath);
        case RegistryIssueKind::MissingAppPath:
            return ExamineAppPath(store, probe, hive, keyPath);
        case RegistryIssueKind::OrphanedComServer:
            return ExamineComClass(store, probe, hive, keyPath);
        default:
            return Fine();
    }
}

// ----- value kinds -----

// "C:\Apps\tool.exe.FriendlyAppName" → "C:\Apps\tool.exe". Explorer appends
// the field name to the program path; anything else in the key (LangID, the
// "@shell32.dll,-123" resource names) is not a path.
std::string MuiCacheProgram(const std::string& valueName) {
    if (valueName.empty() || valueName.front() == '@') return {};
    const std::string lower = Lower(valueName);
    size_t at = std::string::npos;
    for (size_t pos = lower.find(".exe"); pos != std::string::npos;
         pos = lower.find(".exe", pos + 1)) {
        const size_t after = pos + 4;
        if (after == lower.size() || lower[after] == '.') at = after;
    }
    return at == std::string::npos ? std::string() : valueName.substr(0, at);
}

Examined ExamineValue(RegistryIssueKind kind, FileProbe& probe,
                      const RegistryValue& value) {
    switch (kind) {
        case RegistryIssueKind::MissingStartupProgram: {
            if (!value.IsString()) return Fine();
            const std::string file = ExecutableOfCommand(value.Text());
            if (file.empty()) return Unjudged();
            return FromState(JudgePath(probe, file), file,
                             value.name.empty() ? FileNameOf(file) : value.name);
        }
        case RegistryIssueKind::MissingSharedDll:
        case RegistryIssueKind::MissingInstallerFolder: {
            // The value's name is the path; its data is a counter or empty.
            if (Trim(value.name).empty()) return Fine();
            return FromState(JudgePath(probe, value.name), value.name,
                             FileNameOf(StripQuotes(value.name)));
        }
        case RegistryIssueKind::StaleMuiCacheEntry: {
            const std::string program = MuiCacheProgram(value.name);
            if (program.empty()) return Fine();
            return FromState(JudgePath(probe, program), program,
                             value.IsString() ? value.Text() : FileNameOf(program));
        }
        case RegistryIssueKind::MissingHelpFile: {
            // Name: the help file; data: the folder it was installed to.
            if (!value.IsString() || value.name.empty()) return Fine();
            std::string folder = StripQuotes(value.Text());
            if (folder.empty()) return Fine();
            if (folder.back() != '\\' && folder.back() != '/') folder += '\\';
            const std::string file = folder + value.name;
            return FromState(JudgePath(probe, file), file, value.name);
        }
        default:
            return Fine();
    }
}

std::string HiveKeyName(RegistryHive hive, const std::string& path) {
    return std::string(RegistryHiveName(hive)) + "\\" + path;
}

// ===== .REG WRITING =====

std::string EscapeRegString(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (char c : text) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

std::string RegValueName(const std::string& name) {
    return name.empty() ? std::string("@") : "\"" + EscapeRegString(name) + "\"";
}

// regedit's own layout for long binary data: comma-separated bytes, broken
// with a trailing backslash so no line passes 80 columns.
void AppendHexBytes(std::string& line, std::string& out,
                    const std::vector<uint8_t>& bytes) {
    static const char* digits = "0123456789abcdef";
    for (size_t i = 0; i < bytes.size(); ++i) {
        line += digits[bytes[i] >> 4];
        line += digits[bytes[i] & 0xF];
        if (i + 1 < bytes.size()) {
            line += ',';
            if (line.size() > 76) {
                out += line + "\\\r\n";
                line = "  ";
            }
        }
    }
    out += line + "\r\n";
}

// A REG_SZ that reads back the same when written as "text": UTF-16 of whole
// units, NUL-terminated or not, with no NUL or line break inside.
bool IsPlainString(const RegistryValue& value) {
    if (value.type != RegString || value.data.size() % 2 != 0) return false;
    const size_t units = value.data.size() / 2;
    for (size_t i = 0; i < units; ++i) {
        const uint16_t unit = static_cast<uint16_t>(
            value.data[2 * i] | (value.data[2 * i + 1] << 8));
        const bool last = (i + 1 == units);
        if (unit == 0 && !last) return false;
        if (unit == '\r' || unit == '\n') return false;
    }
    return true;
}

void AppendValueLine(std::string& out, const RegistryValue& value) {
    const std::string name = RegValueName(value.name);
    if (IsPlainString(value)) {
        out += name + "=\"" + EscapeRegString(value.Text()) + "\"\r\n";
        return;
    }
    if (value.type == RegDword && value.data.size() == 4) {
        char buffer[16];
        std::snprintf(buffer, sizeof buffer, "%08x", value.Dword());
        out += name + "=dword:" + buffer + "\r\n";
        return;
    }
    std::string line = name + "=";
    if (value.type == RegBinary) {
        line += "hex:";
    } else {
        char buffer[16];
        std::snprintf(buffer, sizeof buffer, "hex(%x):", value.type);
        line += buffer;
    }
    AppendHexBytes(line, out, value.data);
}

void AppendKeyTree(std::string& out, RegistryStore& store, RegistryHive hive,
                   const std::string& path, int depth) {
    out += "[" + HiveKeyName(hive, path) + "]\r\n";
    for (const auto& value : store.Values(hive, path)) AppendValueLine(out, value);
    out += "\r\n";
    // A registry is at most 512 levels deep; the bound only stops a broken
    // store from recursing forever.
    if (depth >= 512) return;
    for (const auto& child : store.SubKeys(hive, path)) {
        AppendKeyTree(out, store, hive, path + "\\" + child, depth + 1);
    }
}

std::string LocalTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y%m%d-%H%M%S", &local);
    return buffer;
}

} // namespace

// ===== NAMES =====

const char* RegistryHiveName(RegistryHive hive) {
    return hive == RegistryHive::CurrentUser ? "HKEY_CURRENT_USER"
                                             : "HKEY_LOCAL_MACHINE";
}

const char* RegistryHiveShortName(RegistryHive hive) {
    return hive == RegistryHive::CurrentUser ? "HKCU" : "HKLM";
}

const char* RegistryIssueTitle(RegistryIssueKind kind) {
    switch (kind) {
        case RegistryIssueKind::ObsoleteUninstallEntry: return "Leftover uninstall entries";
        case RegistryIssueKind::MissingStartupProgram:  return "Startup entries for missing programs";
        case RegistryIssueKind::MissingAppPath:         return "App Paths of missing programs";
        case RegistryIssueKind::MissingSharedDll:       return "Shared DLLs that are gone";
        case RegistryIssueKind::StaleMuiCacheEntry:     return "Cached names of missing programs";
        case RegistryIssueKind::MissingHelpFile:        return "Missing help files";
        case RegistryIssueKind::MissingInstallerFolder: return "Installer records of missing folders";
        case RegistryIssueKind::OrphanedComServer:      return "Orphaned per-user COM classes";
        case RegistryIssueKind::KindCount:              break;
    }
    return "Registry entries";
}

const char* RegistryIssueDescription(RegistryIssueKind kind) {
    switch (kind) {
        case RegistryIssueKind::ObsoleteUninstallEntry:
            return "Programs still listed under Apps & features although their "
                   "uninstaller and install folder are gone. Programs managed by "
                   "Windows Installer are never listed here.";
        case RegistryIssueKind::MissingStartupProgram:
            return "Commands Windows tries to run at sign-in for programs that no "
                   "longer exist.";
        case RegistryIssueKind::MissingAppPath:
            return "Registrations that let Run and the shell start a program by "
                   "name, for programs that no longer exist.";
        case RegistryIssueKind::MissingSharedDll:
            return "Usage counters installers keep for shared files that have "
                   "since been deleted.";
        case RegistryIssueKind::StaleMuiCacheEntry:
            return "Display names Explorer remembers for programs that have been "
                   "deleted.";
        case RegistryIssueKind::MissingHelpFile:
            return "Help and HTML Help registrations whose file is gone.";
        case RegistryIssueKind::MissingInstallerFolder:
            return "Folders Windows Installer recorded as created by a setup, "
                   "that no longer exist. Left unticked: an installer may still "
                   "expect them.";
        case RegistryIssueKind::OrphanedComServer:
            return "Per-user COM classes whose every server file is gone. Left "
                   "unticked: a program being repaired may still rely on them.";
        case RegistryIssueKind::KindCount:
            break;
    }
    return "";
}

bool RegistryIssueSafeByDefault(RegistryIssueKind kind) {
    return kind != RegistryIssueKind::MissingInstallerFolder &&
           kind != RegistryIssueKind::OrphanedComServer;
}

// ===== VALUES =====

std::string RegistryValue::Text() const {
    if (!IsString()) return {};
    std::u16string wide;
    wide.reserve(data.size() / 2);
    for (size_t i = 0; i + 1 < data.size(); i += 2) {
        const char16_t unit = static_cast<char16_t>(data[i] | (data[i + 1] << 8));
        if (unit == 0) break;
        wide.push_back(unit);
    }
    return UltraCanvas::PathUtf8Detail::Utf16ToUtf8(wide);
}

uint32_t RegistryValue::Dword(uint32_t fallback) const {
    if (type != RegDword || data.size() < 4) return fallback;
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

RegistryValue RegistryValue::String(const std::string& name, const std::string& utf8,
                                    uint32_t type) {
    RegistryValue value;
    value.name = name;
    value.type = type;
    const auto wide = UltraCanvas::PathUtf8Detail::Utf8ToUtf16<std::u16string>(utf8);
    value.data.reserve((wide.size() + 1) * 2);
    for (char16_t unit : wide) {
        value.data.push_back(static_cast<uint8_t>(unit & 0xFF));
        value.data.push_back(static_cast<uint8_t>(unit >> 8));
    }
    value.data.push_back(0);
    value.data.push_back(0);
    return value;
}

RegistryValue RegistryValue::DwordValue(const std::string& name, uint32_t number) {
    RegistryValue value;
    value.name = name;
    value.type = RegDword;
    value.data = { static_cast<uint8_t>(number & 0xFF),
                   static_cast<uint8_t>((number >> 8) & 0xFF),
                   static_cast<uint8_t>((number >> 16) & 0xFF),
                   static_cast<uint8_t>((number >> 24) & 0xFF) };
    return value;
}

// ===== FINDINGS =====

std::string RegistryFinding::FullKeyName() const {
    return HiveKeyName(hive, keyPath);
}

std::string RegistryFinding::Location() const {
    if (wholeKey) return FullKeyName();
    return FullKeyName() + "  →  " +
           (valueName.empty() ? std::string("(Default)") : valueName);
}

size_t RegistryScanReport::SelectedCount() const {
    size_t count = 0;
    for (const auto& finding : findings) {
        if (finding.selected) ++count;
    }
    return count;
}

// ===== COMMAND LINES AND PATHS =====

std::string ExecutableOfCommand(const std::string& commandLine) {
    std::string program, rest;
    if (!SplitCommand(commandLine, program, rest)) return {};

    const std::string name = Lower(FileNameOf(program));
    if (name == "rundll32.exe" || name == "rundll32") {
        // rundll32 <dll>,<entry> [args] — the DLL is what has to exist.
        std::string target = Trim(rest);
        if (!target.empty() && target.front() == '"') {
            const size_t close = target.find('"', 1);
            target = close == std::string::npos ? target.substr(1)
                                                : target.substr(1, close - 1);
        } else {
            target = target.substr(0, target.find(','));
        }
        return Trim(target);
    }
    if (name == "regsvr32.exe" || name == "regsvr32") {
        // regsvr32 [/s] [/u] [/i[:cmd]] [/n] <dll>
        std::string target = Trim(rest);
        while (!target.empty() && (target.front() == '/' || target.front() == '-')) {
            const size_t space = target.find(' ');
            target = space == std::string::npos ? std::string()
                                                : Trim(target.substr(space));
        }
        return StripQuotes(target);
    }
    // Hosts whose argument, not the host, is the point, and which cannot be
    // pinned to one file: leave them unjudged rather than judge the host.
    static const char* const kHosts[] = {
        "msiexec.exe", "msiexec", "cmd.exe", "cmd", "powershell.exe",
        "powershell", "pwsh.exe", "pwsh", "wscript.exe", "wscript",
        "cscript.exe", "cscript", "mshta.exe", "mshta", "explorer.exe",
        "explorer", "conhost.exe", "start"
    };
    for (const char* host : kHosts) {
        if (name == host) return {};
    }
    return program;
}

PathState JudgePath(FileProbe& probe, const std::string& rawPath) {
    std::string path = StripQuotes(rawPath);
    if (path.find('%') != std::string::npos) path = probe.ExpandEnvironment(path);
    std::replace(path.begin(), path.end(), '/', '\\');

    // Absolute, drive-letter, fully expanded, no wildcards. A bare name is
    // looked up on PATH, a UNC path is a share that may be offline, and a
    // "\\?\" path is someone else's careful spelling: none are judged.
    if (path.size() < 3 || !std::isalpha(static_cast<unsigned char>(path[0])) ||
        path[1] != ':' || path[2] != '\\') {
        return PathState::Unknown;
    }
    if (path.find_first_of("%*?<>|\"") != std::string::npos) return PathState::Unknown;
    if (path.find("\\..\\") != std::string::npos ||
        EndsWithNoCase(path, "\\..")) {
        return PathState::Unknown;
    }

    // Nothing inside the Windows folder: System32 vs SysWOW64 vs Sysnative,
    // WinSxS and the servicing stack decide what is there, not this program.
    std::string windows = StripQuotes(probe.WindowsDirectory());
    std::replace(windows.begin(), windows.end(), '/', '\\');
    if (windows.empty()) windows = "C:\\Windows";
    while (!windows.empty() && windows.back() == '\\') windows.pop_back();
    if (EqualsNoCase(path, windows) || StartsWithNoCase(path, windows + "\\")) {
        return PathState::Unknown;
    }

    while (path.size() > 3 && path.back() == '\\') path.pop_back();
    return probe.Check(path);
}

// ===== SCANNER =====

RegistryScanReport RegistryScanner::Scan(RegistryStore& store, FileProbe& probe,
                                         const RegistryScanProgressCallback& progress) {
    RegistryScanReport report;
    RegistryScanProgress state;

    auto record = [&](RegistryIssueKind kind, const CheckRoot& root,
                      const Examined& examined, const std::string& keyPath,
                      bool wholeKey, const std::string& valueName) {
        ++report.entriesExamined;
        if (examined.verdict == Verdict::Unjudged) ++report.entriesUnjudged;
        if (examined.verdict != Verdict::Defective) return;
        RegistryFinding finding;
        finding.kind = kind;
        finding.hive = root.hive;
        finding.keyPath = keyPath;
        finding.wholeKey = wholeKey;
        finding.valueName = valueName;
        finding.target = examined.target;
        finding.label = examined.label;
        finding.selected = RegistryIssueSafeByDefault(kind);
        report.findings.push_back(std::move(finding));
    };

    for (const CheckRoot& root : kCheckRoots) {
        if (cancel_) { report.cancelled = true; break; }
        if (!store.KeyExists(root.hive, root.path)) continue;

        state.currentCheck = RegistryIssueTitle(root.kind);
        if (IsKeyKind(root.kind)) {
            for (const auto& child : store.SubKeys(root.hive, root.path)) {
                if (cancel_) { report.cancelled = true; break; }
                const std::string keyPath = std::string(root.path) + "\\" + child;
                record(root.kind, root,
                       ExamineKey(root.kind, store, probe, root.hive, keyPath),
                       keyPath, /*wholeKey=*/true, {});
            }
        } else {
            for (const auto& value : store.Values(root.hive, root.path)) {
                if (cancel_) { report.cancelled = true; break; }
                record(root.kind, root, ExamineValue(root.kind, probe, value),
                       root.path, /*wholeKey=*/false, value.name);
            }
        }
        if (progress) {
            state.entriesExamined = report.entriesExamined;
            state.findings = report.findings.size();
            progress(state);
        }
        if (report.cancelled) break;
    }

    // Per-kind totals, in the order of the enum.
    for (int k = 0; k < static_cast<int>(RegistryIssueKind::KindCount); ++k) {
        const auto kind = static_cast<RegistryIssueKind>(k);
        RegistryKindSummary summary{ kind, 0, RegistryIssueSafeByDefault(kind) };
        for (const auto& finding : report.findings) {
            if (finding.kind == kind) ++summary.count;
        }
        if (summary.count > 0) report.kinds.push_back(summary);
    }
    return report;
}

bool RegistryScanner::InsideAllowedRoot(const RegistryFinding& finding) {
    if (finding.keyPath.empty()) return false;
    for (const CheckRoot& root : kCheckRoots) {
        if (root.kind != finding.kind || root.hive != finding.hive) continue;
        if (finding.wholeKey != IsKeyKind(root.kind)) continue;
        if (finding.wholeKey) {
            // Directly below the root: one more segment, and a real name.
            const std::string leaf = LastSegment(finding.keyPath);
            if (!leaf.empty() && leaf != "." && leaf != ".." &&
                EqualsNoCase(ParentKey(finding.keyPath), root.path)) {
                return true;
            }
        } else if (EqualsNoCase(finding.keyPath, root.path)) {
            return true;
        }
    }
    return false;
}

bool RegistryScanner::StillDefective(RegistryStore& store, FileProbe& probe,
                                     const RegistryFinding& finding) {
    Examined now;
    if (finding.wholeKey) {
        if (!store.KeyExists(finding.hive, finding.keyPath)) return false;
        now = ExamineKey(finding.kind, store, probe, finding.hive, finding.keyPath);
    } else {
        const auto values = store.Values(finding.hive, finding.keyPath);
        const RegistryValue* value = FindValue(values, finding.valueName);
        if (!value) return false;
        now = ExamineValue(finding.kind, probe, *value);
    }
    return now.verdict == Verdict::Defective &&
           EqualsNoCase(now.target, finding.target);
}

// ===== BACKUP =====

std::string BuildRegistryBackup(RegistryStore& store,
                                const std::vector<RegistryFinding>& findings) {
    std::string out = "Windows Registry Editor Version 5.00\r\n\r\n";
    for (const auto& finding : findings) {
        if (!finding.selected) continue;
        if (finding.wholeKey) {
            AppendKeyTree(out, store, finding.hive, finding.keyPath, 0);
            continue;
        }
        const auto values = store.Values(finding.hive, finding.keyPath);
        const RegistryValue* value = FindValue(values, finding.valueName);
        if (!value) continue;
        out += "[" + finding.FullKeyName() + "]\r\n";
        AppendValueLine(out, *value);
        out += "\r\n";
    }
    return out;
}

bool WriteRegistryBackup(const std::string& path, const std::string& regText,
                         std::string* error) {
    std::error_code ec;
    const auto parent = PathFromUtf8(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);

    const auto wide = UltraCanvas::PathUtf8Detail::Utf8ToUtf16<std::u16string>(regText);
    std::vector<uint8_t> bytes;
    bytes.reserve(2 + wide.size() * 2);
    bytes.push_back(0xFF);   // UTF-16LE byte-order mark
    bytes.push_back(0xFE);
    for (char16_t unit : wide) {
        bytes.push_back(static_cast<uint8_t>(unit & 0xFF));
        bytes.push_back(static_cast<uint8_t>(unit >> 8));
    }

    std::FILE* file = OpenFileUtf8(path, "wb");
    if (!file) {
        if (error) *error = "cannot create " + path;
        return false;
    }
    const bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    return true;
}

std::string DefaultRegistryBackupPath() {
    std::string base = LocalAppDataDir();
    if (base.empty()) base = UserDataDir();
    if (base.empty()) base = UserTempDir();
    return base + "/UltraCleaner/RegistryBackups/registry-" + LocalTimestamp() + ".reg";
}

// ===== CLEANER =====

RegistryCleanReport RegistryCleaner::Clean(RegistryStore& store, FileProbe& probe,
                                           const RegistryScanReport& report,
                                           const RegistryCleanOptions& options,
                                           const RegistryCleanProgressCallback& progress) {
    RegistryCleanReport result;
    result.simulated = options.simulate;

    // First pass: what is still eligible right now.
    std::vector<RegistryFinding> eligible;
    for (const auto& finding : report.findings) {
        if (!finding.selected) continue;
        if (!RegistryScanner::InsideAllowedRoot(finding)) {
            ++result.refusedByGuard;
            continue;
        }
        if (!RegistryScanner::StillDefective(store, probe, finding)) {
            ++result.skippedChanged;
            continue;
        }
        eligible.push_back(finding);
    }

    if (options.simulate) {
        result.removed = eligible.size();
        return result;
    }
    if (eligible.empty()) return result;

    // The backup comes first, and without it nothing goes.
    result.backupPath = options.backupPath.empty() ? DefaultRegistryBackupPath()
                                                   : options.backupPath;
    std::string error;
    if (!WriteRegistryBackup(result.backupPath, BuildRegistryBackup(store, eligible),
                             &error)) {
        result.aborted = true;
        result.failures.push_back({ result.backupPath,
                                    "The backup could not be written (" + error +
                                        "), so nothing was removed." });
        result.backupPath.clear();
        return result;
    }

    for (size_t i = 0; i < eligible.size(); ++i) {
        if (cancel_) { result.cancelled = true; break; }
        const RegistryFinding& finding = eligible[i];
        if (progress) progress(i + 1, eligible.size(), finding.Location());

        // Checked once more at the moment of removal: a reinstall between
        // the backup and now is not something to undo.
        if (!RegistryScanner::StillDefective(store, probe, finding)) {
            ++result.skippedChanged;
            continue;
        }
        error.clear();
        const bool ok = finding.wholeKey
            ? store.DeleteKeyTree(finding.hive, finding.keyPath, &error)
            : store.DeleteValue(finding.hive, finding.keyPath, finding.valueName, &error);
        if (ok) {
            ++result.removed;
        } else {
            result.failures.push_back({ finding.Location(),
                                        error.empty() ? "Could not be removed." : error });
        }
    }
    return result;
}

} // namespace UltraCleaner
