// UltraCloud/core/UltraCloudService.cpp
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudService.h>
#include <UltraCloud/UltraCloudWebDav.h>   // NormalizePath

#include <filesystem>
#include <string>

namespace UltraCloud {

std::shared_ptr<ICloudProvider> CloudService::ProviderFor(const Account& account) const {
    return GetProvider(account.providerId);
}

Result CloudService::Resolve(const std::string& accountId, Account& account,
                             Credentials& credentials,
                             std::shared_ptr<ICloudProvider>& provider) const {
    Result got = accounts_.Get(accountId, account);
    if (!got) return got;
    provider = ProviderFor(account);
    if (!provider)
        return Result::Error(ResultCode::Unsupported,
                             "no provider registered for '" + account.providerId + "'");
    secrets_.Retrieve(accountId, credentials);
    if (credentials.username.empty()) credentials.username = account.username;
    return Result::Ok();
}

Result CloudService::AddAccount(Account& account, const Credentials& credentials, bool verify) {
    if (account.providerId.empty())
        return Result::Error(ResultCode::InvalidArgument, "account needs a provider");
    auto provider = GetProvider(account.providerId);
    if (!provider)
        return Result::Error(ResultCode::Unsupported,
                             "no provider registered for '" + account.providerId + "'");
    if (account.accountId.empty())
        account.accountId = MakeAccountId(account.providerId, account.username, account.serverUrl);
    if (account.displayName.empty())
        account.displayName = provider->DisplayName() + " (" + account.username + ")";
    if (account.remoteFolder.empty()) account.remoteFolder = "/Shared from ULTRA OS";

    Credentials creds = credentials;
    if (creds.username.empty()) creds.username = account.username;
    if (verify) {
        Result v = provider->Verify(account, creds);
        if (!v) return v;
    }
    if (!secrets_.Store(account.accountId, creds))
        return Result::Error(ResultCode::IoError, "could not store the account's credentials");
    Result stored = accounts_.Upsert(account);
    if (!stored) { secrets_.Remove(account.accountId); return stored; }
    // Reflect the default flag the store decided on.
    Account fresh;
    if (accounts_.Get(account.accountId, fresh)) account.isDefault = fresh.isDefault;
    return Result::Ok();
}

Result CloudService::RemoveAccount(const std::string& accountId) {
    Result r = accounts_.Remove(accountId);
    secrets_.Remove(accountId);
    return r;
}

Result CloudService::List(const std::string& accountId, const std::string& path,
                          std::vector<Entry>& out) {
    Account a; Credentials c; std::shared_ptr<ICloudProvider> p;
    Result r = Resolve(accountId, a, c, p);
    if (!r) return r;
    return p->List(a, c, NormalizePath(path), out);
}

Result CloudService::Upload(const std::string& accountId, const std::string& localPath,
                            const std::string& remotePath) {
    Account a; Credentials c; std::shared_ptr<ICloudProvider> p;
    Result r = Resolve(accountId, a, c, p);
    if (!r) return r;
    return p->Upload(a, c, localPath, NormalizePath(remotePath));
}

Result CloudService::CreateShareLink(const std::string& accountId, const std::string& remotePath,
                                     const ShareLinkOptions& options, ShareLink& out) {
    Account a; Credentials c; std::shared_ptr<ICloudProvider> p;
    Result r = Resolve(accountId, a, c, p);
    if (!r) return r;
    return p->CreateShareLink(a, c, NormalizePath(remotePath), options, out);
}

Result CloudService::UploadAndShare(const std::string& accountId, const std::string& localPath,
                                    const std::string& remoteFolder,
                                    const ShareLinkOptions& options, ShareLink& out,
                                    std::string* remotePathOut) {
    Account a; Credentials c; std::shared_ptr<ICloudProvider> p;
    Result r = Resolve(accountId, a, c, p);
    if (!r) return r;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(localPath, ec))
        return Result::Error(ResultCode::IoError, "not a file: " + localPath);

    const std::string folder = NormalizePath(remoteFolder.empty() ? a.remoteFolder : remoteFolder);
    if (folder != "/") {
        // Create the folder if it is missing; "already there" is not an error.
        Result mk = p->MakeDirectory(a, c, folder);
        if (!mk && mk.httpStatus != 405 && mk.httpStatus != 409 && mk.code != ResultCode::Unsupported)
            return mk;
    }
    const std::string name = std::filesystem::path(localPath).filename().string();
    const std::string remotePath = (folder == "/" ? "" : folder) + "/" + name;

    Result up = p->Upload(a, c, localPath, remotePath);
    if (!up) return up;
    if (remotePathOut) *remotePathOut = remotePath;
    return p->CreateShareLink(a, c, remotePath, options, out);
}

} // namespace UltraCloud
