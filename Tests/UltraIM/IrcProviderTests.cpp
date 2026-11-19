// Tests/UltraIM/IrcProviderTests.cpp
// IRC provider end to end against irc_fake_server.py (a small IRCv3 server,
// standard-library python, plain, TLS and an old server without IRCv3): the
// lines themselves (tags, formatting codes, case folding, splitting); TLS
// with a certificate that verifies (the test's own CA file) and one that does
// not (refused); SASL sign-in, a wrong password (AuthFailed), an anonymous
// sign-in, a nick in use (nick_, nick__); a channel joined and its members;
// messages both ways in a channel and privately; echo-message (Sent with the
// server's msgid); server-time; formatting codes taken out; Latin-1; ACTION;
// a long message split and arriving whole as several lines; a reply; typing;
// away; chathistory on join and FetchMessages paging (and the memory of a
// server without it); search; WHOIS NotFound; CreateGroup with INVITE and an
// invite-only channel refused; PART and a private chat hidden and back; no
// files; the stored session resuming and rejoining; a dropped connection
// reconnecting, rejoining and receiving what came meanwhile. With nothing but
// a web proxy set in the system, a server on another machine reached through
// it (a CONNECT tunnel).
// Skips without python3 and openssl, unless ULTRAIM_TEST_IRC_REQUIRED is set.
// Version: 0.1.1 - IRC through the system's web proxy
// Version: 0.1.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "IrcProvider.h"

#include "ConnectProxy.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetSocket.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace UltraIMIrc;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

namespace fs = std::filesystem;

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// ============================================================================
// THE LINES
// ============================================================================

void TestLines() {
    IrcMessage m;
    CHECK(IrcMessage::Parse("@time=2026-11-17T10:00:00.250Z;msgid=abc;+draft/reply=x\\sy\\:z :bob!b@host PRIVMSG #Chat :hello there",
                            m));
    CHECK(m.command == "PRIVMSG");
    CHECK(m.nick == "bob" && m.user == "b" && m.host == "host");
    CHECK(m.params.size() == 2 && m.Param(0) == "#Chat" && m.Param(1) == "hello there");
    CHECK(m.Tag("msgid") == "abc");
    CHECK(m.Tag("+draft/reply") == "x y;z");
    CHECK(IrcParseServerTime(m.Tag("time")) == 1794909600250LL);
    CHECK(IrcFormatServerTime(1794909600250LL) == "2026-11-17T10:00:00.250Z");
    CHECK(IrcMessage::Parse("PING :token", m) && m.command == "PING" && m.Param(0) == "token" && m.prefix.empty());
    CHECK(IrcMessage::Parse(":irc.example.org 001 alice :Welcome", m) && m.IsNumeric() && m.FromServer());
    CHECK(!IrcMessage::Parse("", m));
    CHECK(IrcFormatLine({{"+typing", "active"}}, "TAGMSG", {"#chat"}) == "@+typing=active TAGMSG #chat");
    CHECK(IrcFormatLine({}, "PRIVMSG", {"bob", "hi there"}) == "PRIVMSG bob :hi there");
    CHECK(IrcFormatLine({}, "PRIVMSG", {"bob", ":)"}) == "PRIVMSG bob ::)");
    CHECK(IrcFormatLine({{"label", "a b;c"}}, "PING", {"x"}) == "@label=a\\sb\\:c PING x");

    CHECK(IrcStripFormatting("\x02" "bold\x02 \x03" "04,12red\x03 \x1Ditalic\x1D \x1Funder\x0F \x16rev\x16 \x1Es\x1E \x11m") ==
          "bold red italic under rev s m");
    CHECK(IrcStripFormatting("\x03" "4,text") == ",text");
    CHECK(IrcStripFormatting("\x04" "FF00AA,00FF00hex") == "hex");
    CHECK(IrcStripFormatting("100\x03%") == "100%");

    CHECK(IrcDecodeText("caf\xc3\xa9") == "caf\xc3\xa9");
    CHECK(IrcDecodeText("caf\xe9") == "caf\xc3\xa9");
    CHECK(!IrcIsUtf8("\xc0\x80"));

    CHECK(IrcFold("Nick[Away]~", IrcCaseMapping::Rfc1459) == "nick{away}^");
    CHECK(IrcFold("Nick[Away]~", IrcCaseMapping::StrictRfc1459) == "nick{away}~");
    CHECK(IrcFold("Nick[Away]~", IrcCaseMapping::Ascii) == "nick[away]~");

    const auto parts = IrcSplitText("one two three four", 9);
    CHECK(parts.size() == 3 && parts[0] == "one two" && parts[1] == "three" && parts[2] == "four");
    std::string umlauts;
    for (int i = 0; i < 20; ++i) umlauts += "\xc3\xa4";
    for (const std::string& part : IrcSplitText(umlauts, 7)) {
        CHECK(part.size() <= 7 && IrcIsUtf8(part));
    }
    CHECK(IrcIsValidNick("alice_") && IrcIsValidNick("[x]") && !IrcIsValidNick("1abc") && !IrcIsValidNick("a b") &&
          !IrcIsValidNick("#chan"));

    IrcServerAddress a;
    std::string error;
    CHECK(IrcServerAddress::Parse("irc.libera.chat", a, error) && a.tls && a.port == 6697 && a.host == "irc.libera.chat");
    CHECK(IrcServerAddress::Parse("irc://irc.example.org", a, error) && !a.tls && a.port == 6667);
    CHECK(IrcServerAddress::Parse("ircs://irc.example.org:7000/", a, error) && a.tls && a.port == 7000);
    CHECK(IrcServerAddress::Parse("irc.example.org:+6697", a, error) && a.tls && a.port == 6697);
    CHECK(IrcServerAddress::Parse("[::1]:6697", a, error) && a.host == "::1" && a.IsLocal());
    CHECK(a.ToString() == "ircs://[::1]:6697");
    CHECK(!IrcServerAddress::Parse("https://example.org", a, error));
    CHECK(!IrcServerAddress::Parse("example.org:99999", a, error));
    CHECK(!IrcServerAddress::Parse("", a, error));
}

// ============================================================================
// END TO END
// ============================================================================

struct Events {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<UltraIMEvent> list;

    UltraIMEventSink Sink() {
        return [this](const UltraIMEvent& e) {
            std::lock_guard<std::mutex> lock(mutex);
            list.push_back(e);
            changed.notify_all();
        };
    }
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex);
        list.clear();
    }
    bool WaitFor(const std::function<bool(const UltraIMEvent&)>& match, UltraIMEvent* found = nullptr,
                 int seconds = 15) {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(seconds), [&]() {
            for (const auto& e : list) {
                if (match(e)) {
                    if (found) *found = e;
                    return true;
                }
            }
            return false;
        });
    }
    std::vector<UltraIMEvent> All(const std::function<bool(const UltraIMEvent&)>& match) {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<UltraIMEvent> out;
        for (const auto& e : list) {
            if (match(e)) out.push_back(e);
        }
        return out;
    }
};

bool IsReceived(const UltraIMEvent& e, const std::string& text) {
    return e.type == UltraIMEventType::MessageReceive && e.message.text == text;
}

bool WaitResult(const std::function<void(UltraIMResultCallback)>& start, UltraIMResult& out, int seconds = 30) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    start([&](const UltraIMResult& r) {
        std::lock_guard<std::mutex> lock(mutex);
        out = r;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock, std::chrono::seconds(seconds), [&]() { return done; });
}

bool WaitConversation(const std::function<void(UltraIMConversationCallback)>& start, UltraIMConversation& out,
                      UltraIMResult* result = nullptr) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false, ok = false;
    start([&](const UltraIMResult& r, const UltraIMConversation& c) {
        std::lock_guard<std::mutex> lock(mutex);
        out = c;
        ok = r.success;
        if (result) *result = r;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(30), [&]() { return done; });
    return ok;
}

std::vector<UltraIMMessage> Fetch(IrcProvider& p, const std::string& conversationId,
                                  const std::string& before = std::string(), int limit = 50) {
    std::vector<UltraIMMessage> out;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    p.FetchMessages(conversationId, before, limit, [&](const UltraIMResult&, const std::vector<UltraIMMessage>& m) {
        std::lock_guard<std::mutex> lock(mutex);
        out = m;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(30), [&]() { return done; });
    return out;
}

std::vector<UltraIMConversation> Conversations(IrcProvider& p) {
    std::vector<UltraIMConversation> out;
    p.FetchConversations([&](const UltraIMResult&, const std::vector<UltraIMConversation>& list) { out = list; });
    return out;
}

const UltraIMConversation* Find(const std::vector<UltraIMConversation>& list, const std::string& id) {
    for (const auto& c : list) {
        if (c.conversationId == id) return &c;
    }
    return nullptr;
}

UltraIMSearchPage Search(IrcProvider& p, const UltraIMMessageSearch& search) {
    UltraIMSearchPage out;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    p.SearchMessages(search, [&](const UltraIMResult&, const UltraIMSearchPage& page) {
        std::lock_guard<std::mutex> lock(mutex);
        out = page;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(30), [&]() { return done; });
    return out;
}

bool Contains(const std::vector<std::string>& list, const std::string& item) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

#if !defined(_WIN32)
struct FakeServer {
    pid_t pid = -1;
    int plain = 0, tls = 0, basic = 0;

    static int PortOf(const std::string& line, const std::string& key) {
        const size_t at = line.find("\"" + key + "\": ");
        return at == std::string::npos ? 0 : std::atoi(line.c_str() + at + key.size() + 4);
    }
    bool Start(const std::string& python, const std::string& cert, const std::string& key) {
        int out[2];
        if (pipe(out) != 0) return false;
        pid = fork();
        if (pid == 0) {
            dup2(out[1], 1);
            close(out[0]);
            close(out[1]);
            const std::string script = std::string(ULTRAIM_TEST_DIR) + "/irc_fake_server.py";
            execlp(python.c_str(), python.c_str(), script.c_str(), "--accounts", "alice:alice-pass,bob:bob-pass",
                   "--cert", cert.c_str(), "--key", key.c_str(), "--time-offset", "-3600", static_cast<char*>(nullptr));
            _exit(127);
        }
        close(out[1]);
        std::string line;
        char c = 0;
        while (read(out[0], &c, 1) == 1 && c != '\n') line += c;
        close(out[0]);
        plain = PortOf(line, "plain");
        tls = PortOf(line, "tls");
        basic = PortOf(line, "basic");
        if (!plain || !tls || !basic) {
            Stop();
            return false;
        }
        return true;
    }
    void Stop() {
        if (pid > 0) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
            pid = -1;
        }
    }
};

// The server drops the nick's connection, as a broken network would.
void Drop(int port, const std::string& nick) {
    UltraNetHandle h = UltraNet_TcpConnect("127.0.0.1", port);
    CHECK(h != UltraNetInvalidHandle);
    if (h == UltraNetInvalidHandle) return;
    const std::string line = "FAKEDROP " + nick + "\r\n";
    int sent = 0;
    UltraNet_TcpSend(h, std::vector<uint8_t>(line.begin(), line.end()), sent);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    UltraNet_SocketClose(h);
}

UltraIMCredentials Login(const std::string& server, const std::string& nick, const std::string& password = "") {
    UltraIMCredentials c;
    c.server = server;
    c.username = nick;
    c.password = password;
    return c;
}

struct Peer {
    Events events;
    std::unique_ptr<IrcProvider> p;
    UltraIMConfig config;

    Peer(const fs::path& base, const std::string& name, const IrcTimings& timings, const std::string& caFile) {
        p = std::make_unique<IrcProvider>();
        p->SetTimings(timings);
        if (!caFile.empty()) {
            UltraNetTlsOptions tls;
            tls.caBundlePath = caFile;
            p->SetTlsOptions(tls);
        }
        config.dataDirectory = UltraCanvas::PathToUtf8(base / name);
        CHECK(p->Initialize(config, events.Sink()));
    }
    UltraIMResult SignIn(const UltraIMCredentials& c) {
        UltraIMResult r;
        CHECK(WaitResult([&](UltraIMResultCallback cb) { p->SignIn(c, cb); }, r));
        return r;
    }
};

void TestEndToEnd(const FakeServer& server, const fs::path& base, const std::string& caFile) {
    IrcTimings fast;
    fast.reconnectMinMs = 500;
    fast.readSliceMs = 50;
    fast.requestTimeoutMs = 8000;
    const std::string tlsServer = "ircs://localhost:" + std::to_string(server.tls);
    const std::string plainServer = "irc://127.0.0.1:" + std::to_string(server.plain);
    const std::string basicServer = "irc://127.0.0.1:" + std::to_string(server.basic);
    UltraIMResult r;
    UltraIMEvent e;

    // ---- TLS: a certificate that does not verify is refused -------------------
    {
        Peer stranger(base, "stranger", fast, std::string());
        r = stranger.SignIn(Login(tlsServer, "stranger"));
        CHECK(!r.success && r.code == UltraIMResultCode::NetworkError);
        CHECK(r.message.find("certificate") != std::string::npos);
        CHECK(stranger.p->GetConnectionState() == UltraIMConnectionState::Error);
        CHECK(!stranger.p->HasStoredSession());
        // A password is never sent in the clear to another machine.
        r = stranger.SignIn(Login("irc://irc.example.org", "stranger", "secret"));
        CHECK(!r.success && r.code == UltraIMResultCode::InvalidArgument);
        r = stranger.SignIn(UltraIMCredentials());
        CHECK(!r.success && r.code == UltraIMResultCode::InvalidArgument);
        r = stranger.SignIn(Login(plainServer, "9lives"));
        CHECK(!r.success && r.code == UltraIMResultCode::InvalidArgument);
        stranger.p->Shutdown();
    }

    // ---- SASL over TLS: a wrong password, then the right one --------------------
    auto alice = std::make_unique<Peer>(base, "alice", fast, caFile);
    CHECK(alice->p->GetProviderId() == "irc" && alice->p->GetDisplayName() == "IRC" && alice->p->GetVersion() == "0.1.0");
    CHECK(alice->p->GetLoginType() == UltraIMLoginType::UsernamePassword);
    CHECK(!alice->p->HasStoredSession());
    r = alice->SignIn(Login(tlsServer, "alice", "not-it"));
    CHECK(!r.success && r.code == UltraIMResultCode::AuthFailed);
    CHECK(alice->p->GetConnectionState() == UltraIMConnectionState::Error);
    CHECK(!alice->p->HasStoredSession());
    r = alice->SignIn(Login(tlsServer, "alice", "alice-pass"));
    CHECK(r.success);
    CHECK(r.message.empty());
    CHECK(alice->p->GetConnectionState() == UltraIMConnectionState::Connected);
    CHECK(alice->p->GetLocalUserId() == "alice");
    CHECK(alice->p->HasStoredSession());
    CHECK(alice->p->GetServerAddress() == "ircs://localhost:" + std::to_string(server.tls));
    const auto caps = alice->p->GetEnabledCaps();
    for (const char* cap : {"sasl", "server-time", "message-tags", "echo-message", "batch", "away-notify",
                            "labeled-response", "draft/chathistory", "multi-prefix"})
        CHECK(Contains(caps, cap));
    CHECK(UltraIM_HasCapability(alice->p->GetCapabilities(), UltraIMCapabilityFlags::OfflineMessages));
    CHECK(UltraIM_HasCapability(alice->p->GetCapabilities(), UltraIMCapabilityFlags::TypingIndicator));
    CHECK(!UltraIM_HasCapability(alice->p->GetCapabilities(), UltraIMCapabilityFlags::FileTransfer));
    // The server's conversation, with its notice.
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageReceive && x.conversationId == "*server" &&
               x.message.text.find("Looking up your hostname") != std::string::npos;
    }));

    // Bob: plain, to this machine, with SASL; Carol: an old server, anonymous.
    auto bob = std::make_unique<Peer>(base, "bob", fast, caFile);
    CHECK(bob->SignIn(Login(plainServer, "bob", "bob-pass")).success);
    IrcTimings pinging = fast;
    pinging.pingAfterMs = 300;
    pinging.pingTimeoutMs = 3000;
    auto carol = std::make_unique<Peer>(base, "carol", pinging, std::string());
    CHECK(carol->SignIn(Login(basicServer, "carol")).success);
    CHECK(carol->p->GetEnabledCaps().empty());
    CHECK(!UltraIM_HasCapability(carol->p->GetCapabilities(), UltraIMCapabilityFlags::OfflineMessages));

    // ---- a nick in use: nick_, then nick__ --------------------------------------
    {
        Peer second(base, "alice2", fast, std::string());
        r = second.SignIn(Login(plainServer, "alice"));
        CHECK(r.success);
        CHECK(second.p->GetLocalUserId() == "alice_");
        CHECK(r.message.find("alice_") != std::string::npos);
        CHECK(second.events.WaitFor([](const UltraIMEvent& x) {
            return x.type == UltraIMEventType::MessageReceive && x.conversationId == "*server" &&
                   x.message.type == UltraIMMessageType::SystemEvent && x.message.text.find("in use") != std::string::npos;
        }));
        Peer third(base, "alice3", fast, std::string());
        r = third.SignIn(Login(plainServer, "Alice"));
        CHECK(r.success && third.p->GetLocalUserId() == "Alice__");
        Peer fourth(base, "alice4", fast, std::string());
        r = fourth.SignIn(Login(plainServer, "alice"));
        CHECK(!r.success);                          // alice, alice_ and alice__ taken
        third.p->Shutdown();
        second.p->Shutdown();
        fourth.p->Shutdown();
    }

    // ---- a channel joined; its members ------------------------------------------
    const std::string chan = "#ultracanvas";
    UltraIMConversation c;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->p->AddContact("#UltraCanvas", cb); }, c));
    CHECK(c.conversationId == chan && c.title == "#UltraCanvas" && c.isGroup);
    CHECK(c.memberIds.size() == 1 && c.memberIds[0] == "alice");
    CHECK(c.ownerId.empty());
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { bob->p->AddContact("irc:#ultracanvas", cb); }, c));
    CHECK(c.conversationId == chan && c.memberIds.size() == 2 && c.memberIds[0] == "bob" && Contains(c.memberIds, "alice"));
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { carol->p->AddContact("#ULTRACANVAS", cb); }, c));
    CHECK(c.conversationId == chan && c.memberIds.size() == 3);
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == chan &&
               x.conversation.memberIds.size() == 3;
    }));
    CHECK(Find(Conversations(*alice->p), chan) != nullptr);
    // A raw command in the server's conversation: the topic.
    CHECK(alice->p->SendText("*server", "/TOPIC #UltraCanvas :Welcome here", UltraIMSendOptions()));
    CHECK(bob->events.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == chan &&
               x.conversation.description == "Welcome here";
    }));

    // ---- messages in the channel; echo-message; server-time -------------------
    UltraIMSendOptions first;
    first.clientMessageId = "client-1";
    CHECK(alice->p->SendText(chan, "Hello channel", first));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelivered && x.message.clientMessageId == "client-1";
    }, &e));
    CHECK(e.deliveryStatus == UltraIMDeliveryStatus::Sent);
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.clientMessageId == "client-1";
    }, &e));
    const std::string helloId = e.message.messageId;
    CHECK(helloId.rfind("fake", 0) == 0);           // the server's msgid
    CHECK(e.message.isOwn && e.message.text == "Hello channel" && e.message.senderId == "alice");
    CHECK(bob->events.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hello channel"); }, &e));
    CHECK(e.conversationId == chan && e.message.senderId == "alice" && !e.message.isOwn);
    CHECK(e.message.messageId == helloId);
    // The server's clock is an hour behind: its time, not the moment it came.
    CHECK(std::llabs(e.message.timestampMs - (NowMs() - 3600000)) < 120000);
    CHECK(carol->events.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hello channel"); }, &e));
    CHECK(std::llabs(e.message.timestampMs - NowMs()) < 120000);    // no server-time there
    CHECK(e.message.messageId.rfind("irc-", 0) == 0);               // no msgid there
    CHECK(bob->events.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == chan &&
               x.conversation.unreadCount == 1 && x.conversation.lastMessageSnippet == "Hello channel";
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(alice->events.All([](const UltraIMEvent& x) { return IsReceived(x, "Hello channel"); }).empty());

    // Formatting codes; ACTION; Latin-1 from an old client.
    CHECK(bob->p->SendText(chan, "\x02" "bold\x02 \x03" "04,12red\x03 \x1Ditalic\x1D\x0F done", UltraIMSendOptions()));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "bold red italic done"); }));
    UltraIMSendOptions wave;
    wave.clientMessageId = "wave";
    CHECK(bob->p->SendText(chan, "/me waves", wave));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "* bob waves"); }, &e));
    CHECK(e.message.senderId == "bob");
    CHECK(bob->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelivered && x.message.clientMessageId == "wave" &&
               x.message.text == "* bob waves";
    }));
    UltraIMSendOptions latin;
    latin.clientMessageId = "latin";
    CHECK(carol->p->SendText(chan, "caf\xe9", latin));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "caf\xc3\xa9"); }));
    // Without echo-message: Sent once written, under the client's id.
    CHECK(carol->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelivered && x.message.clientMessageId == "latin" &&
               x.deliveryStatus == UltraIMDeliveryStatus::Sent && x.message.messageId == "latin";
    }));

    // ---- a long message: several lines, whole on the other side ---------------
    std::string longText;
    for (int i = 0; i < 300; ++i) longText += (i ? " " : "") + std::string("word") + std::to_string(1000 + i);
    UltraIMSendOptions longOptions;
    longOptions.clientMessageId = "long";
    CHECK(alice->p->SendText(chan, longText, longOptions));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelivered && x.message.clientMessageId == "long" &&
               x.deliveryStatus == UltraIMDeliveryStatus::Sent;
    }, &e));
    CHECK(e.message.text == longText);
    CHECK(bob->events.WaitFor([](const UltraIMEvent& x) {
        const std::string& t = x.message.text;
        return x.type == UltraIMEventType::MessageReceive && t.size() >= 8 && t.compare(t.size() - 8, 8, "word1299") == 0;
    }));
    {
        const auto parts = bob->events.All([](const UltraIMEvent& x) {
            return x.type == UltraIMEventType::MessageReceive && x.message.text.rfind("word1", 0) == 0;
        });
        std::string joined;
        for (const auto& part : parts) joined += (joined.empty() ? "" : " ") + part.message.text;
        CHECK(parts.size() >= 3);
        CHECK(joined == longText);
    }
    std::string umlauts;
    for (int i = 0; i < 400; ++i) umlauts += "\xc3\xa4";      // 800 bytes, no space
    CHECK(alice->p->SendText(chan, umlauts, UltraIMSendOptions()));
    CHECK(bob->events.WaitFor([&](const UltraIMEvent& x) {
        if (x.type != UltraIMEventType::MessageReceive || x.message.text.rfind("\xc3\xa4", 0) != 0) return false;
        return true;
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    {
        const auto parts = bob->events.All([](const UltraIMEvent& x) {
            return x.type == UltraIMEventType::MessageReceive && x.message.text.rfind("\xc3\xa4", 0) == 0;
        });
        std::string joined;
        bool utf8 = true;
        for (const auto& part : parts) {
            joined += part.message.text;
            utf8 = utf8 && IrcIsUtf8(part.message.text);
        }
        CHECK(parts.size() == 2);
        CHECK(utf8);
        CHECK(joined == umlauts);
    }

    // ---- a reply; typing --------------------------------------------------------
    UltraIMSendOptions reply;
    reply.replyToMessageId = helloId;
    CHECK(bob->p->SendText(chan, "Hi back", reply));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hi back"); }, &e));
    CHECK(e.message.replyTo.messageId == helloId);
    CHECK(e.message.replyTo.senderId == "alice" && e.message.replyTo.textSnippet == "Hello channel");
    CHECK(bob->p->SetTyping(chan, true));
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::TypingChange && x.conversationId == chan && x.contactId == "bob" && x.isTyping;
    }));
    CHECK(bob->p->SetTyping(chan, false));
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::TypingChange && x.conversationId == chan && x.contactId == "bob" && !x.isTyping;
    }));

    // ---- private messages: WHOIS, both ways, presence ---------------------------
    UltraIMResult missing;
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { alice->p->AddContact("nobody", cb); }, c, &missing));
    CHECK(missing.code == UltraIMResultCode::NotFound);
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { alice->p->AddContact("not a nick!", cb); }, c, &missing));
    CHECK(missing.code == UltraIMResultCode::InvalidArgument);
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->p->AddContact("Bob", cb); }, c));
    CHECK(c.conversationId == "bob" && c.directContactId == "bob" && !c.isGroup);
    CHECK(c.presence == UltraIMPresence::Online);
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->p->AddContact("irc:BOB", cb); }, c));
    CHECK(c.conversationId == "bob");
    CHECK(alice->p->SendText("bob", "Psst, bob", UltraIMSendOptions()));
    CHECK(bob->events.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Psst, bob"); }, &e));
    CHECK(e.conversationId == "alice" && e.message.senderId == "alice");
    CHECK(bob->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == "alice" && !x.conversation.isGroup &&
               x.conversation.directContactId == "alice";
    }));
    CHECK(bob->p->SendText("alice", "Yes?", UltraIMSendOptions()));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Yes?") && x.conversationId == "bob"; }));
    // Away and back (away-notify).
    CHECK(bob->p->SendText("*server", "AWAY :lunch", UltraIMSendOptions()));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::PresenceChange && x.contactId == "bob" && x.presence == UltraIMPresence::Away;
    }));
    CHECK(bob->p->SendText("*server", "AWAY", UltraIMSendOptions()));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::PresenceChange && x.contactId == "bob" && x.presence == UltraIMPresence::Online &&
               x.conversationId == "bob";
    }));
    {
        std::vector<UltraIMContact> contacts;
        alice->p->FetchContacts([&](const UltraIMResult&, const std::vector<UltraIMContact>& list) { contacts = list; });
        CHECK(contacts.size() == 1 && contacts[0].contactId == "bob");
    }

    // ---- history: chathistory on join, pages; a server without it: memory ------
    auto dave = std::make_unique<Peer>(base, "dave", fast, std::string());
    CHECK(dave->SignIn(Login(plainServer, "dave")).success);
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { dave->p->AddContact("#UltraCanvas", cb); }, c));
    CHECK(dave->events.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == chan &&
               x.conversation.lastMessageSnippet == "Hi back";
    }));
    CHECK(dave->events.All([](const UltraIMEvent& x) { return x.type == UltraIMEventType::MessageReceive && x.conversationId != "*server"; }).empty());
    auto page = Fetch(*dave->p, chan);
    CHECK(page.size() >= 8);
    CHECK(!page.empty() && page.back().text == "Hi back");
    CHECK(std::any_of(page.begin(), page.end(), [&](const UltraIMMessage& m) {
        return m.messageId == helloId && m.text == "Hello channel" && m.senderId == "alice";
    }));
    CHECK(std::is_sorted(page.begin(), page.end(),
                         [](const UltraIMMessage& a, const UltraIMMessage& b) { return a.timestampMs < b.timestampMs; }));
    auto latest = Fetch(*dave->p, chan, std::string(), 3);
    CHECK(latest.size() == 3 && latest.back().text == "Hi back");
    if (latest.size() == 3) {
        auto before = Fetch(*dave->p, chan, latest.front().messageId, 3);
        CHECK(before.size() == 3);
        if (before.size() == 3 && page.size() >= 6) {
            CHECK(before.back().messageId == page[page.size() - 4].messageId);
            CHECK(before.front().messageId == page[page.size() - 6].messageId);
        }
    }
    // Alice's own, back from the server's history.
    auto mine = Fetch(*alice->p, "bob");
    CHECK(mine.size() == 2 && mine.front().text == "Psst, bob" && mine.front().isOwn && mine.back().text == "Yes?");
    // Carol's server keeps no history: what this session saw.
    auto seen = Fetch(*carol->p, chan);
    CHECK(std::any_of(seen.begin(), seen.end(), [](const UltraIMMessage& m) { return m.text == "Hello channel"; }));
    CHECK(std::none_of(seen.begin(), seen.end(), [](const UltraIMMessage& m) { return m.text == "Welcome here"; }));
    UltraIMMessageSearch search;
    search.conversationId = chan;
    search.text = "channel";
    auto found = Search(*alice->p, search);
    CHECK(!found.messages.empty() && found.messages.front().text == "Hello channel");
    CHECK(carol->p->GetConnectionState() == UltraIMConnectionState::Connected);   // pinged, answered
    CHECK(carol->events.All([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConnectionStateChange && x.connectionState == UltraIMConnectionState::Reconnecting;
    }).empty());

    // ---- CreateGroup with an INVITE; invite-only refused; PART ---------------------
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->p->CreateGroup("Team Room", {"bob"}, cb); }, c));
    CHECK(c.conversationId == "#teamroom" && c.title == "#TeamRoom" && c.isGroup && c.memberIds.size() == 1);
    CHECK(bob->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageReceive && x.conversationId == "*server" &&
               x.message.text.find("alice invites you to #TeamRoom") != std::string::npos;
    }));
    CHECK(alice->p->SendText("*server", "MODE #TeamRoom +i", UltraIMSendOptions()));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { carol->p->AddContact("#TeamRoom", cb); }, c, &missing));
    CHECK(missing.code == UltraIMResultCode::PermissionDenied && missing.message.find("invite-only") != std::string::npos);
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { bob->p->AddContact("#TeamRoom", cb); }, c));
    CHECK(c.memberIds.size() == 2);
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == "#teamroom" &&
               x.conversation.memberIds.size() == 2;
    }));
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { carol->p->AddContact("bad,name", cb); }, c, &missing));
    alice->events.Clear();
    CHECK(WaitResult([&](UltraIMResultCallback cb) { bob->p->LeaveConversation("#teamroom", cb); }, r));
    CHECK(r.success);
    CHECK(bob->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationRemove && x.conversationId == "#teamroom";
    }));
    CHECK(!Find(Conversations(*bob->p), "#teamroom"));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == "#teamroom" &&
               x.conversation.memberIds.size() == 1;
    }));
    // A private chat hidden, and back with the next message.
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->p->LeaveConversation("bob", cb); }, r));
    CHECK(r.success && !Find(Conversations(*alice->p), "bob"));
    CHECK(bob->p->SendText("alice", "Still there?", UltraIMSendOptions()));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Still there?") && x.conversationId == "bob"; }));
    CHECK(Find(Conversations(*alice->p), "bob") != nullptr);

    // ---- what IRC does not have ------------------------------------------------
    r = alice->p->SendFile(chan, "/tmp/x.txt", UltraIMSendOptions());
    CHECK(!r.success && r.code == UltraIMResultCode::CapabilityMissing && r.message.find("file") != std::string::npos);
    r = alice->p->SendImage(chan, "/tmp/x.png", "", UltraIMSendOptions());
    CHECK(r.code == UltraIMResultCode::CapabilityMissing);
    CHECK(alice->p->EditMessage(chan, helloId, "x").code == UltraIMResultCode::CapabilityMissing);
    CHECK(alice->p->DeleteMessage(chan, helloId, UltraIMDeleteScope::ForEveryone).code == UltraIMResultCode::CapabilityMissing);

    // ---- the stored session: resumed, the channels joined again ------------------
    alice->p->Shutdown();
    alice = std::make_unique<Peer>(base, "alice", fast, caFile);
    CHECK(alice->p->HasStoredSession());
    bob->events.Clear();
    CHECK(alice->SignIn(UltraIMCredentials()).success);
    CHECK(alice->p->GetLocalUserId() == "alice");
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == chan &&
               Contains(x.conversation.memberIds, "bob") && Contains(x.conversation.memberIds, "alice");
    }));
    // #TeamRoom is invite-only now, and empty: not joined again, and dropped from the session.
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationRemove && x.conversationId == "#teamroom";
    }));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageReceive && x.conversationId == "*server" &&
               x.message.text.find("#TeamRoom is invite-only") != std::string::npos;
    }));
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == "bob" &&
               x.conversation.directContactId == "bob";
    }));
    CHECK(bob->events.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == chan &&
               Contains(x.conversation.memberIds, "alice");
    }));
    CHECK(Contains(alice->p->GetEnabledCaps(), "sasl"));

    // ---- a dropped connection: reconnected, rejoined, what came meanwhile -------
    alice->events.Clear();
    Drop(server.plain, "alice");
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConnectionStateChange && x.connectionState == UltraIMConnectionState::Reconnecting;
    }));
    CHECK(bob->p->SendText(chan, "While you were away", UltraIMSendOptions()));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConnectionStateChange && x.connectionState == UltraIMConnectionState::Connected;
    }, nullptr, 20));
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == chan &&
               Contains(x.conversation.memberIds, "bob") && Contains(x.conversation.memberIds, "alice");
    }));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "While you were away"); }));
    CHECK(bob->p->SendText(chan, "Welcome back", UltraIMSendOptions()));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Welcome back"); }));
    CHECK(alice->p->GetConnectionState() == UltraIMConnectionState::Connected);

    // ---- signing out: the others see the nick leave ------------------------------
    bob->p->SignOut();
    CHECK(bob->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConnectionStateChange && x.connectionState == UltraIMConnectionState::Disconnected;
    }));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::PresenceChange && x.contactId == "bob" && x.presence == UltraIMPresence::Offline;
    }));
    CHECK(bob->p->HasStoredSession());
    CHECK(!bob->p->SendText(chan, "late", UltraIMSendOptions()));

    dave->p->Shutdown();
    carol->p->Shutdown();
    bob->p->Shutdown();
    alice->p->Shutdown();
}
#endif

#if defined(__linux__)
// A server on another machine, with nothing but a web proxy set in the
// system: IRC goes through that (a CONNECT tunnel), as the other networks do,
// and no longer directly past it.
void TestThroughTheSystemsWebProxy(const FakeServer& server, const fs::path& base) {
    ConnectProxy proxy;
    CHECK(proxy.Start());
    SystemWebProxy system(proxy.port);
    IrcTimings fast;
    fast.reconnectMinMs = 500;
    fast.readSliceMs = 50;
    fast.requestTimeoutMs = 8000;
    // A name that exists nowhere: only the proxy can reach it (it carries
    // the connection to the fake server here).
    const std::string target = "irc.proxied.test:" + std::to_string(server.plain);
    Peer carol(base, "carol-proxied", fast, std::string());
    const UltraIMResult r = carol.SignIn(Login("irc://" + target, "carol"));
    CHECK(r.success);
    CHECK(carol.p->GetConnectionState() == UltraIMConnectionState::Connected);
    const std::vector<std::string> targets = proxy.Targets();
    CHECK(targets.size() == 1 && targets.front() == target);
    carol.p->Shutdown();
}
#endif

} // namespace

int main() {
    UltraNet_Initialize();
    TestLines();
#if defined(_WIN32)
    std::printf("IrcProviderTests: end to end skipped on Windows\n");
#else
    const fs::path base = fs::temp_directory_path() / ("ultraim-irc-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    const std::string cert = UltraCanvas::PathToUtf8(base / "cert.pem");
    const std::string key = UltraCanvas::PathToUtf8(base / "key.pem");
    // The server's certificate, which the test trusts as its CA file (localhost, 127.0.0.1).
    const std::string make =
        "openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj \"/CN=localhost\" "
        "-addext \"subjectAltName=DNS:localhost,IP:127.0.0.1\" -addext \"extendedKeyUsage=serverAuth\" "
        "-addext \"keyUsage=digitalSignature,keyEncipherment\" -keyout \"" + key + "\" -out \"" + cert +
        "\" > /dev/null 2>&1";
    FakeServer server;
    const bool ready = std::system("command -v python3 > /dev/null 2>&1") == 0 && std::system(make.c_str()) == 0 &&
                       server.Start("python3", cert, key);
    if (!ready) {
        const bool required = !UltraCanvas::GetEnvUtf8("ULTRAIM_TEST_IRC_REQUIRED").empty();
        std::printf("IrcProviderTests: %s (no python3 or openssl)\n", required ? "FAIL" : "SKIP");
        fs::remove_all(base, ec);
        UltraNet_Shutdown();
        return required ? 1 : 0;
    }
    std::printf("  server plain %d, tls %d, basic %d\n", server.plain, server.tls, server.basic);
    TestEndToEnd(server, base, cert);
#if defined(__linux__)
    TestThroughTheSystemsWebProxy(server, base);
#endif
    server.Stop();
    fs::remove_all(base, ec);
#endif
    UltraNet_Shutdown();
    std::printf("IrcProviderTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
