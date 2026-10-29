// Plugins/UltraIM/matrix/MatrixProvider.cpp
// Matrix as an UltraIM provider
// Version: 0.2.1 - attachments: downloaded (plain and encrypted), SendImage / SendVideo / SendFile
// Version: 0.2.0
// Last Modified: 2026-10-29
// Author: UltraCanvas Framework

#include "MatrixProvider.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraCrypt/UltraCryptCore.h"
#include "UltraCanvasTextUtils.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace UltraIMMatrix {

using UltraCanvas::JSONValue;

namespace {

const char* const kClient = "/_matrix/client/v3";
constexpr size_t kTimelineCap = 500;     // events kept per room in memory

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

bool IsUserId(const std::string& id) {
    return id.size() > 3 && id[0] == '@' && id.find(':') != std::string::npos;
}

std::string Localpart(const std::string& userId) {
    if (userId.empty() || userId[0] != '@') return userId;
    const size_t colon = userId.find(':');
    return userId.substr(1, colon == std::string::npos ? std::string::npos : colon - 1);
}

UltraIMPresence PresenceFrom(const std::string& presence) {
    if (presence == "online") return UltraIMPresence::Online;
    if (presence == "unavailable") return UltraIMPresence::Away;
    if (presence == "offline") return UltraIMPresence::Offline;
    return UltraIMPresence::Unknown;
}

// A reply's body starts with the quoted original ("> <@bob:hs> hi"), then a
// blank line; the reply context carries that already.
std::string StripReplyFallback(const std::string& body) {
    if (body.rfind("> ", 0) != 0) return body;
    size_t at = 0;
    while (at < body.size() && body.compare(at, 2, "> ") == 0) {
        const size_t nl = body.find('\n', at);
        if (nl == std::string::npos) return body;
        at = nl + 1;
    }
    if (at < body.size() && body[at] == '\n') ++at;
    return body.substr(at);
}

std::string SnippetOf(const UltraIMMessage& m) {
    std::string text = m.text;
    switch (m.type) {
        case UltraIMMessageType::Image: text = "[image] " + m.text; break;
        case UltraIMMessageType::File:  text = "[file] " + m.text; break;
        case UltraIMMessageType::Audio: text = "[audio] " + m.text; break;
        case UltraIMMessageType::Video: text = "[video] " + m.text; break;
        default: break;
    }
    const size_t nl = text.find('\n');
    if (nl != std::string::npos) text = text.substr(0, nl);
    return text.size() > 120 ? text.substr(0, 120) : text;
}

const char* const kWaitingForKey = "Waiting for the key to this encrypted message";

UltraIMResult HttpError(const MatrixResponse& r, UltraIMResultCode fallback) {
    UltraIMResultCode code = fallback;
    if (r.status == 0) code = UltraIMResultCode::NetworkError;
    else if (r.errcode == "M_FORBIDDEN") code = UltraIMResultCode::PermissionDenied;
    else if (r.errcode == "M_UNKNOWN_TOKEN" || r.status == 401) code = UltraIMResultCode::NotAuthenticated;
    else if (r.errcode == "M_NOT_FOUND" || r.status == 404) code = UltraIMResultCode::NotFound;
    else if (r.errcode == "M_LIMIT_EXCEEDED") code = UltraIMResultCode::RateLimited;
    return UltraIMResult::Error(code, r.error.empty() ? "The homeserver refused the request" : r.error);
}

} // namespace

// ============================================================================
// LIFECYCLE
// ============================================================================

MatrixProvider::MatrixProvider(std::string providerId) : providerId_(std::move(providerId)) {}

MatrixProvider::~MatrixProvider() {
    Shutdown();
}

UltraIMCapabilityFlags MatrixProvider::GetCapabilities() const {
    using F = UltraIMCapabilityFlags;
    const F flags = F::TextMessages | F::GroupChats | F::ReadReceipts | F::TypingIndicator | F::EditMessage |
                    F::DeleteMessage | F::ReplyToMessage | F::OfflineMessages | F::MultiDevice | F::AddContact |
                    F::CreateGroup | F::ImageMessages | F::FileTransfer;
    return OlmAvailable() ? flags | F::E2EEncryption : flags;
}

UltraIMResult MatrixProvider::Initialize(const UltraIMConfig& config, UltraIMEventSink sink) {
    config_ = config;
    sink_ = std::move(sink);
    if (config.dataDirectory.empty())
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "The Matrix provider needs a data directory (UltraIMConfig::dataDirectory)");
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = UltraCanvas::PathFromUtf8(config.dataDirectory) / UltraCanvas::PathFromUtf8(providerId_);
    fs::create_directories(dir, ec);
    vault_ = UltraVault::DeviceKeyVault(UltraCanvas::PathToUtf8(dir), {"matrix.vault", config.appId + "." + providerId_ + "."});
    vaultReady_ = true;
    return UltraIMResult::Ok();
}

void MatrixProvider::Shutdown() {
    SignOut();
    sink_ = nullptr;
}

namespace {

// ---- media helpers -------------------------------------------------------------

UltraCryptSecureBuffer SecretOf(const std::vector<uint8_t>& bytes) {
    return UltraCryptSecureBuffer(bytes.data(), bytes.size());
}

// base64url (the JWK key) to the standard alphabet; the lenient decoder
// takes the missing padding.
std::string FromBase64Url(std::string text) {
    for (char& c : text) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    return text;
}

std::string UnpaddedBase64(const std::vector<uint8_t>& bytes, bool url) {
    std::string out = UltraCanvas::Base64Encode(bytes, false);
    while (!out.empty() && out.back() == '=') out.pop_back();
    if (url) {
        for (char& c : out) {
            if (c == '+') c = '-';
            else if (c == '/') c = '_';
        }
    }
    return out;
}

// "mxc://server/id" -> the two parts.
bool SplitMxc(const std::string& url, std::string& server, std::string& id) {
    if (url.rfind("mxc://", 0) != 0) return false;
    const size_t slash = url.find('/', 6);
    if (slash == std::string::npos) return false;
    server = url.substr(6, slash - 6);
    id = url.substr(slash + 1);
    return !server.empty() && !id.empty() && id.find('/') == std::string::npos;
}

std::string LowerExtension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size() || name.find('/', dot) != std::string::npos) return std::string();
    std::string ext = name.substr(dot);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

const std::pair<const char*, const char*> kMimeTypes[] = {
    {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".png", "image/png"},   {".gif", "image/gif"},
    {".webp", "image/webp"}, {".bmp", "image/bmp"},  {".svg", "image/svg+xml"}, {".heic", "image/heic"},
    {".mp4", "video/mp4"},  {".m4v", "video/mp4"},   {".mov", "video/quicktime"}, {".webm", "video/webm"},
    {".mkv", "video/x-matroska"}, {".avi", "video/x-msvideo"},
    {".mp3", "audio/mpeg"}, {".ogg", "audio/ogg"},   {".oga", "audio/ogg"},    {".opus", "audio/ogg"},
    {".wav", "audio/wav"},  {".m4a", "audio/mp4"},   {".flac", "audio/flac"},
    {".pdf", "application/pdf"}, {".txt", "text/plain"}, {".zip", "application/zip"},
};

std::string MimeFor(const std::string& path) {
    const std::string ext = LowerExtension(path);
    for (const auto& entry : kMimeTypes) {
        if (ext == entry.first) return entry.second;
    }
    return "application/octet-stream";
}

std::string ExtensionFor(const std::string& mimeType, const std::string& fileName) {
    for (const auto& entry : kMimeTypes) {
        if (mimeType == entry.second) return entry.first;
    }
    return LowerExtension(fileName);
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    std::FILE* f = UltraCanvas::OpenFileUtf8(path, "rb");
    if (!f) return false;
    out.clear();
    uint8_t buffer[65536];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) out.insert(out.end(), buffer, buffer + n);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

bool WriteWholeFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::FILE* f = UltraCanvas::OpenFileUtf8(path, "wb");
    if (!f) return false;
    const bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return (std::fclose(f) == 0) && ok;
}

// The m.file format (v2): AES-256-CTR with the JWK key, the IV and the
// SHA-256 of the ciphertext. The hash is checked before anything is
// decrypted - CTR cannot tell a wrong key from the right one.
bool DecryptMedia(const JSONValue& file, std::vector<uint8_t>& bytes, std::string& error) {
    if (file["key"]["alg"].GetString() != "A256CTR" || file["key"]["kty"].GetString() != "oct") {
        error = "an encrypted file in a format ChatCube does not read";
        return false;
    }
    const std::vector<uint8_t> expected = UltraCanvas::Base64Decode(file["hashes"]["sha256"].GetString());
    std::vector<uint8_t> digest;
    if (!UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, bytes.data(), bytes.size(), digest) || expected.size() != 32 ||
        !UltraCrypt_ConstantTimeEquals(digest.data(), digest.size(), expected.data(), expected.size())) {
        error = "the file does not match its hash";
        return false;
    }
    std::vector<uint8_t> key = UltraCanvas::Base64Decode(FromBase64Url(file["key"]["k"].GetString()));
    UltraCryptCtrParams params;
    params.iv = UltraCanvas::Base64Decode(file["iv"].GetString());
    if (key.size() != 32 || params.iv.size() != 16) {
        UltraCrypt_SecureZero(key.data(), key.size());
        error = "the file's key is not usable";
        return false;
    }
    UltraCryptSecureBuffer plain;
    const bool ok = UltraCrypt_AesCtrDecrypt(SecretOf(key), params, bytes.data(), bytes.size(), plain);
    UltraCrypt_SecureZero(key.data(), key.size());
    if (!ok) {
        error = "the file could not be decrypted";
        return false;
    }
    bytes.assign(plain.Data(), plain.Data() + plain.GetSize());
    return true;
}

// The same the other way: `bytes` become the ciphertext, `fileOut` the
// m.file object without its url.
bool EncryptMedia(std::vector<uint8_t>& bytes, JSONValue& fileOut, std::string& error) {
    std::vector<uint8_t> key(32), iv(16, 0);
    // The IV: 8 random bytes, then 8 zero - the counter's low half, as the
    // format wants it.
    if (!UltraCrypt_RandomBytes(key.data(), key.size()) || !UltraCrypt_RandomBytes(iv.data(), 8)) {
        error = "no randomness for the file's key";
        return false;
    }
    UltraCryptCtrParams params;
    params.iv = iv;
    std::vector<uint8_t> cipher;
    const bool ok = UltraCrypt_AesCtrEncrypt(SecretOf(key), params, bytes.data(), bytes.size(), cipher);
    if (!ok) {
        UltraCrypt_SecureZero(key.data(), key.size());
        error = "the file could not be encrypted";
        return false;
    }
    std::vector<uint8_t> digest;
    UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, cipher.data(), cipher.size(), digest);
    JSONValue jwk = JSONValue::MakeObject();
    jwk.Set("alg", JSONValue("A256CTR"));
    jwk.Set("ext", JSONValue(true));
    jwk.Set("k", JSONValue(UnpaddedBase64(key, true)));
    JSONValue ops = JSONValue::MakeArray();
    ops.Append(JSONValue("encrypt"));
    ops.Append(JSONValue("decrypt"));
    jwk.Set("key_ops", ops);
    jwk.Set("kty", JSONValue("oct"));
    UltraCrypt_SecureZero(key.data(), key.size());
    fileOut = JSONValue::MakeObject();
    fileOut.Set("v", JSONValue("v2"));
    fileOut.Set("key", jwk);
    fileOut.Set("iv", JSONValue(UnpaddedBase64(iv, false)));
    JSONValue hashes = JSONValue::MakeObject();
    hashes.Set("sha256", JSONValue(UnpaddedBase64(digest, false)));
    fileOut.Set("hashes", hashes);
    bytes.swap(cipher);
    return true;
}

} // namespace

void MatrixProvider::Emit(const UltraIMEvent& event) {
    if (sink_) sink_(event);
}

void MatrixProvider::SetState(UltraIMConnectionState state, const std::string& error) {
    if (state_.exchange(state) == state && error.empty()) return;
    UltraIMEvent event;
    event.type = UltraIMEventType::ConnectionStateChange;
    event.providerId = providerId_;
    event.connectionState = state;
    event.errorText = error;
    Emit(event);
}

std::shared_ptr<MatrixHttp> MatrixProvider::Http() const {
    std::lock_guard<std::mutex> lock(httpMutex_);
    return http_;
}

std::string MatrixProvider::GetLocalUserId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return userId_;
}

std::string MatrixProvider::GetDeviceId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deviceId_;
}

std::string MatrixProvider::GetHomeserverUrl() const {
    std::shared_ptr<MatrixHttp> http = Http();
    return http ? http->GetBaseUrl() : std::string();
}

// ============================================================================
// SESSION
// ============================================================================

// The vault is opened for each access (DeviceKeyVault::Use): other modules'
// vaults share the process's one open store.
bool MatrixProvider::SaveSession(const std::string& url, const std::string& userId, const std::string& deviceId,
                                 const std::string& token) {
    if (!vaultReady_) return false;
    JSONValue session = JSONValue::MakeObject();
    session.Set("url", JSONValue(url));
    session.Set("user_id", JSONValue(userId));
    session.Set("device_id", JSONValue(deviceId));
    session.Set("access_token", JSONValue(token));
    bool stored = false;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { stored = v.Store("session", UltraCanvas::JSON::Serialize(session)); });
    return stored;
}

bool MatrixProvider::LoadSession(std::string& url, std::string& userId, std::string& deviceId,
                                 std::string& token) const {
    if (!vaultReady_) return false;
    std::string text;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Retrieve("session", text); });
    if (text.empty()) return false;
    UltraCanvas::JSONParseResult parsed;
    JSONValue session = UltraCanvas::JSON::Parse(text, &parsed);
    url = session["url"].GetString();
    userId = session["user_id"].GetString();
    deviceId = session["device_id"].GetString();
    token = session["access_token"].GetString();
    return !url.empty() && !token.empty();
}

bool MatrixProvider::HasStoredSession() const {
    std::string url, userId, deviceId, token;
    return LoadSession(url, userId, deviceId, token);
}

std::string MatrixProvider::DiscoverBaseUrl(const std::string& server) {
    std::shared_ptr<MatrixHttp> http = Http();
    if (!http) return std::string();
    std::string base = Trim(server);
    if (base.rfind("http://", 0) != 0 && base.rfind("https://", 0) != 0) base = "https://" + base;
    while (!base.empty() && base.back() == '/') base.pop_back();
    // A server name may delegate its client API elsewhere (matrix.org ->
    // matrix-client.matrix.org): .well-known says where.
    http->SetBaseUrl(base);
    MatrixResponse known = http->Await("GET", "/.well-known/matrix/client", nullptr, 8000);
    const std::string delegated = known.Ok() ? known.body["m.homeserver"]["base_url"].GetString() : std::string();
    if (!delegated.empty()) {
        base = delegated;
        while (!base.empty() && base.back() == '/') base.pop_back();
    }
    http->SetBaseUrl(base);
    MatrixResponse versions = http->Await("GET", "/_matrix/client/versions", nullptr, 10000);
    if (!versions.Ok() || !versions.body["versions"].IsArray()) return std::string();
    return base;
}

void MatrixProvider::SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) {
    auto finish = [onResult](const UltraIMResult& r) { if (onResult) onResult(r); };
    if (state_ == UltraIMConnectionState::Connected || state_ == UltraIMConnectionState::Connecting) {
        finish(UltraIMResult::Ok());
        return;
    }
    SignOut();      // whatever is left of an earlier session

    std::string storedUrl, storedUser, storedDevice, storedToken;
    const bool stored = LoadSession(storedUrl, storedUser, storedDevice, storedToken);
    const std::string username = Trim(credentials.username);
    const bool usePassword = !username.empty() && !credentials.password.empty();
    const bool useToken = !credentials.accessToken.empty();
    if (!usePassword && !useToken && !stored) {
        finish(UltraIMResult::Error(UltraIMResultCode::NotAuthenticated,
                                    "Sign in to Matrix with your user name and password"));
        return;
    }
    std::string server = Trim(credentials.server);
    if (server.empty() && IsUserId(username)) server = username.substr(username.find(':') + 1);
    // Signing in again as the same user: the same device, when its keys are
    // still here - its encrypted rooms stay readable.
    std::string reuseDevice;
    if (usePassword && stored && !storedDevice.empty() &&
        (storedUser == username || Localpart(storedUser) == Localpart(username)) &&
        MatrixCrypto::StoreExists(CryptoStorePath(storedUser, storedDevice)))
        reuseDevice = storedDevice;
    if (server.empty() && (usePassword || useToken)) {
        finish(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "Give the homeserver, or your full Matrix ID (@name:server)"));
        return;
    }

    {
        std::lock_guard<std::mutex> lock(httpMutex_);
        http_ = std::make_shared<MatrixHttp>();
    }
    stopping_ = false;
    SetState(UltraIMConnectionState::Connecting);
    const std::string password = credentials.password;
    const std::string token = credentials.accessToken;
    // Discovery, login and the sync loop all run on the session's thread.
    {
        std::lock_guard<std::mutex> lock(jobMutex_);
        jobsStopping_ = false;
        jobs_.clear();
    }
    sendThread_ = std::thread([this]() { SendLoop(); });
    syncThread_ = std::thread([this, finish, usePassword, useToken, server, username, password, token, storedUrl,
                               storedToken, reuseDevice]() {
        std::shared_ptr<MatrixHttp> http = Http();
        auto fail = [this, finish](const UltraIMResult& r) {
            if (stopping_) return;
            SetState(UltraIMConnectionState::Error, r.message);
            finish(r);
        };
        std::string url = storedUrl;
        std::string accessToken = storedToken;
        if (usePassword || useToken) {
            url = DiscoverBaseUrl(server);
            if (stopping_) return;
            if (url.empty())
                return fail(UltraIMResult::Error(UltraIMResultCode::NetworkError,
                                                 "No Matrix homeserver answers at " + server));
            if (usePassword) {
                JSONValue login = JSONValue::MakeObject();
                login.Set("type", JSONValue("m.login.password"));
                JSONValue identifier = JSONValue::MakeObject();
                identifier.Set("type", JSONValue("m.id.user"));
                identifier.Set("user", JSONValue(username));
                login.Set("identifier", identifier);
                login.Set("password", JSONValue(password));
                login.Set("initial_device_display_name", JSONValue("ChatCube"));
                if (!reuseDevice.empty()) login.Set("device_id", JSONValue(reuseDevice));
                MatrixResponse r = http->Await("POST", std::string(kClient) + "/login", &login, 20000);
                if (stopping_) return;
                if (!r.Ok()) return fail(HttpError(r, UltraIMResultCode::AuthFailed));
                accessToken = r.body["access_token"].GetString();
                const std::string wellKnown = r.body["well_known"]["m.homeserver"]["base_url"].GetString();
                if (!wellKnown.empty()) {
                    url = wellKnown;
                    while (!url.empty() && url.back() == '/') url.pop_back();
                    http->SetBaseUrl(url);
                }
            } else {
                accessToken = token;
            }
        } else {
            http->SetBaseUrl(url);
        }
        // Whose token this is (and whether it still works).
        http->SetToken(accessToken);
        MatrixResponse who = http->Await("GET", std::string(kClient) + "/account/whoami", nullptr, 15000);
        if (stopping_) return;
        if (!who.Ok()) {
            if (who.status == 401) vault_.Use([](UltraVault::DeviceKeyVault& v) { v.Remove("session"); });
            return fail(HttpError(who, UltraIMResultCode::AuthFailed));
        }
        const std::string userId = who.body["user_id"].GetString();
        const std::string deviceId = who.body["device_id"].GetString();
        SaveSession(url, userId, deviceId, accessToken);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            userId_ = userId;
            deviceId_ = deviceId;
        }
        OpenCrypto(userId, deviceId);
        if (stopping_) return;
        SetState(UltraIMConnectionState::Connected);
        finish(UltraIMResult::Ok());
        SyncLoop();
    });
}

void MatrixProvider::SignOut() {
    stopping_ = true;
    std::shared_ptr<MatrixHttp> http;
    {
        std::lock_guard<std::mutex> lock(httpMutex_);
        http = http_;
    }
    if (http) http->Stop();
    if (syncThread_.joinable()) syncThread_.join();
    {
        std::lock_guard<std::mutex> lock(jobMutex_);
        jobsStopping_ = true;
    }
    jobChanged_.notify_all();
    if (sendThread_.joinable()) sendThread_.join();
    crypto_.Close();
    {
        std::lock_guard<std::mutex> lock(httpMutex_);
        http_.reset();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        rooms_.clear();
        direct_.clear();
        presence_.clear();
        nextBatch_.clear();
        txnToClient_.clear();
        pendingDecrypt_.clear();
        deviceId_.clear();
        media_.clear();
        mediaUses_.clear();
        mediaWaiters_.clear();      // their downloads ended with the session; nobody is told
        downloading_.clear();
        downloadQueue_.clear();
    }
    SetState(UltraIMConnectionState::Disconnected);
}

// ============================================================================
// SYNC
// ============================================================================

void MatrixProvider::SyncLoop() {
    std::shared_ptr<MatrixHttp> http = Http();
    if (!http) return;
    JSONValue filter = JSONValue::MakeObject();
    JSONValue room = JSONValue::MakeObject();
    JSONValue timeline = JSONValue::MakeObject();
    timeline.Set("limit", JSONValue(static_cast<int64_t>(timings_.initialTimelineLimit)));
    room.Set("timeline", timeline);
    JSONValue state = JSONValue::MakeObject();
    state.Set("lazy_load_members", JSONValue(true));
    room.Set("state", state);
    filter.Set("room", room);
    const std::string filterParam = PathSegment(UltraCanvas::JSON::Serialize(filter));

    int backoffMs = 1000;
    bool initial = true;
    while (!stopping_) {
        std::string since;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            since = nextBatch_;
        }
        const int wait = initial ? 0 : timings_.syncTimeoutMs;
        std::string path = std::string(kClient) + "/sync?timeout=" + std::to_string(wait) + "&filter=" + filterParam;
        if (!since.empty()) path += "&since=" + PathSegment(since);
        MatrixResponse r = http->Await("GET", path, nullptr, wait + 20000);
        if (stopping_ || r.cancelled) break;
        if (r.Ok()) {
            // Room keys first: the messages they open may be in this very sync.
            const std::vector<MatrixSessionRef> arrived = crypto_.HandleSync(*http, r.body);
            ApplySync(r.body, initial);
            RetryPending(arrived);
            crypto_.Flush();
            initial = false;
            backoffMs = 1000;
            if (state_ != UltraIMConnectionState::Connected) SetState(UltraIMConnectionState::Connected);
            continue;
        }
        if (r.status == 401) {
            vault_.Use([](UltraVault::DeviceKeyVault& v) { v.Remove("session"); });
            SetState(UltraIMConnectionState::Error, "The Matrix session ended - sign in again");
            UltraIMEvent event;
            event.type = UltraIMEventType::AuthRequire;
            event.providerId = providerId_;
            Emit(event);
            break;
        }
        SetState(UltraIMConnectionState::Reconnecting, r.error);
        for (int waited = 0; waited < backoffMs && !stopping_; waited += 100)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        backoffMs = std::min(backoffMs * 2, 30000);
    }
}

void MatrixProvider::ApplySync(const JSONValue& body, bool initial) {
    std::vector<UltraIMEvent> events;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        nextBatch_ = body["next_batch"].GetString(nextBatch_);
        for (const auto& event : body["account_data"]["events"].GetElements()) {
            if (event["type"].GetString() != "m.direct") continue;
            direct_.clear();
            for (const auto& member : event["content"].GetMembers()) {
                for (const auto& roomId : member.second.GetElements()) direct_[member.first].push_back(roomId.GetString());
            }
        }
        std::set<std::string> presenceChanged;
        for (const auto& event : body["presence"]["events"].GetElements()) {
            if (event["type"].GetString() != "m.presence") continue;
            const std::string sender = event["sender"].GetString();
            const UltraIMPresence presence = PresenceFrom(event["content"]["presence"].GetString());
            if (presence_[sender] != presence) presenceChanged.insert(sender);
            presence_[sender] = presence;
        }
        for (const auto& member : body["rooms"]["join"].GetMembers())
            ApplyRoom(member.first, member.second, false, initial, events);
        for (const auto& member : body["rooms"]["invite"].GetMembers())
            ApplyRoom(member.first, member.second, true, initial, events);
        for (const auto& member : body["rooms"]["leave"].GetMembers()) {
            if (!rooms_.erase(member.first)) continue;
            UltraIMEvent event;
            event.type = UltraIMEventType::ConversationRemove;
            event.providerId = providerId_;
            event.conversationId = member.first;
            events.push_back(event);
        }
        for (const std::string& user : presenceChanged) {
            for (const auto& entry : rooms_) {
                if (DirectUserOf(entry.second) != user) continue;
                UltraIMEvent event;
                event.type = UltraIMEventType::PresenceChange;
                event.providerId = providerId_;
                event.contactId = user;
                event.conversationId = entry.first;
                event.presence = presence_[user];
                events.push_back(event);
            }
        }
    }
    for (const auto& event : events) Emit(event);
    FlushDownloads();
}

void MatrixProvider::ApplyRoom(const std::string& roomId, const JSONValue& data, bool invited, bool initial,
                               std::vector<UltraIMEvent>& events) {
    const bool isNew = rooms_.find(roomId) == rooms_.end();
    Room& room = rooms_[roomId];
    room.id = roomId;
    bool changed = initial || isNew;
    if (invited) {
        room.invited = true;
        for (const auto& event : data["invite_state"]["events"].GetElements()) {
            ApplyState(room, event);
            if (event["type"].GetString() == "m.room.member" && event["state_key"].GetString() == userId_ &&
                event["content"]["membership"].GetString() == "invite")
                room.inviter = event["sender"].GetString();
        }
        changed = true;
    } else {
        if (room.invited) changed = true;
        room.invited = false;
        const JSONValue& heroes = data["summary"]["m.heroes"];
        if (heroes.IsArray()) {
            room.heroes.clear();
            for (const auto& h : heroes.GetElements()) room.heroes.push_back(h.GetString());
        }
        for (const auto& event : data["state"]["events"].GetElements()) ApplyState(room, event);
        const JSONValue& timeline = data["timeline"];
        if (timeline["limited"].GetBoolean(false) && !initial) {
            // A gap: what came before is no longer next to what we hold, and
            // who left in it may not be in the (lazily loaded) state.
            room.timeline.clear();
            room.historyComplete = false;
            room.recipientsKnown = false;
        }
        if (room.prevBatch.empty() || timeline["limited"].GetBoolean(false))
            room.prevBatch = timeline["prev_batch"].GetString(room.prevBatch);
        for (const auto& event : timeline["events"].GetElements()) {
            if (ApplyEvent(room, event, !initial, events)) changed = true;
        }
        for (const auto& event : data["ephemeral"]["events"].GetElements()) {
            const std::string type = event["type"].GetString();
            if (type == "m.typing") {
                std::set<std::string> now;
                for (const auto& u : event["content"]["user_ids"].GetElements()) {
                    if (u.GetString() != userId_) now.insert(u.GetString());
                }
                for (const std::string& u : now) {
                    if (room.typing.count(u)) continue;
                    UltraIMEvent e;
                    e.type = UltraIMEventType::TypingChange;
                    e.providerId = providerId_;
                    e.conversationId = roomId;
                    e.contactId = u;
                    e.isTyping = true;
                    events.push_back(e);
                }
                for (const std::string& u : room.typing) {
                    if (now.count(u)) continue;
                    UltraIMEvent e;
                    e.type = UltraIMEventType::TypingChange;
                    e.providerId = providerId_;
                    e.conversationId = roomId;
                    e.contactId = u;
                    e.isTyping = false;
                    events.push_back(e);
                }
                room.typing = now;
            } else if (type == "m.receipt") {
                ApplyReceipts(room, event["content"], events);
            }
        }
        const JSONValue* counts = data.Find("unread_notifications");
        if (counts && counts->Contains("notification_count")) {
            const int unread = static_cast<int>((*counts)["notification_count"].GetInteger(0));
            if (unread != room.unread) changed = true;
            room.unread = unread;
        }
    }
    if (changed) {
        UltraIMEvent event;
        event.type = UltraIMEventType::ConversationUpdate;
        event.providerId = providerId_;
        event.conversationId = roomId;
        event.conversation = ConversationOf(room);
        events.push_back(event);
    }
}

void MatrixProvider::ApplyState(Room& room, const JSONValue& event) {
    const std::string type = event["type"].GetString();
    const JSONValue& content = event["content"];
    if (type == "m.room.name") {
        room.name = content["name"].GetString();
    } else if (type == "m.room.canonical_alias") {
        room.alias = content["alias"].GetString();
    } else if (type == "m.room.encryption") {
        room.encrypted = true;
        const int64_t ms = content["rotation_period_ms"].GetInteger(0);
        const int64_t msgs = content["rotation_period_msgs"].GetInteger(0);
        if (ms > 0) room.encryption.rotationPeriodMs = ms;
        if (msgs > 0) room.encryption.rotationPeriodMessages = static_cast<int>(msgs);
    } else if (type == "m.room.member") {
        const std::string user = event["state_key"].GetString();
        const std::string membership = content["membership"].GetString();
        if (membership == "join" || membership == "invite") room.recipients.insert(user);
        else room.recipients.erase(user);
        const std::string name = content["displayname"].GetString();
        if (membership == "join" || membership == "invite") {
            room.members[user] = !name.empty() ? name : (room.members.count(user) ? room.members[user] : Localpart(user));
            if (membership == "join") room.joined.insert(user);
            else room.joined.erase(user);
        } else {
            room.joined.erase(user);
            room.members.erase(user);
        }
    }
}

bool MatrixProvider::MessageFrom(const Room& room, const JSONValue& event, UltraIMMessage& out) const {
    if (event["unsigned"].Contains("redacted_because")) return false;
    const JSONValue& content = event["content"];
    out.messageId = event["event_id"].GetString();
    out.providerId = providerId_;
    out.conversationId = room.id;
    out.senderId = event["sender"].GetString();
    out.senderName = NameOf(room, out.senderId);
    out.timestampMs = event["origin_server_ts"].GetInteger(NowMs());
    out.isOwn = out.senderId == userId_;
    out.deliveryStatus = UltraIMDeliveryStatus::Sent;
    // Our own message comes back with the transaction id we sent it under,
    // which is the client message id: the optimistic row and the echo meet.
    const std::string txn = event["unsigned"]["transaction_id"].GetString();
    out.clientMessageId = !txn.empty() ? txn : out.messageId;

    if (event["type"].GetString() == "m.room.encrypted") {
        // Not decrypted (yet): the caller says why in the text.
        out.text = kWaitingForKey;
        return true;
    }
    // The newest edit, when the server bundles it.
    const JSONValue& replaced = event["unsigned"]["m.relations"]["m.replace"];
    const JSONValue& body = replaced.IsObject() && replaced["content"]["m.new_content"].IsObject()
                                ? replaced["content"]["m.new_content"]
                                : content;
    if (replaced.IsObject() && replaced["content"]["m.new_content"].IsObject()) out.isEdited = true;
    const std::string msgtype = body["msgtype"].GetString();
    const std::string text = body["body"].GetString();
    const JSONValue& relates = content["m.relates_to"];
    const std::string replyTo = relates["m.in_reply_to"]["event_id"].GetString();
    out.text = replyTo.empty() ? text : StripReplyFallback(text);
    if (msgtype == "m.emote") {
        out.text = "* " + out.senderName + " " + text;
    } else if (msgtype == "m.image" || msgtype == "m.file" || msgtype == "m.audio" || msgtype == "m.video") {
        out.type = msgtype == "m.image" ? UltraIMMessageType::Image
                 : msgtype == "m.audio" ? UltraIMMessageType::Audio
                 : msgtype == "m.video" ? UltraIMMessageType::Video
                                        : UltraIMMessageType::File;
        // The file: its URL in the clear, or inside the m.file object (the
        // key with it) in an encrypted room. With a `filename`, the body is
        // the caption (MSC2530); without, the body is the name.
        const JSONValue& file = body["file"];
        const JSONValue& info = body["info"];
        const std::string url = file.IsObject() ? file["url"].GetString() : body["url"].GetString();
        const std::string filename = body["filename"].GetString();
        out.attachment.fileName = filename.empty() ? text : filename;
        out.text = !filename.empty() && text != filename ? text : std::string();
        out.attachment.remoteUrl = url;
        out.attachment.mimeType = info["mimetype"].GetString();
        out.attachment.fileSizeBytes = info["size"].GetInteger(0);
        out.attachment.width = static_cast<int>(info["w"].GetInteger(0));
        out.attachment.height = static_cast<int>(info["h"].GetInteger(0));
        out.attachment.durationSeconds = static_cast<double>(info["duration"].GetInteger(0)) / 1000.0;
        const JSONValue& thumbnailFile = info["thumbnail_file"];
        const std::string thumbnailUrl =
            thumbnailFile.IsObject() ? thumbnailFile["url"].GetString() : info["thumbnail_url"].GetString();
        auto here = onDisk_.find(url);
        if (here != onDisk_.end()) {
            out.attachment.localPath = here->second;
            out.attachment.isDownloaded = true;
        }
        auto thumb = onDisk_.find(thumbnailUrl);
        if (thumb != onDisk_.end()) out.attachment.thumbnailPath = thumb->second;
        if (!url.empty()) {
            Media& media = media_[out.messageId];
            media.url = url;
            media.file = file.IsObject() ? file : JSONValue();
            media.thumbnailUrl = thumbnailUrl;
            media.thumbnailFile = thumbnailFile.IsObject() ? thumbnailFile : JSONValue();
            media.mimeType = out.attachment.mimeType;
            media.fileName = out.attachment.fileName;
        }
    }
    if (!replyTo.empty()) {
        out.replyTo.messageId = replyTo;
        for (const auto& m : room.timeline) {
            if (m.messageId != replyTo) continue;
            out.replyTo.senderId = m.senderId;
            out.replyTo.senderName = m.senderName;
            out.replyTo.textSnippet = SnippetOf(m).substr(0, 80);
            break;
        }
    }
    return true;
}

bool MatrixProvider::ApplyEvent(Room& room, const JSONValue& event, bool live, std::vector<UltraIMEvent>& events) {
    const std::string type = event["type"].GetString();
    const JSONValue& content = event["content"];
    if (event.Contains("state_key")) {
        // State in the timeline; a change of membership is also a line in it.
        const std::string user = event["state_key"].GetString();
        const std::string membership = content["membership"].GetString();
        const std::string before = event["unsigned"]["prev_content"]["membership"].GetString();
        ApplyState(room, event);
        if (type != "m.room.member" || membership == before || user == userId_) return false;
        UltraIMMessage line;
        line.messageId = event["event_id"].GetString();
        line.clientMessageId = line.messageId;
        line.providerId = providerId_;
        line.conversationId = room.id;
        line.senderId = event["sender"].GetString();
        line.senderName = NameOf(room, line.senderId);
        line.timestampMs = event["origin_server_ts"].GetInteger(NowMs());
        line.type = UltraIMMessageType::SystemEvent;
        const std::string who = room.members.count(user) ? room.members[user] : Localpart(user);
        // Joining is worth a line when it answers an invitation (the room's
        // creator joining it is not news).
        if (membership == "join" && before != "invite") return false;
        if (membership == "join") line.text = who + " joined";
        else if (membership == "invite") line.text = line.senderName + " invited " + who;
        else if (membership == "leave") line.text = (line.senderId == user ? Localpart(user) : who) + " left";
        else return false;
        room.timeline.push_back(line);
        if (live) {
            UltraIMEvent e;
            e.type = UltraIMEventType::MessageReceive;
            e.providerId = providerId_;
            e.conversationId = room.id;
            e.contactId = line.senderId;
            e.message = line;
            events.push_back(e);
        }
        return true;
    }
    if (type == "m.room.redaction") {
        const std::string target = content["redacts"].GetString(event["redacts"].GetString());
        auto it = std::find_if(room.timeline.begin(), room.timeline.end(),
                               [&](const UltraIMMessage& m) { return m.messageId == target; });
        if (it == room.timeline.end()) return false;
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageDelete;
        e.providerId = providerId_;
        e.conversationId = room.id;
        e.message = *it;
        room.timeline.erase(it);
        if (live) events.push_back(e);
        return true;
    }
    if (type == "m.room.encrypted") {
        JSONValue clear;
        std::string placeholder;
        if (DecryptForTimeline(room, event, clear, placeholder)) return ApplyEvent(room, clear, live, events);
        UltraIMMessage message;
        if (!MessageFrom(room, event, message)) return false;
        message.text = placeholder;
        for (const auto& m : room.timeline) {
            if (m.messageId == message.messageId) return false;
        }
        room.timeline.push_back(message);
        if (live) {
            UltraIMEvent e;
            e.type = UltraIMEventType::MessageReceive;
            e.providerId = providerId_;
            e.conversationId = room.id;
            e.contactId = message.senderId;
            e.message = message;
            events.push_back(e);
        }
        return true;
    }
    if (type != "m.room.message") return false;
    const JSONValue& relates = content["m.relates_to"];
    if (relates["rel_type"].GetString() == "m.replace") {
        const std::string target = relates["event_id"].GetString();
        for (auto& m : room.timeline) {
            if (m.messageId != target || m.senderId != event["sender"].GetString()) continue;
            m.text = content["m.new_content"]["body"].GetString(m.text);
            m.isEdited = true;
            if (live) {
                UltraIMEvent e;
                e.type = UltraIMEventType::MessageUpdate;
                e.providerId = providerId_;
                e.conversationId = room.id;
                e.message = m;
                events.push_back(e);
            }
            return true;
        }
        return false;
    }
    UltraIMMessage message;
    if (!MessageFrom(room, event, message)) return false;
    for (const auto& m : room.timeline) {
        if (m.messageId == message.messageId) return false;      // already have it
    }
    room.timeline.push_back(message);
    QueueMedia(message);
    if (room.timeline.size() > kTimelineCap) {
        media_.erase(room.timeline.front().messageId);
        room.timeline.erase(room.timeline.begin());
        room.historyComplete = false;
    }
    if (live) {
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageReceive;
        e.providerId = providerId_;
        e.conversationId = room.id;
        e.contactId = message.senderId;
        e.message = message;
        events.push_back(e);
        if (!message.isOwn) room.typing.erase(message.senderId);
    }
    return true;
}

void MatrixProvider::ApplyReceipts(Room& room, const JSONValue& content, std::vector<UltraIMEvent>& events) {
    std::vector<std::pair<std::string, std::string>> receipts;     // user, event
    for (const auto& entry : content.GetMembers()) {
        for (const char* kind : {"m.read", "m.read.private"}) {
            for (const auto& reader : entry.second[kind].GetMembers()) receipts.push_back({reader.first, entry.first});
        }
    }
    for (const auto& receipt : receipts) {
        room.receipts[receipt.first] = receipt.second;
        if (receipt.first == userId_) continue;
        // Someone else read up to here: our messages before it are read. In a
        // group "read" means read by someone.
        auto upTo = std::find_if(room.timeline.begin(), room.timeline.end(),
                                 [&](const UltraIMMessage& m) { return m.messageId == receipt.second; });
        if (upTo == room.timeline.end()) continue;
        for (auto it = room.timeline.begin(); it != upTo + 1; ++it) {
            if (!it->isOwn || it->deliveryStatus == UltraIMDeliveryStatus::Read) continue;
            it->deliveryStatus = UltraIMDeliveryStatus::Read;
            UltraIMEvent e;
            e.type = UltraIMEventType::MessageDelivered;
            e.providerId = providerId_;
            e.conversationId = room.id;
            e.message = *it;
            e.deliveryStatus = UltraIMDeliveryStatus::Read;
            events.push_back(e);
        }
    }
}

// ============================================================================
// VIEWS OF THE STATE (mutex held)
// ============================================================================

std::string MatrixProvider::NameOf(const Room& room, const std::string& userId) const {
    auto it = room.members.find(userId);
    if (it != room.members.end() && !it->second.empty()) return it->second;
    return Localpart(userId);
}

std::string MatrixProvider::DirectUserOf(const Room& room) const {
    for (const auto& entry : direct_) {
        if (std::find(entry.second.begin(), entry.second.end(), room.id) != entry.second.end()) return entry.first;
    }
    // Not marked direct, but just the two of us and no name: a direct chat.
    if (room.name.empty() && room.alias.empty() && room.members.size() == 2) {
        for (const auto& m : room.members) {
            if (m.first != userId_) return m.first;
        }
    }
    if (room.invited && room.name.empty() && !room.inviter.empty() && room.members.size() <= 2) return room.inviter;
    return std::string();
}

UltraIMConversation MatrixProvider::ConversationOf(const Room& room) const {
    UltraIMConversation c;
    c.conversationId = room.id;
    c.providerId = providerId_;
    c.directContactId = DirectUserOf(room);
    c.isGroup = c.directContactId.empty();
    if (!room.name.empty()) c.title = room.name;
    else if (!room.alias.empty()) c.title = room.alias;
    else if (!c.directContactId.empty()) c.title = NameOf(room, c.directContactId);
    else {
        std::vector<std::string> names;
        for (const auto& h : room.heroes) names.push_back(NameOf(room, h));
        if (names.empty()) {
            for (const auto& m : room.members) {
                if (m.first != userId_) names.push_back(m.second);
            }
        }
        for (size_t i = 0; i < names.size() && i < 3; ++i) c.title += (i ? ", " : "") + names[i];
        if (c.title.empty()) c.title = "Empty room";
    }
    for (const auto& m : room.joined) c.memberIds.push_back(m);
    c.isEncrypted = room.encrypted;
    if (!c.directContactId.empty()) {
        auto p = presence_.find(c.directContactId);
        if (p != presence_.end()) c.presence = p->second;
    }
    if (room.invited) {
        c.lastMessageSnippet = "Invitation from " + (room.inviter.empty() ? std::string("someone") : NameOf(room, room.inviter));
        c.unreadCount = 1;
    } else {
        for (auto it = room.timeline.rbegin(); it != room.timeline.rend(); ++it) {
            c.lastMessageSnippet = SnippetOf(*it);
            c.lastMessageTimeMs = it->timestampMs;
            break;
        }
        c.unreadCount = room.unread;
    }
    return c;
}

// ============================================================================
// DATA
// ============================================================================

void MatrixProvider::FetchConversations(UltraIMConversationsCallback onResult) {
    std::vector<UltraIMConversation> list;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : rooms_) list.push_back(ConversationOf(entry.second));
    }
    std::sort(list.begin(), list.end(), [](const UltraIMConversation& a, const UltraIMConversation& b) {
        return a.lastMessageTimeMs > b.lastMessageTimeMs;
    });
    onResult(UltraIMResult::Ok(), list);
}

void MatrixProvider::FetchContacts(UltraIMContactsCallback onResult) {
    std::vector<UltraIMContact> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::set<std::string> seen;
        for (const auto& entry : rooms_) {
            const std::string user = DirectUserOf(entry.second);
            if (user.empty() || !seen.insert(user).second) continue;
            UltraIMContact contact;
            contact.contactId = user;
            contact.providerId = providerId_;
            contact.displayName = NameOf(entry.second, user);
            auto p = presence_.find(user);
            if (p != presence_.end()) contact.presence = p->second;
            out.push_back(contact);
        }
    }
    onResult(UltraIMResult::Ok(), out);
}

void MatrixProvider::FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                                   UltraIMMessagesCallback onResult) {
    if (limit <= 0) limit = 50;
    auto slice = [this, conversationId, beforeMessageId, limit]() {
        std::vector<UltraIMMessage> out;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = rooms_.find(conversationId);
        if (it == rooms_.end()) return out;
        const auto& timeline = it->second.timeline;
        size_t end = timeline.size();
        if (!beforeMessageId.empty()) {
            for (size_t i = 0; i < timeline.size(); ++i) {
                if (timeline[i].messageId == beforeMessageId || timeline[i].clientMessageId == beforeMessageId) {
                    end = i;
                    break;
                }
            }
        }
        const size_t begin = end > static_cast<size_t>(limit) ? end - static_cast<size_t>(limit) : 0;
        out.assign(timeline.begin() + static_cast<std::ptrdiff_t>(begin),
                   timeline.begin() + static_cast<std::ptrdiff_t>(end));
        return out;
    };

    std::string from;
    int missing = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = rooms_.find(conversationId);
        if (it == rooms_.end()) {
            onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No such room"), {});
            return;
        }
        const Room& room = it->second;
        size_t available = room.timeline.size();
        if (!beforeMessageId.empty()) {
            for (size_t i = 0; i < room.timeline.size(); ++i) {
                if (room.timeline[i].messageId == beforeMessageId) {
                    available = i;
                    break;
                }
            }
        }
        if (available < static_cast<size_t>(limit) && !room.historyComplete && !room.prevBatch.empty() &&
            !room.invited) {
            from = room.prevBatch;
            missing = limit - static_cast<int>(available);
        }
    }
    std::shared_ptr<MatrixHttp> http = Http();
    if (from.empty() || !http) {
        onResult(UltraIMResult::Ok(), slice());
        return;
    }
    // Older than we hold: page back from where the timeline starts.
    const std::string path = std::string(kClient) + "/rooms/" + PathSegment(conversationId) +
                             "/messages?dir=b&limit=" + std::to_string(missing + 10) + "&from=" + PathSegment(from);
    http->Request("GET", path, nullptr, 20000, [this, conversationId, from, slice, onResult](const MatrixResponse& r) {
        if (r.Ok()) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = rooms_.find(conversationId);
            if (it != rooms_.end() && it->second.prevBatch == from) {
                Room& room = it->second;
                for (const auto& event : r.body["state"].GetElements()) ApplyState(room, event);
                std::vector<UltraIMMessage> older;
                for (const auto& raw : r.body["chunk"].GetElements()) {     // newest first
                    JSONValue clear;
                    std::string placeholder;
                    const bool encrypted = raw["type"].GetString() == "m.room.encrypted";
                    const bool readable = encrypted && DecryptForTimeline(room, raw, clear, placeholder);
                    const JSONValue& event = readable ? clear : raw;
                    const std::string type = event["type"].GetString();
                    if (type != "m.room.message" && type != "m.room.encrypted") continue;
                    if (event["content"]["m.relates_to"]["rel_type"].GetString() == "m.replace") continue;
                    UltraIMMessage m;
                    if (!MessageFrom(room, event, m)) continue;
                    if (encrypted && !readable) m.text = placeholder;
                    bool known = false;
                    for (const auto& have : room.timeline) known = known || have.messageId == m.messageId;
                    if (!known) older.push_back(m);
                }
                std::reverse(older.begin(), older.end());
                for (const auto& m : older) QueueMedia(m);
                room.timeline.insert(room.timeline.begin(), older.begin(), older.end());
                const std::string end = r.body["end"].GetString();
                if (end.empty() || r.body["chunk"].GetElements().empty()) room.historyComplete = true;
                room.prevBatch = end;
            }
        }
        FlushDownloads();
        onResult(r.Ok() ? UltraIMResult::Ok() : HttpError(r, UltraIMResultCode::NetworkError), slice());
    });
}

// ============================================================================
// ROOMS
// ============================================================================

void MatrixProvider::SetDirect(const std::string& userId, const std::string& roomId) {
    JSONValue content = JSONValue::MakeObject();
    std::string me;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& rooms = direct_[userId];
        if (std::find(rooms.begin(), rooms.end(), roomId) == rooms.end()) rooms.push_back(roomId);
        for (const auto& entry : direct_) {
            JSONValue list = JSONValue::MakeArray();
            for (const auto& r : entry.second) list.Append(JSONValue(r));
            content.Set(entry.first, list);
        }
        me = userId_;
    }
    if (std::shared_ptr<MatrixHttp> http = Http())
        http->Request("PUT", std::string(kClient) + "/user/" + PathSegment(me) + "/account_data/m.direct", &content,
                      15000, [](const MatrixResponse&) {});
}

void MatrixProvider::AddContact(const std::string& address, UltraIMConversationCallback onResult) {
    const std::string user = Trim(address);
    if (!IsUserId(user)) {
        onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "A Matrix ID looks like @name:server"), {});
        return;
    }
    std::shared_ptr<MatrixHttp> http = Http();
    if (!http || state_ != UltraIMConnectionState::Connected) {
        onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Matrix"), {});
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (user == userId_) {
            onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "That is your own Matrix ID"), {});
            return;
        }
        // A direct chat with them already: that one.
        auto d = direct_.find(user);
        if (d != direct_.end()) {
            for (const auto& roomId : d->second) {
                auto r = rooms_.find(roomId);
                if (r != rooms_.end()) {
                    UltraIMConversation c = ConversationOf(r->second);
                    onResult(UltraIMResult::Ok(), c);
                    return;
                }
            }
        }
    }
    JSONValue body = JSONValue::MakeObject();
    body.Set("preset", JSONValue("trusted_private_chat"));
    body.Set("is_direct", JSONValue(true));
    const bool encrypt = crypto_.IsOpen();
    if (encrypt) {
        JSONValue encryption = JSONValue::MakeObject();
        encryption.Set("type", JSONValue("m.room.encryption"));
        encryption.Set("state_key", JSONValue(""));
        JSONValue content = JSONValue::MakeObject();
        content.Set("algorithm", JSONValue("m.megolm.v1.aes-sha2"));
        encryption.Set("content", content);
        JSONValue initial = JSONValue::MakeArray();
        initial.Append(encryption);
        body.Set("initial_state", initial);
    }
    JSONValue invite = JSONValue::MakeArray();
    invite.Append(JSONValue(user));
    body.Set("invite", invite);
    http->Request("POST", std::string(kClient) + "/createRoom", &body, 20000,
                  [this, http, user, encrypt, onResult](const MatrixResponse& r) {
        if (!r.Ok()) return onResult(HttpError(r, UltraIMResultCode::Unknown), {});
        const std::string roomId = r.body["room_id"].GetString();
        SetDirect(user, roomId);
        // Their name, for the title until the sync brings the room.
        http->Request("GET", std::string(kClient) + "/profile/" + PathSegment(user) + "/displayname", nullptr, 10000,
                      [this, user, roomId, encrypt, onResult](const MatrixResponse& p) {
            UltraIMConversation c;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                Room& room = rooms_[roomId];
                room.id = roomId;
                room.encrypted = room.encrypted || encrypt;
                room.joined.insert(userId_);
                if (!room.members.count(userId_)) room.members[userId_] = Localpart(userId_);
                const std::string name = p.Ok() ? p.body["displayname"].GetString() : std::string();
                if (!room.members.count(user) || !name.empty()) room.members[user] = name.empty() ? Localpart(user) : name;
                c = ConversationOf(room);
            }
            UltraIMEvent event;
            event.type = UltraIMEventType::ConversationUpdate;
            event.providerId = providerId_;
            event.conversationId = roomId;
            event.conversation = c;
            Emit(event);
            onResult(UltraIMResult::Ok(), c);
        });
    });
}

void MatrixProvider::CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                                 UltraIMConversationCallback onResult) {
    std::shared_ptr<MatrixHttp> http = Http();
    if (!http || state_ != UltraIMConnectionState::Connected) {
        onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Matrix"), {});
        return;
    }
    JSONValue body = JSONValue::MakeObject();
    body.Set("preset", JSONValue("private_chat"));
    body.Set("name", JSONValue(title));
    const bool encrypt = crypto_.IsOpen();
    if (encrypt) {
        JSONValue encryption = JSONValue::MakeObject();
        encryption.Set("type", JSONValue("m.room.encryption"));
        encryption.Set("state_key", JSONValue(""));
        JSONValue content = JSONValue::MakeObject();
        content.Set("algorithm", JSONValue("m.megolm.v1.aes-sha2"));
        encryption.Set("content", content);
        JSONValue initial = JSONValue::MakeArray();
        initial.Append(encryption);
        body.Set("initial_state", initial);
    }
    JSONValue invite = JSONValue::MakeArray();
    for (const auto& m : memberIds) {
        const std::string user = Trim(m);
        if (!IsUserId(user)) {
            onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, user + " is not a Matrix ID (@name:server)"), {});
            return;
        }
        invite.Append(JSONValue(user));
    }
    body.Set("invite", invite);
    http->Request("POST", std::string(kClient) + "/createRoom", &body, 20000,
                  [this, title, memberIds, encrypt, onResult](const MatrixResponse& r) {
        if (!r.Ok()) return onResult(HttpError(r, UltraIMResultCode::Unknown), {});
        const std::string roomId = r.body["room_id"].GetString();
        UltraIMConversation c;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Room& room = rooms_[roomId];
            room.id = roomId;
            room.name = title;
            room.encrypted = room.encrypted || encrypt;
            room.joined.insert(userId_);
            room.members[userId_] = Localpart(userId_);
            for (const auto& m : memberIds) room.members[Trim(m)] = Localpart(Trim(m));
            c = ConversationOf(room);
        }
        UltraIMEvent event;
        event.type = UltraIMEventType::ConversationUpdate;
        event.providerId = providerId_;
        event.conversationId = roomId;
        event.conversation = c;
        Emit(event);
        onResult(UltraIMResult::Ok(), c);
    });
}

// ============================================================================
// SENDING
// ============================================================================

UltraIMResult MatrixProvider::SendText(const std::string& conversationId, const std::string& text,
                                       const UltraIMSendOptions& options) {
    std::shared_ptr<MatrixHttp> http = Http();
    if (!http || state_ == UltraIMConnectionState::Disconnected)
        return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Matrix");
    bool invited = false;
    bool encrypted = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = rooms_.find(conversationId);
        if (it == rooms_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such room");
        invited = it->second.invited;
        encrypted = it->second.encrypted;
    }
    if (encrypted && !crypto_.IsOpen())
        return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing,
                                    "This Matrix room is end-to-end encrypted: " + GetEncryptionError());
    const std::string clientId = options.clientMessageId.empty() ? UltraIM_NewClientMessageId() : options.clientMessageId;
    JSONValue body = JSONValue::MakeObject();
    body.Set("msgtype", JSONValue("m.text"));
    body.Set("body", JSONValue(text));
    if (!options.replyToMessageId.empty()) {
        JSONValue relates = JSONValue::MakeObject();
        JSONValue inReplyTo = JSONValue::MakeObject();
        inReplyTo.Set("event_id", JSONValue(options.replyToMessageId));
        relates.Set("m.in_reply_to", inReplyTo);
        body.Set("m.relates_to", relates);
    }
    // The transaction id is the client message id: a retry is the same
    // message to the server, and our echo comes back carrying it.
    const std::string path = std::string(kClient) + "/rooms/" + PathSegment(conversationId) +
                             "/send/m.room.message/" + PathSegment(clientId);
    auto delivered = [this, conversationId, clientId](UltraIMDeliveryStatus status, const std::string& eventId) {
        UltraIMEvent event;
        event.type = UltraIMEventType::MessageDelivered;
        event.providerId = providerId_;
        event.conversationId = conversationId;
        event.message.conversationId = conversationId;
        event.message.clientMessageId = clientId;
        event.message.messageId = eventId.empty() ? clientId : eventId;
        event.deliveryStatus = status;
        Emit(event);
    };
    auto send = [http, path, body, delivered]() {
        http->Request("PUT", path, &body, 30000, [delivered](const MatrixResponse& r) {
            if (r.Ok()) delivered(UltraIMDeliveryStatus::Sent, r.body["event_id"].GetString());
            else delivered(UltraIMDeliveryStatus::Failed, std::string());
        });
    };
    if (encrypted) {
        // Sharing the room's key waits on the server: the send thread, in order.
        Enqueue([this, http, conversationId, clientId, body, invited, delivered]() {
            if (invited) {
                MatrixResponse joined = http->Await("POST", std::string(kClient) + "/join/" + PathSegment(conversationId),
                                                    nullptr, 20000);
                if (!joined.Ok()) return delivered(UltraIMDeliveryStatus::Failed, std::string());
            }
            std::string eventId, error;
            if (SendEncrypted(http, conversationId, clientId, "m.room.message", body, eventId, error))
                delivered(UltraIMDeliveryStatus::Sent, eventId);
            else
                delivered(UltraIMDeliveryStatus::Failed, std::string());
        });
        return UltraIMResult::Ok();
    }
    if (!invited) {
        send();
        return UltraIMResult::Ok();
    }
    // An invitation: answering it accepts it.
    http->Request("POST", std::string(kClient) + "/join/" + PathSegment(conversationId), nullptr, 20000,
                  [send, delivered](const MatrixResponse& r) {
        if (r.Ok()) send();
        else delivered(UltraIMDeliveryStatus::Failed, std::string());
    });
    return UltraIMResult::Ok();
}

void MatrixProvider::Delivered(const std::string& conversationId, const std::string& clientId,
                               UltraIMDeliveryStatus status, const std::string& eventId) {
    UltraIMEvent event;
    event.type = UltraIMEventType::MessageDelivered;
    event.providerId = providerId_;
    event.conversationId = conversationId;
    event.message.conversationId = conversationId;
    event.message.clientMessageId = clientId;
    event.message.messageId = eventId.empty() ? clientId : eventId;
    event.deliveryStatus = status;
    Emit(event);
}

// ============================================================================
// MEDIA
// A message's picture, video or file lives in the content repository under
// an mxc URL; in an encrypted room the bytes there are AES-CTR ciphertext
// and the message carries the key (the m.file object). A photo, and the
// preview picture of a video or a file, are fetched as the message is kept
// (QueueMedia); a video or a file when the application asks
// (DownloadAttachment). Fetched files go to the cache directory under the
// URL's hash, and a file sent from here is remembered under its URL so the
// echo points at it.
// ============================================================================

std::string MatrixProvider::MediaPath(const std::string& url, const std::string& mimeType,
                                      const std::string& fileName) const {
    namespace fs = std::filesystem;
    const std::string base = config_.cacheDirectory.empty() ? config_.dataDirectory : config_.cacheDirectory;
    const fs::path dir = UltraCanvas::PathFromUtf8(base) / UltraCanvas::PathFromUtf8(providerId_) / "media";
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::vector<uint8_t> digest;
    UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, url.data(), url.size(), digest);
    const std::string name = UltraCrypt_ToHex(digest).substr(0, 32) + ExtensionFor(mimeType, fileName);
    return UltraCanvas::PathToUtf8(dir / UltraCanvas::PathFromUtf8(name));
}

void MatrixProvider::QueueMedia(const UltraIMMessage& message) {
    auto it = media_.find(message.messageId);
    if (it == media_.end()) return;
    const Media& media = it->second;
    MediaUse use;
    use.roomId = message.conversationId;
    use.eventId = message.messageId;
    if (message.type == UltraIMMessageType::Image) {
        if (!message.attachment.isDownloaded) QueueDownload(media.url, media, use);
        return;
    }
    if (message.attachment.thumbnailPath.empty() && !media.thumbnailUrl.empty()) {
        use.thumbnail = true;
        Media preview;
        preview.url = media.thumbnailUrl;
        preview.file = media.thumbnailFile;
        preview.mimeType = "image/jpeg";
        QueueDownload(media.thumbnailUrl, preview, use);
    }
}

void MatrixProvider::QueueDownload(const std::string& url, const Media& media, const MediaUse& use) {
    if (url.empty() || onDisk_.count(url)) return;
    auto& uses = mediaUses_[url];
    bool known = false;
    for (const auto& u : uses) known = known || (u.eventId == use.eventId && u.thumbnail == use.thumbnail);
    if (!known) uses.push_back(use);
    if (downloading_.count(url)) return;
    downloading_.insert(url);
    DownloadJob job;
    job.url = url;
    job.file = media.file;
    job.mimeType = media.mimeType;
    job.fileName = media.fileName;
    downloadQueue_.push_back(job);
}

void MatrixProvider::FlushDownloads() {
    std::vector<DownloadJob> queue;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue.swap(downloadQueue_);
    }
    for (const auto& job : queue) StartDownload(job, false);
}

void MatrixProvider::StartDownload(const DownloadJob& job, bool legacyEndpoint) {
    std::shared_ptr<MatrixHttp> http = Http();
    std::string server, id;
    if (!http || !SplitMxc(job.url, server, id)) {
        MediaFailed(job.url, http ? "not an mxc URL" : "not signed in");
        return;
    }
    // The authenticated endpoint (Matrix 1.11), else the one before it,
    // which an older homeserver still serves.
    const std::string path = legacyEndpoint
        ? "/_matrix/media/v3/download/" + PathSegment(server) + "/" + PathSegment(id)
        : "/_matrix/client/v1/media/download/" + PathSegment(server) + "/" + PathSegment(id);
    http->Download(path, 120000, [this, job, legacyEndpoint](const MatrixBytes& r) {
        if (!r.Ok() && !legacyEndpoint && (r.errcode == "M_UNRECOGNIZED" || r.status == 404 || r.status == 400)) {
            StartDownload(job, true);
            return;
        }
        OnMediaDownloaded(job, r);
    });
}

void MatrixProvider::MediaFailed(const std::string& url, const std::string& why) {
    std::vector<UltraIMAttachmentCallback> waiters;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        downloading_.erase(url);
        mediaUses_.erase(url);
        auto it = mediaWaiters_.find(url);
        if (it != mediaWaiters_.end()) {
            waiters = std::move(it->second);
            mediaWaiters_.erase(it);
        }
    }
    for (const auto& waiter : waiters)
        waiter(UltraIMResult::Error(UltraIMResultCode::NetworkError, "Matrix could not fetch the file: " + why),
               UltraIMAttachment());
}

void MatrixProvider::OnMediaDownloaded(const DownloadJob& job, const MatrixBytes& r) {
    if (!r.Ok()) {
        MediaFailed(job.url, r.error);
        return;
    }
    std::vector<uint8_t> bytes = r.body;
    std::string error;
    if (job.file.IsObject() && !DecryptMedia(job.file, bytes, error)) {
        MediaFailed(job.url, error);
        return;
    }
    const std::string path = MediaPath(job.url, job.mimeType, job.fileName);
    if (!WriteWholeFile(path, bytes)) {
        MediaFailed(job.url, "could not write " + path);
        return;
    }
    std::vector<UltraIMEvent> events;
    std::vector<UltraIMAttachmentCallback> waiters;
    UltraIMAttachment handed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        onDisk_[job.url] = path;
        downloading_.erase(job.url);
        auto uses = mediaUses_.find(job.url);
        if (uses != mediaUses_.end()) {
            for (const MediaUse& use : uses->second) {
                auto room = rooms_.find(use.roomId);
                if (room == rooms_.end()) continue;
                for (auto& m : room->second.timeline) {
                    if (m.messageId != use.eventId) continue;
                    if (use.thumbnail) {
                        m.attachment.thumbnailPath = path;
                    } else {
                        m.attachment.localPath = path;
                        m.attachment.isDownloaded = true;
                        if (!m.attachment.fileSizeBytes) m.attachment.fileSizeBytes = static_cast<int64_t>(bytes.size());
                        handed = m.attachment;
                    }
                    UltraIMEvent e;
                    e.type = UltraIMEventType::MessageUpdate;
                    e.providerId = providerId_;
                    e.conversationId = room->second.id;
                    e.contactId = m.senderId;
                    e.message = m;
                    events.push_back(e);
                    break;
                }
            }
            mediaUses_.erase(uses);
        }
        auto it = mediaWaiters_.find(job.url);
        if (it != mediaWaiters_.end()) {
            waiters = std::move(it->second);
            mediaWaiters_.erase(it);
        }
    }
    if (!waiters.empty() && handed.localPath.empty()) {
        handed.localPath = path;
        handed.isDownloaded = true;
        handed.remoteUrl = job.url;
        handed.mimeType = job.mimeType;
        handed.fileName = job.fileName;
        handed.fileSizeBytes = static_cast<int64_t>(bytes.size());
    }
    for (const auto& e : events) Emit(e);
    for (const auto& waiter : waiters) waiter(UltraIMResult::Ok(), handed);
}

void MatrixProvider::DownloadAttachment(const std::string& conversationId, const std::string& messageId,
                                        UltraIMAttachmentCallback onResult) {
    auto finish = [onResult](const UltraIMResult& r, const UltraIMAttachment& a) { if (onResult) onResult(r, a); };
    if (!Http() || state_ == UltraIMConnectionState::Disconnected) {
        finish(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Matrix"), UltraIMAttachment());
        return;
    }
    UltraIMAttachment attachment;
    bool found = false, here = false, queued = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto room = rooms_.find(conversationId);
        if (room != rooms_.end()) {
            for (const auto& m : room->second.timeline) {
                if (m.messageId != messageId) continue;
                found = true;
                attachment = m.attachment;
                here = attachment.isDownloaded && !attachment.localPath.empty();
                break;
            }
        }
        auto media = media_.find(messageId);
        if (found && !here && media != media_.end() && !media->second.url.empty()) {
            MediaUse use;
            use.roomId = conversationId;
            use.eventId = messageId;
            mediaWaiters_[media->second.url].push_back(finish);
            QueueDownload(media->second.url, media->second, use);
            queued = true;
        }
    }
    if (here) {
        finish(UltraIMResult::Ok(), attachment);
        return;
    }
    if (!queued) {
        finish(UltraIMResult::Error(UltraIMResultCode::NotFound,
                                    found ? "That message has no attachment" : "No such message"),
               UltraIMAttachment());
        return;
    }
    FlushDownloads();
}

UltraIMResult MatrixProvider::SendImage(const std::string& conversationId, const std::string& localPath,
                                        const std::string& caption, const UltraIMSendOptions& options) {
    return SendMedia(conversationId, localPath, caption, UltraIMMessageType::Image, options);
}

UltraIMResult MatrixProvider::SendVideo(const std::string& conversationId, const std::string& localPath,
                                        const std::string& caption, const UltraIMSendOptions& options) {
    return SendMedia(conversationId, localPath, caption, UltraIMMessageType::Video, options);
}

UltraIMResult MatrixProvider::SendFile(const std::string& conversationId, const std::string& localPath,
                                       const UltraIMSendOptions& options) {
    return SendMedia(conversationId, localPath, std::string(), UltraIMMessageType::File, options);
}

UltraIMResult MatrixProvider::SendMedia(const std::string& conversationId, const std::string& localPath,
                                        const std::string& caption, UltraIMMessageType type,
                                        const UltraIMSendOptions& options) {
    std::shared_ptr<MatrixHttp> http = Http();
    if (!http || state_ == UltraIMConnectionState::Disconnected)
        return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Matrix");
    bool invited = false;
    bool encrypted = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = rooms_.find(conversationId);
        if (it == rooms_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such room");
        invited = it->second.invited;
        encrypted = it->second.encrypted;
    }
    if (encrypted && !crypto_.IsOpen())
        return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing,
                                    "This Matrix room is end-to-end encrypted: " + GetEncryptionError());
    auto bytes = std::make_shared<std::vector<uint8_t>>();
    if (localPath.empty() || !ReadWholeFile(localPath, *bytes))
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "There is no file " + localPath);
    const std::string clientId = options.clientMessageId.empty() ? UltraIM_NewClientMessageId() : options.clientMessageId;
    const std::string name = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(localPath).filename());
    const std::string mime = MimeFor(localPath);
    const std::string msgtype = type == UltraIMMessageType::Image ? "m.image"
                              : type == UltraIMMessageType::Video ? "m.video"
                              : type == UltraIMMessageType::Audio ? "m.audio"
                                                                  : "m.file";
    const std::string replyTo = options.replyToMessageId;
    // The upload waits on the server, and so does the room key in an
    // encrypted room: the send thread, in order.
    Enqueue([this, http, conversationId, clientId, bytes, name, mime, msgtype, caption, replyTo, invited, encrypted,
             localPath]() {
        if (invited) {
            MatrixResponse joined = http->Await("POST", std::string(kClient) + "/join/" + PathSegment(conversationId),
                                                nullptr, 20000);
            if (!joined.Ok()) return Delivered(conversationId, clientId, UltraIMDeliveryStatus::Failed, std::string());
        }
        const int64_t size = static_cast<int64_t>(bytes->size());
        JSONValue fileInfo;
        std::string error;
        if (encrypted && !EncryptMedia(*bytes, fileInfo, error))
            return Delivered(conversationId, clientId, UltraIMDeliveryStatus::Failed, std::string());
        MatrixResponse up = http->Upload("/_matrix/media/v3/upload?filename=" + PathSegment(name),
                                         encrypted ? std::string("application/octet-stream") : mime, *bytes, 300000);
        bytes->clear();
        const std::string mxc = up.body["content_uri"].GetString();
        if (!up.Ok() || mxc.empty()) return Delivered(conversationId, clientId, UltraIMDeliveryStatus::Failed, std::string());
        {
            // The echo's attachment points at the file sent.
            std::lock_guard<std::mutex> lock(mutex_);
            onDisk_[mxc] = localPath;
        }
        JSONValue body = JSONValue::MakeObject();
        body.Set("msgtype", JSONValue(msgtype));
        body.Set("body", JSONValue(caption.empty() ? name : caption));
        body.Set("filename", JSONValue(name));
        JSONValue info = JSONValue::MakeObject();
        info.Set("mimetype", JSONValue(mime));
        info.Set("size", JSONValue(size));
        body.Set("info", info);
        if (encrypted) {
            fileInfo.Set("url", JSONValue(mxc));
            body.Set("file", fileInfo);
        } else {
            body.Set("url", JSONValue(mxc));
        }
        if (!replyTo.empty()) {
            JSONValue relates = JSONValue::MakeObject();
            JSONValue inReplyTo = JSONValue::MakeObject();
            inReplyTo.Set("event_id", JSONValue(replyTo));
            relates.Set("m.in_reply_to", inReplyTo);
            body.Set("m.relates_to", relates);
        }
        std::string eventId;
        if (encrypted) {
            if (SendEncrypted(http, conversationId, clientId, "m.room.message", body, eventId, error))
                Delivered(conversationId, clientId, UltraIMDeliveryStatus::Sent, eventId);
            else
                Delivered(conversationId, clientId, UltraIMDeliveryStatus::Failed, std::string());
            return;
        }
        MatrixResponse sent = http->Await("PUT", std::string(kClient) + "/rooms/" + PathSegment(conversationId) +
                                                     "/send/m.room.message/" + PathSegment(clientId),
                                          &body, 30000);
        if (sent.Ok()) Delivered(conversationId, clientId, UltraIMDeliveryStatus::Sent, sent.body["event_id"].GetString());
        else Delivered(conversationId, clientId, UltraIMDeliveryStatus::Failed, std::string());
    });
    return UltraIMResult::Ok();
}

UltraIMResult MatrixProvider::EditMessage(const std::string& conversationId, const std::string& messageId,
                                          const std::string& newText) {
    std::shared_ptr<MatrixHttp> http = Http();
    if (!http) return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Matrix");
    JSONValue fresh = JSONValue::MakeObject();
    fresh.Set("msgtype", JSONValue("m.text"));
    fresh.Set("body", JSONValue(newText));
    JSONValue body = JSONValue::MakeObject();
    body.Set("msgtype", JSONValue("m.text"));
    body.Set("body", JSONValue("* " + newText));          // what an older client shows
    body.Set("m.new_content", fresh);
    JSONValue relates = JSONValue::MakeObject();
    relates.Set("rel_type", JSONValue("m.replace"));
    relates.Set("event_id", JSONValue(messageId));
    body.Set("m.relates_to", relates);
    bool encrypted = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = rooms_.find(conversationId);
        encrypted = it != rooms_.end() && it->second.encrypted;
    }
    if (encrypted) {
        if (!crypto_.IsOpen())
            return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing,
                                        "This Matrix room is end-to-end encrypted: " + GetEncryptionError());
        Enqueue([this, http, conversationId, body]() {
            std::string eventId, error;
            SendEncrypted(http, conversationId, UltraIM_NewClientMessageId(), "m.room.message", body, eventId, error);
        });
        return UltraIMResult::Ok();
    }
    // The edit arrives back through the sync as a MessageUpdate.
    http->Request("PUT", std::string(kClient) + "/rooms/" + PathSegment(conversationId) + "/send/m.room.message/" +
                             PathSegment(UltraIM_NewClientMessageId()),
                  &body, 30000, [](const MatrixResponse&) {});
    return UltraIMResult::Ok();
}

UltraIMResult MatrixProvider::DeleteMessage(const std::string& conversationId, const std::string& messageId) {
    std::shared_ptr<MatrixHttp> http = Http();
    if (!http) return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Matrix");
    // The redaction arrives back through the sync as a MessageDelete.
    http->Request("PUT", std::string(kClient) + "/rooms/" + PathSegment(conversationId) + "/redact/" +
                             PathSegment(messageId) + "/" + PathSegment(UltraIM_NewClientMessageId()),
                  nullptr, 30000, [](const MatrixResponse&) {});
    return UltraIMResult::Ok();
}

UltraIMResult MatrixProvider::MarkRead(const std::string& conversationId, const std::string& upToMessageId) {
    std::shared_ptr<MatrixHttp> http = Http();
    if (!http) return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Matrix");
    std::string eventId = upToMessageId;
    UltraIMConversation c;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = rooms_.find(conversationId);
        if (it == rooms_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such room");
        Room& room = it->second;
        if (room.invited) return UltraIMResult::Ok();
        if (eventId.empty()) {
            for (auto m = room.timeline.rbegin(); m != room.timeline.rend(); ++m) {
                if (!m->messageId.empty() && m->messageId[0] == '$') {
                    eventId = m->messageId;
                    break;
                }
            }
        }
        if (eventId.empty()) return UltraIMResult::Ok();
        room.unread = 0;
        c = ConversationOf(room);
    }
    JSONValue body = JSONValue::MakeObject();
    body.Set("m.fully_read", JSONValue(eventId));
    body.Set("m.read", JSONValue(eventId));
    http->Request("POST", std::string(kClient) + "/rooms/" + PathSegment(conversationId) + "/read_markers", &body,
                  15000, [](const MatrixResponse&) {});
    UltraIMEvent event;
    event.type = UltraIMEventType::ConversationUpdate;
    event.providerId = providerId_;
    event.conversationId = conversationId;
    event.conversation = c;
    Emit(event);
    return UltraIMResult::Ok();
}

UltraIMResult MatrixProvider::SetTyping(const std::string& conversationId, bool isTyping) {
    std::shared_ptr<MatrixHttp> http = Http();
    if (!http) return UltraIMResult::Ok();
    std::string me;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = rooms_.find(conversationId);
        if (it == rooms_.end() || it->second.invited) return UltraIMResult::Ok();
        me = userId_;
    }
    JSONValue body = JSONValue::MakeObject();
    body.Set("typing", JSONValue(isTyping));
    if (isTyping) body.Set("timeout", JSONValue(static_cast<int64_t>(10000)));
    http->Request("PUT", std::string(kClient) + "/rooms/" + PathSegment(conversationId) + "/typing/" + PathSegment(me),
                  &body, 15000, [](const MatrixResponse&) {});
    return UltraIMResult::Ok();
}

// ============================================================================
// ENCRYPTION
// ============================================================================

std::string MatrixProvider::GetEncryptionError() const {
    if (!OlmAvailable()) return "this ChatCube was built without end-to-end encryption";
    std::lock_guard<std::mutex> lock(mutex_);
    return cryptoError_.empty() ? std::string("encryption is not set up yet") : cryptoError_;
}

std::string MatrixProvider::CryptoStorePath(const std::string& userId, const std::string& deviceId) const {
    // One store per account and device; the name says neither.
    const std::string id = userId + "|" + deviceId;
    std::vector<uint8_t> digest;
    UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, id.data(), id.size(), digest);
    const std::string name = "crypto-" + UltraCrypt_ToHex(digest).substr(0, 24) + ".store";
    const std::filesystem::path dir =
        UltraCanvas::PathFromUtf8(config_.dataDirectory) / UltraCanvas::PathFromUtf8(providerId_);
    return UltraCanvas::PathToUtf8(dir / UltraCanvas::PathFromUtf8(name));
}

void MatrixProvider::OpenCrypto(const std::string& userId, const std::string& deviceId) {
    auto failed = [this](const std::string& why) {
        std::lock_guard<std::mutex> lock(mutex_);
        cryptoError_ = why;
    };
    if (!OlmAvailable()) return failed("this ChatCube was built without end-to-end encryption");
    if (!vaultReady_ || deviceId.empty()) return failed("there is no device to keep the keys of");
    // The store's key lives in the vault; the store itself is a file of ours.
    const std::string account = "crypto:" + userId + ":" + deviceId;
    std::string keyText;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Retrieve(account, keyText); });
    std::vector<uint8_t> key = UltraCanvas::Base64Decode(keyText);
    const std::string path = CryptoStorePath(userId, deviceId);
    if (key.size() != 32) {
        if (MatrixCrypto::StoreExists(path)) return failed("the key to this device's encryption keys is gone");
        if (!UltraCrypt_RandomBytes(key, 32)) return failed("no randomness for the key store");
        bool stored = false;
        const std::string encoded = UltraCanvas::Base64Encode(key, false);
        vault_.Use([&](UltraVault::DeviceKeyVault& v) { stored = v.Store(account, encoded); });
        if (!stored) return failed("the vault does not keep the key store's key");
    }
    std::string error;
    const bool opened = crypto_.Open(path, key, userId, deviceId, error);
    UltraCrypt_SecureZero(key.data(), key.size());
    if (!opened) return failed(error);
    std::shared_ptr<MatrixHttp> http = Http();
    if (!http || !crypto_.UploadKeys(*http, -1, true, error)) {
        crypto_.Close();
        return failed("the homeserver did not take this device's keys: " + error);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    cryptoError_.clear();
}

bool MatrixProvider::DecryptForTimeline(Room& room, const JSONValue& event, JSONValue& clear,
                                        std::string& placeholder) {
    MatrixDecrypted d = crypto_.DecryptRoomEvent(room.id, event);
    if (d.state == MatrixDecryptState::Ok && d.event["type"].GetString() != "m.room.encrypted") {
        clear = d.event;
        return true;
    }
    if (d.state == MatrixDecryptState::MissingKey) {
        auto& waiting = pendingDecrypt_[room.id + "|" + d.sessionId];
        const std::string eventId = event["event_id"].GetString();
        bool held = false;
        for (const auto& e : waiting) held = held || e["event_id"].GetString() == eventId;
        if (!held) waiting.push_back(event);
        placeholder = kWaitingForKey;
    } else if (!crypto_.IsOpen()) {
        // (mutex_ held: cryptoError_ read here, not through GetEncryptionError)
        placeholder = "Encrypted message - " + (!OlmAvailable() ? std::string("this ChatCube was built without "
                                                                              "end-to-end encryption")
                                                : cryptoError_.empty() ? std::string("encryption is not set up")
                                                                       : cryptoError_);
    } else {
        placeholder = "This encrypted message cannot be read: " + d.error;
    }
    return false;
}

void MatrixProvider::RetryPending(const std::vector<MatrixSessionRef>& arrived) {
    if (arrived.empty()) return;
    std::vector<UltraIMEvent> events;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& ref : arrived) {
            auto held = pendingDecrypt_.find(ref.first + "|" + ref.second);
            if (held == pendingDecrypt_.end()) continue;
            const std::vector<JSONValue> waiting = std::move(held->second);
            pendingDecrypt_.erase(held);
            auto r = rooms_.find(ref.first);
            if (r == rooms_.end()) continue;
            Room& room = r->second;
            for (const JSONValue& event : waiting) {
                const std::string eventId = event["event_id"].GetString();
                auto at = std::find_if(room.timeline.begin(), room.timeline.end(),
                                       [&](const UltraIMMessage& m) { return m.messageId == eventId; });
                JSONValue clear;
                std::string placeholder;
                if (!DecryptForTimeline(room, event, clear, placeholder)) {
                    if (at != room.timeline.end() && at->text != placeholder) {
                        at->text = placeholder;
                        UltraIMEvent e;
                        e.type = UltraIMEventType::MessageUpdate;
                        e.providerId = providerId_;
                        e.conversationId = room.id;
                        e.message = *at;
                        events.push_back(e);
                    }
                    continue;
                }
                UltraIMMessage message;
                const bool plain = clear["type"].GetString() == "m.room.message" &&
                                   clear["content"]["m.relates_to"]["rel_type"].GetString() != "m.replace";
                if (plain && at != room.timeline.end() && MessageFrom(room, clear, message)) {
                    // The placeholder becomes the message, where it stood.
                    *at = message;
                    QueueMedia(message);
                    UltraIMEvent e;
                    e.type = UltraIMEventType::MessageUpdate;
                    e.providerId = providerId_;
                    e.conversationId = room.id;
                    e.message = message;
                    events.push_back(e);
                    continue;
                }
                // An edit (or something not shown): no line of its own.
                if (at != room.timeline.end()) {
                    UltraIMEvent e;
                    e.type = UltraIMEventType::MessageDelete;
                    e.providerId = providerId_;
                    e.conversationId = room.id;
                    e.message = *at;
                    events.push_back(e);
                    room.timeline.erase(at);
                }
                ApplyEvent(room, clear, true, events);
            }
            UltraIMEvent update;
            update.type = UltraIMEventType::ConversationUpdate;
            update.providerId = providerId_;
            update.conversationId = room.id;
            update.conversation = ConversationOf(room);
            events.push_back(update);
        }
    }
    for (const auto& e : events) Emit(e);
    FlushDownloads();
}

bool MatrixProvider::SendEncrypted(const std::shared_ptr<MatrixHttp>& http, const std::string& roomId,
                                   const std::string& txnId, const std::string& type, const JSONValue& content,
                                   std::string& eventId, std::string& error) {
    std::vector<std::string> members;
    MatrixRoomEncryption settings;
    bool known = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = rooms_.find(roomId);
        if (it == rooms_.end()) {
            error = "No such room";
            return false;
        }
        settings = it->second.encryption;
        known = it->second.recipientsKnown;
        members.assign(it->second.recipients.begin(), it->second.recipients.end());
    }
    if (!known) {
        // Everyone joined or invited, not only those the sync mentioned.
        MatrixResponse r = http->Await("GET", std::string(kClient) + "/rooms/" + PathSegment(roomId) +
                                                  "/members?not_membership=leave",
                                       nullptr, 20000);
        if (!r.Ok()) {
            error = r.error;
            return false;
        }
        std::set<std::string> fresh;
        for (const auto& member : r.body["chunk"].GetElements()) {
            const std::string membership = member["content"]["membership"].GetString();
            if (membership == "join" || membership == "invite") fresh.insert(member["state_key"].GetString());
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = rooms_.find(roomId);
        if (it != rooms_.end()) {
            it->second.recipients = fresh;
            it->second.recipientsKnown = true;
        }
        members.assign(fresh.begin(), fresh.end());
    }
    JSONValue encrypted;
    if (!crypto_.EncryptRoomEvent(*http, roomId, members, settings, type, content, encrypted, error)) return false;
    MatrixResponse r = http->Await("PUT", std::string(kClient) + "/rooms/" + PathSegment(roomId) +
                                              "/send/m.room.encrypted/" + PathSegment(txnId),
                                   &encrypted, 30000);
    if (!r.Ok()) {
        error = r.error;
        return false;
    }
    eventId = r.body["event_id"].GetString();
    return true;
}

void MatrixProvider::Enqueue(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(jobMutex_);
        if (jobsStopping_) return;
        jobs_.push_back(std::move(job));
    }
    jobChanged_.notify_one();
}

void MatrixProvider::SendLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(jobMutex_);
            jobChanged_.wait(lock, [this]() { return jobsStopping_ || !jobs_.empty(); });
            if (jobsStopping_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        job();
    }
}

void MatrixProvider::ExportEncryptionKeys(const std::string& passphrase, UltraIMKeyExportCallback onResult) {
    if (!crypto_.IsOpen()) {
        if (onResult)
            onResult(UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "Encryption: " + GetEncryptionError()),
                     std::string());
        return;
    }
    // The passphrase stretching takes its time: on the send thread.
    Enqueue([this, passphrase, onResult]() {
        std::string out, error;
        const bool ok = crypto_.ExportRoomKeys(passphrase, exportRounds_, out, error);
        if (onResult)
            onResult(ok ? UltraIMResult::Ok() : UltraIMResult::Error(UltraIMResultCode::InvalidArgument, error), out);
    });
}

void MatrixProvider::ImportEncryptionKeys(const std::string& data, const std::string& passphrase,
                                          UltraIMKeyImportCallback onResult) {
    if (!crypto_.IsOpen()) {
        if (onResult)
            onResult(UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "Encryption: " + GetEncryptionError()),
                     0);
        return;
    }
    Enqueue([this, data, passphrase, onResult]() {
        std::vector<MatrixSessionRef> imported;
        std::string error;
        const bool ok = crypto_.ImportRoomKeys(data, passphrase, imported, error);
        // Messages that were waiting for these keys open now.
        if (ok) RetryPending(imported);
        if (onResult)
            onResult(ok ? UltraIMResult::Ok() : UltraIMResult::Error(UltraIMResultCode::InvalidArgument, error),
                     static_cast<int>(imported.size()));
    });
}

} // namespace UltraIMMatrix
