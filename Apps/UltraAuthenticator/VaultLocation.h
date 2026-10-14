// Apps/UltraAuthenticator/VaultLocation.h
// Where UltraAuthenticator keeps its vault when no --vault path is given: one
// per-user application data folder holding accounts.vault and, beside it,
// settings.ini (Preferences.h).
//
//   Windows  %APPDATA%\UltraAuthenticator
//   macOS    ~/Library/Application Support/UltraAuthenticator
//   others   $XDG_DATA_HOME/UltraAuthenticator, else ~/.local/share/UltraAuthenticator
//
// $XDG_DATA_HOME, when set, wins on every platform, so a test or a portable
// setup can point the app elsewhere. The rule is the framework's
// (UltraCanvasDataFolder.h), shared with UltraMail, UltraPassword, UltraSocial
// and EmailCleaner; this file adds UltraAuthenticator's folder name, the vault
// file that marks a folder as its own, and where earlier versions kept it.
//
// Up to 0.2.6 the folder was ~/.local/share/UltraAuthenticator wherever HOME
// was set - on macOS always, and on Windows ahead of %APPDATA%. AdoptLegacyDataDir
// moves such a folder into place once, whole, so the vault opens as before
// and the settings beside it come along.
//
// All strings are UTF-8 (UltraCanvasPathUtf8.h).
// Version: 0.1.0 - the per-platform application data folder
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasDataFolder.h"   // DataFolderPlatform

#include <string>
#include <vector>

namespace UltraCanvas {
namespace Authenticator {

// The folder's name under the platform's application data root, and the
// vault file in it.
inline constexpr const char* kDataFolderName = "UltraAuthenticator";
inline constexpr const char* kVaultFileName  = "accounts.vault";

using DataDirPlatform = DataFolderPlatform;

// The platform this build runs on.
DataDirPlatform CurrentDataDirPlatform();

// The data folder for the given platform and environment values (each may be
// empty). Pure, so every platform's rule is testable anywhere. Empty when the
// environment names no place for it.
std::string ResolveDataDir(DataDirPlatform platform,
                           const std::string& xdgDataHome,
                           const std::string& appData,
                           const std::string& home);

// Where 0.2.6 and earlier may have left the vault on this platform, other
// than where it goes now: ~/.local/share/UltraAuthenticator on macOS, and on
// Windows when HOME is set. Pure, like ResolveDataDir. Linux kept its
// folder, so it has none.
std::vector<std::string> LegacyDataDirs(DataDirPlatform platform,
                                        const std::string& home);

// When `dataDir` holds no vault file yet, move the first of `candidates` that
// does - the whole folder, settings included - to `dataDir`: renamed when it
// can be, copied and then removed when the two are on different drives.
// Returns the folder the vault came from; empty when nothing was moved. A
// failure leaves the old folder where it was and says why in `error`.
std::string AdoptLegacyDataDir(const std::string& dataDir,
                               const std::vector<std::string>& candidates,
                               std::string& error);

// <dataDir>/accounts.vault
std::string VaultPathIn(const std::string& dataDir);

// The default vault file: ResolveDataDir for this platform (XDG_DATA_HOME,
// APPDATA and HOME read as UTF-8), the working directory when the environment
// names no home; a vault an earlier version left elsewhere is moved into place
// first (AdoptLegacyDataDir), and the folder is created. `movedFrom`, when
// given, receives the folder the vault was moved from, and `error` what went
// wrong moving it; neither stops the path from being returned.
std::string DefaultVaultPath(std::string* movedFrom = nullptr, std::string* error = nullptr);

} // namespace Authenticator
} // namespace UltraCanvas
