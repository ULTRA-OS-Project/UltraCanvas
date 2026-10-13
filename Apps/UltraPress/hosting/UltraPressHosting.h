// Apps/UltraPress/hosting/UltraPressHosting.h
// The hosts a site is published to: UltraCloud accounts - FTP, FTPS, SFTP,
// WebDAV, Nextcloud - kept by the ultrapress program under the settings
// folder (hosts.db and the credential vault beside it), the way UltraFiler
// keeps its remote drives. A site names one of them in site.json
// (`hosting`); the engine's deploy (engine/UltraPressDeploy.h) gets an
// Uploader onto it from here. Built only where UltraCloud is; the program
// says so when it is not.
// Version: 0.5.0
// Last Modified: 2026-10-13
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
    // providers are registered, and UltraNet initialised, on the way.
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

    // The folder the list and the vault live in: <settings folder>/UltraPress.
    static std::string ConfigDirectory();
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
