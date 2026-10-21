// Tests/UltraIM/DirectProviderTests.cpp
// ChatCube Direct: the envelope, the store, and two providers end to end
// through a local relay (P2P, read receipts, offline queue, groups)
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "DirectCrypto.h"
#include "DirectProvider.h"
#include "DirectStore.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraRTC/UltraRTCCore.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace UltraIMDirect;

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

fs::path TempDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / ("ultraim-direct-" + name + "-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// ============================================================================
// UNITS
// ============================================================================

void TestEnvelope() {
    Identity alice, bob, eve;
    std::string error;
    CHECK(CreateIdentity(alice, error) && CreateIdentity(bob, error) && CreateIdentity(eve, error));
    CHECK(alice.userId.size() == 32 && alice.userId != bob.userId);

    KeyRing ring;
    Envelope envelope;
    CHECK(ring.Seal(alice, bob.signPublic, bob.userId, "dm", "id-1", 1234, "{\"type\":\"text\",\"text\":\"hi\"}",
                    envelope, error));
    CHECK(envelope.ciphertext.find("hi") == std::string::npos);

    Envelope parsed;
    CHECK(Envelope::FromJson(envelope.ToJson(), parsed));
    KeyRing bobRing;
    std::string body;
    CHECK(bobRing.Open(bob, alice.signPublic, parsed, body, error));
    CHECK(body == "{\"type\":\"text\",\"text\":\"hi\"}");

    // The header is authenticated: a changed timestamp or sender fails.
    Envelope tampered = parsed;
    tampered.timestampMs = 9999;
    CHECK(!bobRing.Open(bob, alice.signPublic, tampered, body, error));
    // Not for Eve, and Eve cannot pass as Alice.
    KeyRing eveRing;
    CHECK(!eveRing.Open(eve, alice.signPublic, parsed, body, error));
    CHECK(!bobRing.Open(bob, eve.signPublic, parsed, body, error));
    // A key that is not the recipient's is refused before sealing.
    CHECK(!ring.Seal(alice, eve.signPublic, bob.userId, "dm", "id-2", 1, "{}", envelope, error));

    // The identity survives the round trip through its secret.
    Identity restored;
    CHECK(IdentityFromSecret(alice.signSecret, restored, error) && restored.userId == alice.userId);
    const std::string sig = Sign(alice, SignalMessage(alice.userId, bob.userId, "s", "offer", "x"));
    CHECK(Verify(alice.signPublic, SignalMessage(alice.userId, bob.userId, "s", "offer", "x"), sig));
    CHECK(!Verify(alice.signPublic, SignalMessage(alice.userId, bob.userId, "s", "answer", "x"), sig));
}

void TestStore() {
    Store store;
    std::string error;
    const fs::path dir = TempDir("store");
    CHECK(store.Open(UltraCanvas::PathToUtf8(dir / "s.sqlite3"), "direct", error));
    UltraIMConversation c;
    c.conversationId = "bob";
    c.title = "Bob";
    store.UpsertConversation(c);

    UltraIMMessage m;
    m.conversationId = "bob";
    m.clientMessageId = "m1";
    m.text = "one";
    m.timestampMs = 10;
    m.isOwn = true;
    m.deliveryStatus = UltraIMDeliveryStatus::Sending;
    CHECK(store.AddMessage(m));
    CHECK(!store.AddMessage(m));                    // the same id twice is one message
    CHECK(store.UpdateStatus("bob", "m1", UltraIMDeliveryStatus::Read));
    CHECK(!store.UpdateStatus("bob", "m1", UltraIMDeliveryStatus::Sent));     // never downgraded
    CHECK(!store.UpdateStatus("bob", "m1", UltraIMDeliveryStatus::Failed));   // a delivered message cannot fail
    UltraIMMessage back;
    CHECK(store.GetMessage("bob", "m1", back) && back.deliveryStatus == UltraIMDeliveryStatus::Read);

    for (int i = 2; i <= 5; ++i) {
        UltraIMMessage n = m;
        n.clientMessageId = "m" + std::to_string(i);
        n.isOwn = false;
        n.timestampMs = 10 + i;
        store.AddMessage(n);
    }
    auto page = store.Messages("bob", "", 2);
    CHECK(page.size() == 2 && page[0].clientMessageId == "m4" && page[1].clientMessageId == "m5");
    auto older = store.Messages("bob", "m4", 10);
    CHECK(older.size() == 3 && older[0].clientMessageId == "m1");
    UltraIMConversation after;
    CHECK(store.GetConversation("bob", after) && after.unreadCount == 4 && after.lastMessageTimeMs == 15);
    CHECK(store.UnreadIncoming("bob").size() == 4);
    CHECK(store.UnreadIncoming("bob").empty());     // reported once
    store.Close();
}

// ============================================================================
// END TO END
// ============================================================================

// Every event a provider raised, for the test to wait on.
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
    bool WaitFor(const std::function<bool(const UltraIMEvent&)>& match, int seconds = 30) {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(seconds), [&]() {
            for (const auto& e : list) {
                if (match(e)) return true;
            }
            return false;
        });
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

bool WaitConversation(const std::function<void(UltraIMConversationCallback)>& start, UltraIMConversation& out) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false, ok = false;
    start([&](const UltraIMResult& r, const UltraIMConversation& c) {
        std::lock_guard<std::mutex> lock(mutex);
        out = c;
        ok = r.success;
        if (!r) std::fprintf(stderr, "  conversation call failed: %s\n", r.message.c_str());
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(30), [&]() { return done; });
    return ok;
}

#if !defined(_WIN32)
struct Relay {
    pid_t pid = -1;
    std::string url;

    bool Start(const fs::path& dataDir) {
        if (std::system("python3 -c 'import fastapi, uvicorn, cryptography' >/dev/null 2>&1") != 0) return false;
        const int port = 20000 + static_cast<int>(::getpid() % 20000);
        url = "http://127.0.0.1:" + std::to_string(port);
        pid = fork();
        if (pid == 0) {
            if (chdir(ULTRAIM_RELAY_DIR) != 0) _exit(127);
            setenv("CHATCUBE_RELAY_PORT", std::to_string(port).c_str(), 1);
            setenv("CHATCUBE_RELAY_DB", UltraCanvas::PathToUtf8(dataDir / "relay.sqlite3").c_str(), 1);
            execlp("python3", "python3", "-m", "chatcube_relay", static_cast<char*>(nullptr));
            _exit(127);
        }
        for (int i = 0; i < 100; ++i) {
            UltraNetResponse response;
            UltraNetHttpOptions options;
            options.timeoutMs = 1000;
            if (UltraNet_HttpGet(url + "/healthz", response, options) && response.statusCode == 200) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        Stop();
        return false;
    }
    void Stop() {
        if (pid > 0) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
            pid = -1;
        }
    }
};
#endif

bool IsDelivered(const UltraIMEvent& e, const std::string& clientId, UltraIMDeliveryStatus status) {
    return e.type == UltraIMEventType::MessageDelivered && e.message.clientMessageId == clientId &&
           e.deliveryStatus == status;
}

bool IsReceived(const UltraIMEvent& e, const std::string& text) {
    return e.type == UltraIMEventType::MessageReceive && e.message.text == text;
}

void TestEndToEnd() {
#if defined(_WIN32)
    std::printf("DirectProviderTests: end-to-end part skipped on Windows\n");
#else
    const fs::path base = TempDir("e2e");
    UltraNet_Initialize();
    Relay relay;
    if (!relay.Start(base)) {
        // CI sets the variable: there a skip would read as a pass.
        const bool required = !UltraCanvas::GetEnvUtf8("ULTRAIM_TEST_RELAY_REQUIRED").empty();
        std::printf("DirectProviderTests: %s end to end (no python3 with fastapi, uvicorn, cryptography)\n",
                    required ? "FAIL" : "SKIP");
        CHECK(!required);
        return;
    }
    CHECK(UltraRtc_Initialize());

    ProviderTimings fast;
    fast.relayFallbackMs = 4000;
    fast.linkTimeoutMs = 8000;

    Events aliceEvents, bobEvents;
    auto alice = std::make_unique<DirectProvider>("alice");
    auto bob = std::make_unique<DirectProvider>("bob");
    alice->SetTimings(fast);
    bob->SetTimings(fast);
    UltraIMConfig aliceConfig, bobConfig;
    aliceConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "alice");
    bobConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "bob");
    CHECK(alice->Initialize(aliceConfig, aliceEvents.Sink()));
    CHECK(bob->Initialize(bobConfig, bobEvents.Sink()));

    UltraIMCredentials aliceLogin;
    aliceLogin.loginType = UltraIMLoginType::KeyPair;
    aliceLogin.server = relay.url;
    aliceLogin.displayName = "Alice";
    UltraIMCredentials bobLogin = aliceLogin;
    bobLogin.displayName = "Bob";

    UltraIMResult r;
    // Without createAccount there is no identity to sign in with.
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(aliceLogin, cb); }, r));
    CHECK(!r && r.code == UltraIMResultCode::NotAuthenticated);
    aliceLogin.createAccount = bobLogin.createAccount = true;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(aliceLogin, cb); }, r) && r);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { bob->SignIn(bobLogin, cb); }, r) && r);
    const std::string aliceId = alice->GetLocalUserId();
    const std::string bobId = bob->GetLocalUserId();
    CHECK(aliceId.size() == 32 && bobId.size() == 32 && aliceId != bobId);

    UltraIMConversation withBob, withAlice;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact(bobId, cb); }, withBob));
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { bob->AddContact(aliceId, cb); }, withAlice));
    CHECK(withBob.conversationId == bobId && withBob.title == "Bob");

    // Both online: the message goes peer to peer and comes back delivered.
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::PresenceChange && e.contactId == bobId &&
               e.presence == UltraIMPresence::Online;
    }) || bob->GetConnectionState() == UltraIMConnectionState::Connected);
    UltraIMSendOptions o1;
    o1.clientMessageId = "msg-p2p";
    CHECK(alice->SendText(bobId, "hello over the data channel", o1));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "hello over the data channel"); }));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return IsDelivered(e, "msg-p2p", UltraIMDeliveryStatus::Delivered);
    }));
    CHECK(alice->CountSentOverLink() >= 1);
    CHECK(alice->IsLinkOpen(bobId));
    std::printf("  P2P: %d copies over the link, %d over the relay\n", alice->CountSentOverLink(),
                alice->CountSentOverRelay());

    // Bob reads it: Alice sees the read receipt.
    CHECK(bob->MarkRead(aliceId, ""));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) { return IsDelivered(e, "msg-p2p", UltraIMDeliveryStatus::Read); }));

    // Bob goes offline: the next message waits in the relay queue.
    bob->SignOut();
    const int relayedBefore = alice->CountSentOverRelay();
    UltraIMSendOptions o2;
    o2.clientMessageId = "msg-offline";
    CHECK(alice->SendText(bobId, "while you were away", o2));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) { return IsDelivered(e, "msg-offline", UltraIMDeliveryStatus::Sent); }));
    CHECK(alice->CountSentOverRelay() > relayedBefore);
    // Back with the stored identity (no createAccount), Bob gets it.
    bobLogin.createAccount = false;
    bobLogin.server.clear();                // remembered
    CHECK(WaitResult([&](UltraIMResultCallback cb) { bob->SignIn(bobLogin, cb); }, r) && r);
    CHECK(bob->GetLocalUserId() == bobId);
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "while you were away"); }));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return IsDelivered(e, "msg-offline", UltraIMDeliveryStatus::Delivered);
    }));

    // A group: through the relay, and only once at Bob's.
    UltraIMConversation group;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->CreateGroup("Team", {bobId}, cb); }, group));
    CHECK(group.isGroup && group.memberIds.size() == 2);
    UltraIMSendOptions o3;
    o3.clientMessageId = "msg-group";
    CHECK(alice->SendText(group.conversationId, "hello team", o3));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return IsReceived(e, "hello team") && e.conversationId == group.conversationId;
    }));
    CHECK(!bob->CanCall(group.conversationId, false));

    // History is local: Bob's store holds what arrived, oldest first.
    std::vector<UltraIMMessage> history;
    bob->FetchMessages(aliceId, "", 50, [&](const UltraIMResult&, const std::vector<UltraIMMessage>& m) { history = m; });
    CHECK(history.size() == 2);
    if (history.size() == 2) CHECK(history[0].text == "hello over the data channel");

    alice->Shutdown();
    bob->Shutdown();
    UltraRtc_Shutdown();
    relay.Stop();
    std::error_code ec;
    fs::remove_all(base, ec);
#endif
}

} // namespace

int main() {
    TestEnvelope();
    TestStore();
    TestEndToEnd();
    std::printf("DirectProviderTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
