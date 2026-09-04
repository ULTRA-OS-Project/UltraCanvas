// UltraCloud/include/UltraCloud/UltraCloudService.h
// The app-facing facade: accounts + secrets + providers behind one object.
// "Share this file through my default cloud account" is one call.
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudAccounts.h"
#include "UltraCloudProvider.h"
#include "UltraCloudSecrets.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCloud {

class CloudService {
public:
    CloudService(AccountStore& accounts, ISecretStore& secrets)
        : accounts_(accounts), secrets_(secrets) {}

    AccountStore& Accounts() { return accounts_; }
    ISecretStore& Secrets()  { return secrets_; }

    // Store an account and its credentials. Fills accountId when empty; the
    // first account becomes the default. With `verify` the provider is asked
    // to sign in first and nothing is stored if that fails.
    Result AddAccount(Account& account, const Credentials& credentials, bool verify);
    // Sign in through the provider's OAuth2 flow (`openUrl` shows the consent
    // page), fill the account's user name from the provider, then store it
    // like AddAccount. For providers whose Capabilities().needsOAuth is set.
    Result SignInAccount(Account& account,
                         const std::function<void(const std::string& url)>& openUrl);
    Result RemoveAccount(const std::string& accountId);

    std::shared_ptr<ICloudProvider> ProviderFor(const Account& account) const;

    Result List(const std::string& accountId, const std::string& path, std::vector<Entry>& out);
    Result Upload(const std::string& accountId, const std::string& localPath,
                  const std::string& remotePath);
    Result CreateShareLink(const std::string& accountId, const std::string& remotePath,
                           const ShareLinkOptions& options, ShareLink& out);

    // Upload `localPath` into `remoteFolder` (empty = the account's remoteFolder,
    // created if missing) and return a share link for it.
    Result UploadAndShare(const std::string& accountId, const std::string& localPath,
                          const std::string& remoteFolder, const ShareLinkOptions& options,
                          ShareLink& out, std::string* remotePathOut = nullptr);

private:
    // Account + credentials + provider for an id. An expired OAuth token is
    // refreshed here and the new one stored, so callers never see a stale one.
    Result Resolve(const std::string& accountId, Account& account, Credentials& credentials,
                   std::shared_ptr<ICloudProvider>& provider);

    AccountStore& accounts_;
    ISecretStore& secrets_;
};

} // namespace UltraCloud
