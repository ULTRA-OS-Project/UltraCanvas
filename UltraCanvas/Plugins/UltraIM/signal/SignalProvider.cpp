// Plugins/UltraIM/signal/SignalProvider.cpp
// Signal - signal-cli's accounts, contacts and groups as UltraIM conversations
// Version: 0.2.0 - the provider's own daemon is started for the shared proxy setting (Java's proxy options in
//                  its environment) and started again when the setting changes; a proxy signal-cli cannot
//                  use, or a signal-cli that reads no Java options, is not left to connect directly
// Version: 0.1.1 - a receipt that comes before the send is recorded is kept and applied, not lost
// Version: 0.1.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "SignalProvider.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCrypt/UltraCryptCore.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <utility>

namespace UltraIMSignal {

using UltraCanvas::JSONValue;

namespace {

constexpr int64_t kDeleteWindowMs = 24LL * 60 * 60 * 1000;     // Signal: delete for everyone within a day
constexpr const char* kGroupPrefix = "group:";

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

// "127.0.0.1:7583", "http://127.0.0.1:7583/api/v1/rpc" -> "http://127.0.0.1:7583".
std::string NormalizeDaemon(const std::string& server) {
    std::string url = Trim(server);
    if (url.empty()) return url;
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) url = "http://" + url;
    while (!url.empty() && url.back() == '/') url.pop_back();
    for (const char* suffix : {"/api/v1/rpc", "/api/v1/check", "/api/v1"}) {
        const std::string s = suffix;
        if (url.size() > s.size() && url.compare(url.size() - s.size(), s.size(), s) == 0) url.erase(url.size() - s.size());
    }
    return url;
}

// "+49 151 1234-567", "0049 (151) 1234567" -> "+491511234567"; "" when it is no number.
std::string NormalizeNumber(const std::string& text) {
    std::string t = Trim(text);
    if (t.rfind("00", 0) == 0) t = "+" + t.substr(2);
    if (t.empty() || t[0] != '+') return std::string();
    std::string out = "+";
    for (size_t i = 1; i < t.size(); ++i) {
        const char c = t[i];
        if (std::isdigit(static_cast<unsigned char>(c))) out += c;
        else if (c == ' ' || c == '-' || c == '(' || c == ')' || c == '/' || c == '.') continue;
        else return std::string();
    }
    return out.size() >= 6 ? out : std::string();
}

// A Signal username: a nickname, a dot, at least two digits ("name.01").
bool LooksLikeUsername(const std::string& s) {
    const size_t dot = s.rfind('.');
    if (dot == std::string::npos || dot == 0 || s.size() - dot - 1 < 2) return false;
    for (size_t i = dot + 1; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    for (size_t i = 0; i < dot; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (!std::isalnum(c) && c != '_') return false;
    }
    return true;
}

bool IsGroupId(const std::string& conversationId) {
    return conversationId.rfind(kGroupPrefix, 0) == 0;
}

// "<author>|<timestamp>" -> its two halves.
bool SplitMessageId(const std::string& id, std::string& author, int64_t& timestamp) {
    const size_t bar = id.rfind('|');
    if (bar == std::string::npos || bar == 0 || bar + 1 >= id.size()) return false;
    author = id.substr(0, bar);
    timestamp = 0;
    for (size_t i = bar + 1; i < id.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(id[i]))) return false;
        timestamp = timestamp * 10 + (id[i] - '0');
    }
    return timestamp > 0;
}

std::string MessageIdOf(const std::string& author, int64_t timestamp) {
    return author + "|" + std::to_string(timestamp);
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

std::string ExtensionFor(const std::string& mime) {
    for (const auto& entry : kMimeTypes) {
        if (mime == entry.second) return entry.first;
    }
    return std::string();
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

bool WriteWholeFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::FILE* f = UltraCanvas::OpenFileUtf8(path, "wb");
    if (!f) return false;
    const bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    return ok;
}

JSONValue StringArray(const std::vector<std::string>& items) {
    JSONValue a = JSONValue::MakeArray();
    for (const std::string& s : items) a.Append(JSONValue(s));
    return a;
}

UltraIMEvent EventOf(UltraIMEventType type, const std::string& providerId, const std::string& conversationId) {
    UltraIMEvent e;
    e.type = type;
    e.providerId = providerId;
    e.conversationId = conversationId;
    return e;
}

std::string ReadTail(const std::string& path, size_t maxBytes) {
    std::FILE* f = UltraCanvas::OpenFileUtf8(path, "rb");
    if (!f) return std::string();
    std::string text;
    char buffer[4096];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
        text.append(buffer, n);
        if (text.size() > maxBytes * 4) text.erase(0, text.size() - maxBytes);
    }
    std::fclose(f);
    if (text.size() > maxBytes) text.erase(0, text.size() - maxBytes);
    return Trim(text);
}

} // namespace

// ============================================================================
// LIFETIME
// ============================================================================

SignalProvider::SignalProvider(std::string providerId) : providerId_(std::move(providerId)) {}

SignalProvider::~SignalProvider() {
    Shutdown();
}

UltraIMCapabilityFlags SignalProvider::GetCapabilities() const {
    using F = UltraIMCapabilityFlags;
    return F::TextMessages | F::GroupChats | F::ImageMessages | F::FileTransfer | F::ReplyToMessage |
           F::EditMessage | F::DeleteMessage | F::ReadReceipts | F::TypingIndicator | F::OfflineMessages |
           F::MultiDevice | F::E2EEncryption | F::AddContact | F::CreateGroup | F::SearchMessages |
           F::LeaveConversation | F::VoiceNotes;
}

UltraIMResult SignalProvider::Initialize(const UltraIMConfig& config, UltraIMEventSink sink) {
    config_ = config;
    sink_ = std::move(sink);
    if (config.dataDirectory.empty())
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "The Signal provider needs a data directory (UltraIMConfig::dataDirectory)");
    std::error_code ec;
    const std::filesystem::path dir = UltraCanvas::PathFromUtf8(config.dataDirectory) / UltraCanvas::PathFromUtf8(providerId_);
    std::filesystem::create_directories(dir, ec);
    vault_ = UltraVault::DeviceKeyVault(UltraCanvas::PathToUtf8(dir), {"signal.vault", config.appId + "." + providerId_ + "."});
    vaultReady_ = true;
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        stopping_ = false;
    }
    stopFlag_ = false;
    if (!worker_.joinable()) worker_ = std::thread([this]() { WorkLoop(); });
    if (!listener_.joinable()) listener_ = std::thread([this]() { ListenLoop(); });
    return UltraIMResult::Ok();
}

void SignalProvider::Shutdown() {
    SignOut();
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        stopping_ = true;
        jobs_.clear();
    }
    stopFlag_ = true;
    workWake_.notify_all();
    {
        std::lock_guard<std::mutex> lock(listenMutex_);
        listenMode_ = ListenMode::Idle;
    }
    listenWake_.notify_all();
    if (worker_.joinable()) worker_.join();
    if (listener_.joinable()) listener_.join();
    daemon_.Stop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ownDaemon_) daemonUrl_.clear();
        ownDaemon_ = false;
    }
    sink_ = nullptr;
}

void SignalProvider::Enqueue(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        if (stopping_) return;
        jobs_.push_back(std::move(job));
    }
    workWake_.notify_one();
}

void SignalProvider::WorkLoop() {
    // A call under way when the provider shuts down ends there.
    UltraNet_SetThreadAbortCheck([this]() { return stopFlag_.load(); });
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

void SignalProvider::SetListenMode(ListenMode mode) {
    {
        std::lock_guard<std::mutex> lock(listenMutex_);
        listenMode_ = mode;
    }
    listenWake_.notify_all();
}

void SignalProvider::ListenLoop() {
    for (;;) {
        ListenMode mode = ListenMode::Idle;
        uint64_t generation = 0;
        {
            std::unique_lock<std::mutex> lock(listenMutex_);
            listenWake_.wait(lock, [this]() { return stopFlag_.load() || listenMode_ != ListenMode::Idle; });
            if (stopFlag_.load()) break;
            mode = listenMode_;
            generation = listenGeneration_.load();
        }
        // finishLink and receive wait on the daemon: a sign-out (a new
        // generation) or the shutdown ends them within a second.
        UltraNet_SetThreadAbortCheck([this, generation]() {
            return stopFlag_.load() || listenGeneration_.load() != generation;
        });
        if (mode == ListenMode::Link) LinkOnListener(generation);
        else if (mode == ListenMode::Receive) ReceiveOnListener(generation);
    }
    UltraNet_SetThreadAbortCheck(nullptr);
}

void SignalProvider::Emit(const UltraIMEvent& event) {
    if (sink_) sink_(event);
}

void SignalProvider::SetState(UltraIMConnectionState state, const std::string& error) {
    if (state_.exchange(state) == state && error.empty()) return;
    UltraIMEvent event = EventOf(UltraIMEventType::ConnectionStateChange, providerId_, std::string());
    event.connectionState = state;
    event.errorText = error;
    Emit(event);
}

std::string SignalProvider::GetLocalUserId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return account_;
}

std::string SignalProvider::GetDaemonUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return daemonUrl_;
}

int64_t SignalProvider::GetDaemonProcessId() const {
    return daemon_.ProcessId();
}

UltraIMAuthStep SignalProvider::GetAuthStep() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return authStep_;
}

std::string SignalProvider::GetAuthHint() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return authHint_;
}

void SignalProvider::FetchAccountProfile(UltraIMAccountProfileCallback onResult) {
    UltraIMAccountProfile profile;
    UltraIMResult result = UltraIMResult::Ok();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (account_.empty()) {
            result = UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Signal");
        } else {
            // The profile is the phone's: read-only here.
            profile.phoneNumber = number_;
            profile.displayName = number_.empty() ? account_ : number_;
            profile.phoneVerified = !number_.empty();
        }
    }
    if (onResult) onResult(result, profile);
}

// ============================================================================
// THE DAEMON
// ============================================================================

SignalResponse SignalProvider::Rpc(const std::string& method, const JSONValue& params, int timeoutMs) {
    UltraNetHttpRequest request;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        request.url = daemonUrl_ + "/api/v1/rpc";
    }
    SignalResponse r;
    if (request.url == "/api/v1/rpc") {
        r.error = "No signal-cli daemon";
        return r;
    }
    JSONValue call = JSONValue::MakeObject();
    call.Set("jsonrpc", JSONValue("2.0"));
    call.Set("id", JSONValue(static_cast<int64_t>(++rpcId_)));
    call.Set("method", JSONValue(method));
    if (!params.IsNull()) call.Set("params", params);
    const std::string json = UltraCanvas::JSON::Serialize(call);
    request.method = UltraNetHttpMethod::Post;
    request.body.assign(json.begin(), json.end());
    request.headers.Set("Content-Type", "application/json");
    request.headers.Set("Accept", "application/json");
    request.options.timeoutMs = timeoutMs;
    request.options.maxReceiveSize = 512LL * 1024 * 1024;     // getAttachment: a file in base64
    UltraNetResponse raw;
    UltraNet_HttpRequest(request, raw);
    r.status = raw.IsComplete() ? raw.statusCode : 0;
    if (r.status == 0) {
        r.error = !raw.transferError.empty() ? raw.transferError : "The signal-cli daemon does not answer";
        return r;
    }
    UltraCanvas::JSONParseResult parsed;
    const JSONValue answer = UltraCanvas::JSON::Parse(raw.GetBodyAsString(), &parsed);
    if (const JSONValue* error = answer.Find("error"); error && !error->IsNull()) {
        r.rpcError = true;
        r.errorCode = (*error)["code"].GetInteger(0);
        r.error = (*error)["message"].GetString();
        if (r.error.empty()) r.error = "signal-cli refused " + method;
        return r;
    }
    if (r.status < 200 || r.status >= 300) {
        r.error = "HTTP " + std::to_string(r.status);
        return r;
    }
    r.result = answer["result"];
    return r;
}

bool SignalProvider::DaemonAnswers(const std::string& url, int timeoutMs) {
    UltraNetHttpOptions options;
    options.timeoutMs = timeoutMs;
    options.connectTimeoutMs = timeoutMs;
    UltraNetResponse raw;
    UltraNet_HttpGet(url + "/api/v1/check", raw, options);
    return raw.IsComplete() && raw.statusCode == 200;
}

std::string SignalProvider::DataPath(const std::string& name) const {
    std::error_code ec;
    const std::filesystem::path dir = UltraCanvas::PathFromUtf8(config_.dataDirectory) / UltraCanvas::PathFromUtf8(providerId_);
    std::filesystem::create_directories(dir, ec);
    return UltraCanvas::PathToUtf8(dir / UltraCanvas::PathFromUtf8(name));
}

std::string SignalProvider::ConfigDirectory() const {
    return DataPath("signal-cli");
}

UltraIMResult SignalProvider::EnsureDaemon(const std::string& server) {
    std::string url = NormalizeDaemon(server);
    if (url.empty()) {
        std::string storedDaemon, number, aci;
        LoadSession(storedDaemon, number, aci);
        url = storedDaemon;
    }
    if (!url.empty()) {
        // The user's own daemon.
        if (!DaemonAnswers(url, 5000))
            return UltraIMResult::Error(UltraIMResultCode::NetworkError,
                                        "No signal-cli daemon answers at " + url +
                                            " (start it with: signal-cli daemon --http " +
                                            url.substr(url.find("://") + 3) + " --receive-mode=manual)");
        std::lock_guard<std::mutex> lock(mutex_);
        daemonUrl_ = url;
        ownDaemon_ = false;
        return UltraIMResult::Ok();
    }
    // Our own daemon connects as the proxy setting says, or is not started:
    // signal-cli reaches Signal by itself, past UltraNet.
    const DaemonProxy proxy = ChooseDaemonProxy();
    {
        std::string running;
        bool sameProxy = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (ownDaemon_) running = daemonUrl_;
            sameProxy = daemonProxy_ == proxy;
        }
        if (!running.empty() && sameProxy && daemon_.IsRunning() && DaemonAnswers(running, 3000)) return UltraIMResult::Ok();
    }
    {
        // What this start is for, however it ends: DaemonProxyChanged asks
        // for another only when the setting has moved on from it.
        std::lock_guard<std::mutex> lock(mutex_);
        daemonProxy_ = proxy;
    }
    if (proxy.kind == DaemonProxy::Kind::Unusable) {
        // One started for the setting before does not go on connecting its way.
        daemon_.Stop();
        return UltraIMResult::Error(UltraIMResultCode::Unsupported, proxy.text);
    }
    const std::string program = FindSignalCli(config_.pluginDirectory);
    if (program.empty())
        return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing,
                                    "Signal needs signal-cli (which needs Java 21): install it, or give the address "
                                    "of a running signal-cli daemon (signal-cli daemon --http 127.0.0.1:7583)");
    // The proxy reaches signal-cli as Java options. For a signal-cli that no
    // Java runtime starts (its native build) nothing shows they are read: it
    // is taken to connect directly, and not used for a proxy.
    const UltraIMResult notJava = UltraIMResult::Error(
        UltraIMResultCode::Unsupported,
        "Signal cannot connect through the proxy " + proxy.text + ": this signal-cli (" + program +
            ") is not run by Java, which is where it takes a proxy from. Install signal-cli's Java distribution, or "
            "run the daemon yourself and give its address.");
    if (proxy.kind == DaemonProxy::Kind::Through) {
        bool known = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            known = notJava_ == program;
        }
        if (known) {
            daemon_.Stop();
            return notJava;
        }
    }
    const int port = FreeLoopbackPort();
    if (port <= 0) return UltraIMResult::Error(UltraIMResultCode::NetworkError, "No free port for signal-cli");
    const std::string configDir = ConfigDirectory();
    std::error_code ec;
    std::filesystem::create_directories(UltraCanvas::PathFromUtf8(configDir), ec);
    const std::string logPath = DataPath("signal-cli.log");
    const std::string address = "127.0.0.1:" + std::to_string(port);
    std::vector<std::pair<std::string, std::string>> environment;
    if (!proxy.jvmOptions.empty()) environment.emplace_back("JAVA_TOOL_OPTIONS", JavaToolOptionsWith(proxy.jvmOptions));
    // Where this start's lines begin in the log (it is appended to).
    std::error_code sizeError;
    const auto logSize = std::filesystem::file_size(UltraCanvas::PathFromUtf8(logPath), sizeError);
    const uint64_t logStart = sizeError ? 0 : static_cast<uint64_t>(logSize);
    std::string error;
    if (!daemon_.Start(program, {"--config", configDir, "daemon", "--http", address, "--receive-mode=manual"}, logPath,
                       error, environment))
        return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, error);
    const std::string url2 = "http://" + address;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timings_.daemonStartMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (stopFlag_.load()) {
            daemon_.Stop();
            return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Shutting down");
        }
        if (!daemon_.IsRunning()) {
            const std::string tail = ReadTail(logPath, 400);
            return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing,
                                        "signal-cli did not start (it needs Java 21)" +
                                            (tail.empty() ? std::string() : ": " + tail));
        }
        if (DaemonAnswers(url2, 1000)) {
            if (proxy.kind == DaemonProxy::Kind::Through && !JvmTookOptions(logPath, logStart)) {
                // It answers, but no Java runtime said it read the options:
                // it would reach Signal directly.
                daemon_.Stop();
                std::lock_guard<std::mutex> lock(mutex_);
                notJava_ = program;
                return notJava;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            daemonUrl_ = url2;
            ownDaemon_ = true;
            return UltraIMResult::Ok();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    daemon_.Stop();
    return UltraIMResult::Error(UltraIMResultCode::Timeout, "signal-cli did not answer in time (see " + logPath + ")");
}

bool SignalProvider::DaemonProxyChanged() const {
    DaemonProxy started;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ownDaemon_) return false;
        started = daemonProxy_;
    }
    return !(ChooseDaemonProxy() == started);
}

// ============================================================================
// SIGNING IN
// ============================================================================

bool SignalProvider::SaveSession() {
    if (!vaultReady_) return false;
    JSONValue session = JSONValue::MakeObject();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session.Set("daemon", JSONValue(ownDaemon_ ? std::string() : daemonUrl_));
        session.Set("number", JSONValue(number_));
        session.Set("aci", JSONValue(aci_));
        session.Set("hidden", StringArray(std::vector<std::string>(hidden_.begin(), hidden_.end())));
    }
    bool stored = false;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { stored = v.Store("session", UltraCanvas::JSON::Serialize(session)); });
    return stored;
}

bool SignalProvider::LoadSession(std::string& daemon, std::string& number, std::string& aci,
                                 std::set<std::string>* hidden) const {
    if (!vaultReady_) return false;
    std::string text;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Retrieve("session", text); });
    if (text.empty()) return false;
    UltraCanvas::JSONParseResult parsed;
    const JSONValue session = UltraCanvas::JSON::Parse(text, &parsed);
    daemon = session["daemon"].GetString();
    number = session["number"].GetString();
    aci = session["aci"].GetString();
    if (hidden) {
        for (const JSONValue& h : session["hidden"].GetElements()) hidden->insert(h.GetString());
    }
    return !number.empty() || !aci.empty();
}

bool SignalProvider::HasStoredSession() const {
    std::string daemon, number, aci;
    return LoadSession(daemon, number, aci);
}

void SignalProvider::SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) {
    if (!vaultReady_) {
        if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotInitialized, "Initialize first"));
        return;
    }
    if (state_.load() != UltraIMConnectionState::Connected) SetState(UltraIMConnectionState::Connecting);
    Enqueue([this, credentials, onResult]() { SignInOnWorker(credentials, onResult); });
}

void SignalProvider::SignInOnWorker(UltraIMCredentials credentials, UltraIMResultCallback onResult) {
    const auto fail = [this, &onResult](const UltraIMResult& r) {
        SetState(UltraIMConnectionState::Error, r.message);
        if (onResult) onResult(r);
    };
    if (state_.load() == UltraIMConnectionState::Connected) {
        if (onResult) onResult(UltraIMResult::Ok());
        return;
    }
    {
        // A link waiting for its scan: the same link again.
        std::unique_lock<std::mutex> lock(mutex_);
        if (authStep_ == UltraIMAuthStep::OtherDevice && !authHint_.empty()) {
            const std::string hint = authHint_;
            lock.unlock();
            SetState(UltraIMConnectionState::Disconnected);
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NeedsInput, hint));
            return;
        }
    }
    {
        // A sign-in asked for tries the signal-cli there is now afresh.
        std::lock_guard<std::mutex> lock(mutex_);
        notJava_.clear();
    }
    const UltraIMResult daemon = EnsureDaemon(credentials.server);
    if (!daemon) return fail(daemon);

    const SignalResponse accounts = Rpc("listAccounts", JSONValue(), 20000);
    if (!accounts.Ok())
        return fail(UltraIMResult::Error(UltraIMResultCode::NetworkError, "signal-cli does not list its accounts: " + accounts.error));
    std::string storedDaemon, storedNumber, storedAci;
    std::set<std::string> hidden;
    LoadSession(storedDaemon, storedNumber, storedAci, &hidden);
    const std::string wanted = NormalizeNumber(credentials.phoneNumber);
    std::string number, aci;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hidden_ = hidden;
    }
    const auto& list = accounts.result.GetElements();
    for (const JSONValue& a : list) {
        const std::string n = a["number"].GetString(), id = a["aci"].GetString();
        const bool isWanted = !wanted.empty() && n == wanted;
        const bool isStored = wanted.empty() && ((!storedNumber.empty() && n == storedNumber) ||
                                                 (storedNumber.empty() && !storedAci.empty() && id == storedAci));
        if (isWanted || isStored) {
            number = n;
            aci = id;
        }
    }
    if (!number.empty() || !aci.empty()) return Connect(number, aci, onResult);

    // Not linked (any more): a QR code for the phone.
    std::string uri;
    const UltraIMResult started = StartLink(uri);
    if (!started) return fail(started);
    AskForLink(uri, onResult);
}

UltraIMResult SignalProvider::StartLink(std::string& uri) {
    const SignalResponse r = Rpc("startLink", JSONValue(), 30000);
    uri = r.Ok() ? r.result["deviceLinkUri"].GetString() : std::string();
    if (uri.empty())
        return UltraIMResult::Error(UltraIMResultCode::NetworkError,
                                    "signal-cli could not start linking: " + (r.error.empty() ? "no link" : r.error));
    return UltraIMResult::Ok();
}

// The link to scan, asked for: the step is set, the SignIn waiting is
// answered NeedsInput, and only then is AuthStepChange raised - as Telegram
// does, so an application that answers the event with a SignIn gets its own
// answer. finishLink then waits on the listening thread.
void SignalProvider::AskForLink(const std::string& uri, const UltraIMResultCallback& onResult) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        authStep_ = UltraIMAuthStep::OtherDevice;
        authHint_ = uri;
    }
    SetState(UltraIMConnectionState::Disconnected);
    if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NeedsInput, uri));
    UltraIMEvent event = EventOf(UltraIMEventType::AuthStepChange, providerId_, std::string());
    event.authStep = UltraIMAuthStep::OtherDevice;
    event.errorText = uri;
    Emit(event);
    SetListenMode(ListenMode::Link);
}

void SignalProvider::LinkOnListener(uint64_t generation) {
    std::string uri;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        uri = authHint_;
    }
    if (uri.empty()) {
        SetListenMode(ListenMode::Idle);
        return;
    }
    JSONValue params = JSONValue::MakeObject();
    params.Set("deviceLinkUri", JSONValue(uri));
    params.Set("deviceName", JSONValue("ChatCube"));
    // Until the phone scans the code, or signal-cli gives up on the link.
    const SignalResponse r = Rpc("finishLink", params, -1);
    if (listenGeneration_.load() != generation || stopFlag_.load()) return;
    {
        std::lock_guard<std::mutex> lock(listenMutex_);
        if (listenMode_ == ListenMode::Link) listenMode_ = ListenMode::Idle;
    }
    if (r.Ok()) {
        const std::string number = r.result["number"].GetString();
        const std::string aci = r.result["aci"].GetString();
        Enqueue([this, number, aci, generation]() {
            if (listenGeneration_.load() != generation) return;
            Connect(number, aci, nullptr);
        });
        return;
    }
    if (r.rpcError && Lower(r.error).find("timed out") != std::string::npos) {
        // The link expired before it was scanned: a new one.
        Enqueue([this, generation]() {
            if (listenGeneration_.load() != generation) return;
            std::string next;
            const UltraIMResult started = StartLink(next);
            if (!started) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    authStep_ = UltraIMAuthStep::Idle;
                    authHint_.clear();
                }
                SetState(UltraIMConnectionState::Error, started.message);
                return;
            }
            AskForLink(next, nullptr);
        });
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        authStep_ = UltraIMAuthStep::Idle;
        authHint_.clear();
    }
    SetState(UltraIMConnectionState::Error, "Linking to the phone failed: " + r.error);
}

void SignalProvider::Connect(const std::string& number, const std::string& aci, const UltraIMResultCallback& onResult) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        number_ = number;
        aci_ = aci;
        account_ = !number.empty() ? number : aci;
        authStep_ = UltraIMAuthStep::Idle;
        authHint_.clear();
        contacts_.clear();
        numberOfUuid_.clear();
        chats_.clear();
        history_.clear();
        conversationOf_.clear();
        clientOf_.clear();
        receipted_.clear();
        if (!number.empty() && !aci.empty()) numberOfUuid_[aci] = number;
    }
    SaveSession();
    std::vector<UltraIMEvent> events;
    LoadDirectory(events);
    SetState(UltraIMConnectionState::Connected);
    if (onResult) onResult(UltraIMResult::Ok());
    for (const auto& e : events) Emit(e);
    SetListenMode(ListenMode::Receive);
}

void SignalProvider::SignOut() {
    ++listenGeneration_;
    SetListenMode(ListenMode::Idle);
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        jobs_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        account_.clear();
        chats_.clear();
        authStep_ = UltraIMAuthStep::Idle;
        authHint_.clear();
    }
    if (state_.load() != UltraIMConnectionState::Disconnected) SetState(UltraIMConnectionState::Disconnected);
}

// ============================================================================
// CONTACTS, GROUPS, CONVERSATIONS
// ============================================================================

std::string SignalProvider::KeyOf(const std::string& number, const std::string& uuid) {
    if (!number.empty()) {
        if (!uuid.empty()) numberOfUuid_[uuid] = number;
        return number;
    }
    auto it = numberOfUuid_.find(uuid);
    return it != numberOfUuid_.end() ? it->second : uuid;
}

std::string SignalProvider::NameOf(const std::string& key) const {
    auto it = contacts_.find(key);
    if (it != contacts_.end()) {
        if (!it->second.name.empty()) return it->second.name;
        if (!it->second.seenName.empty()) return it->second.seenName;
        if (!it->second.number.empty()) return it->second.number;
        if (!it->second.username.empty()) return it->second.username;
    }
    return key;
}

UltraIMConversation SignalProvider::ConversationOf(const Chat& chat) const {
    UltraIMConversation c;
    c.conversationId = chat.id;
    c.providerId = providerId_;
    c.isGroup = chat.isGroup;
    c.isEncrypted = true;
    c.memberIds.push_back(account_);
    if (chat.isGroup) {
        for (const std::string& m : chat.members) {
            if (m != account_) c.memberIds.push_back(m);
        }
        c.title = chat.groupName;
        if (c.title.empty()) {
            for (const std::string& m : chat.members) {
                if (m != account_) c.title += (c.title.empty() ? "" : ", ") + NameOf(m);
            }
        }
    } else {
        c.directContactId = chat.contactKey;
        if (chat.contactKey != account_) c.memberIds.push_back(chat.contactKey);
        c.title = NameOf(chat.contactKey);
    }
    for (const std::string& m : c.memberIds) c.memberNames[m] = m == account_ ? (number_.empty() ? m : number_) : NameOf(m);
    if (chat.hasLast) {
        c.lastMessageSnippet = SnippetOf(chat.last);
        c.lastMessageTimeMs = chat.last.timestampMs;
    }
    c.unreadCount = chat.unread;
    return c;
}

SignalProvider::Chat& SignalProvider::ChatFor(const std::string& conversationId, bool isGroup, const std::string& key,
                                              std::vector<UltraIMEvent>& events) {
    auto it = chats_.find(conversationId);
    if (it != chats_.end()) return it->second;
    if (!isGroup) hidden_.erase(conversationId);   // left here, and written to again: back
    Chat& chat = chats_[conversationId];
    chat.id = conversationId;
    chat.isGroup = isGroup;
    if (isGroup) chat.groupId = conversationId.substr(std::string(kGroupPrefix).size());
    else chat.contactKey = key;
    (void)events;
    return chat;
}

void SignalProvider::ReadGroup(const JSONValue& g, std::vector<UltraIMEvent>& events) {
    const std::string gid = g["id"].GetString();
    if (gid.empty()) return;
    const std::string id = kGroupPrefix + gid;
    if (g.Find("isMember") && !g["isMember"].GetBoolean(true)) {
        if (chats_.erase(id)) events.push_back(EventOf(UltraIMEventType::ConversationRemove, providerId_, id));
        return;
    }
    Chat& chat = ChatFor(id, true, std::string(), events);
    chat.groupName = g["name"].GetString();
    chat.members.clear();
    for (const JSONValue& m : g["members"].GetElements()) {
        // {number, uuid, isAdmin} (older signal-cli: the number alone).
        const std::string key = m.IsString() ? m.GetString() : KeyOf(m["number"].GetString(), m["uuid"].GetString());
        if (key.empty()) continue;
        chat.members.push_back(key);
        if (key != account_ && !contacts_.count(key)) {
            Contact& c = contacts_[key];
            c.key = key;
            if (m.IsObject()) {
                c.number = m["number"].GetString();
                c.uuid = m["uuid"].GetString();
            }
        }
    }
    UltraIMEvent e = EventOf(UltraIMEventType::ConversationUpdate, providerId_, id);
    e.conversation = ConversationOf(chat);
    events.push_back(e);
}

void SignalProvider::LoadDirectory(std::vector<UltraIMEvent>& events) {
    std::string account;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        account = account_;
    }
    JSONValue params = JSONValue::MakeObject();
    params.Set("account", JSONValue(account));
    const SignalResponse contacts = Rpc("listContacts", params, 30000);
    const SignalResponse groups = Rpc("listGroups", params, 30000);
    std::lock_guard<std::mutex> lock(mutex_);
    if (contacts.Ok()) {
        for (const JSONValue& c : contacts.result.GetElements()) {
            const std::string key = KeyOf(c["number"].GetString(), c["uuid"].GetString());
            if (key.empty() || key == account_) continue;
            Contact& contact = contacts_[key];
            contact.key = key;
            contact.number = c["number"].GetString();
            contact.uuid = c["uuid"].GetString();
            contact.username = c["username"].GetString();
            // The name the phone's contact has, else the nickname, else the profile's.
            std::string name = Trim(c["name"].GetString());
            if (name.empty()) name = Trim(c["nickName"].GetString());
            if (name.empty()) name = Trim(c["givenName"].GetString() + " " + c["familyName"].GetString());
            if (name.empty()) name = Trim(c["profile"]["givenName"].GetString() + " " + c["profile"]["familyName"].GetString());
            if (!name.empty()) contact.name = name;
            // A conversation for those the account talks to (profile shared:
            // a conversation accepted or started), not every address-book entry.
            const bool talks = c["profileSharing"].GetBoolean(false) && !c["isBlocked"].GetBoolean(false) &&
                               !c["isHidden"].GetBoolean(false) && !c["unregistered"].GetBoolean(false);
            if (talks && !hidden_.count(key)) ChatFor(key, false, key, events);
        }
    }
    std::set<std::string> listedGroups;
    if (groups.Ok()) {
        for (const JSONValue& g : groups.result.GetElements()) {
            listedGroups.insert(kGroupPrefix + g["id"].GetString());
            ReadGroup(g, events);
        }
        // Groups gone from the list: left on the phone.
        for (auto it = chats_.begin(); it != chats_.end();) {
            if (it->second.isGroup && !listedGroups.count(it->first)) {
                events.push_back(EventOf(UltraIMEventType::ConversationRemove, providerId_, it->first));
                it = chats_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const auto& [id, chat] : chats_) {
        if (chat.isGroup) continue;     // (told by ReadGroup)
        UltraIMEvent e = EventOf(UltraIMEventType::ConversationUpdate, providerId_, id);
        e.conversation = ConversationOf(chat);
        events.push_back(e);
    }
}

bool SignalProvider::RefreshGroup(const std::string& groupId, std::vector<UltraIMEvent>& events) {
    std::string account;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        account = account_;
    }
    JSONValue params = JSONValue::MakeObject();
    params.Set("account", JSONValue(account));
    params.Set("groupId", StringArray({groupId}));
    const SignalResponse r = Rpc("listGroups", params, 20000);
    if (!r.Ok()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const JSONValue& g : r.result.GetElements()) {
        if (g["id"].GetString() == groupId) {
            ReadGroup(g, events);
            return true;
        }
    }
    return false;
}

void SignalProvider::FetchConversations(UltraIMConversationsCallback onResult) {
    Enqueue([this, onResult]() {
        if (state_.load() != UltraIMConnectionState::Connected) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Signal"), {});
            return;
        }
        // The phone's groups and contacts as they are now.
        std::vector<UltraIMEvent> events;
        LoadDirectory(events);
        std::vector<UltraIMConversation> list;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [id, chat] : chats_) list.push_back(ConversationOf(chat));
        }
        std::sort(list.begin(), list.end(), [](const UltraIMConversation& a, const UltraIMConversation& b) {
            return a.lastMessageTimeMs > b.lastMessageTimeMs;
        });
        for (const auto& e : events) {
            if (e.type == UltraIMEventType::ConversationRemove) Emit(e);
        }
        if (onResult) onResult(UltraIMResult::Ok(), list);
    });
}

void SignalProvider::FetchContacts(UltraIMContactsCallback onResult) {
    std::vector<UltraIMContact> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [key, contact] : contacts_) {
            if (key == account_) continue;
            UltraIMContact c;
            c.contactId = key;
            c.providerId = providerId_;
            c.displayName = NameOf(key);
            out.push_back(c);
        }
    }
    if (onResult) onResult(UltraIMResult::Ok(), out);
}

// ============================================================================
// MESSAGES
// ============================================================================

void SignalProvider::Remember(const UltraIMMessage& m) {
    auto& list = history_[m.conversationId];
    // In timestamp order (a message from the phone may come after a newer one).
    auto pos = list.end();
    while (pos != list.begin() && std::prev(pos)->timestampMs > m.timestampMs) --pos;
    list.insert(pos, m);
    conversationOf_[m.messageId] = m.conversationId;
    const size_t limit = static_cast<size_t>(std::max(10, timings_.historyLimit));
    while (list.size() > limit) {
        conversationOf_.erase(list.front().messageId);
        list.pop_front();
    }
}

UltraIMMessage* SignalProvider::FindMessage(const std::string& messageId) {
    std::string id = messageId;
    auto c = conversationOf_.find(id);
    if (c == conversationOf_.end()) {
        // A client id, before the row learnt the message's own.
        for (const auto& [mid, client] : clientOf_) {
            if (client == messageId) {
                id = mid;
                c = conversationOf_.find(id);
                break;
            }
        }
    }
    if (c == conversationOf_.end()) return nullptr;
    auto h = history_.find(c->second);
    if (h == history_.end()) return nullptr;
    for (auto it = h->second.rbegin(); it != h->second.rend(); ++it) {
        if (it->messageId == id) return &*it;
    }
    return nullptr;
}

const UltraIMMessage* SignalProvider::FindMessage(const std::string& messageId) const {
    return const_cast<SignalProvider*>(this)->FindMessage(messageId);
}

void SignalProvider::RefreshLast(Chat& chat) {
    chat.hasLast = false;
    auto h = history_.find(chat.id);
    if (h != history_.end() && !h->second.empty()) {
        chat.last = h->second.back();
        chat.hasLast = true;
    }
}

UltraIMMessage SignalProvider::MessageFrom(const JSONValue& dm, const std::string& author, int64_t timestamp,
                                           const std::string& conversationId, bool isOwn) const {
    UltraIMMessage m;
    m.providerId = providerId_;
    m.conversationId = conversationId;
    m.messageId = MessageIdOf(author, timestamp);
    auto client = clientOf_.find(m.messageId);
    m.clientMessageId = client != clientOf_.end() ? client->second : m.messageId;
    m.senderId = author;
    m.senderName = isOwn ? (number_.empty() ? author : number_) : NameOf(author);
    m.isOwn = isOwn;
    m.text = dm["message"].GetString();
    m.timestampMs = timestamp;
    m.deliveryStatus = isOwn ? UltraIMDeliveryStatus::Sent : UltraIMDeliveryStatus::Delivered;
    const auto& files = dm["attachments"].GetElements();
    if (!files.empty()) {
        const JSONValue& a = files.front();
        m.attachment.remoteUrl = a["id"].GetString();      // signal-cli's attachment id
        m.attachment.fileName = a["filename"].GetString();
        m.attachment.mimeType = a["contentType"].GetString();
        m.attachment.fileSizeBytes = a["size"].GetInteger(0);
        m.attachment.width = static_cast<int>(a["width"].GetInteger(0));
        m.attachment.height = static_cast<int>(a["height"].GetInteger(0));
        if (m.attachment.mimeType.empty()) m.attachment.mimeType = MimeFor(m.attachment.fileName);
        if (m.attachment.fileName.empty()) m.attachment.fileName = m.attachment.remoteUrl;
        m.type = a["isVoiceNote"].GetBoolean(false) ? UltraIMMessageType::Audio : TypeForMime(m.attachment.mimeType);
        if (m.text.empty()) m.text = a["caption"].GetString();
        if (files.size() > 1) m.text += (m.text.empty() ? "" : "\n") + std::string("+") + std::to_string(files.size() - 1);
    }
    if (const JSONValue* quote = dm.Find("quote"); quote && quote->IsObject()) {
        std::string quoted = (*quote)["authorNumber"].GetString();
        if (quoted.empty()) quoted = (*quote)["author"].GetString();
        const std::string uuid = (*quote)["authorUuid"].GetString();
        if (quoted.empty()) quoted = uuid;
        if (!quoted.empty() && quoted[0] != '+') {
            auto n = numberOfUuid_.find(quoted);
            if (n != numberOfUuid_.end()) quoted = n->second;
        }
        const int64_t quotedAt = (*quote)["id"].GetInteger(0);
        if (!quoted.empty() && quotedAt > 0) {
            m.replyTo.messageId = MessageIdOf(quoted, quotedAt);
            m.replyTo.senderId = quoted;
            m.replyTo.senderName = quoted == account_ ? (number_.empty() ? quoted : number_) : NameOf(quoted);
            m.replyTo.textSnippet = (*quote)["text"].GetString().substr(0, 80);
            if (const UltraIMMessage* original = FindMessage(m.replyTo.messageId)) {
                if (m.replyTo.textSnippet.empty()) m.replyTo.textSnippet = SnippetOf(*original);
            }
        }
    }
    return m;
}

void SignalProvider::FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                                   UltraIMMessagesCallback onResult) {
    std::vector<UltraIMMessage> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!chats_.count(conversationId)) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation"), {});
            return;
        }
        auto h = history_.find(conversationId);
        if (h != history_.end()) {
            const auto& list = h->second;
            size_t end = list.size();
            if (!beforeMessageId.empty()) {
                std::string id = beforeMessageId;
                if (const UltraIMMessage* m = FindMessage(beforeMessageId)) id = m->messageId;
                end = 0;
                for (size_t i = 0; i < list.size(); ++i) {
                    if (list[i].messageId == id) {
                        end = i;
                        break;
                    }
                }
            }
            const size_t count = static_cast<size_t>(limit <= 0 ? 50 : limit);
            const size_t begin = end > count ? end - count : 0;
            out.assign(list.begin() + static_cast<std::ptrdiff_t>(begin), list.begin() + static_cast<std::ptrdiff_t>(end));
        }
    }
    if (onResult) onResult(UltraIMResult::Ok(), out);
}

void SignalProvider::SearchMessages(const UltraIMMessageSearch& search, UltraIMSearchCallback onResult) {
    if (search.conversationId.empty()) {
        if (onResult)
            onResult(UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "Signal searches one conversation at a time"),
                     UltraIMSearchPage());
        return;
    }
    UltraIM_SearchByFetching(
        [this](const std::string& conversationId, const std::string& before, int limit, UltraIMMessagesCallback cb) {
            FetchMessages(conversationId, before, limit, std::move(cb));
        },
        search, std::move(onResult));
}

// ============================================================================
// NEWS: receive
// ============================================================================

void SignalProvider::ReceiveOnListener(uint64_t generation) {
    std::string account;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        account = account_;
    }
    if (account.empty()) {
        SetListenMode(ListenMode::Idle);
        return;
    }
    // Our daemon connects as the proxy setting said when it was started: the
    // setting changed, it is started again for it (signal-cli keeps the link
    // and the account on disk). When it cannot be - a proxy signal-cli has
    // no means for - it stays stopped, and the state says why.
    if (DaemonProxyChanged()) {
        Enqueue([this, generation]() {
            if (listenGeneration_.load() != generation || !DaemonProxyChanged()) return;
            SetState(UltraIMConnectionState::Reconnecting, "The proxy setting changed: signal-cli is started again");
            {
                std::lock_guard<std::mutex> lock(mutex_);
                daemonUrl_.clear();
            }
            const UltraIMResult started = EnsureDaemon(std::string());
            if (!started && listenGeneration_.load() == generation)
                SetState(UltraIMConnectionState::Error, started.message);
        });
        std::unique_lock<std::mutex> lock(listenMutex_);
        listenWake_.wait_for(lock, std::chrono::milliseconds(std::max(200, timings_.retryMs)), [this, generation]() {
            return stopFlag_.load() || listenGeneration_.load() != generation;
        });
        return;
    }
    const int timeout = std::clamp(timings_.receiveTimeoutSec, 1, 5);
    JSONValue params = JSONValue::MakeObject();
    params.Set("account", JSONValue(account));
    params.Set("timeout", JSONValue(timeout));
    // One at a time: receive answers as soon as it has as many as asked for,
    // and only after a quiet `timeout` otherwise.
    params.Set("maxMessages", JSONValue(1));
    const SignalResponse r = Rpc("receive", params, (timeout + 30) * 1000);
    if (listenGeneration_.load() != generation || stopFlag_.load()) return;
    if (!r.Ok()) {
        if (state_.load() == UltraIMConnectionState::Connected)
            SetState(UltraIMConnectionState::Reconnecting, "signal-cli: " + r.error);
        bool own = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            own = ownDaemon_;
        }
        if (own && r.status == 0 && !daemon_.IsRunning()) {
            // Our daemon is gone: started again (on another port) - unless
            // it has been meanwhile (a job before this one, for the proxy).
            Enqueue([this, generation]() {
                if (listenGeneration_.load() != generation || daemon_.IsRunning()) return;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    daemonUrl_.clear();
                }
                EnsureDaemon(std::string());
            });
        }
        std::unique_lock<std::mutex> lock(listenMutex_);
        listenWake_.wait_for(lock, std::chrono::milliseconds(std::max(200, timings_.retryMs)), [this, generation]() {
            return stopFlag_.load() || listenGeneration_.load() != generation;
        });
        return;
    }
    // (Error: the daemon had been stopped for a proxy it could not use.)
    const UltraIMConnectionState state = state_.load();
    if (state == UltraIMConnectionState::Reconnecting || state == UltraIMConnectionState::Error)
        SetState(UltraIMConnectionState::Connected);
    for (const JSONValue& item : r.result.GetElements()) {
        std::vector<UltraIMEvent> events;
        std::vector<UltraIMMessage> media;
        HandleEnvelope(item, events, media);
        if (listenGeneration_.load() != generation) return;
        for (const auto& e : events) Emit(e);
        for (const auto& m : media) QueueMedia(m);
    }
}

void SignalProvider::HandleEnvelope(const JSONValue& item, std::vector<UltraIMEvent>& events,
                                    std::vector<UltraIMMessage>& media) {
    const JSONValue& env = item["envelope"];
    if (!env.IsObject()) return;
    std::string source, groupToRead;
    std::set<std::string> touched;
    size_t hiddenBefore = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hiddenBefore = hidden_.size();
        source = KeyOf(env["sourceNumber"].GetString(), env["sourceUuid"].GetString());
        if (source.empty()) source = env["source"].GetString();
        if (source.empty()) return;
        const std::string sourceName = Trim(env["sourceName"].GetString());
        if (source != account_) {
            Contact& c = contacts_[source];
            c.key = source;
            if (c.number.empty()) c.number = env["sourceNumber"].GetString();
            if (c.uuid.empty()) c.uuid = env["sourceUuid"].GetString();
            if (!sourceName.empty()) c.seenName = sourceName;
        }

        // The conversation a data message belongs to: its group, else its 1:1.
        const auto conversationOf = [&](const JSONValue& dm, const std::string& other, bool& isGroup) {
            const std::string gid = dm["groupInfo"]["groupId"].GetString();
            isGroup = !gid.empty();
            if (isGroup && !chats_.count(kGroupPrefix + gid)) groupToRead = gid;
            return isGroup ? kGroupPrefix + gid : other;
        };
        const auto received = [&](UltraIMMessage m, bool isGroup, const std::string& other) {
            if (FindMessage(m.messageId)) return;   // (told already)
            Chat& chat = ChatFor(m.conversationId, isGroup, other, events);
            Remember(m);
            RefreshLast(chat);
            if (!m.isOwn) ++chat.unread;
            UltraIMEvent e = EventOf(UltraIMEventType::MessageReceive, providerId_, m.conversationId);
            e.contactId = m.senderId;
            e.message = m;
            events.push_back(e);
            media.push_back(m);
            touched.insert(m.conversationId);
        };
        const auto deleted = [&](const std::string& messageId, const std::string& conversationId) {
            UltraIMMessage* old = FindMessage(messageId);
            const std::string inConversation = old ? old->conversationId : conversationId;
            if (old) {
                auto& list = history_[inConversation];
                list.erase(std::remove_if(list.begin(), list.end(),
                                          [&](const UltraIMMessage& x) { return x.messageId == messageId; }),
                           list.end());
                conversationOf_.erase(messageId);
                auto chat = chats_.find(inConversation);
                if (chat != chats_.end()) RefreshLast(chat->second);
            }
            UltraIMEvent e = EventOf(UltraIMEventType::MessageDelete, providerId_, inConversation);
            e.message.providerId = providerId_;
            e.message.conversationId = inConversation;
            e.message.messageId = messageId;
            auto client = clientOf_.find(messageId);
            e.message.clientMessageId = client != clientOf_.end() ? client->second : messageId;
            events.push_back(e);
            touched.insert(inConversation);
        };
        const auto edited = [&](const std::string& messageId, const JSONValue& dm) {
            UltraIMMessage* old = FindMessage(messageId);
            if (!old) return;
            old->text = dm["message"].GetString();
            old->isEdited = true;
            UltraIMEvent e = EventOf(UltraIMEventType::MessageUpdate, providerId_, old->conversationId);
            e.message = *old;
            events.push_back(e);
            auto chat = chats_.find(old->conversationId);
            if (chat != chats_.end()) RefreshLast(chat->second);
            touched.insert(old->conversationId);
        };

        if (const JSONValue* dm = env.Find("dataMessage"); dm && dm->IsObject()) {
            bool isGroup = false;
            const std::string conversationId = conversationOf(*dm, source, isGroup);
            if (const JSONValue* del = dm->Find("remoteDelete"); del && del->IsObject()) {
                deleted(MessageIdOf(source, (*del)["targetTimestamp"].GetInteger(0)), conversationId);
            } else if (!(*dm)["message"].GetString().empty() || !(*dm)["attachments"].GetElements().empty()) {
                const int64_t ts = (*dm)["timestamp"].GetInteger(env["timestamp"].GetInteger(0));
                received(MessageFrom(*dm, source, ts, conversationId, source == account_), isGroup, source);
            } else if (isGroup) {
                // A group changed (made, renamed, someone in or out): read again.
                groupToRead = (*dm)["groupInfo"]["groupId"].GetString();
            }
        }
        if (const JSONValue* edit = env.Find("editMessage"); edit && edit->IsObject()) {
            edited(MessageIdOf(source, (*edit)["targetSentTimestamp"].GetInteger(0)), (*edit)["dataMessage"]);
        }
        if (const JSONValue* sync = env.Find("syncMessage"); sync && sync->IsObject()) {
            // Sent from the phone (or another linked device): ours.
            if (const JSONValue* sent = sync->Find("sentMessage"); sent && sent->IsObject()) {
                const std::string other = KeyOf((*sent)["destinationNumber"].GetString(), (*sent)["destinationUuid"].GetString());
                bool isGroup = false;
                const std::string conversationId = conversationOf(*sent, other.empty() ? (*sent)["destination"].GetString() : other, isGroup);
                if (!conversationId.empty()) {
                    if (const JSONValue* del = sent->Find("remoteDelete"); del && del->IsObject()) {
                        deleted(MessageIdOf(account_, (*del)["targetTimestamp"].GetInteger(0)), conversationId);
                    } else if (!(*sent)["message"].GetString().empty() || !(*sent)["attachments"].GetElements().empty()) {
                        const int64_t ts = (*sent)["timestamp"].GetInteger(env["timestamp"].GetInteger(0));
                        received(MessageFrom(*sent, account_, ts, conversationId, true), isGroup, conversationId);
                    }
                }
            }
            // Read on another device: nothing unread here either.
            for (const JSONValue& read : (*sync)["readMessages"].GetElements()) {
                std::string sender = KeyOf(read["senderNumber"].GetString(), read["senderUuid"].GetString());
                if (sender.empty()) sender = read["sender"].GetString();
                const UltraIMMessage* m = FindMessage(MessageIdOf(sender, read["timestamp"].GetInteger(0)));
                if (!m) continue;
                receipted_.insert(m->messageId);
                auto chat = chats_.find(m->conversationId);
                if (chat != chats_.end() && chat->second.unread > 0) {
                    chat->second.unread = 0;
                    touched.insert(chat->first);
                }
            }
        }
        if (const JSONValue* receipt = env.Find("receiptMessage"); receipt && receipt->IsObject()) {
            const bool read = (*receipt)["isRead"].GetBoolean(false) || (*receipt)["isViewed"].GetBoolean(false);
            const bool delivered = (*receipt)["isDelivery"].GetBoolean(false);
            if (read || delivered) {
                const UltraIMDeliveryStatus status = read ? UltraIMDeliveryStatus::Read : UltraIMDeliveryStatus::Delivered;
                for (const JSONValue& t : (*receipt)["timestamps"].GetElements()) {
                    const std::string ownId = MessageIdOf(account_, t.GetInteger(0));
                    UltraIMMessage* m = FindMessage(ownId);
                    if (!m) {
                        // Ours, but the send that made it is still returning
                        // (the other side was quicker): kept until it is recorded.
                        UltraIMDeliveryStatus& early = earlyReceipts_[ownId];
                        if (status == UltraIMDeliveryStatus::Read || early != UltraIMDeliveryStatus::Read) early = status;
                        if (earlyReceipts_.size() > 1000) earlyReceipts_.erase(earlyReceipts_.begin());
                        continue;
                    }
                    if (!m->isOwn) continue;
                    // Never back: a read message is not "delivered" again.
                    if (m->deliveryStatus == UltraIMDeliveryStatus::Read ||
                        (m->deliveryStatus == UltraIMDeliveryStatus::Delivered && !read))
                        continue;
                    m->deliveryStatus = status;
                    UltraIMEvent e = EventOf(UltraIMEventType::MessageDelivered, providerId_, m->conversationId);
                    e.contactId = source;
                    e.message = *m;
                    e.deliveryStatus = status;
                    events.push_back(e);
                }
            }
        }
        if (const JSONValue* typing = env.Find("typingMessage"); typing && typing->IsObject()) {
            const std::string gid = (*typing)["groupId"].GetString();
            UltraIMEvent e = EventOf(UltraIMEventType::TypingChange, providerId_, gid.empty() ? source : kGroupPrefix + gid);
            e.contactId = source;
            e.isTyping = (*typing)["action"].GetString() == "STARTED";
            events.push_back(e);
        }
    }
    if (!groupToRead.empty()) {
        // A group not known here yet, or changed: its name and members.
        std::vector<UltraIMEvent> groupEvents;
        RefreshGroup(groupToRead, groupEvents);
        // Before the message: the list has the group when the message comes.
        events.insert(events.begin(), groupEvents.begin(), groupEvents.end());
        touched.erase(kGroupPrefix + groupToRead);
    }
    bool unhidden = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        unhidden = hidden_.size() != hiddenBefore;
    }
    if (unhidden) SaveSession();    // (a conversation left here, written to again)
    std::lock_guard<std::mutex> lock(mutex_);
    for (const std::string& id : touched) {
        auto chat = chats_.find(id);
        if (chat == chats_.end()) continue;
        UltraIMEvent e = EventOf(UltraIMEventType::ConversationUpdate, providerId_, id);
        e.conversation = ConversationOf(chat->second);
        events.push_back(e);
    }
    if (!groupToRead.empty()) {
        // (Its ConversationUpdate came first; the snippet with this one.)
        auto chat = chats_.find(kGroupPrefix + groupToRead);
        if (chat != chats_.end() && chat->second.hasLast) {
            UltraIMEvent e = EventOf(UltraIMEventType::ConversationUpdate, providerId_, chat->first);
            e.conversation = ConversationOf(chat->second);
            events.push_back(e);
        }
    }
}

// ============================================================================
// ATTACHMENTS
// ============================================================================

std::string SignalProvider::MediaPath(const std::string& id, const std::string& fileName) const {
    std::vector<uint8_t> digest;
    UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, id.data(), id.size(), digest);
    const std::string base = config_.cacheDirectory.empty() ? config_.dataDirectory : config_.cacheDirectory;
    std::error_code ec;
    const std::filesystem::path dir = UltraCanvas::PathFromUtf8(base) / UltraCanvas::PathFromUtf8(providerId_) / "media";
    std::filesystem::create_directories(dir, ec);
    return UltraCanvas::PathToUtf8(dir / UltraCanvas::PathFromUtf8(UltraCrypt_ToHex(digest).substr(0, 24) +
                                                                     LowerExtension(fileName)));
}

UltraIMResult SignalProvider::FetchFile(UltraIMMessage& m) {
    const std::string id = m.attachment.remoteUrl;
    if (id.empty()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No attachment");
    std::string ext = LowerExtension(id);
    if (ext.empty()) ext = LowerExtension(m.attachment.fileName);
    if (ext.empty()) ext = ExtensionFor(m.attachment.mimeType);
    std::error_code ec;
    bool own = false;
    std::string account;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        own = ownDaemon_;
        account = account_;
    }
    if (own) {
        // signal-cli keeps what it received in <its data>/attachments/<id>;
        // with its extension in the id, the file serves as it is.
        const std::filesystem::path kept = UltraCanvas::PathFromUtf8(ConfigDirectory()) / "attachments" / UltraCanvas::PathFromUtf8(id);
        if (std::filesystem::is_regular_file(kept, ec)) {
            if (!LowerExtension(id).empty()) {
                m.attachment.localPath = UltraCanvas::PathToUtf8(kept);
                m.attachment.isDownloaded = true;
                return UltraIMResult::Ok();
            }
            const std::string copy = MediaPath(id, "x" + ext);
            std::filesystem::copy_file(kept, UltraCanvas::PathFromUtf8(copy), std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) {
                m.attachment.localPath = copy;
                m.attachment.isDownloaded = true;
                return UltraIMResult::Ok();
            }
        }
    }
    const std::string path = MediaPath(id, "x" + ext);
    if (!std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(path), ec)) {
        JSONValue params = JSONValue::MakeObject();
        params.Set("account", JSONValue(account));
        params.Set("id", JSONValue(id));
        if (IsGroupId(m.conversationId)) params.Set("groupId", JSONValue(m.conversationId.substr(std::string(kGroupPrefix).size())));
        else params.Set("recipient", JSONValue(m.senderId));
        const SignalResponse r = Rpc("getAttachment", params, 300000);
        if (!r.Ok())
            return UltraIMResult::Error(r.status == 0 ? UltraIMResultCode::NetworkError : UltraIMResultCode::NotFound,
                                        "signal-cli has not the file: " + r.error);
        const std::vector<uint8_t> bytes = UltraCanvas::Base64Decode(r.result["data"].GetString());
        if (!WriteWholeFile(path, bytes)) return UltraIMResult::Error(UltraIMResultCode::PluginError, "Could not write " + path);
    }
    m.attachment.localPath = path;
    m.attachment.isDownloaded = true;
    return UltraIMResult::Ok();
}

void SignalProvider::QueueMedia(const UltraIMMessage& message) {
    // A picture comes by itself; the rest when asked.
    if (message.type != UltraIMMessageType::Image || message.attachment.remoteUrl.empty() ||
        message.attachment.isDownloaded)
        return;
    Enqueue([this, message]() {
        UltraIMMessage m = message;
        if (!FetchFile(m)) return;
        UltraIMMessage updated = m;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (UltraIMMessage* kept = FindMessage(message.messageId)) {
                kept->attachment.localPath = m.attachment.localPath;
                kept->attachment.isDownloaded = true;
                updated = *kept;
            }
        }
        UltraIMEvent e = EventOf(UltraIMEventType::MessageUpdate, providerId_, updated.conversationId);
        e.message = updated;
        Emit(e);
    });
}

void SignalProvider::DownloadAttachment(const std::string& conversationId, const std::string& messageId,
                                        UltraIMAttachmentCallback onResult) {
    (void)conversationId;
    Enqueue([this, messageId, onResult]() {
        UltraIMMessage m;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const UltraIMMessage* kept = FindMessage(messageId);
            if (!kept || (kept->attachment.remoteUrl.empty() && kept->attachment.localPath.empty())) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No attachment"), UltraIMAttachment());
                return;
            }
            m = *kept;
        }
        if (m.attachment.isDownloaded && !m.attachment.localPath.empty()) {
            if (onResult) onResult(UltraIMResult::Ok(), m.attachment);
            return;
        }
        const UltraIMResult r = FetchFile(m);
        if (!r) {
            if (onResult) onResult(r, UltraIMAttachment());
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (UltraIMMessage* kept = FindMessage(m.messageId)) {
                kept->attachment = m.attachment;
                m = *kept;
            }
        }
        if (onResult) onResult(UltraIMResult::Ok(), m.attachment);
        UltraIMEvent e = EventOf(UltraIMEventType::MessageUpdate, providerId_, m.conversationId);
        e.message = m;
        Emit(e);
    });
}

// ============================================================================
// SENDING
// ============================================================================

void SignalProvider::Target(const Chat& chat, JSONValue& params) const {
    params.Set("account", JSONValue(account_));
    if (chat.isGroup) params.Set("groupId", JSONValue(chat.groupId));
    else params.Set("recipient", StringArray({chat.contactKey}));
}

UltraIMResult SignalProvider::SendText(const std::string& conversationId, const std::string& text,
                                       const UltraIMSendOptions& options) {
    return SendSignal(conversationId, text, std::string(), UltraIMMessageType::Text, options);
}

UltraIMResult SignalProvider::SendImage(const std::string& conversationId, const std::string& localPath,
                                        const std::string& caption, const UltraIMSendOptions& options) {
    return SendSignal(conversationId, caption, localPath, UltraIMMessageType::Image, options);
}

UltraIMResult SignalProvider::SendVideo(const std::string& conversationId, const std::string& localPath,
                                        const std::string& caption, const UltraIMSendOptions& options) {
    return SendSignal(conversationId, caption, localPath, UltraIMMessageType::Video, options);
}

UltraIMResult SignalProvider::SendVoiceNote(const std::string& conversationId, const std::string& localPath,
                                            const UltraIMSendOptions& options) {
    return SendSignal(conversationId, std::string(), localPath, UltraIMMessageType::Audio, options);
}

UltraIMResult SignalProvider::SendFile(const std::string& conversationId, const std::string& localPath,
                                       const UltraIMSendOptions& options) {
    return SendSignal(conversationId, std::string(), localPath, UltraIMMessageType::File, options);
}

UltraIMResult SignalProvider::SendSignal(const std::string& conversationId, const std::string& text,
                                         const std::string& localPath, UltraIMMessageType type,
                                         const UltraIMSendOptions& options) {
    if (state_.load() != UltraIMConnectionState::Connected && state_.load() != UltraIMConnectionState::Reconnecting)
        return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Signal");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!chats_.count(conversationId)) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation");
    }
    int64_t size = 0;
    if (!localPath.empty()) {
        std::error_code ec;
        const std::filesystem::path p = UltraCanvas::PathFromUtf8(localPath);
        if (!std::filesystem::is_regular_file(p, ec))
            return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "There is no file " + localPath);
        size = static_cast<int64_t>(std::filesystem::file_size(p, ec));
    } else if (Trim(text).empty()) {
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Nothing to send");
    }
    const std::string clientId = options.clientMessageId.empty() ? UltraIM_NewClientMessageId() : options.clientMessageId;
    Enqueue([this, conversationId, text, localPath, type, options, clientId, size]() {
        const auto progress = [&](int64_t done) {
            UltraIMEvent e = EventOf(UltraIMEventType::TransferProgress, providerId_, conversationId);
            e.message.conversationId = conversationId;
            e.message.clientMessageId = clientId;
            e.message.messageId = clientId;
            e.transferredBytes = done;
            e.totalBytes = size;
            e.isUpload = true;
            Emit(e);
        };
        const auto failed = [&](const std::string& why) {
            UltraIMEvent e = EventOf(UltraIMEventType::MessageDelivered, providerId_, conversationId);
            e.message.conversationId = conversationId;
            e.message.clientMessageId = clientId;
            e.message.messageId = clientId;
            e.deliveryStatus = UltraIMDeliveryStatus::Failed;
            e.errorText = why;
            Emit(e);
        };
        JSONValue params = JSONValue::MakeObject();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto chat = chats_.find(conversationId);
            if (chat == chats_.end()) return failed("No such conversation");
            Target(chat->second, params);
            if (!options.replyToMessageId.empty()) {
                if (const UltraIMMessage* quoted = FindMessage(options.replyToMessageId)) {
                    std::string author;
                    int64_t at = 0;
                    if (SplitMessageId(quoted->messageId, author, at)) {
                        params.Set("quoteTimestamp", JSONValue(at));
                        params.Set("quoteAuthor", JSONValue(author));
                        params.Set("quoteMessage", JSONValue(quoted->text));
                    }
                }
            }
        }
        if (!text.empty()) params.Set("message", JSONValue(text));
        if (!localPath.empty()) {
            // signal-cli reads the file from disk itself.
            params.Set("attachments", StringArray({localPath}));
            if (type == UltraIMMessageType::Audio) params.Set("voiceNote", JSONValue(true));
            progress(0);
        }
        const SignalResponse r = Rpc("send", params, localPath.empty() ? 60000 : 900000);
        if (!r.Ok()) return failed(r.error);
        const auto& results = r.result["results"].GetElements();
        bool anyDelivered = results.empty();
        std::string why;
        for (const JSONValue& x : results) {
            const std::string kind = x["type"].GetString();
            if (kind == "SUCCESS" || kind.empty()) anyDelivered = true;
            else if (why.empty()) why = kind == "UNREGISTERED_FAILURE" ? "Not on Signal (any more)" : kind;
        }
        if (!anyDelivered) return failed(why);
        const int64_t ts = r.result["timestamp"].GetInteger(0);
        if (!localPath.empty()) progress(size);
        UltraIMMessage sent;
        UltraIMConversation conversation;
        UltraIMDeliveryStatus lateStatus = UltraIMDeliveryStatus::Sent;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            JSONValue dm = JSONValue::MakeObject();
            dm.Set("message", JSONValue(text));
            const std::string id = MessageIdOf(account_, ts);
            clientOf_[id] = clientId;
            sent = MessageFrom(dm, account_, ts, conversationId, true);
            if (params.Find("quoteTimestamp")) {
                const std::string quoted = MessageIdOf(params["quoteAuthor"].GetString(), params["quoteTimestamp"].GetInteger(0));
                sent.replyTo.messageId = quoted;
                if (const UltraIMMessage* original = FindMessage(quoted)) {
                    sent.replyTo.senderId = original->senderId;
                    sent.replyTo.senderName = original->senderName;
                    sent.replyTo.textSnippet = SnippetOf(*original);
                }
            }
            if (!localPath.empty()) {
                sent.type = type;
                sent.attachment.localPath = localPath;
                sent.attachment.isDownloaded = true;
                sent.attachment.fileName = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(localPath).filename());
                sent.attachment.mimeType = MimeFor(localPath);
                sent.attachment.fileSizeBytes = size;
                if (options.durationSeconds > 0) sent.attachment.durationSeconds = options.durationSeconds;
                if (type == UltraIMMessageType::File) sent.type = UltraIMMessageType::File;
            }
            Remember(sent);
            // The other side's receipt was quicker than the send's answer.
            if (auto early = earlyReceipts_.find(id); early != earlyReceipts_.end()) {
                lateStatus = early->second;
                earlyReceipts_.erase(early);
                if (UltraIMMessage* kept = FindMessage(id)) kept->deliveryStatus = lateStatus;
            }
            auto chat = chats_.find(conversationId);
            if (chat != chats_.end()) {
                RefreshLast(chat->second);
                conversation = ConversationOf(chat->second);
            }
        }
        UltraIMEvent delivered = EventOf(UltraIMEventType::MessageDelivered, providerId_, conversationId);
        delivered.message = sent;
        delivered.deliveryStatus = UltraIMDeliveryStatus::Sent;
        Emit(delivered);
        // The row learns the message's id (for an edit, a delete, a reply).
        UltraIMEvent updated = EventOf(UltraIMEventType::MessageUpdate, providerId_, conversationId);
        updated.message = sent;
        Emit(updated);
        if (!conversation.conversationId.empty()) {
            UltraIMEvent changed = EventOf(UltraIMEventType::ConversationUpdate, providerId_, conversationId);
            changed.conversation = conversation;
            Emit(changed);
        }
        if (lateStatus != UltraIMDeliveryStatus::Sent) {
            UltraIMEvent late = EventOf(UltraIMEventType::MessageDelivered, providerId_, conversationId);
            late.message = sent;
            late.message.deliveryStatus = lateStatus;
            late.deliveryStatus = lateStatus;
            Emit(late);
        }
    });
    return UltraIMResult::Ok();
}

// ============================================================================
// EDITING, DELETING, READING, TYPING
// ============================================================================

UltraIMResult SignalProvider::EditMessage(const std::string& conversationId, const std::string& messageId,
                                          const std::string& newText) {
    std::string id;
    int64_t at = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const UltraIMMessage* m = FindMessage(messageId);
        if (!m) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such message");
        if (!m->isOwn) return UltraIMResult::Error(UltraIMResultCode::PermissionDenied, "Only its sender can change a message");
        if (Trim(newText).empty() && m->attachment.remoteUrl.empty() && m->attachment.localPath.empty())
            return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "A text cannot be empty - delete it instead");
        std::string author;
        if (!SplitMessageId(m->messageId, author, at)) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such message");
        id = m->messageId;
    }
    Enqueue([this, conversationId, id, at, newText]() {
        JSONValue params = JSONValue::MakeObject();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto chat = chats_.find(conversationId);
            if (chat == chats_.end()) return;
            Target(chat->second, params);
        }
        params.Set("message", JSONValue(newText));
        // Signal names the message edited by its first timestamp, at every edit.
        params.Set("editTimestamp", JSONValue(at));
        const SignalResponse r = Rpc("send", params, 60000);
        if (!r.Ok()) return;
        UltraIMMessage m;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            UltraIMMessage* kept = FindMessage(id);
            if (!kept) return;
            kept->text = newText;
            kept->isEdited = true;
            m = *kept;
            auto chat = chats_.find(conversationId);
            if (chat != chats_.end()) RefreshLast(chat->second);
        }
        UltraIMEvent e = EventOf(UltraIMEventType::MessageUpdate, providerId_, conversationId);
        e.message = m;
        Emit(e);
    });
    return UltraIMResult::Ok();
}

UltraIMDeleteChoices SignalProvider::GetDeleteChoices(const std::string& conversationId,
                                                      const std::string& messageId) const {
    (void)conversationId;
    UltraIMDeleteChoices choices;
    std::lock_guard<std::mutex> lock(mutex_);
    const UltraIMMessage* m = FindMessage(messageId);
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
    choices.forEveryone = m && m->isOwn && now - m->timestampMs < kDeleteWindowMs;
    return choices;
}

UltraIMResult SignalProvider::DeleteMessage(const std::string& conversationId, const std::string& messageId,
                                            UltraIMDeleteScope scope) {
    if (scope == UltraIMDeleteScope::ForMe)
        return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "Signal here deletes for everyone only");
    std::string id, clientId;
    int64_t at = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const UltraIMMessage* m = FindMessage(messageId);
        if (!m) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such message");
        if (!m->isOwn) return UltraIMResult::Error(UltraIMResultCode::PermissionDenied, "Only its sender can delete a message");
        std::string author;
        if (!SplitMessageId(m->messageId, author, at)) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such message");
        id = m->messageId;
        clientId = m->clientMessageId;
    }
    Enqueue([this, conversationId, id, clientId, at]() {
        JSONValue params = JSONValue::MakeObject();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto chat = chats_.find(conversationId);
            if (chat == chats_.end()) return;
            Target(chat->second, params);
        }
        params.Set("targetTimestamp", JSONValue(at));
        const SignalResponse r = Rpc("remoteDelete", params, 60000);
        if (!r.Ok()) return;
        UltraIMConversation conversation;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& list = history_[conversationId];
            list.erase(std::remove_if(list.begin(), list.end(), [&](const UltraIMMessage& x) { return x.messageId == id; }),
                       list.end());
            conversationOf_.erase(id);
            auto chat = chats_.find(conversationId);
            if (chat != chats_.end()) {
                RefreshLast(chat->second);
                conversation = ConversationOf(chat->second);
            }
        }
        UltraIMEvent e = EventOf(UltraIMEventType::MessageDelete, providerId_, conversationId);
        e.message.providerId = providerId_;
        e.message.conversationId = conversationId;
        e.message.messageId = id;
        e.message.clientMessageId = clientId;
        Emit(e);
        if (!conversation.conversationId.empty()) {
            UltraIMEvent changed = EventOf(UltraIMEventType::ConversationUpdate, providerId_, conversationId);
            changed.conversation = conversation;
            Emit(changed);
        }
    });
    return UltraIMResult::Ok();
}

UltraIMResult SignalProvider::MarkRead(const std::string& conversationId, const std::string& upToMessageId) {
    std::map<std::string, std::vector<int64_t>> bySender;
    std::string account;
    UltraIMConversation conversation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto chat = chats_.find(conversationId);
        if (chat == chats_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation");
        account = account_;
        std::string upTo = upToMessageId;
        if (const UltraIMMessage* m = upTo.empty() ? nullptr : FindMessage(upTo)) upTo = m->messageId;
        for (const UltraIMMessage& m : history_[conversationId]) {
            if (!m.isOwn && receipted_.insert(m.messageId).second) {
                std::string author;
                int64_t at = 0;
                if (SplitMessageId(m.messageId, author, at)) bySender[author].push_back(at);
            }
            if (!upTo.empty() && m.messageId == upTo) break;
        }
        const bool changed = chat->second.unread != 0;
        chat->second.unread = 0;
        if (changed) conversation = ConversationOf(chat->second);
    }
    if (!conversation.conversationId.empty()) {
        UltraIMEvent e = EventOf(UltraIMEventType::ConversationUpdate, providerId_, conversationId);
        e.conversation = conversation;
        Emit(e);
    }
    if (bySender.empty()) return UltraIMResult::Ok();
    Enqueue([this, account, bySender]() {
        for (const auto& [sender, stamps] : bySender) {
            JSONValue params = JSONValue::MakeObject();
            params.Set("account", JSONValue(account));
            params.Set("recipient", JSONValue(sender));
            JSONValue list = JSONValue::MakeArray();
            for (int64_t t : stamps) list.Append(JSONValue(t));
            params.Set("targetTimestamp", list);
            params.Set("type", JSONValue("read"));
            Rpc("sendReceipt", params, 30000);
        }
    });
    return UltraIMResult::Ok();
}

UltraIMResult SignalProvider::SetTyping(const std::string& conversationId, bool isTyping) {
    JSONValue params = JSONValue::MakeObject();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto chat = chats_.find(conversationId);
        if (chat == chats_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation");
        Target(chat->second, params);
    }
    if (!isTyping) params.Set("stop", JSONValue(true));
    Enqueue([this, params]() { Rpc("sendTyping", params, 20000); });
    return UltraIMResult::Ok();
}

// ============================================================================
// CONTACTS AND GROUPS MADE
// ============================================================================

UltraIMResult SignalProvider::ResolveAddress(const std::string& address, Contact& out) {
    std::string text = Trim(address);
    while (!text.empty() && text[0] == '@') text.erase(0, 1);
    if (text.empty()) return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Whom? (+49 151 ... or a username)");
    std::string account;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        account = account_;
        auto known = contacts_.find(text);
        if (known != contacts_.end()) {
            out = known->second;
            return UltraIMResult::Ok();
        }
    }
    const std::string number = NormalizeNumber(text);
    const bool username = number.empty() && LooksLikeUsername(text);
    if (number.empty() && !username)
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "A phone number in international form (+49 151 ...) or a Signal username (name.01)");
    JSONValue params = JSONValue::MakeObject();
    params.Set("account", JSONValue(account));
    if (username) params.Set("username", StringArray({text}));
    else params.Set("recipient", StringArray({number}));
    const SignalResponse r = Rpc("getUserStatus", params, 30000);
    if (!r.Ok())
        return UltraIMResult::Error(r.status == 0 ? UltraIMResultCode::NetworkError : UltraIMResultCode::NotFound,
                                    "Signal could not look up " + text + ": " + r.error);
    const auto& list = r.result.GetElements();
    if (list.empty() || !list.front()["isRegistered"].GetBoolean(false))
        return UltraIMResult::Error(UltraIMResultCode::NotFound, text + " is not on Signal");
    const JSONValue& status = list.front();
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string uuid = status["uuid"].GetString();
    const std::string key = KeyOf(username ? status["number"].GetString() : number, uuid);
    Contact& c = contacts_[key];
    c.key = key;
    if (c.number.empty() && !username) c.number = number;
    if (c.uuid.empty()) c.uuid = uuid;
    if (username) c.username = text;
    out = c;
    return UltraIMResult::Ok();
}

void SignalProvider::AddContact(const std::string& address, UltraIMConversationCallback onResult) {
    Enqueue([this, address, onResult]() {
        if (state_.load() != UltraIMConnectionState::Connected) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Signal"), {});
            return;
        }
        Contact contact;
        const UltraIMResult r = ResolveAddress(address, contact);
        if (!r) {
            if (onResult) onResult(r, {});
            return;
        }
        std::vector<UltraIMEvent> events;
        UltraIMConversation conversation;
        bool unhidden = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (contact.key == account_) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "That is you"), {});
                return;
            }
            unhidden = hidden_.count(contact.key) > 0;
            Chat& chat = ChatFor(contact.key, false, contact.key, events);
            conversation = ConversationOf(chat);
        }
        if (unhidden) SaveSession();
        UltraIMEvent e = EventOf(UltraIMEventType::ConversationUpdate, providerId_, conversation.conversationId);
        e.conversation = conversation;
        Emit(e);
        if (onResult) onResult(UltraIMResult::Ok(), conversation);
    });
}

void SignalProvider::CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                                 UltraIMConversationCallback onResult) {
    Enqueue([this, title, memberIds, onResult]() {
        if (state_.load() != UltraIMConnectionState::Connected) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to Signal"), {});
            return;
        }
        std::vector<std::string> members;
        std::string account;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            account = account_;
        }
        for (const std::string& address : memberIds) {
            Contact c;
            const UltraIMResult r = ResolveAddress(address, c);
            if (!r) {
                if (onResult) onResult(r, {});
                return;
            }
            if (c.key != account && std::find(members.begin(), members.end(), c.key) == members.end()) members.push_back(c.key);
        }
        if (members.empty()) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "A group needs someone besides you"), {});
            return;
        }
        JSONValue params = JSONValue::MakeObject();
        params.Set("account", JSONValue(account));
        params.Set("name", JSONValue(Trim(title)));
        params.Set("members", StringArray(members));
        const SignalResponse r = Rpc("updateGroup", params, 60000);
        const std::string gid = r.Ok() ? r.result["groupId"].GetString() : std::string();
        if (gid.empty()) {
            if (onResult)
                onResult(UltraIMResult::Error(r.status == 0 ? UltraIMResultCode::NetworkError : UltraIMResultCode::PermissionDenied,
                                              "Signal did not make the group: " + (r.error.empty() ? "no group id" : r.error)),
                         {});
            return;
        }
        std::vector<UltraIMEvent> events;
        if (!RefreshGroup(gid, events)) {
            // Not listed yet: as it was asked for.
            std::lock_guard<std::mutex> lock(mutex_);
            Chat& chat = ChatFor(kGroupPrefix + gid, true, std::string(), events);
            chat.groupName = Trim(title);
            chat.members = members;
            chat.members.insert(chat.members.begin(), account);
            UltraIMEvent e = EventOf(UltraIMEventType::ConversationUpdate, providerId_, chat.id);
            e.conversation = ConversationOf(chat);
            events.push_back(e);
        }
        UltraIMConversation conversation;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto chat = chats_.find(kGroupPrefix + gid);
            if (chat != chats_.end()) conversation = ConversationOf(chat->second);
        }
        for (const auto& e : events) Emit(e);
        if (onResult) onResult(UltraIMResult::Ok(), conversation);
    });
}

void SignalProvider::LeaveConversation(const std::string& conversationId, UltraIMResultCallback onResult) {
    Enqueue([this, conversationId, onResult]() {
        Chat chat;
        std::string account;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = chats_.find(conversationId);
            if (it == chats_.end()) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation"));
                return;
            }
            chat = it->second;
            account = account_;
        }
        if (chat.isGroup) {
            JSONValue params = JSONValue::MakeObject();
            params.Set("account", JSONValue(account));
            params.Set("groupId", JSONValue(chat.groupId));
            const SignalResponse r = Rpc("quitGroup", params, 60000);
            if (!r.Ok()) {
                if (onResult)
                    onResult(UltraIMResult::Error(r.status == 0 ? UltraIMResultCode::NetworkError : UltraIMResultCode::PermissionDenied,
                                                  "Signal did not leave the group: " + r.error));
                return;
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            chats_.erase(conversationId);
            auto h = history_.find(conversationId);
            if (h != history_.end()) {
                for (const UltraIMMessage& m : h->second) conversationOf_.erase(m.messageId);
                history_.erase(h);
            }
            if (!chat.isGroup) hidden_.insert(conversationId);
        }
        if (!chat.isGroup) SaveSession();
        Emit(EventOf(UltraIMEventType::ConversationRemove, providerId_, conversationId));
        if (onResult) onResult(UltraIMResult::Ok());
    });
}

} // namespace UltraIMSignal
