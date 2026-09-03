// UltraCloud/providers/UltraCloudNextcloud.cpp
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudNextcloud.h>

#include <DataFormats/UltraCanvasJSON.h>
#include <UltraNet/UltraNetUrl.h>

#include <ctime>
#include <string>

namespace UltraCloud {

std::string NextcloudDavUrl(const std::string& serverUrl, const std::string& username,
                            const std::string& path) {
    return JoinUrl(serverUrl, "/remote.php/dav/files/" + UltraNet_UrlEncode(username)
                                  + EncodePath(NormalizePath(path)));
}

std::string NextcloudShareApiUrl(const std::string& serverUrl) {
    return JoinUrl(serverUrl, "/ocs/v2.php/apps/files_sharing/api/v1/shares?format=json");
}

std::string FormatExpireDate(int64_t epoch) {
    if (epoch <= 0) return "";
    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

std::string BuildOcsShareForm(const std::string& path, const ShareLinkOptions& options) {
    // shareType 3 = public link; permissions 1 = read, 15 = read+write+create+delete.
    std::string form = "path=" + UltraNet_UrlEncode(NormalizePath(path)) + "&shareType=3"
                     + "&permissions=" + (options.readOnly ? "1" : "15");
    if (!options.password.empty()) form += "&password=" + UltraNet_UrlEncode(options.password);
    if (options.expiresAt > 0)     form += "&expireDate=" + FormatExpireDate(options.expiresAt);
    if (!options.label.empty())    form += "&label=" + UltraNet_UrlEncode(options.label);
    return form;
}

bool ParseOcsShareResponse(const std::string& json, ShareLink& out, std::string& error) {
    using namespace UltraCanvas;
    JSONParseResult pr;
    JSONValue root = JSON::Parse(json, &pr);
    if (!root.IsObject() || !root.Contains("ocs")) { error = "not an OCS answer"; return false; }
    const JSONValue& ocs = root["ocs"];
    const JSONValue& meta = ocs["meta"];
    const int64_t status = meta.IsObject() ? meta["statuscode"].GetInteger(0) : 0;
    if (status != 100 && status != 200) {
        error = meta.IsObject() ? meta["message"].GetString("OCS error") : "OCS error";
        if (status) error += " (OCS " + std::to_string(status) + ")";
        return false;
    }
    const JSONValue& data = ocs["data"];
    if (!data.IsObject() || data["url"].GetString().empty()) { error = "no link in the answer"; return false; }
    out = ShareLink{};
    out.url = data["url"].GetString();
    if (data.Contains("id")) {
        const JSONValue& id = data["id"];
        out.id = id.IsString() ? id.GetString() : std::to_string(id.GetInteger(0));
    }
    return true;
}

ProviderCapabilities NextcloudProvider::Capabilities() const {
    ProviderCapabilities c;
    c.browse = true; c.upload = true; c.shareLinks = true;
    c.passwordProtectedLinks = true; c.expiringLinks = true;
    c.needsServerUrl = true;
    return c;
}

std::string NextcloudProvider::DavUrl(const Account& account, const std::string& path) const {
    return NextcloudDavUrl(account.serverUrl, account.username, path);
}

Result NextcloudProvider::CreateShareLink(const Account& account, const Credentials& credentials,
                                          const std::string& remotePath,
                                          const ShareLinkOptions& options, ShareLink& out) {
    UltraNetHttpRequest req;
    req.url = NextcloudShareApiUrl(account.serverUrl);
    req.method = UltraNetHttpMethod::Post;
    req.headers.Set("OCS-APIRequest", "true");
    req.headers.Set("Accept", "application/json");
    req.headers.Set("Content-Type", "application/x-www-form-urlencoded");
    const std::string form = BuildOcsShareForm(remotePath, options);
    req.body.assign(form.begin(), form.end());

    UltraNetResponse resp;
    UltraNetResult net = Send(credentials, req, resp);
    Result r = FromHttp(net, resp, "share " + remotePath);
    if (!r) return r;

    std::string error;
    if (!ParseOcsShareResponse(std::string(resp.body.begin(), resp.body.end()), out, error))
        return Result::Error(ResultCode::Server, "share " + remotePath + ": " + error, resp.statusCode);
    out.expiresAt = options.expiresAt;
    return Result::Ok();
}

} // namespace UltraCloud
