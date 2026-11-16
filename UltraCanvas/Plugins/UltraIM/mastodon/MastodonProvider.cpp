// Plugins/UltraIM/mastodon/MastodonProvider.cpp
// Mastodon - the direct messages of a Mastodon server as UltraIM conversations
// Version: 0.1.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "MastodonProvider.h"

#include "HTMLReader/HTMLDocument.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCrypt/UltraCryptCore.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetOAuth2.h"
#include "UltraNet/UltraNetUrl.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace UltraIMMastodon {

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

// "mastodon.social", "https://mastodon.social/" -> "https://mastodon.social".
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

// Mastodon's ids are numbers that grow with time, sent as text: the longer
// is the larger, then by their digits.
bool IdLess(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return a.size() < b.size();
    return a < b;
}

int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// "2026-11-16T10:04:05.123Z" -> milliseconds since 1970 (0 when unreadable).
int64_t ParseTime(const std::string& text) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return 0;
    int64_t ms = 0;
    const size_t dot = text.find('.');
    if (dot != std::string::npos) {
        int digits = 0;
        for (size_t i = dot + 1; i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) && digits < 3; ++i, ++digits)
            ms = ms * 10 + (text[i] - '0');
        while (digits++ < 3) ms *= 10;
    }
    const int64_t days = DaysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d));
    return ((days * 24 + h) * 60 + mi) * 60000 + static_cast<int64_t>(s) * 1000 + ms;
}

std::string LowerExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || path.find_first_of("/\\?", dot) != std::string::npos) return std::string();
    return Lower(path.substr(dot));
}

const std::pair<const char*, const char*> kMimeTypes[] = {
    {".jpg", "image/jpeg"},  {".jpeg", "image/jpeg"}, {".png", "image/png"},    {".gif", "image/gif"},
    {".webp", "image/webp"}, {".heic", "image/heic"}, {".avif", "image/avif"},
    {".mp4", "video/mp4"},   {".m4v", "video/mp4"},   {".mov", "video/quicktime"}, {".webm", "video/webm"},
    {".mp3", "audio/mpeg"},  {".ogg", "audio/ogg"},   {".oga", "audio/ogg"},    {".opus", "audio/ogg"},
    {".wav", "audio/wav"},   {".m4a", "audio/mp4"},   {".flac", "audio/flac"},  {".aac", "audio/aac"},
};

std::string MimeFor(const std::string& path) {
    const std::string ext = LowerExtension(path);
    for (const auto& entry : kMimeTypes) {
        if (ext == entry.first) return entry.second;
    }
    return "application/octet-stream";
}

// The last part of a URL's path, without its query: the file's name.
std::string FileNameOf(const std::string& url) {
    std::string path = url.substr(0, url.find('?'));
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// The words of a status: its HTML as text, without the mentions it starts
// with (everyone in a direct chat is mentioned in every status).
std::string TextOf(const JSONValue& status) {
    std::string text = UltraCanvas::HTML::ExtractPlainText(status["content"].GetString(),
                                                           UltraCanvas::HTML::PlainTextLayout::Lines);
    text = Trim(text);
    for (;;) {
        if (text.empty() || text[0] != '@') break;
        size_t end = 0;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
        text = Trim(text.substr(end));
    }
    const std::string warning = Trim(status["spoiler_text"].GetString());
    if (!warning.empty()) text = warning + (text.empty() ? "" : "\n" + text);
    return text;
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

} // namespace

// ============================================================================
// LIFETIME
// ============================================================================

MastodonProvider::MastodonProvider(std::string providerId) : providerId_(std::move(providerId)) {}

MastodonProvider::~MastodonProvider() {
    Shutdown();
}

UltraIMCapabilityFlags MastodonProvider::GetCapabilities() const {
    using F = UltraIMCapabilityFlags;
    return F::TextMessages | F::GroupChats | F::ImageMessages | F::FileTransfer | F::ReplyToMessage |
           F::EditMessage | F::DeleteMessage | F::OfflineMessages | F::MultiDevice | F::AddContact |
           F::CreateGroup | F::SearchMessages | F::LeaveConversation | F::VoiceNotes;
}

UltraIMResult MastodonProvider::Initialize(const UltraIMConfig& config, UltraIMEventSink sink) {
    config_ = config;
    sink_ = std::move(sink);
    if (config.dataDirectory.empty())
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "The Mastodon provider needs a data directory (UltraIMConfig::dataDirectory)");
    std::error_code ec;
    const std::filesystem::path dir = UltraCanvas::PathFromUtf8(config.dataDirectory) / UltraCanvas::PathFromUtf8(providerId_);
    std::filesystem::create_directories(dir, ec);
    vault_ = UltraVault::DeviceKeyVault(UltraCanvas::PathToUtf8(dir), {"mastodon.vault", config.appId + "." + providerId_ + "."});
    vaultReady_ = true;
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        stopping_ = false;
    }
    if (!worker_.joinable()) worker_ = std::thread([this]() { WorkLoop(); });
    return UltraIMResult::Ok();
}

void MastodonProvider::Shutdown() {
    SignOut();
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        stopping_ = true;
        jobs_.clear();
    }
    workWake_.notify_all();
    if (worker_.joinable()) worker_.join();
    sink_ = nullptr;
}

void MastodonProvider::Enqueue(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        if (stopping_) return;
        jobs_.push_back(std::move(job));
    }
    workWake_.notify_one();
}

void MastodonProvider::WorkLoop() {
    auto nextPoll = std::chrono::steady_clock::now();
    for (;;) {
        std::function<void()> job;
        bool poll = false;
        {
            std::unique_lock<std::mutex> lock(workMutex_);
            workWake_.wait_until(lock, nextPoll, [this]() { return stopping_ || !jobs_.empty(); });
            if (stopping_) return;
            if (!jobs_.empty()) {
                job = std::move(jobs_.front());
                jobs_.pop_front();
            } else {
                poll = polling_;
                nextPoll = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(200, timings_.pollMs));
            }
        }
        if (job) job();
        else if (poll) Poll(false);
        if (std::chrono::steady_clock::now() >= nextPoll) {
            // Due while jobs ran: next time round.
            std::lock_guard<std::mutex> lock(workMutex_);
            if (polling_ && jobs_.empty()) {
                nextPoll = std::chrono::steady_clock::now();
            }
        }
    }
}

void MastodonProvider::Emit(const UltraIMEvent& event) {
    if (sink_) sink_(event);
}

void MastodonProvider::SetState(UltraIMConnectionState state, const std::string& error) {
    if (state_.exchange(state) == state && error.empty()) return;
    UltraIMEvent event;
    event.type = UltraIMEventType::ConnectionStateChange;
    event.providerId = providerId_;
    event.connectionState = state;
    event.errorText = error;
    Emit(event);
}

std::string MastodonProvider::GetLocalUserId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return me_.id;
}

std::string MastodonProvider::GetServerUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return base_;
}

// ============================================================================
// HTTP
// ============================================================================

MastodonResponse MastodonProvider::Request(const std::string& method, const std::string& path, const JSONValue* body,
                                           int timeoutMs) {
    UltraNetHttpRequest request;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        request.url = base_ + path;
        if (!token_.empty()) request.headers.Set("Authorization", "Bearer " + token_);
    }
    request.method = method == "POST"     ? UltraNetHttpMethod::Post
                     : method == "PUT"    ? UltraNetHttpMethod::Put
                     : method == "DELETE" ? UltraNetHttpMethod::Delete
                                          : UltraNetHttpMethod::Get;
    request.headers.Set("Accept", "application/json");
    request.options.timeoutMs = timeoutMs;
    if (body) {
        const std::string json = UltraCanvas::JSON::Serialize(*body);
        request.body.assign(json.begin(), json.end());
        request.headers.Set("Content-Type", "application/json");
        // A status sent twice (a retry after a lost answer) posts once.
        if (method == "POST" && body->IsObject() && !(*body)["_idempotency"].GetString().empty()) {
            request.headers.Set("Idempotency-Key", (*body)["_idempotency"].GetString());
            JSONValue clean = *body;
            clean.Remove("_idempotency");
            const std::string cleaned = UltraCanvas::JSON::Serialize(clean);
            request.body.assign(cleaned.begin(), cleaned.end());
        }
    }
    UltraNetResponse raw;
    UltraNet_HttpRequest(request, raw);
    MastodonResponse r;
    r.status = raw.IsComplete() ? raw.statusCode : 0;
    if (!raw.body.empty()) {
        UltraCanvas::JSONParseResult parsed;
        r.body = UltraCanvas::JSON::Parse(raw.GetBodyAsString(), &parsed);
    }
    if (!r.Ok()) {
        r.error = r.body["error"].GetString();
        if (r.error.empty()) r.error = !raw.transferError.empty() ? raw.transferError : "HTTP " + std::to_string(raw.statusCode);
    }
    return r;
}

MastodonResponse MastodonProvider::Upload(const std::string& path, const std::string& localPath,
                                          const std::string& mimeType,
                                          const std::function<void(int64_t, int64_t)>& onProgress) {
    MastodonResponse r;
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
        request.url = base_ + path;
        request.headers.Set("Authorization", "Bearer " + token_);
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
        r.error = r.body["error"].GetString();
        if (r.error.empty()) r.error = !raw.transferError.empty() ? raw.transferError : "HTTP " + std::to_string(raw.statusCode);
    }
    return r;
}

bool MastodonProvider::Download(const std::string& url, const std::string& toPath) {
    if (url.empty()) return false;
    UltraNetHttpOptions options;
    options.followRedirects = true;
    const UltraNetResult r = UltraNet_HttpDownloadFile(url, toPath, options);
    std::error_code ec;
    return r.success && std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(toPath), ec);
}

std::string MastodonProvider::MediaPath(const std::string& url, const std::string& fileName) const {
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

bool MastodonProvider::SaveSession(const std::string& base, const std::string& token) {
    if (!vaultReady_) return false;
    JSONValue session = JSONValue::MakeObject();
    session.Set("url", JSONValue(base));
    session.Set("access_token", JSONValue(token));
    bool stored = false;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { stored = v.Store("session", UltraCanvas::JSON::Serialize(session)); });
    return stored;
}

bool MastodonProvider::LoadSession(std::string& base, std::string& token) const {
    if (!vaultReady_) return false;
    std::string text;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Retrieve("session", text); });
    if (text.empty()) return false;
    UltraCanvas::JSONParseResult parsed;
    const JSONValue session = UltraCanvas::JSON::Parse(text, &parsed);
    base = session["url"].GetString();
    token = session["access_token"].GetString();
    return !base.empty() && !token.empty();
}

bool MastodonProvider::HasStoredSession() const {
    std::string base, token;
    return LoadSession(base, token);
}

void MastodonProvider::SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) {
    if (!vaultReady_) {
        if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotInitialized, "Initialize first"));
        return;
    }
    cancelSignIn_ = false;
    SetState(UltraIMConnectionState::Connecting);
    Enqueue([this, credentials, onResult]() { SignInOnWorker(credentials, onResult); });
}

void MastodonProvider::SignInOnWorker(UltraIMCredentials credentials, UltraIMResultCallback onResult) {
    const auto fail = [this, &onResult](const UltraIMResult& r) {
        SetState(UltraIMConnectionState::Error, r.message);
        if (onResult) onResult(r);
    };
    std::string storedBase, storedToken;
    LoadSession(storedBase, storedToken);
    std::string base = NormalizeBase(credentials.server);
    if (base.empty()) base = storedBase;
    if (base.empty())
        return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Which Mastodon server? (mastodon.social, ...)"));
    std::string token = credentials.accessToken;
    if (token.empty() && base == storedBase) token = storedToken;
    if (!token.empty()) {
        const UltraIMResult r = StartSession(base, token);
        if (r) {
            if (onResult) onResult(r);
            return;
        }
        // A stored token the server no longer takes: sign in again in the browser.
        if (!credentials.accessToken.empty() || r.code != UltraIMResultCode::AuthFailed) return fail(r);
        token.clear();
    }
    const UltraIMResult browser = BrowserSignIn(base, token);
    if (!browser) return fail(browser);
    const UltraIMResult started = StartSession(base, token);
    if (!started) return fail(started);
    if (onResult) onResult(started);
}

UltraIMResult MastodonProvider::BrowserSignIn(const std::string& base, std::string& token) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        base_ = base;
        token_.clear();
    }
    // The application as this server knows it: registered once, kept.
    std::string clientId, clientSecret;
    const std::string appKey = "app:" + base + "|" + redirectUri_;
    std::string text;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Retrieve(appKey, text); });
    if (!text.empty()) {
        UltraCanvas::JSONParseResult parsed;
        const JSONValue app = UltraCanvas::JSON::Parse(text, &parsed);
        clientId = app["client_id"].GetString();
        clientSecret = app["client_secret"].GetString();
    }
    if (clientId.empty()) {
        JSONValue app = JSONValue::MakeObject();
        app.Set("client_name", JSONValue("ChatCube"));
        app.Set("redirect_uris", JSONValue(redirectUri_));
        app.Set("scopes", JSONValue("read write"));
        app.Set("website", JSONValue("https://ultraos.eu"));
        const MastodonResponse r = Request("POST", "/api/v1/apps", &app, 20000);
        if (!r.Ok())
            return UltraIMResult::Error(UltraIMResultCode::NetworkError,
                                        r.status == 0 ? "The server " + HostOf(base) + " does not answer: " + r.error
                                                      : "The server did not register ChatCube: " + r.error);
        clientId = r.body["client_id"].GetString();
        clientSecret = r.body["client_secret"].GetString();
        JSONValue keep = JSONValue::MakeObject();
        keep.Set("client_id", JSONValue(clientId));
        keep.Set("client_secret", JSONValue(clientSecret));
        vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Store(appKey, UltraCanvas::JSON::Serialize(keep)); });
    }
    UltraNetOAuth2Config oauth;
    oauth.authorizationEndpoint = base + "/oauth/authorize";
    oauth.tokenEndpoint = base + "/oauth/token";
    oauth.clientId = clientId;
    oauth.clientSecret = clientSecret;
    oauth.secretInBody = true;
    oauth.scopes = {"read", "write"};
    oauth.redirectUri = redirectUri_;
    UltraNetOAuth2Token answer;
    const UltraNetResult r = UltraNet_OAuth2AuthorizeInteractive(
        oauth,
        [this](const std::string& url) {
            // The application opens the consent page in the browser.
            UltraIMEvent event;
            event.type = UltraIMEventType::AuthStepChange;
            event.providerId = providerId_;
            event.authStep = UltraIMAuthStep::Browser;
            event.errorText = url;
            Emit(event);
        },
        answer, timings_.oauthTimeoutMs, [this]() { return cancelSignIn_.load(); });
    if (!r.success || !answer.IsValid())
        return UltraIMResult::Error(UltraIMResultCode::AuthFailed,
                                    r.message.empty() ? "The server did not let ChatCube in" : r.message);
    token = answer.accessToken;
    return UltraIMResult::Ok();
}

UltraIMResult MastodonProvider::StartSession(const std::string& base, const std::string& token) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        base_ = base;
        token_ = token;
    }
    const MastodonResponse r = Request("GET", "/api/v1/accounts/verify_credentials", nullptr, 20000);
    if (!r.Ok()) {
        if (r.status == 401 || r.status == 403)
            return UltraIMResult::Error(UltraIMResultCode::AuthFailed, "The server no longer takes this sign-in: " + r.error);
        return UltraIMResult::Error(UltraIMResultCode::NetworkError, "The server does not answer: " + r.error);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        me_ = AccountFrom(r.body);
        chats_.clear();
        seen_.clear();
        keyOfStatus_.clear();
        byId_.clear();
        clientOfStatus_.clear();
    }
    SaveSession(base, token);
    SetState(UltraIMConnectionState::Connected);
    Poll(true);
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        polling_ = true;
    }
    return UltraIMResult::Ok();
}

void MastodonProvider::SignOut() {
    cancelSignIn_ = true;
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        polling_ = false;
        jobs_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        token_.clear();
        chats_.clear();
    }
    if (state_.load() != UltraIMConnectionState::Disconnected) SetState(UltraIMConnectionState::Disconnected);
}

// ============================================================================
// CHATS
// ============================================================================

MastodonProvider::Account MastodonProvider::AccountFrom(const JSONValue& account) const {
    Account a;
    std::string acct = account["acct"].GetString();
    // Here, "user"; elsewhere "user@server": everywhere one form.
    if (!acct.empty() && acct.find('@') == std::string::npos) {
        std::string base;
        {
            // (Called with mutex_ held or not: base_ is read through a copy.)
            base = base_;
        }
        acct += "@" + HostOf(base);
    }
    a.id = Lower(acct);
    a.acct = acct;
    a.name = account["display_name"].GetString();
    if (a.name.empty()) a.name = account["username"].GetString();
    a.avatarUrl = account["avatar_static"].GetString();
    return a;
}

std::string MastodonProvider::KeyOf(const std::vector<Account>& others) const {
    std::vector<std::string> ids;
    for (const Account& a : others) ids.push_back(a.id);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::string key;
    for (const std::string& id : ids) key += (key.empty() ? "" : " ") + id;
    return key;
}

bool MastodonProvider::MessageFrom(const JSONValue& status, const std::string& key, UltraIMMessage& out) {
    const std::string id = status["id"].GetString();
    if (id.empty()) return false;
    const Account sender = AccountFrom(status["account"]);
    out = UltraIMMessage();
    out.providerId = providerId_;
    out.conversationId = key;
    out.messageId = id;
    auto client = clientOfStatus_.find(id);
    out.clientMessageId = client != clientOfStatus_.end() ? client->second : id;
    out.senderId = sender.id;
    out.senderName = sender.name;
    out.isOwn = sender.id == me_.id;
    out.text = TextOf(status);
    out.timestampMs = ParseTime(status["created_at"].GetString());
    out.isEdited = !status["edited_at"].GetString().empty();
    out.replyTo.messageId = status["in_reply_to_id"].GetString();
    out.deliveryStatus = out.isOwn ? UltraIMDeliveryStatus::Sent : UltraIMDeliveryStatus::Delivered;
    const auto media = status["media_attachments"].GetElements();
    if (!media.empty()) {
        const JSONValue& m = media.front();
        const std::string type = m["type"].GetString();
        out.type = type == "image"                      ? UltraIMMessageType::Image
                   : (type == "video" || type == "gifv") ? UltraIMMessageType::Video
                   : type == "audio"                    ? UltraIMMessageType::Audio
                                                        : UltraIMMessageType::File;
        out.attachment.remoteUrl = m["url"].GetString();
        if (out.attachment.remoteUrl.empty()) out.attachment.remoteUrl = m["remote_url"].GetString();
        out.attachment.fileName = FileNameOf(out.attachment.remoteUrl);
        out.attachment.mimeType = MimeFor(out.attachment.fileName);
        const JSONValue& original = m["meta"]["original"];
        out.attachment.width = static_cast<int>(original["width"].GetInteger(0));
        out.attachment.height = static_cast<int>(original["height"].GetInteger(0));
        out.attachment.durationSeconds = original["duration"].GetNumber(0.0);
        if (media.size() > 1) out.text += (out.text.empty() ? "" : "\n") + std::string("+") + std::to_string(media.size() - 1);
    }
    return true;
}

UltraIMConversation MastodonProvider::ConversationOf(const Chat& chat) const {
    UltraIMConversation c;
    c.conversationId = chat.key;
    c.providerId = providerId_;
    c.isGroup = chat.accounts.size() > 1;
    for (const Account& a : chat.accounts) {
        c.title += (c.title.empty() ? "" : ", ") + a.name;
        c.memberIds.push_back(a.id);
        c.memberNames[a.id] = a.name;
    }
    if (!me_.id.empty()) {
        c.memberIds.insert(c.memberIds.begin(), me_.id);
        c.memberNames[me_.id] = me_.name;
    }
    if (!c.isGroup && !chat.accounts.empty()) c.directContactId = chat.accounts.front().id;
    if (chat.hasLast) {
        c.lastMessageSnippet = SnippetOf(chat.last);
        c.lastMessageTimeMs = chat.last.timestampMs;
    }
    c.unreadCount = chat.unread ? 1 : 0;
    return c;
}

std::string MastodonProvider::MentionsOf(const Chat& chat) const {
    std::string mentions;
    for (const Account& a : chat.accounts) mentions += "@" + a.acct + " ";
    return mentions;
}

void MastodonProvider::Poll(bool first) {
    const MastodonResponse r = Request("GET", "/api/v1/conversations?limit=40", nullptr, 20000);
    if (!r.Ok()) {
        if (r.status == 401) SetState(UltraIMConnectionState::Error, "The server no longer takes this sign-in");
        return;
    }
    std::vector<UltraIMEvent> events;
    std::vector<std::pair<std::string, std::string>> threadsToRead;   // key, previous last status
    std::vector<UltraIMMessage> media;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // The newest of each set of people first, so that one is "the" chat.
        std::vector<JSONValue> list = r.body.GetElements();
        for (const JSONValue& conv : list) {
            std::vector<Account> others;
            for (const JSONValue& a : conv["accounts"].GetElements()) {
                Account account = AccountFrom(a);
                if (account.id != me_.id) others.push_back(account);
            }
            if (others.empty()) continue;           // a note to oneself
            const std::string key = KeyOf(others);
            Chat& chat = chats_[key];
            const bool known = !chat.key.empty();
            chat.key = key;
            chat.accounts = others;
            chat.serverIds.insert(conv["id"].GetString());
            const JSONValue& status = conv["last_status"];
            const std::string statusId = status["id"].GetString();
            bool changed = !known || chat.unread != conv["unread"].GetBoolean(false);
            if (!statusId.empty() && !chat.lastStatusId.empty() && IdLess(statusId, chat.lastStatusId) && !first) {
                // The last one went: it was deleted.
                UltraIMEvent e;
                e.type = UltraIMEventType::MessageDelete;
                e.providerId = providerId_;
                e.conversationId = key;
                e.message.providerId = providerId_;
                e.message.conversationId = key;
                e.message.messageId = chat.lastStatusId;
                auto client = clientOfStatus_.find(chat.lastStatusId);
                e.message.clientMessageId = client != clientOfStatus_.end() ? client->second : chat.lastStatusId;
                events.push_back(e);
                byId_.erase(chat.lastStatusId);
                chat.lastStatusId.clear();
            }
            if (!statusId.empty() && (chat.lastStatusId.empty() || !IdLess(statusId, chat.lastStatusId))) {
                UltraIMMessage m;
                if (MessageFrom(status, key, m)) {
                    const std::string previous = chat.lastStatusId;
                    const std::string edited = status["edited_at"].GetString();
                    if (statusId != chat.lastStatusId || edited != chat.lastEditedAt) changed = true;
                    bool viaThread = false;
                    if (!first && !seen_.count(statusId)) {
                        if (!previous.empty() && previous != m.replyTo.messageId) {
                            // Others came before it since the last look: the
                            // thread brings them all, in order, this one last.
                            threadsToRead.push_back({key, previous});
                            viaThread = true;
                        } else {
                            if (m.replyTo.messageId == previous) m.replyTo = UltraIMReplyContext();
                            UltraIMEvent e;
                            e.type = UltraIMEventType::MessageReceive;
                            e.providerId = providerId_;
                            e.conversationId = key;
                            e.contactId = m.senderId;
                            e.message = m;
                            events.push_back(e);
                            media.push_back(m);
                        }
                    } else if (!first && edited != chat.lastEditedAt && statusId == chat.lastStatusId) {
                        UltraIMEvent e;
                        e.type = UltraIMEventType::MessageUpdate;
                        e.providerId = providerId_;
                        e.conversationId = key;
                        e.message = m;
                        events.push_back(e);
                    }
                    if (!viaThread) seen_.insert(statusId);
                    keyOfStatus_[statusId] = key;
                    byId_[statusId] = m;
                    chat.lastStatusId = statusId;
                    chat.lastEditedAt = edited;
                    chat.last = m;
                    chat.hasLast = true;
                }
            }
            chat.unread = conv["unread"].GetBoolean(false);
            if (changed) {
                UltraIMEvent e;
                e.type = UltraIMEventType::ConversationUpdate;
                e.providerId = providerId_;
                e.conversationId = key;
                e.conversation = ConversationOf(chat);
                events.push_back(e);
            }
        }
    }
    for (const auto& e : events) Emit(e);
    for (const auto& m : media) QueueMedia(m);
    // Statuses that came between two looks: the thread of the newest.
    for (const auto& [key, previous] : threadsToRead) {
        Chat copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = chats_.find(key);
            if (it == chats_.end()) continue;
            copy = it->second;
        }
        std::set<std::string> unseen;
        const std::vector<UltraIMMessage> thread = ThreadOf(copy, &unseen);
        for (const UltraIMMessage& m : thread) {
            if (!unseen.count(m.messageId) || !IdLess(previous, m.messageId)) continue;
            UltraIMEvent e;
            e.type = UltraIMEventType::MessageReceive;
            e.providerId = providerId_;
            e.conversationId = key;
            e.contactId = m.senderId;
            e.message = m;
            Emit(e);
            QueueMedia(m);
        }
    }
}

std::vector<UltraIMMessage> MastodonProvider::ThreadOf(Chat& chat, std::set<std::string>* unseen) {
    std::vector<UltraIMMessage> thread;
    if (chat.lastStatusId.empty()) return thread;
    // The context of a status is the line up to it and what answers it; a
    // reply to an earlier message branches. The root's context is all of it.
    std::string rootId;
    MastodonResponse context;
    for (const std::string& start : {chat.rootId, chat.lastStatusId}) {
        if (start.empty()) continue;
        context = Request("GET", "/api/v1/statuses/" + start + "/context", nullptr, 20000);
        if (context.Ok()) {
            rootId = start;
            break;
        }
    }
    if (rootId.empty()) return thread;
    const auto ancestors = context.body["ancestors"].GetElements();
    if (context.Ok() && !ancestors.empty()) {
        rootId = ancestors.front()["id"].GetString();
        context = Request("GET", "/api/v1/statuses/" + rootId + "/context", nullptr, 20000);
    }
    const MastodonResponse root = Request("GET", "/api/v1/statuses/" + rootId, nullptr, 20000);
    std::vector<JSONValue> statuses;
    if (root.Ok()) statuses.push_back(root.body);
    if (context.Ok()) {
        for (const JSONValue& s : context.body["ancestors"].GetElements()) statuses.push_back(s);
        for (const JSONValue& s : context.body["descendants"].GetElements()) statuses.push_back(s);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::string, UltraIMMessage, bool (*)(const std::string&, const std::string&)> ordered(IdLess);
    for (const JSONValue& s : statuses) {
        if (s["visibility"].GetString() != "direct") continue;       // only the chat's own
        UltraIMMessage m;
        if (!MessageFrom(s, chat.key, m)) continue;
        ordered[m.messageId] = m;
    }
    std::string previous;
    for (auto& [id, m] : ordered) {
        // Every status answers the one before it: a reply only when it
        // answers another.
        if (m.replyTo.messageId == previous || m.replyTo.messageId.empty()) {
            m.replyTo = UltraIMReplyContext();
        } else {
            auto quoted = ordered.find(m.replyTo.messageId);
            if (quoted != ordered.end()) {
                m.replyTo.senderId = quoted->second.senderId;
                m.replyTo.senderName = quoted->second.senderName;
                m.replyTo.textSnippet = quoted->second.text.substr(0, 80);
            }
        }
        previous = id;
        if (unseen && !seen_.count(id)) unseen->insert(id);
        byId_[id] = m;
        keyOfStatus_[id] = chat.key;
        seen_.insert(id);
        thread.push_back(m);
    }
    auto live = chats_.find(chat.key);
    if (live != chats_.end()) {
        live->second.messages = thread;
        if (!thread.empty()) live->second.rootId = thread.front().messageId;
    }
    return thread;
}

void MastodonProvider::QueueMedia(const UltraIMMessage& message) {
    // A picture comes by itself (its file); the rest when asked.
    if (message.type != UltraIMMessageType::Image || message.attachment.remoteUrl.empty()) return;
    Enqueue([this, message]() {
        const std::string path = MediaPath(message.attachment.remoteUrl, message.attachment.fileName);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(path), ec) &&
            !Download(message.attachment.remoteUrl, path))
            return;
        UltraIMMessage updated = message;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = byId_.find(message.messageId);
            if (it != byId_.end()) updated = it->second;
            updated.attachment.localPath = path;
            updated.attachment.isDownloaded = true;
            byId_[message.messageId] = updated;
        }
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageUpdate;
        e.providerId = providerId_;
        e.conversationId = updated.conversationId;
        e.message = updated;
        Emit(e);
    });
}

void MastodonProvider::FetchConversations(UltraIMConversationsCallback onResult) {
    Enqueue([this, onResult]() {
        if (state_.load() != UltraIMConnectionState::Connected) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Mastodon"), {});
            return;
        }
        std::vector<UltraIMConversation> list;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [key, chat] : chats_) list.push_back(ConversationOf(chat));
        }
        if (onResult) onResult(UltraIMResult::Ok(), list);
    });
}

void MastodonProvider::FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                                     UltraIMMessagesCallback onResult) {
    (void)limit;
    Enqueue([this, conversationId, beforeMessageId, onResult]() {
        Chat copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = chats_.find(conversationId);
            if (it == chats_.end()) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation"), {});
                return;
            }
            copy = it->second;
        }
        // The thread comes whole: nothing before it.
        if (!beforeMessageId.empty()) {
            if (onResult) onResult(UltraIMResult::Ok(), {});
            return;
        }
        const std::vector<UltraIMMessage> thread = ThreadOf(copy);
        if (onResult) onResult(UltraIMResult::Ok(), thread);
        for (const UltraIMMessage& m : thread) QueueMedia(m);
    });
}

void MastodonProvider::SearchMessages(const UltraIMMessageSearch& search, UltraIMSearchCallback onResult) {
    if (search.conversationId.empty()) {
        if (onResult)
            onResult(UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "Mastodon searches one conversation at a time"),
                     UltraIMSearchPage());
        return;
    }
    UltraIM_SearchByFetching(
        [this](const std::string& conversationId, const std::string& before, int limit, UltraIMMessagesCallback cb) {
            FetchMessages(conversationId, before, limit, std::move(cb));
        },
        search, std::move(onResult));
}

void MastodonProvider::FetchContacts(UltraIMContactsCallback onResult) {
    std::vector<UltraIMContact> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::set<std::string> seen;
        for (const auto& [key, chat] : chats_) {
            for (const Account& a : chat.accounts) {
                if (!seen.insert(a.id).second) continue;
                UltraIMContact c;
                c.contactId = a.id;
                c.providerId = providerId_;
                c.displayName = a.name;
                out.push_back(c);
            }
        }
    }
    if (onResult) onResult(UltraIMResult::Ok(), out);
}

UltraIMResult MastodonProvider::ResolveAccount(const std::string& address, Account& out) {
    std::string acct = Trim(address);
    if (acct.rfind("http://", 0) == 0 || acct.rfind("https://", 0) == 0) {
        // https://server/@user -> user@server
        const std::string host = HostOf(acct);
        const size_t at = acct.find("/@");
        if (at == std::string::npos) return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Not a profile's address");
        std::string user = acct.substr(at + 2);
        user = user.substr(0, user.find_first_of("/?"));
        acct = user.find('@') == std::string::npos ? user + "@" + host : user;
    }
    while (!acct.empty() && acct[0] == '@') acct.erase(0, 1);
    if (acct.empty()) return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Whom? (@user@server)");
    MastodonResponse r = Request("GET", "/api/v1/accounts/lookup?acct=" + UltraNet_UrlEncode(acct), nullptr, 20000);
    if (!r.Ok() && acct.find('@') != std::string::npos) {
        // Someone on another server this one has not met: it asks theirs.
        const MastodonResponse found =
            Request("GET", "/api/v2/search?type=accounts&resolve=true&limit=1&q=" + UltraNet_UrlEncode("@" + acct), nullptr, 30000);
        const auto accounts = found.body["accounts"].GetElements();
        if (found.Ok() && !accounts.empty()) {
            r.status = 200;
            r.body = accounts.front();
        }
    }
    if (!r.Ok()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "There is no @" + acct + " on Mastodon");
    std::lock_guard<std::mutex> lock(mutex_);
    out = AccountFrom(r.body);
    return UltraIMResult::Ok();
}

void MastodonProvider::AddContact(const std::string& address, UltraIMConversationCallback onResult) {
    CreateGroup(std::string(), {address}, std::move(onResult));
}

void MastodonProvider::CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                                   UltraIMConversationCallback onResult) {
    (void)title;
    Enqueue([this, memberIds, onResult]() {
        if (state_.load() != UltraIMConnectionState::Connected) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Mastodon"), {});
            return;
        }
        std::vector<Account> others;
        for (const std::string& address : memberIds) {
            Account a;
            const UltraIMResult r = ResolveAccount(address, a);
            if (!r) {
                if (onResult) onResult(r, {});
                return;
            }
            if (a.id != me_.id) others.push_back(a);
        }
        if (others.empty()) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "That is you"), {});
            return;
        }
        UltraIMConversation conversation;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const std::string key = KeyOf(others);
            Chat& chat = chats_[key];
            chat.key = key;
            if (chat.accounts.empty()) chat.accounts = others;
            conversation = ConversationOf(chat);
        }
        UltraIMEvent e;
        e.type = UltraIMEventType::ConversationUpdate;
        e.providerId = providerId_;
        e.conversationId = conversation.conversationId;
        e.conversation = conversation;
        Emit(e);
        if (onResult) onResult(UltraIMResult::Ok(), conversation);
    });
}

void MastodonProvider::LeaveConversation(const std::string& conversationId, UltraIMResultCallback onResult) {
    Enqueue([this, conversationId, onResult]() {
        std::set<std::string> serverIds;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = chats_.find(conversationId);
            if (it == chats_.end()) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation"));
                return;
            }
            serverIds = it->second.serverIds;
        }
        for (const std::string& id : serverIds) Request("DELETE", "/api/v1/conversations/" + id, nullptr, 20000);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            chats_.erase(conversationId);
        }
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

UltraIMResult MastodonProvider::SendText(const std::string& conversationId, const std::string& text,
                                         const UltraIMSendOptions& options) {
    return SendStatus(conversationId, text, std::string(), UltraIMMessageType::Text, options);
}

UltraIMResult MastodonProvider::SendImage(const std::string& conversationId, const std::string& localPath,
                                          const std::string& caption, const UltraIMSendOptions& options) {
    return SendStatus(conversationId, caption, localPath, UltraIMMessageType::Image, options);
}

UltraIMResult MastodonProvider::SendVideo(const std::string& conversationId, const std::string& localPath,
                                          const std::string& caption, const UltraIMSendOptions& options) {
    return SendStatus(conversationId, caption, localPath, UltraIMMessageType::Video, options);
}

UltraIMResult MastodonProvider::SendVoiceNote(const std::string& conversationId, const std::string& localPath,
                                              const UltraIMSendOptions& options) {
    return SendStatus(conversationId, std::string(), localPath, UltraIMMessageType::Audio, options);
}

UltraIMResult MastodonProvider::SendFile(const std::string& conversationId, const std::string& localPath,
                                         const UltraIMSendOptions& options) {
    return SendStatus(conversationId, std::string(), localPath, UltraIMMessageType::File, options);
}

UltraIMResult MastodonProvider::SendStatus(const std::string& conversationId, const std::string& text,
                                           const std::string& localPath, UltraIMMessageType type,
                                           const UltraIMSendOptions& options) {
    if (state_.load() != UltraIMConnectionState::Connected)
        return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Mastodon");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!chats_.count(conversationId)) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation");
    }
    std::string mime;
    if (!localPath.empty()) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(localPath), ec))
            return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "There is no file " + localPath);
        mime = MimeFor(localPath);
        if (mime.rfind("image/", 0) != 0 && mime.rfind("video/", 0) != 0 && mime.rfind("audio/", 0) != 0)
            return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing,
                                        "Mastodon takes pictures, videos and sound only - not this kind of file");
    } else if (Trim(text).empty()) {
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Nothing to send");
    }
    const std::string clientId = options.clientMessageId.empty() ? UltraIM_NewClientMessageId() : options.clientMessageId;
    Enqueue([this, conversationId, text, localPath, mime, type, options, clientId]() {
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
        std::string mediaId;
        if (!localPath.empty()) {
            int lastPercent = -1;
            const MastodonResponse up = Upload("/api/v2/media", localPath, mime, [&](int64_t now, int64_t total) {
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
            mediaId = up.body["id"].GetString();
            // A video or sound is processed first: 202, its url null until then.
            const auto start = std::chrono::steady_clock::now();
            JSONValue media = up.body;
            while (media["url"].GetString().empty()) {
                if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timings_.mediaWaitMs))
                    return failed("The server is still processing the file");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                const MastodonResponse again = Request("GET", "/api/v1/media/" + mediaId, nullptr, 20000);
                if (again.status == 206) continue;
                if (!again.Ok()) return failed(again.error);
                media = again.body;
            }
        }
        std::string mentions, lastStatus;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = chats_.find(conversationId);
            if (it == chats_.end()) return failed("No such conversation");
            mentions = MentionsOf(it->second);
            lastStatus = it->second.lastStatusId;
        }
        std::string replyTo = options.replyToMessageId;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // The window knows our own messages by their client id at first.
            for (const auto& [statusId, client] : clientOfStatus_) {
                if (client == replyTo) replyTo = statusId;
            }
        }
        JSONValue body = JSONValue::MakeObject();
        body.Set("status", JSONValue(mentions + text));
        body.Set("visibility", JSONValue("direct"));
        const std::string inReplyTo = !replyTo.empty() ? replyTo : lastStatus;
        if (!inReplyTo.empty()) body.Set("in_reply_to_id", JSONValue(inReplyTo));
        if (!mediaId.empty()) {
            JSONValue ids = JSONValue::MakeArray();
            ids.Append(JSONValue(mediaId));
            body.Set("media_ids", ids);
        }
        body.Set("_idempotency", JSONValue(clientId));
        const MastodonResponse r = Request("POST", "/api/v1/statuses", &body, 60000);
        if (!r.Ok()) return failed(r.error);
        UltraIMMessage sent;
        UltraIMConversation conversation;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const std::string statusId = r.body["id"].GetString();
            clientOfStatus_[statusId] = clientId;
            MessageFrom(r.body, conversationId, sent);
            if (sent.replyTo.messageId == lastStatus && replyTo.empty()) sent.replyTo = UltraIMReplyContext();
            sent.attachment.localPath = localPath;
            sent.attachment.isDownloaded = !localPath.empty();
            seen_.insert(statusId);
            keyOfStatus_[statusId] = conversationId;
            byId_[statusId] = sent;
            Chat& chat = chats_[conversationId];
            chat.lastStatusId = statusId;
            chat.lastEditedAt.clear();
            chat.last = sent;
            chat.hasLast = true;
            conversation = ConversationOf(chat);
        }
        UltraIMEvent delivered;
        delivered.type = UltraIMEventType::MessageDelivered;
        delivered.providerId = providerId_;
        delivered.conversationId = conversationId;
        delivered.message = sent;
        delivered.deliveryStatus = UltraIMDeliveryStatus::Sent;
        Emit(delivered);
        // The row learns the status's id (for an edit, a delete, a reply).
        UltraIMEvent updated;
        updated.type = UltraIMEventType::MessageUpdate;
        updated.providerId = providerId_;
        updated.conversationId = conversationId;
        updated.message = sent;
        Emit(updated);
        UltraIMEvent changed;
        changed.type = UltraIMEventType::ConversationUpdate;
        changed.providerId = providerId_;
        changed.conversationId = conversationId;
        changed.conversation = conversation;
        Emit(changed);
    });
    return UltraIMResult::Ok();
}

void MastodonProvider::DownloadAttachment(const std::string& conversationId, const std::string& messageId,
                                          UltraIMAttachmentCallback onResult) {
    (void)conversationId;
    Enqueue([this, messageId, onResult]() {
        UltraIMMessage m;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = byId_.find(messageId);
            if (it == byId_.end()) {
                for (const auto& [statusId, client] : clientOfStatus_) {
                    if (client == messageId) it = byId_.find(statusId);
                }
            }
            if (it == byId_.end() || it->second.attachment.remoteUrl.empty()) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No attachment"), UltraIMAttachment());
                return;
            }
            m = it->second;
        }
        if (m.attachment.isDownloaded && !m.attachment.localPath.empty()) {
            if (onResult) onResult(UltraIMResult::Ok(), m.attachment);
            return;
        }
        const std::string path = MediaPath(m.attachment.remoteUrl, m.attachment.fileName);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(path), ec) && !Download(m.attachment.remoteUrl, path)) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NetworkError, "Could not fetch the file"), UltraIMAttachment());
            return;
        }
        m.attachment.localPath = path;
        m.attachment.isDownloaded = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            byId_[m.messageId] = m;
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

UltraIMResult MastodonProvider::EditMessage(const std::string& conversationId, const std::string& messageId,
                                            const std::string& newText) {
    std::string statusId = messageId;
    std::string mentions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, client] : clientOfStatus_) {
            if (client == messageId) statusId = id;
        }
        auto it = byId_.find(statusId);
        if (it == byId_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such message");
        if (!it->second.isOwn) return UltraIMResult::Error(UltraIMResultCode::PermissionDenied, "Only its sender can change a message");
        auto chat = chats_.find(conversationId);
        if (chat != chats_.end()) mentions = MentionsOf(chat->second);
    }
    if (Trim(newText).empty())
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "A text cannot be empty - delete it instead");
    Enqueue([this, statusId, mentions, newText, conversationId]() {
        JSONValue body = JSONValue::MakeObject();
        body.Set("status", JSONValue(mentions + newText));
        // An edit names the attachments it keeps: all of them.
        const MastodonResponse current = Request("GET", "/api/v1/statuses/" + statusId, nullptr, 20000);
        const auto attached = current.body["media_attachments"].GetElements();
        if (current.Ok() && !attached.empty()) {
            JSONValue ids = JSONValue::MakeArray();
            for (const JSONValue& a : attached) ids.Append(JSONValue(a["id"].GetString()));
            body.Set("media_ids", ids);
        }
        const MastodonResponse r = Request("PUT", "/api/v1/statuses/" + statusId, &body, 30000);
        if (!r.Ok()) return;
        UltraIMMessage m;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!MessageFrom(r.body, conversationId, m)) return;
            auto old = byId_.find(statusId);
            if (old != byId_.end()) {
                m.replyTo = old->second.replyTo;
                m.attachment.localPath = old->second.attachment.localPath;
                m.attachment.isDownloaded = old->second.attachment.isDownloaded;
            }
            m.isEdited = true;
            byId_[statusId] = m;
            auto chat = chats_.find(conversationId);
            if (chat != chats_.end() && chat->second.lastStatusId == statusId) {
                chat->second.last = m;
                chat->second.lastEditedAt = r.body["edited_at"].GetString();
            }
        }
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageUpdate;
        e.providerId = providerId_;
        e.conversationId = conversationId;
        e.message = m;
        Emit(e);
    });
    return UltraIMResult::Ok();
}

UltraIMDeleteChoices MastodonProvider::GetDeleteChoices(const std::string& conversationId,
                                                        const std::string& messageId) const {
    (void)conversationId;
    UltraIMDeleteChoices choices;
    std::lock_guard<std::mutex> lock(mutex_);
    std::string statusId = messageId;
    for (const auto& [id, client] : clientOfStatus_) {
        if (client == messageId) statusId = id;
    }
    auto it = byId_.find(statusId);
    choices.forEveryone = it != byId_.end() && it->second.isOwn;
    return choices;
}

UltraIMResult MastodonProvider::DeleteMessage(const std::string& conversationId, const std::string& messageId,
                                              UltraIMDeleteScope scope) {
    if (scope == UltraIMDeleteScope::ForMe)
        return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "Mastodon deletes for everyone only");
    std::string statusId = messageId;
    std::string clientId = messageId;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, client] : clientOfStatus_) {
            if (client == messageId || id == messageId) {
                statusId = id;
                clientId = client;
            }
        }
        auto it = byId_.find(statusId);
        if (it == byId_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such message");
        if (!it->second.isOwn) return UltraIMResult::Error(UltraIMResultCode::PermissionDenied, "Only its sender can delete a message");
    }
    Enqueue([this, statusId, clientId, conversationId]() {
        const MastodonResponse r = Request("DELETE", "/api/v1/statuses/" + statusId, nullptr, 30000);
        if (!r.Ok() && r.status != 404) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = byId_.find(statusId);
            auto chat = chats_.find(conversationId);
            if (it != byId_.end() && chat != chats_.end() && chat->second.lastStatusId == statusId) {
                // The chat's last is the one before it now (what the next look says).
                chat->second.lastStatusId = r.body["in_reply_to_id"].GetString();
                chat->second.hasLast = false;
                for (auto m = chat->second.messages.rbegin(); m != chat->second.messages.rend(); ++m) {
                    if (m->messageId != statusId) {
                        chat->second.lastStatusId = m->messageId;
                        chat->second.last = *m;
                        chat->second.hasLast = true;
                        break;
                    }
                }
            }
            byId_.erase(statusId);
        }
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageDelete;
        e.providerId = providerId_;
        e.conversationId = conversationId;
        e.message.providerId = providerId_;
        e.message.conversationId = conversationId;
        e.message.messageId = statusId;
        e.message.clientMessageId = clientId;
        Emit(e);
    });
    return UltraIMResult::Ok();
}

UltraIMResult MastodonProvider::MarkRead(const std::string& conversationId, const std::string& upToMessageId) {
    (void)upToMessageId;
    std::set<std::string> serverIds;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = chats_.find(conversationId);
        if (it == chats_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation");
        if (!it->second.unread) return UltraIMResult::Ok();
        it->second.unread = false;
        serverIds = it->second.serverIds;
    }
    Enqueue([this, serverIds]() {
        for (const std::string& id : serverIds) Request("POST", "/api/v1/conversations/" + id + "/read", nullptr, 20000);
    });
    return UltraIMResult::Ok();
}

} // namespace UltraIMMastodon
