// UltraCloud/providers/UltraCloudDropbox.cpp
// Version: 0.3.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudDropbox.h>
#include <UltraCloud/UltraCloudWebDav.h>   // NormalizePath

#include "core/UltraCloudInternal.h"

#include <algorithm>
#include <fstream>
#include <string>

using namespace UltraCloud::internal;

namespace UltraCloud {

namespace {
const char* kApi     = "https://api.dropboxapi.com/2/";
const char* kContent = "https://content.dropboxapi.com/2/";

Entry EntryFrom(const JSONValue& e) {
    Entry out;
    out.name = e["name"].GetString();
    out.path = NormalizePath(e["path_display"].GetString(e["path_lower"].GetString()));
    out.isDirectory = e[".tag"].GetString() == "folder";
    out.size = e["size"].GetInteger(0);
    out.modified = e["server_modified"].GetString();
    return out;
}
} // namespace

std::string DropboxPath(const std::string& path) {
    const std::string p = NormalizePath(path);
    return p == "/" ? "" : p;
}

ProviderCapabilities DropboxProvider::Capabilities() const {
    ProviderCapabilities c;
    c.browse = true; c.upload = true; c.shareLinks = true;
    c.passwordProtectedLinks = true; c.expiringLinks = true;   // paid plans
    c.needsOAuth = true;
    return c;
}

UltraNetOAuth2Config DropboxProvider::OAuthConfig(const OAuthApp& app) const {
    UltraNetOAuth2Config cfg;
    cfg.authorizationEndpoint = "https://www.dropbox.com/oauth2/authorize";
    cfg.tokenEndpoint = "https://api.dropboxapi.com/oauth2/token";
    cfg.clientId = app.clientId;
    cfg.clientSecret = app.clientSecret;
    cfg.redirectUri = app.redirectUri;
    cfg.scopes = {"account_info.read", "files.metadata.read", "files.content.read",
                  "files.content.write", "sharing.read", "sharing.write"};
    cfg.extraAuthParams["token_access_type"] = "offline";
    cfg.usePkce = true;
    return cfg;
}

Result DropboxProvider::Rpc(const Credentials& credentials, const std::string& endpoint,
                            const std::string& jsonBody, UltraNetResponse& response,
                            const std::string& what) {
    UltraNetHttpRequest req;
    req.url = std::string(kApi) + endpoint;
    req.method = UltraNetHttpMethod::Post;
    req.headers.Set("Content-Type", "application/json");
    req.body = Bytes(jsonBody);
    UltraNetResult net = Send(credentials, req, response);
    return FromHttp(net, response, what);
}

Result DropboxProvider::Content(const Credentials& credentials, const std::string& endpoint,
                                const std::string& apiArg, std::vector<uint8_t> body,
                                UltraNetResponse& response, const std::string& what) {
    UltraNetHttpRequest req;
    req.url = std::string(kContent) + endpoint;
    req.method = UltraNetHttpMethod::Post;
    req.headers.Set("Dropbox-API-Arg", apiArg);
    req.headers.Set("Content-Type", "application/octet-stream");
    req.body = std::move(body);
    req.options.timeoutMs = 600000;
    UltraNetResult net = Send(credentials, req, response);
    return FromHttp(net, response, what);
}

Result DropboxProvider::Verify(const Account&, const Credentials& credentials) {
    UltraNetResponse resp;
    return Rpc(credentials, "users/get_current_account", "null", resp, "sign in to Dropbox");
}

Result DropboxProvider::AccountInfo(const Account&, const Credentials& credentials,
                                    std::string& username, std::string& displayName) {
    UltraNetResponse resp;
    Result r = Rpc(credentials, "users/get_current_account", "null", resp, "read Dropbox account");
    if (!r) return r;
    JSONValue v = ParseJson(BodyText(resp));
    username = v["email"].GetString();
    displayName = v["name"]["display_name"].GetString();
    return Result::Ok();
}

Result DropboxProvider::List(const Account&, const Credentials& credentials,
                             const std::string& path, std::vector<Entry>& out) {
    out.clear();
    UltraNetResponse resp;
    JSONValue arg = JSONValue::MakeObject();
    arg.Set("path", DropboxPath(path));
    Result r = Rpc(credentials, "files/list_folder", ToJson(arg), resp, "list " + path);
    if (!r) return r;
    while (true) {
        JSONValue v = ParseJson(BodyText(resp));
        const JSONValue& entries = v["entries"];
        for (std::size_t i = 0; i < entries.GetSize(); ++i) out.push_back(EntryFrom(entries[i]));
        if (!v["has_more"].GetBoolean(false)) break;
        JSONValue cont = JSONValue::MakeObject();
        cont.Set("cursor", v["cursor"].GetString());
        resp = UltraNetResponse{};
        r = Rpc(credentials, "files/list_folder/continue", ToJson(cont), resp, "list " + path);
        if (!r) return r;
    }
    std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        return a.name < b.name;
    });
    return Result::Ok();
}

Result DropboxProvider::MakeDirectory(const Account&, const Credentials& credentials,
                                      const std::string& path) {
    UltraNetResponse resp;
    JSONValue arg = JSONValue::MakeObject();
    arg.Set("path", DropboxPath(path));
    arg.Set("autorename", false);
    Result r = Rpc(credentials, "files/create_folder_v2", ToJson(arg), resp, "create folder " + path);
    // Already there: Dropbox answers 409 with a path/conflict error.
    if (!r && resp.statusCode == 409 && BodyText(resp).find("conflict") != std::string::npos)
        return Result::Ok();
    return r;
}

Result DropboxProvider::Upload(const Account&, const Credentials& credentials,
                               const std::string& localPath, const std::string& remotePath) {
    const int64_t total = FileSize(localPath);
    std::ifstream is(localPath, std::ios::binary);
    if (total < 0 || !is) return Result::Error(ResultCode::IoError, "cannot read " + localPath);
    const std::string what = "upload " + remotePath;

    JSONValue commit = JSONValue::MakeObject();
    commit.Set("path", DropboxPath(remotePath));
    commit.Set("mode", "overwrite");
    commit.Set("mute", true);

    if (total <= simpleUploadLimit_) {
        std::vector<uint8_t> data;
        ReadChunk(is, 0, total, data);
        UltraNetResponse resp;
        return Content(credentials, "files/upload", ToJson(commit), std::move(data), resp, what);
    }

    // Upload session: start carries the first chunk, append_v2 the middle
    // ones, finish the last one together with the commit. Every chunk is
    // read from disk when it is sent, so memory stays at one chunk.
    std::vector<uint8_t> chunk;
    ReadChunk(is, 0, chunkSize_, chunk);
    int64_t offset = static_cast<int64_t>(chunk.size());
    JSONValue startArg = JSONValue::MakeObject();
    startArg.Set("close", false);
    UltraNetResponse startResp;
    Result r = Content(credentials, "files/upload_session/start", ToJson(startArg),
                       std::move(chunk), startResp, what);
    if (!r) return r;
    const std::string sessionId = ParseJson(BodyText(startResp))["session_id"].GetString();
    if (sessionId.empty()) return Result::Error(ResultCode::Server, what + ": no upload session");

    auto cursor = [&sessionId](int64_t at) {
        JSONValue c = JSONValue::MakeObject();
        c.Set("session_id", sessionId);
        c.Set("offset", at);
        return c;
    };
    while (total - offset > chunkSize_) {
        ReadChunk(is, offset, chunkSize_, chunk);
        JSONValue appendArg = JSONValue::MakeObject();
        appendArg.Set("cursor", cursor(offset));
        appendArg.Set("close", false);
        UltraNetResponse appendResp;
        r = Content(credentials, "files/upload_session/append_v2", ToJson(appendArg),
                    std::move(chunk), appendResp, what);
        if (!r) return r;
        offset += chunkSize_;
    }
    ReadChunk(is, offset, total - offset, chunk);
    JSONValue finishArg = JSONValue::MakeObject();
    finishArg.Set("cursor", cursor(offset));
    finishArg.Set("commit", commit);
    UltraNetResponse finishResp;
    return Content(credentials, "files/upload_session/finish", ToJson(finishArg),
                   std::move(chunk), finishResp, what);
}

Result DropboxProvider::Download(const Account&, const Credentials& credentials,
                                 const std::string& remotePath, const std::string& localPath) {
    JSONValue arg = JSONValue::MakeObject();
    arg.Set("path", DropboxPath(remotePath));
    UltraNetHttpRequest req;
    req.url = std::string(kContent) + "files/download";
    req.method = UltraNetHttpMethod::Post;
    req.headers.Set("Dropbox-API-Arg", ToJson(arg));
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

Result DropboxProvider::CreateShareLink(const Account&, const Credentials& credentials,
                                        const std::string& remotePath,
                                        const ShareLinkOptions& options, ShareLink& out) {
    JSONValue settings = JSONValue::MakeObject();
    settings.Set("requested_visibility", options.password.empty() ? "public" : "password");
    settings.Set("audience", "public");
    settings.Set("access", options.readOnly ? "viewer" : "editor");
    if (!options.password.empty()) settings.Set("link_password", options.password);
    if (options.expiresAt > 0)     settings.Set("expires", Rfc3339(options.expiresAt));
    JSONValue arg = JSONValue::MakeObject();
    arg.Set("path", DropboxPath(remotePath));
    arg.Set("settings", settings);

    UltraNetResponse resp;
    Result r = Rpc(credentials, "sharing/create_shared_link_with_settings", ToJson(arg), resp,
                   "share " + remotePath);
    if (!r) {
        // A link already exists for the file: fetch it instead of failing.
        if (resp.statusCode == 409 &&
            BodyText(resp).find("shared_link_already_exists") != std::string::npos) {
            JSONValue q = JSONValue::MakeObject();
            q.Set("path", DropboxPath(remotePath));
            q.Set("direct_only", true);
            UltraNetResponse listResp;
            Result lr = Rpc(credentials, "sharing/list_shared_links", ToJson(q), listResp,
                            "share " + remotePath);
            if (!lr) return lr;
            JSONValue v = ParseJson(BodyText(listResp));
            const JSONValue& links = v["links"];
            if (links.GetSize() == 0)
                return Result::Error(ResultCode::Server, "share " + remotePath + ": no link returned");
            out = ShareLink{};
            out.url = links[0]["url"].GetString();
            out.id = links[0]["id"].GetString();
            return Result::Ok();
        }
        return r;
    }
    JSONValue v = ParseJson(BodyText(resp));
    if (v["url"].GetString().empty())
        return Result::Error(ResultCode::Server, "share " + remotePath + ": no link in the answer");
    out = ShareLink{};
    out.url = v["url"].GetString();
    out.id = v["id"].GetString();
    out.expiresAt = options.expiresAt;
    return Result::Ok();
}

} // namespace UltraCloud
