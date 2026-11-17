// Plugins/UltraIM/irc/IrcProvider.cpp
// IRC - the channels and private messages of an IRC network as UltraIM conversations
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "IrcProvider.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetMime.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace UltraIMIrc {

using UltraCanvas::JSONValue;

namespace {

constexpr const char* kServerChatId = "*server";
constexpr size_t kHistoryKept = 5000;       // messages kept in memory per conversation
constexpr int kTypingRepeatMs = 3000;       // +typing=active at most this often

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

std::vector<std::string> SplitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(sep, start);
        if (end == std::string::npos) end = s.size();
        if (end > start) out.push_back(s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string SnippetOf(const std::string& text) {
    std::string s = text.substr(0, 80);
    // Not inside a UTF-8 character.
    while (!s.empty() && s.size() < text.size() && (static_cast<unsigned char>(text[s.size()]) & 0xC0) == 0x80)
        s.pop_back();
    std::replace(s.begin(), s.end(), '\n', ' ');
    return s;
}

} // namespace

// ============================================================================
// THE SERVER'S ADDRESS
// ============================================================================

bool IrcServerAddress::Parse(const std::string& text, IrcServerAddress& out, std::string& error) {
    out = IrcServerAddress();
    std::string t = Trim(text);
    if (t.empty()) {
        error = "Which IRC server? (irc.libera.chat, ...)";
        return false;
    }
    const std::string lower = Lower(t);
    if (lower.rfind("ircs://", 0) == 0) {
        t = t.substr(7);
    } else if (lower.rfind("irc://", 0) == 0) {
        t = t.substr(6);
        out.tls = false;
        out.port = 6667;
    } else if (t.find("://") != std::string::npos) {
        error = "\"" + text + "\" is not an IRC server's address (irc.example.org, ircs://irc.example.org:6697)";
        return false;
    }
    const size_t slash = t.find('/');
    if (slash != std::string::npos) t = t.substr(0, slash);
    std::string portText;
    if (!t.empty() && t[0] == '[') {
        const size_t close = t.find(']');
        if (close == std::string::npos) {
            error = "\"" + text + "\" is not an IRC server's address";
            return false;
        }
        out.host = t.substr(1, close - 1);
        if (close + 1 < t.size() && t[close + 1] == ':') portText = t.substr(close + 2);
    } else if (std::count(t.begin(), t.end(), ':') == 1) {
        const size_t colon = t.find(':');
        out.host = t.substr(0, colon);
        portText = t.substr(colon + 1);
    } else {
        out.host = t;
    }
    if (!portText.empty()) {
        // "+6697" is how some networks write a TLS port.
        if (portText[0] == '+') portText = portText.substr(1);
        int port = 0;
        for (char c : portText) {
            if (!std::isdigit(static_cast<unsigned char>(c)) || port > 65535) {
                port = -1;
                break;
            }
            port = port * 10 + (c - '0');
        }
        if (port <= 0 || port > 65535) {
            error = "\"" + portText + "\" is not a port number";
            return false;
        }
        out.port = port;
    }
    if (out.host.empty()) {
        error = "\"" + text + "\" names no server";
        return false;
    }
    return true;
}

std::string IrcServerAddress::ToString() const {
    const std::string h = host.find(':') != std::string::npos ? "[" + host + "]" : host;
    return std::string(tls ? "ircs://" : "irc://") + h + ":" + std::to_string(port);
}

bool IrcServerAddress::IsLocal() const {
    const std::string h = Lower(host);
    return h == "127.0.0.1" || h == "::1" || h == "localhost";
}

// ============================================================================
// LIFETIME
// ============================================================================

IrcProvider::IrcProvider(std::string providerId) : providerId_(std::move(providerId)) {}

IrcProvider::~IrcProvider() {
    Shutdown();
}

UltraIMCapabilityFlags IrcProvider::GetCapabilities() const {
    using F = UltraIMCapabilityFlags;
    F flags = F::TextMessages | F::GroupChats | F::ReplyToMessage | F::TypingIndicator | F::AddContact |
              F::CreateGroup | F::SearchMessages | F::LeaveConversation;
    if (chatHistoryOn_.load()) flags = flags | F::OfflineMessages;
    return flags;
}

UltraIMResult IrcProvider::Initialize(const UltraIMConfig& config, UltraIMEventSink sink) {
    config_ = config;
    sink_ = std::move(sink);
    if (config.dataDirectory.empty())
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                    "The IRC provider needs a data directory (UltraIMConfig::dataDirectory)");
    std::error_code ec;
    const std::filesystem::path dir =
        UltraCanvas::PathFromUtf8(config.dataDirectory) / UltraCanvas::PathFromUtf8(providerId_);
    std::filesystem::create_directories(dir, ec);
    vault_ = UltraVault::DeviceKeyVault(UltraCanvas::PathToUtf8(dir), {"irc.vault", config.appId + "." + providerId_ + "."});
    vaultReady_ = true;
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        stopping_ = false;
    }
    if (!worker_.joinable()) worker_ = std::thread([this]() { Run(); });
    return UltraIMResult::Ok();
}

void IrcProvider::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        stopping_ = true;
        jobs_.clear();
    }
    workWake_.notify_all();
    if (worker_.joinable()) worker_.join();
    state_.store(UltraIMConnectionState::Disconnected);
    sink_ = nullptr;
}

void IrcProvider::SetTlsOptions(const UltraNetTlsOptions& options) {
    std::lock_guard<std::mutex> lock(mutex_);
    tlsOptions_ = options;
}

std::string IrcProvider::GetServerAddress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return haveSession_ ? address_.ToString() : std::string();
}

std::vector<std::string> IrcProvider::GetEnabledCaps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<std::string>(enabledCaps_.begin(), enabledCaps_.end());
}

std::string IrcProvider::GetLocalUserId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // (Kept while reconnecting: the session's nick.)
    return registered_ || ready_ || wanted_ ? nick_ : std::string();
}

void IrcProvider::FetchAccountProfile(UltraIMAccountProfileCallback onResult) {
    UltraIMAccountProfile profile;
    UltraIMResult result = UltraIMResult::Ok();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ready_) {
            result = UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not connected to an IRC server");
        } else {
            profile.displayName = nick_;
            if (saslOk_ && Fold(account_) != Fold(nick_)) profile.displayName += " (" + account_ + ")";
        }
    }
    if (onResult) onResult(result, profile);
}

// ============================================================================
// THE THREAD
// ============================================================================

void IrcProvider::Enqueue(std::function<void()> job, bool locked) {
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        if (stopping_) return;
        jobs_.push_back(Job{std::move(job), locked});
    }
    workWake_.notify_one();
}

void IrcProvider::Post(std::function<void()> fn) {
    outbox_.push_back(std::move(fn));
}

void IrcProvider::Emit(const UltraIMEvent& event) {
    if (sink_) sink_(event);
}

void IrcProvider::Queue(const UltraIMEvent& event) {
    UltraIMEvent e = event;
    e.providerId = providerId_;
    Post([this, e]() { Emit(e); });
}

void IrcProvider::QueueState(UltraIMConnectionState state, const std::string& error) {
    if (state_.exchange(state) == state && error.empty()) return;
    UltraIMEvent event;
    event.type = UltraIMEventType::ConnectionStateChange;
    event.connectionState = state;
    event.errorText = error;
    Queue(event);
}

void IrcProvider::SetState(UltraIMConnectionState state, const std::string& error) {
    if (state_.exchange(state) == state && error.empty()) return;
    UltraIMEvent event;
    event.type = UltraIMEventType::ConnectionStateChange;
    event.providerId = providerId_;
    event.connectionState = state;
    event.errorText = error;
    Emit(event);
}

void IrcProvider::Flush() {
    std::vector<std::function<void()>> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        out.swap(outbox_);
    }
    for (auto& fn : out) fn();
}

void IrcProvider::Run() {
    for (;;) {
        bool open = conn_.IsOpen();
        bool wanted = false;
        Clock::time_point reconnectAt;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            wanted = wanted_;
            reconnectAt = reconnectAt_;
        }
        std::deque<Job> jobs;
        {
            std::unique_lock<std::mutex> lock(workMutex_);
            if (!open && jobs_.empty() && !stopping_) {
                Clock::time_point until = Clock::now() + std::chrono::milliseconds(250);
                if (wanted && reconnectAt < until) until = reconnectAt;
                workWake_.wait_until(lock, until, [this]() { return stopping_ || !jobs_.empty(); });
            }
            if (stopping_) break;
            jobs.swap(jobs_);
        }
        for (Job& job : jobs) {
            if (job.locked) {
                std::lock_guard<std::mutex> lock(mutex_);
                job.run();
            } else {
                job.run();
            }
            Flush();
        }
        if (conn_.IsOpen()) {
            std::vector<std::string> lines;
            const IrcConnection::ReadStatus status = conn_.Read(timings_.readSliceMs, lines);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const std::string& line : lines) {
                    if (!conn_.IsOpen()) break;
                    HandleLine(line);
                }
                if (conn_.IsOpen()) {
                    if (status == IrcConnection::ReadStatus::Closed) ConnectionLost("The server closed the connection");
                    else Housekeeping();
                }
            }
            Flush();
        } else {
            bool due = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                due = wanted_ && Clock::now() >= reconnectAt_;
            }
            if (due) {
                Reconnect();
                Flush();
            }
        }
    }
    // Shut down: a word to the server, and no callback or event any more.
    std::lock_guard<std::mutex> lock(mutex_);
    if (conn_.IsOpen()) {
        SendLine("QUIT :Leaving");
        conn_.Close();
    }
    outbox_.clear();
    wanted_ = false;
    ready_ = registered_ = false;
}

// ============================================================================
// SIGNING IN
// ============================================================================

bool IrcProvider::LoadSession(Session& out) const {
    if (!vaultReady_) return false;
    std::string text;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { v.Retrieve("session", text); });
    if (text.empty()) return false;
    UltraCanvas::JSONParseResult parsed;
    const JSONValue session = UltraCanvas::JSON::Parse(text, &parsed);
    out = Session();
    out.server = session["server"].GetString();
    out.nick = session["nick"].GetString();
    out.password = session["password"].GetString();
    out.account = session["account"].GetString();
    for (const JSONValue& c : session["channels"].GetElements()) out.channels.push_back(c.GetString());
    for (const JSONValue& q : session["queries"].GetElements()) out.queries.push_back(q.GetString());
    return !out.server.empty() && !out.nick.empty();
}

IrcProvider::Session IrcProvider::CurrentSession() const {
    Session s;
    s.server = address_.ToString();
    s.nick = wantedNick_;
    s.password = password_;
    s.account = account_;
    for (const auto& [id, chat] : chats_) {
        if (chat.kind == ChatKind::Channel && chat.wanted) s.channels.push_back(chat.name);
        else if (chat.kind == ChatKind::Private && !chat.hidden) s.queries.push_back(chat.name);
    }
    return s;
}

bool IrcProvider::SaveSession() {
    if (!vaultReady_ || !haveSession_) return false;
    const Session s = CurrentSession();
    JSONValue session = JSONValue::MakeObject();
    session.Set("server", JSONValue(s.server));
    session.Set("nick", JSONValue(s.nick));
    session.Set("password", JSONValue(s.password));
    session.Set("account", JSONValue(s.account));
    JSONValue channels = JSONValue::MakeArray();
    for (const std::string& c : s.channels) channels.Append(JSONValue(c));
    session.Set("channels", channels);
    JSONValue queries = JSONValue::MakeArray();
    for (const std::string& q : s.queries) queries.Append(JSONValue(q));
    session.Set("queries", queries);
    bool stored = false;
    vault_.Use([&](UltraVault::DeviceKeyVault& v) { stored = v.Store("session", UltraCanvas::JSON::Serialize(session)); });
    return stored;
}

bool IrcProvider::HasStoredSession() const {
    Session s;
    return LoadSession(s);
}

void IrcProvider::SignIn(const UltraIMCredentials& credentials, UltraIMResultCallback onResult) {
    if (!vaultReady_) {
        if (onResult) onResult(UltraIMResult::Error(UltraIMResultCode::NotInitialized, "Initialize first"));
        return;
    }
    SetState(UltraIMConnectionState::Connecting);
    Enqueue([this, credentials, onResult]() { SignInOnThread(credentials, onResult); }, false);
}

void IrcProvider::SignInOnThread(UltraIMCredentials credentials, UltraIMResultCallback onResult) {
    const auto fail = [this, &onResult](const UltraIMResult& r) {
        SetState(UltraIMConnectionState::Error, r.message);
        if (onResult) onResult(r);
    };
    Session stored;
    const bool haveStored = LoadSession(stored);
    const std::string server = Trim(credentials.server);
    const std::string nick = Trim(credentials.username);
    Session s;
    if (nick.empty() && server.empty()) {
        if (!haveStored)
            return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                             "Sign in with an IRC server and a nickname"));
        s = stored;
    } else {
        IrcServerAddress given, before;
        std::string ignored;
        const bool sameServer = haveStored && (server.empty() || (IrcServerAddress::Parse(server, given, ignored) &&
                                                                  IrcServerAddress::Parse(stored.server, before, ignored) &&
                                                                  given.ToString() == before.ToString()));
        s.server = server.empty() && haveStored ? stored.server : server;
        s.nick = nick.empty() && sameServer ? stored.nick : nick;
        if (s.nick.empty())
            return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Which nickname? (credentials.username)"));
        s.password = credentials.password;
        s.account = Trim(credentials.displayName).empty() ? s.nick : Trim(credentials.displayName);
        // The same server and nick as the stored session: its channels and chats too.
        if (sameServer && IrcFold(stored.nick, IrcCaseMapping::Rfc1459) == IrcFold(s.nick, IrcCaseMapping::Rfc1459)) {
            s.channels = stored.channels;
            s.queries = stored.queries;
        }
    }
    IrcServerAddress address;
    std::string error;
    if (!IrcServerAddress::Parse(s.server, address, error))
        return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, error));
    if (!IrcIsValidNick(s.nick))
        return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                         "\"" + s.nick + "\" is not a nickname IRC allows (letters, digits and []\\`_^{|}-, "
                                         "not starting with a digit)"));
    if (!s.password.empty() && !address.tls && !address.IsLocal())
        return fail(UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                         "The password is sent only over TLS: use ircs:// (port 6697), or sign in "
                                         "without it"));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wanted_ = false;
        if (conn_.IsOpen()) {
            SendLine("QUIT :Signing in again");
            conn_.Close();
        }
        DropPending(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Signing in again"));
        chats_.clear();
        away_.clear();
        quit_.clear();
        monitorOnline_.clear();
        typingSent_.clear();
        serverIds_.clear();
        address_ = address;
        wantedNick_ = s.nick;
        password_ = s.password;
        account_ = s.account;
        haveSession_ = true;
        for (const std::string& channel : s.channels) {
            if (channel.empty()) continue;
            Chat& chat = ChatFor(channel, ChatKind::Channel);
            chat.wanted = true;
        }
        resumeQueries_ = s.queries;
    }
    Flush();
    const UltraIMResult r = ConnectAndRegister();
    UltraIMResult answer = r;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!r) {
            chats_.clear();
            resumeQueries_.clear();
            QueueState(UltraIMConnectionState::Error, r.message);
        } else {
            wanted_ = true;
            reconnectTries_ = 0;
            SaveSession();
            answer.message = nickNote_;
        }
        Post([onResult, answer]() {
            if (onResult) onResult(answer);
        });
    }
    Flush();
}

void IrcProvider::SignOut() {
    Enqueue([this]() {
        wanted_ = false;
        if (conn_.IsOpen()) {
            SendLine("QUIT :Leaving");
            conn_.Close();
        }
        DropPending(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Signed out"));
        registered_ = ready_ = false;
        chats_.clear();
        resumeQueries_.clear();
        QueueState(UltraIMConnectionState::Disconnected);
    });
}

void IrcProvider::ResetConnectionState() {
    offeredCaps_.clear();
    enabledCaps_.clear();
    capLsDone_ = false;
    capReqsOpen_ = 0;
    capEnded_ = false;
    saslOk_ = false;
    registered_ = false;
    ready_ = false;
    regFailed_ = false;
    regFailure_ = UltraIMResult::Ok();
    nickTries_ = 0;
    nickNote_.clear();
    serverError_.clear();
    clockOffsetMs_ = 0;
    caseMapping_ = IrcCaseMapping::Rfc1459;
    chanTypes_ = "#&";
    prefixSymbols_ = "~&@%+";
    monitorLimit_ = 0;
    historyLimit_ = 0;
    pingOut_ = false;
    batches_.clear();
    monitored_.clear();
    monitorOnline_.clear();
    away_.clear();
    nick_ = wantedNick_;
}

UltraIMResult IrcProvider::ConnectAndRegister() {
    IrcServerAddress address;
    UltraNetTlsOptions tls;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ResetConnectionState();
        address = address_;
        tls = tlsOptions_;
    }
    std::string error;
    bool certificateRefused = false;
    if (!conn_.Open(address.host, address.port, address.tls, tls, timings_.connectTimeoutMs, error, certificateRefused)) {
        return UltraIMResult::Error(UltraIMResultCode::NetworkError,
                                    "Could not connect to " + address.host + ":" + std::to_string(address.port) + ": " + error);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastHeard_ = Clock::now();
        BeginRegistration();
    }
    Flush();
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timings_.registerTimeoutMs);
    UltraIMResult result = UltraIMResult::Ok();
    bool done = false;
    while (!done) {
        {
            std::lock_guard<std::mutex> lock(workMutex_);
            if (stopping_) {
                result = UltraIMResult::Error(UltraIMResultCode::NotConnected, "Shutting down");
                break;
            }
        }
        std::vector<std::string> lines;
        const IrcConnection::ReadStatus status = conn_.Read(timings_.readSliceMs, lines);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const std::string& line : lines) {
                if (regFailed_ || ready_) break;
                HandleLine(line);
            }
            const Clock::time_point now = Clock::now();
            if (regFailed_) {
                result = regFailure_;
                done = true;
            } else if (ready_) {
                done = true;
            } else if (status == IrcConnection::ReadStatus::Closed || !conn_.IsOpen()) {
                result = UltraIMResult::Error(UltraIMResultCode::NetworkError,
                                              "The server closed the connection" +
                                                  (serverError_.empty() ? std::string() : ": " + serverError_));
                done = true;
            } else if (registered_ && now - welcomedAt_ > std::chrono::seconds(5)) {
                OnReady();                          // no end of MOTD: ready all the same
                done = true;
            } else if (now > deadline) {
                result = UltraIMResult::Error(UltraIMResultCode::Timeout, "The IRC server did not let us in in time");
                done = true;
            }
            if (done && !result) {
                if (conn_.IsOpen()) {
                    SendLine("QUIT");
                    conn_.Close();
                }
                registered_ = ready_ = false;
            }
        }
        Flush();
    }
    if (!result) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (conn_.IsOpen()) conn_.Close();
        registered_ = ready_ = false;
    }
    return result;
}

void IrcProvider::BeginRegistration() {
    SendLine("CAP LS 302");
    SendLine("NICK " + wantedNick_);
    // The user name: the nick's letters and digits (some servers refuse others).
    std::string user;
    for (char c : wantedNick_) {
        if (std::isalnum(static_cast<unsigned char>(c))) user += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (user.empty()) user = "ultraim";
    if (user.size() > 10) user.resize(10);
    SendLine("USER " + user + " 0 * :" + wantedNick_);
}

void IrcProvider::OnCap(const IrcMessage& m) {
    const std::string sub = m.Param(1);
    if (sub == "LS") {
        const bool more = m.params.size() >= 4 && m.Param(2) == "*";
        for (const std::string& item : SplitOn(m.params.empty() ? std::string() : m.params.back(), ' ')) {
            const size_t eq = item.find('=');
            offeredCaps_[eq == std::string::npos ? item : item.substr(0, eq)] =
                eq == std::string::npos ? std::string() : item.substr(eq + 1);
        }
        if (more || capLsDone_) return;
        capLsDone_ = true;
        const bool tlsOrLocal = conn_.IsTls() || address_.IsLocal();
        std::vector<std::string> wanted;
        if (!password_.empty() && tlsOrLocal && offeredCaps_.count("sasl")) wanted.push_back("sasl");
        for (const char* cap : {"server-time", "message-tags", "echo-message", "batch", "away-notify", "account-tag",
                                "multi-prefix", "labeled-response"}) {
            if (offeredCaps_.count(cap)) wanted.push_back(cap);
        }
        const char* history = offeredCaps_.count("chathistory") ? "chathistory"
                              : offeredCaps_.count("draft/chathistory") ? "draft/chathistory" : nullptr;
        if (history) {
            wanted.push_back(history);
            if (offeredCaps_.count("draft/event-playback")) wanted.push_back("draft/event-playback");
        }
        if (wanted.empty()) {
            FinishCaps();
            return;
        }
        // Several requests when one line would be too long (each is all or nothing).
        std::string line;
        for (const std::string& cap : wanted) {
            if (!line.empty() && line.size() + cap.size() > 400) {
                SendLine("CAP REQ :" + line);
                ++capReqsOpen_;
                line.clear();
            }
            line += (line.empty() ? "" : " ") + cap;
        }
        SendLine("CAP REQ :" + line);
        ++capReqsOpen_;
    } else if (sub == "ACK" || sub == "NAK") {
        if (sub == "ACK") {
            for (const std::string& item : SplitOn(m.params.empty() ? std::string() : m.params.back(), ' ')) {
                if (item[0] == '-') enabledCaps_.erase(item.substr(1));
                else enabledCaps_.insert(item);
            }
            chatHistoryOn_.store(HasChatHistory());
        }
        if (capReqsOpen_ > 0 && --capReqsOpen_ == 0 && !capEnded_) FinishCaps();
    } else if (sub == "DEL") {
        for (const std::string& item : SplitOn(m.params.empty() ? std::string() : m.params.back(), ' '))
            enabledCaps_.erase(item);
        chatHistoryOn_.store(HasChatHistory());
    }
}

void IrcProvider::FinishCaps() {
    if (CapOn("sasl") && !password_.empty() && !saslOk_) {
        const std::string mechanisms = offeredCaps_["sasl"];
        if (!mechanisms.empty() && ("," + mechanisms + ",").find(",PLAIN,") == std::string::npos) {
            regFailed_ = true;
            regFailure_ = UltraIMResult::Error(UltraIMResultCode::AuthFailed,
                                               "The server does not take a password sign-in (SASL PLAIN); it offers " +
                                                   mechanisms);
            return;
        }
        SendLine("AUTHENTICATE PLAIN");
        return;
    }
    SendLine("CAP END");
    capEnded_ = true;
}

void IrcProvider::SendSaslPlain() {
    std::string payload = account_;
    payload += '\0';
    payload += account_;
    payload += '\0';
    payload += password_;
    const std::string encoded = UltraNet_Base64Encode(std::vector<uint8_t>(payload.begin(), payload.end()), false);
    size_t at = 0;
    while (at < encoded.size()) {
        const std::string chunk = encoded.substr(at, 400);
        SendLine("AUTHENTICATE " + chunk);
        at += chunk.size();
    }
    if (encoded.empty() || encoded.size() % 400 == 0) SendLine("AUTHENTICATE +");
}

void IrcProvider::TryNextNick(const std::string& taken) {
    if (nickTries_ >= 2) {
        regFailed_ = true;
        regFailure_ = UltraIMResult::Error(UltraIMResultCode::InvalidState,
                                           "The nicknames " + wantedNick_ + ", " + wantedNick_ + "_ and " + wantedNick_ +
                                               "__ are all in use on this network: choose another");
        return;
    }
    ++nickTries_;
    nick_ = wantedNick_ + std::string(static_cast<size_t>(nickTries_), '_');
    nickNote_ = "The nickname " + wantedNick_ + " is in use: you are " + nick_ + " on this network";
    (void)taken;
    SendLine("NICK " + nick_);
}

void IrcProvider::OnReady() {
    ready_ = true;
    lastHeard_ = Clock::now();
    pingOut_ = false;
    if (!nickNote_.empty()) ServerNote(nickNote_);
    // A server without SASL: the password goes to NickServ (never in the clear to a far server).
    if (!password_.empty() && !saslOk_ && (conn_.IsTls() || address_.IsLocal())) {
        const std::string identify = "IDENTIFY " + account_ + " " + password_;
        SendCommand("PRIVMSG", {"NickServ", identify});
        if (CapOn("echo-message")) {
            // Its echo must not show the password in a "NickServ" chat.
            Outgoing o;
            o.silent = true;
            o.target = Fold("NickServ");
            o.lines.emplace_back(std::string(), identify);
            o.deadline = Clock::now() + std::chrono::milliseconds(timings_.requestTimeoutMs);
            outgoing_.push_back(o);
        }
    }
    // The private chats of the stored session.
    for (const std::string& nick : resumeQueries_) {
        if (!IrcIsValidNick(nick)) continue;
        bool created = false;
        Chat& chat = ChatFor(nick, ChatKind::Private, &created);
        chat.hidden = false;
        if (created) QueueConversation(chat);
    }
    resumeQueries_.clear();
    for (auto& [id, chat] : chats_) {
        if (chat.kind == ChatKind::Private && !chat.hidden) Monitor(chat.name, true);
    }
    // The channels to be in: JOIN #a,#b,... in lines that fit.
    std::string line;
    for (auto& [id, chat] : chats_) {
        if (chat.kind != ChatKind::Channel || !chat.wanted || chat.joined) continue;
        if (!line.empty() && line.size() + chat.name.size() > 400) {
            SendLine("JOIN " + line);
            line.clear();
        }
        line += (line.empty() ? "" : ",") + chat.name;
    }
    if (!line.empty()) SendLine("JOIN " + line);
    QueueState(UltraIMConnectionState::Connected);
}

void IrcProvider::DropPending(const UltraIMResult& why) {
    for (PendingJoin& p : joins_) {
        auto cb = p.onResult;
        Post([cb, why]() {
            if (cb) cb(why, UltraIMConversation());
        });
    }
    joins_.clear();
    for (PendingWhois& p : whois_) {
        auto cb = p.onResult;
        Post([cb, why]() {
            if (cb) cb(why, UltraIMConversation());
        });
    }
    whois_.clear();
    for (PendingHistory& p : history_) {
        auto cb = p.onResult;
        std::vector<UltraIMMessage> page;
        if (const Chat* chat = FindChat(p.target)) page = MemoryPage(*chat, p.beforeId, p.limit);
        Post([cb, page]() {
            if (cb) cb(UltraIMResult::Ok(), page);
        });
    }
    history_.clear();
    for (PendingPart& p : parts_) {
        // Not answered: it is left here all the same.
        if (Chat* chat = FindChat(p.channel)) {
            UltraIMEvent removed;
            removed.type = UltraIMEventType::ConversationRemove;
            removed.conversationId = chat->id;
            Queue(removed);
            chats_.erase(chat->id);
        }
        auto cb = p.onResult;
        Post([cb]() {
            if (cb) cb(UltraIMResult::Ok());
        });
    }
    parts_.clear();
    for (Outgoing& o : outgoing_) {
        if (o.silent) continue;
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageDelivered;
        e.conversationId = o.conversationId;
        e.message = o.message;
        e.message.deliveryStatus = UltraIMDeliveryStatus::Failed;
        e.deliveryStatus = UltraIMDeliveryStatus::Failed;
        e.errorText = "The connection to the IRC server was lost before the server confirmed the message";
        Queue(e);
    }
    outgoing_.clear();
    batches_.clear();
}

void IrcProvider::ConnectionLost(const std::string& why) {
    if (conn_.IsOpen()) conn_.Close();
    registered_ = ready_ = false;
    DropPending(UltraIMResult::Error(UltraIMResultCode::NetworkError, why));
    for (auto& [id, chat] : chats_) {
        if (chat.kind != ChatKind::Channel) continue;
        chat.joined = false;
        chat.namesOpen = false;
        chat.historyAsked = false;
        chat.members.clear();
    }
    monitored_.clear();
    monitorOnline_.clear();
    away_.clear();
    for (auto& [id, chat] : chats_) {
        if (chat.kind == ChatKind::Private) UpdatePresence(id);
    }
    if (wanted_) {
        reconnectTries_ = 0;
        reconnectAt_ = Clock::now() + std::chrono::milliseconds(timings_.reconnectMinMs);
        QueueState(UltraIMConnectionState::Reconnecting, why);
    } else {
        QueueState(UltraIMConnectionState::Disconnected, why);
    }
}

void IrcProvider::Reconnect() {
    const UltraIMResult r = ConnectAndRegister();
    std::lock_guard<std::mutex> lock(mutex_);
    if (r) {
        reconnectTries_ = 0;
        return;
    }
    if (!wanted_) return;
    if (r.code == UltraIMResultCode::AuthFailed) {
        // The password no longer works: the user must sign in again.
        wanted_ = false;
        QueueState(UltraIMConnectionState::Error, r.message);
        UltraIMEvent event;
        event.type = UltraIMEventType::AuthRequire;
        event.errorText = r.message;
        Queue(event);
        return;
    }
    ++reconnectTries_;
    int64_t delay = timings_.reconnectMinMs;
    for (int i = 0; i < reconnectTries_ && delay < timings_.reconnectMaxMs; ++i) delay *= 2;
    delay = std::min<int64_t>(delay, timings_.reconnectMaxMs);
    reconnectAt_ = Clock::now() + std::chrono::milliseconds(delay);
    QueueState(UltraIMConnectionState::Reconnecting, r.message);
}

void IrcProvider::Housekeeping() {
    const Clock::time_point now = Clock::now();
    if (!pingOut_ && now - lastHeard_ > std::chrono::milliseconds(timings_.pingAfterMs)) {
        SendLine("PING :ultraim-" + std::to_string(++counter_));
        pingOut_ = true;
        pingSentAt_ = now;
    } else if (pingOut_ && now - pingSentAt_ > std::chrono::milliseconds(timings_.pingTimeoutMs)) {
        ConnectionLost("The IRC server stopped answering");
        return;
    }
    for (size_t i = 0; i < joins_.size();) {
        if (now < joins_[i].deadline) {
            ++i;
            continue;
        }
        auto cb = joins_[i].onResult;
        joins_.erase(joins_.begin() + static_cast<std::ptrdiff_t>(i));
        Post([cb]() {
            if (cb) cb(UltraIMResult::Error(UltraIMResultCode::Timeout, "The server did not answer the JOIN"),
                       UltraIMConversation());
        });
    }
    for (size_t i = 0; i < whois_.size();) {
        if (now < whois_[i].deadline) {
            ++i;
            continue;
        }
        auto cb = whois_[i].onResult;
        whois_.erase(whois_.begin() + static_cast<std::ptrdiff_t>(i));
        Post([cb]() {
            if (cb) cb(UltraIMResult::Error(UltraIMResultCode::Timeout, "The server did not answer the WHOIS"),
                       UltraIMConversation());
        });
    }
    while (!history_.empty() && now >= history_.front().deadline) {
        PendingHistory p = history_.front();
        history_.pop_front();
        std::vector<UltraIMMessage> page;
        if (const Chat* chat = FindChat(p.target)) page = MemoryPage(*chat, p.beforeId, p.limit);
        auto cb = p.onResult;
        Post([cb, page]() {
            if (cb) cb(UltraIMResult::Ok(), page);
        });
    }
    for (size_t i = 0; i < parts_.size();) {
        if (now < parts_[i].deadline) {
            ++i;
            continue;
        }
        PendingPart p = parts_[i];
        parts_.erase(parts_.begin() + static_cast<std::ptrdiff_t>(i));
        if (Chat* chat = FindChat(p.channel)) {
            UltraIMEvent removed;
            removed.type = UltraIMEventType::ConversationRemove;
            removed.conversationId = chat->id;
            Queue(removed);
            chats_.erase(p.channel);
            SaveSession();
        }
        Post([cb = p.onResult]() {
            if (cb) cb(UltraIMResult::Ok());
        });
    }
    for (size_t i = 0; i < outgoing_.size();) {
        if (now < outgoing_[i].deadline) {
            ++i;
            continue;
        }
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageDelivered;
        e.conversationId = outgoing_[i].conversationId;
        e.message = outgoing_[i].message;
        e.message.deliveryStatus = UltraIMDeliveryStatus::Failed;
        e.deliveryStatus = UltraIMDeliveryStatus::Failed;
        e.errorText = "The IRC server did not confirm the message";
        if (!outgoing_[i].silent) Queue(e);
        outgoing_.erase(outgoing_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

bool IrcProvider::SendLine(const std::string& line) {
    return conn_.Send(line);
}

bool IrcProvider::SendCommand(const std::string& command, const std::vector<std::string>& params,
                              const std::map<std::string, std::string>& tags) {
    return SendLine(IrcFormatLine(tags, command, params));
}

std::string IrcProvider::NewLabel() {
    return "u" + std::to_string(++counter_);
}

bool IrcProvider::HasChatHistory() const {
    return CapOn("draft/chathistory") || CapOn("chathistory");
}

// ============================================================================
// LINES
// ============================================================================

void IrcProvider::HandleLine(const std::string& raw) {
    IrcMessage m;
    if (!IrcMessage::Parse(raw, m)) return;
    lastHeard_ = Clock::now();
    pingOut_ = false;
    // A live line's server-time sets the server's clock - not a message's, which a
    // bouncer may play back long after it was written, nor one in a batch.
    if (m.HasTag("time") && m.command != "PRIVMSG" && m.command != "NOTICE" && m.Tag("batch").empty()) {
        const int64_t at = IrcParseServerTime(m.Tag("time"));
        if (at > 0) clockOffsetMs_ = at - NowMs();
    }
    // A line of a chathistory batch is kept for the batch's end.
    std::string ref = m.Tag("batch");
    for (int depth = 0; !ref.empty() && depth < 8; ++depth) {
        auto it = batches_.find(ref);
        if (it == batches_.end()) break;
        if (it->second.type == "chathistory" || it->second.type == "draft/chathistory") {
            if (m.command == "PRIVMSG" || m.command == "NOTICE") it->second.lines.push_back(m);
            return;
        }
        ref = it->second.parent;
    }
    HandleMessage(m);
}

std::string IrcProvider::EffectiveLabel(const IrcMessage& m) const {
    if (m.HasTag("label")) return m.Tag("label");
    std::string ref = m.Tag("batch");
    for (int depth = 0; !ref.empty() && depth < 8; ++depth) {
        auto it = batches_.find(ref);
        if (it == batches_.end()) break;
        if (!it->second.label.empty()) return it->second.label;
        ref = it->second.parent;
    }
    return std::string();
}

void IrcProvider::HandleMessage(const IrcMessage& m) {
    const std::string& c = m.command;
    if (c == "PING") {
        SendCommand("PONG", {m.Param(0)});
    } else if (c == "PONG") {
        // (lastHeard_ is set by every line.)
    } else if (c == "CAP") {
        OnCap(m);
    } else if (c == "AUTHENTICATE") {
        if (m.Param(0) == "+") SendSaslPlain();
    } else if (c == "900") {
        account_ = m.Param(2).empty() ? account_ : m.Param(2);
    } else if (c == "903") {
        saslOk_ = true;
        if (!capEnded_) {
            SendLine("CAP END");
            capEnded_ = true;
        }
    } else if (c == "902" || c == "904" || c == "905" || c == "906") {
        const std::string why = c == "902" ? "The account cannot be used now (it is locked or held)"
                                           : "The server did not accept the password for the account " + account_;
        if (!registered_) {
            regFailed_ = true;
            regFailure_ = UltraIMResult::Error(UltraIMResultCode::AuthFailed, why);
            SendLine("QUIT");
        } else {
            ServerNote(why);
        }
    } else if (c == "464") {
        regFailed_ = !registered_;
        regFailure_ = UltraIMResult::Error(UltraIMResultCode::AuthFailed, "The server's password is not right");
    } else if (c == "465") {
        regFailed_ = !registered_;
        regFailure_ = UltraIMResult::Error(UltraIMResultCode::PermissionDenied,
                                           "The server does not let you in: " + m.Param(m.params.size() - 1));
    } else if (c == "001") {
        registered_ = true;
        welcomedAt_ = Clock::now();
        if (!m.Param(0).empty()) nick_ = m.Param(0);
        if (Fold(nick_) == Fold(wantedNick_)) nickNote_.clear();
    } else if (c == "005") {
        for (size_t i = 1; i + 1 < m.params.size(); ++i) {
            const std::string& token = m.params[i];
            const size_t eq = token.find('=');
            const std::string key = eq == std::string::npos ? token : token.substr(0, eq);
            const std::string value = eq == std::string::npos ? std::string() : token.substr(eq + 1);
            if (key == "CASEMAPPING") caseMapping_ = IrcCaseMappingFromName(value);
            else if (key == "CHANTYPES") chanTypes_ = value;
            else if (key == "NETWORK") network_ = value;
            else if (key == "MONITOR") monitorLimit_ = value.empty() ? 100 : std::max(0, std::atoi(value.c_str()));
            else if (key == "CHATHISTORY" || key == "draft/CHATHISTORY") historyLimit_ = std::atoi(value.c_str());
            else if (key == "PREFIX") {
                const size_t close = value.find(')');
                if (close != std::string::npos) prefixSymbols_ = value.substr(close + 1);
            }
        }
    } else if (c == "376" || c == "422") {
        if (registered_ && !ready_) OnReady();
    } else if (c == "433" || c == "432" || c == "436") {
        if (!registered_) {
            if (c == "432") {
                regFailed_ = true;
                regFailure_ = UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                                   "The server does not allow the nickname " + m.Param(1));
            } else {
                TryNextNick(m.Param(1));
            }
        } else {
            ServerNote("The nickname " + m.Param(1) + " is in use");
        }
    } else if (c == "ERROR") {
        serverError_ = m.Param(0);
        if (!ready_) {
            regFailed_ = true;
            regFailure_ = UltraIMResult::Error(UltraIMResultCode::NetworkError,
                                               "The server closed the connection: " + serverError_);
        } else {
            ConnectionLost("The server closed the connection: " + serverError_);
        }
    } else if (c == "JOIN") {
        OnJoin(m);
    } else if (c == "PART") {
        OnPart(m.nick, m.Param(0), false, std::string(), m.Param(1));
    } else if (c == "KICK") {
        OnPart(m.Param(1), m.Param(0), true, m.nick, m.Param(2));
    } else if (c == "QUIT") {
        OnQuit(m);
    } else if (c == "NICK") {
        OnNick(m);
    } else if (c == "PRIVMSG" || c == "NOTICE") {
        OnPrivmsg(m, c == "NOTICE");
    } else if (c == "TAGMSG") {
        OnTagmsg(m);
    } else if (c == "BATCH") {
        const std::string ref = m.Param(0);
        if (ref.size() > 1 && ref[0] == '+') {
            Batch b;
            b.type = m.Param(1);
            b.params.assign(m.params.size() > 2 ? m.params.begin() + 2 : m.params.end(), m.params.end());
            b.label = m.Tag("label");
            b.parent = m.Tag("batch");
            batches_[ref.substr(1)] = b;
        } else if (ref.size() > 1 && ref[0] == '-') {
            OnBatchEnd(ref.substr(1));
        }
    } else if (c == "ACK" || c == "ACCOUNT" || c == "CHGHOST" || c == "SETNAME" || c == "MODE" || c == "341") {
        // Nothing to show.
    } else {
        OnServerReply(m);
    }
}

void IrcProvider::OnServerReply(const IrcMessage& m) {
    const std::string& c = m.command;
    if (c == "TOPIC") {
        if (Chat* chat = FindChat(Fold(m.Param(0)))) {
            chat->topic = IrcStripFormatting(IrcDecodeText(m.Param(1)));
            SystemLine(*chat, m.nick + (chat->topic.empty() ? " cleared the topic" : " set the topic: " + chat->topic));
        }
    } else if (c == "332") {
        if (Chat* chat = FindChat(Fold(m.Param(1)))) {
            chat->topic = IrcStripFormatting(IrcDecodeText(m.Param(2)));
            QueueConversation(*chat);
        }
    } else if (c == "331") {
        if (Chat* chat = FindChat(Fold(m.Param(1)))) chat->topic.clear();
    } else if (c == "353") {
        Chat* chat = FindChat(Fold(m.Param(2)));
        if (!chat) return;
        if (!chat->namesOpen) {
            chat->names.clear();
            chat->namesOpen = true;
        }
        for (std::string name : SplitOn(m.Param(3), ' ')) {
            while (!name.empty() && prefixSymbols_.find(name[0]) != std::string::npos) name.erase(0, 1);
            const size_t bang = name.find('!');
            if (bang != std::string::npos) name = name.substr(0, bang);
            if (!name.empty()) chat->names[Fold(name)] = name;
        }
    } else if (c == "366") {
        const std::string id = Fold(m.Param(1));
        Chat* chat = FindChat(id);
        if (!chat) return;
        if (chat->namesOpen) {
            chat->members = chat->names;
            chat->names.clear();
            chat->namesOpen = false;
        }
        if (chat->joined) chat->members[Fold(nick_)] = nick_;
        QueueConversation(*chat);
        for (const auto& [folded, written] : chat->members) {
            quit_.erase(folded);
            if (!IsMe(written)) UpdatePresence(folded);
        }
        const UltraIMConversation conversation = ConversationOf(*chat);
        for (size_t i = 0; i < joins_.size();) {
            if (joins_[i].channel != id) {
                ++i;
                continue;
            }
            for (const std::string& nick : joins_[i].invite) SendCommand("INVITE", {nick, chat->name});
            auto cb = joins_[i].onResult;
            joins_.erase(joins_.begin() + static_cast<std::ptrdiff_t>(i));
            Post([cb, conversation]() {
                if (cb) cb(UltraIMResult::Ok(), conversation);
            });
        }
        if (chat->joined && !chat->historyAsked && HasChatHistory()) AskLatestHistory(*chat);
    } else if (c == "AWAY") {
        const std::string folded = Fold(m.nick);
        if (m.params.empty() || m.Param(0).empty()) away_.erase(folded);
        else away_.insert(folded);
        UpdatePresence(folded);
    } else if (c == "730" || c == "731") {
        for (std::string target : SplitOn(m.Param(1), ',')) {
            const size_t bang = target.find('!');
            if (bang != std::string::npos) target = target.substr(0, bang);
            const std::string folded = Fold(target);
            monitorOnline_[folded] = c == "730";
            if (c == "730") quit_.erase(folded);
            else away_.erase(folded);
            UpdatePresence(folded);
        }
    } else if (c == "311") {
        const std::string folded = Fold(m.Param(1));
        for (PendingWhois& p : whois_) {
            if (p.nick == folded) {
                p.found = true;
                p.written = m.Param(1);
            }
        }
    } else if (c == "318") {
        const std::string folded = Fold(m.Param(1));
        for (size_t i = 0; i < whois_.size();) {
            if (whois_[i].nick != folded) {
                ++i;
                continue;
            }
            PendingWhois p = whois_[i];
            whois_.erase(whois_.begin() + static_cast<std::ptrdiff_t>(i));
            if (!p.found) {
                Post([cb = p.onResult, nick = p.written]() {
                    if (cb) cb(UltraIMResult::Error(UltraIMResultCode::NotFound, "No one is called " + nick + " on this network"),
                               UltraIMConversation());
                });
                continue;
            }
            bool created = false;
            Chat& chat = ChatFor(p.written, ChatKind::Private, &created);
            chat.name = p.written;
            const bool wasHidden = chat.hidden;
            chat.hidden = false;
            // Found by WHOIS: on the network now.
            if (monitorLimit_ <= 0) monitorOnline_[folded] = true;
            quit_.erase(folded);
            chat.presence = PresenceOf(folded);
            Monitor(chat.name, true);
            if (created || wasHidden) SaveSession();
            QueueConversation(chat);
            Post([cb = p.onResult, conversation = ConversationOf(chat)]() {
                if (cb) cb(UltraIMResult::Ok(), conversation);
            });
        }
    } else if (c == "401" || c == "406") {
        const std::string folded = Fold(m.Param(1));
        bool answered = false;
        for (size_t i = 0; i < whois_.size();) {
            if (whois_[i].nick != folded) {
                ++i;
                continue;
            }
            Post([cb = whois_[i].onResult, nick = m.Param(1)]() {
                if (cb) cb(UltraIMResult::Error(UltraIMResultCode::NotFound, "No one is called " + nick + " on this network"),
                           UltraIMConversation());
            });
            whois_.erase(whois_.begin() + static_cast<std::ptrdiff_t>(i));
            answered = true;
        }
        if (!answered && c == "401") {
            FailOutgoing(m.Param(1), "No one is called " + m.Param(1) + " on the network now");
            if (Chat* chat = FindChat(folded)) SystemLine(*chat, m.Param(1) + " is not on the network now");
        }
    } else if (c == "404") {
        FailOutgoing(m.Param(1), "The channel " + m.Param(1) + " does not take your messages: " + m.Param(2));
    } else if (c == "442") {
        // Not in it: a PART waiting for that is done.
        const std::string id = Fold(m.Param(1));
        for (size_t i = 0; i < parts_.size();) {
            if (parts_[i].channel != id) {
                ++i;
                continue;
            }
            Post([cb = parts_[i].onResult]() {
                if (cb) cb(UltraIMResult::Ok());
            });
            parts_.erase(parts_.begin() + static_cast<std::ptrdiff_t>(i));
            if (FindChat(id)) {
                UltraIMEvent removed;
                removed.type = UltraIMEventType::ConversationRemove;
                removed.conversationId = id;
                Queue(removed);
                chats_.erase(id);
                SaveSession();
            }
        }
    } else if (c == "INVITE") {
        ServerNote(m.nick + " invites you to " + m.Param(1) + " (add " + m.Param(1) + " to join it)", m.nick);
    } else if (c == "FAIL" || c == "WARN" || c == "NOTE") {
        if (c == "FAIL" && m.Param(0) == "CHATHISTORY") {
            // FAIL CHATHISTORY <code> <subcommand> <target> :text - the target is the last but one.
            FailHistory(m.params.size() >= 2 ? Fold(m.params[m.params.size() - 2]) : std::string());
        } else {
            ServerNote(m.params.empty() ? c : m.params.back());
        }
    } else if (OnJoinError(m)) {
        // answered
    } else if (m.IsNumeric() && m.command[0] >= '4' && m.command[0] <= '5') {
        // An error no request waited for: in the server's conversation.
        std::string text;
        for (size_t i = 1; i < m.params.size(); ++i) {
            if (!text.empty()) text += i + 1 == m.params.size() ? ": " : " ";
            text += m.params[i];
        }
        ServerNote(IrcDecodeText(text));
    }
}

bool IrcProvider::OnJoinError(const IrcMessage& m) {
    const std::string& c = m.command;
    UltraIMResultCode code = UltraIMResultCode::PermissionDenied;
    const std::string channel = m.Param(1);
    std::string why;
    if (c == "403" || c == "476") {
        code = UltraIMResultCode::NotFound;
        why = "There is no channel " + channel + " (" + m.Param(2) + ")";
    } else if (c == "405") {
        code = UltraIMResultCode::InvalidState;
        why = "You are in too many channels to join " + channel;
    } else if (c == "471") {
        why = channel + " is full";
    } else if (c == "473") {
        why = channel + " is invite-only: someone in it must invite you";
    } else if (c == "474") {
        why = "You are banned from " + channel;
    } else if (c == "475") {
        why = channel + " needs a key: add it after the name (\"" + channel + " key\")";
    } else if (c == "477" || c == "489") {
        why = channel + " is for registered nicknames (or TLS connections) only: sign in with your account's password";
    } else {
        return false;
    }
    const std::string id = Fold(channel);
    bool answered = false;
    for (size_t i = 0; i < joins_.size();) {
        if (joins_[i].channel != id) {
            ++i;
            continue;
        }
        Post([cb = joins_[i].onResult, code, why]() {
            if (cb) cb(UltraIMResult::Error(code, why), UltraIMConversation());
        });
        joins_.erase(joins_.begin() + static_cast<std::ptrdiff_t>(i));
        answered = true;
    }
    if (!answered) {
        ServerNote(why);
        // A channel of the session that cannot be joined again: not tried again.
        Chat* chat = FindChat(id);
        if (chat && chat->kind == ChatKind::Channel && !chat->joined && c != "405") {
            chat->wanted = false;
            UltraIMEvent removed;
            removed.type = UltraIMEventType::ConversationRemove;
            removed.conversationId = id;
            Queue(removed);
            chats_.erase(id);
            SaveSession();
        }
    }
    return true;
}

void IrcProvider::OnJoin(const IrcMessage& m) {
    const std::string channel = m.Param(0);
    if (channel.empty()) return;
    const std::string folded = Fold(m.nick);
    if (IsMe(m.nick)) {
        if (!m.user.empty()) myUser_ = m.user;
        if (!m.host.empty()) myHost_ = m.host;
        bool created = false;
        Chat& chat = ChatFor(channel, ChatKind::Channel, &created);
        chat.name = channel;
        const bool wasWanted = chat.wanted;
        chat.joined = true;
        chat.wanted = true;
        chat.historyAsked = false;
        chat.members.clear();
        chat.members[folded] = m.nick;
        chat.names.clear();
        chat.namesOpen = false;
        if (!wasWanted) SaveSession();
        return;                                 // told at the end of NAMES (366)
    }
    Chat* chat = FindChat(Fold(channel));
    if (!chat || !chat->joined) return;
    chat->members[folded] = m.nick;
    quit_.erase(folded);
    QueueConversation(*chat);
    UpdatePresence(folded);
}

void IrcProvider::OnPart(const std::string& nick, const std::string& channel, bool kicked, const std::string& by,
                         const std::string& reason) {
    const std::string id = Fold(channel);
    Chat* chat = FindChat(id);
    if (!chat || chat->kind != ChatKind::Channel) return;
    const std::string why = IrcStripFormatting(IrcDecodeText(reason));
    if (IsMe(nick)) {
        std::vector<std::string> former;
        for (const auto& [folded, written] : chat->members) former.push_back(folded);
        if (kicked) {
            chat->joined = false;
            chat->wanted = false;
            chat->members.clear();
            SystemLine(*chat, "You were removed from " + chat->name + " by " + by + (why.empty() ? "" : ": " + why));
            SaveSession();
        } else {
            for (size_t i = 0; i < parts_.size();) {
                if (parts_[i].channel != id) {
                    ++i;
                    continue;
                }
                Post([cb = parts_[i].onResult]() {
                    if (cb) cb(UltraIMResult::Ok());
                });
                parts_.erase(parts_.begin() + static_cast<std::ptrdiff_t>(i));
            }
            UltraIMEvent removed;
            removed.type = UltraIMEventType::ConversationRemove;
            removed.conversationId = id;
            Queue(removed);
            chats_.erase(id);
            SaveSession();
        }
        for (const std::string& folded : former) UpdatePresence(folded);
        return;
    }
    const std::string folded = Fold(nick);
    chat->members.erase(folded);
    if (kicked) SystemLine(*chat, by + " removed " + nick + " from " + chat->name + (why.empty() ? "" : ": " + why));
    QueueConversation(*chat);
    UpdatePresence(folded);
}

void IrcProvider::OnQuit(const IrcMessage& m) {
    const std::string folded = Fold(m.nick);
    if (IsMe(m.nick)) return;
    quit_.insert(folded);
    away_.erase(folded);
    monitorOnline_.erase(folded);
    for (auto& [id, chat] : chats_) {
        if (chat.kind == ChatKind::Channel && chat.members.erase(folded)) QueueConversation(chat);
    }
    UpdatePresence(folded);
}

void IrcProvider::OnNick(const IrcMessage& m) {
    const std::string oldNick = m.nick;
    const std::string newNick = m.Param(0);
    if (newNick.empty()) return;
    const std::string oldFolded = Fold(oldNick);
    const std::string newFolded = Fold(newNick);
    const bool me = IsMe(oldNick);
    if (me) nick_ = newNick;
    for (auto& [id, chat] : chats_) {
        if (chat.kind == ChatKind::Channel && chat.members.erase(oldFolded)) {
            chat.members[newFolded] = newNick;
            QueueConversation(chat);
        }
    }
    if (me) return;
    if (away_.erase(oldFolded)) away_.insert(newFolded);
    auto it = chats_.find(oldFolded);
    if (it == chats_.end() || it->second.kind != ChatKind::Private) return;
    // The private chat follows the nick.
    Chat moved = std::move(it->second);
    chats_.erase(it);
    UltraIMEvent removed;
    removed.type = UltraIMEventType::ConversationRemove;
    removed.conversationId = oldFolded;
    Queue(removed);
    Monitor(oldNick, false);
    moved.id = newFolded;
    moved.name = newNick;
    for (UltraIMMessage& msg : moved.history) msg.conversationId = newFolded;
    Chat& chat = chats_[newFolded] = std::move(moved);
    Monitor(newNick, true);
    SystemLine(chat, oldNick + " is now known as " + newNick);
    UpdatePresence(newFolded);
    SaveSession();
}

void IrcProvider::OnTagmsg(const IrcMessage& m) {
    if (IsMe(m.nick)) return;
    std::string typing = m.Tag("+typing");
    if (typing.empty()) typing = m.Tag("+draft/typing");
    if (typing.empty()) return;
    const std::string target = m.Param(0);
    const std::string id = IsChannelName(target) ? Fold(target) : Fold(m.nick);
    const Chat* chat = FindChat(id);
    if (!chat || chat->hidden) return;
    UltraIMEvent e;
    e.type = UltraIMEventType::TypingChange;
    e.conversationId = id;
    e.contactId = m.nick;
    e.isTyping = typing == "active";
    Queue(e);
}

void IrcProvider::OnBatchEnd(const std::string& ref) {
    auto it = batches_.find(ref);
    if (it == batches_.end()) return;
    Batch b = std::move(it->second);
    batches_.erase(it);
    if (b.type != "chathistory" && b.type != "draft/chathistory") return;
    const std::string target = b.params.empty() ? std::string() : Fold(b.params[0]);
    std::string label = b.label;
    for (std::string parent = b.parent; label.empty() && !parent.empty();) {
        auto p = batches_.find(parent);
        if (p == batches_.end()) break;
        label = p->second.label;
        parent = p->second.parent;
    }
    auto pending = history_.end();
    if (!label.empty()) {
        pending = std::find_if(history_.begin(), history_.end(), [&](const PendingHistory& p) { return p.label == label; });
    }
    if (pending == history_.end()) {
        pending = std::find_if(history_.begin(), history_.end(), [&](const PendingHistory& p) { return p.target == target; });
    }
    if (pending == history_.end()) return;
    PendingHistory p = *pending;
    history_.erase(pending);
    ResolveHistory(p, b.lines);
}

void IrcProvider::ResolveHistory(PendingHistory p, const std::vector<IrcMessage>& lines) {
    std::vector<UltraIMMessage> page;
    for (const IrcMessage& line : lines) {
        UltraIMMessage msg;
        std::string name;
        ChatKind kind = ChatKind::Private;
        if (!MessageFrom(line, line.command == "NOTICE", false, msg, name, kind)) continue;
        msg.conversationId = p.target;
        page.push_back(msg);
    }
    std::stable_sort(page.begin(), page.end(),
                     [](const UltraIMMessage& a, const UltraIMMessage& b) { return a.timestampMs < b.timestampMs; });
    if (Chat* chat = FindChat(p.target)) {
        const bool hadHistory = !chat->history.empty();
        const int64_t lastKnown = hadHistory ? chat->history.back().timestampMs : 0;
        for (UltraIMMessage& msg : page) {
            FillReply(*chat, msg);
            const bool known = std::any_of(chat->history.begin(), chat->history.end(),
                                           [&](const UltraIMMessage& h) { return h.messageId == msg.messageId; });
            if (known) continue;
            // Back after a lost connection: what came meanwhile is new.
            if (p.onJoin && hadHistory && msg.timestampMs >= lastKnown) Receive(*chat, msg, false);
            else Remember(*chat, msg);
        }
        QueueConversation(*chat);
    }
    if (p.onResult) {
        Post([cb = p.onResult, page]() { cb(UltraIMResult::Ok(), page); });
    }
}

void IrcProvider::FailHistory(const std::string& target) {
    auto pending = std::find_if(history_.begin(), history_.end(),
                                [&](const PendingHistory& p) { return target.empty() || p.target == target; });
    if (pending == history_.end()) pending = history_.begin();
    if (pending == history_.end()) return;
    PendingHistory p = *pending;
    history_.erase(pending);
    std::vector<UltraIMMessage> page;
    if (const Chat* chat = FindChat(p.target)) page = MemoryPage(*chat, p.beforeId, p.limit);
    if (p.onResult) Post([cb = p.onResult, page]() { cb(UltraIMResult::Ok(), page); });
}

void IrcProvider::AskLatestHistory(Chat& chat) {
    chat.historyAsked = true;
    PendingHistory p;
    p.target = chat.id;
    p.onJoin = true;
    p.limit = 50;
    p.deadline = Clock::now() + std::chrono::milliseconds(timings_.requestTimeoutMs);
    std::map<std::string, std::string> tags;
    if (CapOn("labeled-response")) tags["label"] = p.label = NewLabel();
    const int limit = historyLimit_ > 0 ? std::min(historyLimit_, 50) : 50;
    history_.push_back(p);
    SendCommand("CHATHISTORY", {"LATEST", chat.name, "*", std::to_string(limit)}, tags);
}

// ============================================================================
// MESSAGES
// ============================================================================

bool IrcProvider::MessageFrom(const IrcMessage& m, bool notice, bool live, UltraIMMessage& out,
                              std::string& conversationName, ChatKind& kind) {
    if (m.params.size() < 2) return false;
    std::string target = m.Param(0);
    // STATUSMSG ("@#chat": to its operators): the channel.
    while (target.size() > 1 && prefixSymbols_.find(target[0]) != std::string::npos &&
           chanTypes_.find(target[1]) != std::string::npos)
        target.erase(0, 1);
    std::string text = m.Param(1);
    const bool fromMe = IsMe(m.nick);
    bool action = false;
    if (!text.empty() && text[0] == '\x01') {
        std::string body = text.substr(1);
        if (!body.empty() && body.back() == '\x01') body.pop_back();
        const size_t space = body.find(' ');
        std::string command = space == std::string::npos ? body : body.substr(0, space);
        for (char& ch : command) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        const std::string argument = space == std::string::npos ? std::string() : body.substr(space + 1);
        if (command != "ACTION") {
            // A CTCP request to us is answered, never shown; a reply is not shown either.
            if (live && !notice && !fromMe && !IsChannelName(target) && !m.FromServer()) {
                std::string reply;
                if (command == "VERSION") reply = "VERSION UltraIM IRC 0.1.0";
                else if (command == "PING") reply = "PING " + argument;
                else if (command == "TIME") reply = "TIME " + IrcFormatServerTime(NowMs());
                else if (command == "CLIENTINFO") reply = "CLIENTINFO ACTION CLIENTINFO PING TIME VERSION";
                if (!reply.empty()) SendCommand("NOTICE", {m.nick, "\x01" + reply + "\x01"});
            }
            return false;
        }
        action = true;
        text = argument;
    }
    std::string readable = IrcStripFormatting(IrcDecodeText(text));
    if (action) readable = "* " + m.nick + " " + readable;

    if (m.FromServer() || target == "*" || (!target.empty() && target[0] == '$')) {
        kind = ChatKind::Server;
        conversationName = kServerChatId;
    } else if (IsChannelName(target)) {
        kind = ChatKind::Channel;
        conversationName = target;
    } else if (fromMe) {
        kind = ChatKind::Private;
        conversationName = target;
    } else if (notice && live) {
        // A notice from a nick with no chat open (NickServ, ChanServ): the server's conversation.
        const Chat* open = FindChat(Fold(m.nick));
        if (!open || open->hidden || open->kind != ChatKind::Private) {
            kind = ChatKind::Server;
            conversationName = kServerChatId;
        } else {
            kind = ChatKind::Private;
            conversationName = m.nick;
        }
    } else {
        kind = ChatKind::Private;
        conversationName = m.nick;
    }

    out = UltraIMMessage();
    out.providerId = providerId_;
    out.conversationId = kind == ChatKind::Server ? std::string(kServerChatId) : Fold(conversationName);
    out.senderId = m.nick;
    out.senderName = m.nick;
    out.type = UltraIMMessageType::Text;
    out.text = readable;
    const int64_t at = m.HasTag("time") ? IrcParseServerTime(m.Tag("time")) : 0;
    out.timestampMs = at > 0 ? at : ServerNow();
    out.isOwn = fromMe;
    out.deliveryStatus = UltraIMDeliveryStatus::Sent;
    const std::string msgid = m.Tag("msgid");
    if (!msgid.empty()) {
        out.messageId = msgid;
        serverIds_.insert(msgid);
    } else {
        out.messageId = NewMessageId();
    }
    std::string reply = m.Tag("+draft/reply");
    if (reply.empty()) reply = m.Tag("+reply");
    out.replyTo.messageId = reply;
    return true;
}

void IrcProvider::OnPrivmsg(const IrcMessage& m, bool notice) {
    if (m.params.size() < 2) return;
    if (!notice && IsMe(m.nick) && CapOn("echo-message") && MatchEcho(m, m.Param(0), m.Param(1))) return;
    UltraIMMessage msg;
    std::string name;
    ChatKind kind = ChatKind::Private;
    if (!MessageFrom(m, notice, true, msg, name, kind)) return;
    bool created = false;
    Chat& chat = kind == ChatKind::Server ? ServerChat() : ChatFor(name, kind, &created);
    if (kind == ChatKind::Server) created = chat.history.empty() && chat.unread == 0;
    Receive(chat, msg, created);
}

bool IrcProvider::MatchEcho(const IrcMessage& m, const std::string& target, const std::string& text) {
    const std::string label = EffectiveLabel(m);
    std::string t = target;
    while (t.size() > 1 && prefixSymbols_.find(t[0]) != std::string::npos && chanTypes_.find(t[1]) != std::string::npos)
        t.erase(0, 1);
    const std::string folded = Fold(t);
    for (size_t i = 0; i < outgoing_.size(); ++i) {
        Outgoing& o = outgoing_[i];
        auto line = o.lines.end();
        if (!label.empty()) {
            line = std::find_if(o.lines.begin(), o.lines.end(),
                                [&](const std::pair<std::string, std::string>& l) { return l.first == label; });
        } else if (o.target == folded && !o.lines.empty() && o.lines.front().second == text) {
            line = o.lines.begin();
        }
        if (line == o.lines.end()) continue;
        const bool first = o.serverId.empty() && o.serverTimeMs == 0;
        o.lines.erase(line);
        if (first) {
            o.serverId = m.Tag("msgid");
            o.serverTimeMs = m.HasTag("time") ? IrcParseServerTime(m.Tag("time")) : 0;
            if (o.serverTimeMs == 0) o.serverTimeMs = -1;   // (taken: the first line came)
        }
        if (!o.lines.empty()) return true;
        if (o.silent) {
            outgoing_.erase(outgoing_.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
        // Every line came back: sent, with the server's id.
        UltraIMMessage message = o.message;
        message.deliveryStatus = UltraIMDeliveryStatus::Sent;
        if (!o.serverId.empty()) {
            message.messageId = o.serverId;
            serverIds_.insert(o.serverId);
        }
        if (o.serverTimeMs > 0) message.timestampMs = o.serverTimeMs;
        if (Chat* chat = FindChat(o.conversationId)) {
            Remember(*chat, message);
            QueueConversation(*chat);
        }
        UltraIMEvent delivered;
        delivered.type = UltraIMEventType::MessageDelivered;
        delivered.conversationId = o.conversationId;
        delivered.message = message;
        delivered.deliveryStatus = UltraIMDeliveryStatus::Sent;
        Queue(delivered);
        UltraIMEvent updated;
        updated.type = UltraIMEventType::MessageUpdate;
        updated.conversationId = o.conversationId;
        updated.message = message;
        Queue(updated);
        outgoing_.erase(outgoing_.begin() + static_cast<std::ptrdiff_t>(i));
        return true;
    }
    return false;
}

void IrcProvider::FailOutgoing(const std::string& target, const std::string& why) {
    const std::string folded = Fold(target);
    for (size_t i = 0; i < outgoing_.size(); ++i) {
        if (outgoing_[i].target != folded || outgoing_[i].silent) continue;
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageDelivered;
        e.conversationId = outgoing_[i].conversationId;
        e.message = outgoing_[i].message;
        e.message.deliveryStatus = UltraIMDeliveryStatus::Failed;
        e.deliveryStatus = UltraIMDeliveryStatus::Failed;
        e.errorText = why;
        Queue(e);
        outgoing_.erase(outgoing_.begin() + static_cast<std::ptrdiff_t>(i));
        return;
    }
}

void IrcProvider::FillReply(const Chat& chat, UltraIMMessage& message) const {
    if (message.replyTo.messageId.empty()) return;
    for (auto it = chat.history.rbegin(); it != chat.history.rend(); ++it) {
        if (it->messageId != message.replyTo.messageId) continue;
        message.replyTo.senderId = it->senderId;
        message.replyTo.senderName = it->senderName;
        message.replyTo.textSnippet = SnippetOf(it->text);
        return;
    }
}

void IrcProvider::Remember(Chat& chat, const UltraIMMessage& message) {
    for (const UltraIMMessage& h : chat.history) {
        if (h.messageId == message.messageId) return;
    }
    // In time order (history pages arrive after newer messages).
    auto at = chat.history.end();
    while (at != chat.history.begin() && (at - 1)->timestampMs > message.timestampMs) --at;
    chat.history.insert(at, message);
    if (chat.history.size() > kHistoryKept) chat.history.erase(chat.history.begin());
}

void IrcProvider::Receive(Chat& chat, const UltraIMMessage& message, bool created) {
    UltraIMMessage msg = message;
    msg.conversationId = chat.id;
    FillReply(chat, msg);
    for (const UltraIMMessage& h : chat.history) {
        if (h.messageId == msg.messageId) return;
    }
    if (chat.hidden) {
        chat.hidden = false;
        created = true;
    }
    Remember(chat, msg);
    if (!msg.isOwn && msg.type != UltraIMMessageType::SystemEvent) ++chat.unread;
    if (created) {
        QueueConversation(chat);
        if (chat.kind == ChatKind::Private) {
            Monitor(chat.name, true);
            SaveSession();
        }
    }
    UltraIMEvent e;
    e.type = UltraIMEventType::MessageReceive;
    e.conversationId = chat.id;
    e.message = msg;
    Queue(e);
    QueueConversation(chat);
}

void IrcProvider::SystemLine(Chat& chat, const std::string& text) {
    UltraIMMessage msg;
    msg.providerId = providerId_;
    msg.conversationId = chat.id;
    msg.messageId = NewMessageId();
    msg.type = UltraIMMessageType::SystemEvent;
    msg.text = text;
    msg.timestampMs = ServerNow();
    msg.deliveryStatus = UltraIMDeliveryStatus::Sent;
    const bool created = chat.kind == ChatKind::Server && chat.history.empty();
    Receive(chat, msg, created);
}

void IrcProvider::ServerNote(const std::string& text, const std::string& from) {
    (void)from;
    SystemLine(ServerChat(), text);
}

std::string IrcProvider::NewMessageId() {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "irc-%llx-%llu", static_cast<unsigned long long>(NowMs()),
                  static_cast<unsigned long long>(++counter_));
    return buffer;
}

int64_t IrcProvider::ServerNow() const {
    return NowMs() + clockOffsetMs_;
}

std::vector<UltraIMMessage> IrcProvider::MemoryPage(const Chat& chat, const std::string& beforeId, int limit) const {
    if (limit <= 0) limit = 50;
    size_t end = chat.history.size();
    if (!beforeId.empty()) {
        end = 0;
        for (size_t i = 0; i < chat.history.size(); ++i) {
            if (chat.history[i].messageId == beforeId) {
                end = i;
                break;
            }
        }
    }
    const size_t start = end > static_cast<size_t>(limit) ? end - static_cast<size_t>(limit) : 0;
    return std::vector<UltraIMMessage>(chat.history.begin() + static_cast<std::ptrdiff_t>(start),
                                       chat.history.begin() + static_cast<std::ptrdiff_t>(end));
}

// ============================================================================
// CONVERSATIONS
// ============================================================================

bool IrcProvider::IsChannelName(const std::string& name) const {
    return !name.empty() && chanTypes_.find(name[0]) != std::string::npos;
}

bool IrcProvider::IsMe(const std::string& nick) const {
    return !nick.empty() && Fold(nick) == Fold(nick_);
}

IrcProvider::Chat* IrcProvider::FindChat(const std::string& id) {
    auto it = chats_.find(id);
    return it != chats_.end() ? &it->second : nullptr;
}

IrcProvider::Chat& IrcProvider::ChatFor(const std::string& name, ChatKind kind, bool* created) {
    const std::string id = kind == ChatKind::Server ? std::string(kServerChatId) : Fold(name);
    auto it = chats_.find(id);
    if (created) *created = it == chats_.end();
    if (it != chats_.end()) return it->second;
    Chat chat;
    chat.id = id;
    chat.name = name;
    chat.kind = kind;
    if (kind == ChatKind::Private) chat.presence = PresenceOf(id);
    return chats_[id] = chat;
}

IrcProvider::Chat& IrcProvider::ServerChat() {
    return ChatFor(kServerChatId, ChatKind::Server);
}

UltraIMConversation IrcProvider::ConversationOf(const Chat& chat) const {
    UltraIMConversation c;
    c.conversationId = chat.id;
    c.providerId = providerId_;
    c.title = chat.kind == ChatKind::Server ? (network_.empty() ? address_.host : network_) : chat.name;
    c.isGroup = chat.kind == ChatKind::Channel;
    if (chat.kind == ChatKind::Channel) {
        const std::string mine = Fold(nick_);
        auto me = chat.members.find(mine);
        if (me != chat.members.end()) c.memberIds.push_back(me->second);
        for (const auto& [folded, written] : chat.members) {
            if (folded != mine) c.memberIds.push_back(written);
        }
        c.description = chat.topic;
    } else if (chat.kind == ChatKind::Private) {
        c.directContactId = chat.name;
        c.memberIds = {nick_, chat.name};
        c.presence = chat.presence;
    }
    c.unreadCount = chat.unread;
    if (!chat.history.empty()) {
        c.lastMessageSnippet = SnippetOf(chat.history.back().text);
        c.lastMessageTimeMs = chat.history.back().timestampMs;
    } else if (!chat.topic.empty()) {
        c.lastMessageSnippet = SnippetOf(chat.topic);
    }
    return c;
}

void IrcProvider::QueueConversation(const Chat& chat) {
    if (chat.hidden) return;
    UltraIMEvent e;
    e.type = UltraIMEventType::ConversationUpdate;
    e.conversationId = chat.id;
    e.conversation = ConversationOf(chat);
    Queue(e);
}

UltraIMPresence IrcProvider::PresenceOf(const std::string& folded) const {
    int online = -1;                            // unknown
    auto monitor = monitorOnline_.find(folded);
    if (monitor != monitorOnline_.end()) {
        online = monitor->second ? 1 : 0;
    } else {
        for (const auto& [id, chat] : chats_) {
            if (chat.kind == ChatKind::Channel && chat.joined && chat.members.count(folded)) {
                online = 1;
                break;
            }
        }
        if (online < 0 && quit_.count(folded)) online = 0;
    }
    if (online < 0) return UltraIMPresence::Unknown;
    if (online == 0) return UltraIMPresence::Offline;
    return away_.count(folded) ? UltraIMPresence::Away : UltraIMPresence::Online;
}

void IrcProvider::UpdatePresence(const std::string& folded) {
    Chat* chat = FindChat(folded);
    if (!chat || chat->kind != ChatKind::Private) return;
    const UltraIMPresence presence = PresenceOf(folded);
    if (presence == chat->presence) return;
    chat->presence = presence;
    if (chat->hidden) return;
    UltraIMEvent e;
    e.type = UltraIMEventType::PresenceChange;
    e.conversationId = chat->id;
    e.contactId = chat->name;
    e.presence = presence;
    Queue(e);
    QueueConversation(*chat);
}

void IrcProvider::Monitor(const std::string& nick, bool add) {
    if (monitorLimit_ <= 0 || !ready_) return;
    const std::string folded = Fold(nick);
    if (add) {
        if (monitored_.count(folded) || static_cast<int>(monitored_.size()) >= monitorLimit_) return;
        monitored_.insert(folded);
        SendLine("MONITOR + " + nick);
    } else if (monitored_.erase(folded)) {
        SendLine("MONITOR - " + nick);
        monitorOnline_.erase(folded);
    }
}

void IrcProvider::FetchConversations(UltraIMConversationsCallback onResult) {
    std::vector<UltraIMConversation> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, chat] : chats_) {
            if (chat.hidden) continue;
            if (chat.kind == ChatKind::Channel && !chat.joined && !chat.wanted && chat.history.empty()) continue;
            out.push_back(ConversationOf(chat));
        }
    }
    if (onResult) onResult(UltraIMResult::Ok(), out);
}

void IrcProvider::FetchContacts(UltraIMContactsCallback onResult) {
    std::vector<UltraIMContact> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, chat] : chats_) {
            if (chat.kind != ChatKind::Private || chat.hidden) continue;
            UltraIMContact c;
            c.contactId = chat.name;
            c.providerId = providerId_;
            c.displayName = chat.name;
            c.presence = chat.presence;
            if (chat.presence == UltraIMPresence::Away) c.statusText = "away";
            out.push_back(c);
        }
    }
    if (onResult) onResult(UltraIMResult::Ok(), out);
}

void IrcProvider::FetchMessages(const std::string& conversationId, const std::string& beforeMessageId, int limit,
                                UltraIMMessagesCallback onResult) {
    Enqueue([this, conversationId, beforeMessageId, limit, onResult]() {
        Chat* chat = FindChat(conversationId);
        if (!chat) {
            Post([onResult, conversationId]() {
                if (onResult)
                    onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "There is no conversation " + conversationId),
                             {});
            });
            return;
        }
        const int wanted = limit > 0 ? limit : 50;
        const bool fromServer = ready_ && HasChatHistory() && chat->kind != ChatKind::Server &&
                                (chat->kind == ChatKind::Private || chat->joined);
        if (!fromServer) {
            Post([onResult, page = MemoryPage(*chat, beforeMessageId, wanted)]() {
                if (onResult) onResult(UltraIMResult::Ok(), page);
            });
            return;
        }
        PendingHistory p;
        p.target = chat->id;
        p.beforeId = beforeMessageId;
        p.limit = historyLimit_ > 0 ? std::min(wanted, historyLimit_) : wanted;
        p.onResult = onResult;
        p.deadline = Clock::now() + std::chrono::milliseconds(timings_.requestTimeoutMs);
        std::map<std::string, std::string> tags;
        if (CapOn("labeled-response")) tags["label"] = p.label = NewLabel();
        std::vector<std::string> params;
        if (beforeMessageId.empty()) {
            params = {"LATEST", chat->name, "*", std::to_string(p.limit)};
        } else {
            std::string reference = "msgid=" + beforeMessageId;
            if (!serverIds_.count(beforeMessageId)) {
                for (const UltraIMMessage& h : chat->history) {
                    if (h.messageId == beforeMessageId) {
                        reference = "timestamp=" + IrcFormatServerTime(h.timestampMs);
                        break;
                    }
                }
            }
            params = {"BEFORE", chat->name, reference, std::to_string(p.limit)};
        }
        history_.push_back(p);
        SendCommand("CHATHISTORY", params, tags);
    });
}

void IrcProvider::SearchMessages(const UltraIMMessageSearch& search, UltraIMSearchCallback onResult) {
    if (search.conversationId.empty()) {
        if (onResult)
            onResult(UltraIMResult::Error(UltraIMResultCode::CapabilityMissing, "IRC searches one conversation at a time"),
                     UltraIMSearchPage());
        return;
    }
    UltraIM_SearchByFetching(
        [this](const std::string& conversationId, const std::string& before, int limit, UltraIMMessagesCallback cb) {
            FetchMessages(conversationId, before, limit, std::move(cb));
        },
        search, std::move(onResult));
}

void IrcProvider::StartJoin(const std::string& channel, const std::string& key, std::vector<std::string> invite,
                            UltraIMConversationCallback onResult) {
    const std::string id = Fold(channel);
    Chat* chat = FindChat(id);
    if (chat && chat->joined) {
        for (const std::string& nick : invite) SendCommand("INVITE", {nick, chat->name});
        Post([onResult, conversation = ConversationOf(*chat)]() {
            if (onResult) onResult(UltraIMResult::Ok(), conversation);
        });
        return;
    }
    PendingJoin p;
    p.channel = id;
    p.invite = std::move(invite);
    p.onResult = onResult;
    p.deadline = Clock::now() + std::chrono::milliseconds(timings_.requestTimeoutMs);
    joins_.push_back(p);
    if (key.empty()) SendCommand("JOIN", {channel});
    else SendCommand("JOIN", {channel, key});
}

void IrcProvider::AddContact(const std::string& address, UltraIMConversationCallback onResult) {
    Enqueue([this, address, onResult]() {
        const auto fail = [&](UltraIMResultCode code, const std::string& why) {
            Post([onResult, code, why]() {
                if (onResult) onResult(UltraIMResult::Error(code, why), UltraIMConversation());
            });
        };
        std::string a = Trim(address);
        const std::string lower = Lower(a);
        if (lower.rfind("irc://", 0) == 0 || lower.rfind("ircs://", 0) == 0) {
            // irc://server/#channel: the channel (on this server).
            const size_t slash = a.find('/', a.find("://") + 3);
            a = slash == std::string::npos ? std::string() : a.substr(slash + 1);
        } else if (lower.rfind("irc:", 0) == 0) {
            a = a.substr(4);
        }
        a = Trim(a);
        if (a.empty()) return fail(UltraIMResultCode::InvalidArgument, "Which channel (#name) or nickname?");
        if (!ready_) return fail(UltraIMResultCode::NotConnected, "Not connected to the IRC server");
        if (IsChannelName(a)) {
            const size_t space = a.find(' ');
            const std::string channel = space == std::string::npos ? a : a.substr(0, space);
            const std::string key = space == std::string::npos ? std::string() : Trim(a.substr(space + 1));
            if (channel.find_first_of(",\x07") != std::string::npos)
                return fail(UltraIMResultCode::InvalidArgument, "\"" + channel + "\" is not a channel name");
            StartJoin(channel, key, {}, onResult);
            return;
        }
        if (a[0] == '@') a.erase(0, 1);
        if (!IrcIsValidNick(a))
            return fail(UltraIMResultCode::InvalidArgument,
                        "\"" + a + "\" is neither a channel (#name) nor a nickname");
        if (IsMe(a)) return fail(UltraIMResultCode::InvalidArgument, "That is your own nickname");
        PendingWhois p;
        p.nick = Fold(a);
        p.written = a;
        p.onResult = onResult;
        p.deadline = Clock::now() + std::chrono::milliseconds(timings_.requestTimeoutMs);
        whois_.push_back(p);
        SendCommand("WHOIS", {a});
    });
}

void IrcProvider::CreateGroup(const std::string& title, const std::vector<std::string>& memberIds,
                              UltraIMConversationCallback onResult) {
    Enqueue([this, title, memberIds, onResult]() {
        std::string name;
        for (char c : title) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (std::isspace(u) || u < 0x20 || c == ',' || c == '\x07') continue;
            name += c;
        }
        if (!name.empty() && !IsChannelName(name)) name = "#" + name;
        if (name.size() < 2) {
            Post([onResult]() {
                if (onResult)
                    onResult(UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "The channel needs a name"),
                             UltraIMConversation());
            });
            return;
        }
        if (!ready_) {
            Post([onResult]() {
                if (onResult)
                    onResult(UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not connected to the IRC server"),
                             UltraIMConversation());
            });
            return;
        }
        std::vector<std::string> invite;
        for (std::string member : memberIds) {
            member = Trim(member);
            if (Lower(member).rfind("irc:", 0) == 0) member = member.substr(4);
            if (!member.empty() && member[0] == '@') member.erase(0, 1);
            if (IrcIsValidNick(member) && !IsMe(member)) invite.push_back(member);
        }
        StartJoin(name, std::string(), invite, onResult);
    });
}

void IrcProvider::LeaveConversation(const std::string& conversationId, UltraIMResultCallback onResult) {
    Enqueue([this, conversationId, onResult]() {
        Chat* chat = FindChat(conversationId);
        if (!chat) {
            Post([onResult, conversationId]() {
                if (onResult)
                    onResult(UltraIMResult::Error(UltraIMResultCode::NotFound, "There is no conversation " + conversationId));
            });
            return;
        }
        if (chat->kind == ChatKind::Channel && chat->joined && ready_) {
            chat->wanted = false;
            PendingPart p;
            p.channel = chat->id;
            p.onResult = onResult;
            p.deadline = Clock::now() + std::chrono::milliseconds(timings_.requestTimeoutMs);
            parts_.push_back(p);
            SendCommand("PART", {chat->name, "Leaving"});
            return;
        }
        UltraIMEvent removed;
        removed.type = UltraIMEventType::ConversationRemove;
        removed.conversationId = chat->id;
        Queue(removed);
        if (chat->kind == ChatKind::Channel) {
            chats_.erase(conversationId);
        } else {
            // Hidden: its history stays for when it comes back.
            chat->hidden = true;
            chat->unread = 0;
            if (chat->kind == ChatKind::Private) Monitor(chat->name, false);
        }
        SaveSession();
        Post([onResult]() {
            if (onResult) onResult(UltraIMResult::Ok());
        });
    });
}

// ============================================================================
// SENDING
// ============================================================================

size_t IrcProvider::PayloadLimit(const std::string& target, bool action) const {
    // What the server relays: ":nick!user@host PRIVMSG target :text\r\n" in 512 bytes.
    const size_t prefix = 1 + nick_.size() + 1 + (myUser_.empty() ? 10 : myUser_.size()) + 1 +
                          (myHost_.empty() ? 63 : myHost_.size()) + 1;
    const size_t command = std::string("PRIVMSG ").size() + target.size() + 2;
    size_t used = prefix + command + 2 + (action ? 9 : 0);
    if (used + 32 > 512) used = 512 - 32;
    return 512 - used;
}

UltraIMResult IrcProvider::SendText(const std::string& conversationId, const std::string& text,
                                    const UltraIMSendOptions& options) {
    if (Trim(text).empty()) return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Nothing to send");
    if (state_.load() != UltraIMConnectionState::Connected)
        return UltraIMResult::Error(UltraIMResultCode::NotConnected, "Not connected to the IRC server");
    const std::string clientId = options.clientMessageId.empty() ? UltraIM_NewClientMessageId() : options.clientMessageId;
    Enqueue([this, conversationId, text, options, clientId]() { SendOn(conversationId, text, options, clientId); });
    return UltraIMResult::Ok();
}

void IrcProvider::SendOn(const std::string& conversationId, const std::string& text, const UltraIMSendOptions& options,
                         const std::string& clientId) {
    UltraIMMessage message;
    message.providerId = providerId_;
    message.messageId = clientId;
    message.clientMessageId = clientId;
    message.conversationId = conversationId;
    message.senderId = nick_;
    message.senderName = nick_;
    message.type = UltraIMMessageType::Text;
    message.text = text;
    message.timestampMs = ServerNow();
    message.isOwn = true;
    message.deliveryStatus = UltraIMDeliveryStatus::Sending;
    message.replyTo.messageId = options.replyToMessageId;
    const auto fail = [&](const std::string& why) {
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageDelivered;
        e.conversationId = conversationId;
        e.message = message;
        e.message.deliveryStatus = UltraIMDeliveryStatus::Failed;
        e.deliveryStatus = UltraIMDeliveryStatus::Failed;
        e.errorText = why;
        Queue(e);
    };
    if (!ready_ || !conn_.IsOpen()) return fail("Not connected to the IRC server");
    Chat* chat = FindChat(conversationId);
    if (!chat) {
        if (conversationId == kServerChatId) chat = &ServerChat();
        else if (IrcIsValidNick(conversationId)) chat = &ChatFor(conversationId, ChatKind::Private);
        else return fail("There is no conversation " + conversationId);
    }
    const auto sent = [&](UltraIMMessage m) {
        m.deliveryStatus = UltraIMDeliveryStatus::Sent;
        Remember(*chat, m);
        UltraIMEvent delivered;
        delivered.type = UltraIMEventType::MessageDelivered;
        delivered.conversationId = chat->id;
        delivered.message = m;
        delivered.deliveryStatus = UltraIMDeliveryStatus::Sent;
        Queue(delivered);
        UltraIMEvent updated;
        updated.type = UltraIMEventType::MessageUpdate;
        updated.conversationId = chat->id;
        updated.message = m;
        Queue(updated);
        QueueConversation(*chat);
    };
    if (chat->kind == ChatKind::Server) {
        // A raw command.
        std::string line = Trim(text);
        if (!line.empty() && line[0] == '/') line.erase(0, 1);
        std::replace(line.begin(), line.end(), '\n', ' ');
        std::replace(line.begin(), line.end(), '\r', ' ');
        if (line.empty()) return fail("Nothing to send");
        if (!SendLine(line)) {
            fail("The connection to the IRC server was lost");
            ConnectionLost("The connection to the IRC server was lost");
            return;
        }
        return sent(message);
    }
    if (chat->kind == ChatKind::Channel && !chat->joined)
        return fail("You are not in " + chat->name + " now - join it again to write in it");
    if (chat->hidden) {
        chat->hidden = false;
        QueueConversation(*chat);
    }
    const bool action = text.rfind("/me ", 0) == 0;
    std::vector<std::string> logical;
    if (action) {
        std::string body = text.substr(4);
        std::replace(body.begin(), body.end(), '\n', ' ');
        std::replace(body.begin(), body.end(), '\r', ' ');
        logical.push_back(body);
        message.text = "* " + nick_ + " " + body;
    } else {
        for (std::string line : SplitOn(text, '\n')) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!Trim(line).empty()) logical.push_back(line);
        }
    }
    if (logical.empty()) return fail("Nothing to send");
    FillReply(*chat, message);
    const size_t limit = PayloadLimit(chat->name, action);
    const bool echo = CapOn("echo-message");
    const bool labels = echo && CapOn("labeled-response");
    const bool tags = CapOn("message-tags");
    Outgoing o;
    o.clientId = clientId;
    o.conversationId = chat->id;
    o.target = chat->id;
    o.message = message;
    o.message.conversationId = chat->id;
    bool first = true;
    for (const std::string& line : logical) {
        for (const std::string& part : IrcSplitText(line, limit)) {
            const std::string wire = action ? "\x01" "ACTION " + part + "\x01" : part;
            std::map<std::string, std::string> lineTags;
            std::string label;
            if (labels) lineTags["label"] = label = NewLabel();
            if (first && tags && !options.replyToMessageId.empty() && serverIds_.count(options.replyToMessageId))
                lineTags["+draft/reply"] = options.replyToMessageId;
            first = false;
            if (!SendCommand("PRIVMSG", {chat->name, wire}, lineTags)) {
                fail("The connection to the IRC server was lost");
                ConnectionLost("The connection to the IRC server was lost");
                return;
            }
            if (echo) o.lines.emplace_back(label, wire);
        }
    }
    typingSent_.erase(chat->id);
    if (echo) {
        o.deadline = Clock::now() + std::chrono::milliseconds(timings_.requestTimeoutMs);
        outgoing_.push_back(o);
        return;
    }
    // No echo from the server: sent once written.
    sent(o.message);
}

UltraIMResult IrcProvider::NoFiles() const {
    return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing,
                                "IRC has no file transfer: share a link to the file instead");
}

UltraIMResult IrcProvider::SendImage(const std::string&, const std::string&, const std::string&,
                                     const UltraIMSendOptions&) {
    return NoFiles();
}

UltraIMResult IrcProvider::SendVideo(const std::string&, const std::string&, const std::string&,
                                     const UltraIMSendOptions&) {
    return NoFiles();
}

UltraIMResult IrcProvider::SendVoiceNote(const std::string&, const std::string&, const UltraIMSendOptions&) {
    return NoFiles();
}

UltraIMResult IrcProvider::SendFile(const std::string&, const std::string&, const UltraIMSendOptions&) {
    return NoFiles();
}

UltraIMResult IrcProvider::MarkRead(const std::string& conversationId, const std::string& upToMessageId) {
    (void)upToMessageId;
    Enqueue([this, conversationId]() {
        Chat* chat = FindChat(conversationId);
        if (!chat || chat->unread == 0) return;
        chat->unread = 0;
        QueueConversation(*chat);
    });
    return UltraIMResult::Ok();
}

UltraIMResult IrcProvider::SetTyping(const std::string& conversationId, bool isTyping) {
    Enqueue([this, conversationId, isTyping]() {
        if (!ready_ || !CapOn("message-tags")) return;
        Chat* chat = FindChat(conversationId);
        if (!chat || chat->kind == ChatKind::Server || (chat->kind == ChatKind::Channel && !chat->joined)) return;
        const Clock::time_point now = Clock::now();
        auto last = typingSent_.find(chat->id);
        if (isTyping) {
            if (last != typingSent_.end() && now - last->second < std::chrono::milliseconds(kTypingRepeatMs)) return;
            typingSent_[chat->id] = now;
            SendCommand("TAGMSG", {chat->name}, {{"+typing", "active"}});
        } else if (last != typingSent_.end()) {
            typingSent_.erase(last);
            SendCommand("TAGMSG", {chat->name}, {{"+typing", "done"}});
        }
    });
    return UltraIMResult::Ok();
}

} // namespace UltraIMIrc
