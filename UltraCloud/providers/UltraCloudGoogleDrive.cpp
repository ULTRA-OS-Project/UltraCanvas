// UltraCloud/providers/UltraCloudGoogleDrive.cpp
// Version: 0.3.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudGoogleDrive.h>
#include <UltraCloud/UltraCloudWebDav.h>   // NormalizePath

#include "core/UltraCloudInternal.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

using namespace UltraCloud::internal;

namespace UltraCloud {

namespace {
const char* kApi    = "https://www.googleapis.com/drive/v3";
const char* kUpload = "https://www.googleapis.com/upload/drive/v3";
const char* kFolderMime = "application/vnd.google-apps.folder";

std::vector<std::string> Segments(const std::string& path) {
    std::vector<std::string> out;
    std::stringstream ss(NormalizePath(path));
    std::string seg;
    while (std::getline(ss, seg, '/')) if (!seg.empty()) out.push_back(seg);
    return out;
}

std::string EscapeQuery(const std::string& s) {
    std::string out;
    for (char c : s) { if (c == '\\' || c == '\'') out.push_back('\\'); out.push_back(c); }
    return out;
}
} // namespace

std::string GoogleDriveChildQuery(const std::string& parentId, const std::string& name) {
    return "'" + EscapeQuery(parentId) + "' in parents and name = '" + EscapeQuery(name)
         + "' and trashed = false";
}

ProviderCapabilities GoogleDriveProvider::Capabilities() const {
    ProviderCapabilities c;
    c.browse = true; c.upload = true; c.shareLinks = true;
    c.passwordProtectedLinks = false; c.expiringLinks = false;   // not for "anyone" links
    c.needsOAuth = true;
    return c;
}

UltraNetOAuth2Config GoogleDriveProvider::OAuthConfig(const OAuthApp& app) const {
    UltraNetOAuth2Config cfg;
    cfg.authorizationEndpoint = "https://accounts.google.com/o/oauth2/v2/auth";
    cfg.tokenEndpoint = "https://oauth2.googleapis.com/token";
    cfg.clientId = app.clientId;
    cfg.clientSecret = app.clientSecret;
    cfg.redirectUri = app.redirectUri;
    cfg.scopes = {"https://www.googleapis.com/auth/drive"};
    cfg.extraAuthParams["access_type"] = "offline";
    cfg.extraAuthParams["prompt"] = "consent";
    cfg.usePkce = true;
    return cfg;
}

Result GoogleDriveProvider::GetJson(const Credentials& credentials, const std::string& url,
                                    JSONValue& out, const std::string& what) {
    UltraNetHttpRequest req;
    req.url = url;
    UltraNetResponse resp;
    UltraNetResult net = Send(credentials, req, resp);
    Result r = FromHttp(net, resp, what);
    if (!r) return r;
    out = ParseJson(BodyText(resp));
    return Result::Ok();
}

Result GoogleDriveProvider::FindChild(const Credentials& credentials, const std::string& parentId,
                                      const std::string& name, std::string& id, std::string& mimeType) {
    JSONValue v;
    Result r = GetJson(credentials,
        std::string(kApi) + "/files" + Query({{"q", GoogleDriveChildQuery(parentId, name)},
                                             {"fields", "files(id,mimeType)"}, {"pageSize", "1"}}),
        v, "look up " + name);
    if (!r) return r;
    const JSONValue& files = v["files"];
    if (files.GetSize() == 0) return Result::Error(ResultCode::NotFound, "no such entry: " + name);
    id = files[0]["id"].GetString();
    mimeType = files[0]["mimeType"].GetString();
    return Result::Ok();
}

Result GoogleDriveProvider::ResolveId(const Credentials& credentials, const std::string& path,
                                      std::string& id) {
    id = "root";
    for (const auto& seg : Segments(path)) {
        std::string next, mime;
        Result r = FindChild(credentials, id, seg, next, mime);
        if (!r) return r;
        id = next;
    }
    return Result::Ok();
}

Result GoogleDriveProvider::Verify(const Account&, const Credentials& credentials) {
    JSONValue v;
    return GetJson(credentials, std::string(kApi) + "/about" + Query({{"fields", "user"}}), v,
                   "sign in to Google Drive");
}

Result GoogleDriveProvider::AccountInfo(const Account&, const Credentials& credentials,
                                        std::string& username, std::string& displayName) {
    JSONValue v;
    Result r = GetJson(credentials, std::string(kApi) + "/about" + Query({{"fields", "user"}}), v,
                       "read Google account");
    if (!r) return r;
    username = v["user"]["emailAddress"].GetString();
    displayName = v["user"]["displayName"].GetString();
    return Result::Ok();
}

Result GoogleDriveProvider::List(const Account&, const Credentials& credentials,
                                 const std::string& path, std::vector<Entry>& out) {
    out.clear();
    const std::string folder = NormalizePath(path);
    std::string folderId;
    Result rid = ResolveId(credentials, folder, folderId);
    if (!rid) return rid;

    std::string pageToken;
    do {
        std::vector<std::pair<std::string, std::string>> params = {
            {"q", "'" + EscapeQuery(folderId) + "' in parents and trashed = false"},
            {"fields", "nextPageToken,files(id,name,mimeType,size,modifiedTime)"},
            {"pageSize", "200"}};
        if (!pageToken.empty()) params.push_back({"pageToken", pageToken});
        JSONValue v;
        Result r = GetJson(credentials, std::string(kApi) + "/files" + Query(params), v, "list " + path);
        if (!r) return r;
        const JSONValue& files = v["files"];
        for (std::size_t i = 0; i < files.GetSize(); ++i) {
            const JSONValue& f = files[i];
            Entry e;
            e.name = f["name"].GetString();
            e.path = (folder == "/" ? "" : folder) + "/" + e.name;
            e.isDirectory = f["mimeType"].GetString() == kFolderMime;
            e.size = f["size"].IsString() ? std::strtoll(f["size"].GetString().c_str(), nullptr, 10)
                                          : f["size"].GetInteger(0);
            e.modified = f["modifiedTime"].GetString();
            out.push_back(std::move(e));
        }
        pageToken = v["nextPageToken"].GetString();
    } while (!pageToken.empty());

    std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        return a.name < b.name;
    });
    return Result::Ok();
}

Result GoogleDriveProvider::MakeDirectory(const Account&, const Credentials& credentials,
                                          const std::string& path) {
    const std::string p = NormalizePath(path);
    if (p == "/") return Result::Ok();
    std::string parentId;
    Result rp = ResolveId(credentials, Parent(p), parentId);
    if (!rp) return rp;
    std::string existing, mime;
    if (FindChild(credentials, parentId, Leaf(p), existing, mime)) return Result::Ok();

    JSONValue body = JSONValue::MakeObject();
    body.Set("name", Leaf(p));
    body.Set("mimeType", kFolderMime);
    JSONValue parents = JSONValue::MakeArray();
    parents.Append(parentId);
    body.Set("parents", parents);
    UltraNetHttpRequest req;
    req.url = std::string(kApi) + "/files";
    req.method = UltraNetHttpMethod::Post;
    req.headers.Set("Content-Type", "application/json");
    req.body = Bytes(ToJson(body));
    UltraNetResponse resp;
    UltraNetResult net = Send(credentials, req, resp);
    return FromHttp(net, resp, "create folder " + path);
}

Result GoogleDriveProvider::UploadResumable(const Credentials& credentials,
                                            const std::string& localPath, int64_t total,
                                            const std::string& name, const std::string& parentId,
                                            const std::string& existingId, const std::string& what) {
    // Open the session: a create (metadata with the parent) or an update in place.
    UltraNetHttpRequest open;
    if (existingId.empty()) {
        JSONValue meta = JSONValue::MakeObject();
        meta.Set("name", name);
        JSONValue parents = JSONValue::MakeArray();
        parents.Append(parentId);
        meta.Set("parents", parents);
        open.url = std::string(kUpload) + "/files" + Query({{"uploadType", "resumable"}});
        open.method = UltraNetHttpMethod::Post;
        open.headers.Set("Content-Type", "application/json; charset=UTF-8");
        open.body = Bytes(ToJson(meta));
    } else {
        open.url = std::string(kUpload) + "/files/" + existingId + Query({{"uploadType", "resumable"}});
        open.method = UltraNetHttpMethod::Custom;
        open.customMethod = "PATCH";
    }
    open.headers.Set("X-Upload-Content-Type", "application/octet-stream");
    open.headers.Set("X-Upload-Content-Length", std::to_string(total));
    UltraNetResponse openResp;
    UltraNetResult net = Send(credentials, open, openResp);
    Result r = FromHttp(net, openResp, what);
    if (!r) return r;
    const std::string sessionUri = openResp.headers.Get("Location");
    if (sessionUri.empty()) return Result::Error(ResultCode::Server, what + ": no upload session");

    // The chunks. Google answers 308 (Resume Incomplete) to every chunk but
    // the last, which must not be followed as a redirect.
    std::ifstream is(localPath, std::ios::binary);
    if (!is) return Result::Error(ResultCode::IoError, "cannot read " + localPath);
    std::vector<uint8_t> chunk;
    for (int64_t offset = 0; offset < total; offset += chunkSize_) {
        const int64_t length = std::min(chunkSize_, total - offset);
        ReadChunk(is, offset, length, chunk);
        UltraNetHttpRequest put;
        put.url = sessionUri;
        put.method = UltraNetHttpMethod::Put;
        put.headers.Set("Content-Range", ContentRange(offset, length, total));
        put.headers.Set("Content-Type", "application/octet-stream");
        put.body = chunk;
        put.options.followRedirects = false;
        put.options.timeoutMs = 600000;
        UltraNetResponse putResp;
        UltraNetResult pnet = Send(credentials, put, putResp);
        if (putResp.statusCode == 308) continue;   // more to come
        Result pr = FromHttp(pnet, putResp, what);
        if (!pr) return pr;
    }
    return Result::Ok();
}

Result GoogleDriveProvider::Upload(const Account&, const Credentials& credentials,
                                   const std::string& localPath, const std::string& remotePath) {
    const int64_t total = FileSize(localPath);
    if (total < 0) return Result::Error(ResultCode::IoError, "cannot read " + localPath);
    const std::string p = NormalizePath(remotePath);
    const std::string what = "upload " + remotePath;
    std::string parentId;
    Result rp = ResolveId(credentials, Parent(p), parentId);
    if (!rp) return rp;
    std::string existing, mime;
    if (!FindChild(credentials, parentId, Leaf(p), existing, mime)) existing.clear();

    if (total > simpleUploadLimit_)
        return UploadResumable(credentials, localPath, total, Leaf(p), parentId, existing, what);

    std::ifstream is(localPath, std::ios::binary);
    if (!is) return Result::Error(ResultCode::IoError, "cannot read " + localPath);
    std::string data((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());

    UltraNetHttpRequest req;
    if (!existing.empty()) {
        // Same name already there: replace its content in place.
        req.url = std::string(kUpload) + "/files/" + existing + Query({{"uploadType", "media"}});
        req.method = UltraNetHttpMethod::Custom;
        req.customMethod = "PATCH";
        req.headers.Set("Content-Type", "application/octet-stream");
        req.body = Bytes(data);
    } else {
        JSONValue meta = JSONValue::MakeObject();
        meta.Set("name", Leaf(p));
        JSONValue parents = JSONValue::MakeArray();
        parents.Append(parentId);
        meta.Set("parents", parents);
        const std::string boundary = "ultracloud_multipart_boundary";
        std::string body;
        body += "--" + boundary + "\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n";
        body += ToJson(meta) + "\r\n";
        body += "--" + boundary + "\r\nContent-Type: application/octet-stream\r\n\r\n";
        body += data + "\r\n--" + boundary + "--\r\n";
        req.url = std::string(kUpload) + "/files" + Query({{"uploadType", "multipart"}});
        req.method = UltraNetHttpMethod::Post;
        req.headers.Set("Content-Type", "multipart/related; boundary=" + boundary);
        req.body = Bytes(body);
    }
    UltraNetResponse resp;
    UltraNetResult net = Send(credentials, req, resp);
    return FromHttp(net, resp, what);
}

Result GoogleDriveProvider::Download(const Account&, const Credentials& credentials,
                                     const std::string& remotePath, const std::string& localPath) {
    std::string id;
    Result rid = ResolveId(credentials, remotePath, id);
    if (!rid) return rid;
    UltraNetHttpRequest req;
    req.url = std::string(kApi) + "/files/" + id + Query({{"alt", "media"}});
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

Result GoogleDriveProvider::CreateShareLink(const Account&, const Credentials& credentials,
                                            const std::string& remotePath,
                                            const ShareLinkOptions& options, ShareLink& out) {
    std::string id;
    Result rid = ResolveId(credentials, remotePath, id);
    if (!rid) return rid;

    JSONValue perm = JSONValue::MakeObject();
    perm.Set("role", options.readOnly ? "reader" : "writer");
    perm.Set("type", "anyone");
    UltraNetHttpRequest req;
    req.url = std::string(kApi) + "/files/" + id + "/permissions";
    req.method = UltraNetHttpMethod::Post;
    req.headers.Set("Content-Type", "application/json");
    req.body = Bytes(ToJson(perm));
    UltraNetResponse resp;
    UltraNetResult net = Send(credentials, req, resp);
    Result r = FromHttp(net, resp, "share " + remotePath);
    if (!r) return r;
    const std::string permissionId = ParseJson(BodyText(resp))["id"].GetString();

    JSONValue file;
    Result rl = GetJson(credentials, std::string(kApi) + "/files/" + id + Query({{"fields", "webViewLink"}}),
                        file, "share " + remotePath);
    if (!rl) return rl;
    const std::string url = file["webViewLink"].GetString();
    if (url.empty())
        return Result::Error(ResultCode::Server, "share " + remotePath + ": no link in the answer");
    out = ShareLink{};
    out.url = url;
    out.id = permissionId;
    return Result::Ok();
}

} // namespace UltraCloud
