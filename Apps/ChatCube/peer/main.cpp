// Apps/ChatCube/peer/main.cpp
// chatcube-peer - a ChatCube Direct identity on the command line, for trying
// ChatCube (and the relay) with a second person when there is none: it
// signs in, adds contacts, sends messages, and can answer what it receives
// - messages, voice and video calls.
//
//   chatcube-peer --relay http://127.0.0.1:8790 --data /tmp/bob --name Bob --create --print-id
//   chatcube-peer --data /tmp/bob --add <ChatCube ID> --send <ChatCube ID> "hello" --echo 600
//   chatcube-peer --data /tmp/bob --answer-calls --echo 600      (answers with a tone)
//   chatcube-peer --data /tmp/bob --call <ChatCube ID> --video --echo 60 --devices
//   chatcube-peer --data /tmp/ben --name Ben --create --echo 600     (no relay: the local network)
//
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework / ULTRA OS

#include "DirectProvider.h"

#include "UltraIM/UltraIMCore.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraRTC/UltraRTCCore.h"

#include <atomic>
#include <chrono>
#include <cmath>
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

constexpr double kPi = 3.14159265358979323846;   // M_PI is not standard C++

void Usage() {
    std::printf("chatcube-peer - a ChatCube Direct identity on the command line\n\n"
                "  --relay <url>        relay server (remembered after the first sign-in);\n"
                "                       none at all: the local network only\n"
                "  --lan                also find people on the local network (with a relay)\n"
                "  --connect <host:port> reach a ChatCube on the local network by address\n"
                "  --data <dir>         where the identity and messages are kept (required)\n"
                "  --name <name>        display name\n"
                "  --create             make a new identity if there is none\n"
                "  --print-id           print this identity's ChatCube ID\n"
                "  --add <id>           add a contact (may repeat)\n"
                "  --send <id> <text>   send a message to a contact (may repeat)\n"
                "  --echo <seconds>     stay online and answer every message for that long\n"
                "  --answer-calls       answer incoming voice calls\n"
                "  --call <id>          call a contact (--video: a video call)\n"
                "  --devices            calls use the microphone, speaker and camera\n"
                "                       (default: a tone and a moving test picture)\n");
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
    bool answerCalls = false;
    bool useDevices = false;
    std::string callTo;
    std::vector<std::pair<std::string, int>> connects;
    bool callVideo = false;

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
        else if (arg == "--answer-calls") answerCalls = true;
        else if (arg == "--call") callTo = next();
        else if (arg == "--devices") useDevices = true;
        else if (arg == "--video") callVideo = true;
        else if (arg == "--lan") credentials.localNetwork = true;
        else if (arg == "--connect") {
            const std::string address = next();
            const size_t colon = address.rfind(':');
            if (colon == std::string::npos) {
                Usage();
                return EXIT_FAILURE;
            }
            connects.emplace_back(address.substr(0, colon), std::atoi(address.c_str() + colon + 1));
        }
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
    provider.SetCallUseDevices(useDevices);

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
    if (provider.GetLanPort() > 0) std::printf("on the local network, port %d\n", provider.GetLanPort());
    std::fflush(stdout);
    for (const auto& [host, port] : connects) provider.AddLanAddress(host, port);

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

    // Without devices an active call carries a quiet 440 Hz tone, so the
    // other side has something to hear.
    std::atomic<UltraRtcHandle> toneTrack{UltraRtcInvalidHandle};
    std::atomic<bool> toneStop{false};
    // ... and a video call a moving test picture: colour bars with a band
    // sweeping across.
    std::atomic<UltraRtcHandle> pictureTrack{UltraRtcInvalidHandle};
    std::thread picture([&]() {
        UltraRtcVideoFrame frame;
        frame.width = 640;
        frame.height = 480;
        frame.rgba.resize(640 * 480 * 4);
        static const uint8_t bars[7][3] = {{235, 235, 235}, {235, 235, 16}, {16, 235, 235}, {16, 235, 16},
                                           {235, 16, 235}, {235, 16, 16}, {16, 16, 235}};
        int tick = 0;
        while (!toneStop) {
            const UltraRtcHandle track = pictureTrack.load();
            if (track != UltraRtcInvalidHandle) {
                const int band = (tick * 6) % frame.width;
                for (int y = 0; y < frame.height; ++y) {
                    for (int x = 0; x < frame.width; ++x) {
                        uint8_t* p = frame.rgba.data() + (static_cast<size_t>(y) * frame.width + static_cast<size_t>(x)) * 4;
                        const uint8_t* c = bars[x * 7 / frame.width];
                        const bool inBand = x >= band && x < band + 40;
                        p[0] = inBand ? 30 : c[0];
                        p[1] = inBand ? 30 : c[1];
                        p[2] = inBand ? 30 : c[2];
                        p[3] = 255;
                    }
                }
                UltraRtc_SendVideoFrame(track, frame);
                ++tick;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
    });
    std::thread tone([&]() {
        UltraRtcAudioFrame frame;
        frame.samples.resize(960);
        size_t phase = 0;
        while (!toneStop) {
            const UltraRtcHandle track = toneTrack.load();
            if (track != UltraRtcInvalidHandle) {
                for (auto& v : frame.samples)
                    v = static_cast<int16_t>(3000.0 * std::sin(2.0 * kPi * 440.0 * static_cast<double>(phase++) / 48000.0));
                UltraRtc_SendAudioFrame(track, frame);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
    if (!callTo.empty()) {
        // Signalling rides the relay's socket, which opens just after sign-in.
        for (int i = 0; i < 100 && !provider.CanCall(callTo, false); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::string callId;
        UltraIMResult r = provider.StartCall(callTo, callVideo, callId);
        if (!r) std::fprintf(stderr, "chatcube-peer: cannot call: %s\n", r.message.c_str());
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
        } else if (e.type == UltraIMEventType::CallReceive) {
            std::printf("call from %s\n", e.contactId.substr(0, 8).c_str());
            std::fflush(stdout);
            if (answerCalls) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                provider.AcceptCall(e.callId, e.isVideo);
            }
        } else if (e.type == UltraIMEventType::CallStateChange) {
            static const char* const names[] = {"ringing", "connecting", "active", "ended", "failed"};
            std::printf("call %s %s\n", names[static_cast<int>(e.callState)], e.errorText.c_str());
            std::fflush(stdout);
            if (e.callState == UltraIMCallState::Active && !useDevices) {
                toneTrack = provider.GetCallAudioTrack(e.callId);
                pictureTrack = provider.GetCallVideoTrack(e.callId);
            }
            if (e.callState == UltraIMCallState::Ended || e.callState == UltraIMCallState::Failed) {
                toneTrack = UltraRtcInvalidHandle;
                pictureTrack = UltraRtcInvalidHandle;
            }
        } else if (e.type == UltraIMEventType::MessageDelivered) {
            std::printf("  %s -> %d\n", e.message.clientMessageId.substr(0, 8).c_str(),
                        static_cast<int>(e.deliveryStatus));
            std::fflush(stdout);
        }
    }
    toneStop = true;
    tone.join();
    picture.join();
    provider.Shutdown();
    UltraRtc_Shutdown();
    UltraNet_Shutdown();
    return EXIT_SUCCESS;
}
