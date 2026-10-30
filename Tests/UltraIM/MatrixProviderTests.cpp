// Tests/UltraIM/MatrixProviderTests.cpp
// Matrix provider end to end against a real homeserver (Synapse, started by
// matrix_test_server.py): sign-in, a direct chat (invitation, accepting it by
// replying, the echo of our own message, receipts, typing, edit, redaction,
// reply), a picture, a video and a file sent and fetched back byte for byte,
// a group, and a stored session resumed with history paged back -
// all of it end-to-end encrypted when vodozemac is built in. Then the
// encryption itself: only ciphertext on the server, a second device that gets
// the keys from then on and the earlier ones through a key export, signing in
// again on the same device, and matrix-nio (matrix_nio_peer.py) as the other
// side of an encrypted room, key export files going both ways.
// Skips without a python that has matrix-synapse (ULTRAIM_MATRIX_PYTHON, else
// python3), unless ULTRAIM_TEST_MATRIX_REQUIRED is set (CI); the nio part
// skips without matrix-nio[e2e] in that python, unless
// ULTRAIM_TEST_MATRIX_NIO_REQUIRED is set.
// Version: 0.2.2 - the same in a room created unencrypted, so both paths run in one build
// Version: 0.2.1 - a picture, a video and a file each way
// Version: 0.2.0
// Last Modified: 2026-10-29
// Author: UltraCanvas Framework

#include "MatrixHttp.h"
#include "MatrixProvider.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <poll.h>
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

// A file of `size` bytes that is not all the same, under `path`.
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

bool WaitAttachment(const std::function<void(UltraIMAttachmentCallback)>& start, UltraIMAttachment& out,
                    UltraIMResult* result = nullptr, int seconds = 60) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false, ok = false;
    start([&](const UltraIMResult& r, const UltraIMAttachment& a) {
        std::lock_guard<std::mutex> lock(mutex);
        out = a;
        ok = r.success;
        if (result) *result = r;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(seconds), [&]() { return done; });
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

// A python program whose stdout is read a line at a time (matrix_nio_peer.py).
struct Peer {
    pid_t pid = -1;
    int out = -1;
    std::string buffered;

    bool Start(const std::string& python, const std::vector<std::string>& args) {
        int pipeFds[2];
        if (pipe(pipeFds) != 0) return false;
        pid = fork();
        if (pid == 0) {
            dup2(pipeFds[1], 1);
            close(pipeFds[0]);
            close(pipeFds[1]);
            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(python.c_str()));
            for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
            argv.push_back(nullptr);
            execvp(python.c_str(), argv.data());
            _exit(127);
        }
        close(pipeFds[1]);
        out = pipeFds[0];
        return pid > 0;
    }
    // The next line holding `key`, within `seconds`; "" when none came.
    std::string WaitLine(const std::string& key, int seconds) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        for (;;) {
            size_t nl;
            while ((nl = buffered.find('\n')) != std::string::npos) {
                const std::string line = buffered.substr(0, nl);
                buffered.erase(0, nl + 1);
                std::printf("    nio: %s\n", line.c_str());
                if (line.find("\"" + key + "\"") != std::string::npos) return line;
                if (line.find("\"error\"") != std::string::npos) return std::string();
            }
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
            if (left.count() <= 0 || out < 0) return std::string();
            pollfd p{out, POLLIN, 0};
            if (poll(&p, 1, static_cast<int>(left.count())) <= 0) return std::string();
            char chunk[512];
            const ssize_t n = read(out, chunk, sizeof(chunk));
            if (n <= 0) return std::string();
            buffered.append(chunk, static_cast<size_t>(n));
        }
    }
    void Stop() {
        if (out >= 0) close(out);
        out = -1;
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

#if !defined(_WIN32)
std::vector<UltraIMMessage> Fetch(MatrixProvider& p, const std::string& conversationId, int limit) {
    std::vector<UltraIMMessage> out;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    p.FetchMessages(conversationId, "", limit, [&](const UltraIMResult&, const std::vector<UltraIMMessage>& m) {
        std::lock_guard<std::mutex> lock(mutex);
        out = m;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(30), [&]() { return done; });
    return out;
}

bool Export(MatrixProvider& p, const std::string& passphrase, std::string& out) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false, ok = false;
    p.ExportEncryptionKeys(passphrase, [&](const UltraIMResult& r, const std::string& data) {
        std::lock_guard<std::mutex> lock(mutex);
        ok = r.success;
        out = data;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(60), [&]() { return done; });
    return ok;
}

bool Import(MatrixProvider& p, const std::string& data, const std::string& passphrase, int& imported) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false, ok = false;
    p.ImportEncryptionKeys(data, passphrase, [&](const UltraIMResult& r, int n) {
        std::lock_guard<std::mutex> lock(mutex);
        ok = r.success;
        imported = n;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(60), [&]() { return done; });
    return ok;
}

std::unique_ptr<MatrixProvider> Device(const fs::path& dir, Events& events) {
    auto p = std::make_unique<MatrixProvider>("matrix");
    MatrixTimings fast;
    fast.syncTimeoutMs = 2000;
    p->SetTimings(fast);
    p->SetKeyExportRounds(1000);
    UltraIMConfig config;
    config.dataDirectory = UltraCanvas::PathToUtf8(dir);
    p->Initialize(config, events.Sink());
    return p;
}

void TestEncryption(const std::string& url, const fs::path& base, const std::string& python) {
    std::printf("  encryption\n");
    UltraIMResult r;
    Events erinEvents, frankEvents;
    auto erin = Device(base / "erin1", erinEvents);
    auto frank = Device(base / "frank", frankEvents);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { erin->SignIn(Login(url, "erin"), cb); }, r) && r);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { frank->SignIn(Login(url, "frank"), cb); }, r) && r);
    CHECK(erin->IsEncryptionReady() && frank->IsEncryptionReady());
    const std::string erinDevice = erin->GetDeviceId();

    UltraIMConversation dm;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { frank->AddContact("@erin:localhost", cb); }, dm));
    CHECK(dm.isEncrypted);
    CHECK(erinEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == dm.conversationId;
    }));
    CHECK(erin->SendText(dm.conversationId, "hi frank", UltraIMSendOptions()));
    CHECK(frankEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "hi frank"); }));
    CHECK(frank->SendText(dm.conversationId, "secret one", UltraIMSendOptions()));
    UltraIMEvent one;
    CHECK(erinEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "secret one"); }, &one));
    // Frank knows Erin's device, keys checked.
    const auto devices = frank->GetDevicesOf("@erin:localhost");
    CHECK(devices.size() == 1 && devices[0].deviceId == erinDevice && !devices[0].conflicted);

    // What the server holds is ciphertext.
    {
        MatrixHttp raw;
        raw.SetBaseUrl(url);
        UltraCanvas::JSONValue login = UltraCanvas::JSONValue::MakeObject();
        login.Set("type", UltraCanvas::JSONValue("m.login.password"));
        login.Set("user", UltraCanvas::JSONValue("erin"));
        login.Set("password", UltraCanvas::JSONValue("erin-pass"));
        MatrixResponse in = raw.Await("POST", "/_matrix/client/v3/login", &login, 15000);
        CHECK(in.Ok());
        raw.SetToken(in.body["access_token"].GetString());
        MatrixResponse page = raw.Await("GET", "/_matrix/client/v3/rooms/" + PathSegment(dm.conversationId) +
                                                   "/messages?dir=b&limit=50", nullptr, 15000);
        CHECK(page.Ok());
        int encrypted = 0, clear = 0;
        for (const auto& e : page.body["chunk"].GetElements()) {
            if (e["type"].GetString() == "m.room.encrypted") ++encrypted;
            if (e["type"].GetString() == "m.room.message") ++clear;
        }
        CHECK(encrypted >= 2 && clear == 0);
        CHECK(UltraCanvas::JSON::Serialize(page.body).find("secret one") == std::string::npos);
        raw.Await("POST", "/_matrix/client/v3/logout", nullptr, 15000);
    }

    // A second device of Erin's: what comes after it, it reads; what came
    // before waits for a key, and opens when the keys of the first device
    // are imported.
    Events secondEvents;
    auto second = Device(base / "erin2", secondEvents);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { second->SignIn(Login(url, "erin"), cb); }, r) && r);
    CHECK(second->IsEncryptionReady() && second->GetDeviceId() != erinDevice);
    CHECK(secondEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == dm.conversationId;
    }));
    bool waiting = false;
    for (const auto& m : Fetch(*second, dm.conversationId, 20)) {
        if (m.messageId == one.message.messageId) waiting = m.text.find("Waiting for the key") == 0;
    }
    CHECK(waiting);
    // Frank's sync tells him of the new device before he writes again.
    std::this_thread::sleep_for(std::chrono::seconds(4));
    CHECK(frank->SendText(dm.conversationId, "secret two", UltraIMSendOptions()));
    CHECK(secondEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "secret two"); }));
    CHECK(erinEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "secret two"); }));
    CHECK(frank->GetDevicesOf("@erin:localhost").size() == 2);
    std::string keys;
    CHECK(Export(*erin, "erin's export", keys) && keys.find("BEGIN MEGOLM SESSION DATA") != std::string::npos);
    int imported = 0;
    CHECK(!Import(*second, keys, "not the passphrase", imported));
    CHECK(Import(*second, keys, "erin's export", imported) && imported >= 1);
    CHECK(secondEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageUpdate && e.message.messageId == one.message.messageId &&
               e.message.text == "secret one";
    }));

    // Signing in again with the password: the same device, its keys kept.
    erin->Shutdown();
    Events againEvents;
    auto again = Device(base / "erin1", againEvents);
    CHECK(WaitResult([&](UltraIMResultCallback cb) { again->SignIn(Login(url, "erin"), cb); }, r) && r);
    CHECK(again->GetDeviceId() == erinDevice && again->IsEncryptionReady());
    CHECK(frank->SendText(dm.conversationId, "secret three", UltraIMSendOptions()));
    CHECK(againEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "secret three"); }));
    bool history = false;
    for (const auto& m : Fetch(*again, dm.conversationId, 20)) history = history || m.text == "secret one";
    CHECK(history);

    // matrix-nio on the other side of an encrypted room.
    const std::string probe = python + " -c 'import nio, nio.crypto' >/dev/null 2>&1";
    if (std::system(probe.c_str()) != 0) {
        const bool required = !UltraCanvas::GetEnvUtf8("ULTRAIM_TEST_MATRIX_NIO_REQUIRED").empty();
        std::printf("  nio interop: %s (no matrix-nio[e2e] in that python)\n", required ? "FAIL" : "SKIP");
        CHECK(!required);
    } else {
        std::string ours;
        CHECK(Export(*frank, "shared pass", ours));
        const fs::path oursFile = base / "frank-keys.txt";
        const fs::path theirsFile = base / "nio-keys.txt";
        {
            std::ofstream f(oursFile, std::ios::binary);
            f << ours;
        }
        fs::create_directories(base / "nio-store");
        Peer nio;
        CHECK(nio.Start(python, {std::string(ULTRAIM_TEST_DIR) + "/matrix_nio_peer.py", "--url", url, "--user",
                                 "carol", "--password", "carol-pass", "--store",
                                 UltraCanvas::PathToUtf8(base / "nio-store"), "--expect", "hello nio", "--reply",
                                 "hello chatcube", "--import-file", UltraCanvas::PathToUtf8(oursFile),
                                 "--export-file", UltraCanvas::PathToUtf8(theirsFile), "--passphrase",
                                 "shared pass"}));
        CHECK(!nio.WaitLine("ready", 60).empty());
        UltraIMConversation mixed;
        CHECK(WaitConversation([&](UltraIMConversationCallback cb) {
            frank->CreateGroup("Mixed", {"@carol:localhost"}, cb);
        }, mixed));
        CHECK(mixed.isEncrypted);
        CHECK(!nio.WaitLine("joined", 60).empty());
        std::this_thread::sleep_for(std::chrono::seconds(3));
        CHECK(frank->SendText(mixed.conversationId, "hello nio", UltraIMSendOptions()));
        CHECK(!nio.WaitLine("received", 90).empty());
        CHECK(frankEvents.WaitFor([&](const UltraIMEvent& e) {
            return IsReceived(e, "hello chatcube") && e.conversationId == mixed.conversationId;
        }, nullptr, 60));
        CHECK(!nio.WaitLine("imported", 60).empty());     // our export, read by nio
        CHECK(!nio.WaitLine("exported", 60).empty());
        std::ifstream f(theirsFile, std::ios::binary);
        const std::string theirs((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        int fromNio = -1;
        CHECK(Import(*frank, theirs, "shared pass", fromNio) && fromNio >= 0);     // nio's, read by us
        std::printf("  nio interop: %d new sessions from nio's export\n", fromNio);
        nio.Stop();
    }

    again->Shutdown();
    second->Shutdown();
    frank->Shutdown();
}
#endif

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
    if (!hs.Start(base / "synapse", python, "alice,bob,carol,erin,frank")) {
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
    const bool e2ee = OlmAvailable();
    std::printf("  end-to-end encryption: %s\n", e2ee ? "on" : "not built in");
    if (e2ee) CHECK(alice->IsEncryptionReady() && bob->IsEncryptionReady());

    UltraIMConversation none;
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("bob", cb); }, none));

    // A direct chat: Bob sees an invitation and accepts it by answering.
    UltraIMConversation dm;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("@bob:localhost", cb); }, dm));
    CHECK(!dm.isGroup && dm.directContactId == "@bob:localhost" && dm.title == "bob");
    CHECK(dm.isEncrypted == e2ee);
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

    // A picture, a video and a file: uploaded (as ciphertext in an encrypted
    // room), the echo pointing at the file sent; Bob gets the picture by
    // itself, the video and the file when asked for, byte for byte.
    CHECK(UltraIM_HasCapability(alice->GetCapabilities(), UltraIMCapabilityFlags::ImageMessages));
    const std::vector<uint8_t> pictureBytes = WritePattern(base / "pic.png", 70000, 1);
    const std::vector<uint8_t> clipBytes = WritePattern(base / "clip.mp4", 200000, 2);
    const std::vector<uint8_t> notesBytes = WritePattern(base / "notes.pdf", 5000, 3);
    const std::string picture = UltraCanvas::PathToUtf8(base / "pic.png");
    const std::string clip = UltraCanvas::PathToUtf8(base / "clip.mp4");
    const std::string notes = UltraCanvas::PathToUtf8(base / "notes.pdf");
    UltraIMSendOptions oPic;
    oPic.clientMessageId = "alice-pic";
    CHECK(alice->SendImage(dm.conversationId, UltraCanvas::PathToUtf8(base / "missing.png"), "", oPic).code ==
          UltraIMResultCode::InvalidArgument);
    CHECK(alice->SendImage(dm.conversationId, picture, "look", oPic));
    UltraIMEvent picSent, picEcho, picGot, picHere;
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageDelivered && e.message.clientMessageId == "alice-pic" &&
               e.deliveryStatus == UltraIMDeliveryStatus::Sent;
    }, &picSent));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageReceive && e.message.clientMessageId == "alice-pic";
    }, &picEcho));
    CHECK(picEcho.message.type == UltraIMMessageType::Image && picEcho.message.text == "look");
    CHECK(picEcho.message.attachment.fileName == "pic.png" && picEcho.message.attachment.localPath == picture);
    CHECK(picEcho.message.attachment.isDownloaded && picEcho.message.attachment.mimeType == "image/png");
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageReceive && e.message.type == UltraIMMessageType::Image;
    }, &picGot));
    CHECK(picGot.message.text == "look" && picGot.message.attachment.fileName == "pic.png");
    CHECK(picGot.message.attachment.remoteUrl.rfind("mxc://", 0) == 0);
    CHECK(picGot.message.attachment.fileSizeBytes == static_cast<int64_t>(pictureBytes.size()));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageUpdate && e.message.messageId == picGot.message.messageId &&
               e.message.attachment.isDownloaded;
    }, &picHere));
    CHECK(!picHere.message.attachment.localPath.empty() && ReadBytes(picHere.message.attachment.localPath) == pictureBytes);
    CHECK(picHere.message.attachment.localPath != picture);     // Bob's copy, in his cache

    CHECK(alice->SendVideo(dm.conversationId, clip, "", UltraIMSendOptions()));
    UltraIMEvent clipGot;
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageReceive && e.message.type == UltraIMMessageType::Video;
    }, &clipGot));
    CHECK(clipGot.message.attachment.fileName == "clip.mp4" && !clipGot.message.attachment.isDownloaded);
    CHECK(clipGot.message.text.empty() && clipGot.message.attachment.mimeType == "video/mp4");
    UltraIMAttachment fetched;
    UltraIMResult fetchResult;
    CHECK(!WaitAttachment([&](UltraIMAttachmentCallback cb) { bob->DownloadAttachment(dm.conversationId, helloId, cb); },
                          fetched, &fetchResult));
    CHECK(fetchResult.code == UltraIMResultCode::NotFound);     // a text has none
    CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) {
        bob->DownloadAttachment(dm.conversationId, clipGot.message.messageId, cb);
    }, fetched));
    CHECK(fetched.isDownloaded && ReadBytes(fetched.localPath) == clipBytes && fetched.fileName == "clip.mp4");
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageUpdate && e.message.messageId == clipGot.message.messageId &&
               e.message.attachment.isDownloaded;
    }));
    // Asked again: from disk.
    UltraIMAttachment fetchedAgain;
    CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) {
        bob->DownloadAttachment(dm.conversationId, clipGot.message.messageId, cb);
    }, fetchedAgain) && fetchedAgain.localPath == fetched.localPath);

    CHECK(bob->SendFile(dm.conversationId, notes, UltraIMSendOptions()));
    UltraIMEvent notesGot;
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageReceive && e.message.type == UltraIMMessageType::File;
    }, &notesGot));
    CHECK(notesGot.message.attachment.fileName == "notes.pdf" && notesGot.message.attachment.mimeType == "application/pdf");
    CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) {
        alice->DownloadAttachment(dm.conversationId, notesGot.message.messageId, cb);
    }, fetched));
    CHECK(ReadBytes(fetched.localPath) == notesBytes);
    // History brings the picture back with its file.
    {
        const auto recent = Fetch(*bob, dm.conversationId, 20);
        int pictures = 0;
        for (const auto& m : recent) {
            if (m.type == UltraIMMessageType::Image && m.attachment.isDownloaded) ++pictures;
        }
        CHECK(pictures == 1);
    }

    // The same through a room created unencrypted: the file travels in the
    // clear and the preview picture rides as thumbnail_url.
    if (e2ee) {
        alice->SetCreateEncrypted(false);
        UltraIMConversation plain;
        CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->CreateGroup("Plain", {"@bob:localhost"}, cb); },
                               plain));
        alice->SetCreateEncrypted(true);
        CHECK(plain.isGroup && !plain.isEncrypted);
        CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
            return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == plain.conversationId;
        }));
        // Bob joins by answering into the room; only then does its timeline reach him.
        UltraIMSendOptions oJoin;
        oJoin.clientMessageId = "bob-plain-join";
        CHECK(bob->SendText(plain.conversationId, "here", oJoin));
        CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& e) {
            return IsReceived(e, "here") && e.conversationId == plain.conversationId;
        }));
        UltraIMSendOptions oPlain;
        oPlain.clientMessageId = "alice-plain-pic";
        CHECK(alice->SendImage(plain.conversationId, picture, "in the clear", oPlain));
        UltraIMEvent plainGot, plainHere;
        CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
            return e.type == UltraIMEventType::MessageReceive && e.conversationId == plain.conversationId &&
                   e.message.type == UltraIMMessageType::Image;
        }, &plainGot));
        CHECK(plainGot.message.text == "in the clear");
        CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
            return e.type == UltraIMEventType::MessageUpdate && e.message.messageId == plainGot.message.messageId &&
                   e.message.attachment.isDownloaded;
        }, &plainHere));
        CHECK(ReadBytes(plainHere.message.attachment.localPath) == pictureBytes);
        CHECK(alice->SendVideo(plain.conversationId, clip, "", UltraIMSendOptions()));
        UltraIMEvent plainClip;
        CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
            return e.type == UltraIMEventType::MessageReceive && e.conversationId == plain.conversationId &&
                   e.message.type == UltraIMMessageType::Video;
        }, &plainClip));
        UltraIMAttachment plainFetched;
        CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) {
            bob->DownloadAttachment(plain.conversationId, plainClip.message.messageId, cb);
        }, plainFetched));
        CHECK(ReadBytes(plainFetched.localPath) == clipBytes);
    }

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
    CHECK(group.isGroup && group.title == "Team" && group.isEncrypted == e2ee);
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
    CHECK(list.size() == (e2ee ? 3u : 2u));  // the DM, the Team group and, with E2EE built in, the Plain room

    again->Shutdown();
    bob->Shutdown();
    if (e2ee) TestEncryption(hs.url, base, python);
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
