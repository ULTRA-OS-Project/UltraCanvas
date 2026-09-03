// UltraCloud/include/UltraCloud/UltraCloudWebDav.h
// The WebDAV provider (id "webdav"): any DAV server over UltraNet HTTP —
// PROPFIND to list, PUT to upload, GET to download, MKCOL for folders. Share
// links exist only when the account has a public base URL that mirrors the
// DAV root (a plain web folder), in which case the link is base + path.
// Nextcloud derives from this and adds real share links.
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudProvider.h"

#include <UltraNet/UltraNetHttp.h>

#include <functional>
#include <string>
#include <vector>

namespace UltraCloud {

// The HTTP seam: UltraNet_HttpRequest by default, a fake in tests.
using HttpFn = std::function<UltraNetResult(const UltraNetHttpRequest&, UltraNetResponse&)>;

// ---- Pure helpers (tested on their own) --------------------------------------
// Percent-encode a provider path segment by segment, keeping the '/'s.
std::string EncodePath(const std::string& path);
// base + path with exactly one '/' between them.
std::string JoinUrl(const std::string& base, const std::string& path);
// Normalise "a/b" / "/a/b/" / "" to "/a/b" ("/" for the root).
std::string NormalizePath(const std::string& path);
// Parse a PROPFIND multistatus body. `requestHref` is the href of the listed
// folder itself (skipped). Tolerant of namespace prefixes (d:, D:, none).
std::vector<Entry> ParseMultistatus(const std::string& xml, const std::string& folderPath);
// The link a plain web folder gives: publicBaseUrl + encoded path.
std::string PublicFolderLink(const std::string& publicBaseUrl, const std::string& path);

class WebDavProvider : public ICloudProvider {
public:
    explicit WebDavProvider(HttpFn http = nullptr);

    std::string Id() const override { return "webdav"; }
    std::string DisplayName() const override { return "WebDAV server"; }
    ProviderCapabilities Capabilities() const override;

    Result Verify(const Account& account, const Credentials& credentials) override;
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

    // The DAV URL of a provider path. Plain WebDAV: serverUrl + path.
    virtual std::string DavUrl(const Account& account, const std::string& path) const;

protected:
    // One request with Basic (password) or Bearer (token) auth applied.
    UltraNetResult Send(const Credentials& credentials, UltraNetHttpRequest request,
                        UltraNetResponse& response) const;
    // Map an HTTP outcome onto a Result (2xx → Ok).
    static Result FromHttp(const UltraNetResult& net, const UltraNetResponse& response,
                           const std::string& what);

    HttpFn http_;
};

} // namespace UltraCloud
