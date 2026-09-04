// UltraCloud/include/UltraCloud/UltraCloudDropbox.h
// The Dropbox provider (id "dropbox"): Dropbox API v2 over UltraNet HTTP —
// files/list_folder, files/upload (single call, ≤ 150 MB), files/download,
// files/create_folder_v2, sharing/create_shared_link_with_settings (password
// and expiry on paid plans). Signs in with OAuth2 + PKCE, offline access.
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudOAuth.h"

namespace UltraCloud {

// Dropbox spells the root as "" and every other path with a leading '/'.
std::string DropboxPath(const std::string& path);

class DropboxProvider : public OAuthProviderBase {
public:
    explicit DropboxProvider(HttpFn http = nullptr, OAuthHooks hooks = {})
        : OAuthProviderBase(std::move(http), std::move(hooks)) {}

    std::string Id() const override { return "dropbox"; }
    std::string DisplayName() const override { return "Dropbox"; }
    ProviderCapabilities Capabilities() const override;
    UltraNetOAuth2Config OAuthConfig(const OAuthApp& app) const override;

    Result Verify(const Account& account, const Credentials& credentials) override;
    Result AccountInfo(const Account& account, const Credentials& credentials,
                       std::string& username, std::string& displayName) override;
    Result List(const Account& account, const Credentials& credentials,
                const std::string& path, std::vector<Entry>& out) override;
    Result MakeDirectory(const Account& account, const Credentials& credentials,
                         const std::string& path) override;
    Result Upload(const Account& account, const Credentials& credentials,
                  const std::string& localPath, const std::string& remotePath) override;
    Result Download(const Account& account, const Credentials& credentials,
                    const std::string& remotePath, const std::string& localPath) override;
    Result CreateShareLink(const Account& account, const Credentials& credentials,
                           const std::string& remotePath,
                           const ShareLinkOptions& options, ShareLink& out) override;

private:
    // POST an RPC-style call (JSON in, JSON out) to api.dropboxapi.com/2/<endpoint>.
    Result Rpc(const Credentials& credentials, const std::string& endpoint,
               const std::string& jsonBody, UltraNetResponse& response, const std::string& what);
};

} // namespace UltraCloud
