// Apps/UltraCleaner/engine/UltraCleanerRegistry.h
// The Windows registry half of UltraCleaner: finds entries that point at
// programs, files and folders that are no longer there — uninstall entries of
// software that was deleted by hand, startup commands for missing programs,
// App Paths, shared-DLL counters, MUI-cache names, help-file and installer
// folder records, per-user COM servers — backs every one of them up to a
// .reg file and only then removes it.
//
// What it does NOT do is the guesswork the "registry optimiser" products
// sell: it never touches an entry because it looks unused, never compacts
// the hive, and never removes something whose target it cannot see. An
// entry is reported only when its target is provably missing:
//
//   * the path is absolute, on a drive letter that is a fixed local disk
//     that is present right now (a USB stick, a network share, a DVD or a
//     subst'ed drive that is not mounted is "unknown", never "missing");
//   * the filesystem answers "not found" — not "access denied", not "busy";
//   * it is outside the Windows folder, where WOW64 redirection, WinSxS and
//     the system's own repair tooling make "missing" a judgement this
//     program has no business making;
//   * for an uninstall entry, every location it names is gone — the
//     uninstaller and the install folder alike — and it is not owned by
//     Windows Installer or marked as a system component.
//
// Removal is the same two-step guard the file rules use. Every finding is
// re-read and re-checked immediately before it is removed, and refused if
// it changed or if its key is not directly inside one of the roots the
// check table names. Before the first removal, everything that is about to
// go is written to a .reg backup; if the backup cannot be written nothing is
// removed. Double-clicking that file (or `UltraCleaner --registry-restore`)
// puts it all back.
//
// The engine is headless and platform-neutral: it talks to a RegistryStore
// and a FileProbe, so the test suite runs every check on Linux against an
// in-memory registry. CreateSystemRegistryStore / CreateSystemFileProbe give
// the real ones on Windows and nullptr everywhere else.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCleaner {

// ===== THE REGISTRY, AS THE ENGINE SEES IT =====

enum class RegistryHive {
    CurrentUser,    // HKEY_CURRENT_USER
    LocalMachine    // HKEY_LOCAL_MACHINE
};

// "HKEY_CURRENT_USER" / "HKEY_LOCAL_MACHINE" — the spelling a .reg file uses.
const char* RegistryHiveName(RegistryHive hive);
// "HKCU" / "HKLM", for lists.
const char* RegistryHiveShortName(RegistryHive hive);

// The value types the engine distinguishes. Anything else round-trips
// through the backup as hex(<type>) unchanged.
enum RegistryValueType : uint32_t {
    RegNone      = 0,
    RegString    = 1,   // REG_SZ
    RegExpand    = 2,   // REG_EXPAND_SZ
    RegBinary    = 3,   // REG_BINARY
    RegDword     = 4,   // REG_DWORD
    RegMultiStr  = 7,   // REG_MULTI_SZ
    RegQword     = 11   // REG_QWORD
};

struct RegistryValue {
    std::string name;            // "" is the key's default value
    uint32_t type = RegNone;
    std::vector<uint8_t> data;   // exactly as stored: UTF-16LE for strings

    bool IsString() const { return type == RegString || type == RegExpand; }
    // REG_SZ / REG_EXPAND_SZ as UTF-8, without the terminator; "" otherwise.
    std::string Text() const;
    // REG_DWORD as a number; `fallback` for any other type.
    uint32_t Dword(uint32_t fallback = 0) const;

    static RegistryValue String(const std::string& name, const std::string& utf8,
                                uint32_t type = RegString);
    static RegistryValue DwordValue(const std::string& name, uint32_t value);
};

// Reads and removes keys and values. Paths are below the hive, separated by
// backslashes, compared without regard to ASCII case, as Windows does.
class RegistryStore {
public:
    virtual ~RegistryStore() = default;

    virtual bool KeyExists(RegistryHive hive, const std::string& path) = 0;
    // Names of the immediate subkeys; empty when the key cannot be read.
    virtual std::vector<std::string> SubKeys(RegistryHive hive,
                                             const std::string& path) = 0;
    virtual std::vector<RegistryValue> Values(RegistryHive hive,
                                              const std::string& path) = 0;
    virtual bool DeleteValue(RegistryHive hive, const std::string& path,
                             const std::string& name, std::string* error) = 0;
    // Removes the key with everything below it.
    virtual bool DeleteKeyTree(RegistryHive hive, const std::string& path,
                               std::string* error) = 0;
};

// What the filesystem says about a path a registry entry names.
enum class PathState {
    Exists,
    Missing,     // provably not there: "file/path not found" on a fixed disk
    Unknown      // could not be judged — never a reason to remove anything
};

class FileProbe {
public:
    virtual ~FileProbe() = default;
    // `path` is absolute, drive-letter form, with %VARIABLES% expanded.
    virtual PathState Check(const std::string& path) = 0;
    // Expands %VARIABLE% references; an unknown variable is left as it is.
    virtual std::string ExpandEnvironment(const std::string& text) = 0;
    // "C:\Windows" — entries inside it are never judged.
    virtual std::string WindowsDirectory() = 0;
};

// The real registry and filesystem on Windows; nullptr on other platforms.
std::unique_ptr<RegistryStore> CreateSystemRegistryStore();
std::unique_ptr<FileProbe> CreateSystemFileProbe();
bool RegistryCleaningAvailable();

// ===== WHAT A SCAN FINDS =====

enum class RegistryIssueKind {
    ObsoleteUninstallEntry,   // "Apps & features" lists software that is gone
    MissingStartupProgram,    // Run / RunOnce names a missing program
    MissingAppPath,           // App Paths entry for a missing program
    MissingSharedDll,         // SharedDLLs counter for a missing file
    StaleMuiCacheEntry,       // Explorer's cached name of a missing program
    MissingHelpFile,          // Help / HTML Help registration of a missing file
    MissingInstallerFolder,   // Installer\Folders record of a missing folder
    OrphanedComServer,        // per-user COM class whose server file is gone
    KindCount
};

const char* RegistryIssueTitle(RegistryIssueKind kind);
const char* RegistryIssueDescription(RegistryIssueKind kind);
// Whether findings of this kind are ticked after a scan. The kinds that
// other software is likelier to recreate or rely on start unticked.
bool RegistryIssueSafeByDefault(RegistryIssueKind kind);

struct RegistryFinding {
    RegistryIssueKind kind = RegistryIssueKind::ObsoleteUninstallEntry;
    RegistryHive hive = RegistryHive::CurrentUser;
    std::string keyPath;        // below the hive
    bool wholeKey = false;      // remove the key itself, not one value
    std::string valueName;      // when !wholeKey; "" is the default value
    std::string target;         // the missing path that is the evidence
    std::string label;          // what it is about: a program name, a file
    bool selected = true;

    // "HKEY_CURRENT_USER\Software\...\Run" — for lists and messages.
    std::string FullKeyName() const;
    // The key, plus " → value" for a value finding.
    std::string Location() const;
};

struct RegistryKindSummary {
    RegistryIssueKind kind;
    size_t count = 0;
    bool safeByDefault = true;
};

struct RegistryScanReport {
    std::vector<RegistryFinding> findings;
    std::vector<RegistryKindSummary> kinds;    // only kinds with findings
    size_t entriesExamined = 0;
    size_t entriesUnjudged = 0;   // target unknown: removable drive, no access…
    bool cancelled = false;

    size_t SelectedCount() const;
};

struct RegistryScanProgress {
    std::string currentCheck;
    size_t entriesExamined = 0;
    size_t findings = 0;
};

using RegistryScanProgressCallback = std::function<void(const RegistryScanProgress&)>;

// ===== COMMAND LINES AND PATHS =====
// Exposed for the tests; the scanner is the only caller.

// The file a command line runs: a quoted first token, an unquoted path with
// spaces resolved the way CreateProcess does (the shortest prefix ending in
// an executable extension), and the DLL behind `rundll32 x.dll,Entry` and
// `regsvr32 x.dll`. Returns "" for a command that cannot be pinned to one
// file — a bare program name found on PATH, msiexec, a script host. A
// trailing ",<icon index>" is dropped.
std::string ExecutableOfCommand(const std::string& commandLine);

// The state of `rawPath` as evidence: Unknown unless it is an absolute
// drive-letter path outside the Windows folder, else whatever the probe says.
PathState JudgePath(FileProbe& probe, const std::string& rawPath);

// ===== SCANNER =====

class RegistryScanner {
public:
    RegistryScanReport Scan(RegistryStore& store, FileProbe& probe,
                            const RegistryScanProgressCallback& progress = nullptr);

    // Re-examines one finding against the registry and the disk as they are
    // now: true only if it is still exactly as defective as when found.
    static bool StillDefective(RegistryStore& store, FileProbe& probe,
                               const RegistryFinding& finding);

    // The key a finding of this kind must sit in (value findings) or directly
    // below (key findings). Removal refuses anything else.
    static bool InsideAllowedRoot(const RegistryFinding& finding);

    void RequestCancel() { cancel_ = true; }
    void ResetCancel() { cancel_ = false; }

private:
    std::atomic<bool> cancel_{false};
};

// ===== BACKUP AND REMOVAL =====

// The .reg text (regedit's "Version 5.00" format) that recreates every
// selected finding: the whole subtree of a key finding, the one value of a
// value finding. UTF-8 here; WriteRegistryBackup stores it as UTF-16LE with
// a byte-order mark, which is what regedit and reg.exe expect.
std::string BuildRegistryBackup(RegistryStore& store,
                                const std::vector<RegistryFinding>& findings);
bool WriteRegistryBackup(const std::string& path, const std::string& regText,
                         std::string* error);
// %LOCALAPPDATA%\UltraCleaner\RegistryBackups\registry-<time>.reg
std::string DefaultRegistryBackupPath();
// Merges a backup back in (reg.exe import). Windows only.
bool RestoreRegistryBackup(const std::string& path, std::string* error);

struct RegistryCleanOptions {
    // Report what would go, write nothing, remove nothing. The default, so
    // a caller that forgets to choose cannot change the registry.
    bool simulate = true;
    std::string backupPath;   // "" → DefaultRegistryBackupPath()
};

struct RegistryCleanFailure {
    std::string location;
    std::string reason;
};

struct RegistryCleanReport {
    bool simulated = true;
    std::string backupPath;          // "" when no backup was needed or written
    size_t removed = 0;
    size_t skippedChanged = 0;       // fixed, reinstalled or gone since the scan
    size_t refusedByGuard = 0;       // outside the allowed roots
    std::vector<RegistryCleanFailure> failures;
    bool aborted = false;            // the backup failed; nothing was touched
    bool cancelled = false;
};

using RegistryCleanProgressCallback =
    std::function<void(size_t done, size_t total, const std::string& location)>;

class RegistryCleaner {
public:
    RegistryCleanReport Clean(RegistryStore& store, FileProbe& probe,
                              const RegistryScanReport& report,
                              const RegistryCleanOptions& options = {},
                              const RegistryCleanProgressCallback& progress = nullptr);

    void RequestCancel() { cancel_ = true; }
    void ResetCancel() { cancel_ = false; }

private:
    std::atomic<bool> cancel_{false};
};

} // namespace UltraCleaner
