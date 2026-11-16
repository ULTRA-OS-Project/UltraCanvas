// Tests/UltraIM/MastodonProviderTests.cpp
// Mastodon provider end to end against mastodon_fake_server.py (a small
// Mastodon in memory, standard-library python): signing in with a token and
// in the browser (app registration, OAuth with PKCE, the session kept), a
// direct chat opened before its first message, texts both ways, several
// statuses between two looks arriving in order, a reply, an edit, a delete,
// a picture and a video uploaded (progress, processing) and fetched back byte
// for byte, a file Mastodon does not take, search, read, a group, leaving.
// Skips without python3, unless ULTRAIM_TEST_MASTODON_REQUIRED is set.
// Version: 0.1.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "MastodonProvider.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"

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

using namespace UltraIMMastodon;

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

std::vector<UltraIMMessage> Fetch(MastodonProvider& p, const std::string& conversationId) {
    std::vector<UltraIMMessage> out;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    p.FetchMessages(conversationId, std::string(), 50, [&](const UltraIMResult&, const std::vector<UltraIMMessage>& m) {
        std::lock_guard<std::mutex> lock(mutex);
        out = m;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(30), [&]() { return done; });
    return out;
}

UltraIMSearchPage Search(MastodonProvider& p, const UltraIMMessageSearch& search, UltraIMResult* result = nullptr) {
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

UltraIMCredentials Token(const std::string& url, const std::string& user) {
    UltraIMCredentials c;
    c.loginType = UltraIMLoginType::SsoOidc;
    c.server = url;
    c.accessToken = user + "-token";
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
            const std::string script = std::string(ULTRAIM_TEST_DIR) + "/mastodon_fake_server.py";
            execlp(python.c_str(), python.c_str(), script.c_str(), "--users", "alice,bob,carol,dave", "--oauth-user",
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

std::string HostOf(const std::string& url) {
    return url.substr(url.find("://") + 3);
}

void TestEndToEnd(const std::string& url, const fs::path& base) {
    const std::string host = HostOf(url);
    MastodonTimings fast;
    fast.pollMs = 300;
    fast.mediaWaitMs = 10000;
    fast.oauthTimeoutMs = 20000;

    Events aliceEvents, bobEvents, carolEvents;
    auto alice = std::make_unique<MastodonProvider>();
    auto bob = std::make_unique<MastodonProvider>();
    auto carol = std::make_unique<MastodonProvider>();
    for (auto* p : {alice.get(), bob.get(), carol.get()}) p->SetTimings(fast);
    UltraIMConfig aliceConfig, bobConfig, carolConfig;
    aliceConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "alice");
    bobConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "bob");
    carolConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "carol");
    CHECK(alice->Initialize(aliceConfig, aliceEvents.Sink()));
    CHECK(bob->Initialize(bobConfig, bobEvents.Sink()));
    CHECK(carol->Initialize(carolConfig, carolEvents.Sink()));
    CHECK(!alice->HasStoredSession());
    CHECK(alice->GetLoginType() == UltraIMLoginType::SsoOidc);

    // ---- signing in with a token: a wrong one is refused ----------------------
    UltraIMResult r;
    UltraIMCredentials wrong = Token(url, "alice");
    wrong.accessToken = "nobody-token";
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(wrong, cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::AuthFailed);
    CHECK(alice->GetConnectionState() == UltraIMConnectionState::Error);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(Token(url, "alice"), cb); }, r));
    CHECK(r.success);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { bob->SignIn(Token(url, "bob"), cb); }, r));
    CHECK(r.success);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { carol->SignIn(Token(url, "carol"), cb); }, r));
    CHECK(r.success);
    CHECK(alice->GetConnectionState() == UltraIMConnectionState::Connected);
    CHECK(alice->GetLocalUserId() == "alice@" + host);
    CHECK(alice->HasStoredSession());
    CHECK(alice->GetServerUrl() == url);

    // ---- a chat opened before its first message -------------------------------
    const std::string withBob = "bob@" + host;
    const std::string withAlice = "alice@" + host;
    UltraIMConversation chat;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("@bob", cb); }, chat));
    CHECK(chat.conversationId == withBob);
    CHECK(!chat.isGroup && chat.directContactId == withBob);
    CHECK(chat.title == "Bob");
    UltraIMResult missing;
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("@nobody", cb); }, chat, &missing));
    CHECK(missing.code == UltraIMResultCode::NotFound);
    // The address forms: "user@server" and the profile's page are the same chat.
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact(url + "/@bob", cb); }, chat));
    CHECK(chat.conversationId == withBob);

    // ---- texts both ways ------------------------------------------------------
    UltraIMSendOptions first;
    first.clientMessageId = "client-1";
    CHECK(alice->SendText(withBob, "Hello Bob", first));
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
    CHECK(e.conversationId == withAlice);
    CHECK(e.message.senderId == withAlice && e.message.senderName == "Alice");
    CHECK(!e.message.isOwn);
    CHECK(e.message.replyTo.messageId.empty());
    CHECK(e.message.messageId == helloId);
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == withAlice && x.conversation.unreadCount == 1;
    }));
    // Alice's own status is not received back.
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    CHECK(aliceEvents.All(UltraIMEventType::MessageReceive).empty());

    // Bob answers; then two at once: Alice gets them in order, none a quote.
    CHECK(bob->SendText(withAlice, "Hi Alice", UltraIMSendOptions()));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hi Alice"); }, &e));
    CHECK(e.message.replyTo.messageId.empty());
    CHECK(e.conversationId == withBob);
    // Two quickly, so that both come between Alice's looks.
    aliceEvents.Clear();
    MastodonTimings slow = fast;
    slow.pollMs = 3000;
    alice->SetTimings(slow);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK(bob->SendText(withAlice, "one", UltraIMSendOptions()));
    CHECK(bob->SendText(withAlice, "two", UltraIMSendOptions()));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.text == "two" && x.message.isOwn;
    }));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "two"); }));
    alice->SetTimings(fast);
    {
        const auto received = aliceEvents.All(UltraIMEventType::MessageReceive);
        CHECK(received.size() == 2);
        if (received.size() == 2) {
            CHECK(received[0].message.text == "one");
            CHECK(received[1].message.text == "two");
            CHECK(received[0].message.replyTo.messageId.empty());
            CHECK(received[1].message.replyTo.messageId.empty());
        }
    }
    std::string oneId;
    for (const auto& x : aliceEvents.All(UltraIMEventType::MessageReceive)) {
        if (x.message.text == "one") oneId = x.message.messageId;
    }

    // ---- a reply: a quote of the one answered --------------------------------
    UltraIMSendOptions reply;
    reply.clientMessageId = "client-reply";
    reply.replyToMessageId = oneId;
    CHECK(alice->SendText(withBob, "About one", reply));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "About one"); }, &e));
    CHECK(e.message.replyTo.messageId == oneId);
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.clientMessageId == "client-reply";
    }, &e));
    const std::string aboutId = e.message.messageId;
    // The thread shows the quote with its words.
    {
        const auto thread = Fetch(*bob, withAlice);
        CHECK(thread.size() == 5);
        if (thread.size() == 5) {
            CHECK(thread[0].text == "Hello Bob" && !thread[0].isOwn);
            CHECK(thread[1].text == "Hi Alice" && thread[1].isOwn);
            CHECK(thread[2].text == "one");
            CHECK(thread[3].text == "two");
            CHECK(thread[4].text == "About one");
            CHECK(thread[4].replyTo.messageId == oneId);
            CHECK(thread[4].replyTo.textSnippet == "one");
            for (int i = 0; i < 4; ++i) CHECK(thread[i].replyTo.messageId.empty());
            CHECK(thread[0].timestampMs > 1700000000000LL);
        }
    }

    // ---- edit: by its client id, as the window knows it ------------------------
    CHECK(!bob->EditMessage(withAlice, aboutId, "mine now"));          // not Bob's
    CHECK(alice->EditMessage(withBob, "client-reply", "About the first one"));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.messageId == aboutId && x.message.isEdited;
    }, &e));
    CHECK(e.message.text == "About the first one");
    CHECK(e.message.replyTo.messageId == oneId);
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.messageId == aboutId && x.message.isEdited;
    }, &e));
    CHECK(e.message.text == "About the first one");

    // ---- delete: one's own, for everyone only ----------------------------------
    CHECK(alice->GetDeleteChoices(withBob, aboutId).forEveryone);
    CHECK(!alice->GetDeleteChoices(withBob, oneId).forEveryone);
    CHECK(!alice->DeleteMessage(withBob, aboutId, UltraIMDeleteScope::ForMe));
    CHECK(alice->DeleteMessage(withBob, aboutId, UltraIMDeleteScope::ForEveryone));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelete && x.message.messageId == aboutId &&
               x.message.clientMessageId == "client-reply";
    }));
    // Bob's side: the chat's last status went, so it was deleted.
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelete && x.message.messageId == aboutId;
    }));
    CHECK(Fetch(*bob, withAlice).size() == 4);

    // ---- a picture and a video: uploaded, processed, fetched back ------------
    const fs::path png = base / "photo.png";
    const auto pngBytes = WritePattern(png, 300000, 7);
    aliceEvents.Clear();
    UltraIMSendOptions photo;
    photo.clientMessageId = "client-photo";
    CHECK(alice->SendImage(withBob, UltraCanvas::PathToUtf8(png), "a photo", photo));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageDelivered && x.message.clientMessageId == "client-photo";
    }, &e));
    CHECK(e.deliveryStatus == UltraIMDeliveryStatus::Sent);
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
    CHECK(e.message.attachment.width == 64);
    CHECK(e.message.attachment.fileName == "photo.png");
    const std::string photoId = e.message.messageId;
    // A picture comes by itself.
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageUpdate && x.message.messageId == photoId && x.message.attachment.isDownloaded;
    }, &e));
    CHECK(ReadBytes(e.message.attachment.localPath) == pngBytes);

    const fs::path webm = base / "clip.webm";
    const auto webmBytes = WritePattern(webm, 120000, 11);
    CHECK(alice->SendVideo(withBob, UltraCanvas::PathToUtf8(webm), std::string(), UltraIMSendOptions()));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::MessageReceive && x.message.type == UltraIMMessageType::Video;
    }, &e));
    CHECK(e.message.attachment.durationSeconds > 2.0);
    UltraIMAttachment clip;
    const std::string clipId = e.message.messageId;
    CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) { bob->DownloadAttachment(withAlice, clipId, cb); }, clip));
    CHECK(clip.isDownloaded && ReadBytes(clip.localPath) == webmBytes);

    // A file that is not a picture, a video or sound: Mastodon does not take it.
    const fs::path pdf = base / "paper.pdf";
    WritePattern(pdf, 1000, 3);
    const UltraIMResult refused = alice->SendFile(withBob, UltraCanvas::PathToUtf8(pdf), UltraIMSendOptions());
    CHECK(!refused.success && refused.code == UltraIMResultCode::CapabilityMissing);

    // ---- search, read -------------------------------------------------------
    UltraIMMessageSearch search;
    search.conversationId = withAlice;
    search.text = "two";
    auto page = Search(*bob, search);
    CHECK(page.messages.size() == 1 && page.messages[0].text == "two");
    search.text.clear();
    search.kind = UltraIMSearchKind::Videos;
    page = Search(*bob, search);
    CHECK(page.messages.size() == 1);
    UltraIMMessageSearch everywhere;
    everywhere.text = "two";
    UltraIMResult searchResult;
    Search(*bob, everywhere, &searchResult);
    CHECK(searchResult.code == UltraIMResultCode::CapabilityMissing);

    bobEvents.Clear();
    CHECK(bob->MarkRead(withAlice, std::string()));
    // (The read goes to the server on Bob's worker: once it is there.)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    CHECK(alice->SendText(withBob, "Read this", UltraIMSendOptions()));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& x) {
        return x.type == UltraIMEventType::ConversationUpdate && x.conversationId == withAlice && x.conversation.unreadCount == 1;
    }));
    bobEvents.Clear();
    CHECK(bob->MarkRead(withAlice, std::string()));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    CHECK(bobEvents.All(UltraIMEventType::ConversationUpdate).empty());   // read: nothing changed after

    // ---- a group ------------------------------------------------------------
    UltraIMConversation group;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) {
        alice->CreateGroup("ignored", {"@bob", "carol@" + host}, cb);
    }, group));
    CHECK(group.isGroup);
    CHECK(group.conversationId == "bob@" + host + " carol@" + host);
    CHECK(group.memberIds.size() == 3);
    CHECK(alice->SendText(group.conversationId, "Hello both", UltraIMSendOptions()));
    CHECK(carolEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hello both"); }, &e));
    CHECK(e.conversationId == "alice@" + host + " bob@" + host);
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "Hello both"); }, &e));
    CHECK(e.conversationId == "alice@" + host + " carol@" + host);
    // The one-to-one chat is not touched by it.
    CHECK(Fetch(*bob, withAlice).size() == 7);

    // ---- a status that answers nothing: a second thread, the same chat -------
    {
        UltraNetHttpRequest post;
        post.url = url + "/api/v1/statuses";
        post.method = UltraNetHttpMethod::Post;
        post.headers.Set("Authorization", "Bearer bob-token");
        post.headers.Set("Content-Type", "application/json");
        const std::string body = "{\"status\": \"@alice A new thread\", \"visibility\": \"direct\"}";
        post.body.assign(body.begin(), body.end());
        UltraNetResponse answer;
        UltraNet_HttpRequest(post, answer);
        CHECK(answer.statusCode == 200);
    }
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "A new thread"); }, &e));
    CHECK(e.conversationId == withBob);
    CHECK(e.message.replyTo.messageId.empty());
    {
        const auto both = Fetch(*alice, withBob);
        CHECK(both.size() == 8);
        if (both.size() == 8) {
            CHECK(both.front().text == "Hello Bob");
            CHECK(both.back().text == "A new thread");
        }
    }
    // Answering it goes on in both: the history keeps all of it.
    CHECK(alice->SendText(withBob, "In the new thread", UltraIMSendOptions()));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& x) { return IsReceived(x, "In the new thread"); }));
    CHECK(Fetch(*bob, withAlice).size() == 9);

    // ---- leave ----------------------------------------------------------------
    CHECK(WaitResult([&](UltraIMResultCallback cb) { carol->LeaveConversation("alice@" + host + " bob@" + host, cb); }, r));
    CHECK(r.success);
    CHECK(carolEvents.WaitFor([&](const UltraIMEvent& x) { return x.type == UltraIMEventType::ConversationRemove; }));

    // ---- a stored session: the next start signs in by itself ------------------
    alice->Shutdown();
    alice = std::make_unique<MastodonProvider>();
    alice->SetTimings(fast);
    aliceEvents.Clear();
    CHECK(alice->Initialize(aliceConfig, aliceEvents.Sink()));
    CHECK(alice->HasStoredSession());
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(UltraIMCredentials(), cb); }, r));
    CHECK(r.success);
    CHECK(alice->GetLocalUserId() == withAlice);
    CHECK(Fetch(*alice, withBob).size() == 9);

    alice->Shutdown();
    bob->Shutdown();
    carol->Shutdown();
}

void TestBrowserSignIn(const std::string& url, const fs::path& base) {
    const std::string host = HostOf(url);
    MastodonTimings fast;
    fast.pollMs = 300;
    fast.oauthTimeoutMs = 20000;
    const std::string redirect = "http://127.0.0.1:" + std::to_string(30000 + ::getpid() % 20000) + "/callback";

    Events events;
    auto dave = std::make_unique<MastodonProvider>();
    dave->SetTimings(fast);
    dave->SetRedirectUri(redirect);
    UltraIMConfig config;
    config.dataDirectory = UltraCanvas::PathToUtf8(base / "dave");
    CHECK(dave->Initialize(config, events.Sink()));

    // The browser: whoever opens the consent page (the fake approves at once).
    std::thread browser;
    UltraIMCredentials c;
    c.loginType = UltraIMLoginType::SsoOidc;
    c.server = url;
    UltraIMResult r;
    std::string consent;
    const bool answered = WaitResult([&](UltraIMResultCallback cb) {
        dave->SignIn(c, cb);
        UltraIMEvent e;
        if (events.WaitFor([](const UltraIMEvent& x) {
                return x.type == UltraIMEventType::AuthStepChange && x.authStep == UltraIMAuthStep::Browser;
            }, &e)) {
            consent = e.errorText;
            browser = std::thread([consent]() {
                UltraNetHttpRequest request;
                request.url = consent;
                request.method = UltraNetHttpMethod::Get;
                UltraNetResponse response;
                UltraNet_HttpRequest(request, response);
            });
        }
    }, r);
    if (browser.joinable()) browser.join();
    CHECK(answered);
    CHECK(consent.rfind(url + "/oauth/authorize?", 0) == 0);
    CHECK(consent.find("code_challenge=") != std::string::npos);
    CHECK(r.success);
    CHECK(dave->GetLocalUserId() == "dave@" + host);
    CHECK(dave->HasStoredSession());
    dave->Shutdown();

    // Again: the session kept, no browser.
    Events again;
    dave = std::make_unique<MastodonProvider>();
    dave->SetTimings(fast);
    dave->SetRedirectUri(redirect);
    CHECK(dave->Initialize(config, again.Sink()));
    c.server = url;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { dave->SignIn(c, cb); }, r));
    CHECK(r.success);
    CHECK(again.All(UltraIMEventType::AuthStepChange).empty());
    dave->Shutdown();

    // No server, nothing stored: asked which.
    Events none;
    auto nobody = std::make_unique<MastodonProvider>();
    UltraIMConfig empty;
    empty.dataDirectory = UltraCanvas::PathToUtf8(base / "nobody");
    CHECK(nobody->Initialize(empty, none.Sink()));
    CHECK(WaitResult([&](UltraIMResultCallback cb) { nobody->SignIn(UltraIMCredentials(), cb); }, r));
    CHECK(!r.success && r.code == UltraIMResultCode::InvalidArgument);
    nobody->Shutdown();
}
#endif

} // namespace

int main() {
    UltraNet_Initialize();
#if defined(_WIN32)
    std::printf("MastodonProviderTests: skipped on Windows\n");
#else
    const fs::path base = fs::temp_directory_path() / ("ultraim-mastodon-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    FakeServer server;
    if (!server.Start("python3")) {
        const bool required = !UltraCanvas::GetEnvUtf8("ULTRAIM_TEST_MASTODON_REQUIRED").empty();
        std::printf("MastodonProviderTests: %s (no python3)\n", required ? "FAIL" : "SKIP");
        return required ? 1 : 0;
    }
    std::printf("  server %s\n", server.url.c_str());
    TestEndToEnd(server.url, base);
    TestBrowserSignIn(server.url, base);
    server.Stop();
    fs::remove_all(base, ec);
#endif
    UltraNet_Shutdown();
    std::printf("MastodonProviderTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
