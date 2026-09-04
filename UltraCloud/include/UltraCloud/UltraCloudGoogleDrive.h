// UltraCloud/include/UltraCloud/UltraCloudGoogleDrive.h
// The Google Drive provider (id "googledrive"): Drive API v3 over UltraNet
// HTTP. Drive addresses files by id, so every path is resolved segment by
// segment (a "name in parent" query per level). Upload is a multipart create
// (or a media update when the name already exists in the folder); a share
// link is an "anyone with the link" reader permission plus the file's
// webViewLink. Signs in with OAuth2 + PKCE, offline access.
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudOAuth.h"

namespace UltraCanvas { class JSONValue; }

namespace UltraCloud {

// The Drive query for "name inside parent, not trashed" (quotes escaped).
std::string GoogleDriveChildQuery(const std::string& parentId, const std::string& name);

class GoogleDriveProvider : public OAuthProviderBase {
public:
    // Files above this size go through a resumable upload (Google recommends
    // it above 5 MB); chunks are multiples of 256 KiB.
    static constexpr int64_t kDefaultSimpleUploadLimit = 5LL * 1024 * 1024;
    static constexpr int64_t kDefaultChunkSize         = 8LL * 1024 * 1024;

    explicit GoogleDriveProvider(HttpFn http = nullptr, OAuthHooks hooks = {})
        : OAuthProviderBase(std::move(http), std::move(hooks)) {}

    // Tune the resumable thresholds (tests use small ones).
    void SetUploadLimits(int64_t simpleUploadLimit, int64_t chunkSize) {
        simpleUploadLimit_ = simpleUploadLimit; chunkSize_ = chunkSize;
    }

    std::string Id() const override { return "googledrive"; }
    std::string DisplayName() const override { return "Google Drive"; }
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

    // The file id of a provider path ("root" for "/"); NotFound when absent.
    Result ResolveId(const Credentials& credentials, const std::string& path, std::string& id);

private:
    // GET a JSON answer from the Drive API.
    Result GetJson(const Credentials& credentials, const std::string& url,
                   UltraCanvas::JSONValue& out, const std::string& what);
    // The id (and mime type) of `name` inside `parentId`, or NotFound.
    Result FindChild(const Credentials& credentials, const std::string& parentId,
                     const std::string& name, std::string& id, std::string& mimeType);
    // Resumable upload: open a session (create or update), then PUT chunks.
    Result UploadResumable(const Credentials& credentials, const std::string& localPath,
                           int64_t total, const std::string& name, const std::string& parentId,
                           const std::string& existingId, const std::string& what);

    int64_t simpleUploadLimit_ = kDefaultSimpleUploadLimit;
    int64_t chunkSize_         = kDefaultChunkSize;
};

} // namespace UltraCloud
