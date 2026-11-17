// Tests/UltraIM/SignalProviderTests.cpp
// Signal provider end to end against signal_fake_daemon.py (signal-cli's HTTP
// JSON-RPC daemon in memory, standard-library python): no daemon and no
// signal-cli (CapabilityMissing); linking by a QR code (NeedsInput, the
// OtherDevice step with the sgnl:// link, a link that expires and a new one,
// the scan) and the link kept (a new instance connects with no QR); texts both
// ways, a message typed on the phone (ours), a reply, an edit, a remote
// delete, delivery and read receipts, typing; a picture, a file and a voice
// note both ways byte for byte; history and search; a group made, written in
// and left; a 1:1 conversation left and back; AddContact of a number not on
// Signal. Then a daemon the provider starts itself (ULTRAIM_SIGNAL_CLI: a
// wrapper running the fake with signal-cli's arguments), its attachments
// folder read directly, the daemon stopped with Shutdown and started again
// with the link it kept. Skips without python3, unless
// ULTRAIM_TEST_SIGNAL_REQUIRED is set.
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "SignalProvider.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"

#include <algorithm>
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
#include <cerrno>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace UltraIMSignal;
using UltraCanvas::JSONValue;

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

const std::string kAlice = "+4915100000001";
const std::string kBob = "+4915100000002";
const std::string kCarol = "+4915100000003";     // on Signal, never linked here: her phone alone
const std::string kDave = "+4915100000004";
const std::string kErin = "+4915100000005";

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
                 int seconds = 20) {
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
    std::vector<UltraIMEvent> All(UltraIMEventType type) {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<UltraIMEvent> out;
        for (const auto& e : list) {
            if (e.type == type) out.push_back(e);
        }
        return out;
    }
};

bool WaitResult(const std::function<void(UltraIMResultCallback)>& start, UltraIMResult& out, int seconds = 60) {
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

std::vector<UltraIMMessage> Fetch(SignalProvider& p, const std::string& conversationId,
                                  const std::string& before = std::string(), int limit = 50) {
    std::vector<UltraIMMessage> out;
    p.FetchMessages(conversationId, before, limit, [&](const UltraIMResult&, const std::vector<UltraIMMessage>& m) {
        out = m;
    });
    return out;
}

std::vector<UltraIMConversation> Conversations(SignalProvider& p) {
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

const UltraIMConversation* Find(const std::vector<UltraIMConversation>& list, const std::string& id) {
    for (const auto& c : list) {
        if (c.conversationId == id) return &c;
    }
    return nullptr;
}

UltraIMSearchPage Search(SignalProvider& p, const UltraIMMessageSearch& search, UltraIMResult* result = nullptr) {
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

// One of the fake's test endpoints; its answer.
JSONValue PostTest(const std::string& url, const std::string& path, const JSONValue& body, int* status = nullptr) {
    UltraNetHttpRequest post;
    post.url = url + path;
    post.method = UltraNetHttpMethod::Post;
    post.headers.Set("Content-Type", "application/json");
    const std::string text = UltraCanvas::JSON::Serialize(body);
    post.body.assign(text.begin(), text.end());
    UltraNetResponse answer;
    UltraNet_HttpRequest(post, answer);
    if (status) *status = answer.statusCode;
    UltraCanvas::JSONParseResult parsed;
    return UltraCanvas::JSON::Parse(answer.GetBodyAsString(), &parsed);
}

JSONValue Object(std::initializer_list<std::pair<const char*, std::string>> fields) {
    JSONValue o = JSONValue::MakeObject();
    for (const auto& [k, v] : fields) o.Set(k, JSONValue(v));
    return o;
}

SignalTimings Fast() {
    SignalTimings t;
    t.receiveTimeoutSec = 1;
    t.retryMs = 300;
    return t;
}

#if !defined(_WIN32)
struct FakeDaemon {
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
            const std::string script = std::string(ULTRAIM_TEST_DIR) + "/signal_fake_daemon.py";
            const std::string accounts = kAlice + "," + kBob + "," + kCarol;
            execlp(python.c_str(), python.c_str(), script.c_str(), "--accounts", accounts.c_str(), "--names",
                   "Alice Adams,Bob Brown,Carol Clark", static_cast<char*>(nullptr));
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

UltraIMCredentials Daemon(const std::string& url) {
    UltraIMCredentials c;
    c.loginType = UltraIMLoginType::QRCode;
    c.server = url;
    return c;
}

// Links the provider to number's phone: SignIn answers NeedsInput with the
// link, the step is OtherDevice, the phone scans it, and it is Connected.
void Link(SignalProvider& p, Events& events, const std::string& url, const std::string& number) {
    UltraIMResult r;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { p.SignIn(Daemon(url), cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::NeedsInput);
    CHECK(p.GetAuthStep() == UltraIMAuthStep::OtherDevice);
    const std::string hint = p.GetAuthHint();
    CHECK(hint.rfind("sgnl://linkdevice?", 0) == 0);
    CHECK(r.message == hint);
    UltraIMEvent e;
    CHECK(events.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::AuthStepChange && x.authStep == UltraIMAuthStep::OtherDevice;
    }, &e));
    CHECK(e.errorText == hint);
    CHECK(p.GetConnectionState() != UltraIMConnectionState::Connected);
    int status = 0;
    PostTest(url, "/_test/scan", Object({{"uri", hint}, {"number", number}}), &status);
    CHECK(status == 200);
    CHECK(events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConnectionStateChange && x.connectionState == UltraIMConnectionState::Connected;
    }));
    CHECK(p.GetConnectionState() == UltraIMConnectionState::Connected);
    CHECK(p.GetAuthStep() == UltraIMAuthStep::Idle);
    CHECK(p.GetLocalUserId() == number);
    CHECK(p.HasStoredSession());
}

void TestNoDaemon(const fs::path& base) {
    // No signal-cli anywhere (the variable names a file that is not there).
    setenv("ULTRAIM_SIGNAL_CLI", UltraCanvas::PathToUtf8(base / "no-signal-cli").c_str(), 1);
    Events events;
    auto p = std::make_unique<SignalProvider>();
    UltraIMConfig config;
    config.dataDirectory = UltraCanvas::PathToUtf8(base / "nobody");
    CHECK(p->Initialize(config, events.Sink()));
    CHECK(p->GetProviderId() == "signal" && p->GetDisplayName() == "Signal" && p->GetVersion() == "0.1.0");
    CHECK(p->GetLoginType() == UltraIMLoginType::QRCode);
    CHECK(UltraIM_HasCapability(p->GetCapabilities(), UltraIMCapabilityFlags::E2EEncryption));
    CHECK(UltraIM_HasCapability(p->GetCapabilities(), UltraIMCapabilityFlags::TypingIndicator));
    CHECK(!p->HasStoredSession());
    UltraIMResult r;
    UltraIMCredentials none;
    none.loginType = UltraIMLoginType::QRCode;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { p->SignIn(none, cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::CapabilityMissing);
    CHECK(r.message.find("signal-cli") != std::string::npos && r.message.find("Java") != std::string::npos);
    CHECK(p->GetConnectionState() == UltraIMConnectionState::Error);
    // A daemon address where nothing listens.
    CHECK(WaitResult([&](UltraIMResultCallback cb) { p->SignIn(Daemon("127.0.0.1:1"), cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::NetworkError);
    p->Shutdown();
    unsetenv("ULTRAIM_SIGNAL_CLI");
}

void TestEndToEnd(const std::string& url, const fs::path& base) {
    Events aliceEvents, bobEvents;
    auto alice = std::make_unique<SignalProvider>();
    auto bob = std::make_unique<SignalProvider>();
    alice->SetTimings(Fast());
    bob->SetTimings(Fast());
    UltraIMConfig aliceConfig, bobConfig;
    aliceConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "alice");
    aliceConfig.cacheDirectory = UltraCanvas::PathToUtf8(base / "alice-cache");
    bobConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "bob");
    bobConfig.cacheDirectory = UltraCanvas::PathToUtf8(base / "bob-cache");
    CHECK(alice->Initialize(aliceConfig, aliceEvents.Sink()));
    CHECK(bob->Initialize(bobConfig, bobEvents.Sink()));

    // ---- linking: the first link expires, a new one is shown and scanned ------
    UltraIMResult r;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(Daemon(url), cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::NeedsInput);
    const std::string first = alice->GetAuthHint();
    CHECK(first.rfind("sgnl://linkdevice?", 0) == 0);
    // Asked again while it waits: the same link.
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(Daemon(url), cb); }, r));
    CHECK(r.code == UltraIMResultCode::NeedsInput && r.message == first);
    PostTest(url, "/_test/expire", Object({{"uri", first}}));
    UltraIMEvent e;
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::AuthStepChange && x.authStep == UltraIMAuthStep::OtherDevice &&
               x.errorText != first;
    }, &e));
    const std::string second = alice->GetAuthHint();
    CHECK(second != first && second == e.errorText && second.rfind("sgnl://", 0) == 0);
    int status = 0;
    PostTest(url, "/_test/scan", Object({{"uri", second}, {"number", kAlice}}), &status);
    CHECK(status == 200);
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConnectionStateChange && x.connectionState == UltraIMConnectionState::Connected;
    }));
    CHECK(alice->GetLocalUserId() == kAlice);
    CHECK(alice->GetAuthStep() == UltraIMAuthStep::Idle);
    Link(*bob, bobEvents, url, kBob);

    // ---- the link kept: a new instance connects at once, no QR -----------------
    alice->Shutdown();
    aliceEvents.Clear();
    alice = std::make_unique<SignalProvider>();
    alice->SetTimings(Fast());
    CHECK(alice->Initialize(aliceConfig, aliceEvents.Sink()));
    CHECK(alice->HasStoredSession());
    UltraIMCredentials stored;
    stored.loginType = UltraIMLoginType::QRCode;     // (the daemon's address remembered)
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(stored, cb); }, r));
    CHECK(r.success);
    CHECK(alice->GetConnectionState() == UltraIMConnectionState::Connected);
    CHECK(aliceEvents.All(UltraIMEventType::AuthStepChange).empty());
    CHECK(alice->GetDaemonUrl() == url);
    CHECK(alice->GetDaemonProcessId() == 0);

    // ---- AddContact: not on Signal, not an address, a number with spaces -------
    UltraIMConversation conv;
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("+49 151 99999999", cb); }, conv, &r));
    CHECK(r.code == UltraIMResultCode::NotFound);
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("bob", cb); }, conv, &r));
    CHECK(r.code == UltraIMResultCode::InvalidArgument);
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("+49 151 0000-0002", cb); }, conv));
    CHECK(conv.conversationId == kBob && conv.directContactId == kBob && !conv.isGroup && conv.isEncrypted);
    const std::string aliceToBob = conv.conversationId;

    // ---- texts both ways; delivered, then read ---------------------------------
    UltraIMSendOptions options;
    options.clientMessageId = "c-hello";
    CHECK(alice->SendText(aliceToBob, "Hello Bob", options));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelivered && x.message.clientMessageId == "c-hello" &&
               x.deliveryStatus == UltraIMDeliveryStatus::Sent;
    }, &e));
    const std::string helloId = e.message.messageId;
    CHECK(helloId.rfind(kAlice + "|", 0) == 0 && e.message.isOwn);
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hello Bob"); }, &e));
    CHECK(e.message.messageId == helloId && e.conversationId == kAlice && e.message.senderId == kAlice);
    CHECK(e.message.senderName == "Alice Adams" && !e.message.isOwn);
    // Bob's device has it: delivered.
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelivered && x.message.messageId == helloId &&
               x.deliveryStatus == UltraIMDeliveryStatus::Delivered && x.message.clientMessageId == "c-hello";
    }));
    UltraIMEvent bobConv;
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == kAlice &&
               x.conversation.unreadCount == 1;
    }, &bobConv));
    CHECK(bobConv.conversation.title == "Alice Adams" && bobConv.conversation.isEncrypted);
    CHECK(bob->MarkRead(kAlice, std::string()));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelivered && x.message.messageId == helloId &&
               x.deliveryStatus == UltraIMDeliveryStatus::Read;
    }));

    // A reply (a quote of Alice's message).
    UltraIMSendOptions reply;
    reply.replyToMessageId = helloId;
    CHECK(bob->SendText(kAlice, "Hi Alice, a reply", reply));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hi Alice, a reply"); }, &e));
    CHECK(e.message.replyTo.messageId == helloId);
    CHECK(e.message.replyTo.senderId == kAlice);
    CHECK(e.message.replyTo.textSnippet == "Hello Bob");
    CHECK(e.message.senderId == kBob && e.message.senderName == "Bob Brown");
    const std::string replyId = e.message.messageId;

    // Typing.
    CHECK(bob->SetTyping(kAlice, true));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::TypingChange && x.contactId == kBob && x.isTyping && x.conversationId == kBob;
    }));
    CHECK(bob->SetTyping(kAlice, false));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::TypingChange && x.contactId == kBob && !x.isTyping;
    }));

    // ---- an edit; only one's own --------------------------------------------------
    CHECK(alice->EditMessage(aliceToBob, replyId, "not mine").code == UltraIMResultCode::PermissionDenied);
    CHECK(alice->EditMessage(aliceToBob, helloId, "Hello Bob, edited"));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.messageId == helloId &&
               x.message.text == "Hello Bob, edited" && x.message.isEdited;
    }));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.messageId == helloId && x.message.isEdited;
    }));

    // ---- a remote delete -------------------------------------------------------------
    options.clientMessageId = "c-oops";
    CHECK(alice->SendText(aliceToBob, "oops, wrong chat", options));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "oops, wrong chat"); }, &e));
    const std::string oopsId = e.message.messageId;
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.messageId == oopsId;
    }));
    CHECK(alice->GetDeleteChoices(aliceToBob, oopsId).forEveryone);
    CHECK(!alice->GetDeleteChoices(aliceToBob, oopsId).forMe);
    CHECK(!alice->GetDeleteChoices(aliceToBob, replyId).forEveryone);
    CHECK(alice->DeleteMessage(aliceToBob, oopsId, UltraIMDeleteScope::ForMe).code == UltraIMResultCode::CapabilityMissing);
    CHECK(alice->DeleteMessage(aliceToBob, replyId, UltraIMDeleteScope::ForEveryone).code == UltraIMResultCode::PermissionDenied);
    // By its client id too (a row that has not learnt the message's id).
    CHECK(alice->DeleteMessage(aliceToBob, "c-oops", UltraIMDeleteScope::ForEveryone));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelete && x.message.messageId == oopsId && x.conversationId == kAlice;
    }));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelete && x.message.messageId == oopsId &&
               x.message.clientMessageId == "c-oops";
    }));

    // ---- typed on Alice's phone: ours here, an ordinary message for Bob ------------
    JSONValue phone = Object({{"from", kAlice}, {"to", kBob}, {"message", "typed on the phone"}});
    PostTest(url, "/_test/phone_send", phone, &status);
    CHECK(status == 200);
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "typed on the phone"); }, &e));
    CHECK(e.message.isOwn && e.message.senderId == kAlice && e.conversationId == kBob);
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "typed on the phone"); }, &e));
    CHECK(!e.message.isOwn && e.conversationId == kAlice);

    // ---- a picture both ways, byte for byte -----------------------------------------
    const auto photo = WritePattern(base / "photo.png", 150000, 7);
    options.clientMessageId = "c-photo";
    CHECK(alice->SendImage(aliceToBob, UltraCanvas::PathToUtf8(base / "photo.png"), "the view", options));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::TransferProgress && x.message.clientMessageId == "c-photo" && x.isUpload &&
               x.transferredBytes == x.totalBytes && x.totalBytes == 150000;
    }));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::TransferProgress && x.message.clientMessageId == "c-photo" && x.transferredBytes == 0;
    }));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelivered && x.message.clientMessageId == "c-photo" &&
               x.deliveryStatus == UltraIMDeliveryStatus::Sent && x.message.type == UltraIMMessageType::Image;
    }));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageReceive && x.message.type == UltraIMMessageType::Image &&
               x.message.text == "the view";
    }, &e));
    CHECK(e.message.attachment.mimeType == "image/png" && e.message.attachment.fileName == "photo.png");
    CHECK(e.message.attachment.fileSizeBytes == 150000);
    const std::string photoId = e.message.messageId;
    // A picture comes by itself.
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.messageId == photoId && x.message.attachment.isDownloaded;
    }, &e));
    CHECK(ReadBytes(e.message.attachment.localPath) == photo);
    const auto photoBack = WritePattern(base / "back.jpg", 64000, 99);
    CHECK(bob->SendImage(kAlice, UltraCanvas::PathToUtf8(base / "back.jpg"), std::string(), UltraIMSendOptions()));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.type == UltraIMMessageType::Image &&
               !x.message.isOwn && x.message.attachment.isDownloaded;
    }, &e));
    CHECK(ReadBytes(e.message.attachment.localPath) == photoBack);

    // ---- a file both ways: fetched when asked for -------------------------------------
    const auto doc = WritePattern(base / "report.pdf", 300001, 3);
    CHECK(alice->SendFile(aliceToBob, UltraCanvas::PathToUtf8(base / "report.pdf"), UltraIMSendOptions()));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageReceive && x.message.type == UltraIMMessageType::File;
    }, &e));
    CHECK(e.message.attachment.fileName == "report.pdf" && !e.message.attachment.isDownloaded);
    const std::string docId = e.message.messageId;
    UltraIMAttachment got;
    CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) { bob->DownloadAttachment(kAlice, docId, cb); }, got));
    CHECK(got.isDownloaded && ReadBytes(got.localPath) == doc);
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.messageId == docId && x.message.attachment.isDownloaded;
    }));
    const auto docBack = WritePattern(base / "notes.txt", 4096, 11);
    CHECK(bob->SendFile(kAlice, UltraCanvas::PathToUtf8(base / "notes.txt"), UltraIMSendOptions()));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageReceive && x.message.type == UltraIMMessageType::File && !x.message.isOwn;
    }, &e));
    CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) { alice->DownloadAttachment(kBob, e.message.messageId, cb); }, got));
    CHECK(ReadBytes(got.localPath) == docBack);
    UltraIMAttachment none;
    CHECK(!WaitAttachment([&](UltraIMAttachmentCallback cb) { alice->DownloadAttachment(kBob, helloId, cb); }, none));

    // A voice note.
    const auto voice = WritePattern(base / "voice.ogg", 20000, 5);
    CHECK(alice->SendVoiceNote(aliceToBob, UltraCanvas::PathToUtf8(base / "voice.ogg"), UltraIMSendOptions()));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageReceive && x.message.type == UltraIMMessageType::Audio;
    }, &e));
    CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) { bob->DownloadAttachment(kAlice, e.message.messageId, cb); }, got));
    CHECK(ReadBytes(got.localPath) == voice);
    CHECK(alice->SendFile(aliceToBob, UltraCanvas::PathToUtf8(base / "missing.bin"), UltraIMSendOptions()).code ==
          UltraIMResultCode::InvalidArgument);

    // ---- history and search (this session's) ---------------------------------------
    const auto history = Fetch(*bob, kAlice);
    CHECK(history.size() >= 7);
    bool ordered = true;
    for (size_t i = 1; i < history.size(); ++i) ordered = ordered && history[i - 1].timestampMs <= history[i].timestampMs;
    CHECK(ordered);
    CHECK(!history.empty() && history.front().messageId == helloId && history.front().text == "Hello Bob, edited");
    CHECK(std::none_of(history.begin(), history.end(), [&](const UltraIMMessage& m) { return m.messageId == oopsId; }));
    const auto older = Fetch(*bob, kAlice, history.back().messageId, 2);
    CHECK(older.size() == 2 && older.back().messageId == history[history.size() - 2].messageId);
    UltraIMMessageSearch search;
    search.conversationId = kAlice;
    search.text = "phone";
    UltraIMSearchPage page = Search(*bob, search);
    CHECK(page.messages.size() == 1 && page.messages.front().text == "typed on the phone");
    search.text = "";
    search.kind = UltraIMSearchKind::Images;
    page = Search(*bob, search);
    CHECK(page.messages.size() == 2);
    search.conversationId.clear();
    search.text = "Hello";
    Search(*bob, search, &r);
    CHECK(r.code == UltraIMResultCode::CapabilityMissing);

    // ---- a group: made, written in (also from Carol's phone), left ---------------------
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->CreateGroup("Weekend", {kBob, kCarol}, cb); }, conv));
    CHECK(conv.isGroup && conv.title == "Weekend" && conv.conversationId.rfind("group:", 0) == 0);
    CHECK(conv.memberIds.size() == 3 && conv.memberIds.front() == kAlice);
    CHECK(std::find(conv.memberIds.begin(), conv.memberIds.end(), kCarol) != conv.memberIds.end());
    const std::string group = conv.conversationId;
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == group && x.conversation.title == "Weekend";
    }));
    CHECK(alice->SendText(group, "Who brings the tent?", UltraIMSendOptions()));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return IsReceived(x, "Who brings the tent?") && x.conversationId == group && x.message.senderId == kAlice;
    }));
    CHECK(bob->SendText(group, "I do", UltraIMSendOptions()));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) { return IsReceived(x, "I do") && x.conversationId == group; }));
    JSONValue fromCarol = Object({{"from", kCarol}, {"groupId", group.substr(6)}, {"message", "and I bring food"}});
    PostTest(url, "/_test/phone_send", fromCarol, &status);
    CHECK(status == 200);
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) {
        return IsReceived(x, "and I bring food") && x.conversationId == group && x.message.senderId == kCarol &&
               x.message.senderName == "Carol Clark";
    }));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) { return IsReceived(x, "and I bring food"); }));
    bobEvents.Clear();
    UltraIMResult left;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { bob->LeaveConversation(group, cb); }, left));
    CHECK(left.success);
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationRemove && x.conversationId == group;
    }));
    CHECK(!Find(Conversations(*bob), group));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == group &&
               x.conversation.memberIds.size() == 2;
    }));

    // ---- a 1:1 conversation left here, back with its next message ---------------------
    CHECK(WaitResult([&](UltraIMResultCallback cb) { bob->LeaveConversation(kAlice, cb); }, left));
    CHECK(left.success);
    CHECK(!Find(Conversations(*bob), kAlice));
    CHECK(Fetch(*bob, kAlice).empty());
    CHECK(alice->SendText(aliceToBob, "still there?", UltraIMSendOptions()));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "still there?"); }));
    const auto bobList = Conversations(*bob);
    CHECK(Find(bobList, kAlice) && Find(bobList, kAlice)->lastMessageSnippet == "still there?");

    // ---- the lists -----------------------------------------------------------------------
    const auto aliceList = Conversations(*alice);
    CHECK(Find(aliceList, kBob) && Find(aliceList, group));
    CHECK(Find(aliceList, group) && Find(aliceList, group)->title == "Weekend");
    std::vector<UltraIMContact> contacts;
    alice->FetchContacts([&](const UltraIMResult&, const std::vector<UltraIMContact>& c) { contacts = c; });
    CHECK(std::any_of(contacts.begin(), contacts.end(), [](const UltraIMContact& c) {
        return c.contactId == kBob && c.displayName == "Bob Brown";
    }));
    UltraIMAccountProfile profile;
    alice->FetchAccountProfile([&](const UltraIMResult&, const UltraIMAccountProfile& p) { profile = p; });
    CHECK(profile.phoneNumber == kAlice);

    // SignOut keeps the link: the next SignIn connects at once.
    alice->SignOut();
    CHECK(alice->GetConnectionState() == UltraIMConnectionState::Disconnected);
    CHECK(alice->HasStoredSession());
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(stored, cb); }, r));
    CHECK(r.success);
    alice->Shutdown();
    bob->Shutdown();
}

bool ProcessGone(pid_t pid) {
    return pid > 0 && kill(pid, 0) != 0 && errno == ESRCH;
}

// A daemon the provider starts itself: ULTRAIM_SIGNAL_CLI names a wrapper
// that runs the fake with the arguments signal-cli gets.
void TestOwnDaemon(const fs::path& base) {
    const fs::path wrapper = base / "signal-cli-wrapper.sh";
    {
        std::FILE* f = UltraCanvas::OpenFileUtf8(UltraCanvas::PathToUtf8(wrapper), "wb");
        if (!f) {
            CHECK(false);
            return;
        }
        const std::string script = std::string("#!/bin/sh\nexec python3 '") + ULTRAIM_TEST_DIR +
                                   "/signal_fake_daemon.py' --accounts " + kDave + "," + kErin +
                                   " --names 'Dave Dunn,Erin Ernst' \"$@\"\n";
        std::fwrite(script.data(), 1, script.size(), f);
        std::fclose(f);
        chmod(wrapper.c_str(), 0755);
    }
    setenv("ULTRAIM_SIGNAL_CLI", UltraCanvas::PathToUtf8(wrapper).c_str(), 1);

    Events daveEvents, erinEvents;
    auto dave = std::make_unique<SignalProvider>();
    dave->SetTimings(Fast());
    UltraIMConfig config;
    config.dataDirectory = UltraCanvas::PathToUtf8(base / "dave");
    CHECK(dave->Initialize(config, daveEvents.Sink()));
    UltraIMResult r;
    UltraIMCredentials own;
    own.loginType = UltraIMLoginType::QRCode;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { dave->SignIn(own, cb); }, r));
    CHECK(r.code == UltraIMResultCode::NeedsInput);
    const pid_t pid = static_cast<pid_t>(dave->GetDaemonProcessId());
    CHECK(pid > 0 && !ProcessGone(pid));
    const std::string url = dave->GetDaemonUrl();
    CHECK(url.rfind("http://127.0.0.1:", 0) == 0);
    // Started with its data under the provider's: the fake says where it listens.
    const fs::path configDir = base / "dave" / "signal" / "signal-cli";
    UltraCanvas::JSONParseResult parsed;
    const JSONValue written = UltraCanvas::JSON::ParseFile(UltraCanvas::PathToUtf8(configDir / "fake-daemon.json"), &parsed);
    CHECK(written["url"].GetString() == url);
    CHECK(written["pid"].GetInteger(0) == pid);
    int status = 0;
    PostTest(url, "/_test/scan", Object({{"uri", dave->GetAuthHint()}, {"number", kDave}}), &status);
    CHECK(status == 200);
    CHECK(daveEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConnectionStateChange && x.connectionState == UltraIMConnectionState::Connected;
    }));
    CHECK(dave->GetLocalUserId() == kDave);

    // Erin on the same daemon, by its address: a picture to Dave lands in the
    // daemon's attachments folder, which Dave's provider reads as it is.
    auto erin = std::make_unique<SignalProvider>();
    erin->SetTimings(Fast());
    UltraIMConfig erinConfig;
    erinConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "erin");
    CHECK(erin->Initialize(erinConfig, erinEvents.Sink()));
    Link(*erin, erinEvents, url, kErin);
    UltraIMConversation conv;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { erin->AddContact(kDave, cb); }, conv));
    const auto picture = WritePattern(base / "erin.png", 5000, 42);
    CHECK(erin->SendImage(kDave, UltraCanvas::PathToUtf8(base / "erin.png"), "hi Dave", UltraIMSendOptions()));
    UltraIMEvent e;
    CHECK(daveEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.attachment.isDownloaded && !x.message.isOwn;
    }, &e));
    CHECK(ReadBytes(e.message.attachment.localPath) == picture);
    CHECK(e.message.attachment.localPath.find(UltraCanvas::PathToUtf8(configDir / "attachments")) == 0);
    erin->Shutdown();

    // Shutdown stops the daemon: the process is gone.
    dave->Shutdown();
    CHECK(ProcessGone(pid));
    CHECK(dave->GetDaemonProcessId() == 0);

    // Started again: the account it linked is still there - no QR.
    daveEvents.Clear();
    dave = std::make_unique<SignalProvider>();
    dave->SetTimings(Fast());
    CHECK(dave->Initialize(config, daveEvents.Sink()));
    CHECK(WaitResult([&](UltraIMResultCallback cb) { dave->SignIn(own, cb); }, r));
    CHECK(r.success);
    CHECK(daveEvents.All(UltraIMEventType::AuthStepChange).empty());
    const pid_t again = static_cast<pid_t>(dave->GetDaemonProcessId());
    CHECK(again > 0 && again != pid);
    dave->Shutdown();
    CHECK(ProcessGone(again));
    unsetenv("ULTRAIM_SIGNAL_CLI");
}
#endif

} // namespace

int main() {
    UltraNet_Initialize();
#if defined(_WIN32)
    std::printf("SignalProviderTests: skipped on Windows\n");
#else
    const fs::path base = fs::temp_directory_path() / ("ultraim-signal-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    TestNoDaemon(base);
    FakeDaemon daemon;
    if (!daemon.Start("python3")) {
        const bool required = !UltraCanvas::GetEnvUtf8("ULTRAIM_TEST_SIGNAL_REQUIRED").empty();
        std::printf("SignalProviderTests: %s (no python3)\n", required ? "FAIL" : "SKIP");
        return required ? 1 : 0;
    }
    std::printf("  daemon %s\n", daemon.url.c_str());
    TestEndToEnd(daemon.url, base);
    daemon.Stop();
    TestOwnDaemon(base);
    fs::remove_all(base, ec);
#endif
    UltraNet_Shutdown();
    std::printf("SignalProviderTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
