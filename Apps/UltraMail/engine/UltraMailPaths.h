// Apps/UltraMail/engine/UltraMailPaths.h
// Where UltraMail keeps its data: one per-user application data folder
// holding the mailbox database (mail.db), the contacts, outbox and cloud
// databases, the per-account message store, the credential vault (vault/),
// preferences.ini, oauth.ini and trace.log.
//
//   Windows  %APPDATA%\UltraMail
//   macOS    ~/Library/Application Support/UltraMail
//   others   $XDG_DATA_HOME/UltraMail, else ~/.local/share/UltraMail
//
// $XDG_DATA_HOME, when set, wins on every platform, so a test or a portable
// setup can point the app elsewhere. The rule is the framework's
// (UltraCanvasDataFolder.h), shared with UltraPassword, UltraAuthenticator,
// UltraSocial and EmailCleaner; this file adds UltraMail's folder name, the
// file that marks a folder as its own, and where earlier versions kept it.
//
// Up to 0.10.50 macOS was treated like Linux and the folder was
// ~/.local/share/UltraMail there (as it was on Windows without %APPDATA%).
// AdoptLegacyDataDir moves such a folder into place once, whole, so the
// accounts, the mail and the vault open as before.
//
// All strings are UTF-8 (UltraCanvasPathUtf8.h).
// Version: 0.1.1 - the rule and the move come from the framework's UltraCanvasDataFolder.h
// Version: 0.1.0 - the per-platform application data folder
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasDataFolder.h"   // DataFolderPlatform

#include <string>
#include <vector>

namespace UltraMail {

// The folder's name under the platform's application data root.
inline constexpr const char* kDataFolderName = "UltraMail";

// The mailbox database (UltraMailApp::Initialize opens it first of all, so a
// folder UltraMail has ever started in holds one).
inline constexpr const char* kMailboxFileName = "mail.db";

using DataDirPlatform = UltraCanvas::DataFolderPlatform;

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

// Where earlier versions may have left the data on this platform, other than
// where it goes now: ~/.local/share/UltraMail on macOS, and on Windows when
// HOME is set (what 0.10.50 used there without %APPDATA%). Pure, like
// ResolveDataDir. Linux kept its folder, so it has none.
std::vector<std::string> LegacyDataDirs(DataDirPlatform platform,
                                        const std::string& home);

// When `dataDir` holds no mailbox database yet, move the first of
// `candidates` that does - the whole folder, vault included - to `dataDir`:
// renamed when it can be, copied and then removed when the two are on
// different drives. Returns the folder the data came from; empty when nothing
// was moved. A failure leaves the old folder where it was and says why in
// `error`.
std::string AdoptLegacyDataDir(const std::string& dataDir,
                               const std::vector<std::string>& candidates,
                               std::string& error);

} // namespace UltraMail
