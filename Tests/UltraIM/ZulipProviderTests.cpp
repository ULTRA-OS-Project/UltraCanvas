// Tests/UltraIM/ZulipProviderTests.cpp
// Zulip provider end to end against zulip_fake_server.py (a small Zulip in
// memory, standard-library python): a wrong password refused, signing in
// with a password and with an API key, the stored session resumed; a direct
// chat opened before its first message, texts both ways through the event
// queue, a group direct message, a stream topic, an edit and a delete seen
// by the other side, a picture and a file uploaded (progress) and fetched
// back byte for byte, read, search, the event queue lost and registered
// again without losing a message, leaving, and a shutdown that does not
// wait for the long-poll; typing both ways (start and stop) in a direct chat,
// a group direct message and a stream topic, nothing for one's own, a stop
// sent only after a start; a typist's start told again while they type (no
// new start sent), stopped by their expiry, by their message (just before
// it), by their stop, and by signing out.
// Skips without python3, unless ULTRAIM_TEST_ZULIP_REQUIRED is set.
// Version: 0.2.0 - typing indicators both ways; the typist's start told again until stop, message or expiry
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "ZulipProvider.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetMime.h"

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

using namespace UltraIMZulip;

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
    // Where the first match is in the list (-1: none).
    int IndexOf(const std::function<bool(const UltraIMEvent&)>& match) {
        std::lock_guard<std::mutex> lock(mutex);
        for (size_t i = 0; i < list.size(); ++i) {
            if (match(list[i])) return static_cast<int>(i);
        }
        return -1;
    }
    int Count(const std::function<bool(const UltraIMEvent&)>& match) {
        std::lock_guard<std::mutex> lock(mutex);
        int n = 0;
        for (const auto& e : list) n += match(e) ? 1 : 0;
        return n;
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

std::vector<UltraIMMessage> Fetch(ZulipProvider& p, const std::string& conversationId,
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

std::vector<UltraIMConversation> Conversations(ZulipProvider& p) {
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

std::vector<UltraIMContact> Contacts(ZulipProvider& p) {
    std::vector<UltraIMContact> out;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    p.FetchContacts([&](const UltraIMResult&, const std::vector<UltraIMContact>& list) {
        std::lock_guard<std::mutex> lock(mutex);
        out = list;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(30), [&]() { return done; });
    return out;
}

UltraIMSearchPage Search(ZulipProvider& p, const UltraIMMessageSearch& search, UltraIMResult* result = nullptr) {
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

UltraIMCredentials Password(const std::string& url, const std::string& user) {
    UltraIMCredentials c;
    c.loginType = UltraIMLoginType::UsernamePassword;
    c.server = url;
    c.username = user + "@example.org";
    c.password = user + "-pass";
    return c;
}

UltraIMCredentials ApiKey(const std::string& url, const std::string& user) {
    UltraIMCredentials c;
    c.loginType = UltraIMLoginType::UsernamePassword;
    c.server = url;
    c.username = user + "@example.org";
    c.accessToken = user + "-key";
    return c;
}

// The test's own look at the server.
std::string Call(const std::string& url, const std::string& method, const std::string& path) {
    UltraNetHttpRequest request;
    request.url = url + path;
    request.method = method == "POST" ? UltraNetHttpMethod::Post : UltraNetHttpMethod::Get;
    UltraNetResponse response;
    UltraNet_HttpRequest(request, response);
    return response.GetBodyAsString();
}

// How many typing notifications the server has had from this user.
int TypingSent(const std::string& url, const std::string& userId) {
    UltraCanvas::JSONParseResult parsed;
    const UltraCanvas::JSONValue answer = UltraCanvas::JSON::Parse(Call(url, "POST", "/_test/typing_counts"), &parsed);
    return static_cast<int>(answer["counts"][userId].GetInteger(0));
}

std::function<bool(const UltraIMEvent&)> IsTyping(const std::string& conversationId, const std::string& who, bool on) {
    return [=](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::TypingChange && x.conversationId == conversationId && x.contactId == who &&
               x.isTyping == on;
    };
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
            const std::string script = std::string(ULTRAIM_TEST_DIR) + "/zulip_fake_server.py";
            execlp(python.c_str(), python.c_str(), script.c_str(), "--users", "alice,bob,carol", "--poll-seconds", "25",
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

void TestEndToEnd(const std::string& url, const fs::path& base) {
    ZulipTimings fast;
    fast.longPollMs = 30000;
    fast.retryMinMs = 200;
    fast.retryMaxMs = 1000;

    Events aliceEvents, bobEvents, carolEvents;
    auto alice = std::make_unique<ZulipProvider>();
    auto bob = std::make_unique<ZulipProvider>();
    auto carol = std::make_unique<ZulipProvider>();
    for (auto* p : {alice.get(), bob.get(), carol.get()}) p->SetTimings(fast);
    UltraIMConfig aliceConfig, bobConfig, carolConfig;
    aliceConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "alice");
    bobConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "bob");
    carolConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "carol");
    CHECK(alice->Initialize(aliceConfig, aliceEvents.Sink()));
    CHECK(bob->Initialize(bobConfig, bobEvents.Sink()));
    CHECK(carol->Initialize(carolConfig, carolEvents.Sink()));
    CHECK(!alice->HasStoredSession());
    CHECK(alice->GetLoginType() == UltraIMLoginType::UsernamePassword);
    CHECK(alice->GetProviderId() == "zulip" && alice->GetDisplayName() == "Zulip");
    CHECK(UltraIM_HasCapability(alice->GetCapabilities(), UltraIMCapabilityFlags::EditMessage));
    CHECK(!UltraIM_HasCapability(alice->GetCapabilities(), UltraIMCapabilityFlags::ReplyToMessage));

    // ---- signing in: a wrong password and a wrong key are refused -------------
    UltraIMResult r;
    UltraIMCredentials wrong = Password(url, "alice");
    wrong.password = "not-it";
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(wrong, cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::AuthFailed);
    CHECK(alice->GetConnectionState() == UltraIMConnectionState::Error);
    CHECK(!alice->HasStoredSession());
    UltraIMCredentials wrongKey = ApiKey(url, "bob");
    wrongKey.accessToken = "nobody-key";
    CHECK(WaitResult([&](UltraIMResultCallback cb) { bob->SignIn(wrongKey, cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::AuthFailed);

    // With a password (the API key fetched), with an API key.
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(Password(url, "alice"), cb); }, r));
    CHECK(r.success);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { bob->SignIn(ApiKey(url, "bob"), cb); }, r));
    CHECK(r.success);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { carol->SignIn(Password(url, "carol"), cb); }, r));
    CHECK(r.success);
    CHECK(alice->GetConnectionState() == UltraIMConnectionState::Connected);
    CHECK(alice->GetLocalUserId() == "10");
    CHECK(bob->GetLocalUserId() == "11");
    CHECK(alice->HasStoredSession());
    CHECK(alice->GetServerUrl() == url);

    // ---- the people: not oneself, not the bot -----------------------------------
    {
        const auto people = Contacts(*alice);
        CHECK(people.size() == 2);
        if (people.size() == 2) {
            CHECK(people[0].contactId == "11" && people[0].displayName == "Bob");
            CHECK(people[1].contactId == "12" && people[1].displayName == "Carol");
        }
    }

    // ---- a chat opened before its first message -------------------------------
    UltraIMConversation chat;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("bob@example.org", cb); }, chat));
    CHECK(chat.conversationId == "dm:11");
    CHECK(!chat.isGroup && chat.directContactId == "11");
    CHECK(chat.title == "Bob");
    CHECK(chat.memberIds.size() == 2);
    UltraIMResult missing;
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("nobody@example.org", cb); }, chat,
                            &missing));
    CHECK(missing.code == UltraIMResultCode::NotFound);
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("11", cb); }, chat));
    CHECK(chat.conversationId == "dm:11");

    // ---- texts both ways, through the event queue -------------------------------
    UltraIMSendOptions first;
    first.clientMessageId = "client-1";
    CHECK(alice->SendText("dm:11", "Hello Bob", first));
    UltraIMEvent e;
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelivered && x.message.clientMessageId == "client-1";
    }, &e));
    CHECK(e.deliveryStatus == UltraIMDeliveryStatus::Sent);
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.clientMessageId == "client-1";
    }, &e));
    const std::string helloId = e.message.messageId;
    CHECK(!helloId.empty() && helloId != "client-1");
    CHECK(e.message.isOwn && e.message.text == "Hello Bob");

    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hello Bob"); }, &e));
    CHECK(e.conversationId == "dm:10");
    CHECK(e.message.senderId == "10" && e.message.senderName == "Alice");
    CHECK(!e.message.isOwn);
    CHECK(e.message.messageId == helloId);
    CHECK(e.message.timestampMs > 1700000000000LL);
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == "dm:10" &&
               x.conversation.unreadCount == 1 && x.conversation.title == "Alice";
    }));
    // Alice's own message is not received back from her queue.
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    CHECK(aliceEvents.All(UltraIMEventType::MessageReceive).empty());

    CHECK(bob->SendText("dm:10", "Hi Alice", UltraIMSendOptions()));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hi Alice"); }, &e));
    CHECK(e.conversationId == "dm:11");
    CHECK(e.message.senderId == "11");

    // ---- a group direct message ---------------------------------------------
    UltraIMConversation group;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) {
        alice->CreateGroup("ignored", {"bob@example.org", "carol@example.org"}, cb);
    }, group));
    CHECK(group.isGroup);
    CHECK(group.conversationId == "dm:11,12");
    CHECK(group.memberIds.size() == 3);
    CHECK(group.title == "Bob, Carol");
    CHECK(alice->SendText(group.conversationId, "Hello both", UltraIMSendOptions()));
    CHECK(carolEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hello both"); }, &e));
    CHECK(e.conversationId == "dm:10,11");
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hello both"); }, &e));
    CHECK(e.conversationId == "dm:10,12");
    CHECK(carol->SendText("dm:10,11", "Hi from Carol", UltraIMSendOptions()));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hi from Carol"); }, &e));
    CHECK(e.conversationId == "dm:11,12");
    // The one-to-one chat is not touched by it.
    CHECK(Fetch(*bob, "dm:10").size() == 2);

    // ---- a stream topic ---------------------------------------------------------
    const std::string topic = "stream:1:greetings";
    CHECK(alice->SendText(topic, "Hi all", UltraIMSendOptions()));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hi all"); }, &e));
    CHECK(e.conversationId == topic);
    CHECK(carolEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hi all"); }));
    CHECK(bob->SendText(topic, "Hi from Bob", UltraIMSendOptions()));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hi from Bob"); }, &e));
    CHECK(e.conversationId == topic);
    {
        const auto list = Conversations(*bob);
        const UltraIMConversation* t = Find(list, topic);
        CHECK(t != nullptr);
        if (t) {
            CHECK(t->isGroup);
            CHECK(t->title == "#general > greetings");
            CHECK(t->memberIds.size() == 3);
            CHECK(t->lastMessageSnippet == "Hi from Bob");
        }
        CHECK(Find(list, "dm:10") != nullptr);
        CHECK(Find(list, "dm:10,12") != nullptr);
    }
    {
        const auto messages = Fetch(*carol, topic);
        CHECK(messages.size() == 2);
        if (messages.size() == 2) {
            CHECK(messages[0].text == "Hi all" && messages[0].senderName == "Alice");
            CHECK(messages[1].text == "Hi from Bob");
        }
    }

    // ---- typing, both ways ----------------------------------------------------
    CHECK(UltraIM_HasCapability(alice->GetCapabilities(), UltraIMCapabilityFlags::TypingIndicator));
    // A direct chat.
    CHECK(alice->SetTyping("dm:11", true));
    CHECK(bobEvents.WaitFor(IsTyping("dm:10", "10", true)));
    CHECK(alice->SetTyping("dm:11", false));
    CHECK(bobEvents.WaitFor(IsTyping("dm:10", "10", false)));
    CHECK(bob->SetTyping("dm:10", true));
    CHECK(aliceEvents.WaitFor(IsTyping("dm:11", "11", true)));
    CHECK(bob->SetTyping("dm:10", false));
    CHECK(aliceEvents.WaitFor(IsTyping("dm:11", "11", false)));
    // A group direct message: the same conversation as each side names it.
    CHECK(carol->SetTyping("dm:10,11", true));
    CHECK(aliceEvents.WaitFor(IsTyping("dm:11,12", "12", true)));
    CHECK(bobEvents.WaitFor(IsTyping("dm:10,12", "12", true)));
    CHECK(carol->SetTyping("dm:10,11", false));
    CHECK(aliceEvents.WaitFor(IsTyping("dm:11,12", "12", false)));
    CHECK(bobEvents.WaitFor(IsTyping("dm:10,12", "12", false)));
    // A stream topic: everyone subscribed hears it.
    CHECK(bob->SetTyping(topic, true));
    CHECK(aliceEvents.WaitFor(IsTyping(topic, "11", true)));
    CHECK(carolEvents.WaitFor(IsTyping(topic, "11", true)));
    CHECK(alice->SetTyping(topic, true));
    CHECK(bobEvents.WaitFor(IsTyping(topic, "10", true)));
    CHECK(bob->SetTyping(topic, false));
    CHECK(aliceEvents.WaitFor(IsTyping(topic, "11", false)));
    CHECK(carolEvents.WaitFor(IsTyping(topic, "11", false)));
    CHECK(alice->SetTyping(topic, false));
    CHECK(bobEvents.WaitFor(IsTyping(topic, "10", false)));
    CHECK(carolEvents.WaitFor(IsTyping(topic, "10", false)));
    // A stop with no start before it, and the notes to oneself: not sent.
    const int carolSent = TypingSent(url, "12");
    CHECK(carolSent == 2);
    CHECK(carol->SetTyping("dm:10", false));
    CHECK(carol->SetTyping("dm:12", true));
    CHECK(!carol->SetTyping("not-a-chat", true));
    // (Sent after them, on the same worker: when it is through, so are they.)
    CHECK(carol->SetTyping("dm:11", true));
    CHECK(bobEvents.WaitFor(IsTyping("dm:12", "12", true)));
    CHECK(TypingSent(url, "12") == carolSent + 1);
    CHECK(carol->SetTyping("dm:11", false));
    CHECK(bobEvents.WaitFor(IsTyping("dm:12", "12", false)));
    // Nothing for one's own typing (the server hands it back to the typist's queues too).
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(aliceEvents.Count([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::TypingChange && x.contactId == "10";
    }) == 0);
    CHECK(bobEvents.Count([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::TypingChange && x.contactId == "11";
    }) == 0);
    CHECK(carolEvents.Count([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::TypingChange && x.contactId == "12";
    }) == 0);
    // Exactly one start and one stop each, in the direct chat.
    CHECK(bobEvents.Count(IsTyping("dm:10", "10", true)) == 1);
    CHECK(bobEvents.Count(IsTyping("dm:10", "10", false)) == 1);
    CHECK(aliceEvents.Count([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::TypingChange && x.providerId != "zulip";
    }) == 0);

    // ---- edit: by its client id, as the window knows it ------------------------
    CHECK(!bob->EditMessage("dm:10", helloId, "mine now"));        // not Bob's
    CHECK(alice->EditMessage("dm:11", "client-1", "Hello Bob!"));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.messageId == helloId && x.message.isEdited;
    }, &e));
    CHECK(e.message.text == "Hello Bob!");
    CHECK(e.message.clientMessageId == "client-1");
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.messageId == helloId && x.message.isEdited;
    }, &e));
    CHECK(e.message.text == "Hello Bob!");

    // ---- delete: one's own, for everyone only ----------------------------------
    UltraIMSendOptions oops;
    oops.clientMessageId = "client-oops";
    CHECK(alice->SendText("dm:11", "oops", oops));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.clientMessageId == "client-oops";
    }, &e));
    const std::string oopsId = e.message.messageId;
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "oops"); }));
    CHECK(alice->GetDeleteChoices("dm:11", oopsId).forEveryone);
    CHECK(!alice->GetDeleteChoices("dm:11", oopsId).forMe);
    CHECK(!bob->GetDeleteChoices("dm:10", oopsId).forEveryone);
    const UltraIMResult forMe = alice->DeleteMessage("dm:11", oopsId, UltraIMDeleteScope::ForMe);
    CHECK(!forMe.success && forMe.code == UltraIMResultCode::CapabilityMissing);
    CHECK(alice->DeleteMessage("dm:11", "client-oops", UltraIMDeleteScope::ForEveryone));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelete && x.message.messageId == oopsId &&
               x.message.clientMessageId == "client-oops";
    }));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelete && x.message.messageId == oopsId && x.conversationId == "dm:10";
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(aliceEvents.Count([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelete && x.message.messageId == oopsId;
    }) == 1);

    // ---- a picture and a file: uploaded, fetched back --------------------------
    const fs::path png = base / "photo.png";
    const auto pngBytes = WritePattern(png, 300000, 7);
    aliceEvents.Clear();
    UltraIMSendOptions photo;
    photo.clientMessageId = "client-photo";
    CHECK(alice->SendImage("dm:11", UltraCanvas::PathToUtf8(png), "a photo", photo));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelivered && x.message.clientMessageId == "client-photo";
    }, &e));
    CHECK(e.deliveryStatus == UltraIMDeliveryStatus::Sent);
    CHECK(e.message.type == UltraIMMessageType::Image);
    CHECK(e.message.text == "a photo");
    {
        const auto progress = aliceEvents.All(UltraIMEventType::TransferProgress);
        CHECK(!progress.empty());
        if (!progress.empty()) {
            CHECK(progress.back().isUpload);
            CHECK(progress.back().message.clientMessageId == "client-photo");
            CHECK(progress.back().transferredBytes == progress.back().totalBytes);
        }
    }
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageReceive && x.message.type == UltraIMMessageType::Image;
    }, &e));
    CHECK(e.message.text == "a photo");
    CHECK(e.message.attachment.fileName == "photo.png");
    CHECK(e.message.attachment.mimeType == "image/png");
    CHECK(e.message.attachment.remoteUrl.rfind(url + "/user_uploads/", 0) == 0);
    const std::string photoId = e.message.messageId;
    // A picture comes by itself.
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.messageId == photoId && x.message.attachment.isDownloaded;
    }, &e));
    CHECK(ReadBytes(e.message.attachment.localPath) == pngBytes);

    // Any file: a document with a name of another script.
    const fs::path pdf = base / UltraCanvas::PathFromUtf8("r\xC3\xA9sum\xC3\xA9 \xE0\xB8\x97\xE0\xB8\x94.pdf");
    const auto pdfBytes = WritePattern(pdf, 70000, 3);
    CHECK(alice->SendFile("dm:11", UltraCanvas::PathToUtf8(pdf), UltraIMSendOptions()));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageReceive && x.message.type == UltraIMMessageType::File;
    }, &e));
    CHECK(e.message.text.empty());
    CHECK(e.message.attachment.fileName == "r\xC3\xA9sum\xC3\xA9 \xE0\xB8\x97\xE0\xB8\x94.pdf");
    CHECK(!e.message.attachment.isDownloaded);
    const std::string pdfId = e.message.messageId;
    UltraIMAttachment document;
    CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) { bob->DownloadAttachment("dm:10", pdfId, cb); }, document));
    CHECK(document.isDownloaded && ReadBytes(document.localPath) == pdfBytes);

    // The history: oldest first, the edit and the deletion in it; a page before.
    {
        const auto history = Fetch(*bob, "dm:10");
        CHECK(history.size() == 4);
        if (history.size() == 4) {
            CHECK(history[0].text == "Hello Bob!" && history[0].isEdited && !history[0].isOwn);
            CHECK(history[1].text == "Hi Alice" && history[1].isOwn);
            CHECK(history[2].type == UltraIMMessageType::Image && history[2].text == "a photo");
            CHECK(history[3].type == UltraIMMessageType::File);
            const auto older = Fetch(*bob, "dm:10", history[2].messageId, 1);
            CHECK(older.size() == 1 && older[0].text == "Hi Alice");
        }
    }

    // ---- search ---------------------------------------------------------------
    UltraIMMessageSearch search;
    search.conversationId = "dm:10";
    search.text = "hi alice";
    auto page = Search(*bob, search);
    CHECK(page.messages.size() == 1 && page.messages[0].text == "Hi Alice");
    search.text.clear();
    search.kind = UltraIMSearchKind::Images;
    page = Search(*bob, search);
    CHECK(page.messages.size() == 1);
    UltraIMMessageSearch everywhere;
    everywhere.text = "hi";
    UltraIMResult searchResult;
    Search(*bob, everywhere, &searchResult);
    CHECK(searchResult.code == UltraIMResultCode::CapabilityMissing);

    // ---- read -----------------------------------------------------------------
    {
        const auto list = Conversations(*bob);
        const UltraIMConversation* c = Find(list, "dm:10");
        CHECK(c && c->unreadCount == 3);           // Hello Bob!, the photo, the file
    }
    CHECK(bob->MarkRead("dm:10", std::string()));
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    {
        // What the server keeps: read.
        const auto list = Conversations(*bob);
        const UltraIMConversation* c = Find(list, "dm:10");
        CHECK(c && c->unreadCount == 0);
    }

    // ---- the event queue lost: a new one, nothing missed -------------------------
    aliceEvents.Clear();
    CHECK(Call(url, "POST", "/_test/forget_queues").find("success") != std::string::npos);
    CHECK(bob->SendText("dm:10", "After the restart", UltraIMSendOptions()));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "After the restart"); }));
    for (int i = 0; i < 50 && Call(url, "GET", "/_test/queues").find("\"queues\": 3") == std::string::npos; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(Call(url, "GET", "/_test/queues").find("\"queues\": 3") != std::string::npos);
    CHECK(bob->SendText("dm:10", "And later", UltraIMSendOptions()));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "And later"); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(aliceEvents.Count([](const UltraIMEvent& x) { return IsReceived(x, "After the restart"); }) == 1);
    CHECK(alice->GetConnectionState() == UltraIMConnectionState::Connected);
    // Bob's own messages did not come back to him as received.
    CHECK(bobEvents.Count([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageReceive && x.message.isOwn;
    }) == 0);

    // ---- leave -----------------------------------------------------------------
    CHECK(WaitResult([&](UltraIMResultCallback cb) { carol->LeaveConversation("dm:10,11", cb); }, r));
    CHECK(r.success);
    CHECK(carolEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationRemove && x.conversationId == "dm:10,11";
    }));
    CHECK(Find(Conversations(*carol), "dm:10,11") == nullptr);
    // A direct chat comes back with its next message.
    CHECK(alice->SendText("dm:11,12", "Still there?", UltraIMSendOptions()));
    CHECK(carolEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Still there?"); }));
    CHECK(Find(Conversations(*carol), "dm:10,11") != nullptr);
    // A topic left is muted: what comes to it is not news here.
    CHECK(WaitResult([&](UltraIMResultCallback cb) { bob->LeaveConversation(topic, cb); }, r));
    CHECK(r.success);
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationRemove && x.conversationId == topic;
    }));
    CHECK(alice->SendText(topic, "Anyone?", UltraIMSendOptions()));
    CHECK(carolEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Anyone?"); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(bobEvents.Count([](const UltraIMEvent& x) { return IsReceived(x, "Anyone?"); }) == 0);
    CHECK(Find(Conversations(*bob), topic) == nullptr);

    // ---- a stored session: the next start signs in by itself ------------------
    alice->Shutdown();
    alice = std::make_unique<ZulipProvider>();
    alice->SetTimings(fast);
    aliceEvents.Clear();
    CHECK(alice->Initialize(aliceConfig, aliceEvents.Sink()));
    CHECK(alice->HasStoredSession());
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(UltraIMCredentials(), cb); }, r));
    CHECK(r.success);
    CHECK(alice->GetLocalUserId() == "10");
    CHECK(alice->GetServerUrl() == url);
    CHECK(Fetch(*alice, "dm:11").size() == 6);
    // And it hears news again.
    CHECK(bob->SendText("dm:10", "Welcome back", UltraIMSendOptions()));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Welcome back"); }));

    // ---- shutting down with a long-poll open: at once --------------------------
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const auto start = std::chrono::steady_clock::now();
    alice->Shutdown();
    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf("  shutdown with a long-poll open: %lld ms\n", static_cast<long long>(took));
    CHECK(took < 5000);

    bob->Shutdown();
    carol->Shutdown();
}

// The typists, as the provider keeps them: told again, expired, stopped.
void TestTypingRefresh(const std::string& url, const fs::path& base) {
    using Clock = std::chrono::steady_clock;
    const auto msSince = [](Clock::time_point t) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
    };
    ZulipTimings fast;
    fast.longPollMs = 30000;
    fast.retryMinMs = 200;
    fast.retryMaxMs = 1000;
    fast.typingRefreshMs = 300;
    fast.typingExpireMs = 1500;
    Events aliceEvents, bobEvents;
    auto alice = std::make_unique<ZulipProvider>();
    auto bob = std::make_unique<ZulipProvider>();
    alice->SetTimings(fast);
    bob->SetTimings(fast);
    UltraIMConfig aliceConfig, bobConfig;
    aliceConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "refresh-alice");
    bobConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "refresh-bob");
    CHECK(alice->Initialize(aliceConfig, aliceEvents.Sink()));
    CHECK(bob->Initialize(bobConfig, bobEvents.Sink()));
    UltraIMResult r;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(ApiKey(url, "alice"), cb); }, r));
    CHECK(r.success);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { bob->SignIn(ApiKey(url, "bob"), cb); }, r));
    CHECK(r.success);
    const auto starts = [&bobEvents]() { return bobEvents.Count(IsTyping("dm:10", "10", true)); };

    // ---- told again while Alice types, with no new start from her; then expired ----
    const int sent = TypingSent(url, "10");
    auto started = Clock::now();
    CHECK(alice->SetTyping("dm:11", true));
    CHECK(bobEvents.WaitFor(IsTyping("dm:10", "10", true)));
    CHECK(bobEvents.WaitFor(IsTyping("dm:10", "10", false)));
    const auto expiredAfter = msSince(started);
    std::printf("  typing expired after %lld ms, told %d times\n", static_cast<long long>(expiredAfter), starts());
    CHECK(expiredAfter >= 1300 && expiredAfter < 4000);
    CHECK(starts() >= 3);                                   // the start, and the refreshes
    CHECK(TypingSent(url, "10") == sent + 1);               // one start only from Alice
    {
        // Nothing after the stop.
        const int startsAtStop = starts();
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        CHECK(starts() == startsAtStop);
        CHECK(bobEvents.Count(IsTyping("dm:10", "10", false)) == 1);
        bobEvents.Clear();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(bobEvents.Count([](const UltraIMEvent& x) { return x.type == UltraIMEventType::TypingChange; }) == 0);

    // ---- her message stops it: just before the message, long before the expiry ----
    started = Clock::now();
    CHECK(alice->SetTyping("dm:11", true));
    CHECK(bobEvents.WaitFor(IsTyping("dm:10", "10", true)));
    CHECK(alice->SendText("dm:11", "Typed it", UltraIMSendOptions()));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Typed it"); }));
    CHECK(msSince(started) < 1300);
    {
        const int stopAt = bobEvents.IndexOf(IsTyping("dm:10", "10", false));
        const int postAt = bobEvents.IndexOf([](const UltraIMEvent& x) { return IsReceived(x, "Typed it"); });
        CHECK(stopAt >= 0 && stopAt == postAt - 1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    CHECK(bobEvents.Count(IsTyping("dm:10", "10", false)) == 1);   // not expired a second time
    bobEvents.Clear();

    // ---- her stop ends the refreshes ------------------------------------------------
    CHECK(alice->SetTyping("dm:11", true));
    CHECK(bobEvents.WaitFor(IsTyping("dm:10", "10", true)));
    CHECK(alice->SetTyping("dm:11", false));
    CHECK(bobEvents.WaitFor(IsTyping("dm:10", "10", false)));
    const int startsAtStop = starts();
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    CHECK(starts() == startsAtStop);
    CHECK(bobEvents.Count(IsTyping("dm:10", "10", false)) == 1);
    bobEvents.Clear();

    // ---- signing out stops whoever was typing --------------------------------------
    CHECK(alice->SetTyping("dm:11", true));
    CHECK(bobEvents.WaitFor(IsTyping("dm:10", "10", true)));
    bob->SignOut();
    CHECK(bobEvents.Count(IsTyping("dm:10", "10", false)) == 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(bobEvents.Count(IsTyping("dm:10", "10", false)) == 1);
    alice->Shutdown();
    bob->Shutdown();
}

void TestNoServer(const fs::path& base) {
    Events none;
    auto nobody = std::make_unique<ZulipProvider>();
    UltraIMConfig empty;
    empty.dataDirectory = UltraCanvas::PathToUtf8(base / "nobody");
    CHECK(nobody->Initialize(empty, none.Sink()));
    UltraIMResult r;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { nobody->SignIn(UltraIMCredentials(), cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::InvalidArgument);
    // Nothing listens there: a network error, said at once.
    UltraIMCredentials c;
    c.server = "http://127.0.0.1:9";
    c.username = "a@example.org";
    c.password = "x";
    CHECK(WaitResult([&](UltraIMResultCallback cb) { nobody->SignIn(c, cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::NetworkError);
    nobody->Shutdown();
}
#endif

} // namespace

int main() {
    UltraNet_Initialize();
#if defined(_WIN32)
    std::printf("ZulipProviderTests: skipped on Windows\n");
#else
    const fs::path base = fs::temp_directory_path() / ("ultraim-zulip-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    FakeServer server;
    if (!server.Start("python3")) {
        const bool required = !UltraCanvas::GetEnvUtf8("ULTRAIM_TEST_ZULIP_REQUIRED").empty();
        std::printf("ZulipProviderTests: %s (no python3)\n", required ? "FAIL" : "SKIP");
        return required ? 1 : 0;
    }
    std::printf("  server %s\n", server.url.c_str());
    TestEndToEnd(server.url, base);
    TestTypingRefresh(server.url, base);
    TestNoServer(base);
    server.Stop();
    fs::remove_all(base, ec);
#endif
    UltraNet_Shutdown();
    std::printf("ZulipProviderTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
