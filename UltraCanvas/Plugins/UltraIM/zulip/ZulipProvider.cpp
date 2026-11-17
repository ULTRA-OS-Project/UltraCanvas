// Plugins/UltraIM/zulip/ZulipProvider.cpp
// Zulip - the streams and direct messages of a Zulip organization as UltraIM conversations
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "ZulipProvider.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCrypt/UltraCryptCore.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetMime.h"
#include "UltraNet/UltraNetUrl.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace UltraIMZulip {

using UltraCanvas::JSONValue;

namespace {

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "chat.zulip.org", "https://chat.zulip.org/" -> "https://chat.zulip.org".
std::string NormalizeBase(const std::string& server) {
    std::string base = Trim(server);
    if (base.empty()) return base;
    if (base.rfind("http://", 0) != 0 && base.rfind("https://", 0) != 0) base = "https://" + base;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base;
}

std::string HostOf(const std::string& base) {
    std::string host = base;
    const size_t scheme = host.find("://");
    if (scheme != std::string::npos) host = host.substr(scheme + 3);
    const size_t slash = host.find('/');
    if (slash != std::string::npos) host = host.substr(0, slash);
    return host;
}

// Zulip's ids are numbers; the JSON has them as numbers, ours as text.
std::string IdText(const JSONValue& v) {
    if (v.IsNumber()) return std::to_string(v.GetInteger());
    return v.GetString();
}

int64_t ParseId(const std::string& text) {
    int64_t value = 0;
    const char* end = text.data() + text.size();
    const auto r = std::from_chars(text.data(), end, value);
    return (r.ec == std::errc() && r.ptr == end) ? value : 0;
}

bool AllDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// User ids in their numeric order: the longer is the larger, then by digits.
bool IdLess(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return a.size() < b.size();
    return a < b;
}

std::string Join(const std::vector<std::string>& parts, const std::string& with) {
    std::string out;
    for (const std::string& p : parts) out += (out.empty() ? "" : with) + p;
    return out;
}

std::vector<std::string> Split(const std::string& text, char at) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t next = text.find(at, start);
        out.push_back(text.substr(start, next == std::string::npos ? std::string::npos : next - start));
        if (next == std::string::npos) break;
        start = next + 1;
    }
    return out;
}

std::string LowerExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || path.find_first_of("/\\?", dot) != std::string::npos) return std::string();
    return Lower(path.substr(dot));
}

const std::pair<const char*, const char*> kMimeTypes[] = {
    {".jpg", "image/jpeg"},  {".jpeg", "image/jpeg"}, {".png", "image/png"},    {".gif", "image/gif"},
    {".webp", "image/webp"}, {".heic", "image/heic"}, {".avif", "image/avif"},  {".bmp", "image/bmp"},
    {".mp4", "video/mp4"},   {".m4v", "video/mp4"},   {".mov", "video/quicktime"}, {".webm", "video/webm"},
    {".mp3", "audio/mpeg"},  {".ogg", "audio/ogg"},   {".oga", "audio/ogg"},    {".opus", "audio/ogg"},
    {".wav", "audio/wav"},   {".m4a", "audio/mp4"},   {".flac", "audio/flac"},  {".aac", "audio/aac"},
    {".pdf", "application/pdf"}, {".zip", "application/zip"}, {".txt", "text/plain"},
};

std::string MimeFor(const std::string& path) {
    const std::string ext = LowerExtension(path);
    for (const auto& entry : kMimeTypes) {
        if (ext == entry.first) return entry.second;
    }
    return "application/octet-stream";
}

UltraIMMessageType TypeForMime(const std::string& mime) {
    if (mime.rfind("image/", 0) == 0) return UltraIMMessageType::Image;
    if (mime.rfind("video/", 0) == 0) return UltraIMMessageType::Video;
    if (mime.rfind("audio/", 0) == 0) return UltraIMMessageType::Audio;
    return UltraIMMessageType::File;
}

// "%20" and the like in a URL's last part, for the name shown.
std::string PercentDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string FileNameOf(const std::string& url) {
    std::string path = url.substr(0, url.find('?'));
    const size_t slash = path.find_last_of('/');
    return PercentDecode(slash == std::string::npos ? path : path.substr(slash + 1));
}

// A file's name inside a Markdown link's [label]: no brackets, no new lines.
std::string LinkLabel(const std::string& name) {
    std::string out;
    for (char c : name) out += (c == '[' || c == ']' || c == '\n' || c == '\r') ? '_' : c;
    return out.empty() ? "file" : out;
}

// What a message's Markdown says, as UltraIM shows it: the first upload it
// links becomes the attachment, and its link leaves the text when it stands
// on a line of its own (Zulip's own clients write "[name](/user_uploads/...)"
// on a line after the caption). linkLine gets that line, to put back on an edit.
void ParseContent(const std::string& content, const std::string& base, UltraIMMessage& m, std::string* linkLine) {
    m.text = Trim(content);
    m.type = UltraIMMessageType::Text;
    m.attachment = UltraIMAttachment();
    if (linkLine) linkLine->clear();
    size_t pos = 0;
    while ((pos = content.find("](", pos)) != std::string::npos) {
        const size_t close = content.find(')', pos + 2);
        if (close == std::string::npos) break;
        const std::string url = content.substr(pos + 2, close - pos - 2);
        const bool upload = url.rfind("/user_uploads/", 0) == 0 ||
                            (!base.empty() && url.rfind(base + "/user_uploads/", 0) == 0);
        const size_t open = content.rfind('[', pos);
        if (!upload || open == std::string::npos || content.find('\n', open) < pos) {
            pos = close;
            continue;
        }
        const std::string label = content.substr(open + 1, pos - open - 1);
        m.attachment.remoteUrl = url[0] == '/' ? base + url : url;
        m.attachment.fileName = label.empty() ? FileNameOf(url) : label;
        m.attachment.mimeType = MimeFor(m.attachment.fileName.find('.') != std::string::npos ? m.attachment.fileName
                                                                                            : FileNameOf(url));
        m.type = TypeForMime(m.attachment.mimeType);
        const size_t lineStart = content.rfind('\n', open) == std::string::npos ? 0 : content.rfind('\n', open) + 1;
        const size_t lineEnd = std::min(content.find('\n', close), content.size());
        const std::string link = content.substr(open, close + 1 - open);
        if (Trim(content.substr(lineStart, lineEnd - lineStart)) == link) {
            std::string rest = content.substr(0, lineStart) + (lineEnd < content.size() ? content.substr(lineEnd + 1) : "");
            m.text = Trim(rest);
            if (linkLine) *linkLine = link;
        }
        return;
    }
}

std::string SnippetOf(const UltraIMMessage& m) {
    switch (m.type) {
        case UltraIMMessageType::Image: return "[photo] " + m.text;
        case UltraIMMessageType::Video: return "[video] " + m.text;
        case UltraIMMessageType::Audio: return "[audio] " + m.text;
        case UltraIMMessageType::File:  return "[file] " + (m.text.empty() ? m.attachment.fileName : m.text);
        default: break;
    }
    std::string s = m.text.substr(0, 80);
    std::replace(s.begin(), s.end(), '\n', ' ');
    return s;
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    std::FILE* f = UltraCanvas::OpenFileUtf8(path, "rb");
    if (!f) return false;
    out.clear();
    uint8_t buffer[65536];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) out.insert(out.end(), buffer, buffer + n);
    std::fclose(f);
    return true;
}

bool HasFlag(const std::vector<JSONValue>& flags, const char* flag) {
    for (const JSONValue& f : flags) {
        if (f.GetString() == flag) return true;
    }
    return false;
}

std::string FormEncode(const std::vector<std::pair<std::string, std::string>>& params) {
    std::string out;
    for (const auto& [key, value] : params) {
        out += (out.empty() ? "" : "&") + UltraNet_UrlEncode(key) + "=" + UltraNet_UrlEncode(value);
    }
    return out;
}

} // namespace

// ============================================================================
// LIFETIME
// ============================================================================

ZulipProvider::ZulipProvider(std::string providerId) : providerId_(std::move(providerId)) {}

ZulipProvider::~ZulipProvider() {
    Shutdown();
}

UltraIMCapabilityFlags ZulipProvider::GetCapabilities() const {
    using F = UltraIMCapabilityFlags;
    return F::TextMessages | F::GroupChats | F::ImageMessages | F::FileTransfer | F::EditMessage |
           F::DeleteMessage | F::OfflineMessages | F::MultiDevice | F::AddContact | F::CreateGroup |
           F::SearchMessages | F::LeaveConversation | F::VoiceNotes;
}

UltraIMResult ZulipProvider::Initialize(const UltraIMConfig& config, UltraIMEventSink sink) {
    config_ = config;
    sink_ = std::move(sink);
    if (config.dataDirectory.empty())
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "The Zulip provider needs a data directory (UltraIMConfig::dataDirectory)");
    std::error_code ec;
    const std::filesystem::path dir = UltraCanvas::PathFromUtf8(config.dataDirectory) / UltraCanvas::PathFromUtf8(providerId_);
    std::filesystem::create_directories(dir, ec);
    vault_ = UltraVault::DeviceKeyVault(UltraCanvas::PathToUtf8(dir), {"zulip.vault", config.appId + "." + providerId_ + "."});
    vaultReady_ = true;
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        stopping_ = false;
    }
    workAbort_ = false;
    if (!worker_.joinable()) worker_ = std::thread([this]() { WorkLoop(); });
    return UltraIMResult::Ok();
}

void ZulipProvider::Shutdown() {
    SignOut();
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        stopping_ = true;
        jobs_.clear();
    }
    // A request the worker is in the middle of ends now, not at its timeout.
    workAbort_ = true;
    workWake_.notify_all();
    if (worker_.joinable()) worker_.join();
    sink_ = nullptr;
}

void ZulipProvider::Enqueue(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        if (stopping_) return;
        jobs_.push_back(std::move(job));
    }
    workWake_.notify_one();
}

void ZulipProvider::WorkLoop() {
    UltraNet_SetThreadAbortCheck([this]() { return workAbort_.load(); });
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(workMutex_);
            workWake_.wait(lock, [this]() { return stopping_ || !jobs_.empty(); });
            if (stopping_) break;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        job();
    }
    UltraNet_SetThreadAbortCheck(nullptr);
}

void ZulipProvider::Emit(const UltraIMEvent& event) {
    // The worker and the event queue's thread both tell: one at a time.
    std::lock_guard<std::recursive_mutex> lock(emitMutex_);
    if (sink_) sink_(event);
}

void ZulipProvider::SetState(UltraIMConnectionState state, const std::string& error) {
    if (state_.exchange(state) == state && error.empty()) return;
    UltraIMEvent event;
    event.type = UltraIMEventType::ConnectionStateChange;
    event.providerId = providerId_;
    event.connectionState = state;
    event.errorText = error;
    Emit(event);
}

void ZulipProvider::AuthLost(const std::string& why) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (authLost_) return;
        authLost_ = true;
    }
    {
        std::lock_guard<std::mutex> lock(eventsMutex_);
        stopEvents_ = true;
    }
    eventsWake_.notify_all();
    SetState(UltraIMConnectionState::Error, why);
    UltraIMEvent event;
    event.type = UltraIMEventType::AuthRequire;
    event.providerId = providerId_;
    event.errorText = why;
    Emit(event);
}

std::string ZulipProvider::GetLocalUserId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return me_.id;
}

std::string ZulipProvider::GetServerUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return base_;
}

// ============================================================================
// HTTP
// ============================================================================

std::string ZulipProvider::AuthHeader() const {
    // (Called with mutex_ held.)
    if (email_.empty() || apiKey_.empty()) return std::string();
    const std::string pair = email_ + ":" + apiKey_;
    return "Basic " + UltraNet_Base64Encode(std::vector<uint8_t>(pair.begin(), pair.end()), false);
}

ZulipResponse ZulipProvider::Request(const std::string& method, const std::string& path, const Params& params,
                                     int timeoutMs, bool withAuth) {
    UltraNetHttpRequest request;
    const std::string form = FormEncode(params);
    const bool inBody = method == "POST" || method == "PATCH";
    {
        std::lock_guard<std::mutex> lock(mutex_);
        request.url = base_ + path + (!inBody && !form.empty() ? "?" + form : std::string());
        const std::string auth = withAuth ? AuthHeader() : std::string();
        if (!auth.empty()) request.headers.Set("Authorization", auth);
    }
    request.method = method == "POST"     ? UltraNetHttpMethod::Post
                     : method == "PATCH"  ? UltraNetHttpMethod::Patch
                     : method == "DELETE" ? UltraNetHttpMethod::Delete
                                          : UltraNetHttpMethod::Get;
    request.headers.Set("Accept", "application/json");
    request.options.timeoutMs = timeoutMs;
    if (inBody) {
        request.body.assign(form.begin(), form.end());
        request.headers.Set("Content-Type", "application/x-www-form-urlencoded");
    }
    UltraNetResponse raw;
    UltraNet_HttpRequest(request, raw);
    ZulipResponse r;
    r.status = raw.IsComplete() ? raw.statusCode : 0;
    if (!raw.body.empty()) {
        UltraCanvas::JSONParseResult parsed;
        r.body = UltraCanvas::JSON::Parse(raw.GetBodyAsString(), &parsed);
    }
    if (!r.Ok()) {
        r.error = r.body["msg"].GetString();
        r.code = r.body["code"].GetString();
        if (r.error.empty()) r.error = !raw.transferError.empty() ? raw.transferError : "HTTP " + std::to_string(raw.statusCode);
        if (r.status == 401 && withAuth && state_.load() == UltraIMConnectionState::Connected)
            AuthLost("Zulip no longer takes this sign-in: " + r.error);
    }
    return r;
}

ZulipResponse ZulipProvider::Upload(const std::string& localPath, const std::string& mimeType,
                                    const std::function<void(int64_t, int64_t)>& onProgress) {
    ZulipResponse r;
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(localPath, bytes)) {
        r.error = "There is no file " + localPath;
        return r;
    }
    // multipart/form-data with the one part, "file".
    const std::string boundary = "UltraIM" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string name = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(localPath).filename());
    std::string quotedName;
    for (char c : name) quotedName += (c == '"' || c == '\r' || c == '\n') ? '_' : c;
    const std::string head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + quotedName +
                             "\"\r\nContent-Type: " + mimeType + "\r\n\r\n";
    const std::string tail = "\r\n--" + boundary + "--\r\n";
    UltraNetHttpRequest request;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        request.url = base_ + "/api/v1/user_uploads";
        request.headers.Set("Authorization", AuthHeader());
    }
    request.method = UltraNetHttpMethod::Post;
    request.headers.Set("Accept", "application/json");
    request.headers.Set("Content-Type", "multipart/form-data; boundary=" + boundary);
    request.body.reserve(head.size() + bytes.size() + tail.size());
    request.body.insert(request.body.end(), head.begin(), head.end());
    request.body.insert(request.body.end(), bytes.begin(), bytes.end());
    request.body.insert(request.body.end(), tail.begin(), tail.end());
    request.options.timeoutMs = 600000;
    if (onProgress) request.onUploadProgress = onProgress;
    UltraNetResponse raw;
    UltraNet_HttpRequest(request, raw);
    r.status = raw.IsComplete() ? raw.statusCode : 0;
    if (!raw.body.empty()) {
        UltraCanvas::JSONParseResult parsed;
        r.body = UltraCanvas::JSON::Parse(raw.GetBodyAsString(), &parsed);
    }
    if (!r.Ok()) {
        r.error = r.body["msg"].GetString();
        if (r.error.empty()) r.error = !raw.transferError.empty() ? raw.transferError : "HTTP " + std::to_string(raw.statusCode);
    }
    return r;
}

bool ZulipProvider::Download(const std::string& url, const std::string& toPath) {
    if (url.empty()) return false;
    UltraNetHttpRequest request;
    request.url = url;
    request.method = UltraNetHttpMethod::Get;
    request.options.followRedirects = true;
    request.options.outputFilePath = toPath;
    {
        // The organization's uploads are its members' only: signed, and
        // only towards the organization itself.
        std::lock_guard<std::mutex> lock(mutex_);
        if (!base_.empty() && url.rfind(base_ + "/", 0) == 0) request.headers.Set("Authorization", AuthHeader());
    }
    UltraNetResponse raw;
    UltraNet_HttpRequest(request, raw);
    std::error_code ec;
    if (!raw.IsComplete() || !raw.IsSuccess()) {
        std::filesystem::remove(UltraCanvas::PathFromUtf8(toPath), ec);
        return false;
    }
    return std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(toPath), ec);
}

std::string ZulipProvider::MediaPath(const std::string& url, const std::string& fileName) const {
    std::vector<uint8_t> digest;
    UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, url.data(), url.size(), digest);
    const std::string base = config_.cacheDirectory.empty() ? config_.dataDirectory : config_.cacheDirectory;
    std::error_code ec;
    const std::filesystem::path dir = UltraCanvas::PathFromUtf8(base) / UltraCanvas::PathFromUtf8(providerId_) / "media";
    std::filesystem::create_directories(dir, ec);
    return UltraCanvas::PathToUtf8(dir / UltraCanvas::PathFromUtf8(UltraCrypt_ToHex(digest).substr(0, 24) +
                                                                     LowerExtension(fileName)));
}

// ============================================================================
// SIGNING IN
// ============================================================================

bool ZulipProvider::SaveSession(const std::string& base, const std::string& email, const std::string& key) {
    if (!vaultReady_) return false;
    JSONValue session = JSONValue::MakeObject();
    session.Set("url", JSONValue(base));
    session.Set("email", JSONValue(email));
    session.Set("api_key", JSONValue(key));
    bool stored = false;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { stored = v.Store("session", UltraCanvas::JSON::Serialize(session)); });
    return stored;
}

bool ZulipProvider::LoadSession(std::string& base, std::string& email, std::string& key) const {
    if (!vaultReady_) return false;
    std::string text;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Retrieve("session", text); });
    if (text.empty()) return false;
    UltraCanvas::JSONParseResult parsed;
    const JSONValue session = UltraCanvas::JSON::Parse(text, &parsed);
    base = session["url"].GetString();
    email = session["email"].GetString();
    key = session["api_key"].GetString();
    return !base.empty() && !email.empty() && !key.empty();
}

bool ZulipProvider::HasStoredSession() const {
    std::string base, email, key;
    return LoadSession(base, email, key);
}

void ZulipProvider::SaveHidden() {
    std::string key, text;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        key = "hidden:" + base_ + "|" + Lower(email_);
        JSONValue list = JSONValue::MakeArray();
        for (const std::string& id : hidden_) list.Append(JSONValue(id));
        text = UltraCanvas::JSON::Serialize(list);
    }
    if (vaultReady_) vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Store(key, text); });
}

void ZulipProvider::LoadHidden() {
    std::string key, text;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        key = "hidden:" + base_ + "|" + Lower(email_);
    }
    if (vaultReady_) vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Retrieve(key, text); });
    std::set<std::string> hidden;
    if (!text.empty()) {
        UltraCanvas::JSONParseResult parsed;
        const JSONValue list = UltraCanvas::JSON::Parse(text, &parsed);
        for (const JSONValue& id : list.GetElements()) hidden.insert(id.GetString());
    }
    std::lock_guard<std::mutex> lock(mutex_);
    hidden_ = hidden;
}

void ZulipProvider::SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) {
    if (!vaultReady_) {
        if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotInitialized, "Initialize first"));
        return;
    }
    SetState(UltraIMConnectionState::Connecting);
    Enqueue([this, credentials, onResult]() { SignInOnWorker(credentials, onResult); });
}

void ZulipProvider::SignInOnWorker(UltraIMCredentials credentials, UltraIMResultCallback onResult) {
    const auto fail = [this, &onResult](const UltraIMResult& r) {
        SetState(UltraIMConnectionState::Error, r.message);
        if (onResult) onResult(r);
    };
    std::string storedBase, storedEmail, storedKey;
    LoadSession(storedBase, storedEmail, storedKey);
    std::string base = NormalizeBase(credentials.server);
    if (base.empty()) base = storedBase;
    if (base.empty())
        return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                         "Which Zulip organization? (chat.zulip.org, ...)"));
    std::string email = Trim(credentials.username);
    std::string key = Trim(credentials.accessToken);
    if (!credentials.password.empty()) {
        if (email.empty())
            return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "The account's e-mail address, please"));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            base_ = base;
            email_.clear();
            apiKey_.clear();
        }
        const ZulipResponse r = Request("POST", "/api/v1/fetch_api_key",
                                        {{"username", email}, {"password", credentials.password}}, 30000, false);
        if (!r.Ok()) {
            if (r.status == 0)
                return fail(UltraIMResult::Error(UltraIMResultCode::NetworkError,
                                                 "The server " + HostOf(base) + " does not answer: " + r.error));
            if (r.status == 401 || r.status == 403)
                return fail(UltraIMResult::Error(UltraIMResultCode::AuthFailed, r.error));
            if (r.status == 404)
                return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                                 HostOf(base) + " is not a Zulip organization"));
            return fail(UltraIMResult::Error(UltraIMResultCode::AuthFailed, r.error));
        }
        key = r.body["api_key"].GetString();
        if (!r.body["email"].GetString().empty()) email = r.body["email"].GetString();
    } else if (key.empty() && base == storedBase && (email.empty() || Lower(email) == Lower(storedEmail))) {
        key = storedKey;
        email = storedEmail;
    }
    if (key.empty())
        return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                         "The e-mail address and the password (or an API key), please"));
    if (email.empty())
        return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "The account's e-mail address, please"));
    const UltraIMResult started = StartSession(base, email, key);
    if (!started) return fail(started);
    if (onResult) onResult(started);
}

UltraIMResult ZulipProvider::StartSession(const std::string& base, const std::string& email, const std::string& key) {
    StopEvents();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        base_ = base;
        email_ = email;
        apiKey_ = key;
        authLost_ = false;
        queueId_.clear();
        lastEventId_ = -1;
    }
    const ZulipResponse r = Request("GET", "/api/v1/users/me", {}, 20000);
    if (!r.Ok()) {
        if (r.status == 401 || r.status == 403)
            return UltraIMResult::Error(UltraIMResultCode::AuthFailed, "Zulip does not take this sign-in: " + r.error);
        return UltraIMResult::Error(UltraIMResultCode::NetworkError, "The server does not answer: " + r.error);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        me_.id = IdText(r.body["user_id"]);
        me_.email = r.body["email"].GetString(email);
        me_.name = r.body["full_name"].GetString(me_.email);
        people_.clear();
        people_[me_.id] = me_;
        streamMembers_.clear();
        streamNames_.clear();
        chats_.clear();
        byId_.clear();
        linkLines_.clear();
        clientOfMessage_.clear();
        pendingLocal_.clear();
        maxSeenId_ = 0;
    }
    SaveSession(base, email, key);
    LoadHidden();
    LoadPeople();
    // The queue first: what comes while the list is read is not lost.
    RegisterQueue();
    SetState(UltraIMConnectionState::Connected);
    LoadConversations();
    {
        std::lock_guard<std::mutex> lock(eventsMutex_);
        stopEvents_ = false;
    }
    events_ = std::thread([this]() { EventLoop(); });
    return UltraIMResult::Ok();
}

void ZulipProvider::SignOut() {
    StopEvents();
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        jobs_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        apiKey_.clear();
        chats_.clear();
        queueId_.clear();
    }
    if (state_.load() != UltraIMConnectionState::Disconnected) SetState(UltraIMConnectionState::Disconnected);
}

// ============================================================================
// THE EVENT QUEUE
// ============================================================================

void ZulipProvider::StopEvents() {
    {
        std::lock_guard<std::mutex> lock(eventsMutex_);
        stopEvents_ = true;
    }
    eventsWake_.notify_all();
    // From the queue's own thread (a sink that signs out on AuthRequire), it
    // ends by itself and is joined by the next sign-in or the shutdown.
    if (events_.joinable() && events_.get_id() != std::this_thread::get_id()) events_.join();
}

void ZulipProvider::WaitEvents(int ms) {
    std::unique_lock<std::mutex> lock(eventsMutex_);
    eventsWake_.wait_for(lock, std::chrono::milliseconds(ms), [this]() { return stopEvents_.load(); });
}

bool ZulipProvider::RegisterQueue() {
    const ZulipResponse r = Request("POST", "/api/v1/register",
                                    {{"event_types", "[\"message\",\"update_message\",\"delete_message\"]"},
                                     {"apply_markdown", "false"},
                                     {"client_gravatar", "false"}},
                                    30000);
    if (!r.Ok() || r.body["queue_id"].GetString().empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    queueId_ = r.body["queue_id"].GetString();
    lastEventId_ = r.body["last_event_id"].GetInteger(-1);
    return true;
}

void ZulipProvider::EventLoop() {
    // A long-poll open when the provider signs out ends at once (within
    // about a second), not when the server answers.
    UltraNet_SetThreadAbortCheck([this]() { return stopEvents_.load(); });
    int wait = std::max(50, timings_.retryMinMs);
    while (!stopEvents_.load()) {
        std::string queue;
        int64_t last = -1;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue = queueId_;
            last = lastEventId_;
        }
        if (queue.empty()) {
            // The first one failed, or the server forgot it: a new one, and
            // the messages that came in between.
            if (!RegisterQueue()) {
                if (stopEvents_.load()) break;
                if (state_.load() == UltraIMConnectionState::Connected) SetState(UltraIMConnectionState::Reconnecting);
                WaitEvents(wait);
                wait = std::min(wait * 2, std::max(wait, timings_.retryMaxMs));
                continue;
            }
            CatchUp();
            if (!stopEvents_.load() && state_.load() == UltraIMConnectionState::Reconnecting)
                SetState(UltraIMConnectionState::Connected);
            continue;
        }
        const ZulipResponse r = Request("GET", "/api/v1/events",
                                        {{"queue_id", queue}, {"last_event_id", std::to_string(last)}},
                                        timings_.longPollMs);
        if (stopEvents_.load()) break;
        if (r.Ok()) {
            wait = std::max(50, timings_.retryMinMs);
            if (state_.load() == UltraIMConnectionState::Reconnecting) SetState(UltraIMConnectionState::Connected);
            for (const JSONValue& event : r.body["events"].GetElements()) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (queueId_ != queue) break;
                    lastEventId_ = std::max(lastEventId_, event["id"].GetInteger(lastEventId_));
                }
                HandleEvent(event);
            }
            continue;
        }
        if (r.code == "BAD_EVENT_QUEUE_ID") {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queueId_ == queue) queueId_.clear();
            continue;
        }
        if (r.status == 401) break;                 // AuthLost has told
        if (state_.load() == UltraIMConnectionState::Connected) SetState(UltraIMConnectionState::Reconnecting);
        WaitEvents(wait);
        wait = std::min(wait * 2, std::max(wait, timings_.retryMaxMs));
    }
    UltraNet_SetThreadAbortCheck(nullptr);
}

void ZulipProvider::CatchUp() {
    int64_t after = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        after = maxSeenId_;
    }
    if (after <= 0) return;
    const ZulipResponse r = Request("GET", "/api/v1/messages",
                                    {{"anchor", std::to_string(after)},
                                     {"num_before", "0"},
                                     {"num_after", "200"},
                                     {"include_anchor", "false"},
                                     {"apply_markdown", "false"},
                                     {"narrow", "[]"}},
                                    30000);
    if (!r.Ok()) return;
    for (const JSONValue& m : r.body["messages"].GetElements()) Received(m, m["flags"].GetElements(), std::string());
}

void ZulipProvider::HandleEvent(const JSONValue& event) {
    const std::string type = event["type"].GetString();
    if (type == "message") {
        Received(event["message"], event["flags"].GetElements(), event["local_message_id"].GetString());
        return;
    }
    if (type == "update_message") {
        const JSONValue* content = event.Find("content");
        if (!content) return;                       // a topic moved, a link previewed: nothing to show
        const int64_t id = event["message_id"].GetInteger();
        UltraIMMessage m;
        bool known = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = byId_.find(id);
            if (it != byId_.end()) {
                m = it->second;
                known = true;
            }
        }
        if (!known) {
            // Not met yet: as the server has it now.
            const ZulipResponse r = Request("GET", "/api/v1/messages/" + std::to_string(id),
                                            {{"apply_markdown", "false"}}, 20000);
            if (!r.Ok()) return;
            std::lock_guard<std::mutex> lock(mutex_);
            if (!MessageFrom(r.body["message"], m)) return;
        }
        UltraIMMessage edited = m;
        std::string linkLine;
        std::string base;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            base = base_;
        }
        ParseContent(content->GetString(), base, edited, &linkLine);
        if (edited.attachment.remoteUrl == m.attachment.remoteUrl) {
            edited.attachment.localPath = m.attachment.localPath;
            edited.attachment.isDownloaded = m.attachment.isDownloaded;
        }
        if (known && m.isEdited && edited.text == m.text) return;       // our own edit, told already
        edited.isEdited = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            linkLines_[id] = linkLine;
            Remember(edited);
            Chat* chat = ChatOf(edited.conversationId);
            if (chat && chat->hasLast && chat->last.messageId == edited.messageId) chat->last = edited;
        }
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageUpdate;
        e.providerId = providerId_;
        e.conversationId = edited.conversationId;
        e.message = edited;
        Emit(e);
        return;
    }
    if (type == "delete_message") {
        std::vector<int64_t> ids;
        for (const JSONValue& id : event["message_ids"].GetElements()) ids.push_back(id.GetInteger());
        if (ids.empty() && event.Contains("message_id")) ids.push_back(event["message_id"].GetInteger());
        for (const int64_t id : ids) {
            UltraIMEvent e;
            bool listed = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = byId_.find(id);
                if (it == byId_.end()) continue;    // never shown here
                e.type = UltraIMEventType::MessageDelete;
                e.providerId = providerId_;
                e.conversationId = it->second.conversationId;
                e.message.providerId = providerId_;
                e.message.conversationId = it->second.conversationId;
                e.message.messageId = it->second.messageId;
                e.message.clientMessageId = it->second.clientMessageId;
                byId_.erase(it);
                linkLines_.erase(id);
                Chat* chat = ChatOf(e.conversationId);
                if (chat) {
                    chat->unread.erase(id);
                    if (chat->hasLast && chat->last.messageId == e.message.messageId) {
                        // The one before it is the last now, where it is known.
                        chat->hasLast = false;
                        for (auto m = byId_.rbegin(); m != byId_.rend(); ++m) {
                            if (m->second.conversationId == chat->id) {
                                chat->last = m->second;
                                chat->hasLast = true;
                                break;
                            }
                        }
                    }
                    listed = !hidden_.count(chat->id);
                }
            }
            Emit(e);
            if (listed) EmitConversation(e.conversationId);
        }
        return;
    }
    // "heartbeat" and the rest: nothing to do.
}

void ZulipProvider::Received(const JSONValue& message, const std::vector<JSONValue>& flags, const std::string& localId) {
    UltraIMMessage m;
    bool tell = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const int64_t id = message["id"].GetInteger();
        if (id <= 0 || byId_.count(id)) return;     // told already (or ours, sent from here)
        // Our own, sent from here: by the local id the queue hands back, or -
        // from a queue registered after it was sent - by its words.
        std::string client;
        if (!localId.empty() && pendingLocal_.count(localId)) {
            client = localId;
        } else if (IdText(message["sender_id"]) == me_.id) {
            const std::string content = Trim(message["content"].GetString());
            for (const auto& [clientId, sentContent] : pendingLocal_) {
                if (sentContent == content) {
                    client = clientId;
                    break;
                }
            }
        }
        const bool echo = !client.empty();
        if (echo) {
            pendingLocal_.erase(client);
            clientOfMessage_[std::to_string(id)] = client;
        }
        if (!MessageFrom(message, m)) return;
        Chat* chat = ChatOf(m.conversationId);
        if (!chat) return;
        if (!m.isOwn && !HasFlag(flags, "read")) chat->unread.insert(id);
        Remember(m);
        UpdateLast(*chat, m);
        if (echo) return;                           // our own, from this device: SendPost tells
        if (hidden_.count(chat->id)) {
            // A direct chat comes back with its next message; a topic left
            // stays muted.
            if (chat->isStream) return;
            hidden_.erase(chat->id);
            tell = true;
        }
    }
    if (tell) SaveHidden();
    UltraIMEvent e;
    e.type = UltraIMEventType::MessageReceive;
    e.providerId = providerId_;
    e.conversationId = m.conversationId;
    e.contactId = m.senderId;
    e.message = m;
    Emit(e);
    EmitConversation(m.conversationId);
    QueueMedia(m);
}

// ============================================================================
// CONVERSATIONS
// ============================================================================

void ZulipProvider::LoadPeople() {
    const ZulipResponse r = Request("GET", "/api/v1/users", {}, 30000);
    if (!r.Ok()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const JSONValue& u : r.body["members"].GetElements()) {
        Person p;
        p.id = IdText(u["user_id"]);
        p.email = u["email"].GetString();
        p.name = u["full_name"].GetString(p.email);
        if (!p.id.empty()) people_[p.id] = p;
    }
}

ZulipProvider::Chat& ZulipProvider::ChatFor(const JSONValue& message) {
    std::string id;
    Chat fresh;
    if (message["type"].GetString() == "stream") {
        fresh.isStream = true;
        fresh.streamId = IdText(message["stream_id"]);
        fresh.streamName = message["display_recipient"].GetString();
        fresh.topic = message["subject"].GetString();
        if (!fresh.streamName.empty()) streamNames_[fresh.streamId] = fresh.streamName;
        id = "stream:" + fresh.streamId + ":" + fresh.topic;
    } else {
        for (const JSONValue& r : message["display_recipient"].GetElements()) {
            Person p;
            p.id = IdText(r["id"]);
            p.email = r["email"].GetString();
            p.name = r["full_name"].GetString(p.email);
            if (p.id.empty()) continue;
            if (!people_.count(p.id)) people_[p.id] = p;
            if (p.id != me_.id) fresh.others.push_back(p.id);
        }
        if (fresh.others.empty()) fresh.others.push_back(me_.id);       // notes to oneself
        std::sort(fresh.others.begin(), fresh.others.end(), IdLess);
        id = "dm:" + Join(fresh.others, ",");
    }
    Chat& chat = chats_[id];
    if (chat.id.empty()) {
        fresh.id = id;
        chat = fresh;
    } else if (chat.isStream && chat.streamName.empty()) {
        chat.streamName = fresh.streamName;
    }
    return chat;
}

ZulipProvider::Chat* ZulipProvider::ChatOf(const std::string& conversationId) {
    auto it = chats_.find(conversationId);
    if (it != chats_.end()) return &it->second;
    Chat chat;
    chat.id = conversationId;
    if (conversationId.rfind("dm:", 0) == 0) {
        for (const std::string& id : Split(conversationId.substr(3), ',')) {
            if (!AllDigits(id)) return nullptr;
            chat.others.push_back(id);
        }
        std::sort(chat.others.begin(), chat.others.end(), IdLess);
        if (chat.id != "dm:" + Join(chat.others, ",")) return nullptr;
    } else if (conversationId.rfind("stream:", 0) == 0) {
        const size_t colon = conversationId.find(':', 7);
        if (colon == std::string::npos) return nullptr;
        chat.isStream = true;
        chat.streamId = conversationId.substr(7, colon - 7);
        chat.topic = conversationId.substr(colon + 1);
        if (!AllDigits(chat.streamId)) return nullptr;
        auto name = streamNames_.find(chat.streamId);
        if (name != streamNames_.end()) chat.streamName = name->second;
    } else {
        return nullptr;
    }
    return &(chats_[conversationId] = chat);
}

UltraIMConversation ZulipProvider::ConversationOf(const Chat& chat) const {
    UltraIMConversation c;
    c.conversationId = chat.id;
    c.providerId = providerId_;
    const auto nameOf = [this](const std::string& id) {
        auto p = people_.find(id);
        return p != people_.end() && !p->second.name.empty() ? p->second.name : id;
    };
    if (chat.isStream) {
        c.isGroup = true;
        c.title = "#" + (chat.streamName.empty() ? chat.streamId : chat.streamName) + " > " + chat.topic;
        auto members = streamMembers_.find(chat.streamId);
        if (members != streamMembers_.end()) c.memberIds = members->second;
        if (!me_.id.empty() && std::find(c.memberIds.begin(), c.memberIds.end(), me_.id) == c.memberIds.end())
            c.memberIds.insert(c.memberIds.begin(), me_.id);
    } else {
        const bool self = chat.others.size() == 1 && chat.others.front() == me_.id;
        c.isGroup = chat.others.size() > 1;
        std::vector<std::string> names;
        for (const std::string& id : chat.others) names.push_back(nameOf(id));
        c.title = Join(names, ", ");
        if (!self) c.memberIds.push_back(me_.id);
        for (const std::string& id : chat.others) c.memberIds.push_back(id);
        if (!c.isGroup) c.directContactId = chat.others.front();
    }
    for (const std::string& id : c.memberIds) c.memberNames[id] = nameOf(id);
    if (chat.hasLast) {
        c.lastMessageSnippet = SnippetOf(chat.last);
        c.lastMessageTimeMs = chat.last.timestampMs;
    }
    c.unreadCount = static_cast<int>(chat.unread.size());
    return c;
}

bool ZulipProvider::MessageFrom(const JSONValue& message, UltraIMMessage& out) {
    const int64_t id = message["id"].GetInteger();
    if (id <= 0) return false;
    const Chat& chat = ChatFor(message);
    out = UltraIMMessage();
    out.providerId = providerId_;
    out.conversationId = chat.id;
    out.messageId = std::to_string(id);
    auto client = clientOfMessage_.find(out.messageId);
    out.clientMessageId = client != clientOfMessage_.end() ? client->second : out.messageId;
    out.senderId = IdText(message["sender_id"]);
    out.senderName = message["sender_full_name"].GetString();
    if (out.senderName.empty()) {
        auto p = people_.find(out.senderId);
        if (p != people_.end()) out.senderName = p->second.name;
    }
    if (!out.senderId.empty() && !people_.count(out.senderId)) {
        Person p;
        p.id = out.senderId;
        p.email = message["sender_email"].GetString();
        p.name = out.senderName;
        people_[p.id] = p;
    }
    out.isOwn = out.senderId == me_.id;
    std::string linkLine;
    ParseContent(message["content"].GetString(), base_, out, &linkLine);
    linkLines_[id] = linkLine;
    out.timestampMs = message["timestamp"].GetInteger() * 1000;
    out.isEdited = message["last_edit_timestamp"].GetInteger() > 0;
    out.deliveryStatus = out.isOwn ? UltraIMDeliveryStatus::Sent : UltraIMDeliveryStatus::Delivered;
    return true;
}

void ZulipProvider::Remember(const UltraIMMessage& m) {
    const int64_t id = ParseId(m.messageId);
    if (id <= 0) return;
    auto old = byId_.find(id);
    UltraIMMessage kept = m;
    if (old != byId_.end() && old->second.attachment.remoteUrl == m.attachment.remoteUrl && kept.attachment.localPath.empty()) {
        kept.attachment.localPath = old->second.attachment.localPath;
        kept.attachment.isDownloaded = old->second.attachment.isDownloaded;
    }
    byId_[id] = kept;
    maxSeenId_ = std::max(maxSeenId_, id);
}

void ZulipProvider::UpdateLast(Chat& chat, const UltraIMMessage& m) {
    if (!chat.hasLast || ParseId(m.messageId) >= ParseId(chat.last.messageId)) {
        chat.last = m;
        chat.hasLast = true;
    }
}

std::string ZulipProvider::ServerIdOf(const std::string& messageId) const {
    for (const auto& [id, client] : clientOfMessage_) {
        if (client == messageId) return id;
    }
    return messageId;
}

std::string ZulipProvider::NarrowOf(const Chat& chat) const {
    JSONValue narrow = JSONValue::MakeArray();
    if (chat.isStream) {
        JSONValue stream = JSONValue::MakeObject();
        stream.Set("operator", JSONValue("stream"));
        stream.Set("operand", JSONValue(ParseId(chat.streamId)));
        narrow.Append(stream);
        JSONValue topic = JSONValue::MakeObject();
        topic.Set("operator", JSONValue("topic"));
        topic.Set("operand", JSONValue(chat.topic));
        narrow.Append(topic);
    } else {
        JSONValue ids = JSONValue::MakeArray();
        for (const std::string& id : chat.others) ids.Append(JSONValue(ParseId(id)));
        JSONValue dm = JSONValue::MakeObject();
        dm.Set("operator", JSONValue("dm"));
        dm.Set("operand", ids);
        narrow.Append(dm);
    }
    return UltraCanvas::JSON::Serialize(narrow);
}

void ZulipProvider::EmitConversation(const std::string& conversationId) {
    UltraIMEvent e;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = chats_.find(conversationId);
        if (it == chats_.end() || hidden_.count(conversationId)) return;
        e.conversation = ConversationOf(it->second);
    }
    e.type = UltraIMEventType::ConversationUpdate;
    e.providerId = providerId_;
    e.conversationId = conversationId;
    Emit(e);
}

void ZulipProvider::LoadConversations() {
    struct Stream {
        std::string id, name;
        std::vector<std::string> topics, members;
        std::vector<JSONValue> messages;
    };
    std::vector<Stream> streams;
    const ZulipResponse subs = Request("GET", "/api/v1/users/me/subscriptions", {}, 30000);
    for (const JSONValue& s : subs.body["subscriptions"].GetElements()) {
        Stream stream;
        stream.id = IdText(s["stream_id"]);
        stream.name = s["name"].GetString();
        if (!stream.id.empty()) streams.push_back(stream);
    }
    for (Stream& stream : streams) {
        const ZulipResponse members = Request("GET", "/api/v1/streams/" + stream.id + "/members", {}, 30000);
        for (const JSONValue& id : members.body["subscribers"].GetElements()) stream.members.push_back(IdText(id));
        const ZulipResponse topics = Request("GET", "/api/v1/users/me/" + stream.id + "/topics", {}, 30000);
        for (const JSONValue& t : topics.body["topics"].GetElements()) {
            if (static_cast<int>(stream.topics.size()) >= timings_.topicsPerStream) break;
            stream.topics.push_back(t["name"].GetString());
        }
        JSONValue narrow = JSONValue::MakeArray();
        JSONValue term = JSONValue::MakeObject();
        term.Set("operator", JSONValue("stream"));
        term.Set("operand", JSONValue(ParseId(stream.id)));
        narrow.Append(term);
        const ZulipResponse recent = Request("GET", "/api/v1/messages",
                                             {{"anchor", "newest"}, {"num_before", "200"}, {"num_after", "0"},
                                              {"apply_markdown", "false"}, {"narrow", UltraCanvas::JSON::Serialize(narrow)}},
                                             30000);
        stream.messages = recent.body["messages"].GetElements();
    }
    const ZulipResponse direct = Request("GET", "/api/v1/messages",
                                         {{"anchor", "newest"}, {"num_before", "200"}, {"num_after", "0"},
                                          {"apply_markdown", "false"},
                                          {"narrow", "[{\"operator\":\"is\",\"operand\":\"dm\"}]"}},
                                         30000);
    std::vector<std::string> changed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::string, std::pair<std::string, size_t>> before;     // id -> last message, unread
        for (const auto& [id, chat] : chats_) before[id] = {chat.hasLast ? chat.last.messageId : "", chat.unread.size()};
        std::vector<JSONValue> messages = direct.body["messages"].GetElements();
        for (const Stream& stream : streams) {
            streamNames_[stream.id] = stream.name;
            streamMembers_[stream.id] = stream.members;
            for (const std::string& topic : stream.topics) {
                Chat* chat = ChatOf("stream:" + stream.id + ":" + topic);
                if (chat && chat->streamName.empty()) chat->streamName = stream.name;
            }
            messages.insert(messages.end(), stream.messages.begin(), stream.messages.end());
        }
        for (const JSONValue& message : messages) {
            UltraIMMessage m;
            if (!MessageFrom(message, m)) continue;
            Chat* chat = ChatOf(m.conversationId);
            if (!chat) continue;
            const int64_t id = ParseId(m.messageId);
            if (!m.isOwn && !HasFlag(message["flags"].GetElements(), "read")) chat->unread.insert(id);
            else chat->unread.erase(id);
            Remember(m);
            UpdateLast(*chat, m);
        }
        for (const auto& [id, chat] : chats_) {
            if (hidden_.count(id)) continue;
            auto was = before.find(id);
            if (was == before.end() || was->second.first != (chat.hasLast ? chat.last.messageId : "") ||
                was->second.second != chat.unread.size())
                changed.push_back(id);
        }
    }
    for (const std::string& id : changed) EmitConversation(id);
}

void ZulipProvider::QueueMedia(const UltraIMMessage& message) {
    // A picture comes by itself (its file); the rest when asked.
    if (message.type != UltraIMMessageType::Image || message.attachment.remoteUrl.empty() ||
        message.attachment.isDownloaded)
        return;
    Enqueue([this, message]() {
        const std::string path = MediaPath(message.attachment.remoteUrl, message.attachment.fileName);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(path), ec) &&
            !Download(message.attachment.remoteUrl, path))
            return;
        UltraIMMessage updated = message;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const int64_t id = ParseId(message.messageId);
            auto it = byId_.find(id);
            if (it != byId_.end()) updated = it->second;
            updated.attachment.localPath = path;
            updated.attachment.isDownloaded = true;
            if (it != byId_.end()) it->second = updated;
        }
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageUpdate;
        e.providerId = providerId_;
        e.conversationId = updated.conversationId;
        e.message = updated;
        Emit(e);
    });
}

void ZulipProvider::FetchConversations(UltraIMConversationsCallback onResult) {
    Enqueue([this, onResult]() {
        if (state_.load() != UltraIMConnectionState::Connected &&
            state_.load() != UltraIMConnectionState::Reconnecting) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Zulip"), {});
            return;
        }
        LoadConversations();
        std::vector<UltraIMConversation> list;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [id, chat] : chats_) {
                if (!hidden_.count(id)) list.push_back(ConversationOf(chat));
            }
        }
        std::stable_sort(list.begin(), list.end(), [](const UltraIMConversation& a, const UltraIMConversation& b) {
            return a.lastMessageTimeMs > b.lastMessageTimeMs;
        });
        if (onResult) onResult(UltraIMResult::Ok(), list);
    });
}

void ZulipProvider::FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                                  UltraIMMessagesCallback onResult) {
    Enqueue([this, conversationId, beforeMessageId, limit, onResult]() {
        std::string narrow, anchor;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Chat* chat = ChatOf(conversationId);
            if (!chat) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation"), {});
                return;
            }
            narrow = NarrowOf(*chat);
            anchor = beforeMessageId.empty() ? "newest" : ServerIdOf(beforeMessageId);
        }
        if (anchor != "newest" && ParseId(anchor) <= 0) {
            if (onResult) onResult(UltraIMResult::Ok(), {});       // not sent yet: nothing before it here
            return;
        }
        const ZulipResponse r = Request("GET", "/api/v1/messages",
                                        {{"anchor", anchor},
                                         {"num_before", std::to_string(limit > 0 ? limit : 50)},
                                         {"num_after", "0"},
                                         {"include_anchor", anchor == "newest" ? "true" : "false"},
                                         {"apply_markdown", "false"},
                                         {"narrow", narrow}},
                                        30000);
        if (!r.Ok()) {
            if (onResult)
                onResult(UltraIMResult::Error(r.status == 0 ? UltraIMResultCode::NetworkError : UltraIMResultCode::Unknown,
                                              r.error),
                         {});
            return;
        }
        std::vector<UltraIMMessage> page;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const JSONValue& message : r.body["messages"].GetElements()) {
                UltraIMMessage m;
                if (!MessageFrom(message, m) || m.conversationId != conversationId) continue;
                Remember(m);
                m = byId_[ParseId(m.messageId)];        // with a picture fetched before
                Chat* chat = ChatOf(conversationId);
                if (chat) UpdateLast(*chat, m);
                page.push_back(m);
            }
        }
        std::sort(page.begin(), page.end(), [](const UltraIMMessage& a, const UltraIMMessage& b) {
            return ParseId(a.messageId) < ParseId(b.messageId);
        });
        if (onResult) onResult(UltraIMResult::Ok(), page);
        for (const UltraIMMessage& m : page) QueueMedia(m);
    });
}

void ZulipProvider::SearchMessages(const UltraIMMessageSearch& search, UltraIMSearchCallback onResult) {
    if (search.conversationId.empty()) {
        if (onResult)
            onResult(UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "Zulip searches one conversation at a time"),
                     UltraIMSearchPage());
        return;
    }
    UltraIM_SearchByFetching(
        [this](const std::string& conversationId, const std::string& before, int limit, UltraIMMessagesCallback cb) {
            FetchMessages(conversationId, before, limit, std::move(cb));
        },
        search, std::move(onResult));
}

void ZulipProvider::FetchContacts(UltraIMContactsCallback onResult) {
    Enqueue([this, onResult]() {
        const ZulipResponse r = Request("GET", "/api/v1/users", {}, 30000);
        if (!r.Ok()) {
            if (onResult)
                onResult(UltraIMResult::Error(r.status == 0 ? UltraIMResultCode::NetworkError : UltraIMResultCode::Unknown,
                                              r.error),
                         {});
            return;
        }
        std::vector<UltraIMContact> out;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const JSONValue& u : r.body["members"].GetElements()) {
                Person p;
                p.id = IdText(u["user_id"]);
                p.email = u["email"].GetString();
                p.name = u["full_name"].GetString(p.email);
                if (p.id.empty()) continue;
                people_[p.id] = p;
                if (p.id == me_.id || u["is_bot"].GetBoolean(false) || !u["is_active"].GetBoolean(true)) continue;
                UltraIMContact c;
                c.contactId = p.id;
                c.providerId = providerId_;
                c.displayName = p.name;
                c.statusText = p.email;
                out.push_back(c);
            }
        }
        std::sort(out.begin(), out.end(), [](const UltraIMContact& a, const UltraIMContact& b) {
            return UltraIM_FoldCase(a.displayName) < UltraIM_FoldCase(b.displayName);
        });
        if (onResult) onResult(UltraIMResult::Ok(), out);
    });
}

UltraIMResult ZulipProvider::ResolvePerson(const std::string& address, Person& out) {
    std::string who = Trim(address);
    while (!who.empty() && who[0] == '@') who.erase(0, 1);
    if (who.rfind("mailto:", 0) == 0) who = who.substr(7);
    if (who.empty()) return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Whom? (an e-mail address)");
    if (!AllDigits(who) && who.find('@') == std::string::npos)
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Zulip finds people by their e-mail address");
    const ZulipResponse r = Request("GET", "/api/v1/users/" + UltraNet_UrlEncode(who), {}, 20000);
    if (!r.Ok()) {
        if (r.status == 0) return UltraIMResult::Error(UltraIMResultCode::NetworkError, r.error);
        return UltraIMResult::Error(UltraIMResultCode::NotFound, "There is no " + who + " in this Zulip organization");
    }
    const JSONValue& u = r.body["user"];
    out.id = IdText(u["user_id"]);
    out.email = u["email"].GetString(who);
    out.name = u["full_name"].GetString(out.email);
    if (out.id.empty()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "There is no " + who + " in this Zulip organization");
    std::lock_guard<std::mutex> lock(mutex_);
    people_[out.id] = out;
    return UltraIMResult::Ok();
}

void ZulipProvider::AddContact(const std::string& address, UltraIMConversationCallback onResult) {
    CreateGroup(std::string(), {address}, std::move(onResult));
}

void ZulipProvider::CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                                UltraIMConversationCallback onResult) {
    (void)title;
    Enqueue([this, memberIds, onResult]() {
        if (state_.load() != UltraIMConnectionState::Connected &&
            state_.load() != UltraIMConnectionState::Reconnecting) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Zulip"), {});
            return;
        }
        std::vector<std::string> others;
        std::string me;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            me = me_.id;
        }
        for (const std::string& address : memberIds) {
            Person p;
            const UltraIMResult r = ResolvePerson(address, p);
            if (!r) {
                if (onResult) onResult(r, {});
                return;
            }
            if (p.id != me && std::find(others.begin(), others.end(), p.id) == others.end()) others.push_back(p.id);
        }
        if (others.empty()) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "That is you"), {});
            return;
        }
        std::sort(others.begin(), others.end(), IdLess);
        const std::string id = "dm:" + Join(others, ",");
        UltraIMConversation conversation;
        bool unhidden = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Chat* chat = ChatOf(id);
            if (!chat) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "No such people"), {});
                return;
            }
            unhidden = hidden_.erase(id) > 0;
            conversation = ConversationOf(*chat);
        }
        if (unhidden) SaveHidden();
        UltraIMEvent e;
        e.type = UltraIMEventType::ConversationUpdate;
        e.providerId = providerId_;
        e.conversationId = id;
        e.conversation = conversation;
        Emit(e);
        if (onResult) onResult(UltraIMResult::Ok(), conversation);
    });
}

void ZulipProvider::LeaveConversation(const std::string& conversationId, UltraIMResultCallback onResult) {
    Enqueue([this, conversationId, onResult]() {
        Chat chat;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Chat* found = ChatOf(conversationId);
            if (!found) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation"));
                return;
            }
            chat = *found;
        }
        if (chat.isStream) {
            // Muted, so its news stays away on the other devices too
            // (Zulip 7.0+; before, the older way).
            const ZulipResponse r = Request("POST", "/api/v1/user_topics",
                                            {{"stream_id", chat.streamId}, {"topic", chat.topic}, {"visibility_policy", "1"}},
                                            20000);
            if (!r.Ok() && r.status != 0)
                Request("PATCH", "/api/v1/users/me/subscriptions/muted_topics",
                        {{"stream_id", chat.streamId}, {"topic", chat.topic}, {"op", "add"}}, 20000);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            hidden_.insert(conversationId);
            auto it = chats_.find(conversationId);
            if (it != chats_.end()) it->second.unread.clear();
        }
        SaveHidden();
        UltraIMEvent e;
        e.type = UltraIMEventType::ConversationRemove;
        e.providerId = providerId_;
        e.conversationId = conversationId;
        Emit(e);
        if (onResult) onResult(UltraIMResult::Ok());
    });
}

// ============================================================================
// SENDING
// ============================================================================

UltraIMResult ZulipProvider::SendText(const std::string& conversationId, const std::string& text,
                                      const UltraIMSendOptions& options) {
    return SendPost(conversationId, text, std::string(), UltraIMMessageType::Text, options);
}

UltraIMResult ZulipProvider::SendImage(const std::string& conversationId, const std::string& localPath,
                                       const std::string& caption, const UltraIMSendOptions& options) {
    return SendPost(conversationId, caption, localPath, UltraIMMessageType::Image, options);
}

UltraIMResult ZulipProvider::SendVideo(const std::string& conversationId, const std::string& localPath,
                                       const std::string& caption, const UltraIMSendOptions& options) {
    return SendPost(conversationId, caption, localPath, UltraIMMessageType::Video, options);
}

UltraIMResult ZulipProvider::SendVoiceNote(const std::string& conversationId, const std::string& localPath,
                                           const UltraIMSendOptions& options) {
    return SendPost(conversationId, std::string(), localPath, UltraIMMessageType::Audio, options);
}

UltraIMResult ZulipProvider::SendFile(const std::string& conversationId, const std::string& localPath,
                                      const UltraIMSendOptions& options) {
    return SendPost(conversationId, std::string(), localPath, UltraIMMessageType::File, options);
}

UltraIMResult ZulipProvider::SendPost(const std::string& conversationId, const std::string& text,
                                      const std::string& localPath, UltraIMMessageType type,
                                      const UltraIMSendOptions& options) {
    if (state_.load() != UltraIMConnectionState::Connected && state_.load() != UltraIMConnectionState::Reconnecting)
        return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Zulip");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ChatOf(conversationId)) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation");
    }
    if (!localPath.empty()) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(localPath), ec))
            return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "There is no file " + localPath);
    } else if (Trim(text).empty()) {
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Nothing to send");
    }
    const std::string clientId = options.clientMessageId.empty() ? UltraIM_NewClientMessageId() : options.clientMessageId;
    Enqueue([this, conversationId, text, localPath, type, clientId]() {
        const auto failed = [this, &conversationId, &clientId](const std::string& why) {
            UltraIMEvent e;
            e.type = UltraIMEventType::MessageDelivered;
            e.providerId = providerId_;
            e.conversationId = conversationId;
            e.message.conversationId = conversationId;
            e.message.clientMessageId = clientId;
            e.message.messageId = clientId;
            e.deliveryStatus = UltraIMDeliveryStatus::Failed;
            e.errorText = why;
            Emit(e);
        };
        std::string content = Trim(text);
        if (!localPath.empty()) {
            int lastPercent = -1;
            const ZulipResponse up = Upload(localPath, MimeFor(localPath), [&](int64_t now, int64_t total) {
                if (total <= 0) return;
                const int percent = static_cast<int>(std::min(now, total) * 100 / total);
                if (percent == lastPercent) return;
                lastPercent = percent;
                UltraIMEvent e;
                e.type = UltraIMEventType::TransferProgress;
                e.providerId = providerId_;
                e.conversationId = conversationId;
                e.message.conversationId = conversationId;
                e.message.clientMessageId = clientId;
                e.message.messageId = clientId;
                e.transferredBytes = std::min(now, total);
                e.totalBytes = total;
                e.isUpload = true;
                Emit(e);
            });
            if (!up.Ok()) return failed(up.error);
            // "url" since Zulip 9.0, "uri" before.
            std::string uri = up.body["url"].GetString();
            if (uri.empty()) uri = up.body["uri"].GetString();
            if (uri.empty()) return failed("The server did not keep the file");
            const std::string name = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(localPath).filename());
            content += (content.empty() ? "" : "\n") + std::string("[") + LinkLabel(name) + "](" + uri + ")";
        }
        Params params;
        std::string queue;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Chat* chat = ChatOf(conversationId);
            if (!chat) return failed("No such conversation");
            if (chat->isStream) {
                params = {{"type", "stream"}, {"to", chat->streamId}, {"topic", chat->topic}};
            } else {
                std::vector<std::string> ids;
                for (const std::string& id : chat->others) ids.push_back(id);
                params = {{"type", "direct"}, {"to", "[" + Join(ids, ",") + "]"}};
            }
            queue = queueId_;
            pendingLocal_[clientId] = content;
        }
        params.push_back({"content", content});
        if (!queue.empty()) {
            // The queue's echo of it then carries clientId: not received again.
            params.push_back({"queue_id", queue});
            params.push_back({"local_id", clientId});
        }
        const ZulipResponse r = Request("POST", "/api/v1/messages", params, 60000);
        if (!r.Ok()) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pendingLocal_.erase(clientId);
            }
            return failed(r.error);
        }
        UltraIMMessage sent;
        bool unhidden = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const int64_t id = r.body["id"].GetInteger();
            pendingLocal_.erase(clientId);
            clientOfMessage_[std::to_string(id)] = clientId;
            auto echoed = byId_.find(id);
            if (echoed != byId_.end()) {
                sent = echoed->second;
            } else {
                sent.providerId = providerId_;
                sent.conversationId = conversationId;
                sent.messageId = std::to_string(id);
                sent.senderId = me_.id;
                sent.senderName = me_.name;
                sent.isOwn = true;
                sent.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
                std::string linkLine;
                ParseContent(content, base_, sent, &linkLine);
                linkLines_[id] = linkLine;
            }
            sent.clientMessageId = clientId;
            sent.deliveryStatus = UltraIMDeliveryStatus::Sent;
            if (!localPath.empty()) {
                sent.type = type;
                sent.attachment.localPath = localPath;
                sent.attachment.isDownloaded = true;
            }
            Remember(sent);
            Chat* chat = ChatOf(conversationId);
            if (chat) UpdateLast(*chat, sent);
            unhidden = hidden_.erase(conversationId) > 0;
        }
        if (unhidden) SaveHidden();
        UltraIMEvent delivered;
        delivered.type = UltraIMEventType::MessageDelivered;
        delivered.providerId = providerId_;
        delivered.conversationId = conversationId;
        delivered.message = sent;
        delivered.deliveryStatus = UltraIMDeliveryStatus::Sent;
        Emit(delivered);
        // The row learns the message's id (for an edit, a delete).
        UltraIMEvent updated;
        updated.type = UltraIMEventType::MessageUpdate;
        updated.providerId = providerId_;
        updated.conversationId = conversationId;
        updated.message = sent;
        Emit(updated);
        EmitConversation(conversationId);
    });
    return UltraIMResult::Ok();
}

void ZulipProvider::DownloadAttachment(const std::string& conversationId, const std::string& messageId,
                                       UltraIMAttachmentCallback onResult) {
    (void)conversationId;
    Enqueue([this, messageId, onResult]() {
        UltraIMMessage m;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = byId_.find(ParseId(ServerIdOf(messageId)));
            if (it == byId_.end() || it->second.attachment.remoteUrl.empty()) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No attachment"), UltraIMAttachment());
                return;
            }
            m = it->second;
        }
        std::error_code ec;
        if (m.attachment.isDownloaded && !m.attachment.localPath.empty() &&
            std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(m.attachment.localPath), ec)) {
            if (onResult) onResult(UltraIMResult::Ok(), m.attachment);
            return;
        }
        const std::string path = MediaPath(m.attachment.remoteUrl, m.attachment.fileName);
        if (!std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(path), ec) && !Download(m.attachment.remoteUrl, path)) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NetworkError, "Could not fetch the file"), UltraIMAttachment());
            return;
        }
        m.attachment.localPath = path;
        m.attachment.isDownloaded = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = byId_.find(ParseId(m.messageId));
            if (it != byId_.end()) it->second.attachment = m.attachment;
        }
        if (onResult) onResult(UltraIMResult::Ok(), m.attachment);
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageUpdate;
        e.providerId = providerId_;
        e.conversationId = m.conversationId;
        e.message = m;
        Emit(e);
    });
}

// ============================================================================
// EDITING, DELETING, READING
// ============================================================================

UltraIMResult ZulipProvider::EditMessage(const std::string& conversationId, const std::string& messageId,
                                         const std::string& newText) {
    int64_t id = 0;
    std::string linkLine;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        id = ParseId(ServerIdOf(messageId));
        auto it = byId_.find(id);
        if (it == byId_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such message");
        if (!it->second.isOwn) return UltraIMResult::Error(UltraIMResultCode::PermissionDenied, "Only its sender can change a message");
        auto link = linkLines_.find(id);
        if (link != linkLines_.end()) linkLine = link->second;
    }
    if (Trim(newText).empty() && linkLine.empty())
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "A text cannot be empty - delete it instead");
    Enqueue([this, id, linkLine, newText, conversationId]() {
        // The upload's link stays: the text is what was shown without it.
        const std::string text = Trim(newText);
        const std::string content = linkLine.empty() ? text : (text.empty() ? linkLine : text + "\n" + linkLine);
        const ZulipResponse r = Request("PATCH", "/api/v1/messages/" + std::to_string(id), {{"content", content}}, 30000);
        if (!r.Ok()) return;
        UltraIMMessage m;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = byId_.find(id);
            if (it == byId_.end()) return;
            m = it->second;
            UltraIMMessage edited = m;
            std::string line;
            ParseContent(content, base_, edited, &line);
            if (edited.attachment.remoteUrl == m.attachment.remoteUrl) {
                edited.type = m.type;
                edited.attachment = m.attachment;
            }
            edited.isEdited = true;
            linkLines_[id] = line;
            it->second = edited;
            m = edited;
            Chat* chat = ChatOf(conversationId);
            if (chat && chat->hasLast && chat->last.messageId == m.messageId) chat->last = m;
        }
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageUpdate;
        e.providerId = providerId_;
        e.conversationId = m.conversationId;
        e.message = m;
        Emit(e);
    });
    return UltraIMResult::Ok();
}

UltraIMDeleteChoices ZulipProvider::GetDeleteChoices(const std::string& conversationId,
                                                     const std::string& messageId) const {
    (void)conversationId;
    UltraIMDeleteChoices choices;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byId_.find(ParseId(ServerIdOf(messageId)));
    choices.forEveryone = it != byId_.end() && it->second.isOwn;
    return choices;
}

UltraIMResult ZulipProvider::DeleteMessage(const std::string& conversationId, const std::string& messageId,
                                           UltraIMDeleteScope scope) {
    (void)conversationId;
    if (scope == UltraIMDeleteScope::ForMe)
        return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "Zulip deletes for everyone only");
    int64_t id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        id = ParseId(ServerIdOf(messageId));
        auto it = byId_.find(id);
        if (it == byId_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such message");
        if (!it->second.isOwn) return UltraIMResult::Error(UltraIMResultCode::PermissionDenied, "Only its sender can delete a message");
    }
    Enqueue([this, id]() {
        const ZulipResponse r = Request("DELETE", "/api/v1/messages/" + std::to_string(id), {}, 30000);
        // "Invalid message(s)": gone already; any other refusal (the
        // organization lets no one delete, the time to do it is over) leaves it.
        if (!r.Ok() && !(r.status == 400 && r.error.find("Invalid message") != std::string::npos)) return;
        // As the queue's delete_message would say it; whichever comes first tells.
        JSONValue event = JSONValue::MakeObject();
        event.Set("type", JSONValue("delete_message"));
        event.Set("message_id", JSONValue(id));
        HandleEvent(event);
    });
    return UltraIMResult::Ok();
}

UltraIMResult ZulipProvider::MarkRead(const std::string& conversationId, const std::string& upToMessageId) {
    (void)upToMessageId;
    std::set<int64_t> unread;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = chats_.find(conversationId);
        if (it == chats_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation");
        unread.swap(it->second.unread);
    }
    if (unread.empty()) return UltraIMResult::Ok();
    std::string ids;
    for (const int64_t id : unread) ids += (ids.empty() ? "" : ",") + std::to_string(id);
    Enqueue([this, ids]() {
        Request("POST", "/api/v1/messages/flags", {{"messages", "[" + ids + "]"}, {"op", "add"}, {"flag", "read"}}, 20000);
    });
    return UltraIMResult::Ok();
}

} // namespace UltraIMZulip
