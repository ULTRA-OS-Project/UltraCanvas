// Tests/UltraIM/XmppProviderTests.cpp
// XMPP provider end to end against xmpp_fake_server.py (a small XMPP server
// over WebSocket, standard-library python): signing in - a wrong password
// refused, SCRAM-SHA-256, SCRAM-SHA-1 when that is all the server offers,
// PLAIN (nothing kept), the WebSocket found from the server's host-meta,
// the kept SCRAM keys signing in with no password and refused once the
// password changed; a contact added and the subscription approved both
// ways, presence; texts both ways, a message kept for someone offline,
// carbons from a second client of the same account, receipts and read
// markers, typing, a correction, a retraction, a reply; the archive paged
// and searched; a file and a picture uploaded (progress) and fetched back
// byte for byte; a room made, joined by the invited, messages both ways,
// its history, left, joined again by its address; a chat hidden and back
// with its next message; a dropped connection reconnecting.
// Skips without python3, unless ULTRAIM_TEST_XMPP_REQUIRED is set.
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "XmppProvider.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetWebSocket.h"

#include <algorithm>
#include <atomic>
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

using namespace UltraIMXmpp;

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
    bool WaitFor(const std::function<bool(const UltraIMEvent&)>& match, UltraIMEvent* found = nullptr, int seconds = 15) {
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
    int Count(const std::function<bool(const UltraIMEvent&)>& match) {
        std::lock_guard<std::mutex> lock(mutex);
        return static_cast<int>(std::count_if(list.begin(), list.end(), match));
    }
};

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

std::vector<UltraIMMessage> Fetch(XmppProvider& p, const std::string& conversationId,
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

std::vector<UltraIMConversation> Conversations(XmppProvider& p) {
    std::vector<UltraIMConversation> out;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    p.FetchConversations([&](const UltraIMResult&, const std::vector<UltraIMConversation>& list) {
        std::lock_guard<std::mutex> lock(mutex);
        out = list;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(30), [&]() { return done; });
    return out;
}

std::vector<UltraIMContact> Contacts(XmppProvider& p) {
    std::vector<UltraIMContact> out;
    p.FetchContacts([&](const UltraIMResult&, const std::vector<UltraIMContact>& list) { out = list; });
    return out;
}

const UltraIMConversation* Find(const std::vector<UltraIMConversation>& list, const std::string& id) {
    for (const auto& c : list) {
        if (c.conversationId == id) return &c;
    }
    return nullptr;
}

UltraIMSearchPage Search(XmppProvider& p, const UltraIMMessageSearch& search, UltraIMResult* result = nullptr) {
    UltraIMSearchPage out;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    p.SearchMessages(search, [&](const UltraIMResult& r, const UltraIMSearchPage& page) {
        std::lock_guard<std::mutex> lock(mutex);
        out = page;
        if (result) *result = r;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(30), [&]() { return done; });
    return out;
}

bool WaitAttachment(const std::function<void(UltraIMAttachmentCallback)>& start, UltraIMAttachment& out) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false, ok = false;
    start([&](const UltraIMResult& r, const UltraIMAttachment& a) {
        std::lock_guard<std::mutex> lock(mutex);
        out = a;
        ok = r.success;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(30), [&]() { return done; });
    return ok;
}

std::vector<uint8_t> WritePattern(const fs::path& path, size_t size, uint8_t seed) {
    std::vector<uint8_t> bytes(size);
    for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>((i * 31 + seed) ^ (i >> 3));
    std::FILE* f = UltraCanvas::OpenFileUtf8(UltraCanvas::PathToUtf8(path), "wb");
    if (f) {
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    }
    return bytes;
}

std::vector<uint8_t> ReadBytes(const std::string& path) {
    std::vector<uint8_t> out;
    std::FILE* f = UltraCanvas::OpenFileUtf8(path, "rb");
    if (!f) return out;
    uint8_t buffer[4096];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) out.insert(out.end(), buffer, buffer + n);
    std::fclose(f);
    return out;
}

bool IsReceived(const UltraIMEvent& e, const std::string& text) {
    return e.type == UltraIMEventType::MessageReceive && e.message.text == text;
}

UltraIMCredentials Password(const std::string& server, const std::string& user) {
    UltraIMCredentials c;
    c.server = server;
    c.username = user + "@example.org";
    c.password = user + "-pass";
    return c;
}

UltraIMSendOptions Client(const std::string& id, const std::string& replyTo = std::string()) {
    UltraIMSendOptions o;
    o.clientMessageId = id;
    o.replyToMessageId = replyTo;
    return o;
}

void Post(const std::string& url, const std::string& path, const std::string& json) {
    UltraNetHttpRequest post;
    post.url = url + path;
    post.method = UltraNetHttpMethod::Post;
    post.headers.Set("Content-Type", "application/json");
    post.body.assign(json.begin(), json.end());
    UltraNetResponse answer;
    UltraNet_HttpRequest(post, answer);
    CHECK(answer.statusCode == 200);
}

#if !defined(_WIN32)
struct FakeServer {
    pid_t pid = -1;
    std::string url;

    bool Start(const std::string& python) {
        const std::string probe = python + " -c 'import http.server' >/dev/null 2>&1";
        if (std::system(probe.c_str()) != 0) return false;
        int out[2];
        if (pipe(out) != 0) return false;
        pid = fork();
        if (pid == 0) {
            dup2(out[1], 1);
            close(out[0]);
            close(out[1]);
            const std::string script = std::string(ULTRAIM_TEST_DIR) + "/xmpp_fake_server.py";
            execlp(python.c_str(), python.c_str(), script.c_str(), "--users", "alice,bob,carol,dave",
                   static_cast<char*>(nullptr));
            _exit(127);
        }
        close(out[1]);
        std::string line;
        char c = 0;
        while (read(out[0], &c, 1) == 1 && c != '\n') line += c;
        close(out[0]);
        const size_t at = line.find("\"url\": \"");
        if (at == std::string::npos) {
            Stop();
            return false;
        }
        const size_t begin = at + 8;
        url = line.substr(begin, line.find('"', begin) - begin);
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

struct Client_ {
    std::unique_ptr<XmppProvider> p;
    Events events;
    UltraIMConfig config;
};

std::unique_ptr<Client_> NewClient(const fs::path& dir) {
    auto c = std::make_unique<Client_>();
    c->p = std::make_unique<XmppProvider>();
    XmppTimings fast;
    fast.requestTimeoutMs = 10000;
    fast.reconnectFirstMs = 300;
    fast.reconnectMaxMs = 2000;
    c->p->SetTimings(fast);
    c->config.dataDirectory = UltraCanvas::PathToUtf8(dir);
    c->config.cacheDirectory = UltraCanvas::PathToUtf8(dir / "cache");
    CHECK(c->p->Initialize(c->config, c->events.Sink()));
    return c;
}

UltraIMResult SignIn(XmppProvider& p, const UltraIMCredentials& credentials) {
    UltraIMResult r;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { p.SignIn(credentials, cb); }, r));
    return r;
}

// What the application (or another provider) installed before the XMPP
// streams: its sockets' events must still reach it.
struct Foreign {
    std::mutex mutex;
    std::condition_variable changed;
    std::atomic<UltraNetHandle> opened{UltraNetInvalidHandle};
    std::string text;
    bool closed = false;
};
Foreign g_foreign;

void InstallForeignCallbacks() {
    UltraNetWebSocketCallbacks bag;
    bag.onOpen = [](UltraNetHandle h) { g_foreign.opened = h; };
    bag.onText = [](UltraNetHandle, const std::string& text) {
        std::lock_guard<std::mutex> lock(g_foreign.mutex);
        g_foreign.text += text;
        g_foreign.changed.notify_all();
    };
    bag.onClose = [](UltraNetHandle, int, const std::string&) {
        std::lock_guard<std::mutex> lock(g_foreign.mutex);
        g_foreign.closed = true;
        g_foreign.changed.notify_all();
    };
    UltraNet_WebSocketSetCallbacks(bag);
}

void TestForeignSocket(const std::string& ws) {
    const UltraNetHandle h = UltraNet_WebSocketConnect(ws);
    CHECK(h != UltraNetInvalidHandle);
    CHECK(g_foreign.opened.load() == h);
    UltraNet_WebSocketSendText(h, "<open xmlns=\"urn:ietf:params:xml:ns:xmpp-framing\" to=\"example.org\" version=\"1.0\"/>");
    {
        std::unique_lock<std::mutex> lock(g_foreign.mutex);
        CHECK(g_foreign.changed.wait_for(lock, std::chrono::seconds(10),
                                         [] { return g_foreign.text.find("mechanisms") != std::string::npos; }));
    }
    UltraNet_WebSocketClose(h);
}

void TestEndToEnd(const std::string& url, const fs::path& base) {
    const std::string ws = "ws" + url.substr(4) + "/xmpp-websocket";
    InstallForeignCallbacks();
    const std::string aliceJid = "alice@example.org", bobJid = "bob@example.org", carolJid = "carol@example.org";
    auto alice = NewClient(base / "alice");
    auto bob = NewClient(base / "bob");
    auto carol = NewClient(base / "carol");
    XmppProvider& a = *alice->p;
    XmppProvider& b = *bob->p;
    CHECK(a.GetProviderId() == "xmpp" && a.GetDisplayName() == "XMPP" && a.GetVersion() == "0.1.0");
    CHECK(a.GetLoginType() == UltraIMLoginType::UsernamePassword);
    CHECK(UltraIM_HasCapability(a.GetCapabilities(), UltraIMCapabilityFlags::TypingIndicator));
    CHECK(UltraIM_HasCapability(a.GetCapabilities(), UltraIMCapabilityFlags::ReadReceipts));
    CHECK(UltraIM_HasCapability(a.GetCapabilities(), UltraIMCapabilityFlags::GroupChats));
    CHECK(!UltraIM_HasCapability(a.GetCapabilities(), UltraIMCapabilityFlags::E2EEncryption));
    CHECK(!a.HasStoredSession());

    // ---- signing in ---------------------------------------------------------------------
    UltraIMCredentials wrong = Password(ws, "alice");
    wrong.password = "not-it";
    UltraIMResult r = SignIn(a, wrong);
    CHECK(!r.success && r.code == UltraIMResultCode::AuthFailed);
    CHECK(a.GetConnectionState() == UltraIMConnectionState::Error);
    CHECK(!a.HasStoredSession());
    UltraIMCredentials notJid = Password(ws, "alice");
    notJid.username = "alice";
    r = SignIn(a, notJid);
    CHECK(!r.success && r.code == UltraIMResultCode::InvalidArgument);
    // No TLS to another computer: refused before anything is sent.
    UltraIMCredentials plainWs = Password("ws://chat.example.net/xmpp-websocket", "alice");
    r = SignIn(a, plainWs);
    CHECK(!r.success && r.code == UltraIMResultCode::NetworkError);
    // The WebSocket found from the server's host-meta (XEP-0156).
    r = SignIn(a, Password(url, "alice"));
    CHECK(r.success);
    CHECK(a.GetConnectionState() == UltraIMConnectionState::Connected);
    CHECK(a.GetWebSocketUrl() == ws);
    CHECK(a.GetLocalUserId() == aliceJid);
    CHECK(a.GetBoundJid().rfind(aliceJid + "/ChatCube-", 0) == 0);
    CHECK(a.HasStoredSession());
    r = SignIn(b, Password(ws, "bob"));
    CHECK(r.success);
    // The streams' router is in now: another socket's events still go where they went.
    TestForeignSocket(ws);
    // SCRAM-SHA-1 when that is all there is; its keys kept too.
    Post(url, "/_test/mechanisms", "{\"mechanisms\": [\"SCRAM-SHA-1\"]}");
    r = SignIn(*carol->p, Password(ws, "carol"));
    CHECK(r.success);
    CHECK(carol->p->HasStoredSession());
    // PLAIN alone: signed in, nothing kept, the password asked for again.
    Post(url, "/_test/mechanisms", "{\"mechanisms\": [\"PLAIN\"]}");
    auto dave = NewClient(base / "dave");
    r = SignIn(*dave->p, Password(ws, "dave"));
    CHECK(r.success);
    CHECK(!dave->p->HasStoredSession());
    dave->p->SignOut();
    r = SignIn(*dave->p, UltraIMCredentials());
    CHECK(!r.success && r.code == UltraIMResultCode::InvalidArgument);
    dave->p->Shutdown();
    Post(url, "/_test/mechanisms", "{\"mechanisms\": [\"SCRAM-SHA-256\", \"SCRAM-SHA-1\", \"PLAIN\"]}");
    // The kept keys sign in on the next start: no password.
    alice->p->Shutdown();
    alice = NewClient(base / "alice");
    XmppProvider& a2 = *alice->p;
    CHECK(a2.HasStoredSession());
    r = SignIn(a2, UltraIMCredentials());
    CHECK(r.success);
    CHECK(a2.GetLocalUserId() == aliceJid && a2.GetWebSocketUrl() == ws);
    XmppProvider& A = a2;

    // ---- a contact: the subscription both ways, presence ---------------------------------------
    UltraIMConversation chat;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { A.AddContact("xmpp:Bob@Example.org", cb); }, chat));
    CHECK(chat.conversationId == bobJid && !chat.isGroup && chat.directContactId == bobJid);
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::PresenceChange && e.contactId == bobJid && e.presence == UltraIMPresence::Online;
    }));
    CHECK(bob->events.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::PresenceChange && e.contactId == aliceJid && e.presence == UltraIMPresence::Online;
    }));
    CHECK(bob->events.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == aliceJid;
    }));
    {
        auto contacts = Contacts(b);
        bool found = false;
        for (const auto& c : contacts) found = found || (c.contactId == aliceJid && c.presence == UltraIMPresence::Online);
        CHECK(found);
    }
    UltraIMResult bad;
    UltraIMConversation none;
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { A.AddContact("not an address", cb); }, none, &bad));
    CHECK(bad.code == UltraIMResultCode::InvalidArgument);

    // ---- texts both ways, receipts, read markers --------------------------------------------------
    CHECK(A.SendText(bobJid, "hello bob", Client("c-1")));
    UltraIMEvent got;
    CHECK(alice->events.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageDelivered && e.message.clientMessageId == "c-1" &&
               e.deliveryStatus == UltraIMDeliveryStatus::Sent;
    }));
    CHECK(bob->events.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "hello bob"); }, &got));
    CHECK(got.conversationId == aliceJid && got.message.senderId == aliceJid && got.message.messageId == "c-1");
    CHECK(!got.message.isOwn);
    CHECK(alice->events.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageDelivered && e.message.messageId == "c-1" &&
               e.deliveryStatus == UltraIMDeliveryStatus::Delivered;
    }));
    CHECK(b.MarkRead(aliceJid, ""));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageDelivered && e.message.messageId == "c-1" &&
               e.deliveryStatus == UltraIMDeliveryStatus::Read;
    }));
    CHECK(b.SendText(aliceJid, "hello alice", Client("b-1")));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "hello alice"); }, &got));
    CHECK(got.conversationId == bobJid && got.message.senderId == bobJid);

    // Text that is XML's own: escaped on the way, the same on arrival.
    const std::string awkward = "<b>bold</b> & \"quoted\" 'single' </message> \xC3\xBC \xF0\x9F\x98\x80\nline two";
    CHECK(A.SendText(bobJid, awkward, Client("c-xml")));
    CHECK(bob->events.WaitFor([&](const UltraIMEvent& e) { return IsReceived(e, awkward); }));

    // ---- typing ---------------------------------------------------------------------------------------
    CHECK(b.SetTyping(aliceJid, true));
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::TypingChange && e.contactId == bobJid && e.isTyping;
    }));
    CHECK(b.SetTyping(aliceJid, false));
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::TypingChange && e.contactId == bobJid && !e.isTyping;
    }));

    // ---- a correction, a reply, a retraction ----------------------------------------------------------
    CHECK(A.EditMessage(bobJid, "c-1", "hello bob!"));
    CHECK(bob->events.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageUpdate && e.message.messageId == "c-1" && e.message.isEdited &&
               e.message.text == "hello bob!";
    }));
    CHECK(b.EditMessage(aliceJid, "c-1", "not mine").code == UltraIMResultCode::PermissionDenied);
    CHECK(b.EditMessage(aliceJid, "no-such-id", "x").code == UltraIMResultCode::NotFound);
    CHECK(b.SendText(aliceJid, "a reply", Client("b-2", "c-1")));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "a reply"); }, &got));
    CHECK(got.message.replyTo.messageId == "c-1");
    CHECK(got.message.replyTo.textSnippet.find("hello bob!") != std::string::npos);
    CHECK(A.SendText(bobJid, "oops", Client("c-oops")));
    CHECK(bob->events.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "oops"); }));
    CHECK(A.GetDeleteChoices(bobJid, "c-oops").forEveryone);
    CHECK(!b.GetDeleteChoices(aliceJid, "c-oops").forEveryone);
    CHECK(b.DeleteMessage(aliceJid, "c-oops", UltraIMDeleteScope::ForEveryone).code == UltraIMResultCode::PermissionDenied);
    CHECK(A.DeleteMessage(bobJid, "c-oops", UltraIMDeleteScope::ForMe).code == UltraIMResultCode::CapabilityMissing);
    CHECK(A.DeleteMessage(bobJid, "c-oops", UltraIMDeleteScope::ForEveryone));
    CHECK(bob->events.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageDelete && e.message.messageId == "c-oops";
    }));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageDelete && e.message.messageId == "c-oops";
    }));
    // The retraction did not show as a message of its own.
    CHECK(bob->events.Count([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageReceive && e.message.text.find("retract") != std::string::npos;
    }) == 0);

    // ---- carbons: a second client of alice's ------------------------------------------------------------
    auto second = NewClient(base / "alice-second");
    r = SignIn(*second->p, Password(ws, "alice"));
    CHECK(r.success);
    CHECK(second->p->GetBoundJid() != A.GetBoundJid());
    UltraIMConversation viaSecond;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { second->p->AddContact(bobJid, cb); }, viaSecond));
    CHECK(second->p->SendText(bobJid, "from the other client", Client("s-1")));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "from the other client"); }, &got));
    CHECK(got.message.isOwn && got.conversationId == bobJid && got.message.messageId == "s-1");
    CHECK(bob->events.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "from the other client"); }));
    CHECK(b.SendText(aliceJid, "to both of you", Client("b-3")));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "to both of you"); }));
    CHECK(second->events.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "to both of you"); }));
    second->p->Shutdown();

    // ---- kept while offline ---------------------------------------------------------------------------------
    carol->p->Shutdown();
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { A.AddContact(carolJid, cb); }, chat));
    CHECK(A.SendText(carolJid, "while you were away", Client("c-away")));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageDelivered && e.message.clientMessageId == "c-away";
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    carol = NewClient(base / "carol");
    XmppProvider& C = *carol->p;
    r = SignIn(C, UltraIMCredentials());     // the SHA-1 keys kept before
    CHECK(r.success);
    CHECK(carol->events.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "while you were away"); }, &got));
    CHECK(got.conversationId == aliceJid && got.message.timestampMs > 0 && got.message.timestampMs <= UltraIMXmpp::NowMs());
    // The subscription asked for while away is approved on arrival.
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::PresenceChange && e.contactId == carolJid && e.presence == UltraIMPresence::Online;
    }));

    // ---- the archive: pages, corrections and retractions applied, search ---------------------------------
    {
        const auto last = Fetch(A, bobJid, std::string(), 3);
        CHECK(last.size() == 3);
        if (last.size() == 3) {
            CHECK(last[2].text == "to both of you");
            CHECK(last[0].timestampMs <= last[2].timestampMs);
            const auto older = Fetch(A, bobJid, last[0].messageId, 50);
            bool corrected = false, retracted = false, firstBob = false;
            for (const auto& m : older) {
                if (m.messageId == "c-1") corrected = m.text == "hello bob!" && m.isEdited && m.isOwn;
                if (m.text == "oops") retracted = true;
                if (m.text == "hello alice") firstBob = !m.isOwn && m.senderId == bobJid;
            }
            CHECK(corrected);
            CHECK(!retracted);
            CHECK(firstBob);
            CHECK(!older.empty() && older.back().timestampMs <= last[0].timestampMs);
        }
        UltraIMMessageSearch search;
        search.conversationId = bobJid;
        search.text = "REPLY";
        const UltraIMSearchPage page = Search(A, search);
        CHECK(page.messages.size() == 1 && !page.messages.empty() && page.messages[0].text == "a reply");
        search.conversationId.clear();
        UltraIMResult sr;
        Search(A, search, &sr);
        CHECK(sr.code == UltraIMResultCode::CapabilityMissing);
    }

    // ---- files: uploaded with progress, fetched back ------------------------------------------------------------
    {
        const fs::path dir = base / "files";
        fs::create_directories(dir);
        const auto fileBytes = WritePattern(dir / "report.pdf", 300 * 1024, 7);
        CHECK(A.SendFile(bobJid, UltraCanvas::PathToUtf8(dir / "report.pdf"), Client("c-file")));
        CHECK(alice->events.WaitFor([](const UltraIMEvent& e) {
            return e.type == UltraIMEventType::MessageDelivered && e.message.clientMessageId == "c-file" &&
                   e.deliveryStatus == UltraIMDeliveryStatus::Sent;
        }, nullptr, 30));
        CHECK(alice->events.Count([](const UltraIMEvent& e) {
            return e.type == UltraIMEventType::TransferProgress && e.isUpload && e.message.clientMessageId == "c-file";
        }) >= 1);
        CHECK(alice->events.WaitFor([](const UltraIMEvent& e) {
            return e.type == UltraIMEventType::TransferProgress && e.isUpload && e.message.clientMessageId == "c-file" &&
                   e.transferredBytes == e.totalBytes && e.totalBytes == 300 * 1024;
        }));
        CHECK(bob->events.WaitFor([](const UltraIMEvent& e) {
            return e.type == UltraIMEventType::MessageReceive && e.message.messageId == "c-file";
        }, &got));
        CHECK(got.message.type == UltraIMMessageType::File && got.message.attachment.fileName == "report.pdf");
        CHECK(!got.message.attachment.isDownloaded);
        UltraIMAttachment attachment;
        CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) { b.DownloadAttachment(aliceJid, "c-file", cb); }, attachment));
        CHECK(attachment.isDownloaded && ReadBytes(attachment.localPath) == fileBytes);

        const auto pictureBytes = WritePattern(dir / "Photo one.png", 40 * 1024, 3);
        CHECK(A.SendImage(bobJid, UltraCanvas::PathToUtf8(dir / "Photo one.png"), "a picture", Client("c-pic")));
        CHECK(bob->events.WaitFor([](const UltraIMEvent& e) {
            return e.type == UltraIMEventType::MessageReceive && e.message.messageId == "c-pic";
        }, &got, 30));
        CHECK(got.message.type == UltraIMMessageType::Image && got.message.text == "a picture");
        CHECK(got.message.attachment.fileName == "Photo one.png");
        // A picture comes by itself.
        CHECK(bob->events.WaitFor([](const UltraIMEvent& e) {
            return e.type == UltraIMEventType::MessageUpdate && e.message.messageId == "c-pic" && e.message.attachment.isDownloaded;
        }, &got));
        CHECK(ReadBytes(got.message.attachment.localPath) == pictureBytes);
    }

    // ---- a room ------------------------------------------------------------------------------------------------------
    UltraIMConversation room;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { A.CreateGroup("Team Room", {bobJid, carolJid}, cb); }, room));
    CHECK(room.isGroup && room.title == "Team Room");
    CHECK(room.conversationId.find("@conference.example.org") != std::string::npos);
    CHECK(room.ownerId == aliceJid);
    CHECK(std::find(room.memberIds.begin(), room.memberIds.end(), bobJid) != room.memberIds.end());
    const std::string roomId = room.conversationId;
    // The invited join by themselves.
    for (Client_* c : {bob.get(), carol.get()}) {
        CHECK(c->events.WaitFor([&](const UltraIMEvent& e) {
            return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == roomId &&
                   e.conversation.isGroup && e.conversation.title == "Team Room";
        }));
    }
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == roomId &&
               e.conversation.memberNames.count(carolJid) && e.conversation.memberNames.count(bobJid);
    }));
    CHECK(A.SendText(roomId, "hi team", Client("r-1")));
    for (Client_* c : {bob.get(), carol.get()}) {
        CHECK(c->events.WaitFor([&](const UltraIMEvent& e) { return IsReceived(e, "hi team") && e.conversationId == roomId; }, &got));
        CHECK(got.message.senderId == aliceJid && got.message.senderName == "alice" && !got.message.isOwn);
    }
    CHECK(b.SendText(roomId, "hi alice", Client("r-2")));
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& e) { return IsReceived(e, "hi alice") && e.conversationId == roomId; }, &got));
    CHECK(got.message.senderId == bobJid);
    // Our own message's echo from the room is not a second message.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(alice->events.Count([](const UltraIMEvent& e) { return IsReceived(e, "hi team"); }) == 0);
    {
        const auto history = Fetch(C, roomId);
        bool fromAlice = false, fromBob = false;
        for (const auto& m : history) {
            fromAlice = fromAlice || (m.text == "hi team" && m.senderId == aliceJid);
            fromBob = fromBob || (m.text == "hi alice" && m.senderId == bobJid);
        }
        CHECK(fromAlice && fromBob);
    }
    // Leaving; and back by the room's address.
    UltraIMResult left;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { C.LeaveConversation(roomId, cb); }, left));
    CHECK(left.success);
    CHECK(carol->events.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationRemove && e.conversationId == roomId;
    }));
    CHECK(Find(Conversations(C), roomId) == nullptr);
    UltraIMConversation back;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { C.AddContact(roomId, cb); }, back));
    CHECK(back.isGroup && back.conversationId == roomId && back.title == "Team Room");
    CHECK(C.SendText(roomId, "carol is back", Client("r-3")));
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& e) { return IsReceived(e, "carol is back"); }));

    // ---- a chat hidden, back with its next message --------------------------------------------------------------------
    CHECK(WaitResult([&](UltraIMResultCallback cb) { A.LeaveConversation(bobJid, cb); }, left));
    CHECK(left.success);
    CHECK(Find(Conversations(A), bobJid) == nullptr);
    CHECK(b.SendText(aliceJid, "still there?", Client("b-4")));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "still there?"); }));
    CHECK(Find(Conversations(A), bobJid) != nullptr);

    // ---- a dropped connection reconnects --------------------------------------------------------------------------------
    alice->events.Clear();
    Post(url, "/_test/drop", "{\"user\": \"alice\"}");
    CHECK(alice->events.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConnectionStateChange && e.connectionState == UltraIMConnectionState::Reconnecting;
    }));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConnectionStateChange && e.connectionState == UltraIMConnectionState::Connected;
    }, nullptr, 20));
    CHECK(A.GetConnectionState() == UltraIMConnectionState::Connected);
    // The room joined again (its messages arrive), chats go on.
    CHECK(alice->events.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == roomId && !e.conversation.memberIds.empty();
    }));
    CHECK(b.SendText(aliceJid, "after the drop", Client("b-5")));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "after the drop"); }));
    CHECK(C.SendText(roomId, "room after the drop", Client("r-4")));
    CHECK(alice->events.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "room after the drop"); }));
    CHECK(alice->events.Count([](const UltraIMEvent& e) { return IsReceived(e, "hi alice"); }) == 0);

    // ---- a changed password: the kept keys no longer sign in --------------------------------------------------------------
    carol->p->Shutdown();
    Post(url, "/_test/password", "{\"user\": \"carol\", \"password\": \"new-pass\"}");
    carol = NewClient(base / "carol");
    CHECK(carol->p->HasStoredSession());
    r = SignIn(*carol->p, UltraIMCredentials());
    CHECK(!r.success && r.code == UltraIMResultCode::AuthFailed);
    CHECK(!carol->p->HasStoredSession());
    UltraIMCredentials renewed = Password(ws, "carol");
    renewed.password = "new-pass";
    r = SignIn(*carol->p, renewed);
    CHECK(r.success);

    carol->p->Shutdown();
    bob->p->Shutdown();
    alice->p->Shutdown();
    CHECK(A.GetConnectionState() == UltraIMConnectionState::Disconnected);
}
#endif

} // namespace

int main() {
    UltraNet_Initialize();
#if defined(_WIN32)
    std::printf("XmppProviderTests: skipped on Windows\n");
#else
    const fs::path base = fs::temp_directory_path() / ("ultraim-xmpp-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    FakeServer server;
    if (!server.Start("python3")) {
        const bool required = !UltraCanvas::GetEnvUtf8("ULTRAIM_TEST_XMPP_REQUIRED").empty();
        std::printf("XmppProviderTests: %s (no python3)\n", required ? "FAIL" : "SKIP");
        return required ? 1 : 0;
    }
    std::printf("  server %s\n", server.url.c_str());
    TestEndToEnd(server.url, base);
    server.Stop();
    fs::remove_all(base, ec);
#endif
    UltraNet_Shutdown();
    std::printf("XmppProviderTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
