// Tests/UltraIM/MattermostProviderTests.cpp
// Mattermost provider end to end against mattermost_fake_server.py (a small
// Mattermost in memory, standard-library python): signing in with a password
// (a wrong one refused; by username and by e-mail), with a multi-factor code
// (NeedsInput, the Code step, a wrong code, the right one), with a personal
// access token, and with the stored session (and one the server ended); the
// team's channels as groups; a direct message opened by its address; texts
// both ways, several between two looks in order, thread replies, an edit, a
// delete, a picture, a file and a voice note uploaded (progress) and fetched
// back byte for byte; pages of history, search, read, a group message,
// leaving a channel and hiding a direct or group message (back with its next
// post); typing both ways in a direct message and a team channel (the
// others' over the WebSocket), its stop said by the provider (after
// typingStopMs, at once with their post, for everyone when the socket
// drops), nothing for one's own, the socket dropped and connected again, and
// a shutdown that does not wait. Skips without python3, unless
// ULTRAIM_TEST_MATTERMOST_REQUIRED is set.
// Version: 0.2.0 - typing indicators both ways (POST .../typing, the WebSocket)
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "MattermostProvider.h"

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
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace UltraIMMattermost;

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
    int Count(const std::function<bool(const UltraIMEvent&)>& match) {
        std::lock_guard<std::mutex> lock(mutex);
        int n = 0;
        for (const auto& e : list) n += match(e) ? 1 : 0;
        return n;
    }
    // Where the first match is in the list (-1: none).
    int IndexOf(const std::function<bool(const UltraIMEvent&)>& match) {
        std::lock_guard<std::mutex> lock(mutex);
        for (size_t i = 0; i < list.size(); ++i) {
            if (match(list[i])) return static_cast<int>(i);
        }
        return -1;
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

std::vector<UltraIMMessage> Fetch(MattermostProvider& p, const std::string& conversationId,
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

std::vector<UltraIMConversation> Conversations(MattermostProvider& p) {
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

std::string IdByTitle(const std::vector<UltraIMConversation>& list, const std::string& title) {
    for (const auto& c : list) {
        if (c.title == title) return c.conversationId;
    }
    return std::string();
}

UltraIMSearchPage Search(MattermostProvider& p, const UltraIMMessageSearch& search, UltraIMResult* result = nullptr) {
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
    c.server = url;
    c.username = user;
    c.password = user + "-pass";
    return c;
}

UltraIMCredentials Pat(const std::string& url, const std::string& user) {
    UltraIMCredentials c;
    c.server = url;
    c.accessToken = user + "-pat";
    return c;
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
            const std::string script = std::string(ULTRAIM_TEST_DIR) + "/mattermost_fake_server.py";
            execlp(python.c_str(), python.c_str(), script.c_str(), "--users", "alice,bob,carol,dave", "--mfa-user",
                   "dave", static_cast<char*>(nullptr));
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

// The server ends the user's sessions (their personal access tokens stay).
void Revoke(const std::string& url, const std::string& user) {
    UltraNetHttpRequest post;
    post.url = url + "/_test/revoke";
    post.method = UltraNetHttpMethod::Post;
    post.headers.Set("Content-Type", "application/json");
    const std::string body = "{\"user\": \"" + user + "\"}";
    post.body.assign(body.begin(), body.end());
    UltraNetResponse answer;
    UltraNet_HttpRequest(post, answer);
    CHECK(answer.statusCode == 200);
}

void TestEndToEnd(const std::string& url, const fs::path& base) {
    MattermostTimings fast;
    fast.pollMs = 300;

    Events aliceEvents, bobEvents, carolEvents;
    auto alice = std::make_unique<MattermostProvider>();
    auto bob = std::make_unique<MattermostProvider>();
    auto carol = std::make_unique<MattermostProvider>();
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
    CHECK(alice->GetProviderId() == "mattermost" && alice->GetDisplayName() == "Mattermost");

    // ---- signing in: a wrong password is refused ------------------------------
    UltraIMResult r;
    UltraIMCredentials wrong = Password(url, "alice");
    wrong.password = "not-it";
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(wrong, cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::AuthFailed);
    CHECK(alice->GetConnectionState() == UltraIMConnectionState::Error);
    CHECK(!alice->HasStoredSession());
    // By username, by e-mail address, by a personal access token.
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(Password(url, "alice"), cb); }, r));
    CHECK(r.success);
    UltraIMCredentials byMail = Password(url, "bob");
    byMail.username = "bob@example.org";
    CHECK(WaitResult([&](UltraIMResultCallback cb) { bob->SignIn(byMail, cb); }, r));
    CHECK(r.success);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { carol->SignIn(Pat(url, "carol"), cb); }, r));
    CHECK(r.success);
    CHECK(alice->GetConnectionState() == UltraIMConnectionState::Connected);
    const std::string aliceId = alice->GetLocalUserId();
    const std::string bobId = bob->GetLocalUserId();
    const std::string carolId = carol->GetLocalUserId();
    CHECK(aliceId.size() == 26 && bobId.size() == 26 && carolId.size() == 26);
    CHECK(aliceId != bobId);
    CHECK(alice->HasStoredSession());
    CHECK(alice->GetServerUrl() == url);
    CHECK(alice->GetAuthStep() == UltraIMAuthStep::Idle);

    // ---- the team's channels: groups -------------------------------------------
    auto list = Conversations(*alice);
    const std::string townSquare = IdByTitle(list, "Town Square (Team A)");
    const std::string offTopic = IdByTitle(list, "Off-Topic (Team A)");
    CHECK(!townSquare.empty() && !offTopic.empty());
    if (const UltraIMConversation* c = Find(list, townSquare)) {
        CHECK(c->isGroup);
        CHECK(!c->ownerId.empty());
        CHECK(c->memberIds.size() == 4);
        CHECK(!c->memberIds.empty() && c->memberIds.front() == aliceId);
        CHECK(c->memberNames.count(bobId) && c->memberNames.at(bobId) == "Bob");
        CHECK(c->unreadCount == 0);
        CHECK(c->lastMessageSnippet.empty());          // only "joined" posts in it
    }

    // ---- a direct message, opened by its address --------------------------------
    UltraIMConversation chat;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("@bob", cb); }, chat));
    const std::string dm = chat.conversationId;
    CHECK(dm.size() == 26);
    CHECK(!chat.isGroup && chat.directContactId == bobId);
    CHECK(chat.title == "Bob");
    CHECK(chat.memberIds.size() == 2);
    UltraIMResult missing;
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("nobody", cb); }, chat, &missing));
    CHECK(missing.code == UltraIMResultCode::NotFound);
    // The other forms of the address are the same conversation.
    for (const std::string& address : {std::string("bob"), std::string("bob@example.org"), bobId}) {
        CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact(address, cb); }, chat));
        CHECK(chat.conversationId == dm);
    }

    // ---- texts both ways ------------------------------------------------------
    UltraIMSendOptions first;
    first.clientMessageId = "client-1";
    CHECK(alice->SendText(dm, "Hello **Bob**", first));
    UltraIMEvent e;
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelivered && x.message.clientMessageId == "client-1";
    }, &e));
    CHECK(e.deliveryStatus == UltraIMDeliveryStatus::Sent);
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.clientMessageId == "client-1";
    }, &e));
    const std::string helloId = e.message.messageId;
    CHECK(helloId.size() == 26);
    CHECK(e.message.isOwn && e.message.text == "Hello **Bob**");

    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hello **Bob**"); }, &e));
    CHECK(e.conversationId == dm);                 // a channel's id is the same on both sides
    CHECK(e.message.senderId == aliceId && e.message.senderName == "Alice");
    CHECK(!e.message.isOwn);
    CHECK(e.message.replyTo.messageId.empty());
    CHECK(e.message.messageId == helloId);
    CHECK(e.message.timestampMs > 1700000000000LL);
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == dm && x.conversation.unreadCount == 1 &&
               !x.conversation.isGroup && x.conversation.directContactId == aliceId && x.conversation.title == "Alice";
    }));
    // Alice's own post is not received back.
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    CHECK(aliceEvents.All(UltraIMEventType::MessageReceive).empty());

    CHECK(bob->SendText(dm, "Hi Alice", UltraIMSendOptions()));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hi Alice"); }, &e));
    CHECK(e.conversationId == dm);
    CHECK(e.message.senderId == bobId);

    // ---- several between two looks: in order ------------------------------------
    aliceEvents.Clear();
    MattermostTimings slow = fast;
    slow.pollMs = 3000;
    alice->SetTimings(slow);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK(bob->SendText(dm, "one", UltraIMSendOptions()));
    CHECK(bob->SendText(dm, "two", UltraIMSendOptions()));
    CHECK(bob->SendText(dm, "three", UltraIMSendOptions()));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.text == "three" && x.message.isOwn;
    }));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "three"); }));
    alice->SetTimings(fast);
    std::string oneId;
    {
        const auto received = aliceEvents.All(UltraIMEventType::MessageReceive);
        CHECK(received.size() == 3);
        if (received.size() == 3) {
            CHECK(received[0].message.text == "one");
            CHECK(received[1].message.text == "two");
            CHECK(received[2].message.text == "three");
            oneId = received[0].message.messageId;
        }
    }

    // ---- thread replies --------------------------------------------------------
    UltraIMSendOptions reply;
    reply.clientMessageId = "client-reply";
    reply.replyToMessageId = oneId;
    CHECK(alice->SendText(dm, "About one", reply));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "About one"); }, &e));
    CHECK(e.message.replyTo.messageId == oneId);
    CHECK(e.message.replyTo.textSnippet == "one");
    CHECK(e.message.replyTo.senderId == bobId && e.message.replyTo.senderName == "Bob");
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.clientMessageId == "client-reply";
    }, &e));
    const std::string aboutId = e.message.messageId;
    CHECK(e.message.replyTo.messageId == oneId);
    // Answering a reply goes on in its thread: the first post's.
    UltraIMSendOptions inThread;
    inThread.replyToMessageId = aboutId;
    CHECK(bob->SendText(dm, "Reply in thread", inThread));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Reply in thread"); }, &e));
    CHECK(e.message.replyTo.messageId == oneId);

    // ---- the history in pages ----------------------------------------------------
    {
        const auto all = Fetch(*bob, dm);
        CHECK(all.size() == 7);
        if (all.size() == 7) {
            CHECK(all[0].text == "Hello **Bob**" && !all[0].isOwn);
            CHECK(all[1].text == "Hi Alice" && all[1].isOwn);
            CHECK(all[2].text == "one" && all[3].text == "two" && all[4].text == "three");
            CHECK(all[5].text == "About one" && all[5].replyTo.messageId == oneId && all[5].replyTo.textSnippet == "one");
            CHECK(all[6].text == "Reply in thread" && all[6].replyTo.messageId == oneId);
            for (int i = 0; i < 5; ++i) CHECK(all[i].replyTo.messageId.empty());
        }
        const auto newest = Fetch(*bob, dm, std::string(), 3);
        CHECK(newest.size() == 3);
        if (newest.size() == 3) {
            CHECK(newest[0].text == "three" && newest[2].text == "Reply in thread");
            const auto older = Fetch(*bob, dm, newest[0].messageId, 3);
            CHECK(older.size() == 3);
            if (older.size() == 3) {
                CHECK(older[0].text == "Hi Alice" && older[2].text == "two");
                const auto oldest = Fetch(*bob, dm, older[0].messageId, 3);
                CHECK(oldest.size() == 1 && !oldest.empty() && oldest[0].text == "Hello **Bob**");
                if (!oldest.empty()) CHECK(Fetch(*bob, dm, oldest[0].messageId, 3).empty());
            }
        }
    }

    // ---- edit: by its client id, as the window knows it ------------------------
    CHECK(!bob->EditMessage(dm, aboutId, "mine now"));          // not Bob's
    CHECK(alice->EditMessage(dm, "client-reply", "About the first one"));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.messageId == aboutId && x.message.isEdited;
    }, &e));
    CHECK(e.message.text == "About the first one");
    CHECK(e.message.clientMessageId == "client-reply");
    CHECK(e.message.replyTo.messageId == oneId);
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.messageId == aboutId && x.message.isEdited;
    }, &e));
    CHECK(e.message.text == "About the first one");

    // ---- delete: one's own, for everyone only ----------------------------------
    CHECK(alice->GetDeleteChoices(dm, aboutId).forEveryone);
    CHECK(!alice->GetDeleteChoices(dm, aboutId).forMe);
    CHECK(!alice->GetDeleteChoices(dm, oneId).forEveryone);
    const UltraIMResult forMe = alice->DeleteMessage(dm, aboutId, UltraIMDeleteScope::ForMe);
    CHECK(!forMe.success && forMe.code == UltraIMResultCode::CapabilityMissing);
    CHECK(!alice->DeleteMessage(dm, oneId, UltraIMDeleteScope::ForEveryone));      // Bob's
    CHECK(alice->DeleteMessage(dm, aboutId, UltraIMDeleteScope::ForEveryone));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelete && x.message.messageId == aboutId &&
               x.message.clientMessageId == "client-reply";
    }));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelete && x.message.messageId == aboutId && x.conversationId == dm;
    }));
    CHECK(Fetch(*bob, dm).size() == 6);

    // ---- a picture, a file, a voice note: uploaded, fetched back --------------
    const fs::path png = base / "photo.png";
    const auto pngBytes = WritePattern(png, 300000, 7);
    aliceEvents.Clear();
    UltraIMSendOptions photo;
    photo.clientMessageId = "client-photo";
    CHECK(alice->SendImage(dm, UltraCanvas::PathToUtf8(png), "a photo", photo));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelivered && x.message.clientMessageId == "client-photo";
    }, &e));
    CHECK(e.deliveryStatus == UltraIMDeliveryStatus::Sent);
    CHECK(e.message.type == UltraIMMessageType::Image);
    {
        const auto progress = aliceEvents.All(UltraIMEventType::TransferProgress);
        CHECK(!progress.empty());
        if (!progress.empty()) {
            CHECK(progress.back().isUpload);
            CHECK(progress.back().message.clientMessageId == "client-photo");
            CHECK(progress.back().conversationId == dm);
            CHECK(progress.back().transferredBytes == progress.back().totalBytes);
            CHECK(progress.back().totalBytes >= 300000);
        }
    }
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageReceive && x.message.type == UltraIMMessageType::Image;
    }, &e));
    CHECK(e.message.text == "a photo");
    CHECK(e.message.attachment.fileName == "photo.png");
    CHECK(e.message.attachment.mimeType == "image/png");
    CHECK(e.message.attachment.width == 64);
    CHECK(e.message.attachment.fileSizeBytes == 300000);
    const std::string photoId = e.message.messageId;
    // A picture comes by itself.
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.messageId == photoId && x.message.attachment.isDownloaded;
    }, &e));
    CHECK(ReadBytes(e.message.attachment.localPath) == pngBytes);

    const fs::path pdf = base / "paper.pdf";
    const auto pdfBytes = WritePattern(pdf, 50000, 3);
    CHECK(alice->SendFile(dm, UltraCanvas::PathToUtf8(pdf), UltraIMSendOptions()));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageReceive && x.message.type == UltraIMMessageType::File;
    }, &e));
    CHECK(e.message.attachment.fileName == "paper.pdf");
    CHECK(e.message.attachment.fileSizeBytes == 50000);
    CHECK(!e.message.attachment.isDownloaded);
    const std::string paperId = e.message.messageId;
    UltraIMAttachment paper;
    CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) { bob->DownloadAttachment(dm, paperId, cb); }, paper));
    CHECK(paper.isDownloaded && ReadBytes(paper.localPath) == pdfBytes);

    const fs::path ogg = base / "voice.ogg";
    const auto oggBytes = WritePattern(ogg, 20000, 5);
    UltraIMSendOptions voice;
    voice.durationSeconds = 2.5;
    CHECK(alice->SendVoiceNote(dm, UltraCanvas::PathToUtf8(ogg), voice));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageReceive && x.message.type == UltraIMMessageType::Audio;
    }, &e));
    const std::string voiceId = e.message.messageId;
    UltraIMAttachment sound;
    CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) { bob->DownloadAttachment(dm, voiceId, cb); }, sound));
    CHECK(ReadBytes(sound.localPath) == oggBytes);
    CHECK(!alice->SendFile(dm, UltraCanvas::PathToUtf8(base / "no-such-file.bin"), UltraIMSendOptions()));

    // ---- search -------------------------------------------------------------
    UltraIMMessageSearch search;
    search.conversationId = dm;
    search.text = "two";
    auto page = Search(*bob, search);
    CHECK(page.messages.size() == 1 && !page.messages.empty() && page.messages[0].text == "two");
    search.text.clear();
    search.kind = UltraIMSearchKind::Files;
    page = Search(*bob, search);
    CHECK(page.messages.size() == 1 && !page.messages.empty() && page.messages[0].attachment.fileName == "paper.pdf");
    search.kind = UltraIMSearchKind::Images;
    CHECK(Search(*bob, search).messages.size() == 1);
    UltraIMMessageSearch everywhere;
    everywhere.text = "two";
    UltraIMResult searchResult;
    Search(*bob, everywhere, &searchResult);
    CHECK(searchResult.code == UltraIMResultCode::CapabilityMissing);

    // ---- read -----------------------------------------------------------------
    {
        const UltraIMConversation* c = nullptr;
        list = Conversations(*bob);
        c = Find(list, dm);
        CHECK(c && c->unreadCount == 3);                 // the picture, the file, the voice note
        CHECK(c && c->lastMessageSnippet.rfind("[audio]", 0) == 0);
    }
    bobEvents.Clear();
    CHECK(bob->MarkRead(dm, std::string()));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == dm && x.conversation.unreadCount == 0;
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));      // a few looks: the server agrees
    list = Conversations(*bob);
    CHECK(Find(list, dm) && Find(list, dm)->unreadCount == 0);
    CHECK(!bob->MarkRead("no-such-conversation", std::string()));

    // ---- a group message ----------------------------------------------------------
    UltraIMConversation group;
    UltraIMResult groupResult;
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { alice->CreateGroup("x", {"@alice"}, cb); }, group, &groupResult));
    CHECK(groupResult.code == UltraIMResultCode::InvalidArgument);
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->CreateGroup("ignored", {"bob", "@carol"}, cb); }, group));
    const std::string groupId = group.conversationId;
    CHECK(group.isGroup);
    CHECK(group.memberIds.size() == 3);
    CHECK(group.title.find("Bob") != std::string::npos && group.title.find("Carol") != std::string::npos);
    CHECK(group.title.find("Alice") == std::string::npos);
    CHECK(alice->SendText(groupId, "Hello both", UltraIMSendOptions()));
    CHECK(carolEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hello both"); }, &e));
    CHECK(e.conversationId == groupId);
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hello both"); }, &e));
    CHECK(e.conversationId == groupId);
    CHECK(carolEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == groupId && x.conversation.isGroup &&
               x.conversation.title.find("Alice") != std::string::npos;
    }));
    // The same people again: the same group.
    UltraIMConversation again;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->CreateGroup("", {carolId, bobId}, cb); }, again));
    CHECK(again.conversationId == groupId);

    // ---- the team's channel ----------------------------------------------------------
    CHECK(carol->SendText(townSquare, "Hi all", UltraIMSendOptions()));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hi all"); }, &e));
    CHECK(e.conversationId == townSquare);
    CHECK(e.message.senderName == "Carol");

    // ---- leaving -----------------------------------------------------------------
    // A group message is hidden; its next post brings it back.
    CHECK(WaitResult([&](UltraIMResultCallback cb) { carol->LeaveConversation(groupId, cb); }, r));
    CHECK(r.success);
    CHECK(carolEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationRemove && x.conversationId == groupId;
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    CHECK(!Find(Conversations(*carol), groupId));
    carolEvents.Clear();
    CHECK(bob->SendText(groupId, "Back again", UltraIMSendOptions()));
    CHECK(carolEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Back again"); }, &e));
    CHECK(e.conversationId == groupId);
    CHECK(Find(Conversations(*carol), groupId) != nullptr);
    // A team's default channel cannot be left; another channel can.
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->LeaveConversation(townSquare, cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::PermissionDenied);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->LeaveConversation(offTopic, cb); }, r));
    CHECK(r.success);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    list = Conversations(*alice);
    CHECK(!Find(list, offTopic));
    CHECK(Find(list, townSquare) != nullptr);
    // The direct message: hidden, back with the next post.
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->LeaveConversation(dm, cb); }, r));
    CHECK(r.success);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    CHECK(!Find(Conversations(*alice), dm));
    CHECK(bob->SendText(dm, "Still there?", UltraIMSendOptions()));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Still there?"); }, &e));
    CHECK(e.conversationId == dm);
    CHECK(Find(Conversations(*alice), dm) != nullptr);

    // ---- contacts -------------------------------------------------------------------
    {
        std::vector<UltraIMContact> contacts;
        alice->FetchContacts([&](const UltraIMResult&, const std::vector<UltraIMContact>& c) { contacts = c; });
        CHECK(contacts.size() == 1 && !contacts.empty() && contacts[0].contactId == bobId &&
              contacts[0].displayName == "Bob");
    }

    // ---- a stored session: the next start signs in by itself ------------------
    alice->Shutdown();
    alice = std::make_unique<MattermostProvider>();
    alice->SetTimings(fast);
    aliceEvents.Clear();
    CHECK(alice->Initialize(aliceConfig, aliceEvents.Sink()));
    CHECK(alice->HasStoredSession());
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(UltraIMCredentials(), cb); }, r));
    CHECK(r.success);
    CHECK(alice->GetLocalUserId() == aliceId);
    CHECK(Find(Conversations(*alice), dm) != nullptr);
    CHECK(Fetch(*alice, dm).size() == 10);

    // ---- a session the server ended ------------------------------------------------
    // While connected: AuthRequire, and the session is forgotten.
    Revoke(url, "bob");
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return x.type == UltraIMEventType::AuthRequire; }));
    CHECK(bob->GetConnectionState() == UltraIMConnectionState::Error);
    CHECK(!bob->HasStoredSession());
    // At the next start: AuthFailed.
    alice->Shutdown();
    Revoke(url, "alice");
    alice = std::make_unique<MattermostProvider>();
    CHECK(alice->Initialize(aliceConfig, aliceEvents.Sink()));
    CHECK(alice->HasStoredSession());
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(UltraIMCredentials(), cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::AuthFailed);
    CHECK(!alice->HasStoredSession());

    alice->Shutdown();
    bob->Shutdown();
    carol->Shutdown();
}

std::string Get(const std::string& url, const std::string& path) {
    UltraNetHttpRequest get;
    get.url = url + path;
    get.method = UltraNetHttpMethod::Get;
    UltraNetResponse answer;
    UltraNet_HttpRequest(get, answer);
    return answer.GetBodyAsString();
}

// The user's WebSockets the server has signed in: open now, and ever.
std::pair<int, int> Sockets(const std::string& url, const std::string& user) {
    UltraCanvas::JSONParseResult parsed;
    const UltraCanvas::JSONValue all = UltraCanvas::JSON::Parse(Get(url, "/_test/sockets"), &parsed);
    return {static_cast<int>(all[user]["open"].GetInteger(0)), static_cast<int>(all[user]["opened"].GetInteger(0))};
}

bool WaitSockets(const std::string& url, const std::string& user, int open, int openedAtLeast) {
    for (int i = 0; i < 200; ++i) {
        const auto now = Sockets(url, user);
        if (now.first == open && now.second >= openedAtLeast) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

int TypingSent(const std::string& url, const std::string& user) {
    UltraCanvas::JSONParseResult parsed;
    const UltraCanvas::JSONValue all = UltraCanvas::JSON::Parse(Get(url, "/_test/typing_counts"), &parsed);
    return static_cast<int>(all[user].GetInteger(0));
}

std::function<bool(const UltraIMEvent&)> IsTyping(const std::string& conversationId, const std::string& who, bool on) {
    return [=](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::TypingChange && x.conversationId == conversationId && x.contactId == who &&
               x.isTyping == on;
    };
}

void TestTyping(const std::string& url, const fs::path& base) {
    using Clock = std::chrono::steady_clock;
    const auto msSince = [](Clock::time_point t) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
    };
    MattermostTimings fast;
    fast.pollMs = 300;
    fast.typingStopMs = 3000;
    fast.socketRetryMinMs = 200;
    fast.socketRetryMaxMs = 1000;
    Events carolEvents, daveEvents, aliceEvents;
    auto carol = std::make_unique<MattermostProvider>();
    auto dave = std::make_unique<MattermostProvider>();
    auto alice = std::make_unique<MattermostProvider>();
    UltraIMConfig carolConfig, daveConfig, aliceConfig;
    carolConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "typing-carol");
    daveConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "typing-dave");
    aliceConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "typing-alice");
    for (auto* p : {carol.get(), dave.get(), alice.get()}) p->SetTimings(fast);
    CHECK(carol->Initialize(carolConfig, carolEvents.Sink()));
    CHECK(dave->Initialize(daveConfig, daveEvents.Sink()));
    CHECK(alice->Initialize(aliceConfig, aliceEvents.Sink()));
    CHECK(UltraIM_HasCapability(carol->GetCapabilities(), UltraIMCapabilityFlags::TypingIndicator));
    UltraIMResult r;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { carol->SignIn(Pat(url, "carol"), cb); }, r));
    CHECK(r.success);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { dave->SignIn(Pat(url, "dave"), cb); }, r));
    CHECK(r.success);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(Pat(url, "alice"), cb); }, r));
    CHECK(r.success);
    const std::string carolId = carol->GetLocalUserId();
    const std::string daveId = dave->GetLocalUserId();
    // Each has its WebSocket, signed in.
    CHECK(WaitSockets(url, "carol", 1, 1));
    CHECK(WaitSockets(url, "dave", 1, 1));
    CHECK(WaitSockets(url, "alice", 1, 1));

    // ---- a direct message ---------------------------------------------------------
    UltraIMConversation chat;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { carol->AddContact("@dave", cb); }, chat));
    const std::string dm = chat.conversationId;
    CHECK(daveEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == dm;
    }));
    CHECK(!dave->SetTyping("no-such-channel-at-all-here", true));
    // Carol types: Dave sees it, and its end - Mattermost says none - after typingStopMs.
    auto started = Clock::now();
    CHECK(carol->SetTyping(dm, true));
    CHECK(daveEvents.WaitFor(IsTyping(dm, carolId, true)));
    CHECK(msSince(started) < 2000);
    started = Clock::now();
    CHECK(daveEvents.WaitFor(IsTyping(dm, carolId, false)));
    const auto quiet = msSince(started);
    std::printf("  typing stopped by itself after %lld ms\n", static_cast<long long>(quiet));
    CHECK(quiet >= 2000 && quiet < 6000);
    // "Stopped" sends nothing: Mattermost has no word for it.
    const int carolSent = TypingSent(url, "carol");
    CHECK(carolSent == 1);
    CHECK(carol->SetTyping(dm, false));
    // Dave types, then posts: the stop comes with the post, before typingStopMs.
    started = Clock::now();
    CHECK(dave->SetTyping(dm, true));
    CHECK(carolEvents.WaitFor(IsTyping(dm, daveId, true)));
    CHECK(dave->SendText(dm, "typed it", UltraIMSendOptions()));
    CHECK(carolEvents.WaitFor(IsTyping(dm, daveId, false)));
    CHECK(msSince(started) < 2500);
    CHECK(carolEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "typed it"); }));
    const int stopAt = carolEvents.IndexOf(IsTyping(dm, daveId, false));
    const int postAt = carolEvents.IndexOf([](const UltraIMEvent& x) { return IsReceived(x, "typed it"); });
    CHECK(stopAt >= 0 && stopAt < postAt);
    CHECK(TypingSent(url, "carol") == carolSent);

    // ---- a team channel: everyone in it -------------------------------------------
    const std::string townSquare = IdByTitle(Conversations(*carol), "Town Square (Team A)");
    CHECK(!townSquare.empty());
    CHECK(carol->SetTyping(townSquare, true));
    CHECK(daveEvents.WaitFor(IsTyping(townSquare, carolId, true)));
    CHECK(aliceEvents.WaitFor(IsTyping(townSquare, carolId, true)));
    CHECK(aliceEvents.WaitFor(IsTyping(townSquare, carolId, false)));

    // ---- nothing for one's own (the server sends it to the typist's socket too) ----
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(carolEvents.Count([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::TypingChange && x.contactId == carolId;
    }) == 0);
    CHECK(daveEvents.Count([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::TypingChange && x.contactId == daveId;
    }) == 0);

    // ---- the socket dropped: the typists stop at once, and it connects again -------
    CHECK(daveEvents.WaitFor(IsTyping(townSquare, carolId, false)));
    daveEvents.Clear();
    CHECK(carol->SetTyping(townSquare, true));
    CHECK(daveEvents.WaitFor(IsTyping(townSquare, carolId, true)));
    const int daveOpened = Sockets(url, "dave").second;
    UltraNetHttpRequest drop;
    drop.url = url + "/_test/drop_sockets";
    drop.method = UltraNetHttpMethod::Post;
    drop.headers.Set("Content-Type", "application/json");
    const std::string who = "{\"user\": \"dave\"}";
    drop.body.assign(who.begin(), who.end());
    UltraNetResponse dropped;
    started = Clock::now();
    UltraNet_HttpRequest(drop, dropped);
    CHECK(dropped.statusCode == 200);
    CHECK(daveEvents.WaitFor(IsTyping(townSquare, carolId, false)));
    CHECK(msSince(started) < 2500);
    CHECK(WaitSockets(url, "dave", 1, daveOpened + 1));
    CHECK(dave->GetConnectionState() == UltraIMConnectionState::Connected);
    daveEvents.Clear();
    CHECK(carol->SetTyping(dm, true));
    CHECK(daveEvents.WaitFor(IsTyping(dm, carolId, true)));

    // ---- signing out closes it; a shutdown does not wait --------------------------
    dave->SignOut();
    CHECK(WaitSockets(url, "dave", 0, 0));
    started = Clock::now();
    carol->Shutdown();
    alice->Shutdown();
    dave->Shutdown();
    const auto took = msSince(started);
    std::printf("  shutdown with the typing socket open: %lld ms\n", static_cast<long long>(took));
    CHECK(took < 3000);
    CHECK(WaitSockets(url, "carol", 0, 0));
    CHECK(WaitSockets(url, "alice", 0, 0));
}

void TestMultiFactor(const std::string& url, const fs::path& base) {
    MattermostTimings fast;
    fast.pollMs = 300;
    Events events;
    auto dave = std::make_unique<MattermostProvider>();
    dave->SetTimings(fast);
    UltraIMConfig config;
    config.dataDirectory = UltraCanvas::PathToUtf8(base / "dave");
    CHECK(dave->Initialize(config, events.Sink()));

    // The password: then the code is asked for.
    UltraIMResult r;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { dave->SignIn(Password(url, "dave"), cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::NeedsInput);
    CHECK(dave->GetAuthStep() == UltraIMAuthStep::Code);
    CHECK(!dave->GetAuthHint().empty());
    UltraIMEvent e;
    CHECK(events.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::AuthStepChange && x.authStep == UltraIMAuthStep::Code;
    }, &e));
    CHECK(!e.errorText.empty());
    CHECK(dave->GetConnectionState() != UltraIMConnectionState::Connected);
    CHECK(!dave->HasStoredSession());

    // A wrong code: refused, still the code step; the right one signs in, the
    // server, username and password remembered from before.
    UltraIMCredentials code;
    code.otpCode = "000000";
    CHECK(WaitResult([&](UltraIMResultCallback cb) { dave->SignIn(code, cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::AuthFailed);
    CHECK(dave->GetAuthStep() == UltraIMAuthStep::Code);
    code.otpCode = "123456";
    CHECK(WaitResult([&](UltraIMResultCallback cb) { dave->SignIn(code, cb); }, r));
    CHECK(r.success);
    CHECK(dave->GetAuthStep() == UltraIMAuthStep::Idle);
    CHECK(dave->GetConnectionState() == UltraIMConnectionState::Connected);
    CHECK(dave->GetLocalUserId().size() == 26);
    CHECK(dave->HasStoredSession());
    dave->Shutdown();

    // A personal access token: no code.
    Events other;
    auto token = std::make_unique<MattermostProvider>();
    UltraIMConfig tokenConfig;
    tokenConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "dave-token");
    CHECK(token->Initialize(tokenConfig, other.Sink()));
    CHECK(WaitResult([&](UltraIMResultCallback cb) { token->SignIn(Pat(url, "dave"), cb); }, r));
    CHECK(r.success);
    CHECK(other.All(UltraIMEventType::AuthStepChange).empty());
    UltraIMCredentials badToken = Pat(url, "dave");
    badToken.accessToken = "nobody-pat";
    token->Shutdown();
    token = std::make_unique<MattermostProvider>();
    UltraIMConfig badConfig;
    badConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "bad-token");
    CHECK(token->Initialize(badConfig, other.Sink()));
    CHECK(WaitResult([&](UltraIMResultCallback cb) { token->SignIn(badToken, cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::AuthFailed);
    // Nothing given, nothing stored: asked which server; a code with no sign-in before it.
    CHECK(WaitResult([&](UltraIMResultCallback cb) { token->SignIn(UltraIMCredentials(), cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::InvalidArgument);
    UltraIMCredentials lone;
    lone.server = url;
    lone.otpCode = "123456";
    CHECK(WaitResult([&](UltraIMResultCallback cb) { token->SignIn(lone, cb); }, r));
    CHECK(!r.success);
    token->Shutdown();
}
#endif

} // namespace

int main() {
    UltraNet_Initialize();
#if defined(_WIN32)
    std::printf("MattermostProviderTests: skipped on Windows\n");
#else
    const fs::path base = fs::temp_directory_path() / ("ultraim-mattermost-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    FakeServer server;
    if (!server.Start("python3")) {
        const bool required = !UltraCanvas::GetEnvUtf8("ULTRAIM_TEST_MATTERMOST_REQUIRED").empty();
        std::printf("MattermostProviderTests: %s (no python3)\n", required ? "FAIL" : "SKIP");
        return required ? 1 : 0;
    }
    std::printf("  server %s\n", server.url.c_str());
    TestEndToEnd(server.url, base);
    TestMultiFactor(server.url, base);
    TestTyping(server.url, base);
    server.Stop();
    fs::remove_all(base, ec);
#endif
    UltraNet_Shutdown();
    std::printf("MattermostProviderTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
