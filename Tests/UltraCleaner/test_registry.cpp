// Tests/UltraCleaner/test_registry.cpp
// The registry cleaner, run against an in-memory registry and a pretend
// disk: what each check reports, what it refuses to judge, the .reg backup,
// and that removal re-checks, guards and backs up before it touches a key.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_support.h"

#include "UltraCleanerRegistry.h"
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace UltraCleaner;
using ultracleaner_test::TempTree;

namespace {

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// A registry: keys by lower-cased "<hive>|<path>", each with its values.
// Subkeys are derived from the key set, as in the real thing.
class FakeRegistry : public RegistryStore {
public:
    struct Node {
        std::string path;   // original spelling
        std::vector<RegistryValue> values;
    };

    void Set(RegistryHive hive, const std::string& path, RegistryValue value) {
        Node& node = Ensure(hive, path);
        for (auto& existing : node.values) {
            if (Lower(existing.name) == Lower(value.name)) { existing = value; return; }
        }
        node.values.push_back(std::move(value));
    }
    void SetText(RegistryHive hive, const std::string& path, const std::string& name,
                 const std::string& text) {
        Set(hive, path, RegistryValue::String(name, text));
    }
    Node& Ensure(RegistryHive hive, const std::string& path) {
        // Every ancestor exists too.
        for (size_t at = path.find('\\'); at != std::string::npos;
             at = path.find('\\', at + 1)) {
            auto& parent = keys_[KeyOf(hive, path.substr(0, at))];
            if (parent.path.empty()) parent.path = path.substr(0, at);
        }
        Node& node = keys_[KeyOf(hive, path)];
        if (node.path.empty()) node.path = path;
        return node;
    }

    bool KeyExists(RegistryHive hive, const std::string& path) override {
        return keys_.count(KeyOf(hive, path)) > 0;
    }
    std::vector<std::string> SubKeys(RegistryHive hive, const std::string& path) override {
        std::vector<std::string> names;
        const std::string prefix = KeyOf(hive, path) + "\\";
        for (const auto& [key, node] : keys_) {
            if (key.rfind(prefix, 0) != 0) continue;
            const std::string rest = key.substr(prefix.size());
            if (rest.find('\\') != std::string::npos) continue;
            names.push_back(node.path.substr(node.path.size() - rest.size()));
        }
        return names;
    }
    std::vector<RegistryValue> Values(RegistryHive hive, const std::string& path) override {
        auto found = keys_.find(KeyOf(hive, path));
        return found == keys_.end() ? std::vector<RegistryValue>{} : found->second.values;
    }
    bool DeleteValue(RegistryHive hive, const std::string& path, const std::string& name,
                     std::string* error) override {
        if (denyWrites) { if (error) *error = "Access denied"; return false; }
        auto found = keys_.find(KeyOf(hive, path));
        if (found == keys_.end()) return false;
        auto& values = found->second.values;
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (Lower(it->name) == Lower(name)) { values.erase(it); ++deletions; return true; }
        }
        return false;
    }
    bool DeleteKeyTree(RegistryHive hive, const std::string& path,
                       std::string* error) override {
        if (denyWrites) { if (error) *error = "Access denied"; return false; }
        const std::string key = KeyOf(hive, path);
        if (!keys_.count(key)) return false;
        for (auto it = keys_.begin(); it != keys_.end();) {
            if (it->first == key || it->first.rfind(key + "\\", 0) == 0) it = keys_.erase(it);
            else ++it;
        }
        ++deletions;
        return true;
    }

    bool denyWrites = false;
    int deletions = 0;

private:
    static std::string KeyOf(RegistryHive hive, const std::string& path) {
        return std::string(RegistryHiveShortName(hive)) + "|" + Lower(path);
    }
    std::map<std::string, Node> keys_;
};

// A disk: C: and D: are fixed, E: is a removable drive (always Unknown).
// Files that exist are listed; anything else on C:/D: is missing.
class FakeDisk : public FileProbe {
public:
    void Add(const std::string& path) { present_.insert(Lower(path)); }

    PathState Check(const std::string& path) override {
        ++checks;
        const char drive = static_cast<char>(std::toupper(static_cast<unsigned char>(path[0])));
        if (drive != 'C' && drive != 'D') return PathState::Unknown;
        if (denied_.count(Lower(path))) return PathState::Unknown;
        const std::string lower = Lower(path);
        for (const auto& file : present_) {
            if (file == lower || file.rfind(lower + "\\", 0) == 0) return PathState::Exists;
        }
        return PathState::Missing;
    }
    std::string ExpandEnvironment(const std::string& text) override {
        std::string out = text;
        const std::string var = "%ProgramFiles%";
        for (size_t at = Lower(out).find(Lower(var)); at != std::string::npos;
             at = Lower(out).find(Lower(var))) {
            out.replace(at, var.size(), "C:\\Program Files");
        }
        return out;
    }
    std::string WindowsDirectory() override { return "C:\\Windows"; }

    void Deny(const std::string& path) { denied_.insert(Lower(path)); }
    int checks = 0;

private:
    std::set<std::string> present_;
    std::set<std::string> denied_;
};

constexpr RegistryHive HKCU = RegistryHive::CurrentUser;
constexpr RegistryHive HKLM = RegistryHive::LocalMachine;

const std::string kUninstall = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
const std::string kUserUninstall = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
const std::string kRun = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const std::string kAppPaths = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths";
const std::string kSharedDlls = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\SharedDLLs";
const std::string kMuiCache =
    "Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache";
const std::string kClsid = "Software\\Classes\\CLSID";

size_t CountKind(const RegistryScanReport& report, RegistryIssueKind kind) {
    size_t count = 0;
    for (const auto& finding : report.findings) {
        if (finding.kind == kind) ++count;
    }
    return count;
}

const RegistryFinding* FindingFor(const RegistryScanReport& report,
                                  const std::string& keyOrValue) {
    for (const auto& finding : report.findings) {
        if (Lower(finding.keyPath).find(Lower(keyOrValue)) != std::string::npos ||
            Lower(finding.valueName) == Lower(keyOrValue)) {
            return &finding;
        }
    }
    return nullptr;
}

std::string ReadUtf16File(const std::string& path) {
    std::FILE* file = UltraCanvas::OpenFileUtf8(path, "rb");
    if (!file) return {};
    std::string bytes;
    char buffer[4096];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, file)) > 0) bytes.append(buffer, n);
    std::fclose(file);
    // ASCII-only test content: every other byte.
    std::string text;
    for (size_t i = 2; i + 1 < bytes.size(); i += 2) text += bytes[i];
    if (bytes.size() >= 2 &&
        !(static_cast<unsigned char>(bytes[0]) == 0xFF &&
          static_cast<unsigned char>(bytes[1]) == 0xFE)) {
        return "NO BOM";
    }
    return text;
}

} // namespace

// ===== COMMAND LINES =====

TEST(RegistryCommandLineQuotedPath) {
    REQUIRE_EQ(ExecutableOfCommand("\"C:\\Program Files\\App\\app.exe\" --minimized"),
               std::string("C:\\Program Files\\App\\app.exe"));
}

TEST(RegistryCommandLineUnquotedPathWithSpaces) {
    REQUIRE_EQ(ExecutableOfCommand("C:\\Program Files\\My App\\app.exe /background"),
               std::string("C:\\Program Files\\My App\\app.exe"));
}

TEST(RegistryCommandLineIconIndexIsDropped) {
    REQUIRE_EQ(ExecutableOfCommand("C:\\Apps\\tool.exe,0"), std::string("C:\\Apps\\tool.exe"));
    REQUIRE_EQ(ExecutableOfCommand("\"C:\\Apps\\tool.exe\",-101"),
               std::string("C:\\Apps\\tool.exe"));
}

TEST(RegistryCommandLineRundllAndRegsvrNameTheirDll) {
    REQUIRE_EQ(ExecutableOfCommand(
                   "C:\\Windows\\System32\\rundll32.exe C:\\Apps\\helper.dll,Start -x"),
               std::string("C:\\Apps\\helper.dll"));
    REQUIRE_EQ(ExecutableOfCommand("regsvr32 /s /u \"C:\\Apps\\plug in.dll\""),
               std::string("C:\\Apps\\plug in.dll"));
}

TEST(RegistryCommandLineHostsAndAmbiguityAreNotPinned) {
    REQUIRE(ExecutableOfCommand("MsiExec.exe /X{12345678-1234-1234-1234-123456789012}").empty());
    REQUIRE(ExecutableOfCommand("cmd /c C:\\Apps\\start.bat").empty());
    REQUIRE(ExecutableOfCommand("C:\\Some Folder\\no extension here").empty());
    REQUIRE(ExecutableOfCommand("   ").empty());
}

TEST(RegistryJudgeOnlyAbsoluteFixedPathsOutsideWindows) {
    FakeDisk disk;
    REQUIRE(JudgePath(disk, "app.exe") == PathState::Unknown);              // on PATH
    REQUIRE(JudgePath(disk, "\\\\server\\share\\app.exe") == PathState::Unknown);
    REQUIRE(JudgePath(disk, "E:\\Portable\\app.exe") == PathState::Unknown); // removable
    REQUIRE(JudgePath(disk, "C:\\Windows\\System32\\gone.dll") == PathState::Unknown);
    REQUIRE(JudgePath(disk, "c:/windows/SysWOW64/gone.dll") == PathState::Unknown);
    REQUIRE(JudgePath(disk, "%UNKNOWNVAR%\\app.exe") == PathState::Unknown);
    REQUIRE(JudgePath(disk, "C:\\Apps\\..\\Windows\\x.dll") == PathState::Unknown);
    REQUIRE(JudgePath(disk, "C:\\WindowsApps\\gone.exe") == PathState::Missing);
    REQUIRE(JudgePath(disk, "%ProgramFiles%\\Gone\\gone.exe") == PathState::Missing);
    disk.Add("C:\\Program Files\\Here\\here.exe");
    REQUIRE(JudgePath(disk, "\"C:\\Program Files\\Here\\here.exe\"") == PathState::Exists);
}

// ===== CHECKS =====

TEST(RegistryUninstallEntryReportedOnlyWhenEverythingIsGone) {
    FakeRegistry reg;
    FakeDisk disk;
    // Gone: uninstaller and folder both missing.
    reg.SetText(HKLM, kUninstall + "\\OldApp", "DisplayName", "Old App 1.0");
    reg.SetText(HKLM, kUninstall + "\\OldApp", "UninstallString",
                "\"C:\\Program Files\\OldApp\\uninst.exe\" /S");
    reg.SetText(HKLM, kUninstall + "\\OldApp", "InstallLocation", "C:\\Program Files\\OldApp");
    // Uninstaller gone but the folder still there: kept.
    reg.SetText(HKLM, kUninstall + "\\HalfApp", "UninstallString", "C:\\Half\\uninst.exe");
    reg.SetText(HKLM, kUninstall + "\\HalfApp", "InstallLocation", "C:\\Half\\Data");
    disk.Add("C:\\Half\\Data");
    // Installed: uninstaller present.
    reg.SetText(HKLM, kUninstall + "\\GoodApp", "UninstallString", "C:\\Good\\uninst.exe");
    disk.Add("C:\\Good\\uninst.exe");
    // Windows Installer and system components are never judged.
    reg.SetText(HKLM, kUninstall + "\\{MSI}", "UninstallString", "C:\\Msi\\gone.exe");
    reg.Set(HKLM, kUninstall + "\\{MSI}", RegistryValue::DwordValue("WindowsInstaller", 1));
    reg.SetText(HKLM, kUninstall + "\\Sys", "UninstallString", "C:\\Sys\\gone.exe");
    reg.Set(HKLM, kUninstall + "\\Sys", RegistryValue::DwordValue("SystemComponent", 1));
    // Installed on a removable drive: cannot tell.
    reg.SetText(HKCU, kUserUninstall + "\\Portable", "UninstallString", "E:\\P\\uninst.exe");
    // Uninstaller gone, folder on a removable drive: cannot tell either.
    reg.SetText(HKCU, kUserUninstall + "\\Split", "UninstallString", "C:\\S\\uninst.exe");
    reg.SetText(HKCU, kUserUninstall + "\\Split", "InstallLocation", "E:\\S");

    RegistryScanner scanner;
    const auto report = scanner.Scan(reg, disk);
    REQUIRE_EQ(CountKind(report, RegistryIssueKind::ObsoleteUninstallEntry), static_cast<size_t>(1));
    const RegistryFinding* finding = FindingFor(report, "OldApp");
    REQUIRE(finding != nullptr);
    REQUIRE(finding->wholeKey);
    REQUIRE_EQ(finding->label, std::string("Old App 1.0"));
    REQUIRE_EQ(finding->target, std::string("C:\\Program Files\\OldApp\\uninst.exe"));
    REQUIRE(finding->selected);
    REQUIRE(report.entriesUnjudged >= static_cast<size_t>(2));
}

TEST(RegistryStartupEntriesForMissingProgramsAreValues) {
    FakeRegistry reg;
    FakeDisk disk;
    reg.SetText(HKCU, kRun, "OldUpdater", "\"C:\\Program Files\\Old\\updater.exe\" -tray");
    reg.SetText(HKCU, kRun, "Present", "C:\\Present\\p.exe");
    reg.SetText(HKCU, kRun, "OnPath", "helper.exe --quiet");
    reg.SetText(HKCU, kRun, "SystemTool", "C:\\Windows\\System32\\gone.exe");
    disk.Add("C:\\Present\\p.exe");

    RegistryScanner scanner;
    const auto report = scanner.Scan(reg, disk);
    REQUIRE_EQ(report.findings.size(), static_cast<size_t>(1));
    REQUIRE_EQ(report.findings[0].valueName, std::string("OldUpdater"));
    REQUIRE(!report.findings[0].wholeKey);
    REQUIRE_EQ(report.findings[0].keyPath, kRun);
}

TEST(RegistryAppPathsSharedDllsMuiCacheAndHelp) {
    FakeRegistry reg;
    FakeDisk disk;
    reg.SetText(HKLM, kAppPaths + "\\gone.exe", "", "C:\\Gone\\gone.exe");
    reg.SetText(HKLM, kAppPaths + "\\here.exe", "", "\"C:\\Here\\here.exe\"");
    disk.Add("C:\\Here\\here.exe");

    reg.Set(HKLM, kSharedDlls, RegistryValue::DwordValue("C:\\Gone\\shared.dll", 1));
    reg.Set(HKLM, kSharedDlls, RegistryValue::DwordValue("C:\\Here\\here.exe", 2));

    reg.SetText(HKCU, kMuiCache, "C:\\Gone\\gone.exe.FriendlyAppName", "Gone");
    reg.SetText(HKCU, kMuiCache, "C:\\Here\\here.exe.ApplicationCompany", "Here Inc");
    reg.SetText(HKCU, kMuiCache, "@shell32.dll,-12345", "Something");
    reg.SetText(HKCU, kMuiCache, "LangID", "x");

    reg.SetText(HKLM, "SOFTWARE\\Microsoft\\Windows\\HTML Help", "gone.chm", "C:\\Gone\\");
    reg.SetText(HKLM, "SOFTWARE\\Microsoft\\Windows\\HTML Help", "here.chm", "C:\\Here");
    disk.Add("C:\\Here\\here.chm");

    RegistryScanner scanner;
    const auto report = scanner.Scan(reg, disk);
    REQUIRE_EQ(CountKind(report, RegistryIssueKind::MissingAppPath), static_cast<size_t>(1));
    REQUIRE_EQ(CountKind(report, RegistryIssueKind::MissingSharedDll), static_cast<size_t>(1));
    REQUIRE_EQ(CountKind(report, RegistryIssueKind::StaleMuiCacheEntry), static_cast<size_t>(1));
    REQUIRE_EQ(CountKind(report, RegistryIssueKind::MissingHelpFile), static_cast<size_t>(1));
    REQUIRE(FindingFor(report, "gone.exe") != nullptr);
    REQUIRE_EQ(report.kinds.size(), static_cast<size_t>(4));
}

TEST(RegistryComClassNeedsEveryServerGoneAndStartsUnticked) {
    FakeRegistry reg;
    FakeDisk disk;
    const std::string orphan = kClsid + "\\{AAAAAAAA-0000-0000-0000-000000000001}";
    reg.SetText(HKCU, orphan, "", "Old Shell Extension");
    reg.SetText(HKCU, orphan + "\\InprocServer32", "", "C:\\Old\\ext.dll");
    const std::string mixed = kClsid + "\\{AAAAAAAA-0000-0000-0000-000000000002}";
    reg.SetText(HKCU, mixed + "\\InprocServer32", "", "C:\\Old\\gone.dll");
    reg.SetText(HKCU, mixed + "\\LocalServer32", "", "C:\\Here\\server.exe");
    disk.Add("C:\\Here\\server.exe");
    const std::string system = kClsid + "\\{AAAAAAAA-0000-0000-0000-000000000003}";
    reg.SetText(HKCU, system + "\\InprocServer32", "", "mscoree.dll");
    const std::string alias = kClsid + "\\{AAAAAAAA-0000-0000-0000-000000000004}";
    reg.SetText(HKCU, alias + "\\TreatAs", "", "{something}");

    RegistryScanner scanner;
    const auto report = scanner.Scan(reg, disk);
    REQUIRE_EQ(report.findings.size(), static_cast<size_t>(1));
    REQUIRE_EQ(report.findings[0].label, std::string("Old Shell Extension"));
    REQUIRE(!report.findings[0].selected);
}

// ===== GUARD =====

TEST(RegistryGuardAcceptsOnlyTheTableRoots) {
    RegistryFinding key;
    key.kind = RegistryIssueKind::ObsoleteUninstallEntry;
    key.hive = HKLM;
    key.wholeKey = true;
    key.keyPath = kUninstall + "\\OldApp";
    REQUIRE(RegistryScanner::InsideAllowedRoot(key));

    key.keyPath = kUninstall;                              // the root itself
    REQUIRE(!RegistryScanner::InsideAllowedRoot(key));
    key.keyPath = kUninstall + "\\OldApp\\Deeper";         // two levels down
    REQUIRE(!RegistryScanner::InsideAllowedRoot(key));
    key.keyPath = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run\\X";
    REQUIRE(!RegistryScanner::InsideAllowedRoot(key));
    key.keyPath = kUninstall + "\\OldApp";
    key.hive = HKCU;                                       // wrong hive's spelling
    key.keyPath = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\..";
    REQUIRE(!RegistryScanner::InsideAllowedRoot(key));

    RegistryFinding value;
    value.kind = RegistryIssueKind::MissingStartupProgram;
    value.hive = HKCU;
    value.keyPath = kRun;
    value.valueName = "OldUpdater";
    REQUIRE(RegistryScanner::InsideAllowedRoot(value));
    value.wholeKey = true;                                 // a value kind never takes a key
    REQUIRE(!RegistryScanner::InsideAllowedRoot(value));
    value.wholeKey = false;
    value.kind = RegistryIssueKind::MissingSharedDll;      // right key, wrong check
    REQUIRE(!RegistryScanner::InsideAllowedRoot(value));
}

// ===== BACKUP =====

TEST(RegistryBackupTextRecreatesKeysAndValues) {
    FakeRegistry reg;
    reg.SetText(HKLM, kUninstall + "\\OldApp", "DisplayName", "Old \"quoted\" App");
    reg.SetText(HKLM, kUninstall + "\\OldApp", "UninstallString", "C:\\Old\\uninst.exe");
    reg.Set(HKLM, kUninstall + "\\OldApp", RegistryValue::DwordValue("EstimatedSize", 0x1234));
    reg.Set(HKLM, kUninstall + "\\OldApp",
            RegistryValue::String("Expand", "%ProgramFiles%\\Old", RegExpand));
    reg.SetText(HKLM, kUninstall + "\\OldApp\\Sub", "", "child");
    reg.SetText(HKCU, kRun, "OldUpdater", "C:\\Old\\u.exe");

    RegistryFinding key;
    key.kind = RegistryIssueKind::ObsoleteUninstallEntry;
    key.hive = HKLM;
    key.wholeKey = true;
    key.keyPath = kUninstall + "\\OldApp";
    RegistryFinding value;
    value.kind = RegistryIssueKind::MissingStartupProgram;
    value.hive = HKCU;
    value.keyPath = kRun;
    value.valueName = "OldUpdater";

    const std::string text = BuildRegistryBackup(reg, { key, value });
    auto has = [&](const std::string& needle) { return text.find(needle) != std::string::npos; };
    REQUIRE(text.rfind("Windows Registry Editor Version 5.00\r\n", 0) == 0);
    REQUIRE(has("[HKEY_LOCAL_MACHINE\\" + kUninstall + "\\OldApp]\r\n"));
    REQUIRE(has("\"DisplayName\"=\"Old \\\"quoted\\\" App\"\r\n"));
    REQUIRE(has("\"UninstallString\"=\"C:\\\\Old\\\\uninst.exe\"\r\n"));
    REQUIRE(has("\"EstimatedSize\"=dword:00001234\r\n"));
    REQUIRE(has("\"Expand\"=hex(2):25,00,50,00"));
    REQUIRE(has("[HKEY_LOCAL_MACHINE\\" + kUninstall + "\\OldApp\\Sub]\r\n@=\"child\"\r\n"));
    REQUIRE(has("[HKEY_CURRENT_USER\\" + kRun + "]\r\n\"OldUpdater\"=\"C:\\\\Old\\\\u.exe\"\r\n"));
}

TEST(RegistryBackupLongHexLinesAreWrapped) {
    FakeRegistry reg;
    RegistryValue blob;
    blob.name = "Blob";
    blob.type = RegBinary;
    blob.data.assign(200, 0xAB);
    reg.Set(HKCU, kRun, blob);
    RegistryFinding value;
    value.kind = RegistryIssueKind::MissingStartupProgram;
    value.hive = HKCU;
    value.keyPath = kRun;
    value.valueName = "Blob";
    const std::string text = BuildRegistryBackup(reg, { value });
    size_t lineStart = 0;
    while (lineStart < text.size()) {
        size_t end = text.find("\r\n", lineStart);
        if (end == std::string::npos) end = text.size();
        REQUIRE(end - lineStart <= 80);
        lineStart = end + 2;
    }
    REQUIRE(text.find(",\\\r\n  ab") != std::string::npos);
}

// ===== CLEANING =====

TEST(RegistryCleanSimulatesByDefault) {
    FakeRegistry reg;
    FakeDisk disk;
    reg.SetText(HKCU, kRun, "OldUpdater", "C:\\Old\\u.exe");
    RegistryScanner scanner;
    const auto report = scanner.Scan(reg, disk);
    RegistryCleaner cleaner;
    const auto result = cleaner.Clean(reg, disk, report);
    REQUIRE(result.simulated);
    REQUIRE_EQ(result.removed, static_cast<size_t>(1));
    REQUIRE(result.backupPath.empty());
    REQUIRE_EQ(reg.deletions, 0);
    REQUIRE_EQ(reg.Values(HKCU, kRun).size(), static_cast<size_t>(1));
}

TEST(RegistryCleanBacksUpThenRemovesTheTickedOnly) {
    TempTree tree;
    FakeRegistry reg;
    FakeDisk disk;
    reg.SetText(HKCU, kRun, "OldUpdater", "C:\\Old\\u.exe");
    reg.SetText(HKCU, kRun, "AlsoOld", "C:\\Old\\v.exe");
    reg.SetText(HKLM, kUninstall + "\\OldApp", "UninstallString", "C:\\Old\\uninst.exe");
    reg.SetText(HKLM, kUninstall + "\\OldApp\\Sub", "x", "y");

    RegistryScanner scanner;
    auto report = scanner.Scan(reg, disk);
    REQUIRE_EQ(report.findings.size(), static_cast<size_t>(3));
    for (auto& finding : report.findings) {
        if (finding.valueName == "AlsoOld") finding.selected = false;
    }

    RegistryCleanOptions options;
    options.simulate = false;
    options.backupPath = tree.Dir("backups") + "/registry.reg";
    RegistryCleaner cleaner;
    const auto result = cleaner.Clean(reg, disk, report, options);

    REQUIRE(!result.simulated);
    REQUIRE(!result.aborted);
    REQUIRE_EQ(result.removed, static_cast<size_t>(2));
    REQUIRE(result.failures.empty());
    REQUIRE_EQ(result.backupPath, options.backupPath);
    REQUIRE(!reg.KeyExists(HKLM, kUninstall + "\\OldApp"));
    REQUIRE(!reg.KeyExists(HKLM, kUninstall + "\\OldApp\\Sub"));
    const auto left = reg.Values(HKCU, kRun);
    REQUIRE_EQ(left.size(), static_cast<size_t>(1));
    REQUIRE_EQ(left[0].name, std::string("AlsoOld"));

    const std::string backup = ReadUtf16File(options.backupPath);
    REQUIRE(backup != "NO BOM");
    REQUIRE(backup.find("\"OldUpdater\"") != std::string::npos);
    REQUIRE(backup.find("OldApp\\Sub]") != std::string::npos);
    REQUIRE(backup.find("AlsoOld") == std::string::npos);
}

TEST(RegistryCleanSkipsWhatWasReinstalledSinceTheScan) {
    TempTree tree;
    FakeRegistry reg;
    FakeDisk disk;
    reg.SetText(HKCU, kRun, "OldUpdater", "C:\\Old\\u.exe");
    RegistryScanner scanner;
    const auto report = scanner.Scan(reg, disk);
    REQUIRE_EQ(report.findings.size(), static_cast<size_t>(1));

    disk.Add("C:\\Old\\u.exe");   // reinstalled between scan and clean
    RegistryCleanOptions options;
    options.simulate = false;
    options.backupPath = tree.Dir("b") + "/r.reg";
    RegistryCleaner cleaner;
    const auto result = cleaner.Clean(reg, disk, report, options);
    REQUIRE_EQ(result.removed, static_cast<size_t>(0));
    REQUIRE_EQ(result.skippedChanged, static_cast<size_t>(1));
    REQUIRE_EQ(reg.deletions, 0);
    REQUIRE(!tree.Exists("b/r.reg"));   // nothing to back up, nothing written
}

TEST(RegistryCleanRefusesAFindingOutsideTheRoots) {
    TempTree tree;
    FakeRegistry reg;
    FakeDisk disk;
    reg.SetText(HKLM, "SOFTWARE\\Precious\\Thing", "UninstallString", "C:\\Old\\uninst.exe");
    RegistryScanReport report;
    RegistryFinding forged;
    forged.kind = RegistryIssueKind::ObsoleteUninstallEntry;
    forged.hive = HKLM;
    forged.wholeKey = true;
    forged.keyPath = "SOFTWARE\\Precious\\Thing";
    forged.target = "C:\\Old\\uninst.exe";
    report.findings.push_back(forged);

    RegistryCleanOptions options;
    options.simulate = false;
    options.backupPath = tree.Dir("b") + "/r.reg";
    RegistryCleaner cleaner;
    const auto result = cleaner.Clean(reg, disk, report, options);
    REQUIRE_EQ(result.refusedByGuard, static_cast<size_t>(1));
    REQUIRE_EQ(result.removed, static_cast<size_t>(0));
    REQUIRE(reg.KeyExists(HKLM, "SOFTWARE\\Precious\\Thing"));
}

TEST(RegistryCleanRemovesNothingWhenTheBackupFails) {
    TempTree tree;
    FakeRegistry reg;
    FakeDisk disk;
    reg.SetText(HKCU, kRun, "OldUpdater", "C:\\Old\\u.exe");
    RegistryScanner scanner;
    const auto report = scanner.Scan(reg, disk);

    // The backup's folder is a file: it cannot be created.
    tree.File("blocker", 1);
    RegistryCleanOptions options;
    options.simulate = false;
    options.backupPath = tree.Path("blocker") + "/sub/r.reg";
    RegistryCleaner cleaner;
    const auto result = cleaner.Clean(reg, disk, report, options);
    REQUIRE(result.aborted);
    REQUIRE_EQ(result.removed, static_cast<size_t>(0));
    REQUIRE_EQ(result.failures.size(), static_cast<size_t>(1));
    REQUIRE_EQ(reg.deletions, 0);
    REQUIRE_EQ(reg.Values(HKCU, kRun).size(), static_cast<size_t>(1));
}

TEST(RegistryCleanReportsAccessDenied) {
    TempTree tree;
    FakeRegistry reg;
    FakeDisk disk;
    reg.SetText(HKLM, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", "Old", "C:\\Old\\u.exe");
    RegistryScanner scanner;
    const auto report = scanner.Scan(reg, disk);
    reg.denyWrites = true;
    RegistryCleanOptions options;
    options.simulate = false;
    options.backupPath = tree.Dir("b") + "/r.reg";
    RegistryCleaner cleaner;
    const auto result = cleaner.Clean(reg, disk, report, options);
    REQUIRE_EQ(result.removed, static_cast<size_t>(0));
    REQUIRE_EQ(result.failures.size(), static_cast<size_t>(1));
    REQUIRE(result.failures[0].reason.find("Access denied") != std::string::npos);
}

TEST(RegistryUnavailableOffWindows) {
#if defined(_WIN32) || defined(_WIN64)
    REQUIRE(RegistryCleaningAvailable());
    REQUIRE(CreateSystemRegistryStore() != nullptr);
#else
    REQUIRE(!RegistryCleaningAvailable());
    REQUIRE(CreateSystemRegistryStore() == nullptr);
    REQUIRE(CreateSystemFileProbe() == nullptr);
#endif
}
