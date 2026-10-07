// Apps/UltraSocial/engine/UltraSocialPaths.h
// Where UltraSocial keeps its data: one per-user application data folder
// holding the account / outbox / history database (ultrasocial.db) and the
// credential vault (vault/ultrasocial.vault - UltraVault::DeviceKeyVault, see
// UltraSocialCredentialVault.h, so no login is ever written outside it).
//
//   Windows  %APPDATA%\UltraSocial
//   macOS    ~/Library/Application Support/UltraSocial
//   others   $XDG_DATA_HOME/UltraSocial, else ~/.local/share/UltraSocial
//
// $XDG_DATA_HOME, when set, wins on every platform (as in UltraMail), so a
// test or a portable setup can point the app elsewhere.
//
// Up to 0.1.x the folder was $XDG_DATA_HOME or ~/.local/share everywhere, and
// "UltraSocial" in the working directory when HOME was unset - the normal
// case on Windows, so the database and the vault landed wherever the app was
// started from. AdoptLegacyDataDir moves such a folder into place once. The
// database was called social.db then, a name that says too little next to
// other applications' files; RenameLegacyDatabase gives it its own.
//
// All strings are UTF-8 (UltraCanvasPathUtf8.h).
// Version: 0.1.0 - the per-platform application data folder
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>
#include <vector>

namespace UltraSocial {

// The database file in the data folder, and its name up to 0.1.x.
inline constexpr const char* kDatabaseFileName       = "ultrasocial.db";
inline constexpr const char* kLegacyDatabaseFileName = "social.db";

// <dataDir>/ultrasocial.db
std::string DatabasePath(const std::string& dataDir);

enum class DataDirPlatform { Windows, MacOS, Unix };

// The platform this build runs on.
DataDirPlatform CurrentDataDirPlatform();

// The data folder for the given platform and environment values (each may be
// empty). Pure, so every platform's rule is testable anywhere. Empty when the
// environment names no place for it.
std::string ResolveDataDir(DataDirPlatform platform,
                           const std::string& xdgDataHome,
                           const std::string& appData,
                           const std::string& home);

// ResolveDataDir for this platform, reading XDG_DATA_HOME, APPDATA and HOME
// as UTF-8 (GetEnvUtf8).
std::string DefaultDataDir();

// Where 0.1.x may have left its data on this platform, other than where it
// goes now: ~/.local/share/UltraSocial on macOS and Windows, and on Windows
// also "UltraSocial" in the working directory and beside the executable.
// Pure, like ResolveDataDir. Linux kept its folder, so it has none.
std::vector<std::string> LegacyDataDirs(DataDirPlatform platform,
                                        const std::string& home,
                                        const std::string& currentDir,
                                        const std::string& executableDir);

// When `dataDir` holds no database yet (ultrasocial.db, or social.db from
// 0.1.x), move the first of `candidates` that does - the whole folder, vault
// included - to `dataDir`: renamed when it can be, copied and then removed
// when the two are on different drives. Returns the folder the data came
// from; empty when nothing was moved. A failure leaves the old folder where
// it was and says why in `error`.
std::string AdoptLegacyDataDir(const std::string& dataDir,
                               const std::vector<std::string>& candidates,
                               std::string& error);

// A social.db in `dataDir` (0.1.x's name) becomes ultrasocial.db, with any
// SQLite journal files beside it (-journal, -wal, -shm) so nothing committed
// is lost. Nothing happens when ultrasocial.db is already there. True when
// there was a file to rename and it was renamed.
bool RenameLegacyDatabase(const std::string& dataDir, std::string& error);

// Create `dataDir` if needed and, outside Windows (where %APPDATA% is
// per-user already), make it readable by its owner alone: it holds the vault
// and the key that opens it. False when the folder cannot be created.
bool PrepareDataDir(const std::string& dataDir, std::string& error);

} // namespace UltraSocial
