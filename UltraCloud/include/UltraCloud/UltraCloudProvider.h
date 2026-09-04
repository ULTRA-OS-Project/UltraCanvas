// UltraCloud/include/UltraCloud/UltraCloudProvider.h
// The provider interface every cloud storage backend implements, and the
// process-wide provider registry. Providers are stateless: every call gets
// the account and the resolved credentials.
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudTypes.h"

#include <functional>
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

    // ---- Sign-in (OAuth2 providers) ----------------------------------------
    // Interactive sign-in: `openUrl` shows the consent page (the system
    // browser); the tokens land in `out`. Password providers return
    // Unsupported — they sign in with what the user typed.
    virtual Result SignIn(const Account& account,
                          const std::function<void(const std::string& url)>& openUrl,
                          Credentials& out) {
        (void)account; (void)openUrl; (void)out;
        return Result::Error(ResultCode::Unsupported, "this provider signs in with a password");
    }
    // Renew an expired access token from the refresh token. Unsupported when
    // the provider has nothing to refresh.
    virtual Result RefreshCredentials(const Account& account, Credentials& credentials) {
        (void)account; (void)credentials;
        return Result::Error(ResultCode::Unsupported, "nothing to refresh");
    }
    // Who the credentials belong to (user name / display name), for filling
    // an account after an OAuth sign-in. Unsupported when unknown.
    virtual Result AccountInfo(const Account& account, const Credentials& credentials,
                               std::string& username, std::string& displayName) {
        (void)account; (void)credentials; (void)username; (void)displayName;
        return Result::Error(ResultCode::Unsupported, "no account info");
    }
};

// ---- Registry ---------------------------------------------------------------
// Register a provider (replaces one with the same id). The built-in set is
// registered by RegisterBuiltInProviders(); apps call that once at start-up.
void RegisterProvider(std::shared_ptr<ICloudProvider> provider);
std::shared_ptr<ICloudProvider> GetProvider(const std::string& providerId);
std::vector<std::shared_ptr<ICloudProvider>> ListProviders();
void RegisterBuiltInProviders();

// ---- Provider plug-ins (DSOs) ------------------------------------------------
// A cloud storage can also ship as a separate shared library, the way UltraNet
// protocol plug-ins do. The library exports one C entry point:
//
//   extern "C" void UltraCloud_PluginInit(const UltraCloudPluginHost* host);
//
// and calls host->registerProvider(...) for each provider it provides.
// LoadProviderPlugins() dlopens every library in the plug-in directory
// (ULTRACLOUD_PLUGIN_DIR, else <executable dir>/plugins/ultracloud) and
// returns how many libraries registered at least one provider.
struct UltraCloudPluginHost {
    int hostVersion = 1;
    void (*registerProvider)(std::shared_ptr<ICloudProvider> provider) = nullptr;
};
std::string GetPluginDirectory();
void SetPluginDirectory(const std::string& path);
int  LoadProviderPlugins();

} // namespace UltraCloud
