// Apps/ChatCube/peer/main.cpp
// chatcube-peer - a ChatCube Direct identity on the command line, for trying
// ChatCube (and the relay) with a second person when there is none: it
// signs in, adds contacts, sends messages, and can answer what it receives.
//
//   chatcube-peer --relay http://127.0.0.1:8790 --data /tmp/bob --name Bob --create --print-id
//   chatcube-peer --data /tmp/bob --add <ChatCube ID> --send <ChatCube ID> "hello" --echo 600
//
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework / ULTRA OS

#include "DirectProvider.h"

#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraRTC/UltraRTCCore.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

void Usage() {
    std::printf("chatcube-peer - a ChatCube Direct identity on the command line\n\n"
                "  --relay <url>        relay server (remembered after the first sign-in)\n"
                "  --data <dir>         where the identity and messages are kept (required)\n"
                "  --name <name>        display name\n"
                "  --create             make a new identity if there is none\n"
                "  --print-id           print this identity's ChatCube ID\n"
                "  --add <id>           add a contact (may repeat)\n"
                "  --send <id> <text>   send a message to a contact (may repeat)\n"
                "  --echo <seconds>     stay online and answer every message for that long\n");
}

// Events arrive on the provider's threads; this tool handles them on main.
struct Queue {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<UltraIMEvent> events;
};

} // namespace

int main(int argc, char** argv) {
    UltraIMCredentials credentials;
    credentials.loginType = UltraIMLoginType::KeyPair;
    std::string dataDir;
    bool printId = false;
    int echoSeconds = 0;
    std::vector<std::string> adds;
    std::vector<std::pair<std::string, std::string>> sends;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                Usage();
                std::exit(EXIT_FAILURE);
            }
            return argv[++i];
        };
        if (arg == "--relay") credentials.server = next();
        else if (arg == "--data") dataDir = next();
        else if (arg == "--name") credentials.displayName = next();
        else if (arg == "--create") credentials.createAccount = true;
        else if (arg == "--print-id") printId = true;
        else if (arg == "--add") adds.push_back(next());
        else if (arg == "--send") {
            std::string to = next();
            sends.emplace_back(to, next());
        } else if (arg == "--echo") echoSeconds = std::atoi(next().c_str());
        else {
            Usage();
            return arg == "--help" ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }
    if (dataDir.empty()) {
        Usage();
        return EXIT_FAILURE;
    }

    UltraNet_Initialize();
    UltraRtc_Initialize();
    Queue queue;
    UltraIMDirect::DirectProvider provider;
    UltraIMConfig config;
    config.dataDirectory = dataDir;
    if (!provider.Initialize(config, [&queue](const UltraIMEvent& e) {
            std::lock_guard<std::mutex> lock(queue.mutex);
            queue.events.push_back(e);
            queue.changed.notify_all();
        })) {
        std::fprintf(stderr, "chatcube-peer: cannot open %s\n", dataDir.c_str());
        return EXIT_FAILURE;
    }

    // Each call waits for its answer.
    auto wait = [](const std::function<void(std::function<void(bool, std::string)>)>& call) {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false, ok = false;
        std::string message;
        call([&](bool success, std::string text) {
            std::lock_guard<std::mutex> lock(mutex);
            ok = success;
            message = std::move(text);
            done = true;
            cv.notify_all();
        });
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, std::chrono::seconds(30), [&]() { return done; });
        if (!ok) std::fprintf(stderr, "chatcube-peer: %s\n", done ? message.c_str() : "timed out");
        return ok;
    };

    if (!wait([&](auto done) {
            provider.SignIn(credentials, [done](const UltraIMResult& r) { done(r.success, r.message); });
        }))
        return EXIT_FAILURE;
    if (printId) std::printf("%s\n", provider.GetLocalUserId().c_str());
    std::fflush(stdout);

    for (const auto& id : adds) {
        wait([&](auto done) {
            provider.AddContact(id, [done](const UltraIMResult& r, const UltraIMConversation&) {
                done(r.success, r.message);
            });
        });
    }
    for (const auto& [to, text] : sends) {
        UltraIMSendOptions options;
        options.clientMessageId = UltraIM_NewClientMessageId();
        UltraIMResult r = provider.SendText(to, text, options);
        if (!r) std::fprintf(stderr, "chatcube-peer: not sent: %s\n", r.message.c_str());
    }

    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(std::max(echoSeconds, 3));
    while (std::chrono::steady_clock::now() < end) {
        UltraIMEvent e;
        {
            std::unique_lock<std::mutex> lock(queue.mutex);
            if (!queue.changed.wait_until(lock, end, [&]() { return !queue.events.empty(); })) break;
            e = queue.events.front();
            queue.events.pop_front();
        }
        if (e.type == UltraIMEventType::MessageReceive) {
            std::printf("[%s] %s: %s\n", e.conversationId.substr(0, 8).c_str(), e.message.senderName.c_str(),
                        e.message.text.c_str());
            std::fflush(stdout);
            if (echoSeconds > 0) {
                provider.MarkRead(e.conversationId, "");
                provider.SetTyping(e.conversationId, true);
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
                UltraIMSendOptions options;
                options.clientMessageId = UltraIM_NewClientMessageId();
                provider.SendText(e.conversationId, "You said: " + e.message.text, options);
            }
        } else if (e.type == UltraIMEventType::MessageDelivered) {
            std::printf("  %s -> %d\n", e.message.clientMessageId.substr(0, 8).c_str(),
                        static_cast<int>(e.deliveryStatus));
            std::fflush(stdout);
        }
    }
    provider.Shutdown();
    UltraRtc_Shutdown();
    UltraNet_Shutdown();
    return EXIT_SUCCESS;
}
