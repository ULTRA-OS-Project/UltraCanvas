// UltraCloud/include/UltraCloud/UltraCloudOneDrive.h
// The OneDrive provider (id "onedrive"): Microsoft Graph over UltraNet HTTP —
// /me/drive/root:/<path>: children, content (simple PUT up to 4 MB, an
// upload session in 10 MiB chunks above that), createLink (anonymous view
// links with optional password and expiry). Signs in with OAuth2 + PKCE.
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudOAuth.h"

namespace UltraCloud {

// Graph URL of a drive item by path: ".../root/<suffix>" for the root,
// ".../root:/<encoded path>:/<suffix>" otherwise.
std::string OneDriveItemUrl(const std::string& path, const std::string& suffix);

class OneDriveProvider : public OAuthProviderBase {
public:
    // Files above this size go through an upload session.
    static constexpr int64_t kSimpleUploadLimit = 4 * 1024 * 1024;
    // Session chunk: a multiple of 320 KiB, as Graph requires.
    static constexpr int64_t kChunkSize = 10 * 1024 * 1024;

    explicit OneDriveProvider(HttpFn http = nullptr, OAuthHooks hooks = {})
        : OAuthProviderBase(std::move(http), std::move(hooks)) {}

    std::string Id() const override { return "onedrive"; }
    std::string DisplayName() const override { return "OneDrive"; }
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
};

} // namespace UltraCloud
