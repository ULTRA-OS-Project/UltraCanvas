// Apps/UltraStoreAdmin/core/Uploader.cpp
// The publish folder onto the host (Uploader.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Uploader.h"

#include "Verifier.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetFtp.h"
#include "UltraNet/UltraNetHttp.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

using UltraStore::ErrorCode;

namespace UltraStoreAdmin {

namespace {

constexpr const char* kCatalog = "catalog.json";
constexpr const char* kSignature = "catalog.json.sig";

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string TrimSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

bool ReadText(const std::string& pathUtf8, std::string& out) {
    std::ifstream in(UltraCanvas::PathFromUtf8(pathUtf8), std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

bool WriteText(const std::string& pathUtf8, const std::string& text) {
    std::error_code ec;
    const auto path = UltraCanvas::PathFromUtf8(pathUtf8);
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = path.parent_path() / (path.filename().string() + ".tmp");   // path-string-ok: a temp name beside the file
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

// What each target already has: url -> (file -> sha256).
using State = std::map<std::string, std::map<std::string, std::string>>;

State ReadState(const std::string& fileUtf8) {
    State state;
    std::string text;
    if (fileUtf8.empty() || !ReadText(fileUtf8, text)) return state;
    UltraCanvas::JSONParseResult parse;
    const UltraCanvas::JSONValue root = UltraCanvas::JSON::Parse(text, &parse);
    if (!parse.success || !root.IsObject()) return state;
    for (const auto& [url, files] : root.GetMembers()) {
        if (!files.IsObject()) continue;
        for (const auto& [file, hash] : files.GetMembers()) state[url][file] = hash.GetString();
    }
    return state;
}

bool WriteState(const std::string& fileUtf8, const State& state) {
    if (fileUtf8.empty()) return true;
    UltraCanvas::JSONValue root = UltraCanvas::JSONValue::MakeObject();
    for (const auto& [url, files] : state) {
        UltraCanvas::JSONValue o = UltraCanvas::JSONValue::MakeObject();
        for (const auto& [file, hash] : files) o.Set(file, hash);
        root.Set(url, std::move(o));
    }
    UltraCanvas::JSONSerializeOptions options;
    options.pretty = true;
    options.indentWidth = 2;
    return WriteText(fileUtf8, UltraCanvas::JSON::Serialize(root, options));
}

std::string ParentDir(const std::string& relative) {
    const size_t slash = relative.rfind('/');
    return slash == std::string::npos ? std::string() : relative.substr(0, slash);
}

// ===== UltraNet: SFTP / FTPS / FTP =====
class FtpTransport : public IUploadTransport {
public:
    FtpTransport(UploadTarget target, UploadScheme scheme) : target_(std::move(target)), scheme_(scheme) {
        base_ = TrimSlash(target_.url);
    }
    Result<void> EnsureFolder(const std::string&) override { return Result<void>::Succeed(); }   // createMissingDirs
    Result<void> Put(const std::string& relativePath, const std::string& localPathUtf8, uint64_t) override {
        if (!UltraNet_IsInitialized()) UltraNet_Initialize();
        UltraNetFtpOptions options;
        if (!target_.username.empty()) {
            options.credentials.type = UltraNetAuthType::Basic;
            options.credentials.username = target_.username;
        }
        if (!target_.password.empty()) {
            options.credentials.type = UltraNetAuthType::Basic;
            options.credentials.password = target_.password;
        }
        options.useTls = scheme_ == UploadScheme::Ftps;
        options.createMissingDirs = true;
        options.sshPrivateKeyFile = target_.privateKeyFileUtf8;
        options.sshKeyPassphrase = target_.keyPassphrase;
        const UltraNetResult r = UltraNet_FtpUpload(localPathUtf8, base_ + "/" + relativePath, options);
        if (!r) return Result<void>::Failure(ErrorCode::NetworkError, relativePath + ": " + (r.message.empty() ? "the upload failed" : r.message));
        return Result<void>::Succeed();
    }
private:
    UploadTarget target_;
    UploadScheme scheme_;
    std::string base_;
};

// ===== UltraNet: HTTPS PUT (WebDAV) =====
class HttpsTransport : public IUploadTransport {
public:
    explicit HttpsTransport(UploadTarget target) : target_(std::move(target)) { base_ = TrimSlash(target_.url); }
    Result<void> EnsureFolder(const std::string& relativeDir) override {
        UltraNetHttpRequest request = Request(base_ + "/" + relativeDir + "/");
        request.method = UltraNetHttpMethod::Custom;
        request.customMethod = "MKCOL";
        UltraNetResponse response;
        const UltraNetResult r = UltraNet_HttpRequest(request, response);
        // 201 made it; 405 it is there already (or the server knows no MKCOL
        // and makes folders on PUT); 301 some servers answer for a folder.
        if (!response.IsComplete()) return Result<void>::Failure(ErrorCode::NetworkError, relativeDir + ": " + response.transferError);
        if (response.IsSuccess() || response.statusCode == 405 || response.statusCode == 301) return Result<void>::Succeed();
        return Result<void>::Failure(ErrorCode::NetworkError, relativeDir + ": MKCOL answered HTTP " + std::to_string(response.statusCode));
    }
    Result<void> Put(const std::string& relativePath, const std::string& localPathUtf8, uint64_t) override {
        UltraNetHttpRequest request = Request(base_ + "/" + relativePath);
        request.method = UltraNetHttpMethod::Put;
        request.bodyFilePath = localPathUtf8;
        UltraNetResponse response;
        const UltraNetResult r = UltraNet_HttpRequest(request, response);
        if (!response.IsComplete()) return Result<void>::Failure(ErrorCode::NetworkError, relativePath + ": " + response.transferError);
        if (!response.IsSuccess()) return Result<void>::Failure(ErrorCode::NetworkError, relativePath + ": PUT answered HTTP " + std::to_string(response.statusCode));
        (void)r;
        return Result<void>::Succeed();
    }
private:
    UltraNetHttpRequest Request(const std::string& url) const {
        if (!UltraNet_IsInitialized()) UltraNet_Initialize();
        UltraNetHttpRequest request;
        request.url = url;
        request.options.httpsOnlyRedirects = true;
        if (!target_.username.empty() || !target_.password.empty()) {
            request.options.authType = UltraNetAuthType::Basic;
            request.options.credentials.type = UltraNetAuthType::Basic;
            request.options.credentials.username = target_.username;
            request.options.credentials.password = target_.password;
        }
        return request;
    }
    UploadTarget target_;
    std::string base_;
};

} // namespace

UploadScheme SchemeOf(const std::string& url) {
    const std::string lower = Lower(url);
    if (lower.rfind("sftp://", 0) == 0) return UploadScheme::Sftp;
    if (lower.rfind("ftps://", 0) == 0) return UploadScheme::Ftps;
    if (lower.rfind("ftp://", 0) == 0) return UploadScheme::Ftp;
    if (lower.rfind("https://", 0) == 0) return UploadScheme::Https;
    return UploadScheme::Unknown;
}

std::string SchemeName(UploadScheme scheme) {
    switch (scheme) {
        case UploadScheme::Sftp: return "SFTP";
        case UploadScheme::Ftps: return "FTPS";
        case UploadScheme::Ftp: return "FTP";
        case UploadScheme::Https: return "HTTPS PUT";
        case UploadScheme::Unknown: break;
    }
    return "unknown";
}

Result<std::unique_ptr<IUploadTransport>> CreateUltraNetTransport(const UploadTarget& target) {
    using R = Result<std::unique_ptr<IUploadTransport>>;
    const UploadScheme scheme = SchemeOf(target.url);
    switch (scheme) {
        case UploadScheme::Sftp:
        case UploadScheme::Ftps:
        case UploadScheme::Ftp:
            return R::Succeed(std::make_unique<FtpTransport>(target, scheme));
        case UploadScheme::Https:
            return R::Succeed(std::make_unique<HttpsTransport>(target));
        case UploadScheme::Unknown: break;
    }
    return R::Failure(ErrorCode::InvalidArgument,
                      "The upload goes to sftp://, ftps://, ftp:// or https:// (one PUT per file)", target.url);
}

std::string UploadStatePath(const std::string& storeDirUtf8) {
    return UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(storeDirUtf8) / "upload-state.json");
}

Result<std::vector<std::string>> FilesToSend(const std::string& publishDirUtf8) {
    using R = Result<std::vector<std::string>>;
    const auto root = UltraCanvas::PathFromUtf8(publishDirUtf8);
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return R::Failure(ErrorCode::NotFound, "No publish folder at " + publishDirUtf8);
    std::vector<std::string> files;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string relative = UltraCanvas::PathToUtf8(std::filesystem::relative(it->path(), root, ec));
        std::replace(relative.begin(), relative.end(), '\\', '/');
        if (relative == kCatalog || relative == kSignature) continue;
        files.push_back(std::move(relative));
    }
    if (ec) return R::Failure(ErrorCode::IoError, "The publish folder could not be read", ec.message());
    std::sort(files.begin(), files.end());
    if (std::filesystem::is_regular_file(root / kSignature, ec)) files.push_back(kSignature);
    if (std::filesystem::is_regular_file(root / kCatalog, ec)) files.push_back(kCatalog);
    return R::Succeed(std::move(files));
}

Result<UploadReport> UploadPublishFolder(IUploadTransport& transport, const std::string& targetUrl, const UploadOptions& options) {
    using R = Result<UploadReport>;
    const auto files = FilesToSend(options.publishDirUtf8);
    if (!files) return R::Failure(files.error.code, files.error.message, files.error.detail);
    const std::string key = TrimSlash(targetUrl);
    State state = ReadState(options.stateFileUtf8);
    std::map<std::string, std::string>& had = state[key];
    const auto root = UltraCanvas::PathFromUtf8(options.publishDirUtf8);

    // What the server still has that the folder no longer does: said, not
    // deleted - a store half-way through a download needs its file.
    UploadReport report;
    for (auto it = had.begin(); it != had.end();) {
        if (std::find(files.Value().begin(), files.Value().end(), it->first) == files.Value().end()) {
            report.warnings.push_back(it->first + " is no longer in the publish folder; the server still has it");
            it = had.erase(it);
        } else ++it;
    }

    // What to send: the files whose bytes the server does not have.
    struct Item { std::string relative, local, sha256; uint64_t size; };
    std::vector<Item> todo;
    uint64_t bytesTotal = 0;
    for (const std::string& relative : files.Value()) {
        const auto path = root / UltraCanvas::PathFromUtf8(relative);
        Item item;
        item.relative = relative;
        item.local = UltraCanvas::PathToUtf8(path);
        item.sha256 = UltraStore::Sha256HexOfFile(item.local);
        if (item.sha256.empty()) return R::Failure(ErrorCode::IoError, "A file of the publish folder could not be read", relative);
        std::error_code ec;
        item.size = static_cast<uint64_t>(std::filesystem::file_size(path, ec));
        const auto known = had.find(relative);
        if (!options.everything && known != had.end() && known->second == item.sha256) { ++report.filesSkipped; continue; }
        bytesTotal += item.size;
        todo.push_back(std::move(item));
    }

    UploadProgress progress;
    progress.filesTotal = todo.size();
    progress.bytesTotal = bytesTotal;
    std::vector<std::string> foldersMade;
    for (const Item& item : todo) {
        progress.currentFile = item.relative;
        if (options.onProgress) options.onProgress(progress);
        // The folders above it, outermost first, once each.
        std::string dir = ParentDir(item.relative);
        std::vector<std::string> chain;
        for (std::string d = dir; !d.empty(); d = ParentDir(d)) chain.insert(chain.begin(), d);
        for (const std::string& d : chain) {
            if (std::find(foldersMade.begin(), foldersMade.end(), d) != foldersMade.end()) continue;
            if (const auto made = transport.EnsureFolder(d); !made) {
                WriteState(options.stateFileUtf8, state);
                return R::Failure(made.error.code, "The upload stopped at " + item.relative, made.error.message);
            }
            foldersMade.push_back(d);
        }
        if (const auto put = transport.Put(item.relative, item.local, item.size); !put) {
            WriteState(options.stateFileUtf8, state);
            return R::Failure(put.error.code, "The upload stopped at " + item.relative, put.error.message);
        }
        had[item.relative] = item.sha256;
        if (!WriteState(options.stateFileUtf8, state)) {
            report.warnings.push_back("the upload state could not be written to " + options.stateFileUtf8);
        }
        ++report.filesSent;
        report.bytesSent += item.size;
        ++progress.filesDone;
        progress.bytesDone += item.size;
    }
    progress.currentFile.clear();
    if (options.onProgress) options.onProgress(progress);
    return R::Succeed(std::move(report));
}

} // namespace UltraStoreAdmin
