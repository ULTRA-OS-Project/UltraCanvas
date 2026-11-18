// Plugins/UltraIM/mattermost/MattermostProvider.cpp
// Mattermost - the channels and direct messages of a Mattermost server as UltraIM conversations
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "MattermostProvider.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCrypt/UltraCryptCore.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetUrl.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace UltraIMMattermost {

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

// "chat.example.org", "https://chat.example.org/" -> "https://chat.example.org".
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

// Mattermost's ids: 26 lower-case letters and digits.
bool LooksLikeId(const std::string& s) {
    if (s.size() != 26) return false;
    for (char c : s) {
        if (!std::islower(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

// The server's answer to a password without the multi-factor code, or with
// a wrong one (older servers say the first, newer the second for both).
bool IsMfaError(const std::string& id) {
    return id == "mfa.validate_token.authenticate.app_error" || id == "api.user.check_user_mfa.bad_code.app_error" ||
           id.find(".mfa.") != std::string::npos || id.rfind("mfa.", 0) == 0;
}

bool IsSystemPost(const JSONValue& post) {
    return post["type"].GetString().rfind("system_", 0) == 0;
}

std::string LowerExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || path.find_first_of("/\\?", dot) != std::string::npos) return std::string();
    return Lower(path.substr(dot));
}

const std::pair<const char*, const char*> kMimeTypes[] = {
    {".jpg", "image/jpeg"},  {".jpeg", "image/jpeg"}, {".png", "image/png"},    {".gif", "image/gif"},
    {".webp", "image/webp"}, {".heic", "image/heic"}, {".avif", "image/avif"}, {".bmp", "image/bmp"},
    {".mp4", "video/mp4"},   {".m4v", "video/mp4"},   {".mov", "video/quicktime"}, {".webm", "video/webm"},
    {".mp3", "audio/mpeg"},  {".ogg", "audio/ogg"},   {".oga", "audio/ogg"},    {".opus", "audio/ogg"},
    {".wav", "audio/wav"},   {".m4a", "audio/mp4"},   {".flac", "audio/flac"},  {".aac", "audio/aac"},
    {".pdf", "application/pdf"}, {".txt", "text/plain"}, {".zip", "application/zip"},
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

// What a list shows of a conversation: when any of it changed, it is told again.
bool SameListing(const UltraIMConversation& a, const UltraIMConversation& b) {
    return a.title == b.title && a.memberIds == b.memberIds && a.lastMessageSnippet == b.lastMessageSnippet &&
           a.lastMessageTimeMs == b.lastMessageTimeMs && a.unreadCount == b.unreadCount && a.memberCount == b.memberCount;
}

} // namespace

// ============================================================================
// LIFETIME
// ============================================================================

MattermostProvider::MattermostProvider(std::string providerId) : providerId_(std::move(providerId)) {}

MattermostProvider::~MattermostProvider() {
    Shutdown();
}

UltraIMCapabilityFlags MattermostProvider::GetCapabilities() const {
    using F = UltraIMCapabilityFlags;
    return F::TextMessages | F::GroupChats | F::ImageMessages | F::FileTransfer | F::ReplyToMessage |
           F::EditMessage | F::DeleteMessage | F::OfflineMessages | F::MultiDevice | F::AddContact |
           F::CreateGroup | F::SearchMessages | F::LeaveConversation | F::VoiceNotes;
}

UltraIMResult MattermostProvider::Initialize(const UltraIMConfig& config, UltraIMEventSink sink) {
    config_ = config;
    sink_ = std::move(sink);
    if (config.dataDirectory.empty())
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "The Mattermost provider needs a data directory (UltraIMConfig::dataDirectory)");
    std::error_code ec;
    const std::filesystem::path dir = UltraCanvas::PathFromUtf8(config.dataDirectory) / UltraCanvas::PathFromUtf8(providerId_);
    std::filesystem::create_directories(dir, ec);
    vault_ = UltraVault::DeviceKeyVault(UltraCanvas::PathToUtf8(dir), {"mattermost.vault", config.appId + "." + providerId_ + "."});
    vaultReady_ = true;
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        stopping_ = false;
    }
    if (!worker_.joinable()) worker_ = std::thread([this]() { WorkLoop(); });
    return UltraIMResult::Ok();
}

void MattermostProvider::Shutdown() {
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

void MattermostProvider::Enqueue(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        if (stopping_) return;
        jobs_.push_back(std::move(job));
    }
    workWake_.notify_one();
}

void MattermostProvider::WorkLoop() {
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

void MattermostProvider::Emit(const UltraIMEvent& event) {
    if (sink_) sink_(event);
}

void MattermostProvider::SetState(UltraIMConnectionState state, const std::string& error) {
    if (state_.exchange(state) == state && error.empty()) return;
    UltraIMEvent event;
    event.type = UltraIMEventType::ConnectionStateChange;
    event.providerId = providerId_;
    event.connectionState = state;
    event.errorText = error;
    Emit(event);
}

void MattermostProvider::FetchAccountProfile(UltraIMAccountProfileCallback onResult) {
    UltraIMAccountProfile profile;
    UltraIMResult result = UltraIMResult::Ok();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (me_.id.empty()) {
            result = UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Mattermost");
        } else {
            // "Erin Smith (@erin)": the name others see, and the one to type.
            profile.displayName = me_.name == me_.username || me_.username.empty()
                                      ? me_.name : me_.name + " (@" + me_.username + ")";
        }
    }
    if (onResult) onResult(result, profile);
}

std::string MattermostProvider::GetLocalUserId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return me_.id;
}

std::string MattermostProvider::GetServerUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return base_;
}

UltraIMAuthStep MattermostProvider::GetAuthStep() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return authStep_;
}

std::string MattermostProvider::GetAuthHint() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return authHint_;
}

// ============================================================================
// HTTP
// ============================================================================

MattermostResponse MattermostProvider::Request(const std::string& method, const std::string& path, const JSONValue* body,
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
    request.headers.Set("X-Requested-With", "XMLHttpRequest");
    request.options.timeoutMs = timeoutMs;
    if (body) {
        const std::string json = UltraCanvas::JSON::Serialize(*body);
        request.body.assign(json.begin(), json.end());
        request.headers.Set("Content-Type", "application/json");
    }
    UltraNetResponse raw;
    UltraNet_HttpRequest(request, raw);
    MattermostResponse r;
    r.status = raw.IsComplete() ? raw.statusCode : 0;
    r.token = raw.headers.Get("Token");
    if (!raw.body.empty()) {
        UltraCanvas::JSONParseResult parsed;
        r.body = UltraCanvas::JSON::Parse(raw.GetBodyAsString(), &parsed);
    }
    if (!r.Ok()) {
        r.error = r.body["message"].GetString();
        r.errorId = r.body["id"].GetString();
        if (r.error.empty()) r.error = !raw.transferError.empty() ? raw.transferError : "HTTP " + std::to_string(raw.statusCode);
    }
    return r;
}

MattermostResponse MattermostProvider::Upload(const std::string& channelId, const std::string& localPath,
                                              const std::string& mimeType,
                                              const std::function<void(int64_t, int64_t)>& onProgress) {
    MattermostResponse r;
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(localPath, bytes)) {
        r.error = "There is no file " + localPath;
        return r;
    }
    // multipart/form-data: the channel first (the server reads it before the
    // file), then the one part "files".
    const std::string boundary = "UltraIM" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string name = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(localPath).filename());
    std::string quotedName;
    for (char c : name) quotedName += (c == '"' || c == '\r' || c == '\n') ? '_' : c;
    const std::string head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"channel_id\"\r\n\r\n" + channelId +
                             "\r\n--" + boundary + "\r\nContent-Disposition: form-data; name=\"files\"; filename=\"" +
                             quotedName + "\"\r\nContent-Type: " + mimeType + "\r\n\r\n";
    const std::string tail = "\r\n--" + boundary + "--\r\n";
    UltraNetHttpRequest request;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        request.url = base_ + "/api/v4/files";
        request.headers.Set("Authorization", "Bearer " + token_);
    }
    request.method = UltraNetHttpMethod::Post;
    request.headers.Set("Accept", "application/json");
    request.headers.Set("X-Requested-With", "XMLHttpRequest");
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
        r.error = r.body["message"].GetString();
        r.errorId = r.body["id"].GetString();
        if (r.error.empty()) r.error = !raw.transferError.empty() ? raw.transferError : "HTTP " + std::to_string(raw.statusCode);
    }
    return r;
}

bool MattermostProvider::Download(const std::string& url, const std::string& toPath) {
    if (url.empty()) return false;
    UltraNetHttpOptions options;
    options.followRedirects = true;
    {
        // The files are the server's own: they ask for the session too.
        std::lock_guard<std::mutex> lock(mutex_);
        if (!token_.empty()) options.headers.Set("Authorization", "Bearer " + token_);
    }
    options.headers.Set("X-Requested-With", "XMLHttpRequest");
    const UltraNetResult r = UltraNet_HttpDownloadFile(url, toPath, options);
    std::error_code ec;
    if (!r.success) std::filesystem::remove(UltraCanvas::PathFromUtf8(toPath), ec);
    return r.success && std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(toPath), ec);
}

std::string MattermostProvider::MediaPath(const std::string& url, const std::string& fileName) const {
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

bool MattermostProvider::SaveSession(const std::string& base, const std::string& token) {
    if (!vaultReady_) return false;
    JSONValue session = JSONValue::MakeObject();
    session.Set("url", JSONValue(base));
    session.Set("token", JSONValue(token));
    bool stored = false;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { stored = v.Store("session", UltraCanvas::JSON::Serialize(session)); });
    return stored;
}

bool MattermostProvider::LoadSession(std::string& base, std::string& token) const {
    if (!vaultReady_) return false;
    std::string text;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Retrieve("session", text); });
    if (text.empty()) return false;
    UltraCanvas::JSONParseResult parsed;
    const JSONValue session = UltraCanvas::JSON::Parse(text, &parsed);
    base = session["url"].GetString();
    token = session["token"].GetString();
    return !base.empty() && !token.empty();
}

void MattermostProvider::ForgetSession() {
    if (!vaultReady_) return;
    vault_.Use([](UltraVault::DeviceKeyVault& v) { v.Remove("session"); });
}

bool MattermostProvider::HasStoredSession() const {
    std::string base, token;
    return LoadSession(base, token);
}

void MattermostProvider::SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) {
    if (!vaultReady_) {
        if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotInitialized, "Initialize first"));
        return;
    }
    SetState(UltraIMConnectionState::Connecting);
    Enqueue([this, credentials, onResult]() { SignInOnWorker(credentials, onResult); });
}

void MattermostProvider::AskForCode(const std::string& hint, const UltraIMResultCallback& onResult) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        authStep_ = UltraIMAuthStep::Code;
        authHint_ = hint;
    }
    SetState(UltraIMConnectionState::Disconnected);
    // The answer first, then the event: an application that answers the
    // event with the next SignIn has its answer land on that one.
    if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NeedsInput, hint));
    UltraIMEvent event;
    event.type = UltraIMEventType::AuthStepChange;
    event.providerId = providerId_;
    event.authStep = UltraIMAuthStep::Code;
    event.errorText = hint;
    Emit(event);
}

void MattermostProvider::SignInOnWorker(UltraIMCredentials credentials, UltraIMResultCallback onResult) {
    const auto fail = [this, &onResult](const UltraIMResult& r) {
        SetState(UltraIMConnectionState::Error, r.message);
        if (onResult) onResult(r);
    };
    std::string storedBase, storedToken;
    LoadSession(storedBase, storedToken);
    std::string pendingBase, pendingLogin, pendingPassword;
    bool codeAwaited = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingBase = pendingBase_;
        pendingLogin = pendingLogin_;
        pendingPassword = pendingPassword_;
        codeAwaited = authStep_ == UltraIMAuthStep::Code;
    }
    const std::string code = Trim(credentials.otpCode);
    std::string base = NormalizeBase(credentials.server);
    std::string login = Trim(credentials.username);
    std::string password = credentials.password;
    if (!code.empty() && codeAwaited) {
        // The code step: what was given before stands for what is not given now.
        if (base.empty()) base = pendingBase;
        if (login.empty()) login = pendingLogin;
        if (password.empty()) password = pendingPassword;
    }
    if (base.empty()) base = storedBase;
    if (base.empty())
        return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Which Mattermost server? (chat.example.org, ...)"));

    if (!credentials.accessToken.empty()) {
        const UltraIMResult r = StartSession(base, credentials.accessToken);
        if (!r) return fail(r);
        if (onResult) onResult(r);
        return;
    }
    if (!login.empty() && !password.empty()) {
        std::string token;
        const UltraIMResult r = PasswordSignIn(base, login, password, code, token);
        if (r.code == UltraIMResultCode::NeedsInput) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pendingBase_ = base;
                pendingLogin_ = login;
                pendingPassword_ = password;
            }
            return AskForCode(r.message, onResult);
        }
        if (!r) {
            if (!code.empty() && r.code == UltraIMResultCode::AuthFailed && codeAwaited) {
                // A wrong code: still the code step, the rest remembered.
                SetState(UltraIMConnectionState::Disconnected);
                if (onResult) onResult(r);
                return;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                authStep_ = UltraIMAuthStep::Idle;
                authHint_.clear();
                pendingPassword_.clear();
            }
            return fail(r);
        }
        const UltraIMResult started = StartSession(base, token);
        if (!started) return fail(started);
        if (onResult) onResult(started);
        return;
    }
    if (!storedToken.empty() && base == storedBase) {
        const UltraIMResult r = StartSession(base, storedToken);
        if (!r) {
            // A token the server no longer takes: forgotten, so the next
            // sign-in asks for the password.
            if (r.code == UltraIMResultCode::AuthFailed) ForgetSession();
            return fail(r);
        }
        if (onResult) onResult(r);
        return;
    }
    if (!code.empty())
        return fail(UltraIMResult::Error(UltraIMResultCode::InvalidState,
                                         "Sign in with your username and password first; the code comes after"));
    fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                              "Sign in with your username (or e-mail address) and password, or a personal access token"));
}

UltraIMResult MattermostProvider::PasswordSignIn(const std::string& base, const std::string& login,
                                                 const std::string& password, const std::string& code, std::string& token) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        base_ = base;
        token_.clear();
    }
    JSONValue body = JSONValue::MakeObject();
    body.Set("login_id", JSONValue(login));
    body.Set("password", JSONValue(password));
    if (!code.empty()) body.Set("token", JSONValue(code));
    const MattermostResponse r = Request("POST", "/api/v4/users/login", &body, 30000);
    if (r.Ok()) {
        token = r.token;
        if (token.empty())
            return UltraIMResult::Error(UltraIMResultCode::AuthFailed, "The server signed in but sent no session token");
        return UltraIMResult::Ok();
    }
    if (r.status == 0)
        return UltraIMResult::Error(UltraIMResultCode::NetworkError,
                                    "The server " + HostOf(base) + " does not answer: " + r.error);
    if (IsMfaError(r.errorId)) {
        if (code.empty())
            return UltraIMResult::Error(UltraIMResultCode::NeedsInput,
                                        "Enter the code from your authenticator app (multi-factor authentication)");
        return UltraIMResult::Error(UltraIMResultCode::AuthFailed, "The code was not accepted - enter the current one");
    }
    if (r.status == 401 || r.status == 403 || r.status == 400)
        return UltraIMResult::Error(UltraIMResultCode::AuthFailed,
                                    r.error.empty() ? "The username or the password is not right" : r.error);
    if (r.status == 429) return UltraIMResult::Error(UltraIMResultCode::RateLimited, r.error);
    return UltraIMResult::Error(UltraIMResultCode::NetworkError, "The server could not sign in: " + r.error);
}

UltraIMResult MattermostProvider::StartSession(const std::string& base, const std::string& token) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        base_ = base;
        token_ = token;
    }
    const MattermostResponse r = Request("GET", "/api/v4/users/me", nullptr, 20000);
    if (!r.Ok()) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            token_.clear();
        }
        if (r.status == 401 || r.status == 403)
            return UltraIMResult::Error(UltraIMResultCode::AuthFailed, "The server does not take this sign-in: " + r.error);
        return UltraIMResult::Error(UltraIMResultCode::NetworkError, "The server does not answer: " + r.error);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        me_.id = r.body["id"].GetString();
        me_.username = r.body["username"].GetString();
        users_.clear();
        teams_.clear();
        chats_.clear();
        hidden_.clear();
        files_.clear();
        known_.clear();
        deleted_.clear();
        clientOfPost_.clear();
        byId_.clear();
        open_.clear();
        clock_ = 0;
        authStep_ = UltraIMAuthStep::Idle;
        authHint_.clear();
        pendingBase_.clear();
        pendingLogin_.clear();
        pendingPassword_.clear();
    }
    EnsureUsers({me_.id});
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = users_.find(me_.id);
        if (it != users_.end()) me_ = it->second;
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

void MattermostProvider::SessionEnded() {
    ForgetSession();
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        polling_ = false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        token_.clear();
    }
    SetState(UltraIMConnectionState::Error, "The Mattermost session ended - sign in again");
    UltraIMEvent event;
    event.type = UltraIMEventType::AuthRequire;
    event.providerId = providerId_;
    Emit(event);
}

void MattermostProvider::SignOut() {
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        polling_ = false;
        jobs_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        token_.clear();
        chats_.clear();
        authStep_ = UltraIMAuthStep::Idle;
        authHint_.clear();
        pendingPassword_.clear();
    }
    if (state_.load() != UltraIMConnectionState::Disconnected) SetState(UltraIMConnectionState::Disconnected);
}

// ============================================================================
// CHANNELS
// ============================================================================

void MattermostProvider::EnsureUsers(const std::set<std::string>& ids) {
    JSONValue missing = JSONValue::MakeArray();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::string& id : ids) {
            if (!id.empty() && !users_.count(id)) missing.Append(JSONValue(id));
        }
    }
    if (missing.GetSize() == 0) return;
    const MattermostResponse r = Request("POST", "/api/v4/users/ids", &missing, 20000);
    if (!r.Ok()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const JSONValue& u : r.body.GetElements()) {
        User user;
        user.id = u["id"].GetString();
        user.username = u["username"].GetString();
        user.name = Trim(u["first_name"].GetString() + " " + u["last_name"].GetString());
        if (user.name.empty()) user.name = Trim(u["nickname"].GetString());
        if (user.name.empty()) user.name = user.username;
        if (!user.id.empty()) users_[user.id] = user;
    }
}

std::vector<JSONValue> MattermostProvider::PostsOf(const JSONValue& list) const {
    std::vector<JSONValue> posts;
    const JSONValue& byId = list["posts"];
    std::set<std::string> taken;
    for (const JSONValue& id : list["order"].GetElements()) {
        const JSONValue* post = byId.Find(id.GetString());
        if (post && taken.insert(id.GetString()).second) posts.push_back(*post);
    }
    // ("since" answers may leave a post out of order: the map is all of it.)
    for (const auto& [id, post] : byId.GetMembers()) {
        if (taken.insert(id).second) posts.push_back(post);
    }
    std::sort(posts.begin(), posts.end(), [](const JSONValue& a, const JSONValue& b) {
        const int64_t ta = a["create_at"].GetInteger(0), tb = b["create_at"].GetInteger(0);
        if (ta != tb) return ta < tb;
        return a["id"].GetString() < b["id"].GetString();
    });
    return posts;
}

void MattermostProvider::Prepare(const std::vector<JSONValue>& posts) {
    std::set<std::string> userIds, roots, inBatch;
    std::vector<std::string> fileIds;
    for (const JSONValue& p : posts) inBatch.insert(p["id"].GetString());
    for (const JSONValue& p : posts) {
        userIds.insert(p["user_id"].GetString());
        const std::string root = p["root_id"].GetString();
        if (!root.empty() && !inBatch.count(root)) roots.insert(root);
        const auto& ids = p["file_ids"].GetElements();
        if (p["metadata"]["files"].GetElements().empty() && !ids.empty()) fileIds.push_back(ids.front().GetString());
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = roots.begin(); it != roots.end();) it = byId_.count(*it) ? roots.erase(it) : std::next(it);
        fileIds.erase(std::remove_if(fileIds.begin(), fileIds.end(), [this](const std::string& id) { return files_.count(id) > 0; }),
                      fileIds.end());
    }
    // A reply's thread started before this page: its first post, for the quote.
    std::vector<JSONValue> rootPosts;
    for (const std::string& root : roots) {
        const MattermostResponse r = Request("GET", "/api/v4/posts/" + root, nullptr, 20000);
        if (!r.Ok()) continue;
        rootPosts.push_back(r.body);
        userIds.insert(r.body["user_id"].GetString());
    }
    EnsureUsers(userIds);
    for (const std::string& id : fileIds) {
        const MattermostResponse r = Request("GET", "/api/v4/files/" + id + "/info", nullptr, 20000);
        if (!r.Ok()) continue;
        std::lock_guard<std::mutex> lock(mutex_);
        files_[id] = r.body;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (const JSONValue& p : rootPosts) {
        UltraIMMessage m;
        if (MessageFrom(p, m) && !byId_.count(m.messageId)) byId_[m.messageId] = m;
    }
}

bool MattermostProvider::MessageFrom(const JSONValue& post, UltraIMMessage& out) const {
    const std::string id = post["id"].GetString();
    if (id.empty()) return false;
    out = UltraIMMessage();
    out.providerId = providerId_;
    out.conversationId = post["channel_id"].GetString();
    out.messageId = id;
    auto client = clientOfPost_.find(id);
    out.clientMessageId = client != clientOfPost_.end() ? client->second : id;
    out.senderId = post["user_id"].GetString();
    auto user = users_.find(out.senderId);
    out.senderName = user != users_.end() ? user->second.name : post["props"]["override_username"].GetString();
    if (out.senderName.empty()) out.senderName = out.senderId;
    out.isOwn = out.senderId == me_.id;
    out.text = post["message"].GetString();     // markdown, shown as it is
    out.timestampMs = post["create_at"].GetInteger(0);
    out.isEdited = post["edit_at"].GetInteger(0) > 0;
    out.replyTo.messageId = post["root_id"].GetString();
    out.deliveryStatus = out.isOwn ? UltraIMDeliveryStatus::Sent : UltraIMDeliveryStatus::Delivered;
    const auto& metaFiles = post["metadata"]["files"].GetElements();
    const auto& fileIds = post["file_ids"].GetElements();
    const size_t count = std::max(metaFiles.size(), fileIds.size());
    if (count > 0) {
        const JSONValue* info = nullptr;
        std::string fileId;
        if (!metaFiles.empty()) {
            info = &metaFiles.front();
            fileId = info->Get("id").GetString();
        } else {
            fileId = fileIds.front().GetString();
            auto f = files_.find(fileId);
            if (f != files_.end()) info = &f->second;
        }
        out.attachment.remoteUrl = base_ + "/api/v4/files/" + fileId;
        if (info) {
            out.attachment.fileName = info->Get("name").GetString();
            out.attachment.mimeType = info->Get("mime_type").GetString();
            out.attachment.fileSizeBytes = info->Get("size").GetInteger(0);
            out.attachment.width = static_cast<int>(info->Get("width").GetInteger(0));
            out.attachment.height = static_cast<int>(info->Get("height").GetInteger(0));
        }
        if (out.attachment.fileName.empty()) out.attachment.fileName = fileId;
        if (out.attachment.mimeType.empty()) out.attachment.mimeType = MimeFor(out.attachment.fileName);
        out.type = TypeForMime(out.attachment.mimeType);
        if (count > 1) out.text += (out.text.empty() ? "" : "\n") + std::string("+") + std::to_string(count - 1);
    }
    return true;
}

void MattermostProvider::FillReply(UltraIMMessage& m) const {
    if (m.replyTo.messageId.empty()) return;
    auto quoted = byId_.find(m.replyTo.messageId);
    if (quoted == byId_.end()) return;
    m.replyTo.senderId = quoted->second.senderId;
    m.replyTo.senderName = quoted->second.senderName;
    m.replyTo.textSnippet = quoted->second.type == UltraIMMessageType::Text ? quoted->second.text.substr(0, 80)
                                                                             : SnippetOf(quoted->second);
}

UltraIMConversation MattermostProvider::ConversationOf(const Chat& chat) const {
    UltraIMConversation c;
    c.conversationId = chat.id;
    c.providerId = providerId_;
    c.isGroup = chat.type != 'D';
    c.memberIds = chat.memberIds;
    c.memberCount = chat.memberCount;
    const auto nameOf = [this](const std::string& id) {
        auto u = users_.find(id);
        return u != users_.end() ? u->second.name : id;
    };
    for (const std::string& id : chat.memberIds) c.memberNames[id] = nameOf(id);
    if (chat.type == 'D') {
        c.directContactId = chat.otherId;
        c.title = nameOf(chat.otherId);
    } else if (chat.type == 'G') {
        for (const std::string& id : chat.memberIds) {
            if (id != me_.id) c.title += (c.title.empty() ? "" : ", ") + nameOf(id);
        }
    } else {
        c.title = chat.displayName + (chat.teamName.empty() ? "" : " (" + chat.teamName + ")");
        c.ownerId = chat.creatorId;
    }
    if (chat.hasLast) {
        c.lastMessageSnippet = SnippetOf(chat.last);
        c.lastMessageTimeMs = chat.last.timestampMs;
    } else {
        c.lastMessageTimeMs = chat.lastPostAt;
    }
    c.unreadCount = static_cast<int>(std::max<int64_t>(0, chat.totalMsgCount - chat.msgCount));
    return c;
}

void MattermostProvider::LoadMembers(Chat& chat) {
    constexpr int kPage = 200;
    const MattermostResponse r =
        Request("GET", "/api/v4/channels/" + chat.id + "/members?page=0&per_page=" + std::to_string(kPage), nullptr, 20000);
    if (!r.Ok()) return;
    std::vector<std::string> ids;
    std::set<std::string> wanted;
    for (const JSONValue& m : r.body.GetElements()) {
        ids.push_back(m["user_id"].GetString());
        wanted.insert(ids.back());
    }
    EnsureUsers(wanted);
    std::string me;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        me = me_.id;
    }
    // The local user first, as every provider lists it.
    std::stable_partition(ids.begin(), ids.end(), [&me](const std::string& id) { return id == me; });
    chat.memberIds = ids;
    chat.memberCount = 0;
    if (static_cast<int>(ids.size()) >= kPage) {
        const MattermostResponse stats = Request("GET", "/api/v4/channels/" + chat.id + "/stats", nullptr, 20000);
        if (stats.Ok()) chat.memberCount = static_cast<int>(stats.body["member_count"].GetInteger(0));
    }
}

bool MattermostProvider::AddChannel(const JSONValue& channel, int64_t msgCount, int64_t receiveAfter,
                                    std::vector<UltraIMEvent>& events, std::vector<UltraIMMessage>& media) {
    Chat chat;
    chat.id = channel["id"].GetString();
    if (chat.id.empty()) return false;
    const std::string type = channel["type"].GetString();
    chat.type = type.empty() ? 'O' : type[0];
    chat.displayName = channel["display_name"].GetString();
    chat.creatorId = channel["creator_id"].GetString();
    chat.lastPostAt = channel["last_post_at"].GetInteger(0);
    chat.lastRootPostAt = channel["last_root_post_at"].GetInteger(0);
    chat.totalMsgCount = channel["total_msg_count"].GetInteger(0);
    chat.msgCount = msgCount;
    std::string me;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        me = me_.id;
        auto team = teams_.find(channel["team_id"].GetString());
        if (team != teams_.end()) chat.teamName = team->second;
    }
    if (chat.type == 'D') {
        // A direct channel's name is its two people's ids: "<a>__<b>".
        const std::string name = channel["name"].GetString();
        const size_t cut = name.find("__");
        const std::string a = cut == std::string::npos ? name : name.substr(0, cut);
        const std::string b = cut == std::string::npos ? std::string() : name.substr(cut + 2);
        chat.otherId = a == me ? (b.empty() ? a : b) : a;
        chat.memberIds = {me};
        if (chat.otherId != me) chat.memberIds.push_back(chat.otherId);
        EnsureUsers({chat.otherId});
    } else {
        LoadMembers(chat);
    }
    std::vector<JSONValue> posts;
    if (chat.lastPostAt > 0) {
        const int perPage = receiveAfter >= 0 ? 30 : 5;
        const MattermostResponse r =
            Request("GET", "/api/v4/channels/" + chat.id + "/posts?page=0&per_page=" + std::to_string(perPage), nullptr, 20000);
        if (r.Ok()) posts = PostsOf(r.body);
        Prepare(posts);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    chat.sinceMs = chat.lastPostAt;
    for (const JSONValue& p : posts) {
        const std::string id = p["id"].GetString();
        const int64_t update = p["update_at"].GetInteger(0);
        chat.sinceMs = std::max(chat.sinceMs, update);
        clock_ = std::max(clock_, update);
        known_[id] = update;
        if (IsSystemPost(p) || p["delete_at"].GetInteger(0) > 0) continue;
        UltraIMMessage m;
        if (!MessageFrom(p, m)) continue;
        FillReply(m);
        byId_[id] = m;
        if (receiveAfter >= 0 && m.timestampMs > receiveAfter && !sentHere_.count(p["pending_post_id"].GetString())) {
            UltraIMEvent e;
            e.type = UltraIMEventType::MessageReceive;
            e.providerId = providerId_;
            e.conversationId = chat.id;
            e.contactId = m.senderId;
            e.message = m;
            events.push_back(e);
            media.push_back(m);
        }
        chat.last = m;
        chat.hasLast = true;
    }
    clock_ = std::max(clock_, chat.lastPostAt);
    Chat& stored = chats_[chat.id] = chat;
    UltraIMEvent e;
    e.type = UltraIMEventType::ConversationUpdate;
    e.providerId = providerId_;
    e.conversationId = stored.id;
    e.conversation = ConversationOf(stored);
    events.push_back(e);
    return true;
}

void MattermostProvider::ReadChanges(const std::string& channelId, std::vector<UltraIMEvent>& events,
                                     std::vector<UltraIMMessage>& media) {
    int64_t since = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = chats_.find(channelId);
        if (it == chats_.end()) return;
        since = it->second.sinceMs;
    }
    // One before the newest change read: posts of the same millisecond are
    // not missed (what was told already is known by its update_at).
    const MattermostResponse r = Request(
        "GET", "/api/v4/channels/" + channelId + "/posts?since=" + std::to_string(std::max<int64_t>(1, since - 1)), nullptr, 20000);
    if (!r.Ok()) return;
    const std::vector<JSONValue> posts = PostsOf(r.body);
    if (posts.empty()) return;
    Prepare(posts);
    bool lastGone = false, strangers = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = chats_.find(channelId);
        if (it == chats_.end()) return;
        Chat& chat = it->second;
        for (const JSONValue& p : posts) {
            const std::string id = p["id"].GetString();
            const int64_t update = p["update_at"].GetInteger(0);
            const int64_t created = p["create_at"].GetInteger(0);
            chat.sinceMs = std::max(chat.sinceMs, update);
            clock_ = std::max(clock_, update);
            if (IsSystemPost(p)) {
                known_[id] = update;
                continue;
            }
            if (p["delete_at"].GetInteger(0) > 0) {
                // Told unless it came and went between two looks.
                if ((known_.count(id) || created < since) && deleted_.insert(id).second) {
                    UltraIMEvent e;
                    e.type = UltraIMEventType::MessageDelete;
                    e.providerId = providerId_;
                    e.conversationId = channelId;
                    e.message.providerId = providerId_;
                    e.message.conversationId = channelId;
                    e.message.messageId = id;
                    auto client = clientOfPost_.find(id);
                    e.message.clientMessageId = client != clientOfPost_.end() ? client->second : id;
                    events.push_back(e);
                }
                known_.erase(id);
                byId_.erase(id);
                if (chat.hasLast && chat.last.messageId == id) lastGone = true;
                continue;
            }
            auto k = known_.find(id);
            UltraIMMessage m;
            if (!MessageFrom(p, m)) continue;
            FillReply(m);
            if (chat.type != 'D' &&
                std::find(chat.memberIds.begin(), chat.memberIds.end(), m.senderId) == chat.memberIds.end())
                strangers = true;
            if (k != known_.end() && k->second == update) {
                // Told already: only the list's line may follow it.
            } else if (k == known_.end()) {
                known_[id] = update;
                if (sentHere_.count(p["pending_post_id"].GetString())) {
                    // Ours, sent from here: told when it was sent.
                } else if (created >= since) {
                    UltraIMEvent e;
                    e.type = UltraIMEventType::MessageReceive;
                    e.providerId = providerId_;
                    e.conversationId = channelId;
                    e.contactId = m.senderId;
                    e.message = m;
                    events.push_back(e);
                    media.push_back(m);
                } else if (m.isEdited) {
                    // An older post, edited: the window may have it.
                    UltraIMEvent e;
                    e.type = UltraIMEventType::MessageUpdate;
                    e.providerId = providerId_;
                    e.conversationId = channelId;
                    e.message = m;
                    events.push_back(e);
                }
            } else {
                k->second = update;
                auto old = byId_.find(id);
                const bool changed = old == byId_.end() || old->second.text != m.text || old->second.isEdited != m.isEdited;
                if (old != byId_.end()) {
                    m.attachment.localPath = old->second.attachment.localPath;
                    m.attachment.isDownloaded = old->second.attachment.isDownloaded;
                }
                if (changed) {
                    UltraIMEvent e;
                    e.type = UltraIMEventType::MessageUpdate;
                    e.providerId = providerId_;
                    e.conversationId = channelId;
                    e.message = m;
                    events.push_back(e);
                }
            }
            auto old = byId_.find(id);
            if (old != byId_.end() && old->second.attachment.isDownloaded) {
                m.attachment.localPath = old->second.attachment.localPath;
                m.attachment.isDownloaded = true;
            }
            byId_[id] = m;
            if (!chat.hasLast || m.timestampMs >= chat.last.timestampMs) {
                chat.last = m;
                chat.hasLast = true;
            }
        }
    }
    if (lastGone) {
        // The list's line was the one deleted: the one before it now.
        const MattermostResponse page =
            Request("GET", "/api/v4/channels/" + channelId + "/posts?page=0&per_page=5", nullptr, 20000);
        std::vector<JSONValue> recent = page.Ok() ? PostsOf(page.body) : std::vector<JSONValue>();
        Prepare(recent);
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = chats_.find(channelId);
        if (it != chats_.end()) {
            it->second.hasLast = false;
            for (auto p = recent.rbegin(); p != recent.rend(); ++p) {
                if (IsSystemPost(*p) || (*p)["delete_at"].GetInteger(0) > 0) continue;
                UltraIMMessage m;
                if (!MessageFrom(*p, m)) continue;
                it->second.last = m;
                it->second.hasLast = true;
                break;
            }
        }
    }
    if (strangers) {
        // Someone new in the channel: its people again.
        Chat copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = chats_.find(channelId);
            if (it == chats_.end()) return;
            copy = it->second;
        }
        LoadMembers(copy);
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = chats_.find(channelId);
        if (it != chats_.end()) {
            it->second.memberIds = copy.memberIds;
            it->second.memberCount = copy.memberCount;
        }
    }
}

void MattermostProvider::Poll(bool first) {
    const MattermostResponse teams = Request("GET", "/api/v4/users/me/teams", nullptr, 20000);
    if (!teams.Ok()) {
        if (teams.status == 401) SessionEnded();
        else if (teams.status == 0 && state_.load() == UltraIMConnectionState::Connected)
            SetState(UltraIMConnectionState::Reconnecting, teams.error);
        return;
    }
    if (state_.load() == UltraIMConnectionState::Reconnecting) SetState(UltraIMConnectionState::Connected);
    std::map<std::string, std::string> teamNames;
    for (const JSONValue& t : teams.body.GetElements()) teamNames[t["id"].GetString()] = t["display_name"].GetString();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        teams_ = teamNames;
    }
    // Every team's channels; the direct and group ones come with each team.
    std::vector<std::string> order;
    std::map<std::string, JSONValue> channels;
    std::map<std::string, int64_t> read;          // channel -> the local user's msg_count
    for (const auto& [teamId, teamName] : teamNames) {
        const MattermostResponse list = Request("GET", "/api/v4/users/me/teams/" + teamId + "/channels", nullptr, 20000);
        const MattermostResponse members =
            Request("GET", "/api/v4/users/me/teams/" + teamId + "/channels/members", nullptr, 20000);
        if (!list.Ok() || !members.Ok()) return;      // half a list would look like channels left
        for (const JSONValue& ch : list.body.GetElements()) {
            const std::string id = ch["id"].GetString();
            if (id.empty() || ch["delete_at"].GetInteger(0) > 0) continue;
            if (channels.emplace(id, ch).second) order.push_back(id);
        }
        for (const JSONValue& m : members.body.GetElements()) read[m["channel_id"].GetString()] = m["msg_count"].GetInteger(0);
    }
    // The direct and group messages hidden (on this device or another).
    const MattermostResponse prefs = Request("GET", "/api/v4/users/me/preferences", nullptr, 20000);
    if (!prefs.Ok()) return;
    std::set<std::string> hiddenDirect, hiddenGroup;
    for (const JSONValue& p : prefs.body.GetElements()) {
        if (p["value"].GetString() != "false") continue;
        if (p["category"].GetString() == "direct_channel_show") hiddenDirect.insert(p["name"].GetString());
        if (p["category"].GetString() == "group_channel_show") hiddenGroup.insert(p["name"].GetString());
    }
    std::string me;
    int64_t clockBefore = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        me = me_.id;
        clockBefore = clock_;
    }
    const auto otherOf = [&me](const JSONValue& ch) {
        const std::string name = ch["name"].GetString();
        const size_t cut = name.find("__");
        if (cut == std::string::npos) return name;
        const std::string a = name.substr(0, cut), b = name.substr(cut + 2);
        return a == me ? b : a;
    };
    std::set<std::string> listed;
    for (const std::string& id : order) {
        const JSONValue& ch = channels[id];
        const std::string type = ch["type"].GetString();
        const int64_t lastPostAt = ch["last_post_at"].GetInteger(0);
        const bool hiddenNow = (type == "D" && hiddenDirect.count(otherOf(ch))) || (type == "G" && hiddenGroup.count(id));
        std::vector<UltraIMEvent> events;
        std::vector<UltraIMMessage> media;
        bool known = false, reshow = false;
        int64_t receiveAfter = first ? -1 : clockBefore;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            known = chats_.count(id) > 0;
            if (hiddenNow) {
                auto h = hidden_.find(id);
                if (h == hidden_.end()) {
                    // Hidden elsewhere: off the list, until its next post.
                    hidden_[id] = lastPostAt;
                    if (known) {
                        chats_.erase(id);
                        UltraIMEvent e;
                        e.type = UltraIMEventType::ConversationRemove;
                        e.providerId = providerId_;
                        e.conversationId = id;
                        events.push_back(e);
                    }
                } else if (lastPostAt > h->second) {
                    reshow = true;
                    receiveAfter = h->second;
                    hidden_.erase(h);
                    known = chats_.count(id) > 0;
                }
            } else {
                hidden_.erase(id);
            }
        }
        if (hiddenNow && !reshow) {
            for (const auto& e : events) Emit(e);
            continue;
        }
        listed.insert(id);
        if (reshow) {
            // A post in a hidden conversation brings it back, as Mattermost does.
            Chat temp;
            temp.id = id;
            temp.type = type.empty() ? 'O' : type[0];
            temp.otherId = otherOf(ch);
            Hide(temp, false);
        }
        const int64_t msgCount = read.count(id) ? read[id] : ch["total_msg_count"].GetInteger(0);
        if (!known) {
            AddChannel(ch, msgCount, receiveAfter, events, media);
        } else {
            UltraIMConversation before;
            bool readIt = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                Chat& chat = chats_[id];
                before = ConversationOf(chat);
                const bool moved = lastPostAt != chat.lastPostAt ||
                                   ch["last_root_post_at"].GetInteger(0) != chat.lastRootPostAt;
                const bool open = std::find(open_.begin(), open_.end(), id) != open_.end();
                const bool busy = chat.lastPostAt > 0 && std::max(clock_, lastPostAt) - chat.lastPostAt < timings_.busyWindowMs;
                readIt = moved || open || busy;
                chat.lastPostAt = lastPostAt;
                chat.lastRootPostAt = ch["last_root_post_at"].GetInteger(0);
                chat.totalMsgCount = ch["total_msg_count"].GetInteger(0);
                chat.msgCount = msgCount;
                chat.displayName = ch["display_name"].GetString();
                auto team = teamNames.find(ch["team_id"].GetString());
                if (team != teamNames.end()) chat.teamName = team->second;
                clock_ = std::max(clock_, lastPostAt);
            }
            if (readIt) ReadChanges(id, events, media);
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = chats_.find(id);
            if (it != chats_.end()) {
                const UltraIMConversation now = ConversationOf(it->second);
                if (!SameListing(before, now)) {
                    UltraIMEvent e;
                    e.type = UltraIMEventType::ConversationUpdate;
                    e.providerId = providerId_;
                    e.conversationId = id;
                    e.conversation = now;
                    events.push_back(e);
                }
            }
        }
        for (const auto& e : events) Emit(e);
        for (const auto& m : media) QueueMedia(m);
    }
    // Channels gone from the list: left (or removed) elsewhere.
    if (teamNames.empty()) return;
    std::vector<std::string> gone;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = chats_.begin(); it != chats_.end();) {
            if (listed.count(it->first)) {
                ++it;
                continue;
            }
            gone.push_back(it->first);
            it = chats_.erase(it);
        }
    }
    for (const std::string& id : gone) {
        UltraIMEvent e;
        e.type = UltraIMEventType::ConversationRemove;
        e.providerId = providerId_;
        e.conversationId = id;
        Emit(e);
    }
}

void MattermostProvider::QueueMedia(const UltraIMMessage& message) {
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

void MattermostProvider::Hide(const Chat& chat, bool hidden) {
    std::string me;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        me = me_.id;
    }
    JSONValue pref = JSONValue::MakeObject();
    pref.Set("user_id", JSONValue(me));
    pref.Set("category", JSONValue(chat.type == 'D' ? "direct_channel_show" : "group_channel_show"));
    pref.Set("name", JSONValue(chat.type == 'D' ? chat.otherId : chat.id));
    pref.Set("value", JSONValue(hidden ? "false" : "true"));
    JSONValue list = JSONValue::MakeArray();
    list.Append(pref);
    Request("PUT", "/api/v4/users/me/preferences", &list, 20000);
}

void MattermostProvider::FetchConversations(UltraIMConversationsCallback onResult) {
    Enqueue([this, onResult]() {
        if (state_.load() != UltraIMConnectionState::Connected) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Mattermost"), {});
            return;
        }
        std::vector<UltraIMConversation> list;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [id, chat] : chats_) list.push_back(ConversationOf(chat));
        }
        std::sort(list.begin(), list.end(), [](const UltraIMConversation& a, const UltraIMConversation& b) {
            return a.lastMessageTimeMs > b.lastMessageTimeMs;
        });
        if (onResult) onResult(UltraIMResult::Ok(), list);
    });
}

std::string MattermostProvider::PostIdOf(const std::string& messageId) const {
    // (mutex_ held.) The window knows our own posts by their client id at first.
    if (byId_.count(messageId) || known_.count(messageId)) return messageId;
    for (const auto& [postId, client] : clientOfPost_) {
        if (client == messageId) return postId;
    }
    return messageId;
}

void MattermostProvider::FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                                       UltraIMMessagesCallback onResult) {
    Enqueue([this, conversationId, beforeMessageId, limit, onResult]() {
        std::string before;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!chats_.count(conversationId)) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation"), {});
                return;
            }
            before = beforeMessageId.empty() ? std::string() : PostIdOf(beforeMessageId);
            // Open in the window: read at every look, for its edits and deletes.
            open_.erase(std::remove(open_.begin(), open_.end(), conversationId), open_.end());
            open_.push_front(conversationId);
            while (open_.size() > 8) open_.pop_back();
        }
        const int perPage = limit <= 0 ? 30 : std::min(limit, 200);
        std::vector<UltraIMMessage> out;
        // A page of system posts alone ("X joined") shows nothing: the one before it then.
        for (int round = 0; round < 5 && out.empty(); ++round) {
            std::string path = "/api/v4/channels/" + conversationId + "/posts?page=0&per_page=" + std::to_string(perPage);
            if (!before.empty()) path += "&before=" + before;
            const MattermostResponse r = Request("GET", path, nullptr, 30000);
            if (!r.Ok()) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NetworkError, r.error), {});
                return;
            }
            const std::vector<JSONValue> posts = PostsOf(r.body);
            if (posts.empty()) break;
            Prepare(posts);
            std::lock_guard<std::mutex> lock(mutex_);
            for (const JSONValue& p : posts) {
                if (IsSystemPost(p) || p["delete_at"].GetInteger(0) > 0) continue;
                UltraIMMessage m;
                if (!MessageFrom(p, m)) continue;
                FillReply(m);
                auto old = byId_.find(m.messageId);
                if (old != byId_.end() && old->second.attachment.isDownloaded) {
                    m.attachment.localPath = old->second.attachment.localPath;
                    m.attachment.isDownloaded = true;
                }
                byId_[m.messageId] = m;
                if (!known_.count(m.messageId)) known_[m.messageId] = p["update_at"].GetInteger(0);
                out.push_back(m);
            }
            before = posts.front()["id"].GetString();
        }
        if (onResult) onResult(UltraIMResult::Ok(), out);
        for (const UltraIMMessage& m : out) QueueMedia(m);
    });
}

void MattermostProvider::SearchMessages(const UltraIMMessageSearch& search, UltraIMSearchCallback onResult) {
    if (search.conversationId.empty()) {
        if (onResult)
            onResult(UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "Mattermost searches one conversation at a time"),
                     UltraIMSearchPage());
        return;
    }
    UltraIM_SearchByFetching(
        [this](const std::string& conversationId, const std::string& before, int limit, UltraIMMessagesCallback cb) {
            FetchMessages(conversationId, before, limit, std::move(cb));
        },
        search, std::move(onResult));
}

void MattermostProvider::FetchContacts(UltraIMContactsCallback onResult) {
    std::vector<UltraIMContact> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::set<std::string> seen;
        for (const auto& [id, chat] : chats_) {
            if (chat.type != 'D' || chat.otherId == me_.id || !seen.insert(chat.otherId).second) continue;
            UltraIMContact c;
            c.contactId = chat.otherId;
            c.providerId = providerId_;
            auto u = users_.find(chat.otherId);
            c.displayName = u != users_.end() ? u->second.name : chat.otherId;
            out.push_back(c);
        }
    }
    if (onResult) onResult(UltraIMResult::Ok(), out);
}

UltraIMResult MattermostProvider::ResolveUser(const std::string& address, User& out) {
    std::string name = Trim(address);
    while (!name.empty() && name[0] == '@') name.erase(0, 1);
    if (name.empty()) return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Whom? (@username or an e-mail address)");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = users_.find(name);
        if (it != users_.end()) {
            out = it->second;
            return UltraIMResult::Ok();
        }
    }
    MattermostResponse r;
    if (LooksLikeId(name)) r = Request("GET", "/api/v4/users/" + name, nullptr, 20000);
    if (!r.Ok()) {
        r = name.find('@') != std::string::npos
                ? Request("GET", "/api/v4/users/email/" + UltraNet_UrlEncode(name), nullptr, 20000)
                : Request("GET", "/api/v4/users/username/" + UltraNet_UrlEncode(Lower(name)), nullptr, 20000);
    }
    if (!r.Ok()) {
        if (r.status == 0) return UltraIMResult::Error(UltraIMResultCode::NetworkError, "The server does not answer: " + r.error);
        return UltraIMResult::Error(UltraIMResultCode::NotFound, "There is no " + name + " on this Mattermost server");
    }
    const std::string id = r.body["id"].GetString();
    EnsureUsers({id});
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(id);
    if (it == users_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "There is no " + name + " on this Mattermost server");
    out = it->second;
    return UltraIMResult::Ok();
}

UltraIMConversation MattermostProvider::OpenChannel(const JSONValue& channel) {
    const std::string id = channel["id"].GetString();
    bool known = false, wasHidden = false;
    Chat temp;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        known = chats_.count(id) > 0;
        wasHidden = hidden_.erase(id) > 0;
        if (known) temp = chats_[id];
    }
    std::vector<UltraIMEvent> events;
    std::vector<UltraIMMessage> media;
    if (!known) {
        AddChannel(channel, channel["total_msg_count"].GetInteger(0), -1, events, media);
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = chats_.find(id);
        if (it != chats_.end()) temp = it->second;
    }
    // Shown again when it was hidden: asking for it again is wanting it.
    if ((wasHidden || !known) && !temp.id.empty()) Hide(temp, false);
    UltraIMConversation conversation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = chats_.find(id);
        if (it != chats_.end()) conversation = ConversationOf(it->second);
    }
    if (known) {
        UltraIMEvent e;
        e.type = UltraIMEventType::ConversationUpdate;
        e.providerId = providerId_;
        e.conversationId = id;
        e.conversation = conversation;
        events.push_back(e);
    }
    for (const auto& e : events) Emit(e);
    return conversation;
}

void MattermostProvider::AddContact(const std::string& address, UltraIMConversationCallback onResult) {
    CreateGroup(std::string(), {address}, std::move(onResult));
}

void MattermostProvider::CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                                     UltraIMConversationCallback onResult) {
    (void)title;
    Enqueue([this, memberIds, onResult]() {
        if (state_.load() != UltraIMConnectionState::Connected) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Mattermost"), {});
            return;
        }
        std::string me;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            me = me_.id;
        }
        std::vector<std::string> others;
        for (const std::string& address : memberIds) {
            User u;
            const UltraIMResult r = ResolveUser(address, u);
            if (!r) {
                if (onResult) onResult(r, {});
                return;
            }
            if (u.id != me && std::find(others.begin(), others.end(), u.id) == others.end()) others.push_back(u.id);
        }
        if (others.empty()) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "That is you"), {});
            return;
        }
        if (others.size() > 7) {
            if (onResult)
                onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                              "A Mattermost group message has at most 7 others - make a channel for more"),
                         {});
            return;
        }
        JSONValue ids = JSONValue::MakeArray();
        ids.Append(JSONValue(me));
        for (const std::string& id : others) ids.Append(JSONValue(id));
        const MattermostResponse r =
            Request("POST", others.size() == 1 ? "/api/v4/channels/direct" : "/api/v4/channels/group", &ids, 20000);
        if (!r.Ok()) {
            if (onResult)
                onResult(UltraIMResult::Error(r.status == 403 ? UltraIMResultCode::PermissionDenied : UltraIMResultCode::NetworkError,
                                              "The server did not open the conversation: " + r.error),
                         {});
            return;
        }
        const UltraIMConversation conversation = OpenChannel(r.body);
        if (conversation.conversationId.empty()) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NetworkError, "The conversation could not be read"), {});
            return;
        }
        if (onResult) onResult(UltraIMResult::Ok(), conversation);
    });
}

void MattermostProvider::LeaveConversation(const std::string& conversationId, UltraIMResultCallback onResult) {
    Enqueue([this, conversationId, onResult]() {
        Chat chat;
        std::string me;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = chats_.find(conversationId);
            if (it == chats_.end()) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation"));
                return;
            }
            chat = it->second;
            me = me_.id;
        }
        if (chat.type == 'O' || chat.type == 'P') {
            const MattermostResponse r = Request("DELETE", "/api/v4/channels/" + chat.id + "/members/" + me, nullptr, 20000);
            if (!r.Ok() && r.status != 404) {
                // (A team's default channel, town-square, cannot be left.)
                if (onResult)
                    onResult(UltraIMResult::Error(r.status == 0 ? UltraIMResultCode::NetworkError : UltraIMResultCode::PermissionDenied,
                                                  r.error));
                return;
            }
        } else {
            Hide(chat, true);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            chats_.erase(conversationId);
            if (chat.type == 'D' || chat.type == 'G') hidden_[conversationId] = chat.lastPostAt;
            open_.erase(std::remove(open_.begin(), open_.end(), conversationId), open_.end());
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

UltraIMResult MattermostProvider::SendText(const std::string& conversationId, const std::string& text,
                                           const UltraIMSendOptions& options) {
    return SendPost(conversationId, text, std::string(), UltraIMMessageType::Text, options);
}

UltraIMResult MattermostProvider::SendImage(const std::string& conversationId, const std::string& localPath,
                                            const std::string& caption, const UltraIMSendOptions& options) {
    return SendPost(conversationId, caption, localPath, UltraIMMessageType::Image, options);
}

UltraIMResult MattermostProvider::SendVideo(const std::string& conversationId, const std::string& localPath,
                                            const std::string& caption, const UltraIMSendOptions& options) {
    return SendPost(conversationId, caption, localPath, UltraIMMessageType::Video, options);
}

UltraIMResult MattermostProvider::SendVoiceNote(const std::string& conversationId, const std::string& localPath,
                                                const UltraIMSendOptions& options) {
    return SendPost(conversationId, std::string(), localPath, UltraIMMessageType::Audio, options);
}

UltraIMResult MattermostProvider::SendFile(const std::string& conversationId, const std::string& localPath,
                                           const UltraIMSendOptions& options) {
    return SendPost(conversationId, std::string(), localPath, UltraIMMessageType::File, options);
}

UltraIMResult MattermostProvider::SendPost(const std::string& conversationId, const std::string& text,
                                           const std::string& localPath, UltraIMMessageType type,
                                           const UltraIMSendOptions& options) {
    (void)type;     // (the server tells the kind by the file's)
    if (state_.load() != UltraIMConnectionState::Connected)
        return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Mattermost");
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
    } else if (Trim(text).empty()) {
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Nothing to send");
    }
    const std::string clientId = options.clientMessageId.empty() ? UltraIM_NewClientMessageId() : options.clientMessageId;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sentHere_.insert(clientId);
    }
    Enqueue([this, conversationId, text, localPath, mime, options, clientId]() {
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
        std::string fileId;
        if (!localPath.empty()) {
            int lastPercent = -1;
            const MattermostResponse up = Upload(conversationId, localPath, mime, [&](int64_t now, int64_t total) {
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
            const auto infos = up.body["file_infos"].GetElements();
            if (infos.empty()) return failed("The server kept no file");
            fileId = infos.front()["id"].GetString();
            std::lock_guard<std::mutex> lock(mutex_);
            files_[fileId] = infos.front();
        }
        std::string rootId;
        if (!options.replyToMessageId.empty()) {
            // A reply goes into the thread of the post answered: its first post.
            std::lock_guard<std::mutex> lock(mutex_);
            rootId = PostIdOf(options.replyToMessageId);
            auto answered = byId_.find(rootId);
            if (answered != byId_.end() && !answered->second.replyTo.messageId.empty())
                rootId = answered->second.replyTo.messageId;
        }
        JSONValue body = JSONValue::MakeObject();
        body.Set("channel_id", JSONValue(conversationId));
        body.Set("message", JSONValue(text));
        if (!rootId.empty()) body.Set("root_id", JSONValue(rootId));
        if (!fileId.empty()) {
            JSONValue ids = JSONValue::MakeArray();
            ids.Append(JSONValue(fileId));
            body.Set("file_ids", ids);
        }
        // The server takes a post sent twice under one pending id (a retry
        // after a lost answer) once; the polls know it as ours by it.
        body.Set("pending_post_id", JSONValue(clientId));
        const MattermostResponse r = Request("POST", "/api/v4/posts", &body, 60000);
        if (!r.Ok()) return failed(r.error);
        Prepare({r.body});
        UltraIMMessage sent;
        UltraIMConversation conversation;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const std::string postId = r.body["id"].GetString();
            clientOfPost_[postId] = clientId;
            MessageFrom(r.body, sent);
            FillReply(sent);
            sent.attachment.localPath = localPath;
            sent.attachment.isDownloaded = !localPath.empty();
            if (options.durationSeconds > 0) sent.attachment.durationSeconds = options.durationSeconds;
            known_[postId] = r.body["update_at"].GetInteger(0);
            byId_[postId] = sent;
            auto it = chats_.find(conversationId);
            if (it != chats_.end()) {
                // (sinceMs stays: a post of someone else's a moment before
                // this one must still be read.)
                it->second.last = sent;
                it->second.hasLast = true;
                ++it->second.totalMsgCount;
                ++it->second.msgCount;
                conversation = ConversationOf(it->second);
            }
        }
        UltraIMEvent delivered;
        delivered.type = UltraIMEventType::MessageDelivered;
        delivered.providerId = providerId_;
        delivered.conversationId = conversationId;
        delivered.message = sent;
        delivered.deliveryStatus = UltraIMDeliveryStatus::Sent;
        Emit(delivered);
        // The row learns the post's id (for an edit, a delete, a reply).
        UltraIMEvent updated;
        updated.type = UltraIMEventType::MessageUpdate;
        updated.providerId = providerId_;
        updated.conversationId = conversationId;
        updated.message = sent;
        Emit(updated);
        if (!conversation.conversationId.empty()) {
            UltraIMEvent changed;
            changed.type = UltraIMEventType::ConversationUpdate;
            changed.providerId = providerId_;
            changed.conversationId = conversationId;
            changed.conversation = conversation;
            Emit(changed);
        }
    });
    return UltraIMResult::Ok();
}

void MattermostProvider::DownloadAttachment(const std::string& conversationId, const std::string& messageId,
                                            UltraIMAttachmentCallback onResult) {
    (void)conversationId;
    Enqueue([this, messageId, onResult]() {
        UltraIMMessage m;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = byId_.find(PostIdOf(messageId));
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

UltraIMResult MattermostProvider::EditMessage(const std::string& conversationId, const std::string& messageId,
                                              const std::string& newText) {
    std::string postId;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        postId = PostIdOf(messageId);
        auto it = byId_.find(postId);
        if (it == byId_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such message");
        if (!it->second.isOwn) return UltraIMResult::Error(UltraIMResultCode::PermissionDenied, "Only its sender can change a message");
        if (Trim(newText).empty() && it->second.attachment.remoteUrl.empty())
            return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "A text cannot be empty - delete it instead");
    }
    Enqueue([this, postId, newText, conversationId]() {
        JSONValue body = JSONValue::MakeObject();
        body.Set("message", JSONValue(newText));
        const MattermostResponse r = Request("PUT", "/api/v4/posts/" + postId + "/patch", &body, 30000);
        if (!r.Ok()) return;
        Prepare({r.body});
        UltraIMMessage m;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!MessageFrom(r.body, m)) return;
            FillReply(m);
            auto old = byId_.find(postId);
            if (old != byId_.end()) {
                m.attachment.localPath = old->second.attachment.localPath;
                m.attachment.isDownloaded = old->second.attachment.isDownloaded;
            }
            m.isEdited = true;
            byId_[postId] = m;
            known_[postId] = r.body["update_at"].GetInteger(0);
            auto chat = chats_.find(conversationId);
            if (chat != chats_.end() && chat->second.hasLast && chat->second.last.messageId == postId) chat->second.last = m;
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

UltraIMDeleteChoices MattermostProvider::GetDeleteChoices(const std::string& conversationId,
                                                          const std::string& messageId) const {
    (void)conversationId;
    UltraIMDeleteChoices choices;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byId_.find(PostIdOf(messageId));
    choices.forEveryone = it != byId_.end() && it->second.isOwn;
    return choices;
}

UltraIMResult MattermostProvider::DeleteMessage(const std::string& conversationId, const std::string& messageId,
                                                UltraIMDeleteScope scope) {
    if (scope == UltraIMDeleteScope::ForMe)
        return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "Mattermost deletes for everyone only");
    std::string postId, clientId;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        postId = PostIdOf(messageId);
        auto client = clientOfPost_.find(postId);
        clientId = client != clientOfPost_.end() ? client->second : postId;
        auto it = byId_.find(postId);
        if (it == byId_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such message");
        if (!it->second.isOwn) return UltraIMResult::Error(UltraIMResultCode::PermissionDenied, "Only its sender can delete a message");
    }
    Enqueue([this, postId, clientId, conversationId]() {
        const MattermostResponse r = Request("DELETE", "/api/v4/posts/" + postId, nullptr, 30000);
        if (!r.Ok() && r.status != 404) return;
        bool wasLast = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            deleted_.insert(postId);
            known_.erase(postId);
            byId_.erase(postId);
            auto chat = chats_.find(conversationId);
            wasLast = chat != chats_.end() && chat->second.hasLast && chat->second.last.messageId == postId;
        }
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageDelete;
        e.providerId = providerId_;
        e.conversationId = conversationId;
        e.message.providerId = providerId_;
        e.message.conversationId = conversationId;
        e.message.messageId = postId;
        e.message.clientMessageId = clientId;
        Emit(e);
        if (!wasLast) return;
        // The list's line: the post before it now.
        const MattermostResponse page =
            Request("GET", "/api/v4/channels/" + conversationId + "/posts?page=0&per_page=5", nullptr, 20000);
        std::vector<JSONValue> recent = page.Ok() ? PostsOf(page.body) : std::vector<JSONValue>();
        Prepare(recent);
        UltraIMConversation conversation;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto chat = chats_.find(conversationId);
            if (chat == chats_.end()) return;
            chat->second.hasLast = false;
            for (auto p = recent.rbegin(); p != recent.rend(); ++p) {
                if (IsSystemPost(*p) || (*p)["delete_at"].GetInteger(0) > 0) continue;
                UltraIMMessage m;
                if (!MessageFrom(*p, m)) continue;
                chat->second.last = m;
                chat->second.hasLast = true;
                break;
            }
            conversation = ConversationOf(chat->second);
        }
        UltraIMEvent changed;
        changed.type = UltraIMEventType::ConversationUpdate;
        changed.providerId = providerId_;
        changed.conversationId = conversationId;
        changed.conversation = conversation;
        Emit(changed);
    });
    return UltraIMResult::Ok();
}

UltraIMResult MattermostProvider::MarkRead(const std::string& conversationId, const std::string& upToMessageId) {
    (void)upToMessageId;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!chats_.count(conversationId)) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation");
    }
    Enqueue([this, conversationId]() {
        JSONValue body = JSONValue::MakeObject();
        body.Set("channel_id", JSONValue(conversationId));
        const MattermostResponse r = Request("POST", "/api/v4/channels/members/me/view", &body, 20000);
        if (!r.Ok()) return;
        UltraIMConversation conversation;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = chats_.find(conversationId);
            if (it == chats_.end() || it->second.msgCount >= it->second.totalMsgCount) return;
            it->second.msgCount = it->second.totalMsgCount;
            conversation = ConversationOf(it->second);
        }
        UltraIMEvent e;
        e.type = UltraIMEventType::ConversationUpdate;
        e.providerId = providerId_;
        e.conversationId = conversationId;
        e.conversation = conversation;
        Emit(e);
    });
    return UltraIMResult::Ok();
}

} // namespace UltraIMMattermost
