// Apps/UltraPress/hosting/UltraPressHosting.h
// The hosts a site is published to: UltraCloud accounts - FTP, FTPS, SFTP,
// WebDAV, Nextcloud - kept by the ultrapress program in its data folder
// (hosts.db and the credential vault beside it; the folder is the
// framework's one rule for an application's data, UltraCanvasDataFolder.h),
// the way UltraMail keeps its accounts. A site names one of them in site.json
// (`hosting`); the engine's deploy (engine/UltraPressDeploy.h) gets an
// Uploader onto it from here. Built only where UltraCloud is; the program
// says so when it is not.
// Version: 0.6.0 - the data folder rule: DataDirectory, the 0.5 settings folder adopted on the first open
// Version: 0.5.1 - the remove verb for pruning
// Version: 0.5.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraPressDeploy.h"

#include <UltraCloud/UltraCloudService.h>

#include <memory>
#include <string>
#include <vector>

namespace UltraPress {

class Hosts {
public:
    Hosts();
    ~Hosts();
    Hosts(const Hosts&) = delete;
    Hosts& operator=(const Hosts&) = delete;

    // Opens the account list and the vault once; false with the reason. The
    // providers are registered, and UltraNet initialised, on the way; a
    // host list an earlier version kept under the settings folder is moved
    // into the data folder first (AdoptLegacy).
    bool Open(std::string& error);
    // For tests and demos: the list in memory, the secrets for the process,
    // the in-memory provider ("memory") registered; nothing persisted.
    bool OpenInMemory();
    bool IsOpen() const;

    UltraCloud::CloudService& Service();
    std::vector<UltraCloud::Account> List();
    // The account named by its id, or - the id is a slug nobody types - by
    // its display name; NotFound otherwise.
    UltraCloud::Result Find(const std::string& idOrName, UltraCloud::Account& out);
    // Stores a host after the provider accepted the sign-in (`verify`), or
    // as given. The first becomes the default.
    UltraCloud::Result Add(UltraCloud::Account& account, const UltraCloud::Credentials& credentials, bool verify);
    UltraCloud::Result Remove(const std::string& accountId);

    // What DeployFolder uploads through: the account's provider, every
    // failure in the words UltraCloud gives it.
    Uploader UploaderFor(const std::string& accountId);

    // The folder the list and the vault live in: the program's data folder
    // (%APPDATA%\UltraPress, ~/Library/Application Support/UltraPress,
    // $XDG_DATA_HOME/UltraPress else ~/.local/share/UltraPress). Empty
    // without a home.
    static std::string DataDirectory();
    // Where UltraPress 0.5 to 0.7 kept the list: <settings folder>/UltraPress.
    static std::vector<std::string> LegacyDirectories();
    // Moves the first of `candidates` that holds a host list into `dataDir`
    // when that folder holds none yet - the whole folder, vault included.
    // Returns the folder moved from, empty when nothing was; a failure
    // leaves the old folder where it was and says why in `error`.
    static std::string AdoptLegacy(const std::string& dataDir, const std::vector<std::string>& candidates, std::string& error);
    // Whether a provider is a web host a site can be published to: the
    // ones that take a folder of files as they are - "ftp", "webdav",
    // "nextcloud" - and not the share-link clouds or the test fake.
    static bool IsHostProvider(const std::string& providerId);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// One line per host for a list: id, provider, server, user, default mark.
std::string DescribeHost(const UltraCloud::Account& account);

} // namespace UltraPress
