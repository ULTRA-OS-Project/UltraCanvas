// UltraCloud/include/UltraCloud/UltraCloudDropbox.h
// The Dropbox provider (id "dropbox"): Dropbox API v2 over UltraNet HTTP —
// files/list_folder, files/upload (single call up to the simple-upload
// limit) or an upload session in chunks streamed from disk above it
// (upload_session/start → append_v2 → finish), files/download,
// files/create_folder_v2, sharing/create_shared_link_with_settings (password
// and expiry on paid plans). Signs in with OAuth2 + PKCE, offline access.
// Version: 0.3.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudOAuth.h"

namespace UltraCloud {

// Dropbox spells the root as "" and every other path with a leading '/'.
std::string DropboxPath(const std::string& path);

class DropboxProvider : public OAuthProviderBase {
public:
    // Files above this size go through an upload session (Dropbox caps a
    // single files/upload at 150 MB); chunks are multiples of 4 MiB.
    static constexpr int64_t kDefaultSimpleUploadLimit = 150LL * 1024 * 1024;
    static constexpr int64_t kDefaultChunkSize         = 8LL * 1024 * 1024;

    explicit DropboxProvider(HttpFn http = nullptr, OAuthHooks hooks = {})
        : OAuthProviderBase(std::move(http), std::move(hooks)) {}

    // Tune the session thresholds (tests use small ones).
    void SetUploadLimits(int64_t simpleUploadLimit, int64_t chunkSize) {
        simpleUploadLimit_ = simpleUploadLimit; chunkSize_ = chunkSize;
    }

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
    // POST bytes to content.dropboxapi.com/2/<endpoint> with a Dropbox-API-Arg.
    Result Content(const Credentials& credentials, const std::string& endpoint,
                   const std::string& apiArg, std::vector<uint8_t> body,
                   UltraNetResponse& response, const std::string& what);

    int64_t simpleUploadLimit_ = kDefaultSimpleUploadLimit;
    int64_t chunkSize_         = kDefaultChunkSize;
};

} // namespace UltraCloud
