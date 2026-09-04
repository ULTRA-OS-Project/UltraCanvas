// UltraCloud/include/UltraCloud/UltraCloudWebDav.h
// The WebDAV provider (id "webdav"): any DAV server over UltraNet HTTP —
// PROPFIND to list, PUT to upload, GET to download, MKCOL for folders. Share
// links exist only when the account has a public base URL that mirrors the
// DAV root (a plain web folder), in which case the link is base + path.
// Nextcloud derives from this and adds real share links.
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudHttp.h"

#include <string>
#include <vector>

namespace UltraCloud {

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

class WebDavProvider : public HttpProviderBase {
public:
    explicit WebDavProvider(HttpFn http = nullptr) : HttpProviderBase(std::move(http)) {}

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
};

} // namespace UltraCloud
