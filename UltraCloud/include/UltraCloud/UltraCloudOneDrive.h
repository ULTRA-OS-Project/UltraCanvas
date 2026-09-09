// UltraCloud/include/UltraCloud/UltraCloudOneDrive.h
// The OneDrive provider (id "onedrive"): Microsoft Graph over UltraNet HTTP —
// /me/drive/root:/<path>: children, content (simple PUT up to 4 MB, an
// upload session in 10 MiB chunks above that), createLink (anonymous view
// links with optional password and expiry). Signs in with OAuth2 + PKCE.
// Version: 0.3.0
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
    // Files above this size go through an upload session (Graph caps a
    // simple PUT at 4 MB); session chunks are multiples of 320 KiB.
    static constexpr int64_t kDefaultSimpleUploadLimit = 4LL * 1024 * 1024;
    static constexpr int64_t kDefaultChunkSize         = 10LL * 1024 * 1024;

    explicit OneDriveProvider(HttpFn http = nullptr, OAuthHooks hooks = {})
        : OAuthProviderBase(std::move(http), std::move(hooks)) {}

    // Tune the session thresholds (tests use small ones).
    void SetUploadLimits(int64_t simpleUploadLimit, int64_t chunkSize) {
        simpleUploadLimit_ = simpleUploadLimit; chunkSize_ = chunkSize;
    }

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

private:
    int64_t simpleUploadLimit_ = kDefaultSimpleUploadLimit;
    int64_t chunkSize_         = kDefaultChunkSize;
};

} // namespace UltraCloud
