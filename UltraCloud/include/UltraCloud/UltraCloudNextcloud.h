// UltraCloud/include/UltraCloud/UltraCloudNextcloud.h
// The Nextcloud / ownCloud provider (id "nextcloud"): WebDAV under
// /remote.php/dav/files/<user>/ for files, the OCS Share API for public
// links (password, expiry, label supported). Signs in with the account
// password or, better, an app password.
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudWebDav.h"

namespace UltraCloud {

// ---- Pure helpers (tested on their own) --------------------------------------
// https://cloud.example.com/remote.php/dav/files/<user>/<encoded path>
std::string NextcloudDavUrl(const std::string& serverUrl, const std::string& username,
                            const std::string& path);
// The OCS shares endpoint (JSON answer requested).
std::string NextcloudShareApiUrl(const std::string& serverUrl);
// The x-www-form-urlencoded body of a "create public link" request.
std::string BuildOcsShareForm(const std::string& path, const ShareLinkOptions& options);
// Read the link out of an OCS JSON answer; false with `error` set otherwise.
bool ParseOcsShareResponse(const std::string& json, ShareLink& out, std::string& error);
// "YYYY-MM-DD" (UTC) for an epoch second.
std::string FormatExpireDate(int64_t epoch);

class NextcloudProvider : public WebDavProvider {
public:
    explicit NextcloudProvider(HttpFn http = nullptr) : WebDavProvider(std::move(http)) {}

    std::string Id() const override { return "nextcloud"; }
    std::string DisplayName() const override { return "Nextcloud / ownCloud"; }
    ProviderCapabilities Capabilities() const override;

    Result CreateShareLink(const Account& account, const Credentials& credentials,
                           const std::string& remotePath,
                           const ShareLinkOptions& options, ShareLink& out) override;

    std::string DavUrl(const Account& account, const std::string& path) const override;
};

} // namespace UltraCloud
