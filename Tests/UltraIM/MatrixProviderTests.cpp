// Tests/UltraIM/MatrixProviderTests.cpp
// Matrix provider end to end against a real homeserver (Synapse, started by
// matrix_test_server.py): sign-in, a direct chat (invitation, accepting it by
// replying, the echo of our own message, receipts, typing, edit, redaction,
// reply), a group, and a stored session resumed with history paged back.
// Skips without a python that has matrix-synapse (ULTRAIM_MATRIX_PYTHON, else
// python3), unless ULTRAIM_TEST_MATRIX_REQUIRED is set (CI).
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "MatrixProvider.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"

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

using namespace UltraIMMatrix;

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
    bool WaitFor(const std::function<bool(const UltraIMEvent&)>& match, UltraIMEvent* found = nullptr,
                 int seconds = 30) {
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

bool IsReceived(const UltraIMEvent& e, const std::string& text) {
    return e.type == UltraIMEventType::MessageReceive && e.message.text == text;
}

#if !defined(_WIN32)
// Synapse through Tests/UltraIM/matrix_test_server.py.
struct Homeserver {
    pid_t pid = -1;
    std::string url;

    bool Start(const fs::path& dir, const std::string& python, const std::string& users) {
        const std::string probe = python + " -c 'import synapse' >/dev/null 2>&1";
        if (std::system(probe.c_str()) != 0) return false;
        const int port = 21000 + static_cast<int>(::getpid() % 10000);
        int out[2];
        if (pipe(out) != 0) return false;
        pid = fork();
        if (pid == 0) {
            dup2(out[1], 1);
            close(out[0]);
            close(out[1]);
            const std::string script = std::string(ULTRAIM_TEST_DIR) + "/matrix_test_server.py";
            const std::string dirArg = UltraCanvas::PathToUtf8(dir);
            const std::string portArg = std::to_string(port);
            execlp(python.c_str(), python.c_str(), script.c_str(), "--dir", dirArg.c_str(), "--port", portArg.c_str(),
                   "--users", users.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        close(out[1]);
        // One line of JSON once it is up (it takes a few seconds).
        std::string line;
        char c = 0;
        while (read(out[0], &c, 1) == 1 && c != '\n') line += c;
        close(out[0]);
        const size_t at = line.find("\"url\": \"");
        if (at == std::string::npos) {
            std::fprintf(stderr, "homeserver did not start: %s\n", line.c_str());
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
#endif

UltraIMCredentials Login(const std::string& url, const std::string& user) {
    UltraIMCredentials c;
    c.loginType = UltraIMLoginType::UsernamePassword;
    c.server = url;
    c.username = user;
    c.password = user + "-pass";
    return c;
}

void TestEndToEnd() {
#if defined(_WIN32)
    std::printf("MatrixProviderTests: skipped on Windows\n");
#else
    const fs::path base = fs::temp_directory_path() / ("ultraim-matrix-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    const std::string python = UltraCanvas::GetEnvUtf8("ULTRAIM_MATRIX_PYTHON").empty()
                                   ? std::string("python3")
                                   : UltraCanvas::GetEnvUtf8("ULTRAIM_MATRIX_PYTHON");
    Homeserver hs;
    if (!hs.Start(base / "synapse", python, "alice,bob")) {
        const bool required = !UltraCanvas::GetEnvUtf8("ULTRAIM_TEST_MATRIX_REQUIRED").empty();
        std::printf("MatrixProviderTests: %s (no python with matrix-synapse: set ULTRAIM_MATRIX_PYTHON)\n",
                    required ? "FAIL" : "SKIP");
        CHECK(!required);
        return;
    }
    std::printf("  homeserver %s\n", hs.url.c_str());

    MatrixTimings fast;
    fast.syncTimeoutMs = 2000;
    Events aliceEvents, bobEvents;
    auto alice = std::make_unique<MatrixProvider>("matrix");
    auto bob = std::make_unique<MatrixProvider>("matrix");
    alice->SetTimings(fast);
    bob->SetTimings(fast);
    UltraIMConfig aliceConfig, bobConfig;
    aliceConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "alice");
    bobConfig.dataDirectory = UltraCanvas::PathToUtf8(base / "bob");
    CHECK(alice->Initialize(aliceConfig, aliceEvents.Sink()));
    CHECK(bob->Initialize(bobConfig, bobEvents.Sink()));
    CHECK(!alice->HasStoredSession());

    // Sign-in: a wrong password is refused; then both are in.
    UltraIMResult r;
    UltraIMCredentials wrong = Login(hs.url, "alice");
    wrong.password = "not-it";
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(wrong, cb); }, r));
    CHECK(!r && r.code == UltraIMResultCode::PermissionDenied);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { alice->SignIn(Login(hs.url, "alice"), cb); }, r) && r);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { bob->SignIn(Login(hs.url, "bob"), cb); }, r) && r);
    CHECK(alice->GetLocalUserId() == "@alice:localhost" && bob->GetLocalUserId() == "@bob:localhost");
    CHECK(alice->HasStoredSession());
    CHECK(alice->GetConnectionState() == UltraIMConnectionState::Connected);

    UltraIMConversation none;
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("bob", cb); }, none));

    // A direct chat: Bob sees an invitation and accepts it by answering.
    UltraIMConversation dm;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("@bob:localhost", cb); }, dm));
    CHECK(!dm.isGroup && dm.directContactId == "@bob:localhost" && dm.title == "bob");
    UltraIMEvent invite;
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == dm.conversationId &&
               e.conversation.lastMessageSnippet.find("Invitation") == 0;
    }, &invite));
    CHECK(invite.conversation.directContactId == "@alice:localhost");
    UltraIMConversation same;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("@bob:localhost", cb); }, same));
    CHECK(same.conversationId == dm.conversationId);        // one direct chat per person

    UltraIMSendOptions accept;
    accept.clientMessageId = "bob-1";
    CHECK(bob->SendText(dm.conversationId, "hi alice", accept));
    UltraIMEvent hi;
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "hi alice"); }, &hi));
    CHECK(hi.message.senderId == "@bob:localhost" && hi.message.senderName == "bob" && !hi.message.isOwn);

    // Our own message: sent, then its echo carries the client id.
    UltraIMSendOptions o1;
    o1.clientMessageId = "alice-1";
    CHECK(alice->SendText(dm.conversationId, "hello bob", o1));
    UltraIMEvent sent, echo;
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageDelivered && e.message.clientMessageId == "alice-1" &&
               e.deliveryStatus == UltraIMDeliveryStatus::Sent;
    }, &sent));
    CHECK(!sent.message.messageId.empty() && sent.message.messageId[0] == '$');
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return IsReceived(e, "hello bob") && e.message.clientMessageId == "alice-1";
    }, &echo));
    CHECK(echo.message.isOwn && echo.message.messageId == sent.message.messageId);
    UltraIMEvent got;
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "hello bob"); }, &got));
    const std::string helloId = got.message.messageId;

    // Bob reads it: Alice's message turns read.
    CHECK(bob->MarkRead(dm.conversationId, ""));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageDelivered && e.message.clientMessageId == "alice-1" &&
               e.deliveryStatus == UltraIMDeliveryStatus::Read;
    }));

    // Typing.
    CHECK(alice->SetTyping(dm.conversationId, true));
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::TypingChange && e.contactId == "@alice:localhost" && e.isTyping;
    }));
    CHECK(alice->SetTyping(dm.conversationId, false));

    // An edit and a redaction reach Bob.
    CHECK(alice->EditMessage(dm.conversationId, helloId, "hello bob!"));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageUpdate && e.message.messageId == helloId &&
               e.message.text == "hello bob!" && e.message.isEdited;
    }));
    UltraIMSendOptions o2;
    o2.clientMessageId = "alice-2";
    CHECK(alice->SendText(dm.conversationId, "oops", o2));
    UltraIMEvent oops;
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "oops"); }, &oops));
    CHECK(alice->DeleteMessage(dm.conversationId, oops.message.messageId));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageDelete && e.message.messageId == oops.message.messageId;
    }));

    // A reply knows what it answers.
    UltraIMSendOptions reply;
    reply.clientMessageId = "bob-2";
    reply.replyToMessageId = helloId;
    CHECK(bob->SendText(dm.conversationId, "hello to you", reply));
    UltraIMEvent answered;
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "hello to you"); }, &answered));
    CHECK(answered.message.replyTo.messageId == helloId && answered.message.replyTo.senderId == "@alice:localhost");

    // A group: Bob is invited and answers into it.
    UltraIMConversation group;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) {
        alice->CreateGroup("Team", {"@bob:localhost"}, cb);
    }, group));
    CHECK(group.isGroup && group.title == "Team");
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == group.conversationId &&
               e.conversation.title == "Team" && e.conversation.isGroup;
    }));
    UltraIMSendOptions o3;
    o3.clientMessageId = "bob-3";
    CHECK(bob->SendText(group.conversationId, "hi team", o3));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& e) {
        return IsReceived(e, "hi team") && e.conversationId == group.conversationId;
    }));

    // Plenty of history, then a fresh start from the stored session with a
    // short first sync: the older messages are paged back on request.
    for (int i = 0; i < 30; ++i) {
        UltraIMSendOptions o;
        o.clientMessageId = "bulk-" + std::to_string(i);
        alice->SendText(dm.conversationId, "message " + std::to_string(i), o);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "message 29"); }));
    alice->Shutdown();
    Events againEvents;
    auto again = std::make_unique<MatrixProvider>("matrix");
    MatrixTimings shortFirst = fast;
    shortFirst.initialTimelineLimit = 5;
    again->SetTimings(shortFirst);
    CHECK(again->Initialize(aliceConfig, againEvents.Sink()));
    CHECK(again->HasStoredSession());
    UltraIMCredentials resume;
    resume.loginType = UltraIMLoginType::UsernamePassword;
    CHECK(WaitResult([&](UltraIMResultCallback cb) { again->SignIn(resume, cb); }, r) && r);
    CHECK(again->GetLocalUserId() == "@alice:localhost");
    CHECK(againEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == dm.conversationId;
    }));
    std::vector<UltraIMMessage> page;
    std::mutex pageMutex;
    std::condition_variable pageCv;
    bool pageDone = false;
    again->FetchMessages(dm.conversationId, "", 25, [&](const UltraIMResult&, const std::vector<UltraIMMessage>& m) {
        std::lock_guard<std::mutex> lock(pageMutex);
        page = m;
        pageDone = true;
        pageCv.notify_all();
    });
    {
        std::unique_lock<std::mutex> lock(pageMutex);
        pageCv.wait_for(lock, std::chrono::seconds(30), [&]() { return pageDone; });
    }
    std::printf("  history: %zu messages paged back\n", page.size());
    CHECK(page.size() >= 25);
    if (!page.empty()) CHECK(page.back().text == "message 29");
    bool ordered = true;
    for (size_t i = 1; i < page.size(); ++i) ordered = ordered && page[i - 1].timestampMs <= page[i].timestampMs;
    CHECK(ordered);

    std::vector<UltraIMConversation> list;
    again->FetchConversations([&](const UltraIMResult&, const std::vector<UltraIMConversation>& l) { list = l; });
    CHECK(list.size() == 2);

    again->Shutdown();
    bob->Shutdown();
    hs.Stop();
    fs::remove_all(base, ec);
#endif
}

} // namespace

int main() {
    UltraNet_Initialize();
    TestEndToEnd();
    std::printf("MatrixProviderTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
