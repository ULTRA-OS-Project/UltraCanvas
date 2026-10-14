// UltraCloud/include/UltraCloud/UltraCloudService.h
// The app-facing facade: accounts + secrets + providers behind one object.
// "Share this file through my default cloud account" is one call.
// Version: 0.4.0 - an access token the service rejects is renewed and the
//                  call made once more; a cancelled sign-in stores nothing
// Last Modified: 2026-10-14
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
    // to sign in first and nothing is stored if that fails - nor when the
    // calling thread's sign-in has been called off meanwhile
    // (SetThreadSignInCancel): that ends as ResultCode::Cancelled.
    Result AddAccount(Account& account, const Credentials& credentials, bool verify);
    // Sign in through the provider's OAuth2 flow (`openUrl` shows the consent
    // page), fill the account's user name from the provider, then store it
    // like AddAccount. For providers whose Capabilities().needsOAuth is set.
    Result SignInAccount(Account& account,
                         const std::function<void(const std::string& url)>& openUrl);
    Result RemoveAccount(const std::string& accountId);

    std::shared_ptr<ICloudProvider> ProviderFor(const Account& account) const;

    // Every call below runs with the account's credentials resolved
    // (Resolve). One whose access token the service rejects (HTTP 401) while
    // it had not expired here - revoked early, a clock that is off, or expired
    // during a long wait for a service that limits requests - is not failed
    // at once: the token is renewed from the refresh token, stored, and the
    // call made once more. Said in the thread's log (UltraCloudLog.h).
    Result List(const std::string& accountId, const std::string& path, std::vector<Entry>& out);
    // ---- Changing what is there ---------------------------------------------
    // Forwarded to the provider, which answers Unsupported unless it can do
    // it (ProviderCapabilities::modify). `isDirectory` is what the caller
    // already knows from the Entry; `newName` is a bare name, so Rename never
    // moves an entry to another folder.
    Result Delete(const std::string& accountId, const std::string& path,
                  bool isDirectory);
    Result Rename(const std::string& accountId, const std::string& path,
                  const std::string& newName);
    Result MakeDirectory(const std::string& accountId, const std::string& path);

    Result Upload(const std::string& accountId, const std::string& localPath,
                  const std::string& remotePath);
    // The other direction. `localPath` is a full local path including the
    // name to save under, not a folder: the caller knows what it wants the
    // file called, and a provider that had to invent the name would have to
    // guess at a collision it cannot see. An existing file there is
    // overwritten, so a caller that must not lose one picks a free name
    // before calling.
    Result Download(const std::string& accountId, const std::string& remotePath,
                    const std::string& localPath);
    Result CreateShareLink(const std::string& accountId, const std::string& remotePath,
                           const ShareLinkOptions& options, ShareLink& out);

    // Upload `localPath` into `remoteFolder` (empty = the account's remoteFolder,
    // created if missing) and return a share link for it.
    Result UploadAndShare(const std::string& accountId, const std::string& localPath,
                          const std::string& remoteFolder, const ShareLinkOptions& options,
                          ShareLink& out, std::string* remotePathOut = nullptr);

private:
    // Account + credentials + provider for an id. An expired OAuth token is
    // refreshed here and the new one stored, so callers never see a stale one;
    // `renewed`, when given, says whether that happened.
    Result Resolve(const std::string& accountId, Account& account, Credentials& credentials,
                   std::shared_ptr<ICloudProvider>& provider, bool* renewed = nullptr);
    // Resolve, then `call` - and once more with a renewed token when the
    // service rejected the one it had (see above).
    using AccountCall =
            std::function<Result(const Account&, const Credentials&, ICloudProvider&)>;
    Result Run(const std::string& accountId, const AccountCall& call);
    // UploadAndShare's three steps with the account resolved.
    Result UploadAndShareWith(const Account& account, const Credentials& credentials,
                              ICloudProvider& provider, const std::string& localPath,
                              const std::string& remoteFolder, const ShareLinkOptions& options,
                              ShareLink& out, std::string* remotePathOut);
    // Renews the access token and stores it, saying so in the log.
    Result RenewToken(const std::string& accountId, const Account& account,
                      Credentials& credentials, ICloudProvider& provider,
                      const std::string& why);

    AccountStore& accounts_;
    ISecretStore& secrets_;
};

} // namespace UltraCloud
