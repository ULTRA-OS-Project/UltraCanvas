// UltraCloud/providers/UltraCloudOneDrive.cpp
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudOneDrive.h>
#include <UltraCloud/UltraCloudWebDav.h>   // NormalizePath / EncodePath

#include "core/UltraCloudInternal.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace UltraCloud::internal;

namespace UltraCloud {

namespace {
const char* kGraph = "https://graph.microsoft.com/v1.0";

Entry EntryFrom(const JSONValue& item, const std::string& folder) {
    Entry e;
    e.name = item["name"].GetString();
    e.path = (folder == "/" ? "" : folder) + "/" + e.name;
    e.isDirectory = item.Contains("folder");
    e.size = item["size"].GetInteger(0);
    e.modified = item["lastModifiedDateTime"].GetString();
    return e;
}
} // namespace

std::string OneDriveItemUrl(const std::string& path, const std::string& suffix) {
    const std::string p = NormalizePath(path);
    std::string url = std::string(kGraph) + "/me/drive/root";
    if (p != "/") url += ":" + EncodePath(p) + ":";
    if (!suffix.empty()) url += "/" + suffix;
    return url;
}

ProviderCapabilities OneDriveProvider::Capabilities() const {
    ProviderCapabilities c;
    c.browse = true; c.upload = true; c.shareLinks = true;
    c.passwordProtectedLinks = true; c.expiringLinks = true;   // personal accounts
    c.needsOAuth = true;
    return c;
}

UltraNetOAuth2Config OneDriveProvider::OAuthConfig(const OAuthApp& app) const {
    UltraNetOAuth2Config cfg;
    cfg.authorizationEndpoint = "https://login.microsoftonline.com/common/oauth2/v2.0/authorize";
    cfg.tokenEndpoint = "https://login.microsoftonline.com/common/oauth2/v2.0/token";
    cfg.clientId = app.clientId;
    cfg.clientSecret = app.clientSecret;
    cfg.redirectUri = app.redirectUri;
    cfg.scopes = {"Files.ReadWrite", "User.Read", "offline_access"};
    cfg.usePkce = true;
    return cfg;
}

Result OneDriveProvider::Verify(const Account&, const Credentials& credentials) {
    UltraNetHttpRequest req;
    req.url = std::string(kGraph) + "/me/drive";
    UltraNetResponse resp;
    UltraNetResult net = Send(credentials, req, resp);
    return FromHttp(net, resp, "sign in to OneDrive");
}

Result OneDriveProvider::AccountInfo(const Account&, const Credentials& credentials,
                                     std::string& username, std::string& displayName) {
    UltraNetHttpRequest req;
    req.url = std::string(kGraph) + "/me";
    UltraNetResponse resp;
    UltraNetResult net = Send(credentials, req, resp);
    Result r = FromHttp(net, resp, "read OneDrive account");
    if (!r) return r;
    JSONValue v = ParseJson(BodyText(resp));
    username = v["userPrincipalName"].GetString(v["mail"].GetString());
    displayName = v["displayName"].GetString();
    return Result::Ok();
}

Result OneDriveProvider::List(const Account&, const Credentials& credentials,
                              const std::string& path, std::vector<Entry>& out) {
    out.clear();
    const std::string folder = NormalizePath(path);
    std::string url = OneDriveItemUrl(folder, "children")
                    + Query({{"$select", "name,size,lastModifiedDateTime,folder,file"}, {"$top", "200"}});
    while (!url.empty()) {
        UltraNetHttpRequest req;
        req.url = url;
        UltraNetResponse resp;
        UltraNetResult net = Send(credentials, req, resp);
        Result r = FromHttp(net, resp, "list " + path);
        if (!r) return r;
        JSONValue v = ParseJson(BodyText(resp));
        const JSONValue& items = v["value"];
        for (std::size_t i = 0; i < items.GetSize(); ++i) out.push_back(EntryFrom(items[i], folder));
        url = v["@odata.nextLink"].GetString();
    }
    std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        return a.name < b.name;
    });
    return Result::Ok();
}

Result OneDriveProvider::MakeDirectory(const Account&, const Credentials& credentials,
                                       const std::string& path) {
    const std::string p = NormalizePath(path);
    if (p == "/") return Result::Ok();
    JSONValue body = JSONValue::MakeObject();
    body.Set("name", Leaf(p));
    body.Set("folder", JSONValue::MakeObject());
    body.Set("@microsoft.graph.conflictBehavior", "fail");
    UltraNetHttpRequest req;
    req.url = OneDriveItemUrl(Parent(p), "children");
    req.method = UltraNetHttpMethod::Post;
    req.headers.Set("Content-Type", "application/json");
    req.body = Bytes(ToJson(body));
    UltraNetResponse resp;
    UltraNetResult net = Send(credentials, req, resp);
    Result r = FromHttp(net, resp, "create folder " + path);
    if (!r && resp.statusCode == 409) return Result::Ok();   // already there
    return r;
}

Result OneDriveProvider::Upload(const Account&, const Credentials& credentials,
                                const std::string& localPath, const std::string& remotePath) {
    std::ifstream is(localPath, std::ios::binary);
    if (!is) return Result::Error(ResultCode::IoError, "cannot read " + localPath);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    const int64_t total = static_cast<int64_t>(data.size());

    if (total <= kSimpleUploadLimit) {
        UltraNetHttpRequest req;
        req.url = OneDriveItemUrl(remotePath, "content");
        req.method = UltraNetHttpMethod::Put;
        req.headers.Set("Content-Type", "application/octet-stream");
        req.body = std::move(data);
        UltraNetResponse resp;
        UltraNetResult net = Send(credentials, req, resp);
        return FromHttp(net, resp, "upload " + remotePath);
    }

    // Large file: an upload session, then the bytes in chunks against the
    // pre-authorised session URL (no bearer token on those requests).
    JSONValue item = JSONValue::MakeObject();
    item.Set("@microsoft.graph.conflictBehavior", "replace");
    JSONValue body = JSONValue::MakeObject();
    body.Set("item", item);
    UltraNetHttpRequest open;
    open.url = OneDriveItemUrl(remotePath, "createUploadSession");
    open.method = UltraNetHttpMethod::Post;
    open.headers.Set("Content-Type", "application/json");
    open.body = Bytes(ToJson(body));
    UltraNetResponse openResp;
    UltraNetResult net = Send(credentials, open, openResp);
    Result r = FromHttp(net, openResp, "upload " + remotePath);
    if (!r) return r;
    const std::string uploadUrl = ParseJson(BodyText(openResp))["uploadUrl"].GetString();
    if (uploadUrl.empty())
        return Result::Error(ResultCode::Server, "upload " + remotePath + ": no upload session");

    for (int64_t offset = 0; offset < total; offset += kChunkSize) {
        const int64_t end = std::min(offset + kChunkSize, total);
        UltraNetHttpRequest chunk;
        chunk.url = uploadUrl;
        chunk.method = UltraNetHttpMethod::Put;
        chunk.headers.Set("Content-Range", "bytes " + std::to_string(offset) + "-"
                                           + std::to_string(end - 1) + "/" + std::to_string(total));
        chunk.headers.Set("Content-Type", "application/octet-stream");
        chunk.body.assign(data.begin() + offset, data.begin() + end);
        chunk.options.timeoutMs = 600000;
        UltraNetResponse chunkResp;
        UltraNetResult cnet = http_(chunk, chunkResp);   // session URL carries its own auth
        Result cr = FromHttp(cnet, chunkResp, "upload " + remotePath);
        if (!cr) return cr;
    }
    return Result::Ok();
}

Result OneDriveProvider::Download(const Account&, const Credentials& credentials,
                                  const std::string& remotePath, const std::string& localPath) {
    UltraNetHttpRequest req;
    req.url = OneDriveItemUrl(remotePath, "content");   // 302 to the bytes; followed
    UltraNetResponse resp;
    UltraNetResult net = Send(credentials, req, resp);
    Result r = FromHttp(net, resp, "download " + remotePath);
    if (!r) return r;
    std::ofstream os(localPath, std::ios::binary | std::ios::trunc);
    if (!os) return Result::Error(ResultCode::IoError, "cannot write " + localPath);
    os.write(reinterpret_cast<const char*>(resp.body.data()),
             static_cast<std::streamsize>(resp.body.size()));
    return os ? Result::Ok() : Result::Error(ResultCode::IoError, "cannot write " + localPath);
}

Result OneDriveProvider::CreateShareLink(const Account&, const Credentials& credentials,
                                         const std::string& remotePath,
                                         const ShareLinkOptions& options, ShareLink& out) {
    JSONValue body = JSONValue::MakeObject();
    body.Set("type", options.readOnly ? "view" : "edit");
    body.Set("scope", "anonymous");
    if (!options.password.empty()) body.Set("password", options.password);
    if (options.expiresAt > 0)     body.Set("expirationDateTime", Rfc3339(options.expiresAt));
    UltraNetHttpRequest req;
    req.url = OneDriveItemUrl(remotePath, "createLink");
    req.method = UltraNetHttpMethod::Post;
    req.headers.Set("Content-Type", "application/json");
    req.body = Bytes(ToJson(body));
    UltraNetResponse resp;
    UltraNetResult net = Send(credentials, req, resp);
    Result r = FromHttp(net, resp, "share " + remotePath);
    if (!r) return r;
    JSONValue v = ParseJson(BodyText(resp));
    const std::string url = v["link"]["webUrl"].GetString();
    if (url.empty())
        return Result::Error(ResultCode::Server, "share " + remotePath + ": no link in the answer");
    out = ShareLink{};
    out.url = url;
    out.id = v["id"].GetString();
    out.expiresAt = options.expiresAt;
    return Result::Ok();
}

} // namespace UltraCloud
