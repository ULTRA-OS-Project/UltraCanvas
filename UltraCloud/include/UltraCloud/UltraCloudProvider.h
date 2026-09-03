// UltraCloud/include/UltraCloud/UltraCloudProvider.h
// The provider interface every cloud storage backend implements, and the
// process-wide provider registry. Providers are stateless: every call gets
// the account and the resolved credentials.
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudTypes.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCloud {

class ICloudProvider {
public:
    virtual ~ICloudProvider() = default;

    virtual std::string Id() const = 0;            // "nextcloud", "webdav", ...
    virtual std::string DisplayName() const = 0;   // "Nextcloud / ownCloud"
    virtual ProviderCapabilities Capabilities() const = 0;

    // Check that the account + credentials sign in (a cheap request).
    virtual Result Verify(const Account& account, const Credentials& credentials) = 0;

    // Children of `path` ("/" for the root). Folders first is up to the caller.
    virtual Result List(const Account& account, const Credentials& credentials,
                        const std::string& path, std::vector<Entry>& out) = 0;

    virtual Result MakeDirectory(const Account& account, const Credentials& credentials,
                                 const std::string& path) = 0;

    virtual Result Upload(const Account& account, const Credentials& credentials,
                          const std::string& localPath, const std::string& remotePath) = 0;

    virtual Result Download(const Account& account, const Credentials& credentials,
                            const std::string& remotePath, const std::string& localPath) = 0;

    // A link anyone can open. Returns Unsupported when the provider (or this
    // account's configuration) cannot mint one.
    virtual Result CreateShareLink(const Account& account, const Credentials& credentials,
                                   const std::string& remotePath,
                                   const ShareLinkOptions& options, ShareLink& out) = 0;
};

// ---- Registry ---------------------------------------------------------------
// Register a provider (replaces one with the same id). The built-in set is
// registered by RegisterBuiltInProviders(); apps call that once at start-up.
void RegisterProvider(std::shared_ptr<ICloudProvider> provider);
std::shared_ptr<ICloudProvider> GetProvider(const std::string& providerId);
std::vector<std::shared_ptr<ICloudProvider>> ListProviders();
void RegisterBuiltInProviders();

} // namespace UltraCloud
