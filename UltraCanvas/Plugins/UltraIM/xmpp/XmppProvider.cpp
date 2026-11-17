// Plugins/UltraIM/xmpp/XmppProvider.cpp
// XMPP - chats, rooms and the roster of an XMPP account as UltraIM conversations
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "XmppProvider.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCrypt/UltraCryptCore.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace UltraIMXmpp {

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

bool StartsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

std::string HostOf(const std::string& url) {
    std::string host = url;
    const size_t scheme = host.find("://");
    if (scheme != std::string::npos) host = host.substr(scheme + 3);
    const size_t end = host.find_first_of("/?#");
    if (end != std::string::npos) host = host.substr(0, end);
    if (!host.empty() && host.front() == '[') return host.substr(1, host.find(']') - 1);
    const size_t colon = host.rfind(':');
    if (colon != std::string::npos) host = host.substr(0, colon);
    return Lower(host);
}

// This computer: the one place a connection without TLS is taken (tests).
bool IsLoopback(const std::string& url) {
    const std::string host = HostOf(url);
    return host == "127.0.0.1" || host == "localhost" || host == "::1";
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
    if (StartsWith(mime, "image/")) return UltraIMMessageType::Image;
    if (StartsWith(mime, "video/")) return UltraIMMessageType::Video;
    if (StartsWith(mime, "audio/")) return UltraIMMessageType::Audio;
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

// The file's name in an upload's link: its last segment, %-decoded.
std::string UrlFileName(const std::string& url) {
    std::string path = url;
    const size_t end = path.find_first_of("?#");
    if (end != std::string::npos) path = path.substr(0, end);
    const size_t slash = path.find_last_of('/');
    const std::string segment = slash == std::string::npos ? path : path.substr(slash + 1);
    std::string out;
    for (size_t i = 0; i < segment.size(); ++i) {
        if (segment[i] == '%' && i + 2 < segment.size() && std::isxdigit(static_cast<unsigned char>(segment[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(segment[i + 2]))) {
            out += static_cast<char>(std::stoi(segment.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += segment[i];
        }
    }
    // A name for a file on this disk: no folders in it.
    for (char& c : out) {
        if (c == '/' || c == '\\' || static_cast<unsigned char>(c) < 0x20) c = '_';
    }
    return out.empty() ? "file" : out;
}

std::string PercentEncode(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

UltraIMPresence PresenceOfShow(const std::string& show) {
    if (show == "away" || show == "xa") return UltraIMPresence::Away;
    if (show == "dnd") return UltraIMPresence::DoNotDisturb;
    return UltraIMPresence::Online;
}

int Rank(UltraIMPresence p) {
    switch (p) {
        case UltraIMPresence::Online: return 0;
        case UltraIMPresence::Away: return 1;
        case UltraIMPresence::DoNotDisturb: return 2;
        default: return 3;
    }
}

int StatusRank(UltraIMDeliveryStatus s) {
    switch (s) {
        case UltraIMDeliveryStatus::Sending: return 0;
        case UltraIMDeliveryStatus::Sent: return 1;
        case UltraIMDeliveryStatus::Delivered: return 2;
        case UltraIMDeliveryStatus::Read: return 3;
        default: return -1;
    }
}

// A data form (XEP-0004) to submit: FORM_TYPE and the fields.
XmlElement SubmitForm(const std::string& formType, const std::vector<std::pair<std::string, std::string>>& fields) {
    XmlElement x("x", Ns::Data);
    x.Set("type", "submit");
    XmlElement& type = x.Add("field");
    type.Set("var", "FORM_TYPE").Set("type", "hidden");
    type.Add("value").SetText(formType);
    for (const auto& [var, value] : fields) {
        XmlElement& f = x.Add("field");
        f.Set("var", var);
        f.Add("value").SetText(value);
    }
    return x;
}

std::vector<std::string> StatusCodes(const XmlElement& presence) {
    std::vector<std::string> codes;
    for (const XmlElement* x : presence.Children("x", Ns::MucUser)) {
        for (const XmlElement* s : x->Children("status")) codes.push_back(s->Attr("code"));
    }
    return codes;
}

bool HasCode(const std::vector<std::string>& codes, const char* code) {
    return std::find(codes.begin(), codes.end(), code) != codes.end();
}

UltraIMEvent EventOf(UltraIMEventType type, const std::string& providerId, const std::string& conversationId) {
    UltraIMEvent e;
    e.type = type;
    e.providerId = providerId;
    e.conversationId = conversationId;
    return e;
}

// The time a stanza was sent, when it says (XEP-0203), else now.
int64_t StampOf(const XmlElement& m, int64_t otherwise) {
    if (const XmlElement* d = m.Child("delay", Ns::Delay)) {
        const int64_t t = ParseXmppTime(d->Attr("stamp"));
        if (t > 0) return t;
    }
    return otherwise;
}

// The server's stanza-id (XEP-0359) - trusted only from the archive it names.
std::string StanzaIdBy(const XmlElement& m, const std::string& by) {
    for (const XmlElement* s : m.Children("stanza-id", Ns::Sid)) {
        if (JidBare(s->Attr("by")) == by) return s->Attr("id");
    }
    return std::string();
}

std::string RetractTarget(const XmlElement& m) {
    if (const XmlElement* r = m.Child("retract", Ns::Retract)) return r->Attr("id");
    if (const XmlElement* a = m.Child("apply-to", Ns::Fasten)) {
        if (a->Child("retract", Ns::RetractOld)) return a->Attr("id");
    }
    return std::string();
}

} // namespace

// ============================================================================
// LIFETIME
// ============================================================================

XmppProvider::XmppProvider(std::string providerId) : providerId_(std::move(providerId)) {}

XmppProvider::~XmppProvider() {
    Shutdown();
}

UltraIMCapabilityFlags XmppProvider::GetCapabilities() const {
    using F = UltraIMCapabilityFlags;
    return F::TextMessages | F::GroupChats | F::ImageMessages | F::FileTransfer | F::ReplyToMessage |
           F::EditMessage | F::DeleteMessage | F::ReadReceipts | F::TypingIndicator | F::OfflineMessages |
           F::MultiDevice | F::AddContact | F::CreateGroup | F::SearchMessages | F::LeaveConversation | F::VoiceNotes;
}

UltraIMResult XmppProvider::Initialize(const UltraIMConfig& config, UltraIMEventSink sink) {
    config_ = config;
    sink_ = std::move(sink);
    if (config.dataDirectory.empty())
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "The XMPP provider needs a data directory (UltraIMConfig::dataDirectory)");
    std::error_code ec;
    const std::filesystem::path dir = UltraCanvas::PathFromUtf8(config.dataDirectory) / UltraCanvas::PathFromUtf8(providerId_);
    std::filesystem::create_directories(dir, ec);
    vault_ = UltraVault::DeviceKeyVault(UltraCanvas::PathToUtf8(dir), {"xmpp.vault", config.appId + "." + providerId_ + "."});
    vaultReady_ = true;
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        stopping_ = false;
    }
    if (!worker_.joinable()) worker_ = std::thread([this]() { WorkLoop(); });
    return UltraIMResult::Ok();
}

void XmppProvider::Shutdown() {
    SignOut();
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        stopping_ = true;
        jobs_.clear();
        timers_.clear();
    }
    workWake_.notify_all();
    if (worker_.joinable()) worker_.join();
    sink_ = nullptr;
}

void XmppProvider::Enqueue(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        if (stopping_) return;
        jobs_.push_back(std::move(job));
    }
    workWake_.notify_one();
}

void XmppProvider::EnqueueAfter(int delayMs, std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        if (stopping_) return;
        timers_.emplace(std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs), std::move(job));
    }
    workWake_.notify_one();
}

void XmppProvider::WorkLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(workMutex_);
            for (;;) {
                if (stopping_) return;
                if (!jobs_.empty()) {
                    job = std::move(jobs_.front());
                    jobs_.pop_front();
                    break;
                }
                if (!timers_.empty() && timers_.begin()->first <= std::chrono::steady_clock::now()) {
                    job = std::move(timers_.begin()->second);
                    timers_.erase(timers_.begin());
                    break;
                }
                if (timers_.empty()) workWake_.wait(lock);
                else workWake_.wait_until(lock, timers_.begin()->first);
            }
        }
        if (job) job();
    }
}

void XmppProvider::Emit(const UltraIMEvent& event) {
    if (sink_) sink_(event);
}

void XmppProvider::SetState(UltraIMConnectionState state, const std::string& error) {
    if (state_.exchange(state) == state && error.empty()) return;
    UltraIMEvent event = EventOf(UltraIMEventType::ConnectionStateChange, providerId_, std::string());
    event.connectionState = state;
    event.errorText = error;
    Emit(event);
}

std::string XmppProvider::GetLocalUserId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return account_.jid;
}

std::string XmppProvider::GetWebSocketUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return account_.url;
}

std::string XmppProvider::GetBoundJid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return boundJid_;
}

std::shared_ptr<XmppStream> XmppProvider::Stream() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stream_;
}

bool XmppProvider::Send(const XmlElement& element) {
    auto stream = Stream();
    return stream && stream->Send(element);
}

bool XmppProvider::Iq(const XmlElement& iq, XmlElement& reply) {
    auto stream = Stream();
    if (!stream) {
        reply = XmlElement("iq", Ns::Client);
        reply.Set("type", "error");
        return false;
    }
    stream->Iq(iq, reply, timings_.requestTimeoutMs);
    return IqOk(reply);
}

// ============================================================================
// WHAT IS KEPT
// ============================================================================

bool XmppProvider::SaveAccount(const Account& account) {
    if (!vaultReady_ || !account.hasKeys) return false;
    JSONValue a = JSONValue::MakeObject();
    a.Set("jid", JSONValue(account.jid));
    a.Set("url", JSONValue(account.url));
    a.Set("mechanism", JSONValue(ScramMechanismName(account.keys.hash)));
    a.Set("salt", JSONValue(Base64(account.keys.salt)));
    a.Set("iterations", JSONValue(static_cast<int64_t>(account.keys.iterations)));
    a.Set("clientKey", JSONValue(Base64(account.keys.clientKey)));
    a.Set("serverKey", JSONValue(Base64(account.keys.serverKey)));
    bool stored = false;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { stored = v.Store("account", UltraCanvas::JSON::Serialize(a)); });
    return stored;
}

bool XmppProvider::LoadAccount(Account& account) const {
    if (!vaultReady_) return false;
    std::string text;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Retrieve("account", text); });
    if (text.empty()) return false;
    UltraCanvas::JSONParseResult parsed;
    const JSONValue a = UltraCanvas::JSON::Parse(text, &parsed);
    account = Account();
    account.jid = a["jid"].GetString();
    account.url = a["url"].GetString();
    account.keys.hash = a["mechanism"].GetString() == "SCRAM-SHA-1" ? ScramHash::Sha1 : ScramHash::Sha256;
    account.keys.salt = Unbase64(a["salt"].GetString());
    account.keys.iterations = static_cast<uint32_t>(a["iterations"].GetInteger(0));
    account.keys.clientKey = Unbase64(a["clientKey"].GetString());
    account.keys.serverKey = Unbase64(a["serverKey"].GetString());
    account.hasKeys = account.keys.Valid();
    return !account.jid.empty() && !account.url.empty() && account.hasKeys;
}

void XmppProvider::ForgetAccount() {
    if (!vaultReady_) return;
    vault_.Use([](UltraVault::DeviceKeyVault& v) { v.Remove("account"); });
}

bool XmppProvider::HasStoredSession() const {
    Account account;
    return LoadAccount(account);
}

// The chats hidden on this device (LeaveConversation): not secret, a file
// of the provider's own beside the vault.
void XmppProvider::LoadLocalState() {
    const std::string path = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(config_.dataDirectory) /
                                                     UltraCanvas::PathFromUtf8(providerId_) / "state.json");
    std::string text;
    if (std::FILE* f = UltraCanvas::OpenFileUtf8(path, "rb")) {
        char buffer[4096];
        size_t n = 0;
        while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) text.append(buffer, n);
        std::fclose(f);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    hidden_.clear();
    if (text.empty()) return;
    UltraCanvas::JSONParseResult parsed;
    const JSONValue state = UltraCanvas::JSON::Parse(text, &parsed);
    if (state["jid"].GetString() != account_.jid) return;
    for (const JSONValue& id : state["hidden"].GetElements()) hidden_.insert(id.GetString());
}

void XmppProvider::SaveLocalState() {
    JSONValue state = JSONValue::MakeObject();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state.Set("jid", JSONValue(account_.jid));
        JSONValue list = JSONValue::MakeArray();
        for (const std::string& id : hidden_) list.Append(JSONValue(id));
        state.Set("hidden", list);
    }
    const std::string path = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(config_.dataDirectory) /
                                                     UltraCanvas::PathFromUtf8(providerId_) / "state.json");
    if (std::FILE* f = UltraCanvas::OpenFileUtf8(path, "wb")) {
        const std::string text = UltraCanvas::JSON::Serialize(state);
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
}

// ============================================================================
// SIGNING IN
// ============================================================================

void XmppProvider::SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) {
    if (!vaultReady_) {
        if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotInitialized, "Initialize first"));
        return;
    }
    SetState(UltraIMConnectionState::Connecting);
    Enqueue([this, credentials, onResult]() { SignInOnWorker(credentials, onResult); });
}

void XmppProvider::SignInOnWorker(UltraIMCredentials credentials, UltraIMResultCallback onResult) {
    const auto fail = [this, &onResult](const UltraIMResult& r) {
        SetState(UltraIMConnectionState::Error, r.message);
        if (onResult) onResult(r);
    };
    // A sign-in replaces the connection there is.
    std::shared_ptr<XmppStream> old;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        signedIn_ = false;
        ++generation_;
        old = std::move(stream_);
    }
    if (old) old->Close();

    Account stored;
    const bool haveStored = LoadAccount(stored);
    const std::string typed = Trim(credentials.username);
    const std::string jid = JidBare(JidNormalize(typed));
    if (!typed.empty() && (jid.empty() || jid.find('@') == std::string::npos))
        return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                         "An XMPP address looks like name@example.org"));
    Account account;
    bool usingStored = false;
    if (!credentials.password.empty()) {
        if (jid.empty())
            return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                             "Enter your XMPP address (name@example.org) with the password"));
        account.jid = jid;
        account.password = credentials.password;
        if (haveStored && stored.jid == jid) account.url = stored.url;
    } else if (haveStored && (jid.empty() || jid == stored.jid)) {
        account = stored;
        usingStored = true;
    } else {
        return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                         "Sign in with your XMPP address (name@example.org) and password"));
    }
    if (!Trim(credentials.server).empty() || account.url.empty()) {
        std::string error;
        const std::string url = FindWebSocket(JidDomain(account.jid), Trim(credentials.server), error);
        if (url.empty()) return fail(UltraIMResult::Error(UltraIMResultCode::NetworkError, error));
        account.url = url;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (account_.jid != account.jid) {
            // Another account: nothing of the last one's stays.
            contacts_.clear();
            chats_.clear();
            byId_.clear();
            aliases_.clear();
            bookmarks_.clear();
            resource_.clear();
        }
        account_.jid = account.jid;
        account_.url = account.url;
    }
    LoadLocalState();
    const UltraIMResult r = Connect(account, true);
    if (!r) {
        // Keys the server no longer takes (the password was changed): forgotten.
        if (usingStored && r.code == UltraIMResultCode::AuthFailed) ForgetAccount();
        return fail(r);
    }
    if (onResult) onResult(r);
}

std::string XmppProvider::FindWebSocket(const std::string& domain, const std::string& server, std::string& error) {
    std::string url;
    if (StartsWith(server, "wss://") || StartsWith(server, "ws://")) {
        url = server;
    } else {
        std::string base = server.empty() ? "https://" + domain
                                          : server.find("://") == std::string::npos ? "https://" + server : server;
        const size_t path = base.find('/', base.find("://") + 3);
        if (path != std::string::npos) base = base.substr(0, path);
        url = Discover(base);
        // Nothing announced: where Prosody (and many others) put it.
        if (url.empty() && server.empty()) url = "wss://" + domain + "/xmpp-websocket";
        if (url.empty()) {
            error = "No XMPP WebSocket address is announced at " + base + " (/.well-known/host-meta)";
            return std::string();
        }
    }
    if (StartsWith(url, "ws://") && !IsLoopback(url)) {
        error = "The XMPP server's address " + url + " has no TLS (ws://): use its wss:// address";
        return std::string();
    }
    if (!StartsWith(url, "ws://") && !StartsWith(url, "wss://")) {
        error = "Not a WebSocket address: " + url;
        return std::string();
    }
    return url;
}

std::string XmppProvider::Discover(const std::string& base) {
    // XEP-0156: the JSON form first, then the XRD.
    for (int round = 0; round < 2; ++round) {
        UltraNetHttpRequest request;
        request.url = base + (round == 0 ? "/.well-known/host-meta.json" : "/.well-known/host-meta");
        request.method = UltraNetHttpMethod::Get;
        request.options.timeoutMs = 15000;
        request.options.followRedirects = true;
        request.options.httpsOnlyRedirects = StartsWith(base, "https://");
        UltraNetResponse raw;
        UltraNet_HttpRequest(request, raw);
        if (!raw.IsComplete() || raw.statusCode != 200) continue;
        std::vector<std::string> found;
        if (round == 0) {
            UltraCanvas::JSONParseResult parsed;
            const JSONValue doc = UltraCanvas::JSON::Parse(raw.GetBodyAsString(), &parsed);
            for (const JSONValue& link : doc["links"].GetElements()) {
                if (link["rel"].GetString() == "urn:xmpp:alt-connections:websocket") found.push_back(link["href"].GetString());
            }
        } else {
            XmlElement xrd;
            if (!XmlParse(raw.GetBodyAsString(), xrd)) continue;
            for (const XmlElement* link : xrd.Children("Link")) {
                if (link->Attr("rel") == "urn:xmpp:alt-connections:websocket") found.push_back(link->Attr("href"));
            }
        }
        // wss:// first; ws:// only when nothing else is there.
        std::stable_sort(found.begin(), found.end(), [](const std::string& a, const std::string& b) {
            return StartsWith(a, "wss://") && !StartsWith(b, "wss://");
        });
        if (!found.empty()) return found.front();
    }
    return std::string();
}

UltraIMResult XmppProvider::Connect(Account& account, bool fresh) {
    auto stream = std::make_shared<XmppStream>();
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        generation = ++generation_;
        stream_ = stream;       // (SignOut reaches it while it signs in)
    }
    const auto abandon = [this, &stream](const UltraIMResult& r) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stream_ == stream) stream_.reset();
        }
        stream->Close();
        return r;
    };
    stream->onElement = [this, generation](const XmlElement& e) { HandleElement(generation, e); };
    stream->onDropped = [this, generation](const std::string& why) {
        Enqueue([this, generation, why]() { Dropped(generation, why); });
    };
    std::string error;
    if (!stream->Connect(account.url, timings_.connectTimeoutMs, timings_.pingIntervalMs, error))
        return abandon(UltraIMResult::Error(UltraIMResultCode::NetworkError,
                                            "Could not reach the XMPP server at " + account.url + ": " + error));
    const std::string domain = JidDomain(account.jid);
    XmlElement open("open", Ns::Framing);
    open.Set("to", domain).Set("version", "1.0").Set("xml:lang", "en");
    const auto isFeatures = [](const XmlElement& e) { return e.Is("features", Ns::Streams); };
    XmlElement features;
    if (!stream->Exchange(open, isFeatures, features, timings_.requestTimeoutMs))
        return abandon(UltraIMResult::Error(UltraIMResultCode::NetworkError,
                                            "The server at " + account.url + " does not speak XMPP over WebSocket"));
    const bool secure = StartsWith(account.url, "wss://") || IsLoopback(account.url);
    const UltraIMResult auth = Authenticate(*stream, features, account, secure);
    if (!auth) return abandon(auth);
    // The stream starts over once signed in (RFC 6120 6.4.6).
    if (!stream->Exchange(open, isFeatures, features, timings_.requestTimeoutMs))
        return abandon(UltraIMResult::Error(UltraIMResultCode::NetworkError, "The server did not restart the stream"));
    std::string resource;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (resource_.empty()) resource_ = "ChatCube-" + RandomId(8);
        resource = resource_;
    }
    XmlElement bind("iq", Ns::Client);
    bind.Set("type", "set");
    bind.Add("bind", Ns::Bind).Add("resource").SetText(resource);
    XmlElement reply;
    stream->Iq(bind, reply, timings_.requestTimeoutMs);
    if (!IqOk(reply))
        return abandon(UltraIMResult::Error(UltraIMResultCode::NetworkError,
                                            "The server did not bind this client: " + IqErrorText(reply)));
    std::string full;
    if (const XmlElement* b = reply.Child("bind", Ns::Bind)) full = b->ChildText("jid");
    if (full.empty()) full = account.jid + "/" + resource;
    // RFC 3921's session, for the servers that still ask for it.
    if (const XmlElement* s = features.Child("session", Ns::Session); s && !s->Child("optional")) {
        XmlElement session("iq", Ns::Client);
        session.Set("type", "set");
        session.Add("session", Ns::Session);
        stream->Iq(session, reply, timings_.requestTimeoutMs);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_) {
            // Signed out (or in again) meanwhile.
            stream->Close();
            return UltraIMResult::Error(UltraIMResultCode::InvalidState, "Signed out while signing in");
        }
        boundJid_ = full;
        resource_ = JidResource(full).empty() ? resource : JidResource(full);
        account_ = account;
        signedIn_ = true;
        backoffMs_ = 0;
    }
    // SCRAM's keys are kept for the next start; after PLAIN nothing is.
    if (account.hasKeys) SaveAccount(account);
    else ForgetAccount();
    SetState(UltraIMConnectionState::Connected);
    AfterBind(stream, fresh);
    return UltraIMResult::Ok();
}

UltraIMResult XmppProvider::Authenticate(XmppStream& stream, const XmlElement& features, Account& account, bool secure) {
    std::set<std::string> offered;
    if (const XmlElement* mechs = features.Child("mechanisms", Ns::Sasl)) {
        for (const XmlElement* m : mechs->Children("mechanism")) offered.insert(Trim(m->text));
    }
    const auto isSasl = [](const XmlElement& e) {
        return e.ns == Ns::Sasl && (e.name == "challenge" || e.name == "success" || e.name == "failure");
    };
    const auto failure = [](const XmlElement& e) {
        std::string why = e.ChildText("text");
        const bool notAuthorized = e.Child("not-authorized") || e.Child("credentials-expired") || e.Child("account-disabled");
        if (why.empty()) why = notAuthorized ? "The address or the password is not right"
                                             : "The server refused the sign-in" + (e.children.empty() ? std::string()
                                                                                    : " (" + e.children.front().name + ")");
        return UltraIMResult::Error(UltraIMResultCode::AuthFailed, why);
    };
    const int t = timings_.requestTimeoutMs;
    for (ScramHash hash : {ScramHash::Sha256, ScramHash::Sha1}) {
        const std::string name = ScramMechanismName(hash);
        if (!offered.count(name)) continue;
        // Kept keys belong to the mechanism they were made for.
        if (account.password.empty() && !(account.hasKeys && account.keys.hash == hash)) continue;
        ScramClient scram(hash, JidLocal(account.jid));
        XmlElement auth("auth", Ns::Sasl);
        auth.Set("mechanism", name).SetText(Base64(scram.ClientFirst()));
        XmlElement reply;
        if (!stream.Exchange(auth, isSasl, reply, t))
            return UltraIMResult::Error(UltraIMResultCode::NetworkError, "The server did not answer the sign-in");
        if (reply.name == "failure") return failure(reply);
        if (reply.name != "challenge")
            return UltraIMResult::Error(UltraIMResultCode::AuthFailed, "The server's SCRAM exchange is not in order");
        const std::vector<uint8_t> first = Unbase64(reply.text);
        std::string finalMessage, error;
        bool outdated = false;
        if (!scram.ClientFinal(std::string(first.begin(), first.end()), account.password,
                               account.hasKeys ? &account.keys : nullptr, finalMessage, error, outdated)) {
            stream.Send(XmlElement("abort", Ns::Sasl));
            return UltraIMResult::Error(UltraIMResultCode::AuthFailed,
                                        outdated ? "The password was changed: sign in with it again" : error);
        }
        XmlElement response("response", Ns::Sasl);
        response.SetText(Base64(finalMessage));
        if (!stream.Exchange(response, isSasl, reply, t))
            return UltraIMResult::Error(UltraIMResultCode::NetworkError, "The server did not answer the sign-in");
        std::string serverFinal;
        if (reply.name == "challenge") {
            // The server's signature as "additional data" before the success.
            const std::vector<uint8_t> extra = Unbase64(reply.text);
            serverFinal.assign(extra.begin(), extra.end());
            if (!stream.Exchange(XmlElement("response", Ns::Sasl), isSasl, reply, t))
                return UltraIMResult::Error(UltraIMResultCode::NetworkError, "The server did not answer the sign-in");
        }
        if (reply.name == "failure") return failure(reply);
        if (reply.name != "success")
            return UltraIMResult::Error(UltraIMResultCode::AuthFailed, "The server's SCRAM exchange is not in order");
        if (serverFinal.empty()) {
            const std::vector<uint8_t> extra = Unbase64(reply.text);
            serverFinal.assign(extra.begin(), extra.end());
        }
        if (!scram.VerifyServerFinal(serverFinal))
            return UltraIMResult::Error(UltraIMResultCode::AuthFailed,
                                        "The server could not prove that it knows the password (SCRAM)");
        account.keys = scram.Keys();
        account.hasKeys = true;
        account.password.clear();       // the keys do from here on
        return UltraIMResult::Ok();
    }
    if (offered.count("PLAIN")) {
        if (account.password.empty())
            return UltraIMResult::Error(UltraIMResultCode::AuthFailed,
                                        "Enter the password: this server takes it every time (PLAIN)");
        if (!secure)
            return UltraIMResult::Error(UltraIMResultCode::AuthFailed,
                                        "The server offers only PLAIN, which would send the password without TLS");
        XmlElement auth("auth", Ns::Sasl);
        auth.Set("mechanism", "PLAIN").SetText(Base64(std::string(1, '\0') + JidLocal(account.jid) + std::string(1, '\0') +
                                                      account.password));
        XmlElement reply;
        if (!stream.Exchange(auth, isSasl, reply, t))
            return UltraIMResult::Error(UltraIMResultCode::NetworkError, "The server did not answer the sign-in");
        if (reply.name != "success") return failure(reply);
        account.hasKeys = false;        // the password stays in memory, for a reconnect
        return UltraIMResult::Ok();
    }
    if (account.password.empty() && account.hasKeys)
        return UltraIMResult::Error(UltraIMResultCode::AuthFailed,
                                    "The server no longer offers " + std::string(ScramMechanismName(account.keys.hash)) +
                                        ": sign in with the password");
    std::string list;
    for (const std::string& m : offered) list += (list.empty() ? "" : ", ") + m;
    return UltraIMResult::Error(UltraIMResultCode::AuthFailed,
                                "The server offers no sign-in this client knows (" + (list.empty() ? "none" : list) + ")");
}

void XmppProvider::AfterBind(std::shared_ptr<XmppStream> stream, bool fresh) {
    (void)stream;
    std::vector<UltraIMEvent> events;
    // The roster (RFC 6121 2.2).
    XmlElement get("iq", Ns::Client);
    get.Set("type", "get");
    get.Add("query", Ns::Roster);
    XmlElement reply;
    if (Iq(get, reply)) {
        if (const XmlElement* query = reply.Child("query", Ns::Roster)) {
            for (const XmlElement& item : query->children) {
                if (item.name == "item") ApplyRosterItem(item, events);
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Gone offline with the connection: presence is told again by the server.
        for (auto& [jid, c] : contacts_) {
            c.resources.clear();
            c.presence = UltraIMPresence::Offline;
        }
        for (auto& [id, chat] : chats_) {
            chat.joined = false;
            chat.occupants.clear();
        }
    }
    for (const UltraIMEvent& e : events) Emit(e);
    // What the account's other clients send and receive (XEP-0280).
    XmlElement carbons("iq", Ns::Client);
    carbons.Set("type", "set");
    carbons.Add("enable", Ns::Carbons);
    Iq(carbons, reply);
    DiscoverServices();
    // Online: the server sends the contacts' presence and what waited offline.
    Send(XmlElement("presence", Ns::Client));
    // The rooms: bookmarked ones, and those this session was in.
    std::vector<std::pair<std::string, std::string>> rooms;
    LoadBookmarks(rooms);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, chat] : chats_) {
            if (!chat.isGroup) continue;
            bool listed = false;
            for (const auto& r : rooms) listed = listed || r.first == id;
            if (!listed) rooms.emplace_back(id, chat.nick);
        }
    }
    for (const auto& [room, nick] : rooms) JoinRoom(room, nick, false);
    (void)fresh;
}

void XmppProvider::Dropped(uint64_t generation, const std::string& why) {
    std::shared_ptr<XmppStream> old;
    int delay = 0;
    std::vector<UltraIMEvent> events;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_ || !signedIn_ || !stream_) return;
        old = std::move(stream_);
        delay = backoffMs_ <= 0 ? timings_.reconnectFirstMs : std::min(backoffMs_ * 2, timings_.reconnectMaxMs);
        backoffMs_ = delay;
        for (auto& [jid, c] : contacts_) {
            if (c.presence == UltraIMPresence::Offline) continue;
            c.resources.clear();
            c.presence = UltraIMPresence::Offline;
            UltraIMEvent e = EventOf(UltraIMEventType::PresenceChange, providerId_, jid);
            e.contactId = jid;
            e.presence = UltraIMPresence::Offline;
            events.push_back(e);
        }
    }
    old->Close();
    SetState(UltraIMConnectionState::Reconnecting, why);
    for (const UltraIMEvent& e : events) Emit(e);
    EnqueueAfter(delay, [this, generation]() { Reconnect(generation); });
}

void XmppProvider::Reconnect(uint64_t generation) {
    Account account;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_ || !signedIn_ || stream_) return;
        account = account_;
    }
    const UltraIMResult r = Connect(account, false);
    if (r) return;
    uint64_t now = 0;
    int delay = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!signedIn_ || r.code == UltraIMResultCode::InvalidState) return;     // signed out meanwhile
        if (r.code == UltraIMResultCode::AuthFailed) {
            signedIn_ = false;
        } else {
            // (Connect took a generation of its own: the next try goes on from it.)
            now = generation_;
            delay = std::min(std::max(backoffMs_, timings_.reconnectFirstMs) * 2, timings_.reconnectMaxMs);
            backoffMs_ = delay;
        }
    }
    if (r.code == UltraIMResultCode::AuthFailed) {
        // The kept keys no longer sign in (the password was changed).
        ForgetAccount();
        SetState(UltraIMConnectionState::Error, r.message);
        Emit(EventOf(UltraIMEventType::AuthRequire, providerId_, std::string()));
        return;
    }
    SetState(UltraIMConnectionState::Reconnecting, r.message);
    EnqueueAfter(delay, [this, now]() { Reconnect(now); });
}

void XmppProvider::SignOut() {
    std::shared_ptr<XmppStream> old;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        signedIn_ = false;
        ++generation_;
        old = std::move(stream_);
        chats_.clear();
        contacts_.clear();
    }
    if (old) {
        // Gone for the others: unavailable, then the stream's end.
        XmlElement away("presence", Ns::Client);
        away.Set("type", "unavailable");
        old->Send(away);
        old->Close();
    }
    if (state_.load() != UltraIMConnectionState::Disconnected) SetState(UltraIMConnectionState::Disconnected);
}

// ============================================================================
// WHAT ARRIVES
// ============================================================================

void XmppProvider::HandleElement(uint64_t generation, const XmlElement& element) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_) return;
    }
    if (element.ns != Ns::Client) return;   // stream-level elements: the waiters'
    if (element.name == "message") HandleMessage(element);
    else if (element.name == "presence") HandlePresence(element);
    else if (element.name == "iq") HandleIq(element);
}

void XmppProvider::HandleIq(const XmlElement& iq) {
    const std::string type = iq.Attr("type");
    if (type != "get" && type != "set") return;     // an answer nobody waited for
    const std::string from = iq.Attr("from");
    std::string me;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        me = account_.jid;
    }
    XmlElement reply("iq", Ns::Client);
    reply.Set("type", "result").Set("id", iq.Attr("id"));
    if (!from.empty()) reply.Set("to", from);
    const XmlElement* query = iq.children.empty() ? nullptr : &iq.children.front();
    if (type == "set" && query && query->Is("query", Ns::Roster) && (from.empty() || JidBare(from) == me)) {
        // A roster push (RFC 6121 2.1.6): from our server alone.
        std::vector<UltraIMEvent> events;
        for (const XmlElement& item : query->children) {
            if (item.name == "item") ApplyRosterItem(item, events);
        }
        Send(reply);
        for (const UltraIMEvent& e : events) Emit(e);
        return;
    }
    if (type == "get" && query && query->Is("query", Ns::DiscoInfo)) {
        XmlElement& info = reply.Add("query", Ns::DiscoInfo);
        info.Add("identity").Set("category", "client").Set("type", "pc").Set("name", "ChatCube");
        for (const char* feature : {Ns::DiscoInfo, Ns::Receipts, Ns::Markers, Ns::ChatStates, Ns::Correct, Ns::Retract,
                                    Ns::Reply, Ns::Oob, Ns::Carbons, Ns::Conference, Ns::Muc, Ns::Ping})
            info.Add("feature").Set("var", feature);
        Send(reply);
        return;
    }
    if (type == "get" && query && query->Is("ping", Ns::Ping)) {
        Send(reply);
        return;
    }
    reply.Set("type", "error");
    XmlElement& error = reply.Add("error");
    error.Set("type", "cancel");
    error.Add("service-unavailable", Ns::Stanzas);
    Send(reply);
}

void XmppProvider::ApplyRosterItem(const XmlElement& item, std::vector<UltraIMEvent>& events) {
    const std::string jid = JidBare(item.Attr("jid"));
    if (jid.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (jid == account_.jid) return;
    const std::string subscription = item.Attr("subscription");
    if (subscription == "remove") {
        contacts_.erase(jid);
        auto chat = chats_.find(jid);
        if (chat != chats_.end() && !chat->second.hasLast) {
            chats_.erase(chat);
            events.push_back(EventOf(UltraIMEventType::ConversationRemove, providerId_, jid));
        }
        return;
    }
    Contact& c = contacts_[jid];
    c.jid = jid;
    c.name = item.Attr("name");
    c.subscription = subscription.empty() ? "none" : subscription;
    c.asked = item.Attr("ask") == "subscribe";
    c.inRoster = true;
    if (hidden_.count(jid)) return;
    bool created = false;
    Chat& chat = ChatFor(jid, false, created);
    UltraIMEvent e = EventOf(UltraIMEventType::ConversationUpdate, providerId_, jid);
    e.conversation = ConversationOf(chat);
    events.push_back(e);
}

void XmppProvider::HandlePresence(const XmlElement& presence) {
    const std::string from = presence.Attr("from");
    const std::string bare = JidBare(from);
    const std::string type = presence.Attr("type");
    std::string me;
    bool isRoom = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        me = account_.jid;
        auto chat = chats_.find(bare);
        isRoom = chat != chats_.end() && chat->second.isGroup;
    }
    if (bare.empty() || bare == me) return;     // our own other clients
    if (isRoom) return HandleRoomPresence(presence, bare);

    if (type == "subscribe") {
        // Kept simple: whoever asks to see our presence may, and is asked back.
        XmlElement yes("presence", Ns::Client);
        yes.Set("to", bare).Set("type", "subscribed");
        Send(yes);
        bool askBack = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto c = contacts_.find(bare);
            askBack = c == contacts_.end() || (!c->second.asked && c->second.subscription != "to" &&
                                               c->second.subscription != "both");
        }
        if (askBack) {
            XmlElement add("iq", Ns::Client);
            add.Set("type", "set").Set("id", "uc" + RandomId(12));
            add.Add("query", Ns::Roster).Add("item").Set("jid", bare);
            Send(add);
            XmlElement ask("presence", Ns::Client);
            ask.Set("to", bare).Set("type", "subscribe");
            Send(ask);
        }
        return;
    }
    if (type == "unsubscribe") {
        XmlElement ok("presence", Ns::Client);
        ok.Set("to", bare).Set("type", "unsubscribed");
        Send(ok);
        return;
    }
    if (!type.empty() && type != "unavailable") return;    // subscribed, unsubscribed, probe, error
    UltraIMEvent changed;
    bool tell = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = contacts_.find(bare);
        if (it == contacts_.end()) return;
        Contact& c = it->second;
        const std::string resource = JidResource(from);
        if (type == "unavailable") c.resources.erase(resource);
        else c.resources[resource] = PresenceOfShow(presence.ChildText("show"));
        UltraIMPresence best = UltraIMPresence::Offline;
        for (const auto& [r, p] : c.resources) {
            if (Rank(p) < Rank(best)) best = p;
        }
        if (best != c.presence) {
            c.presence = best;
            tell = true;
            changed = EventOf(UltraIMEventType::PresenceChange, providerId_, bare);
            changed.contactId = bare;
            changed.presence = best;
        }
    }
    if (tell) Emit(changed);
}

void XmppProvider::HandleRoomPresence(const XmlElement& presence, const std::string& room) {
    const std::string nick = JidResource(presence.Attr("from"));
    const std::string type = presence.Attr("type");
    if (type == "error" || nick.empty()) return;    // (a join's error: JoinRoom waits for it)
    const std::vector<std::string> codes = StatusCodes(presence);
    const XmlElement* x = presence.Child("x", Ns::MucUser);
    const XmlElement* item = x ? x->Child("item") : nullptr;
    UltraIMEvent changed;
    bool tell = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = chats_.find(room);
        if (it == chats_.end()) return;
        Chat& chat = it->second;
        const bool self = HasCode(codes, "110") || nick == chat.nick;
        const bool wasJoined = chat.joined;
        bool membersChanged = false;
        const std::string realJid = item ? JidBare(item->Attr("jid")) : std::string();
        if (type == "unavailable") {
            membersChanged = chat.occupants.erase(nick) > 0;
            if (self) chat.joined = false;
        } else {
            Occupant& o = chat.occupants[nick];
            membersChanged = o.nick.empty();
            o.nick = nick;
            if (!realJid.empty()) o.jid = realJid;
            o.presence = PresenceOfShow(presence.ChildText("show"));
            if (self) chat.joined = true;
        }
        if (item && !realJid.empty()) {
            const std::string affiliation = item->Attr("affiliation");
            if (affiliation == "owner" || affiliation == "admin" || affiliation == "member") {
                membersChanged = membersChanged || chat.affiliations[realJid] != affiliation;
                chat.affiliations[realJid] = affiliation;
            } else if (affiliation == "none" || affiliation == "outcast") {
                membersChanged = chat.affiliations.erase(realJid) > 0 || membersChanged;
            }
        }
        // While a join lists the occupants, one update at its end.
        if (chat.joined && (membersChanged || !wasJoined)) {
            tell = true;
            changed = EventOf(UltraIMEventType::ConversationUpdate, providerId_, room);
            changed.conversation = ConversationOf(chat);
        }
    }
    if (tell) Emit(changed);
}

void XmppProvider::HandleMessage(const XmlElement& message) {
    const std::string type = message.Attr("type").empty() ? "normal" : message.Attr("type");
    const std::string from = message.Attr("from");
    const std::string bare = JidBare(from);
    std::string me;
    bool fromRoom = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        me = account_.jid;
        auto chat = chats_.find(bare);
        fromRoom = chat != chats_.end() && chat->second.isGroup;
    }
    const bool fromServer = from.empty() || bare == me;

    if (type == "error") {
        std::string conversation;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = pendingSends_.find(message.Attr("id"));
            if (it != pendingSends_.end()) conversation = it->second;
        }
        if (!conversation.empty()) Failed(conversation, message.Attr("id"), IqErrorText(message));
        return;
    }
    // An archive query's result (XEP-0313): FetchMessages collects it.
    if (const XmlElement* result = message.Child("result", Ns::Mam)) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mam_.find(result->Attr("queryid"));
        if (it != mam_.end() && (fromServer || fromRoom)) it->second.push_back(*result);
        return;
    }
    // Carbons (XEP-0280): only from our own account.
    const XmlElement* carbon = message.Child("sent", Ns::Carbons);
    if (!carbon) carbon = message.Child("received", Ns::Carbons);
    if (carbon) {
        if (!fromServer) return;
        const XmlElement* forwarded = carbon->Child("forwarded", Ns::Forward);
        const XmlElement* inner = forwarded ? forwarded->Child("message", Ns::Client) : nullptr;
        if (!inner || inner->Attr("type") == "groupchat") return;
        const bool sent = carbon->name == "sent";
        const std::string peer = JidBare(sent ? inner->Attr("to") : inner->Attr("from"));
        if (peer.empty() || peer == me) return;
        std::vector<UltraIMEvent> events;
        std::vector<UltraIMMessage> media;
        std::vector<XmlElement> replies;
        HandleChatMessage(*inner, peer, sent, false, StanzaIdBy(*inner, me), StampOf(*inner, NowMs()), true, events,
                          media, replies);
        for (const UltraIMEvent& e : events) Emit(e);
        for (const XmlElement& r : replies) Send(r);
        for (const UltraIMMessage& m : media) QueueMedia(m);
        return;
    }
    // Bookmarks changed by another of our clients (XEP-0402 notifications).
    if (const XmlElement* event = message.Child("event", Ns::PubSubEvent)) {
        if (!fromServer) return;
        const XmlElement* items = event->Child("items");
        if (!items || items->Attr("node") != Ns::Bookmarks) return;
        for (const XmlElement& it : items->children) {
            const std::string room = JidBare(it.Attr("id"));
            if (room.empty()) continue;
            if (it.name == "item") {
                const XmlElement* conf = it.Child("conference", Ns::Bookmarks);
                const std::string nick = conf ? conf->ChildText("nick") : std::string();
                bool known = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    known = chats_.count(room) > 0;
                    bookmarks_[room] = {conf ? conf->Attr("name") : std::string(), nick};
                }
                if (!known) Enqueue([this, room, nick]() { JoinRoom(room, nick, false); });
            }
        }
        return;
    }
    // An invitation to a room (XEP-0045 mediated, XEP-0249 direct): accepted.
    std::string invitedTo;
    if (const XmlElement* x = message.Child("x", Ns::MucUser); x && x->Child("invite")) invitedTo = bare;
    else if (const XmlElement* direct = message.Child("x", Ns::Conference)) invitedTo = JidBare(direct->Attr("jid"));
    if (!invitedTo.empty()) {
        bool known = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto chat = chats_.find(invitedTo);
            known = chat != chats_.end() && chat->second.joined;
        }
        if (!known) Enqueue([this, invitedTo]() { JoinRoom(invitedTo, std::string(), true); });
        return;
    }
    std::vector<UltraIMEvent> events;
    std::vector<UltraIMMessage> media;
    std::vector<XmlElement> replies;
    if (type == "groupchat") {
        if (!fromRoom || JidResource(from).empty()) return;     // (a room's subject, its own notices)
        bool own = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            own = JidResource(from) == chats_[bare].nick;
        }
        HandleChatMessage(message, bare, own, true, StanzaIdBy(message, bare), StampOf(message, NowMs()), true, events,
                          media, replies);
    } else {
        if (fromServer || fromRoom) return;     // (a room's private messages: not here)
        HandleChatMessage(message, bare, false, false, StanzaIdBy(message, me), StampOf(message, NowMs()), true, events,
                          media, replies);
    }
    for (const UltraIMEvent& e : events) Emit(e);
    for (const XmlElement& r : replies) Send(r);
    for (const UltraIMMessage& m : media) QueueMedia(m);
}

void XmppProvider::Receipt(const std::string& conversationId, const std::string& id, UltraIMDeliveryStatus status,
                           std::vector<UltraIMEvent>& events) {
    // (mutex_ held.) Read covers what was sent before it too.
    const std::string target = Resolve(id);
    auto it = byId_.find(target);
    if (it == byId_.end() || !it->second.message.isOwn || it->second.message.conversationId != conversationId) return;
    std::vector<Known*> marked;
    if (StatusRank(it->second.message.deliveryStatus) < StatusRank(status)) marked.push_back(&it->second);
    if (status == UltraIMDeliveryStatus::Read) {
        const int64_t upTo = it->second.message.timestampMs;
        for (auto& [mid, k] : byId_) {
            if (&k == &it->second || !k.message.isOwn || k.message.conversationId != conversationId) continue;
            if (k.message.timestampMs <= upTo && StatusRank(k.message.deliveryStatus) < StatusRank(status) &&
                StatusRank(k.message.deliveryStatus) >= 0)
                marked.push_back(&k);
        }
    }
    for (Known* k : marked) {
        k->message.deliveryStatus = status;
        UltraIMEvent e = EventOf(UltraIMEventType::MessageDelivered, providerId_, conversationId);
        e.message = k->message;
        e.message.clientMessageId = k->message.messageId;
        e.deliveryStatus = status;
        events.push_back(e);
    }
}

void XmppProvider::HandleChatMessage(const XmlElement& m, const std::string& conversationId, bool isOwn, bool isGroup,
                                     const std::string& archiveId, int64_t timestampMs, bool live,
                                     std::vector<UltraIMEvent>& events, std::vector<UltraIMMessage>& media,
                                     std::vector<XmlElement>& replies) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string senderId = isOwn ? account_.jid : conversationId;
    if (isGroup) {
        const std::string nick = JidResource(m.Attr("from"));
        auto chat = chats_.find(conversationId);
        if (!isOwn && chat != chats_.end()) {
            auto o = chat->second.occupants.find(nick);
            senderId = o != chat->second.occupants.end() && !o->second.jid.empty() ? o->second.jid : conversationId + "/" + nick;
        }
    }
    // Receipts and markers.
    if (const XmlElement* r = m.Child("received", Ns::Receipts); r && !isOwn && !isGroup)
        Receipt(conversationId, r->Attr("id"), UltraIMDeliveryStatus::Delivered, events);
    if (const XmlElement* d = m.Child("displayed", Ns::Markers)) {
        if (isOwn) {
            // Read on another of our clients.
            auto chat = chats_.find(conversationId);
            if (chat != chats_.end() && chat->second.unread > 0) {
                chat->second.unread = 0;
                UltraIMEvent e = EventOf(UltraIMEventType::ConversationUpdate, providerId_, conversationId);
                e.conversation = ConversationOf(chat->second);
                events.push_back(e);
            }
        } else {
            Receipt(conversationId, d->Attr("id"), UltraIMDeliveryStatus::Read, events);
        }
    }
    // Chat states (XEP-0085).
    if (!isOwn && live) {
        for (const char* state : {"composing", "paused", "active", "inactive", "gone"}) {
            if (!m.Child(state, Ns::ChatStates)) continue;
            UltraIMEvent e = EventOf(UltraIMEventType::TypingChange, providerId_, conversationId);
            e.contactId = senderId;
            e.isTyping = std::string(state) == "composing";
            events.push_back(e);
            break;
        }
    }
    // A retraction (XEP-0424): by its sender alone.
    const std::string retracted = RetractTarget(m);
    if (!retracted.empty()) {
        const std::string target = Resolve(retracted);
        auto it = byId_.find(target);
        if (it != byId_.end() && it->second.message.senderId != senderId) return;
        if (it != byId_.end()) byId_.erase(it);
        UltraIMEvent e = EventOf(UltraIMEventType::MessageDelete, providerId_, conversationId);
        e.message.providerId = providerId_;
        e.message.conversationId = conversationId;
        e.message.messageId = target;
        e.message.clientMessageId = target;
        events.push_back(e);
        auto chat = chats_.find(conversationId);
        if (chat != chats_.end() && chat->second.hasLast && chat->second.last.messageId == target) {
            chat->second.hasLast = false;
            for (const auto& [id, k] : byId_) {
                if (k.message.conversationId == conversationId &&
                    (!chat->second.hasLast || k.message.timestampMs >= chat->second.last.timestampMs)) {
                    chat->second.last = k.message;
                    chat->second.hasLast = true;
                }
            }
            UltraIMEvent c = EventOf(UltraIMEventType::ConversationUpdate, providerId_, conversationId);
            c.conversation = ConversationOf(chat->second);
            events.push_back(c);
        }
        return;
    }
    // A correction (XEP-0308): by its sender alone.
    if (const XmlElement* replace = m.Child("replace", Ns::Correct)) {
        if (!m.Child("body")) return;
        const std::string target = Resolve(replace->Attr("id"));
        auto it = byId_.find(target);
        if (it == byId_.end() || it->second.message.senderId != senderId) return;
        Known corrected;
        if (!MessageFrom(m, conversationId, isOwn, isGroup, archiveId, timestampMs, corrected)) return;
        UltraIMMessage& original = it->second.message;
        if (original.attachment.remoteUrl.empty()) original.text = corrected.message.text;
        else original.text = corrected.message.attachment.remoteUrl.empty() ? corrected.message.text : corrected.message.text;
        original.isEdited = true;
        if (!corrected.wireId.empty()) aliases_[corrected.wireId] = target;
        UltraIMEvent e = EventOf(UltraIMEventType::MessageUpdate, providerId_, conversationId);
        e.message = original;
        events.push_back(e);
        auto chat = chats_.find(conversationId);
        if (chat != chats_.end() && chat->second.hasLast && chat->second.last.messageId == target) {
            chat->second.last = original;
            UltraIMEvent c = EventOf(UltraIMEventType::ConversationUpdate, providerId_, conversationId);
            c.conversation = ConversationOf(chat->second);
            events.push_back(c);
        }
        return;
    }
    Known k;
    if (!MessageFrom(m, conversationId, isOwn, isGroup, archiveId, timestampMs, k)) return;
    // Asked for a receipt (XEP-0184): it reached this device.
    if (live && !isOwn && !isGroup && m.Child("request", Ns::Receipts) && !k.wireId.empty()) {
        XmlElement receipt("message", Ns::Client);
        receipt.Set("to", m.Attr("from")).Set("id", "uc" + RandomId(12));
        receipt.Add("received", Ns::Receipts).Set("id", k.wireId);
        replies.push_back(receipt);
    }
    // Seen before (a room's echo of our own, a history repeated, a carbon twice).
    auto seen = byId_.find(k.message.messageId);
    if (seen != byId_.end()) {
        if (!archiveId.empty() && seen->second.archiveId.empty()) {
            seen->second.archiveId = archiveId;
            aliases_[archiveId] = k.message.messageId;
        }
        return;
    }
    Remember(k);
    hidden_.erase(conversationId);
    bool created = false;
    Chat& chat = ChatFor(conversationId, isGroup, created);
    if (!chat.hasLast || chat.last.timestampMs <= k.message.timestampMs) {
        chat.last = k.message;
        chat.hasLast = true;
    }
    chat.lastSeenMs = std::max(chat.lastSeenMs, k.message.timestampMs);
    if (!isOwn) {
        ++chat.unread;
        chat.lastReceivedId = k.message.messageId;
    }
    UltraIMEvent received = EventOf(UltraIMEventType::MessageReceive, providerId_, conversationId);
    received.message = k.message;
    events.push_back(received);
    UltraIMEvent c = EventOf(UltraIMEventType::ConversationUpdate, providerId_, conversationId);
    c.conversation = ConversationOf(chat);
    events.push_back(c);
    if (k.message.type == UltraIMMessageType::Image) media.push_back(k.message);
}

// ============================================================================
// MESSAGES
// ============================================================================

bool XmppProvider::MessageFrom(const XmlElement& m, const std::string& conversationId, bool isOwn, bool isGroup,
                               const std::string& archiveId, int64_t timestampMs, Known& out) const {
    const XmlElement* bodyElement = m.Child("body");
    if (!bodyElement || !RetractTarget(m).empty()) return false;
    std::string body = bodyElement->text;
    out = Known();
    out.wireId = m.Attr("id");
    out.archiveId = archiveId;
    out.from = m.Attr("from");
    std::string originId;
    if (const XmlElement* origin = m.Child("origin-id", Ns::Sid)) originId = origin->Attr("id");
    UltraIMMessage& msg = out.message;
    msg.messageId = !originId.empty() ? originId : !out.wireId.empty() ? out.wireId : !archiveId.empty() ? archiveId : RandomId(16);
    msg.clientMessageId = isOwn ? msg.messageId : std::string();
    msg.providerId = providerId_;
    msg.conversationId = conversationId;
    msg.isOwn = isOwn;
    msg.timestampMs = timestampMs;
    msg.deliveryStatus = UltraIMDeliveryStatus::Sent;
    if (isOwn) {
        msg.senderId = account_.jid;
        msg.senderName = JidLocal(account_.jid);
    } else if (isGroup) {
        const std::string nick = JidResource(out.from);
        msg.senderName = nick;
        msg.senderId = conversationId + "/" + nick;
        auto chat = chats_.find(conversationId);
        if (chat != chats_.end()) {
            auto o = chat->second.occupants.find(nick);
            if (o != chat->second.occupants.end() && !o->second.jid.empty()) msg.senderId = o->second.jid;
        }
        if (const XmlElement* x = m.Child("x", Ns::MucUser)) {
            if (const XmlElement* item = x->Child("item"); item && !item->Attr("jid").empty())
                msg.senderId = JidBare(item->Attr("jid"));
        }
    } else {
        msg.senderId = conversationId;
        msg.senderName = ContactName(conversationId);
    }
    // A reply (XEP-0461), its quote (XEP-0428 fallback) taken off the text.
    if (const XmlElement* reply = m.Child("reply", Ns::Reply)) {
        const std::string rid = reply->Attr("id");
        msg.replyTo.messageId = Resolve(rid);
        auto original = byId_.find(msg.replyTo.messageId);
        if (original != byId_.end()) {
            msg.replyTo.senderId = original->second.message.senderId;
            msg.replyTo.senderName = original->second.message.senderName;
            msg.replyTo.textSnippet = SnippetOf(original->second.message);
        } else {
            msg.replyTo.senderId = isGroup ? reply->Attr("to") : JidBare(reply->Attr("to"));
        }
        for (const XmlElement* fallback : m.Children("fallback", Ns::Fallback)) {
            if (fallback->Attr("for") != Ns::Reply) continue;
            for (const XmlElement* range : fallback->Children("body")) {
                try {
                    const size_t start = std::stoul(range->Attr("start"));
                    const size_t end = std::stoul(range->Attr("end"));
                    if (end > start) body = Utf8Erase(body, start, end);
                } catch (...) {
                }
            }
        }
    }
    // A file by link (XEP-0066 with XEP-0363): the body is the link itself.
    const XmlElement* oob = m.Child("x", Ns::Oob);
    const std::string url = oob ? Trim(oob->ChildText("url")) : std::string();
    if (!url.empty() && url == Trim(body) && (StartsWith(url, "https://") || StartsWith(url, "http://"))) {
        msg.attachment.remoteUrl = url;
        msg.attachment.fileName = UrlFileName(url);
        msg.attachment.mimeType = MimeFor(msg.attachment.fileName);
        msg.type = TypeForMime(msg.attachment.mimeType);
        msg.text = oob->ChildText("desc");
    } else {
        msg.text = body;
    }
    return true;
}

std::string XmppProvider::Resolve(const std::string& id) const {
    // (mutex_ held.)
    if (id.empty() || byId_.count(id)) return id;
    auto alias = aliases_.find(id);
    return alias != aliases_.end() ? alias->second : id;
}

void XmppProvider::Remember(const Known& known) {
    // (mutex_ held.)
    const std::string& id = known.message.messageId;
    auto old = byId_.find(id);
    Known k = known;
    if (old != byId_.end()) {
        if (old->second.message.attachment.isDownloaded) {
            k.message.attachment.localPath = old->second.message.attachment.localPath;
            k.message.attachment.isDownloaded = true;
        }
        if (StatusRank(old->second.message.deliveryStatus) > StatusRank(k.message.deliveryStatus))
            k.message.deliveryStatus = old->second.message.deliveryStatus;
        if (old->second.message.isEdited) {
            k.message.isEdited = true;
            k.message.text = old->second.message.text;
        }
        if (k.archiveId.empty()) k.archiveId = old->second.archiveId;
    }
    if (!k.wireId.empty() && k.wireId != id) aliases_[k.wireId] = id;
    if (!k.archiveId.empty() && k.archiveId != id) aliases_[k.archiveId] = id;
    byId_[id] = k;
}

std::string XmppProvider::ContactName(const std::string& jid) const {
    // (mutex_ held.)
    auto c = contacts_.find(jid);
    if (c != contacts_.end() && !c->second.name.empty()) return c->second.name;
    const std::string local = JidLocal(jid);
    return local.empty() ? jid : local;
}

XmppProvider::Chat& XmppProvider::ChatFor(const std::string& conversationId, bool isGroup, bool& created) {
    // (mutex_ held.)
    auto it = chats_.find(conversationId);
    created = it == chats_.end();
    Chat& chat = chats_[conversationId];
    if (created) {
        chat.id = conversationId;
        chat.isGroup = isGroup;
    }
    return chat;
}

UltraIMConversation XmppProvider::ConversationOf(const Chat& chat) const {
    // (mutex_ held.)
    UltraIMConversation c;
    c.conversationId = chat.id;
    c.providerId = providerId_;
    c.isGroup = chat.isGroup;
    c.isEncrypted = false;      // TLS to the server, not end to end
    if (chat.isGroup) {
        auto bookmark = bookmarks_.find(chat.id);
        c.title = !chat.title.empty() ? chat.title
                  : bookmark != bookmarks_.end() && !bookmark->second.first.empty() ? bookmark->second.first
                                                                                      : JidLocal(chat.id);
        std::set<std::string> members{account_.jid};
        c.memberNames[account_.jid] = chat.nick.empty() ? JidLocal(account_.jid) : chat.nick;
        for (const auto& [nick, o] : chat.occupants) {
            const std::string id = o.jid.empty() ? chat.id + "/" + nick : o.jid;
            members.insert(id);
            if (id != account_.jid) c.memberNames[id] = nick;
        }
        for (const auto& [jid, affiliation] : chat.affiliations) {
            members.insert(jid);
            if (!c.memberNames.count(jid)) c.memberNames[jid] = ContactName(jid);
            if (affiliation == "owner" && c.ownerId.empty()) c.ownerId = jid;
        }
        c.memberIds.assign(members.begin(), members.end());
    } else {
        c.title = ContactName(chat.id);
        c.directContactId = chat.id;
        c.memberIds = {account_.jid, chat.id};
        auto contact = contacts_.find(chat.id);
        c.presence = contact != contacts_.end() ? contact->second.presence : UltraIMPresence::Unknown;
    }
    if (chat.hasLast) {
        c.lastMessageSnippet = SnippetOf(chat.last);
        c.lastMessageTimeMs = chat.last.timestampMs;
    }
    c.unreadCount = chat.unread;
    return c;
}

void XmppProvider::EmitConversation(const std::string& conversationId) {
    UltraIMEvent e = EventOf(UltraIMEventType::ConversationUpdate, providerId_, conversationId);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = chats_.find(conversationId);
        if (it == chats_.end()) return;
        e.conversation = ConversationOf(it->second);
    }
    Emit(e);
}

void XmppProvider::FetchConversations(UltraIMConversationsCallback onResult) {
    Enqueue([this, onResult]() {
        if (state_.load() != UltraIMConnectionState::Connected) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to XMPP"), {});
            return;
        }
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

void XmppProvider::FetchContacts(UltraIMContactsCallback onResult) {
    std::vector<UltraIMContact> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [jid, c] : contacts_) {
            if (!c.inRoster) continue;
            UltraIMContact contact;
            contact.contactId = jid;
            contact.providerId = providerId_;
            contact.displayName = ContactName(jid);
            contact.presence = c.presence;
            if (c.subscription == "none" || c.subscription == "from")
                contact.statusText = c.asked ? "asked to share presence" : "not sharing presence";
            out.push_back(contact);
        }
    }
    if (onResult) onResult(UltraIMResult::Ok(), out);
}

void XmppProvider::FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                                 UltraIMMessagesCallback onResult) {
    Enqueue([this, conversationId, beforeMessageId, limit, onResult]() {
        bool isGroup = false;
        std::string before;
        const std::string queryId = RandomId(12);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto chat = chats_.find(conversationId);
            if (chat == chats_.end()) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation"), {});
                return;
            }
            isGroup = chat->second.isGroup;
            if (!beforeMessageId.empty()) {
                auto known = byId_.find(Resolve(beforeMessageId));
                before = known != byId_.end() && !known->second.archiveId.empty() ? known->second.archiveId : beforeMessageId;
            }
            mam_[queryId] = {};
        }
        // XEP-0313: our archive filtered by the contact, or the room's own.
        XmlElement iq("iq", Ns::Client);
        iq.Set("type", "set");
        if (isGroup) iq.Set("to", conversationId);
        XmlElement& query = iq.Add("query", Ns::Mam);
        query.Set("queryid", queryId);
        std::vector<std::pair<std::string, std::string>> fields;
        if (!isGroup) fields.emplace_back("with", conversationId);
        query.Add(SubmitForm(Ns::Mam, fields));
        XmlElement& set = query.Add("set", Ns::Rsm);
        set.Add("max").SetText(std::to_string(limit <= 0 ? 30 : std::min(limit, 200)));
        set.Add("before").SetText(before);     // empty: the last page
        XmlElement reply;
        const bool ok = Iq(iq, reply);
        std::vector<XmlElement> results;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            results = std::move(mam_[queryId]);
            mam_.erase(queryId);
        }
        if (!ok) {
            if (onResult)
                onResult(UltraIMResult::Error(state_.load() == UltraIMConnectionState::Connected ? UltraIMResultCode::NetworkError
                                                                                                   : UltraIMResultCode::NotConnected,
                                              "The archive did not answer: " + IqErrorText(reply)),
                         {});
            return;
        }
        std::vector<UltraIMMessage> out;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const std::string me = account_.jid;
            std::string nick;
            if (auto chat = chats_.find(conversationId); chat != chats_.end()) nick = chat->second.nick;
            const auto indexOf = [&out](const std::string& id) -> int {
                for (size_t i = 0; i < out.size(); ++i) {
                    if (out[i].messageId == id) return static_cast<int>(i);
                }
                return -1;
            };
            for (const XmlElement& result : results) {
                const XmlElement* forwarded = result.Child("forwarded", Ns::Forward);
                const XmlElement* m = forwarded ? forwarded->Child("message", Ns::Client) : nullptr;
                if (!m) continue;
                const int64_t stamp = StampOf(*forwarded, StampOf(*m, NowMs()));
                bool isOwn = false;
                if (isGroup) {
                    isOwn = JidResource(m->Attr("from")) == nick;
                    if (const XmlElement* x = m->Child("x", Ns::MucUser)) {
                        if (const XmlElement* item = x->Child("item"); item && !item->Attr("jid").empty())
                            isOwn = JidBare(item->Attr("jid")) == me;
                    }
                } else {
                    isOwn = JidBare(m->Attr("from")) == me;
                    if (JidBare(isOwn ? m->Attr("to") : m->Attr("from")) != conversationId) continue;
                }
                // Corrections and retractions in the page change what is in it.
                const std::string retracted = RetractTarget(*m);
                if (!retracted.empty()) {
                    const int at = indexOf(Resolve(retracted));
                    if (at >= 0) out.erase(out.begin() + at);
                    byId_.erase(Resolve(retracted));
                    continue;
                }
                if (const XmlElement* replace = m->Child("replace", Ns::Correct)) {
                    Known corrected;
                    if (!MessageFrom(*m, conversationId, isOwn, isGroup, result.Attr("id"), stamp, corrected)) continue;
                    const std::string target = Resolve(replace->Attr("id"));
                    const int at = indexOf(target);
                    if (at >= 0 && out[at].senderId == corrected.message.senderId) {
                        out[at].text = corrected.message.text;
                        out[at].isEdited = true;
                    }
                    auto known = byId_.find(target);
                    if (known != byId_.end() && known->second.message.senderId == corrected.message.senderId) {
                        known->second.message.text = corrected.message.text;
                        known->second.message.isEdited = true;
                    }
                    continue;
                }
                Known k;
                if (!MessageFrom(*m, conversationId, isOwn, isGroup, result.Attr("id"), stamp, k)) continue;
                Remember(k);
                out.push_back(byId_[k.message.messageId].message);
            }
        }
        if (onResult) onResult(UltraIMResult::Ok(), out);
        for (const UltraIMMessage& m : out) QueueMedia(m);
    });
}

void XmppProvider::SearchMessages(const UltraIMMessageSearch& search, UltraIMSearchCallback onResult) {
    if (search.conversationId.empty()) {
        if (onResult)
            onResult(UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "XMPP searches one conversation at a time"),
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
// FILES
// ============================================================================

std::string XmppProvider::MediaPath(const std::string& url, const std::string& fileName) const {
    std::vector<uint8_t> digest;
    UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, url.data(), url.size(), digest);
    const std::string base = config_.cacheDirectory.empty() ? config_.dataDirectory : config_.cacheDirectory;
    std::error_code ec;
    const std::filesystem::path dir = UltraCanvas::PathFromUtf8(base) / UltraCanvas::PathFromUtf8(providerId_) / "media";
    std::filesystem::create_directories(dir, ec);
    return UltraCanvas::PathToUtf8(dir / UltraCanvas::PathFromUtf8(UltraCrypt_ToHex(digest).substr(0, 24) +
                                                                     LowerExtension(fileName)));
}

bool XmppProvider::Download(const std::string& url, const std::string& toPath, const UltraIMMessage& forMessage) {
    if (url.empty()) return false;
    UltraNetHttpRequest request;
    request.url = url;
    request.method = UltraNetHttpMethod::Get;
    request.options.outputFilePath = toPath;
    request.options.followRedirects = true;
    // A link that came over TLS is not bounced onto a plain connection.
    request.options.httpsOnlyRedirects = StartsWith(url, "https://");
    int lastPercent = -1;
    request.onDownloadProgress = [&](int64_t now, int64_t total) {
        if (total <= 0) return;
        const int percent = static_cast<int>(std::min(now, total) * 100 / total);
        if (percent == lastPercent) return;
        lastPercent = percent;
        UltraIMEvent e = EventOf(UltraIMEventType::TransferProgress, providerId_, forMessage.conversationId);
        e.message = forMessage;
        e.transferredBytes = std::min(now, total);
        e.totalBytes = total;
        e.isUpload = false;
        Emit(e);
    };
    UltraNetResponse raw;
    UltraNet_HttpRequest(request, raw);
    std::error_code ec;
    const bool ok = raw.IsComplete() && raw.statusCode >= 200 && raw.statusCode < 300;
    if (!ok) std::filesystem::remove(UltraCanvas::PathFromUtf8(toPath), ec);
    return ok && std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(toPath), ec);
}

void XmppProvider::QueueMedia(const UltraIMMessage& message) {
    // A picture comes by itself; the rest when asked (DownloadAttachment).
    if (message.type != UltraIMMessageType::Image || message.attachment.remoteUrl.empty() || message.attachment.isDownloaded)
        return;
    Enqueue([this, message]() {
        const std::string path = MediaPath(message.attachment.remoteUrl, message.attachment.fileName);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(path), ec) &&
            !Download(message.attachment.remoteUrl, path, message))
            return;
        UltraIMMessage updated = message;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = byId_.find(message.messageId);
            if (it != byId_.end()) {
                it->second.message.attachment.localPath = path;
                it->second.message.attachment.isDownloaded = true;
                updated = it->second.message;
            } else {
                updated.attachment.localPath = path;
                updated.attachment.isDownloaded = true;
            }
        }
        UltraIMEvent e = EventOf(UltraIMEventType::MessageUpdate, providerId_, updated.conversationId);
        e.message = updated;
        Emit(e);
    });
}

void XmppProvider::DownloadAttachment(const std::string& conversationId, const std::string& messageId,
                                      UltraIMAttachmentCallback onResult) {
    (void)conversationId;
    Enqueue([this, messageId, onResult]() {
        UltraIMMessage m;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = byId_.find(Resolve(messageId));
            if (it == byId_.end() || it->second.message.attachment.remoteUrl.empty()) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No attachment"), UltraIMAttachment());
                return;
            }
            m = it->second.message;
        }
        std::error_code ec;
        if (m.attachment.isDownloaded && !m.attachment.localPath.empty() &&
            std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(m.attachment.localPath), ec)) {
            if (onResult) onResult(UltraIMResult::Ok(), m.attachment);
            return;
        }
        const std::string path = MediaPath(m.attachment.remoteUrl, m.attachment.fileName);
        if (!std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(path), ec) && !Download(m.attachment.remoteUrl, path, m)) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NetworkError, "Could not fetch the file"), UltraIMAttachment());
            return;
        }
        m.attachment.localPath = path;
        m.attachment.isDownloaded = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = byId_.find(m.messageId);
            if (it != byId_.end()) it->second.message.attachment = m.attachment;
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

UltraIMResult XmppProvider::SendText(const std::string& conversationId, const std::string& text,
                                     const UltraIMSendOptions& options) {
    return SendContent(conversationId, text, std::string(), UltraIMMessageType::Text, options);
}

UltraIMResult XmppProvider::SendImage(const std::string& conversationId, const std::string& localPath,
                                      const std::string& caption, const UltraIMSendOptions& options) {
    return SendContent(conversationId, caption, localPath, UltraIMMessageType::Image, options);
}

UltraIMResult XmppProvider::SendVideo(const std::string& conversationId, const std::string& localPath,
                                      const std::string& caption, const UltraIMSendOptions& options) {
    return SendContent(conversationId, caption, localPath, UltraIMMessageType::Video, options);
}

UltraIMResult XmppProvider::SendVoiceNote(const std::string& conversationId, const std::string& localPath,
                                          const UltraIMSendOptions& options) {
    return SendContent(conversationId, std::string(), localPath, UltraIMMessageType::Audio, options);
}

UltraIMResult XmppProvider::SendFile(const std::string& conversationId, const std::string& localPath,
                                     const UltraIMSendOptions& options) {
    return SendContent(conversationId, std::string(), localPath, UltraIMMessageType::File, options);
}

XmlElement XmppProvider::NewMessage(const std::string& conversationId, const std::string& id, bool& isGroup) const {
    // (mutex_ held.)
    auto chat = chats_.find(conversationId);
    isGroup = chat != chats_.end() && chat->second.isGroup;
    XmlElement m("message", Ns::Client);
    m.Set("to", conversationId).Set("type", isGroup ? "groupchat" : "chat").Set("id", id);
    return m;
}

void XmppProvider::Failed(const std::string& conversationId, const std::string& clientId, const std::string& why) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingSends_.erase(clientId);
        auto it = byId_.find(clientId);
        if (it != byId_.end()) it->second.message.deliveryStatus = UltraIMDeliveryStatus::Failed;
    }
    UltraIMEvent e = EventOf(UltraIMEventType::MessageDelivered, providerId_, conversationId);
    e.message.conversationId = conversationId;
    e.message.clientMessageId = clientId;
    e.message.messageId = clientId;
    e.message.providerId = providerId_;
    e.deliveryStatus = UltraIMDeliveryStatus::Failed;
    e.errorText = why;
    Emit(e);
}

void XmppProvider::Delivered(const std::string& conversationId, const UltraIMMessage& sent) {
    UltraIMConversation conversation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Known k;
        k.message = sent;
        k.wireId = sent.messageId;
        Remember(k);
        // (Kept until the session ends: an error answer names it.)
        pendingSends_[sent.messageId] = conversationId;
        if (pendingSends_.size() > 500) pendingSends_.erase(pendingSends_.begin());
        auto chat = chats_.find(conversationId);
        if (chat != chats_.end()) {
            chat->second.last = sent;
            chat->second.hasLast = true;
            chat->second.lastSeenMs = std::max(chat->second.lastSeenMs, sent.timestampMs);
            conversation = ConversationOf(chat->second);
        }
    }
    UltraIMEvent delivered = EventOf(UltraIMEventType::MessageDelivered, providerId_, conversationId);
    delivered.message = sent;
    delivered.deliveryStatus = UltraIMDeliveryStatus::Sent;
    Emit(delivered);
    if (!sent.attachment.remoteUrl.empty()) {
        // The row learns the file's link.
        UltraIMEvent updated = EventOf(UltraIMEventType::MessageUpdate, providerId_, conversationId);
        updated.message = sent;
        Emit(updated);
    }
    if (!conversation.conversationId.empty()) {
        UltraIMEvent changed = EventOf(UltraIMEventType::ConversationUpdate, providerId_, conversationId);
        changed.conversation = conversation;
        Emit(changed);
    }
}

UltraIMResult XmppProvider::SendContent(const std::string& conversationId, const std::string& text,
                                        const std::string& localPath, UltraIMMessageType type,
                                        const UltraIMSendOptions& options) {
    if (state_.load() != UltraIMConnectionState::Connected)
        return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to XMPP");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!chats_.count(conversationId)) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation");
        if (!localPath.empty() && uploadService_.empty())
            return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing,
                                        "This XMPP server has no file upload service (XEP-0363)");
    }
    if (!localPath.empty()) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(localPath), ec))
            return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "There is no file " + localPath);
    } else if (Trim(text).empty()) {
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Nothing to send");
    }
    const std::string clientId = options.clientMessageId.empty() ? UltraIM_NewClientMessageId() : options.clientMessageId;
    Enqueue([this, conversationId, text, localPath, type, options, clientId]() {
        UltraIMMessage sent;
        sent.messageId = clientId;
        sent.clientMessageId = clientId;
        sent.providerId = providerId_;
        sent.conversationId = conversationId;
        sent.isOwn = true;
        sent.text = text;
        sent.type = localPath.empty() ? UltraIMMessageType::Text : type;
        sent.timestampMs = NowMs();
        sent.deliveryStatus = UltraIMDeliveryStatus::Sent;
        std::string getUrl;
        if (!localPath.empty()) {
            // XEP-0363: a slot from the upload service, the file PUT there.
            std::error_code ec;
            const int64_t size = static_cast<int64_t>(std::filesystem::file_size(UltraCanvas::PathFromUtf8(localPath), ec));
            const std::string fileName = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(localPath).filename());
            const std::string mime = MimeFor(localPath);
            std::string service;
            int64_t maxBytes = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                service = uploadService_;
                maxBytes = uploadMaxBytes_;
            }
            if (maxBytes > 0 && size > maxBytes)
                return Failed(conversationId, clientId,
                              "The file is larger than the server takes (" + std::to_string(maxBytes / (1024 * 1024)) + " MB)");
            XmlElement ask("iq", Ns::Client);
            ask.Set("type", "get").Set("to", service);
            ask.Add("request", Ns::Upload).Set("filename", fileName).Set("size", std::to_string(size)).Set("content-type", mime);
            XmlElement reply;
            if (!Iq(ask, reply)) return Failed(conversationId, clientId, "No upload slot: " + IqErrorText(reply));
            const XmlElement* slot = reply.Child("slot", Ns::Upload);
            const XmlElement* put = slot ? slot->Child("put") : nullptr;
            const XmlElement* get = slot ? slot->Child("get") : nullptr;
            if (!put || !get || put->Attr("url").empty() || get->Attr("url").empty())
                return Failed(conversationId, clientId, "The upload service's answer has no slot");
            getUrl = get->Attr("url");
            UltraNetHttpRequest request;
            request.url = put->Attr("url");
            request.method = UltraNetHttpMethod::Put;
            request.bodyFilePath = localPath;
            request.headers.Set("Content-Type", mime);
            for (const XmlElement* header : put->Children("header")) {
                // XEP-0363 5: only these may come from the server.
                const std::string name = Lower(header->Attr("name"));
                if (name == "authorization" || name == "cookie" || name == "expires")
                    request.headers.Set(header->Attr("name"), Trim(header->text));
            }
            request.options.timeoutMs = -1;
            request.options.stallTimeoutMs = 60000;
            request.options.httpsOnlyRedirects = StartsWith(request.url, "https://");
            int lastPercent = -1;
            request.onUploadProgress = [&](int64_t now, int64_t total) {
                if (total <= 0) return;
                const int percent = static_cast<int>(std::min(now, total) * 100 / total);
                if (percent == lastPercent) return;
                lastPercent = percent;
                UltraIMEvent e = EventOf(UltraIMEventType::TransferProgress, providerId_, conversationId);
                e.message.conversationId = conversationId;
                e.message.clientMessageId = clientId;
                e.message.messageId = clientId;
                e.message.providerId = providerId_;
                e.transferredBytes = std::min(now, total);
                e.totalBytes = total;
                e.isUpload = true;
                Emit(e);
            };
            UltraNetResponse raw;
            UltraNet_HttpRequest(request, raw);
            if (!raw.IsComplete() || raw.statusCode < 200 || raw.statusCode >= 300)
                return Failed(conversationId, clientId,
                              "The upload failed: " + (!raw.transferError.empty() ? raw.transferError
                                                                                   : "HTTP " + std::to_string(raw.statusCode)));
            sent.attachment.localPath = localPath;
            sent.attachment.remoteUrl = getUrl;
            sent.attachment.fileName = UrlFileName(getUrl);
            sent.attachment.mimeType = mime;
            sent.attachment.fileSizeBytes = size;
            sent.attachment.isDownloaded = true;
            sent.attachment.durationSeconds = options.durationSeconds;
            sent.type = TypeForMime(mime) == UltraIMMessageType::File ? type : TypeForMime(mime);
        }
        bool isGroup = false;
        XmlElement m;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            m = NewMessage(conversationId, clientId, isGroup);
            sent.senderId = account_.jid;
            sent.senderName = isGroup && chats_.count(conversationId) ? chats_[conversationId].nick : JidLocal(account_.jid);
            m.Add("body").SetText(getUrl.empty() ? text : getUrl);
            if (!getUrl.empty()) {
                XmlElement& oob = m.Add("x", Ns::Oob);
                oob.Add("url").SetText(getUrl);
                if (!text.empty()) oob.Add("desc").SetText(text);
            }
            if (!options.replyToMessageId.empty()) {
                // XEP-0461: the id the other clients know the message by -
                // a room's archive id, a chat's id attribute.
                const std::string target = Resolve(options.replyToMessageId);
                auto original = byId_.find(target);
                if (original != byId_.end()) {
                    const Known& k = original->second;
                    const std::string to = isGroup ? (k.from.empty() ? conversationId + "/" + sent.senderName : k.from)
                                                   : k.message.senderId;
                    const std::string id = isGroup && !k.archiveId.empty() ? k.archiveId
                                           : !k.wireId.empty()              ? k.wireId
                                                                            : k.message.messageId;
                    m.Add("reply", Ns::Reply).Set("to", to).Set("id", id);
                    sent.replyTo.messageId = target;
                    sent.replyTo.senderId = k.message.senderId;
                    sent.replyTo.senderName = k.message.senderName;
                    sent.replyTo.textSnippet = SnippetOf(k.message);
                } else {
                    m.Add("reply", Ns::Reply).Set("to", conversationId).Set("id", options.replyToMessageId);
                    sent.replyTo.messageId = options.replyToMessageId;
                }
            }
            m.Add("origin-id", Ns::Sid).Set("id", clientId);
            if (!isGroup) m.Add("request", Ns::Receipts);
            m.Add("markable", Ns::Markers);
            m.Add("active", Ns::ChatStates);
        }
        if (!Send(m)) return Failed(conversationId, clientId, "Not connected");
        Delivered(conversationId, sent);
    });
    return UltraIMResult::Ok();
}

// ============================================================================
// EDITING, DELETING, READING, TYPING
// ============================================================================

UltraIMResult XmppProvider::EditMessage(const std::string& conversationId, const std::string& messageId,
                                        const std::string& newText) {
    if (state_.load() != UltraIMConnectionState::Connected)
        return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to XMPP");
    if (Trim(newText).empty())
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "A text cannot be empty - delete it instead");
    XmlElement m;
    UltraIMMessage updated;
    UltraIMConversation conversation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = byId_.find(Resolve(messageId));
        if (it == byId_.end() || it->second.message.conversationId != conversationId)
            return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such message");
        if (!it->second.message.isOwn)
            return UltraIMResult::Error(UltraIMResultCode::PermissionDenied, "Only its sender can change a message");
        if (!it->second.message.attachment.remoteUrl.empty())
            return UltraIMResult::Error(UltraIMResultCode::PermissionDenied, "A file's message cannot be changed");
        bool isGroup = false;
        const std::string id = UltraIM_NewClientMessageId();
        m = NewMessage(conversationId, id, isGroup);
        m.Add("body").SetText(newText);
        // XEP-0308: the id of the message as first sent.
        m.Add("replace", Ns::Correct).Set("id", it->second.wireId.empty() ? it->second.message.messageId : it->second.wireId);
        m.Add("origin-id", Ns::Sid).Set("id", id);
        m.Add("store", Ns::Hints);
        it->second.message.text = newText;
        it->second.message.isEdited = true;
        aliases_[id] = it->second.message.messageId;
        updated = it->second.message;
        auto chat = chats_.find(conversationId);
        if (chat != chats_.end() && chat->second.hasLast && chat->second.last.messageId == updated.messageId) {
            chat->second.last = updated;
            conversation = ConversationOf(chat->second);
        }
    }
    if (!Send(m)) return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not connected");
    Enqueue([this, updated, conversation, conversationId]() {
        UltraIMEvent e = EventOf(UltraIMEventType::MessageUpdate, providerId_, conversationId);
        e.message = updated;
        Emit(e);
        if (!conversation.conversationId.empty()) {
            UltraIMEvent c = EventOf(UltraIMEventType::ConversationUpdate, providerId_, conversationId);
            c.conversation = conversation;
            Emit(c);
        }
    });
    return UltraIMResult::Ok();
}

UltraIMDeleteChoices XmppProvider::GetDeleteChoices(const std::string& conversationId, const std::string& messageId) const {
    UltraIMDeleteChoices choices;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byId_.find(Resolve(messageId));
    choices.forEveryone = it != byId_.end() && it->second.message.isOwn && it->second.message.conversationId == conversationId;
    return choices;
}

UltraIMResult XmppProvider::DeleteMessage(const std::string& conversationId, const std::string& messageId,
                                          UltraIMDeleteScope scope) {
    if (scope == UltraIMDeleteScope::ForMe)
        return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "XMPP deletes (retracts) for everyone only");
    if (state_.load() != UltraIMConnectionState::Connected)
        return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to XMPP");
    XmlElement m;
    std::string target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        target = Resolve(messageId);
        auto it = byId_.find(target);
        if (it == byId_.end() || it->second.message.conversationId != conversationId)
            return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such message");
        if (!it->second.message.isOwn)
            return UltraIMResult::Error(UltraIMResultCode::PermissionDenied, "Only its sender can delete a message");
        bool isGroup = false;
        m = NewMessage(conversationId, UltraIM_NewClientMessageId(), isGroup);
        // XEP-0424: a chat names the origin-id, a room its archive's id.
        const std::string id = isGroup && !it->second.archiveId.empty() ? it->second.archiveId : target;
        m.Add("retract", Ns::Retract).Set("id", id);
        m.Add("fallback", Ns::Fallback).Set("for", Ns::Retract);
        m.Add("body").SetText("This person attempted to retract a previous message, but it's unsupported by your client.");
        m.Add("store", Ns::Hints);
    }
    if (!Send(m)) return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not connected");
    Enqueue([this, conversationId, target]() {
        std::vector<UltraIMEvent> events;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            byId_.erase(target);
            UltraIMEvent e = EventOf(UltraIMEventType::MessageDelete, providerId_, conversationId);
            e.message.providerId = providerId_;
            e.message.conversationId = conversationId;
            e.message.messageId = target;
            e.message.clientMessageId = target;
            events.push_back(e);
            auto chat = chats_.find(conversationId);
            if (chat != chats_.end() && chat->second.hasLast && chat->second.last.messageId == target) {
                chat->second.hasLast = false;
                for (const auto& [id, k] : byId_) {
                    if (k.message.conversationId == conversationId &&
                        (!chat->second.hasLast || k.message.timestampMs >= chat->second.last.timestampMs)) {
                        chat->second.last = k.message;
                        chat->second.hasLast = true;
                    }
                }
                UltraIMEvent c = EventOf(UltraIMEventType::ConversationUpdate, providerId_, conversationId);
                c.conversation = ConversationOf(chat->second);
                events.push_back(c);
            }
        }
        for (const UltraIMEvent& e : events) Emit(e);
    });
    return UltraIMResult::Ok();
}

UltraIMResult XmppProvider::MarkRead(const std::string& conversationId, const std::string& upToMessageId) {
    XmlElement m;
    bool send = false;
    UltraIMConversation conversation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto chat = chats_.find(conversationId);
        if (chat == chats_.end()) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation");
        auto it = byId_.find(upToMessageId.empty() ? chat->second.lastReceivedId : Resolve(upToMessageId));
        if (it != byId_.end() && !it->second.message.isOwn) {
            bool isGroup = false;
            m = NewMessage(conversationId, UltraIM_NewClientMessageId(), isGroup);
            // XEP-0333: a chat's message by its id attribute, a room's by its archive id.
            const Known& k = it->second;
            const std::string id = isGroup && !k.archiveId.empty() ? k.archiveId : !k.wireId.empty() ? k.wireId
                                                                                                     : k.message.messageId;
            m.Add("displayed", Ns::Markers).Set("id", id);
            m.Add("store", Ns::Hints);
            send = state_.load() == UltraIMConnectionState::Connected;
        }
        if (chat->second.unread > 0) {
            chat->second.unread = 0;
            conversation = ConversationOf(chat->second);
        }
    }
    if (send) Send(m);
    if (!conversation.conversationId.empty()) {
        Enqueue([this, conversation]() {
            UltraIMEvent e = EventOf(UltraIMEventType::ConversationUpdate, providerId_, conversation.conversationId);
            e.conversation = conversation;
            Emit(e);
        });
    }
    return UltraIMResult::Ok();
}

UltraIMResult XmppProvider::SetTyping(const std::string& conversationId, bool isTyping) {
    if (state_.load() != UltraIMConnectionState::Connected)
        return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to XMPP");
    XmlElement m;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!chats_.count(conversationId)) return UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation");
        bool isGroup = false;
        m = NewMessage(conversationId, UltraIM_NewClientMessageId(), isGroup);
        m.Add(isTyping ? "composing" : "active", Ns::ChatStates);
        m.Add("no-store", Ns::Hints);
    }
    Send(m);
    return UltraIMResult::Ok();
}

// ============================================================================
// CONTACTS, ROOMS, BOOKMARKS
// ============================================================================

void XmppProvider::DiscoverServices() {
    std::string domain;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        domain = JidDomain(account_.jid);
        uploadService_.clear();
        uploadMaxBytes_ = 0;
        mucService_.clear();
    }
    std::vector<std::string> candidates{domain};
    XmlElement items("iq", Ns::Client);
    items.Set("type", "get").Set("to", domain);
    items.Add("query", Ns::DiscoItems);
    XmlElement reply;
    if (Iq(items, reply)) {
        if (const XmlElement* q = reply.Child("query", Ns::DiscoItems)) {
            for (const XmlElement* item : q->Children("item")) {
                if (!item->Attr("jid").empty() && item->Attr("node").empty()) candidates.push_back(item->Attr("jid"));
            }
        }
    }
    for (const std::string& jid : candidates) {
        XmlElement info("iq", Ns::Client);
        info.Set("type", "get").Set("to", jid);
        info.Add("query", Ns::DiscoInfo);
        if (!Iq(info, reply)) continue;
        const XmlElement* q = reply.Child("query", Ns::DiscoInfo);
        if (!q) continue;
        bool upload = false, muc = false;
        for (const XmlElement* f : q->Children("feature")) upload = upload || f->Attr("var") == Ns::Upload;
        for (const XmlElement* id : q->Children("identity")) {
            muc = muc || (id->Attr("category") == "conference" && id->Attr("type") == "text");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (upload && uploadService_.empty()) {
            uploadService_ = jid;
            for (const XmlElement* x : q->Children("x", Ns::Data)) {
                for (const XmlElement* f : x->Children("field")) {
                    if (f->Attr("var") != "max-file-size") continue;
                    try {
                        uploadMaxBytes_ = std::stoll(f->ChildText("value"));
                    } catch (...) {
                    }
                }
            }
        }
        if (muc && mucService_.empty() && jid != domain) mucService_ = jid;
    }
}

bool XmppProvider::IsConference(const std::string& jid, std::string& name) {
    XmlElement info("iq", Ns::Client);
    info.Set("type", "get").Set("to", jid);
    info.Add("query", Ns::DiscoInfo);
    XmlElement reply;
    if (!Iq(info, reply)) return false;
    const XmlElement* q = reply.Child("query", Ns::DiscoInfo);
    if (!q) return false;
    for (const XmlElement* id : q->Children("identity")) {
        if (id->Attr("category") == "conference") {
            name = id->Attr("name");
            return true;
        }
    }
    return false;
}

UltraIMResult XmppProvider::JoinRoom(const std::string& room, std::string nick, bool bookmark) {
    int64_t since = 0;
    bool created = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (nick.empty()) {
            auto b = bookmarks_.find(room);
            if (b != bookmarks_.end()) nick = b->second.second;
        }
        if (nick.empty()) {
            auto chat = chats_.find(room);
            if (chat != chats_.end()) nick = chat->second.nick;
        }
        if (nick.empty()) nick = JidLocal(account_.jid);
        Chat& chat = ChatFor(room, true, created);
        chat.isGroup = true;
        chat.nick = nick;
        since = chat.lastSeenMs;
    }
    auto stream = Stream();
    if (!stream) return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not signed in to XMPP");
    std::string why;
    bool joined = false;
    for (int attempt = 0; attempt < 3 && !joined; ++attempt) {
        XmlElement presence("presence", Ns::Client);
        presence.Set("to", room + "/" + nick);
        XmlElement& x = presence.Add("x", Ns::Muc);
        // What was said while away: since the last message seen here (the
        // archive has the rest), none on a first join.
        XmlElement& history = x.Add("history");
        if (since > 0) history.Set("since", FormatXmppTime(since + 1));
        else history.Set("maxstanzas", "0");
        const std::string want = nick;
        XmlElement reply;
        const bool answered = stream->Exchange(
            presence,
            [room, want](const XmlElement& e) {
                if (!e.Is("presence", Ns::Client) || JidBare(e.Attr("from")) != room) return false;
                if (e.Attr("type") == "error") return true;
                return JidResource(e.Attr("from")) == want && e.Attr("type") != "unavailable";
            },
            reply, timings_.requestTimeoutMs);
        if (!answered) {
            why = "The room did not answer";
            break;
        }
        if (reply.Attr("type") != "error") {
            joined = true;
            break;
        }
        const XmlElement* error = reply.Child("error");
        if (error && error->Child("conflict")) {
            nick = want + "-" + RandomId(3);      // the nickname is someone else's there
            std::lock_guard<std::mutex> lock(mutex_);
            chats_[room].nick = nick;
            continue;
        }
        why = "The room refused to let you in: " + IqErrorText(reply);
        break;
    }
    if (!joined) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (created) chats_.erase(room);
        return UltraIMResult::Error(UltraIMResultCode::PermissionDenied, why);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        chats_[room].joined = true;
        hidden_.erase(room);
    }
    LoadRoomDetails(room);
    if (bookmark) {
        std::string title;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            title = chats_[room].title;
        }
        Bookmark(room, title, nick, true);
    }
    EmitConversation(room);
    return UltraIMResult::Ok();
}

void XmppProvider::LoadRoomDetails(const std::string& room) {
    std::string name;
    if (IsConference(room, name) && !name.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto chat = chats_.find(room);
        if (chat != chats_.end()) chat->second.title = name;
    }
    // Who belongs to it, online or not (a members-only room's members; a
    // room may keep the list to its owners - then the occupants alone).
    std::map<std::string, std::string> affiliations;
    for (const char* affiliation : {"owner", "admin", "member"}) {
        XmlElement iq("iq", Ns::Client);
        iq.Set("type", "get").Set("to", room);
        iq.Add("query", Ns::MucAdmin).Add("item").Set("affiliation", affiliation);
        XmlElement reply;
        if (!Iq(iq, reply)) continue;
        if (const XmlElement* q = reply.Child("query", Ns::MucAdmin)) {
            for (const XmlElement* item : q->Children("item")) {
                if (!item->Attr("jid").empty()) affiliations[JidBare(item->Attr("jid"))] = affiliation;
            }
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto chat = chats_.find(room);
    if (chat == chats_.end()) return;
    for (const auto& [jid, affiliation] : affiliations) chat->second.affiliations[jid] = affiliation;
}

void XmppProvider::LoadBookmarks(std::vector<std::pair<std::string, std::string>>& rooms) {
    // XEP-0402 in PEP; XEP-0048 in private storage on servers without it.
    XmlElement get("iq", Ns::Client);
    get.Set("type", "get");
    get.Add("pubsub", Ns::PubSub).Add("items").Set("node", Ns::Bookmarks);
    XmlElement reply;
    const bool pep = Iq(get, reply);
    // A node never published answers item-not-found: PEP is there all the same.
    const bool pepWorks = pep || StartsWith(IqErrorText(reply), "item-not-found");
    std::map<std::string, std::pair<std::string, std::string>> found;
    std::set<std::string> autojoin;
    if (pep) {
        const XmlElement* pubsub = reply.Child("pubsub", Ns::PubSub);
        const XmlElement* items = pubsub ? pubsub->Child("items") : nullptr;
        if (items) {
            for (const XmlElement* item : items->Children("item")) {
                const XmlElement* conf = item->Child("conference", Ns::Bookmarks);
                const std::string room = JidBare(item->Attr("id"));
                if (!conf || room.empty()) continue;
                found[room] = {conf->Attr("name"), conf->ChildText("nick")};
                const std::string join = conf->Attr("autojoin");
                if (join == "true" || join == "1") autojoin.insert(room);
            }
        }
    }
    if (found.empty()) {
        XmlElement old("iq", Ns::Client);
        old.Set("type", "get");
        old.Add("query", Ns::Private).Add("storage", Ns::BookmarksLegacy);
        if (Iq(old, reply)) {
            const XmlElement* q = reply.Child("query", Ns::Private);
            const XmlElement* storage = q ? q->Child("storage", Ns::BookmarksLegacy) : nullptr;
            if (storage) {
                for (const XmlElement* conf : storage->Children("conference")) {
                    const std::string room = JidBare(conf->Attr("jid"));
                    if (room.empty()) continue;
                    found[room] = {conf->Attr("name"), conf->ChildText("nick")};
                    const std::string join = conf->Attr("autojoin");
                    if (join == "true" || join == "1") autojoin.insert(room);
                }
            }
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    bookmarksLegacy_ = !pepWorks;
    bookmarks_ = found;
    for (const auto& [room, entry] : found) {
        if (autojoin.count(room)) rooms.emplace_back(room, entry.second);
    }
}

void XmppProvider::Bookmark(const std::string& room, const std::string& name, const std::string& nick, bool add) {
    bool legacy = false;
    std::map<std::string, std::pair<std::string, std::string>> all;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (add) bookmarks_[room] = {name, nick};
        else bookmarks_.erase(room);
        legacy = bookmarksLegacy_;
        all = bookmarks_;
    }
    XmlElement reply;
    if (legacy) {
        XmlElement set("iq", Ns::Client);
        set.Set("type", "set");
        XmlElement& storage = set.Add("query", Ns::Private).Add("storage", Ns::BookmarksLegacy);
        for (const auto& [jid, entry] : all) {
            XmlElement& conf = storage.Add("conference");
            conf.Set("jid", jid).Set("autojoin", "true");
            if (!entry.first.empty()) conf.Set("name", entry.first);
            if (!entry.second.empty()) conf.Add("nick").SetText(entry.second);
        }
        Iq(set, reply);
        return;
    }
    XmlElement set("iq", Ns::Client);
    set.Set("type", "set");
    XmlElement& pubsub = set.Add("pubsub", Ns::PubSub);
    if (add) {
        XmlElement& publish = pubsub.Add("publish");
        publish.Set("node", Ns::Bookmarks);
        XmlElement& conf = publish.Add("item").Set("id", room).Add("conference", Ns::Bookmarks);
        conf.Set("autojoin", "true");
        if (!name.empty()) conf.Set("name", name);
        if (!nick.empty()) conf.Add("nick").SetText(nick);
        // Kept, and for this account alone (XEP-0402 3).
        XmlElement& options = pubsub.Add("publish-options");
        options.Add(SubmitForm("http://jabber.org/protocol/pubsub#publish-options",
                               {{"pubsub#persist_items", "true"},
                                {"pubsub#max_items", "max"},
                                {"pubsub#send_last_published_item", "never"},
                                {"pubsub#access_model", "whitelist"}}));
    } else {
        XmlElement& retract = pubsub.Add("retract");
        retract.Set("node", Ns::Bookmarks).Set("notify", "true");
        retract.Add("item").Set("id", room);
    }
    Iq(set, reply);
}

void XmppProvider::AddContact(const std::string& address, UltraIMConversationCallback onResult) {
    Enqueue([this, address, onResult]() {
        const auto fail = [&onResult](UltraIMResultCode code, const std::string& why) {
            if (onResult) onResult(UltraIMResult::Error(code, why), {});
        };
        const std::string jid = JidBare(JidNormalize(address));
        if (jid.empty() || jid.find('@') == std::string::npos)
            return fail(UltraIMResultCode::InvalidArgument, "An XMPP address looks like name@example.org");
        if (state_.load() != UltraIMConnectionState::Connected)
            return fail(UltraIMResultCode::NotConnected, "Not signed in to XMPP");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (jid == account_.jid) return fail(UltraIMResultCode::InvalidArgument, "That is this account's own address");
            auto chat = chats_.find(jid);
            auto contact = contacts_.find(jid);
            if (chat != chats_.end() && (chat->second.isGroup ? chat->second.joined
                                                               : contact != contacts_.end() && contact->second.inRoster)) {
                hidden_.erase(jid);
                const UltraIMConversation c = ConversationOf(chat->second);
                if (onResult) onResult(UltraIMResult::Ok(), c);
                return;
            }
        }
        std::string name;
        if (IsConference(jid, name)) {
            const UltraIMResult r = JoinRoom(jid, std::string(), true);
            if (!r) return fail(r.code, r.message);
        } else {
            // RFC 6121: into the roster, and asked to share presence.
            XmlElement set("iq", Ns::Client);
            set.Set("type", "set");
            set.Add("query", Ns::Roster).Add("item").Set("jid", jid);
            XmlElement reply;
            if (!Iq(set, reply)) return fail(UltraIMResultCode::NetworkError, "The roster was not changed: " + IqErrorText(reply));
            XmlElement subscribe("presence", Ns::Client);
            subscribe.Set("to", jid).Set("type", "subscribe");
            Send(subscribe);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                Contact& c = contacts_[jid];
                c.jid = jid;
                c.inRoster = true;
                c.asked = true;
                hidden_.erase(jid);
                bool created = false;
                ChatFor(jid, false, created);
            }
            SaveLocalState();
            EmitConversation(jid);
        }
        UltraIMConversation c;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto chat = chats_.find(jid);
            if (chat != chats_.end()) c = ConversationOf(chat->second);
        }
        if (onResult) onResult(UltraIMResult::Ok(), c);
    });
}

void XmppProvider::CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                               UltraIMConversationCallback onResult) {
    std::vector<std::string> members;
    for (const std::string& id : memberIds) {
        const std::string jid = JidBare(JidNormalize(id));
        if (jid.empty() || jid.find('@') == std::string::npos) {
            if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Not an XMPP address: " + id), {});
            return;
        }
        if (std::find(members.begin(), members.end(), jid) == members.end()) members.push_back(jid);
    }
    Enqueue([this, title, members, onResult]() {
        const auto fail = [&onResult](UltraIMResultCode code, const std::string& why) {
            if (onResult) onResult(UltraIMResult::Error(code, why), {});
        };
        if (state_.load() != UltraIMConnectionState::Connected)
            return fail(UltraIMResultCode::NotConnected, "Not signed in to XMPP");
        std::string service, me;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            service = mucService_;
            me = account_.jid;
        }
        if (service.empty())
            return fail(UltraIMResultCode::CapabilityMissing, "This XMPP server has no group chat service (XEP-0045)");
        const std::string room = RandomId(12) + "@" + service;
        const std::string nick = JidLocal(me);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bool created = false;
            Chat& chat = ChatFor(room, true, created);
            chat.nick = nick;
            chat.title = title;
        }
        auto stream = Stream();
        XmlElement presence("presence", Ns::Client);
        presence.Set("to", room + "/" + nick);
        presence.Add("x", Ns::Muc);
        XmlElement reply;
        const bool answered = stream && stream->Exchange(
            presence,
            [room, nick](const XmlElement& e) {
                return e.Is("presence", Ns::Client) && JidBare(e.Attr("from")) == room &&
                       (e.Attr("type") == "error" || JidResource(e.Attr("from")) == nick);
            },
            reply, timings_.requestTimeoutMs);
        const auto undo = [this, &room]() {
            std::lock_guard<std::mutex> lock(mutex_);
            chats_.erase(room);
        };
        if (!answered || reply.Attr("type") == "error") {
            undo();
            return fail(UltraIMResultCode::NetworkError, "The room could not be made: " + IqErrorText(reply));
        }
        if (!HasCode(StatusCodes(reply), "201")) {
            undo();
            return fail(UltraIMResultCode::InvalidState, "A room by that address is there already");
        }
        // Its configuration (XEP-0045 10.1.3): named, for its members, kept.
        XmlElement config("iq", Ns::Client);
        config.Set("type", "set").Set("to", room);
        config.Add("query", Ns::MucOwner).Add(SubmitForm("http://jabber.org/protocol/muc#roomconfig",
                                                         {{"muc#roomconfig_roomname", title},
                                                          {"muc#roomconfig_membersonly", "1"},
                                                          {"muc#roomconfig_persistentroom", "1"},
                                                          {"muc#roomconfig_publicroom", "0"},
                                                          {"muc#roomconfig_whois", "anyone"},
                                                          {"mam", "1"}}));
        if (!Iq(config, reply)) {
            undo();
            return fail(UltraIMResultCode::NetworkError, "The room could not be set up: " + IqErrorText(reply));
        }
        if (!members.empty()) {
            // Members first (a members-only room lets in no one else), then invited.
            XmlElement admin("iq", Ns::Client);
            admin.Set("type", "set").Set("to", room);
            XmlElement& query = admin.Add("query", Ns::MucAdmin);
            for (const std::string& jid : members) query.Add("item").Set("affiliation", "member").Set("jid", jid);
            Iq(admin, reply);
            XmlElement invite("message", Ns::Client);
            invite.Set("to", room).Set("id", UltraIM_NewClientMessageId());
            XmlElement& x = invite.Add("x", Ns::MucUser);
            for (const std::string& jid : members) x.Add("invite").Set("to", jid).Add("reason").SetText(title);
            Send(invite);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Chat& chat = chats_[room];
            chat.joined = true;
            chat.affiliations[me] = "owner";
            for (const std::string& jid : members) chat.affiliations[jid] = "member";
        }
        Bookmark(room, title, nick, true);
        UltraIMConversation c;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            c = ConversationOf(chats_[room]);
        }
        UltraIMEvent e = EventOf(UltraIMEventType::ConversationUpdate, providerId_, room);
        e.conversation = c;
        Emit(e);
        if (onResult) onResult(UltraIMResult::Ok(), c);
    });
}

void XmppProvider::LeaveConversation(const std::string& conversationId, UltraIMResultCallback onResult) {
    Enqueue([this, conversationId, onResult]() {
        bool isGroup = false;
        std::string nick;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto chat = chats_.find(conversationId);
            if (chat == chats_.end()) {
                if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "No such conversation"));
                return;
            }
            isGroup = chat->second.isGroup;
            nick = chat->second.nick;
            chats_.erase(chat);
            if (!isGroup) hidden_.insert(conversationId);
        }
        if (isGroup) {
            XmlElement leave("presence", Ns::Client);
            leave.Set("to", conversationId + "/" + nick).Set("type", "unavailable");
            Send(leave);
            Bookmark(conversationId, std::string(), std::string(), false);
        } else {
            SaveLocalState();
        }
        Emit(EventOf(UltraIMEventType::ConversationRemove, providerId_, conversationId));
        if (onResult) onResult(UltraIMResult::Ok());
    });
}

} // namespace UltraIMXmpp
