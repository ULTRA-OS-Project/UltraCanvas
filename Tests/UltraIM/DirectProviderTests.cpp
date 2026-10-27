// Tests/UltraIM/DirectProviderTests.cpp
// ChatCube Direct: the envelope, the store, and two providers end to end
// through a local relay (P2P, read receipts, offline queue, groups, calls),
// and two on the local network with no relay at all
// Version: 0.1.3 - the relay's port probed free, three tries
// Version: 0.1.2 - the plaintext check on the sealed bytes
// Version: 0.1.1 - TestRelayRedirect: the relay behind a redirector
// Last Modified: 2026-10-22
// Author: UltraCanvas Framework

#include "DirectCrypto.h"
#include "DirectProvider.h"
#include "DirectRelayClient.h"
#include "DirectStore.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetPlugins.h"
#include "UltraRTC/UltraRTCCore.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
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

int ProcessId() {
#if defined(_WIN32)
    return _getpid();
#else
    return ::getpid();
#endif
}

fs::path TempDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / ("ultraim-direct-" + name + "-" + std::to_string(ProcessId()));
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
    // The plaintext is not in the sealed bytes. Checked on the bytes, not on
    // the base64 text: base64 of random bytes contains any two letters, "hi"
    // included, every few hundred runs.
    const std::vector<uint8_t> sealed = FromBase64(envelope.ciphertext);
    const std::string sealedBytes(sealed.begin(), sealed.end());
    CHECK(!sealed.empty() && sealedBytes.find("{\"type\":\"text\",\"text\":\"hi\"}") == std::string::npos);

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
    CHECK(store.FindMessage("bob", "m1", back) && back.deliveryStatus == UltraIMDeliveryStatus::Read);

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
    bool Any(const std::function<bool(const UltraIMEvent&)>& match) {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& e : list) {
            if (match(e)) return true;
        }
        return false;
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
// A loopback port nobody listens on, from the one given upwards. The pid
// alone chose the port before, and a build's test rows run several suites
// at once: another process held the port now and then, the relay's bind
// failed, and the end-to-end part failed with it.
int FreePort(int from) {
    for (int port = from; port < from + 100; ++port) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return port;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<uint16_t>(port));
        const bool isFree = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0;
        ::close(fd);
        if (isFree) return port;
    }
    return from;
}

struct Relay {
    pid_t pid = -1;
    std::string url;

    bool Start(const fs::path& dataDir) {
        if (std::system("python3 -c 'import fastapi, uvicorn, cryptography' >/dev/null 2>&1") != 0) return false;
        // Three tries: the port is probed free, but another process can
        // still take it between the probe and the relay's bind.
        for (int attempt = 0; attempt < 3; ++attempt) {
            const int port = FreePort(20000 + static_cast<int>(::getpid() % 20000) + attempt * 101);
            url = "http://127.0.0.1:" + std::to_string(port);
            pid = fork();
            if (pid == 0) {
                if (chdir(ULTRAIM_RELAY_DIR) != 0) _exit(127);
                setenv("CHATCUBE_RELAY_PORT", std::to_string(port).c_str(), 1);
                setenv("CHATCUBE_RELAY_DB", UltraCanvas::PathToUtf8(dataDir / "relay.sqlite3").c_str(), 1);
                // The Telegram application's keys a relay hands its users.
                setenv("CHATCUBE_RELAY_TELEGRAM_API_ID", "1017053", 1);
                setenv("CHATCUBE_RELAY_TELEGRAM_API_HASH", "0123456789abcdef0123456789abcdef", 1);
                execlp("python3", "python3", "-m", "chatcube_relay", static_cast<char*>(nullptr));
                _exit(127);
            }
            for (int i = 0; i < 100; ++i) {
                UltraNetResponse response;
                UltraNetHttpOptions options;
                options.timeoutMs = 1000;
                if (UltraNet_HttpGet(url + "/healthz", response, options) && response.statusCode == 200) return true;
                int status = 0;
                if (waitpid(pid, &status, WNOHANG) == pid) { pid = -1; break; }   // gone: the bind failed, or no module
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            Stop();
            std::printf("DirectProviderTests: the relay did not come up on port %d (try %d)\n", port, attempt + 1);
        }
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

// Answers every request with a 301 to the same path on the relay, as an
// nginx in front of it does from http:// to https://.
struct Redirector {
    pid_t pid = -1;
    std::string url;

    bool Start(const std::string& target) {
        const int port = FreePort(40000 + static_cast<int>(::getpid() % 20000));
        url = "http://127.0.0.1:" + std::to_string(port);
        const std::string script =
            "import sys\n"
            "from http.server import BaseHTTPRequestHandler, HTTPServer\n"
            "class H(BaseHTTPRequestHandler):\n"
            "    def go(self):\n"
            "        n = int(self.headers.get('Content-Length') or 0)\n"
            "        if n: self.rfile.read(n)\n"
            "        self.send_response(301)\n"
            "        self.send_header('Location', sys.argv[2] + self.path)\n"
            "        self.send_header('Content-Length', '0')\n"
            "        self.end_headers()\n"
            "    do_GET = do_POST = go\n"
            "    def log_message(self, *a): pass\n"
            "HTTPServer(('127.0.0.1', int(sys.argv[1])), H).serve_forever()\n";
        pid = fork();
        if (pid == 0) {
            execlp("python3", "python3", "-c", script.c_str(), std::to_string(port).c_str(), target.c_str(),
                   static_cast<char*>(nullptr));
            _exit(127);
        }
        for (int i = 0; i < 100; ++i) {
            UltraNetResponse response;
            UltraNetHttpOptions options;
            options.timeoutMs = 1000;
            options.followRedirects = false;
            if (UltraNet_HttpGet(url + "/healthz", response, options) && response.statusCode == 301) return true;
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

// A relay behind a redirect: the client follows it with the body (libcurl
// would drop a POST's body, and the relay answer 422) and keeps the new
// address; a bare host is https://.
void TestRelayRedirect(const std::string& relayUrl) {
    RelayClient bare;
    bare.SetBaseUrl("relay.example.org/");
    CHECK(bare.GetBaseUrl() == "https://relay.example.org");

    Redirector redirector;
    CHECK(redirector.Start(relayUrl));
    RelayClient client;
    client.SetBaseUrl(redirector.url);
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    RelayResponse answer;
    UltraCanvas::JSONValue body = UltraCanvas::JSONValue::MakeObject();
    body.Set("public_key", UltraCanvas::JSONValue("not base64!"));
    body.Set("display_name", UltraCanvas::JSONValue("Carol"));
    body.Set("signature", UltraCanvas::JSONValue("not base64!"));
    client.Request("POST", "/v1/register", &body, [&](const RelayResponse& r) {
        std::lock_guard<std::mutex> lock(mutex);
        answer = r;
        done = true;
        cv.notify_all();
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(cv.wait_for(lock, std::chrono::seconds(30), [&]() { return done; }));
    }
    // The relay read the body: it refuses the key (400), not the request (422).
    CHECK(answer.status == 400);
    CHECK(answer.body["error"].GetString() == "invalid_request");
    CHECK(client.GetBaseUrl() == relayUrl);
    client.Stop();
    redirector.Stop();
}
#endif

bool IsDelivered(const UltraIMEvent& e, const std::string& clientId, UltraIMDeliveryStatus status) {
    return e.type == UltraIMEventType::MessageDelivered && e.message.clientMessageId == clientId &&
           e.deliveryStatus == status;
}

bool IsReceived(const UltraIMEvent& e, const std::string& text) {
    return e.type == UltraIMEventType::MessageReceive && e.message.text == text;
}

#if !defined(_WIN32)
bool IsCallState(const UltraIMEvent& e, const std::string& callId, UltraIMCallState state) {
    return e.type == UltraIMEventType::CallStateChange && e.callId == callId && e.callState == state;
}

// 1:1 voice calls between two signed-in providers: declined, then answered
// with audio both ways, then hung up.
void TestCalls(DirectProvider& alice, Events& aliceEvents, DirectProvider& bob, Events& bobEvents,
               const std::string& aliceId, const std::string& bobId) {
    alice.SetCallUseDevices(false);
    bob.SetCallUseDevices(false);
    CHECK(alice.CanCall(bobId, false));
    CHECK(alice.CanCall(bobId, true) == UltraRtc_GetCapabilities().video);
    CHECK((alice.GetCapabilities() & UltraIMCapabilityFlags::VoiceCalls) == UltraIMCapabilityFlags::VoiceCalls);

    // Declined.
    std::string declined;
    CHECK(alice.StartCall(bobId, false, declined) && declined.rfind("call:", 0) == 0);
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::CallReceive && e.callId == declined && e.contactId == aliceId && !e.isVideo;
    }));
    CHECK(bob.EndCall(declined));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& e) {
        return IsCallState(e, declined, UltraIMCallState::Ended) && e.errorText == "declined";
    }));
    CHECK(!alice.EndCall(declined));                // over

    // Answered: audio flows both ways.
    std::string callId;
    CHECK(alice.StartCall(bobId, false, callId));
    std::string second;
    CHECK(alice.StartCall(bobId, false, second).code == UltraIMResultCode::InvalidState);
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::CallReceive && e.callId == callId;
    }));
    CHECK(bob.AcceptCall(callId, false));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& e) { return IsCallState(e, callId, UltraIMCallState::Active); }));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) { return IsCallState(e, callId, UltraIMCallState::Active); }));

    std::atomic<int> bobHeard{0};
    std::atomic<int> aliceHeard{0};
    auto loud = [](const UltraRtcAudioFrame& f) {
        double sum = 0;
        for (int16_t v : f.samples) sum += static_cast<double>(v) * v;
        return !f.samples.empty() && std::sqrt(sum / static_cast<double>(f.samples.size())) > 3000.0;
    };
    const UltraRtcHandle aliceTrack = alice.GetCallAudioTrack(callId);
    const UltraRtcHandle bobTrack = bob.GetCallAudioTrack(callId);
    CHECK(aliceTrack != UltraRtcInvalidHandle && bobTrack != UltraRtcInvalidHandle);
    UltraRtc_SetAudioFrameCallback(bobTrack, [&](const UltraRtcAudioFrame& f) { if (loud(f)) ++bobHeard; });
    UltraRtc_SetAudioFrameCallback(aliceTrack, [&](const UltraRtcAudioFrame& f) { if (loud(f)) ++aliceHeard; });
    constexpr double kPi = 3.14159265358979323846;     // M_PI is not standard C++
    UltraRtcAudioFrame tone;
    tone.samples.resize(960);
    for (size_t i = 0; i < tone.samples.size(); ++i)
        tone.samples[i] = static_cast<int16_t>(10000.0 * std::sin(2.0 * kPi * 400.0 * static_cast<double>(i) / 48000.0));
    for (int i = 0; i < 25; ++i) {
        UltraRtc_SendAudioFrame(aliceTrack, tone);
        UltraRtc_SendAudioFrame(bobTrack, tone);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::printf("  call: Bob heard %d loud frames, Alice %d\n", bobHeard.load(), aliceHeard.load());
    CHECK(bobHeard.load() >= 15);
    CHECK(aliceHeard.load() >= 15);

    // Muted: Bob hears nothing more from Alice.
    CHECK(alice.SetCallMuted(callId, true));
    const int heardBefore = bobHeard.load();
    for (int i = 0; i < 10; ++i) {
        UltraRtc_SendAudioFrame(aliceTrack, tone);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(bobHeard.load() == heardBefore);

    // Alice hangs up: Bob's call ends too.
    CHECK(alice.EndCall(callId));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return IsCallState(e, callId, UltraIMCallState::Ended) && e.errorText.empty();
    }));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& e) { return IsCallState(e, callId, UltraIMCallState::Ended); }));
    CHECK(bob.GetCallAudioTrack(callId) == UltraRtcInvalidHandle);

    // Bob offline: the relay cannot deliver the invite and the call ends.
    bob.SignOut();
    CHECK(!bob.CanCall(aliceId, false));            // no relay socket
    std::string unreachable;
    CHECK(alice.StartCall(bobId, false, unreachable));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& e) {
        return IsCallState(e, unreachable, UltraIMCallState::Ended) && e.errorText == "offline";
    }));
}

// A 1:1 video call: pictures fed into Alice's track reach Bob's remote
// sink, Bob's camera going off reaches Alice, then Alice hangs up.
void TestVideoCall(DirectProvider& alice, Events& aliceEvents, DirectProvider& bob, Events& bobEvents,
                   const std::string& bobId) {
    UltraRtcVideoConfig small;
    small.width = 320;
    small.height = 240;
    small.fps = 25;
    small.bitrateKbps = 400;
    alice.SetCallVideoConfig(small);
    bob.SetCallVideoConfig(small);
    CHECK(alice.CanCall(bobId, true));

    std::string callId;
    CHECK(alice.StartCall(bobId, true, callId));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::CallReceive && e.callId == callId && e.isVideo;
    }));
    std::mutex mutex;
    int bobSaw = 0, bobWidth = 0;
    uint8_t bobCentre[3] = {0, 0, 0};
    CHECK(bob.SetCallVideoSinks(callId, [&](const uint8_t* rgba, int width, int height) {
        std::lock_guard<std::mutex> lock(mutex);
        ++bobSaw;
        bobWidth = width;
        const size_t at = (static_cast<size_t>(height / 2) * static_cast<size_t>(width) + static_cast<size_t>(width / 2)) * 4;
        bobCentre[0] = rgba[at];
        bobCentre[1] = rgba[at + 1];
        bobCentre[2] = rgba[at + 2];
    }, nullptr));
    CHECK(bob.AcceptCall(callId, true));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& e) { return IsCallState(e, callId, UltraIMCallState::Active); }));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) { return IsCallState(e, callId, UltraIMCallState::Active); }));
    const UltraRtcHandle aliceVideo = alice.GetCallVideoTrack(callId);
    CHECK(aliceVideo != UltraRtcInvalidHandle);

    // Alice's "camera": purple pictures, paced like one.
    UltraRtcVideoFrame picture;
    picture.width = 320;
    picture.height = 240;
    picture.rgba.resize(320 * 240 * 4);
    for (size_t i = 0; i < picture.rgba.size(); i += 4) {
        picture.rgba[i] = 150;
        picture.rgba[i + 1] = 40;
        picture.rgba[i + 2] = 200;
        picture.rgba[i + 3] = 255;
    }
    // The video track may open a moment after the audio one.
    for (int i = 0; i < 50 && !UltraRtc_IsTrackOpen(aliceVideo); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    for (int i = 0; i < 25; ++i) {
        UltraRtc_SendVideoFrame(aliceVideo, picture);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::printf("  video call: Bob saw %d pictures, %d wide, centre %d,%d,%d\n", bobSaw, bobWidth,
                    bobCentre[0], bobCentre[1], bobCentre[2]);
        CHECK(bobSaw >= 15 && bobWidth == 320);
        CHECK(bobCentre[0] > 120 && bobCentre[0] < 180 && bobCentre[2] > 170);
    }

    // Bob's camera goes off: Alice hears of it.
    CHECK(bob.SetCallCamera(callId, false));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::CallMediaChange && e.callId == callId && !e.isVideo;
    }));
    // Sinks removed: nothing is called after this returns.
    CHECK(bob.SetCallVideoSinks(callId, nullptr, nullptr));

    CHECK(alice.EndCall(callId));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) { return IsCallState(e, callId, UltraIMCallState::Ended); }));
    // The media connection closing before the relayed hang-up arrives is a
    // hang-up for Bob too, not a lost call.
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    CHECK(!bobEvents.Any([&](const UltraIMEvent& e) { return IsCallState(e, callId, UltraIMCallState::Failed); }));
}
#endif

// ============================================================================
// THE LOCAL NETWORK, NO RELAY
// ============================================================================
#if !defined(_WIN32)

bool WaitUntil(const std::function<bool()>& condition, int seconds = 20) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < end) {
        if (condition()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return condition();
}

std::vector<UltraIMContact> Nearby(DirectProvider& provider) {
    std::vector<UltraIMContact> out;
    provider.FetchNearbyContacts([&](const UltraIMResult&, const std::vector<UltraIMContact>& list) { out = list; });
    return out;
}

bool HasNearby(DirectProvider& provider, const std::string& userId) {
    for (const auto& c : Nearby(provider)) {
        if (c.contactId == userId) return true;
    }
    return false;
}

// Carol and Dave sign in with no relay server at all. Dave is told Carol's
// address (mDNS is the next test): the connection proves who each is, they
// add each other, chat peer to peer, call, and a message waits for Dave
// while he is away.
void TestLocalNetwork() {
    const fs::path base = TempDir("lan");
    ProviderTimings timings;
    timings.lanDiscoveryMs = 0;             // addresses only here
    Events carolEvents, daveEvents;
    auto carol = std::make_unique<DirectProvider>("carol");
    auto dave = std::make_unique<DirectProvider>("dave");
    carol->SetTimings(timings);
    dave->SetTimings(timings);
    carol->SetCallUseDevices(false);
    dave->SetCallUseDevices(false);
    UltraIMConfig carolConfig, daveConfig;
    carolConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "carol");
    daveConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "dave");
    CHECK(carol->Initialize(carolConfig, carolEvents.Sink()));
    CHECK(dave->Initialize(daveConfig, daveEvents.Sink()));

    UltraIMCredentials login;
    login.loginType = UltraIMLoginType::KeyPair;
    login.createAccount = true;             // and no server: the local network only
    login.displayName = "Carol";
    UltraIMResult r;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { carol->SignIn(login, cb); }, r) && r);
    login.displayName = "Dave";
    CHECK(WaitResult([&](UltraIMResultCallback cb) { dave->SignIn(login, cb); }, r) && r);
    CHECK(carol->GetConnectionState() == UltraIMConnectionState::Connected);
    const std::string carolId = carol->GetLocalUserId();
    const std::string daveId = dave->GetLocalUserId();
    CHECK(carol->GetLanPort() > 0 && dave->GetLanPort() > 0);
    // The next start signs in by itself - also with a second provider's vault
    // in the same process (DeviceKeyVault::Use takes turns).
    CHECK(carol->HasStoredSession() && dave->HasStoredSession());

    // Without a relay nobody far away can be found, and groups need one.
    UltraIMConversation none;
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) {
        carol->AddContact("0123456789abcdef0123456789abcdef", cb);
    }, none));
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { carol->CreateGroup("Team", {daveId}, cb); }, none));

    dave->AddLanAddress("127.0.0.1", carol->GetLanPort());
    CHECK(WaitUntil([&]() { return carol->IsOnLan(daveId) && dave->IsOnLan(carolId); }));
    CHECK(HasNearby(*carol, daveId));
    CHECK(HasNearby(*dave, carolId));
    {
        auto nearby = Nearby(*carol);
        CHECK(nearby.size() == 1 && nearby[0].displayName == "Dave");
    }

    UltraIMConversation withDave, withCarol;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { carol->AddContact(daveId, cb); }, withDave));
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { dave->AddContact(carolId, cb); }, withCarol));
    CHECK(withDave.title == "Dave" && withCarol.title == "Carol");
    CHECK(withDave.presence == UltraIMPresence::Online);
    CHECK(!HasNearby(*carol, daveId));      // a contact now, not a stranger nearby

    // A message: the data channel is set up through the local connection.
    UltraIMSendOptions o1;
    o1.clientMessageId = "lan-1";
    CHECK(carol->SendText(daveId, "hello across the room", o1));
    CHECK(daveEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "hello across the room"); }));
    CHECK(carolEvents.WaitFor([](const UltraIMEvent& e) { return IsDelivered(e, "lan-1", UltraIMDeliveryStatus::Delivered); }));
    CHECK(carol->CountSentOverLink() >= 1 && carol->CountSentOverRelay() == 0);
    CHECK(dave->MarkRead(carolId, ""));
    CHECK(carolEvents.WaitFor([](const UltraIMEvent& e) { return IsDelivered(e, "lan-1", UltraIMDeliveryStatus::Read); }));

    // A call over the local network.
    if (UltraRtc_GetCapabilities().audio) {
        CHECK(carol->CanCall(daveId, false));
        std::string callId;
        CHECK(carol->StartCall(daveId, false, callId));
        CHECK(daveEvents.WaitFor([&](const UltraIMEvent& e) {
            return e.type == UltraIMEventType::CallReceive && e.callId == callId;
        }));
        CHECK(dave->AcceptCall(callId, false));
        CHECK(carolEvents.WaitFor([&](const UltraIMEvent& e) { return IsCallState(e, callId, UltraIMCallState::Active); }));
        CHECK(carol->EndCall(callId));
        CHECK(daveEvents.WaitFor([&](const UltraIMEvent& e) { return IsCallState(e, callId, UltraIMCallState::Ended); }));
    }

    // Dave goes away: Carol sees him offline, and her message waits.
    dave->SignOut();
    CHECK(carolEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::PresenceChange && e.contactId == daveId &&
               e.presence == UltraIMPresence::Offline;
    }));
    UltraIMSendOptions o2;
    o2.clientMessageId = "lan-2";
    CHECK(carol->SendText(daveId, "when you are back", o2));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    CHECK(!carolEvents.Any([](const UltraIMEvent& e) { return IsDelivered(e, "lan-2", UltraIMDeliveryStatus::Delivered); }));
    // Back (the stored identity, no server): he dials Carol again and gets it.
    UltraIMCredentials again;
    again.loginType = UltraIMLoginType::KeyPair;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { dave->SignIn(again, cb); }, r) && r);
    CHECK(dave->GetLocalUserId() == daveId);
    CHECK(daveEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "when you are back"); }));
    CHECK(carolEvents.WaitFor([](const UltraIMEvent& e) { return IsDelivered(e, "lan-2", UltraIMDeliveryStatus::Delivered); }));

    carol->Shutdown();
    dave->Shutdown();
    std::error_code ec;
    fs::remove_all(base, ec);
}

// Finding each other over mDNS (_chatcube._tcp), with no address given.
// Needs the system's mDNS responder (Avahi, Bonjour, Windows 10 1703+).
void TestLocalNetworkDiscovery() {
    const fs::path base = TempDir("mdns");
    ProviderTimings timings;
    timings.lanDiscoveryMs = 1000;
    Events eveEvents, frankEvents;
    auto eve = std::make_unique<DirectProvider>("eve");
    auto frank = std::make_unique<DirectProvider>("frank");
    eve->SetTimings(timings);
    frank->SetTimings(timings);
    UltraIMConfig eveConfig, frankConfig;
    eveConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "eve");
    frankConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "frank");
    CHECK(eve->Initialize(eveConfig, eveEvents.Sink()));
    CHECK(frank->Initialize(frankConfig, frankEvents.Sink()));
    UltraIMCredentials login;
    login.loginType = UltraIMLoginType::KeyPair;
    login.createAccount = true;
    login.displayName = "Eve";
    UltraIMResult r;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { eve->SignIn(login, cb); }, r) && r);
    login.displayName = "Frank";
    CHECK(WaitResult([&](UltraIMResultCallback cb) { frank->SignIn(login, cb); }, r) && r);
    const std::string eveId = eve->GetLocalUserId();
    const std::string frankId = frank->GetLocalUserId();

    const bool found = WaitUntil([&]() { return HasNearby(*eve, frankId) && HasNearby(*frank, eveId); }, 15);
    const bool required = !UltraCanvas::GetEnvUtf8("ULTRAIM_TEST_MDNS_REQUIRED").empty();
    if (!found && !required) {
        std::printf("  mDNS discovery: SKIP (no mDNS responder answered - Avahi not running?)\n");
    } else {
        CHECK(found);
        CHECK(eveEvents.Any([](const UltraIMEvent& e) { return e.type == UltraIMEventType::NearbyChange; }));
        // Added from the nearby list alone: the connection is made, then proven.
        UltraIMConversation withFrank;
        CHECK(WaitConversation([&](UltraIMConversationCallback cb) { eve->AddContact(frankId, cb); }, withFrank));
        CHECK(withFrank.title == "Frank");
        CHECK(eve->IsOnLan(frankId));
        std::printf("  mDNS discovery: found each other, %s added %s\n", "Eve", withFrank.title.c_str());
    }
    eve->Shutdown();
    frank->Shutdown();
    std::error_code ec;
    fs::remove_all(base, ec);
}
#endif

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
    TestRelayRedirect(relay.url);

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

    // The relay keeps the Telegram application's keys for its users.
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        UltraIMResult got;
        UltraIMCredentials keys;
        auto fetch = [&](const std::string& forProvider) {
            done = false;
            alice->FetchSharedCredentials(forProvider, [&](const UltraIMResult& r, const UltraIMCredentials& c) {
                std::lock_guard<std::mutex> lock(mutex);
                got = r;
                keys = c;
                done = true;
                cv.notify_all();
            });
            std::unique_lock<std::mutex> lock(mutex);
            return cv.wait_for(lock, std::chrono::seconds(10), [&]() { return done; });
        };
        CHECK(fetch("telegram"));
        CHECK(got && keys.apiId == "1017053" && keys.apiHash == "0123456789abcdef0123456789abcdef");
        CHECK(keys.loginType == UltraIMLoginType::PhoneOTP);
        CHECK(fetch("xmpp"));
        CHECK(!got && got.code == UltraIMResultCode::CapabilityMissing);
    }

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

    if (UltraRtc_GetCapabilities().audio && UltraRtc_GetCapabilities().video) {
        TestVideoCall(*alice, aliceEvents, *bob, bobEvents, bobId);
    } else {
        std::printf("  video call skipped: this build has no Opus or no VP8\n");
    }
    if (UltraRtc_GetCapabilities().audio) {
        TestCalls(*alice, aliceEvents, *bob, bobEvents, aliceId, bobId);
    } else {
        std::printf("  calls skipped: this build has no Opus\n");
    }

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
#if !defined(_WIN32)
    UltraNet_Initialize();
    UltraNet_SetPluginDirectory(ULTRAIM_NET_PLUGIN_DIR);     // the mDNS plug-in of this build
    UltraRtc_Initialize();
    TestLocalNetwork();
    TestLocalNetworkDiscovery();
    UltraRtc_Shutdown();
#endif
    std::printf("DirectProviderTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
