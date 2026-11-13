// Tests/UltraWebRealtimeTest.cpp
// Apps' WebSockets and peer connections in UltraWeb (element ABI v3,
// uc_ws_* and uc_rtc_* in UltraWeb/guest/ultraweb.h):
//   - the rules: which addresses an app may open a socket to and what goes
//     with it (SocketRules), and what a peer configuration may say
//     (RtcRules);
//   - guests written as WebAssembly text against fake services: a socket
//     opened, refused, read until empty, written, closed by either side or
//     lost, and a peer whose descriptions, candidates, channels and messages
//     the app polls - and both closed when the guest goes;
//   - calls (ABI v4): tracks, the offer, the camera and the microphone
//     behind the user's answer (asked once, kept per origin in the
//     permission store), pictures into a VideoTile, a track the other side
//     added, and nothing reaching a tile once the guest is gone;
//   - the real services: UltraNet against a loopback WebSocket server that
//     checks the Origin header and the subprotocol (POSIX only), and two
//     UltraRTC peers that connect in this process and exchange data-channel
//     messages, then video pictures (when the build has the engine).
// Version: 0.2.0 - calls: tracks, devices, permissions, VideoTile, real video
// Version: 0.1.0
// Last Modified: 2026-11-12
// Author: UltraCanvas Framework / ULTRA OS

#include "host/UltraWebFetch.h"
#include "host/UltraWebGuest.h"
#include "host/UltraWebPermissions.h"
#include "host/UltraWebRtc.h"
#include "host/UltraWebSocket.h"

#include "ultraweb.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCanvasVideoTile.h"
#include "UltraNet/UltraNetCore.h"
#include "WasmHost/UltraCanvasWasmHost.h"

#ifdef ULTRAWEB_HAS_RTC
#include <UltraRTC/UltraRTC.h>
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0      // macOS: SIGPIPE is ignored in main instead
#endif
#endif

using namespace UltraCanvas;
using namespace UltraWeb;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

namespace {

std::vector<uint8_t> Bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }
std::string Text(const std::vector<uint8_t>& b) { return std::string(b.begin(), b.end()); }

constexpr const char* kAppAddress = "https://app.example/dir/index.wasm";

// ===== THE "UI THREAD" =======================================================
// Tasks posted from any thread, run by Pump on this one.

std::mutex g_lock;
std::condition_variable g_wake;
std::deque<std::function<void()>> g_tasks;

void Post(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> guard(g_lock);
        g_tasks.push_back(std::move(task));
    }
    g_wake.notify_all();
}

// Runs posted tasks until `done` holds or `seconds` pass.
bool Pump(const std::function<bool()>& done, int seconds = 20) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!done()) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> guard(g_lock);
            if (!g_wake.wait_until(guard, until, [] { return !g_tasks.empty(); })) return done();
            task = std::move(g_tasks.front());
            g_tasks.pop_front();
        }
        task();
    }
    return true;
}

// ===== THE RULES =============================================================

void TestSocketRules() {
    std::printf("socket rules\n");
    SocketRequest r;
    std::string why;
    CHECK(SocketRules::Prepare(kAppAddress, "wss://chat.example/room", "chat, v2", r, why) == UC_OK);
    CHECK(r.url == "wss://chat.example/room" && r.origin == "https://app.example");
    CHECK(r.protocols.size() == 2 && r.protocols[0] == "chat" && r.protocols[1] == "v2");
    CHECK(SocketRules::Prepare(kAppAddress, "/live#top", "", r, why) == UC_OK && r.url == "wss://app.example/live");
    CHECK(SocketRules::Prepare(kAppAddress, "feed", "", r, why) == UC_OK && r.url == "wss://app.example/dir/feed");
    CHECK(SocketRules::Prepare(kAppAddress, "https://other.example/s", "", r, why) == UC_OK &&
          r.url == "wss://other.example/s");
    CHECK(SocketRules::Prepare(kAppAddress, "ws://plain.example/", "", r, why) == UC_ERR_DENIED);
    CHECK(why.find("https app") != std::string::npos);
    CHECK(SocketRules::Prepare("http://app.example/a.wasm", "ws://plain.example/", "", r, why) == UC_OK &&
          r.origin == "http://app.example");
    CHECK(SocketRules::Prepare(kAppAddress, "ftp://files.example/", "", r, why) == UC_ERR_DENIED);
    CHECK(SocketRules::Prepare(kAppAddress, "wss://user:pw@chat.example/", "", r, why) == UC_ERR_DENIED);
    CHECK(SocketRules::Prepare(kAppAddress, "wss://chat.example/", "bad proto", r, why) == UC_ERR_STATE);
    CHECK(SocketRules::Prepare(kAppAddress, "wss://chat.example/", "a,a", r, why) == UC_ERR_STATE);
    CHECK(SocketRules::Prepare(kAppAddress, "", "", r, why) == UC_ERR_STATE);
    // An app from a file has no origin, and nothing to read a relative address against.
    CHECK(SocketRules::Prepare("/home/me/app.wasm", "wss://chat.example/", "", r, why) == UC_OK && r.origin == "null");
    CHECK(SocketRules::Prepare("/home/me/app.wasm", "room", "", r, why) == UC_ERR_STATE);
}

void TestRtcConfig() {
    std::printf("peer configuration rules\n");
    RtcConfig c;
    std::string why;
    CHECK(RtcRules::ParseConfig("", kAppAddress, c, why) == UC_OK && c.iceServers.empty() && c.relayOnly);
    CHECK(RtcRules::ParseConfig("{}", "/home/me/app.wasm", c, why) == UC_OK && !c.relayOnly);
    CHECK(RtcRules::ParseConfig(R"({"iceServers":[{"urls":"stun:s.example:3478"},
        {"urls":["turn:t.example","turns:t.example:5349"],"username":"u","credential":"p"}]})", kAppAddress, c, why) == UC_OK);
    CHECK(c.iceServers.size() == 3);
    if (c.iceServers.size() == 3) {
        CHECK(c.iceServers[0].url == "stun:s.example:3478" && c.iceServers[0].username.empty());
        CHECK(c.iceServers[1].url == "turn:t.example" && c.iceServers[1].username == "u" && c.iceServers[1].credential == "p");
        CHECK(c.iceServers[2].url == "turns:t.example:5349");
    }
    CHECK(RtcRules::ParseConfig("{oops", kAppAddress, c, why) == UC_ERR_STATE);
    CHECK(RtcRules::ParseConfig(R"({"iceServers":[{"urls":"http://x.example"}]})", kAppAddress, c, why) == UC_ERR_STATE);
    CHECK(RtcRules::ParseConfig(R"({"iceServers":[{}]})", kAppAddress, c, why) == UC_ERR_STATE);
    std::string many = R"({"iceServers":[)";
    for (int i = 0; i < 9; ++i) many += std::string(i ? "," : "") + R"({"urls":"stun:s.example"})";
    many += "]}";
    CHECK(RtcRules::ParseConfig(many, kAppAddress, c, why) == UC_ERR_LIMIT);
}

// ===== FAKE SERVICES =========================================================

struct FakeConnection : SocketConnection {
    SocketEvents events;
    std::vector<std::string> sent;        // "t:..." or "b:..."
    int closedWith = -1;
    std::string closeReason;
    bool open = false;
    bool Send(const std::vector<uint8_t>& data, bool binary) override {
        if (!open) return false;
        sent.push_back((binary ? "b:" : "t:") + Text(data));
        return true;
    }
    void Close(int code, const std::string& reason) override {
        closedWith = code;
        closeReason = reason;
        open = false;
    }
};

struct FakeSockets {
    std::vector<SocketRequest> requests;
    std::vector<std::shared_ptr<FakeConnection>> connections;
    void Install(GuestServices& services) {
        services.webSocket = [this](const SocketRequest& request, SocketEvents events) {
            requests.push_back(request);
            auto connection = std::make_shared<FakeConnection>();
            connection->events = std::move(events);
            connections.push_back(connection);
            return std::static_pointer_cast<SocketConnection>(connection);
        };
    }
    // As a real service would: nothing reaches a socket the app closed.
    bool Live(size_t i) const { return i < connections.size() && connections[i]->closedWith < 0; }
    void Open(size_t i) {
        if (!Live(i)) return;
        connections[i]->open = true;
        connections[i]->events.onOpen();
    }
    void Message(size_t i, const std::string& data, bool binary = false) {
        if (Live(i)) connections[i]->events.onMessage(Bytes(data), binary);
    }
    void Closed(size_t i, int code) {
        if (Live(i)) connections[i]->events.onClose(code, "");
    }
    void Error(size_t i, const std::string& error, bool tooLarge) {
        if (Live(i)) connections[i]->events.onError(error, tooLarge);
    }
};

struct FakePeer : RtcPeer {
    std::vector<std::string> calls;
    std::vector<std::string> sent;
    uint64_t nextChannel = 100;
    std::vector<uint64_t> open;
    bool closed = false;
    int32_t SetRemote(uint32_t type, const std::string& sdp) override {
        calls.push_back("remote " + std::to_string(type) + " " + sdp);
        return UC_OK;
    }
    int32_t AddCandidate(const std::string& candidate, const std::string& mid) override {
        calls.push_back("candidate " + mid + " " + candidate);
        return UC_OK;
    }
    int32_t OpenChannel(const std::string& label, bool ordered, int32_t maxRetransmits, uint64_t& out) override {
        out = nextChannel++;
        calls.push_back("channel " + label + " " + (ordered ? "ordered" : "unordered") + " " + std::to_string(maxRetransmits));
        return UC_OK;
    }
    int32_t Send(uint64_t channel, const std::vector<uint8_t>& data, bool binary) override {
        if (std::find(open.begin(), open.end(), channel) == open.end()) return UC_ERR_STATE;
        sent.push_back(std::to_string(channel) + (binary ? " b:" : " t:") + Text(data));
        return UC_OK;
    }
    int32_t CloseChannel(uint64_t channel) override {
        calls.push_back("close channel " + std::to_string(channel));
        return UC_OK;
    }
    void Close() override { closed = true; }

    // Calls: recorded, the sinks kept so the test can feed pictures.
    std::map<uint64_t, RtcFrameSink> sinks;
    std::map<uint64_t, RtcFrameSink> selfViews;
    int32_t AddTrack(bool video, const std::string& mid, uint32_t direction, const RtcVideoConfig& config,
                     uint64_t& out) override {
        out = nextChannel++;
        std::string call = std::string("track ") + (video ? "video " : "audio ") + mid + " " + std::to_string(direction);
        if (video) call += " " + std::to_string(config.width) + "x" + std::to_string(config.height) + "@" +
                           std::to_string(config.fps) + "/" + std::to_string(config.kbps);
        calls.push_back(call);
        return UC_OK;
    }
    int32_t Offer() override { calls.push_back("offer"); return UC_OK; }
    int32_t SetMicrophone(uint64_t track, bool on) override {
        calls.push_back("mic " + std::to_string(track) + (on ? " on" : " off"));
        return UC_OK;
    }
    int32_t SetCamera(uint64_t track, bool on, RtcFrameSink selfView) override {
        calls.push_back("camera " + std::to_string(track) + (on ? " on" : " off") + (selfView ? " view" : ""));
        selfViews[track] = std::move(selfView);
        return UC_OK;
    }
    int32_t SetPlayback(uint64_t track, bool on) override {
        calls.push_back("playback " + std::to_string(track) + (on ? " on" : " off"));
        return UC_OK;
    }
    int32_t SetTrackEnabled(uint64_t track, bool enabled) override {
        calls.push_back("enabled " + std::to_string(track) + (enabled ? " on" : " off"));
        return UC_OK;
    }
    int32_t ShowVideo(uint64_t track, RtcFrameSink sink) override {
        calls.push_back("show " + std::to_string(track) + (sink ? " sink" : " none"));
        sinks[track] = std::move(sink);
        return UC_OK;
    }
};

struct FakeRtc {
    std::vector<std::shared_ptr<FakePeer>> peers;
    std::vector<RtcConfig> configs;
    std::vector<std::function<void(RtcEvent)>> sinks;
    void Install(GuestServices& services) {
        services.rtc = [this](const RtcConfig& config, std::function<void(RtcEvent)> onEvent, std::string&) {
            auto peer = std::make_shared<FakePeer>();
            peers.push_back(peer);
            configs.push_back(config);
            sinks.push_back(std::move(onEvent));
            return std::static_pointer_cast<RtcPeer>(peer);
        };
    }
    void Emit(size_t peer, uint32_t kind, uint64_t channel, uint32_t number, const std::string& text) {
        if (peer >= peers.size() || peers[peer]->closed) return;
        RtcEvent event;
        event.kind = kind;
        event.channel = channel;
        event.number = number;
        event.text = text;
        sinks[peer](event);
    }
};

struct Harness {
    std::shared_ptr<UltraCanvasContainer> root = CreateContainer("rtRoot", 0, 0, 800, 600);
    // The fakes before the guest: it closes sockets and peers as it goes.
    FakeSockets sockets;
    FakeRtc rtc;
    // What the app asked the user for, with the bar's answer to give.
    std::vector<std::pair<std::string, std::function<void(bool)>>> asked;
    bool canAsk = true;
    std::vector<std::string> logs;
    std::string failure;
    std::unique_ptr<UltraWebGuest> guest;

    bool Start(const std::string& wat, bool withServices = true) {
        GuestOptions options;
        options.address = kAppAddress;
        options.onLog = [this](const std::string& line) { logs.push_back(line); };
        options.onFailure = [this](const std::string& message) { failure = message; };
        options.defer = [](std::function<void()> task) { task(); };
        if (withServices) {
            sockets.Install(options.services);
            rtc.Install(options.services);
            if (canAsk) {
                options.services.askPermission = [this](const std::string& device, std::function<void(bool)> answer) {
                    asked.emplace_back(device, std::move(answer));
                };
            }
        }
        std::string error;
        guest = UltraWebGuest::Start(Bytes(wat), root, std::move(options), error);
        if (!guest) std::printf("  start failed: %s\n", error.c_str());
        return guest != nullptr;
    }
    std::vector<std::string> AppLogs() {
        std::vector<std::string> app;
        for (const std::string& line : logs)
            if (line.rfind("UltraWeb: ", 0) != 0) app.push_back(line);
        logs.clear();
        return app;
    }
};

bool SameLines(const std::vector<std::string>& got, const std::vector<std::string>& want) {
    if (got == want) return true;
    std::printf("  log lines differ:\n");
    for (size_t i = 0; i < std::max(got.size(), want.size()); ++i)
        std::printf("    %-24s %s\n", i < got.size() ? got[i].c_str() : "-", i < want.size() ? want[i].c_str() : "-");
    return false;
}

// The imports these guests use, and $say: logs "<tag> <value>".
const char* const kPrelude = R"(
  (import "ultracanvas" "uc_log" (func $log (param i32 i32)))
  (import "ultracanvas" "uc_ws_open" (func $ws_open (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_ws_state" (func $ws_state (param i32) (result i32)))
  (import "ultracanvas" "uc_ws_send" (func $ws_send (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_ws_receive" (func $ws_receive (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_ws_close_code" (func $ws_close_code (param i32) (result i32)))
  (import "ultracanvas" "uc_ws_close" (func $ws_close (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_rtc_peer_open" (func $rtc_peer_open (param i32 i32) (result i32)))
  (import "ultracanvas" "uc_rtc_peer_close" (func $rtc_peer_close (param i32) (result i32)))
  (import "ultracanvas" "uc_rtc_set_remote" (func $rtc_set_remote (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_rtc_add_candidate" (func $rtc_add_candidate (param i32 i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_rtc_channel_open" (func $rtc_channel_open (param i32 i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_rtc_send" (func $rtc_send (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_rtc_channel_close" (func $rtc_channel_close (param i32) (result i32)))
  (import "ultracanvas" "uc_rtc_poll" (func $rtc_poll (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_rtc_audio_add" (func $rtc_audio_add (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_rtc_video_add" (func $rtc_video_add (param i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_rtc_offer" (func $rtc_offer (param i32) (result i32)))
  (import "ultracanvas" "uc_rtc_microphone" (func $rtc_microphone (param i32 i32) (result i32)))
  (import "ultracanvas" "uc_rtc_camera" (func $rtc_camera (param i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_rtc_playback" (func $rtc_playback (param i32 i32) (result i32)))
  (import "ultracanvas" "uc_rtc_video_show" (func $rtc_video_show (param i32 i32) (result i32)))
  (import "ultracanvas" "uc_rtc_track_enabled" (func $rtc_track_enabled (param i32 i32) (result i32)))
  (import "ultracanvas" "uc_create" (func $create (param i32 i32) (result i32)))
  (import "ultracanvas" "uc_insert" (func $insert (param i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_release" (func $release (param i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "uc_abi_version") (result i32) (i32.const 4))
  ;; A signed decimal at $dst; returns its length.
  (func $num (param $v i32) (param $dst i32) (result i32)
    (local $u i32) (local $n i32) (local $i i32) (local $t i32)
    (local.set $u (local.get $v))
    (if (i32.lt_s (local.get $v) (i32.const 0)) (then (local.set $u (i32.sub (i32.const 0) (local.get $v)))))
    (loop $emit
      (i32.store8 (i32.add (local.get $dst) (local.get $n))
                  (i32.add (i32.const 48) (i32.rem_u (local.get $u) (i32.const 10))))
      (local.set $n (i32.add (local.get $n) (i32.const 1)))
      (local.set $u (i32.div_u (local.get $u) (i32.const 10)))
      (br_if $emit (local.get $u)))
    (if (i32.lt_s (local.get $v) (i32.const 0))
      (then (i32.store8 (i32.add (local.get $dst) (local.get $n)) (i32.const 45))
            (local.set $n (i32.add (local.get $n) (i32.const 1)))))
    (block $done
      (loop $swap
        (br_if $done (i32.ge_u (local.get $i) (i32.div_u (local.get $n) (i32.const 2))))
        (local.set $t (i32.load8_u (i32.add (local.get $dst) (local.get $i))))
        (i32.store8 (i32.add (local.get $dst) (local.get $i))
                    (i32.load8_u (i32.add (local.get $dst) (i32.sub (i32.sub (local.get $n) (i32.const 1)) (local.get $i)))))
        (i32.store8 (i32.add (local.get $dst) (i32.sub (i32.sub (local.get $n) (i32.const 1)) (local.get $i))) (local.get $t))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $swap)))
    (local.get $n))
  (func $say (param $ptr i32) (param $len i32) (param $v i32)
    (memory.copy (i32.const 60000) (local.get $ptr) (local.get $len))
    (i32.store8 (i32.add (i32.const 60000) (local.get $len)) (i32.const 32))
    (call $log (i32.const 60000)
      (i32.add (i32.add (local.get $len) (i32.const 1))
               (call $num (local.get $v) (i32.add (i32.const 60001) (local.get $len))))))
)";

std::string Guest(const std::string& body) { return std::string("(module ") + kPrelude + body + ")"; }

// ===== WEBSOCKETS THROUGH A GUEST ============================================

// Opens a socket at start; on each event logs the state, reads every waiting
// message (logging its length, kind and text), answers each with "pong",
// and closes the socket on "bye".
const char* const kSocketGuest = R"(
  (data (i32.const 0) "wss://chat.example/room")
  (data (i32.const 32) "chat")
  (data (i32.const 40) "ws://plain.example/")
  (data (i32.const 64) "open")
  (data (i32.const 72) "state")
  (data (i32.const 80) "recv")
  (data (i32.const 88) "pong")
  (data (i32.const 96) "code")
  (data (i32.const 104) "bye")
  (data (i32.const 112) "close")
  (data (i32.const 120) "after")
  (data (i32.const 128) "binary")
  (data (i32.const 136) "send")
  (global $ws (mut i32) (i32.const 0))
  (func (export "uc_main")
    (global.set $ws (call $ws_open (i32.const 0) (i32.const 23) (i32.const 32) (i32.const 4)))
    (call $say (i32.const 64) (i32.const 4) (global.get $ws))
    (call $say (i32.const 64) (i32.const 4) (call $ws_open (i32.const 40) (i32.const 19) (i32.const 0) (i32.const 0)))
    (call $say (i32.const 136) (i32.const 4)
      (call $ws_send (global.get $ws) (i32.const 88) (i32.const 4) (i32.const 0))))
  (func (export "uc_event") (param $h i32) (param $e i32) (param $id i32)
    (local $n i32)
    (call $say (i32.const 72) (i32.const 5) (call $ws_state (local.get $id)))
    (block $done
      (loop $read
        (local.set $n (call $ws_receive (local.get $id) (i32.const 2048) (i32.const 1024) (i32.const 4000)))
        (br_if $done (i32.lt_s (local.get $n) (i32.const 0)))
        (call $say (i32.const 80) (i32.const 4) (local.get $n))
        ;; Too long for the buffer: it stays queued.
        (br_if $done (i32.gt_s (local.get $n) (i32.const 1024)))
        (call $say (i32.const 128) (i32.const 6) (i32.load (i32.const 4000)))
        (call $log (i32.const 2048) (local.get $n))
        (if (i32.and (i32.eq (local.get $n) (i32.const 3)) (i32.eq (i32.load8_u (i32.const 2048)) (i32.const 98)))
          (then
            (call $say (i32.const 112) (i32.const 5)
              (call $ws_close (local.get $id) (i32.const 0) (i32.const 104) (i32.const 3)))
            (call $say (i32.const 120) (i32.const 5) (call $ws_state (local.get $id)))
            (br $done)))
        (call $say (i32.const 136) (i32.const 4)
          (call $ws_send (local.get $id) (i32.const 88) (i32.const 4) (i32.const 0)))
        (br $read)))
    (if (i32.lt_s (call $ws_state (local.get $id)) (i32.const 0))
      (then (if (i32.ne (call $ws_state (local.get $id)) (i32.const -7))
        (then (call $say (i32.const 96) (i32.const 4) (call $ws_close_code (local.get $id)))))))
    (if (i32.eq (call $ws_state (local.get $id)) (i32.const 2))
      (then (call $say (i32.const 96) (i32.const 4) (call $ws_close_code (local.get $id))))))
)";

void TestSocketsThroughGuest() {
    std::printf("WebSockets through a guest\n");
    Harness h;
    CHECK(h.Start(Guest(kSocketGuest)));
    if (!h.guest) return;
    // ws: from an https app is refused; nothing goes before the socket opens.
    CHECK(SameLines(h.AppLogs(), {"open 1", "open -8", "send -5"}));
    CHECK(h.sockets.requests.size() == 1);
    if (h.sockets.requests.size() != 1) return;
    CHECK(h.sockets.requests[0].url == "wss://chat.example/room");
    CHECK(h.sockets.requests[0].origin == "https://app.example");
    CHECK(h.sockets.requests[0].protocols == std::vector<std::string>{"chat"});
    CHECK(h.guest->SocketCount() == 1);

    h.sockets.Open(0);
    CHECK(SameLines(h.AppLogs(), {"state 1"}));
    h.sockets.Message(0, "ping");
    CHECK(SameLines(h.AppLogs(), {"state 1", "recv 4", "binary 0", "ping", "send 0"}));
    h.sockets.Message(0, "AB", true);
    CHECK(SameLines(h.AppLogs(), {"state 1", "recv 2", "binary 1", "AB", "send 0"}));
    CHECK((h.sockets.connections[0]->sent == std::vector<std::string>{"t:pong", "t:pong"}));

    // A message larger than the app's buffer stays queued - and while one
    // waits, the next brings no event: the app reads until none is left.
    h.sockets.Message(0, std::string(2000, 'x'));
    CHECK(SameLines(h.AppLogs(), {"state 1", "recv 2000"}));
    h.sockets.Message(0, "ping");
    CHECK(h.AppLogs().empty());

    // The app closes it on "bye" (behind the two still queued, so a fresh socket).
    Harness b;
    CHECK(b.Start(Guest(kSocketGuest)));
    if (!b.guest) return;
    b.AppLogs();
    b.sockets.Open(0);
    b.sockets.Message(0, "bye");
    CHECK(SameLines(b.AppLogs(), {"state 1", "state 1", "recv 3", "binary 0", "bye", "close 0", "after -7"}));
    CHECK(b.sockets.connections[0]->closedWith == 1000 && b.sockets.connections[0]->closeReason == "bye");
    CHECK(b.guest->SocketCount() == 0);
}

void TestSocketEndings() {
    std::printf("WebSockets closed and lost\n");
    {
        Harness h;
        CHECK(h.Start(Guest(kSocketGuest)));
        if (!h.guest) return;
        h.AppLogs();
        h.sockets.Open(0);
        h.sockets.Closed(0, 4001);
        CHECK(SameLines(h.AppLogs(), {"state 1", "state 2", "code 4001"}));
        h.sockets.Message(0, "late");   // nothing after a close
        CHECK(h.AppLogs().empty());
    }
    {
        Harness h;
        CHECK(h.Start(Guest(kSocketGuest)));
        if (!h.guest) return;
        h.AppLogs();
        h.sockets.Error(0, "could not connect", false);
        CHECK(SameLines(h.AppLogs(), {"state -9", "code 1006"}));
    }
    {
        Harness h;
        CHECK(h.Start(Guest(kSocketGuest)));
        if (!h.guest) return;
        h.AppLogs();
        h.sockets.Open(0);
        h.sockets.Error(0, "a message larger than 1048576 bytes", true);
        CHECK(SameLines(h.AppLogs(), {"state 1", "state -6", "code 1009"}));
    }
    {
        // The guest goes: its open socket is closed, "going away".
        Harness h;
        CHECK(h.Start(Guest(kSocketGuest)));
        if (!h.guest) return;
        h.sockets.Open(0);
        auto connection = h.sockets.connections[0];
        h.guest.reset();
        CHECK(connection->closedWith == 1001);
    }
    {
        // An UltraWeb without WebSockets says so.
        Harness h;
        CHECK(h.Start(Guest(kSocketGuest), /*withServices=*/false));
        if (!h.guest) return;
        CHECK(SameLines(h.AppLogs(), {"open -8", "open -8", "send -7"}));
    }
}

// ===== PEERS THROUGH A GUEST =================================================

// Opens a peer with a TURN server, a channel, sends too early, refuses a
// bad configuration, hands over a remote description and a candidate; on
// each event reads every waiting peer event, sends "hi" on a channel that
// opened, and closes the peer on the message "bye".
std::string PeerGuest() {
    const std::string config = R"({\"iceServers\":[{\"urls\":\"turn:t.example\",\"username\":\"u\",\"credential\":\"p\"}]})";
    // The length as the guest sees it: the escapes are one byte each.
    size_t length = 0;
    for (size_t i = 0; i < config.size(); ++i) {
        if (config[i] == '\\') ++i;
        ++length;
    }
    return std::string(R"(
  (data (i32.const 0) ")") + config + R"(")
  (data (i32.const 512) "chat")
  (data (i32.const 520) "hi")
  (data (i32.const 528) "{oops")
  (data (i32.const 536) "v=0")
  (data (i32.const 544) "candidate:1")
  (data (i32.const 560) "0")
  (data (i32.const 600) "peer")
  (data (i32.const 608) "chan")
  (data (i32.const 616) "send")
  (data (i32.const 624) "bad")
  (data (i32.const 632) "remote")
  (data (i32.const 640) "cand")
  (data (i32.const 648) "kind")
  (data (i32.const 656) "ch")
  (data (i32.const 664) "num")
  (data (i32.const 672) "closepeer")
  (data (i32.const 688) "after")
  (global $peer (mut i32) (i32.const 0))
  (global $chan (mut i32) (i32.const 0))
  (func (export "uc_main")
    (global.set $peer (call $rtc_peer_open (i32.const 0) (i32.const )" + std::to_string(length) + R"()))
    (call $say (i32.const 600) (i32.const 4) (global.get $peer))
    (global.set $chan (call $rtc_channel_open (global.get $peer) (i32.const 512) (i32.const 4) (i32.const 1) (i32.const 3)))
    (call $say (i32.const 608) (i32.const 4) (global.get $chan))
    (call $say (i32.const 616) (i32.const 4) (call $rtc_send (global.get $chan) (i32.const 520) (i32.const 2) (i32.const 0)))
    (call $say (i32.const 624) (i32.const 3) (call $rtc_peer_open (i32.const 528) (i32.const 5)))
    (call $say (i32.const 632) (i32.const 6) (call $rtc_set_remote (global.get $peer) (i32.const 2) (i32.const 536) (i32.const 3)))
    (call $say (i32.const 640) (i32.const 4)
      (call $rtc_add_candidate (global.get $peer) (i32.const 544) (i32.const 11) (i32.const 560) (i32.const 1))))
  (func (export "uc_event") (param $h i32) (param $e i32) (param $id i32)
    (local $n i32)
    (block $done
      (loop $read
        (local.set $n (call $rtc_poll (local.get $id) (i32.const 4000) (i32.const 2048) (i32.const 1024)))
        (br_if $done (i32.lt_s (local.get $n) (i32.const 0)))
        (call $say (i32.const 648) (i32.const 4) (i32.load (i32.const 4000)))
        (call $say (i32.const 656) (i32.const 2) (i32.load (i32.const 4004)))
        (call $say (i32.const 664) (i32.const 3) (i32.load (i32.const 4008)))
        (call $log (i32.const 2048) (local.get $n))
        (if (i32.eq (i32.load (i32.const 4000)) (i32.const 5))
          (then (call $say (i32.const 616) (i32.const 4)
            (call $rtc_send (i32.load (i32.const 4004)) (i32.const 520) (i32.const 2) (i32.const 0)))))
        (if (i32.and (i32.eq (i32.load (i32.const 4000)) (i32.const 7)) (i32.eq (local.get $n) (i32.const 3)))
          (then
            (call $say (i32.const 672) (i32.const 9) (call $rtc_peer_close (local.get $id)))
            (call $say (i32.const 688) (i32.const 5)
              (call $rtc_poll (local.get $id) (i32.const 4000) (i32.const 2048) (i32.const 1024)))
            (br $done)))
        (br $read))))
)";
}

void TestPeersThroughGuest() {
    std::printf("peers through a guest\n");
    Harness h;
    CHECK(h.Start(Guest(PeerGuest())));
    if (!h.guest) return;
    CHECK(SameLines(h.AppLogs(), {"peer 1", "chan 2", "send -5", "bad -5", "remote 0", "cand 0"}));
    CHECK(h.rtc.peers.size() == 1);
    if (h.rtc.peers.size() != 1) return;
    FakePeer& peer = *h.rtc.peers[0];
    // An https app: relayed candidates only, through the TURN server it named.
    CHECK(h.rtc.configs[0].relayOnly);
    CHECK(h.rtc.configs[0].iceServers.size() == 1 && h.rtc.configs[0].iceServers[0].username == "u");
    CHECK((peer.calls == std::vector<std::string>{"channel chat unordered 3", "remote 2 v=0", "candidate 0 candidate:1"}));

    h.rtc.Emit(0, UC_RTC_LOCAL_DESCRIPTION, 0, UC_RTC_OFFER, "v=0 offer");
    CHECK(SameLines(h.AppLogs(), {"kind 1", "ch 0", "num 1", "v=0 offer"}));
    h.rtc.Emit(0, UC_RTC_CANDIDATE, 0, 0, "0\ncandidate:9");
    CHECK(SameLines(h.AppLogs(), {"kind 2", "ch 0", "num 0", "0\ncandidate:9"}));
    h.rtc.Emit(0, UC_RTC_STATE, 0, UC_RTC_STATE_CONNECTED, "");
    CHECK(SameLines(h.AppLogs(), {"kind 3", "ch 0", "num 2", ""}));
    // The app's channel opens (the service's id 100 is the app's 2), and it sends.
    peer.open.push_back(100);
    h.rtc.Emit(0, UC_RTC_CHANNEL_OPEN, 100, 0, "");
    CHECK(SameLines(h.AppLogs(), {"kind 5", "ch 2", "num 0", "", "send 0"}));
    CHECK((peer.sent == std::vector<std::string>{"100 t:hi"}));
    // The other side opens one: a new id for the app, with its label.
    h.rtc.Emit(0, UC_RTC_CHANNEL, 200, 0, "files");
    CHECK(SameLines(h.AppLogs(), {"kind 4", "ch 3", "num 0", "files"}));
    CHECK(h.guest->ChannelCount() == 2);
    // An event for a channel nobody knows is dropped.
    h.rtc.Emit(0, UC_RTC_MESSAGE, 999, 0, "stray");
    CHECK(h.AppLogs().empty());
    h.rtc.Emit(0, UC_RTC_MESSAGE, 200, 1, "AB");
    CHECK(SameLines(h.AppLogs(), {"kind 7", "ch 3", "num 1", "AB"}));
    // "bye": the app closes the peer; its events and channels go with it.
    h.rtc.Emit(0, UC_RTC_MESSAGE, 200, 0, "bye");
    CHECK(SameLines(h.AppLogs(), {"kind 7", "ch 3", "num 0", "bye", "closepeer 0", "after -7"}));
    CHECK(peer.closed);
    CHECK(h.guest->PeerCount() == 0 && h.guest->ChannelCount() == 0);
    CHECK(!h.guest->HasFailed());
}

void TestPeerTeardown() {
    std::printf("peers: teardown, no engine\n");
    {
        // The guest goes: its peer is closed - a closed tab is a hung-up call.
        Harness h;
        CHECK(h.Start(Guest(PeerGuest())));
        if (!h.guest) return;
        std::shared_ptr<FakePeer> peer = h.rtc.peers[0];
        h.guest.reset();
        CHECK(peer->closed);
    }
    {
        // No engine: every call is refused, nothing traps.
        Harness h;
        CHECK(h.Start(Guest(PeerGuest()), /*withServices=*/false));
        if (!h.guest) return;
        CHECK(SameLines(h.AppLogs(), {"peer -8", "chan -7", "send -7", "bad -8", "remote -7", "cand -7"}));
        CHECK(!h.guest->HasFailed());
    }
}

// ===== CALLS THROUGH A GUEST ==================================================

// Opens a peer, two VideoTiles (a remote view and a self view), an audio and
// a video track, makes the offer, asks for the microphone and the camera,
// shows the video track's pictures, plays the audio, mutes it. On each event
// it reads the peer's events; a blocked device it asks for again, and a
// track the other side added it shows in the tile.
const char* const kCallGuest = R"(
  (data (i32.const 0) "VideoTile")
  (data (i32.const 16) "audio")
  (data (i32.const 24) "video")
  (data (i32.const 32) "v2")
  (data (i32.const 64) "peer")
  (data (i32.const 72) "audio")
  (data (i32.const 80) "video")
  (data (i32.const 88) "bad")
  (data (i32.const 96) "offer")
  (data (i32.const 104) "mic")
  (data (i32.const 112) "cam")
  (data (i32.const 120) "show")
  (data (i32.const 128) "showbad")
  (data (i32.const 136) "play")
  (data (i32.const 144) "mute")
  (data (i32.const 152) "kind")
  (data (i32.const 160) "ch")
  (data (i32.const 168) "num")
  (data (i32.const 176) "again")
  (data (i32.const 184) "camaudio")
  (global $peer (mut i32) (i32.const 0))
  (global $tile (mut i32) (i32.const 0))
  (global $self (mut i32) (i32.const 0))
  (global $audio (mut i32) (i32.const 0))
  (global $video (mut i32) (i32.const 0))
  (func (export "uc_main")
    (global.set $peer (call $rtc_peer_open (i32.const 0) (i32.const 0)))
    (call $say (i32.const 64) (i32.const 4) (global.get $peer))
    (global.set $tile (call $create (i32.const 0) (i32.const 9)))
    (global.set $self (call $create (i32.const 0) (i32.const 9)))
    (drop (call $insert (i32.const 1) (global.get $tile) (i32.const 0)))
    (drop (call $insert (i32.const 1) (global.get $self) (i32.const 0)))
    (global.set $audio (call $rtc_audio_add (global.get $peer) (i32.const 16) (i32.const 5) (i32.const 0)))
    (call $say (i32.const 72) (i32.const 5) (global.get $audio))
    (global.set $video (call $rtc_video_add (global.get $peer) (i32.const 24) (i32.const 5) (i32.const 0)
                                            (i32.const 320) (i32.const 240) (i32.const 15) (i32.const 300)))
    (call $say (i32.const 80) (i32.const 5) (global.get $video))
    (call $say (i32.const 88) (i32.const 3) (call $rtc_video_add (global.get $peer) (i32.const 32) (i32.const 2)
                                            (i32.const 0) (i32.const 5000) (i32.const 0) (i32.const 0) (i32.const 0)))
    (call $say (i32.const 96) (i32.const 5) (call $rtc_offer (global.get $peer)))
    (call $say (i32.const 104) (i32.const 3) (call $rtc_microphone (global.get $audio) (i32.const 1)))
    (call $say (i32.const 112) (i32.const 3) (call $rtc_camera (global.get $video) (i32.const 1) (global.get $self)))
    (call $say (i32.const 184) (i32.const 8) (call $rtc_camera (global.get $audio) (i32.const 1) (i32.const 0)))
    (call $say (i32.const 120) (i32.const 4) (call $rtc_video_show (global.get $video) (global.get $tile)))
    (call $say (i32.const 128) (i32.const 7) (call $rtc_video_show (global.get $audio) (global.get $tile)))
    (call $say (i32.const 136) (i32.const 4) (call $rtc_playback (global.get $audio) (i32.const 1)))
    (call $say (i32.const 144) (i32.const 4) (call $rtc_track_enabled (global.get $audio) (i32.const 0))))
  (func (export "uc_event") (param $h i32) (param $e i32) (param $id i32)
    (local $n i32)
    (block $done
      (loop $read
        (local.set $n (call $rtc_poll (local.get $id) (i32.const 4000) (i32.const 2048) (i32.const 1024)))
        (br_if $done (i32.lt_s (local.get $n) (i32.const 0)))
        (call $say (i32.const 152) (i32.const 4) (i32.load (i32.const 4000)))
        (call $say (i32.const 160) (i32.const 2) (i32.load (i32.const 4004)))
        (call $say (i32.const 168) (i32.const 3) (i32.load (i32.const 4008)))
        (call $log (i32.const 2048) (local.get $n))
        (if (i32.and (i32.eq (i32.load (i32.const 4000)) (i32.const 11)) (i32.eqz (i32.load (i32.const 4008))))
          (then (call $say (i32.const 176) (i32.const 5)
            (call $rtc_camera (i32.load (i32.const 4004)) (i32.const 1) (global.get $self)))))
        (if (i32.eq (i32.load (i32.const 4000)) (i32.const 8))
          (then (call $say (i32.const 120) (i32.const 4)
            (call $rtc_video_show (i32.load (i32.const 4004)) (global.get $tile)))))
        (br $read))))
)";

void TestCallsThroughGuest() {
    std::printf("calls through a guest\n");
    Harness h;
    CHECK(h.Start(Guest(kCallGuest)));
    if (!h.guest) return;
    // The camera on an audio track is refused; asking does not wait for the answer.
    CHECK(SameLines(h.AppLogs(), {"peer 1", "audio 2", "video 3", "bad -5", "offer 0", "mic 0", "cam 0",
                                  "camaudio -5", "show 0", "showbad -5", "play 0", "mute 0"}));
    if (h.rtc.peers.empty()) return;
    FakePeer& peer = *h.rtc.peers[0];
    // No device runs before the user answers.
    CHECK((peer.calls == std::vector<std::string>{"track audio audio 0", "track video video 0 320x240@15/300", "offer",
                                                  "show 101 sink", "playback 100 on", "enabled 100 off"}));
    CHECK(h.asked.size() == 2);
    if (h.asked.size() != 2) return;
    CHECK(h.asked[0].first == "microphone" && h.asked[1].first == "camera");

    // The microphone allowed: it starts, and the app hears so.
    h.asked[0].second(true);
    CHECK(SameLines(h.AppLogs(), {"kind 11", "ch 2", "num 1", "microphone"}));
    CHECK(peer.calls.back() == "mic 100 on");
    // The camera blocked: the app hears so, and asking again is refused at
    // once, without another question.
    h.asked[1].second(false);
    CHECK(SameLines(h.AppLogs(), {"kind 11", "ch 3", "num 0", "camera", "again -8"}));
    CHECK(h.asked.size() == 2);
    CHECK(peer.calls.back() == "mic 100 on");   // no camera started

    // Pictures go straight to the tile the app named.
    auto tile = std::dynamic_pointer_cast<UltraCanvasVideoTile>(h.guest->ElementForTest(2));
    CHECK(tile && !tile->HasFrame());
    const uint8_t rgba[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    if (peer.sinks[101]) peer.sinks[101](rgba, 2, 2);
    CHECK(tile && tile->HasFrame());

    // The other side adds a video track: the app gets an id for it and shows it.
    h.rtc.Emit(0, UC_RTC_TRACK, 200, UC_RTC_VIDEO, "video2");
    CHECK(SameLines(h.AppLogs(), {"kind 8", "ch 4", "num 1", "video2", "show 0"}));
    CHECK(peer.calls.back() == "show 200 sink");
    h.rtc.Emit(0, UC_RTC_TRACK_OPEN, 200, 0, "");
    CHECK(SameLines(h.AppLogs(), {"kind 9", "ch 4", "num 0", ""}));
    CHECK(h.guest->TrackCount() == 3);

    // The guest goes: the peer stops, and a picture still on its way reaches no tile.
    RtcFrameSink late = peer.sinks[101];
    std::shared_ptr<FakePeer> holder = h.rtc.peers[0];
    h.guest.reset();
    CHECK(holder->closed);
    if (late) late(rgba, 2, 2);   // must not touch the released tile
    CHECK(true);
}

void TestCallsWithoutAsking() {
    std::printf("calls: an UltraWeb that cannot ask\n");
    Harness h;
    h.canAsk = false;
    CHECK(h.Start(Guest(kCallGuest)));
    if (!h.guest) return;
    const std::vector<std::string> logs = h.AppLogs();
    // The devices are refused at once; the rest works.
    CHECK(logs.size() == 12 && logs[5] == "mic -8" && logs[6] == "cam -8" && logs[8] == "show 0");
}

void TestPermissionStore() {
    std::printf("the permission store\n");
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "UltraWebPermissionTest";
    std::filesystem::remove_all(dir, ec);
    const std::string path = PathToUtf8(dir / "permissions.txt");
    {
        PermissionStore store(path);
        CHECK(store.Get("https://x.org", "camera") == PermissionStore::Answer::NotAsked);
        store.Set("https://x.org", "camera", true);
        store.Set("https://x.org", "microphone", false);
        store.Set("file:///home/me/app.wasm", "camera", false);
        CHECK(store.Count() == 3);
    }
    PermissionStore again(path);
    CHECK(again.Get("https://x.org", "camera") == PermissionStore::Answer::Allowed);
    CHECK(again.Get("https://x.org", "microphone") == PermissionStore::Answer::Blocked);
    CHECK(again.Get("file:///home/me/app.wasm", "camera") == PermissionStore::Answer::Blocked);
    CHECK(again.Get("https://y.org", "camera") == PermissionStore::Answer::NotAsked);
    again.Clear();
    CHECK(again.Count() == 0 && !std::filesystem::exists(PathFromUtf8(path)));
    std::filesystem::remove_all(dir, ec);
}

// ===== THE REAL SOCKET SERVICE ===============================================

#ifndef _WIN32

// SHA-1 (FIPS 180-4), for the handshake's Sec-WebSocket-Accept.
std::array<uint8_t, 20> Sha1(const std::string& message) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::vector<uint8_t> data(message.begin(), message.end());
    const uint64_t bits = uint64_t(data.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0);
    for (int i = 7; i >= 0; --i) data.push_back(uint8_t(bits >> (i * 8)));
    auto rotl = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(data[chunk + i * 4]) << 24 | uint32_t(data[chunk + i * 4 + 1]) << 16 |
                   uint32_t(data[chunk + i * 4 + 2]) << 8 | uint32_t(data[chunk + i * 4 + 3]);
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            const uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::array<uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j) out[size_t(i * 4 + j)] = uint8_t(h[i] >> (24 - j * 8));
    return out;
}

bool RecvAll(int fd, uint8_t* out, size_t n) {
    size_t got = 0;
    while (got < n) {
        const ssize_t r = ::recv(fd, out + got, n - got, 0);
        if (r <= 0) return false;
        got += size_t(r);
    }
    return true;
}

void SendFrame(int fd, uint8_t opcode, const std::string& payload) {
    std::string frame;
    frame += char(0x80 | opcode);
    if (payload.size() < 126) frame += char(payload.size());
    else { frame += char(126); frame += char(payload.size() >> 8); frame += char(payload.size() & 0xff); }
    frame += payload;
    ::send(fd, frame.data(), frame.size(), MSG_NOSIGNAL);
}

// A loopback WebSocket server: completes the handshake (recording the Origin
// and the subprotocol asked for, which it accepts), echoes text and binary,
// answers "close-me" with a close of its own (4000), and a close with a close.
struct WsServer {
    int listener = -1;
    int port = 0;
    std::atomic<bool> stop{false};
    std::thread thread;
    std::vector<std::thread> clients;
    std::mutex lock;
    std::string origin, protocol;

    bool Start() {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return false;
        int one = 1;
        ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
        socklen_t len = sizeof(addr);
        ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(addr.sin_port);
        ::listen(listener, 8);
        thread = std::thread([this] {
            while (!stop) {
                const int client = ::accept(listener, nullptr, nullptr);
                if (client < 0) break;
                std::lock_guard<std::mutex> guard(lock);
                clients.emplace_back([this, client] { Serve(client); });
            }
        });
        return true;
    }
    static std::string Header(const std::string& request, const std::string& name) {
        std::string lower = request;
        for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
        const size_t at = lower.find("\r\n" + name + ":");
        if (at == std::string::npos) return {};
        size_t start = at + 3 + name.size();
        while (start < request.size() && request[start] == ' ') ++start;
        return request.substr(start, request.find("\r\n", start) - start);
    }
    void Serve(int fd) {
        std::string request;
        char c;
        while (request.find("\r\n\r\n") == std::string::npos && ::recv(fd, &c, 1, 0) == 1) request += c;
        const std::string key = Header(request, "sec-websocket-key");
        const std::string asked = Header(request, "sec-websocket-protocol");
        {
            std::lock_guard<std::mutex> guard(lock);
            origin = Header(request, "origin");
            protocol = asked;
        }
        const auto digest = Sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        std::string answer = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                             "Sec-WebSocket-Accept: " + Base64Encode(std::vector<uint8_t>(digest.begin(), digest.end()), false) + "\r\n";
        if (!asked.empty()) answer += "Sec-WebSocket-Protocol: " + asked.substr(0, asked.find(',')) + "\r\n";
        answer += "\r\n";
        ::send(fd, answer.data(), answer.size(), MSG_NOSIGNAL);
        for (;;) {
            uint8_t head[2];
            if (!RecvAll(fd, head, 2)) break;
            const uint8_t opcode = head[0] & 0x0f;
            uint64_t length = head[1] & 0x7f;
            if (length == 126) {
                uint8_t ext[2];
                if (!RecvAll(fd, ext, 2)) break;
                length = uint64_t(ext[0]) << 8 | ext[1];
            } else if (length == 127) {
                uint8_t ext[8];
                if (!RecvAll(fd, ext, 8)) break;
                length = 0;
                for (uint8_t b : ext) length = length << 8 | b;
            }
            uint8_t mask[4] = {0, 0, 0, 0};
            if ((head[1] & 0x80) && !RecvAll(fd, mask, 4)) break;
            std::string payload(size_t(length), '\0');
            if (length && !RecvAll(fd, reinterpret_cast<uint8_t*>(&payload[0]), size_t(length))) break;
            for (size_t i = 0; i < payload.size(); ++i) payload[i] = char(payload[i] ^ mask[i % 4]);
            if (opcode == 0x8) { SendFrame(fd, 0x8, payload.substr(0, 2)); break; }
            if (opcode == 0x9) { SendFrame(fd, 0xA, payload); continue; }
            if (opcode != 0x1 && opcode != 0x2) continue;
            if (payload == "close-me") { SendFrame(fd, 0x8, std::string("\x0f\xa0", 2) + "bye"); continue; }
            SendFrame(fd, opcode, payload);
        }
        ::close(fd);
    }
    void Stop() {
        stop = true;
        ::shutdown(listener, SHUT_RDWR);
        ::close(listener);
        if (thread.joinable()) thread.join();
        std::lock_guard<std::mutex> guard(lock);
        for (auto& client : clients) if (client.joinable()) client.join();
    }
};

void TestNetworkSockets() {
    std::printf("the UltraNet socket service on loopback\n");
    WsServer server;
    if (!server.Start()) { CHECK(false); return; }
    SocketService service = MakeNetworkSockets(Post);
    SocketRequest request;
    request.url = "ws://127.0.0.1:" + std::to_string(server.port) + "/echo";
    request.origin = "https://app.example";
    request.protocols = {"chat"};

    bool opened = false;
    std::vector<std::string> received;
    int closedCode = 0;
    std::string error;
    SocketEvents events;
    events.onOpen = [&]() { opened = true; };
    events.onMessage = [&](std::vector<uint8_t> data, bool binary) { received.push_back((binary ? "b:" : "t:") + Text(data)); };
    events.onClose = [&](int code, std::string) { closedCode = code; };
    events.onError = [&](std::string e, bool) { error = e; };
    std::shared_ptr<SocketConnection> socket = service(request, events);
    CHECK(socket != nullptr);
    CHECK(Pump([&] { return opened || !error.empty(); }));
    CHECK(opened);
    if (!opened) { std::printf("  error: %s\n", error.c_str()); server.Stop(); return; }
    {
        std::lock_guard<std::mutex> guard(server.lock);
        CHECK(server.origin == "https://app.example");
        CHECK(server.protocol == "chat");
    }
    CHECK(socket->Send(Bytes("hello"), false));
    CHECK(socket->Send(Bytes("AB"), true));
    CHECK(Pump([&] { return received.size() >= 2; }));
    CHECK((received == std::vector<std::string>{"t:hello", "b:AB"}));
    // The server closes: the code it gave arrives.
    CHECK(socket->Send(Bytes("close-me"), false));
    CHECK(Pump([&] { return closedCode != 0 || !error.empty(); }));
    CHECK(closedCode == 4000);
    CHECK(!socket->Send(Bytes("late"), false));
    socket->Close(1000, "");

    // Closed by the app: nothing follows.
    bool secondOpen = false, secondHeard = false;
    SocketEvents quiet;
    quiet.onOpen = [&]() { secondOpen = true; };
    quiet.onMessage = [&](std::vector<uint8_t>, bool) { secondHeard = true; };
    quiet.onClose = [&](int, std::string) { secondHeard = true; };
    quiet.onError = [&](std::string, bool) { secondHeard = true; };
    std::shared_ptr<SocketConnection> second = service(request, quiet);
    CHECK(Pump([&] { return secondOpen; }));
    CHECK(second->Send(Bytes("echo me"), false));
    second->Close(1000, "done");
    Pump([] { return false; }, 1);
    CHECK(!secondHeard);

    // Nobody listening: an error, not an open.
    server.Stop();
    std::string refused;
    SocketEvents failing;
    failing.onError = [&](std::string e, bool) { refused = e; };
    failing.onOpen = [&]() { refused = "opened"; };
    std::shared_ptr<SocketConnection> third = service(request, failing);
    CHECK(Pump([&] { return !refused.empty(); }));
    CHECK(refused.find("could not connect") != std::string::npos);
    third->Close(1000, "");
}

#endif

// ===== THE REAL PEER SERVICE =================================================

void TestEnginePeers() {
    std::printf("two UltraRTC peers in one process\n");
    RtcService service = MakeEngineRtc(Post);
    if (!service) {
        std::printf("  (no WebRTC engine in this build)\n");
        return;
    }
    struct Side {
        std::shared_ptr<RtcPeer> peer;
        std::vector<RtcEvent> events;
        std::vector<std::string> messages;
        uint64_t openChannel = 0;
        uint64_t remoteChannel = 0;
    };
    Side a, b;
    // What one side says, the test carries to the other untouched.
    auto signal = [](Side& from, Side& to) {
        return [&from, &to](RtcEvent e) {
            if (e.kind == UC_RTC_LOCAL_DESCRIPTION && to.peer) to.peer->SetRemote(e.number, e.text);
            if (e.kind == UC_RTC_CANDIDATE && to.peer) {
                const size_t newline = e.text.find('\n');
                to.peer->AddCandidate(e.text.substr(newline + 1), e.text.substr(0, newline));
            }
            if (e.kind == UC_RTC_CHANNEL_OPEN) from.openChannel = e.channel;
            if (e.kind == UC_RTC_CHANNEL) from.remoteChannel = e.channel;
            if (e.kind == UC_RTC_MESSAGE) from.messages.push_back((e.number ? "b:" : "t:") + e.text);
            from.events.push_back(std::move(e));
        };
    };
    RtcConfig direct;   // loopback: host candidates, no servers
    std::string error;
    a.peer = service(direct, signal(a, b), error);
    b.peer = service(direct, signal(b, a), error);
    CHECK(a.peer && b.peer);
    if (!a.peer || !b.peer) return;
    uint64_t channel = 0;
    CHECK(a.peer->OpenChannel("chat", true, -1, channel) == UC_OK);
    CHECK(Pump([&] { return a.openChannel == channel && b.remoteChannel != 0; }, 30));
    CHECK(a.openChannel == channel);
    CHECK(b.remoteChannel != 0);
    CHECK(a.peer->Send(channel, Bytes("hello"), false) == UC_OK);
    CHECK(Pump([&] { return !b.messages.empty(); }, 10));
    CHECK(!b.messages.empty() && b.messages[0] == "t:hello");
    CHECK(Pump([&] { return b.openChannel == b.remoteChannel; }, 10));
    CHECK(b.peer->Send(b.remoteChannel, Bytes("AB"), true) == UC_OK);
    CHECK(Pump([&] { return !a.messages.empty(); }, 10));
    CHECK(!a.messages.empty() && a.messages[0] == "b:AB");
    // A connected state was reported on the way.
    bool connected = false;
    for (const RtcEvent& e : a.events) connected = connected || (e.kind == UC_RTC_STATE && e.number == UC_RTC_STATE_CONNECTED);
    CHECK(connected);
    a.peer->Close();
    b.peer->Close();
}

// Two peers with a video track each, the same media id: A offers, B answers
// by itself; A's pictures, fed straight to the engine, reach B's sink.
void TestEngineVideo() {
    std::printf("video between two UltraRTC peers\n");
#ifdef ULTRAWEB_HAS_RTC
    RtcService service = MakeEngineRtc(Post);
    if (!service || !UltraRtc_GetCapabilities().video) {
        std::printf("  (no video in this build)\n");
        return;
    }
    std::shared_ptr<RtcPeer> a, b;
    bool aOpen = false, bOpen = false;
    std::atomic<int> received{0};
    std::atomic<int> width{0};
    auto relay = [](std::shared_ptr<RtcPeer>& to, bool& open) {
        return [&to, &open](RtcEvent e) {
            if (e.kind == UC_RTC_LOCAL_DESCRIPTION && to) to->SetRemote(e.number, e.text);
            if (e.kind == UC_RTC_CANDIDATE && to) {
                const size_t newline = e.text.find('\n');
                to->AddCandidate(e.text.substr(newline + 1), e.text.substr(0, newline));
            }
            if (e.kind == UC_RTC_TRACK_OPEN) open = true;
        };
    };
    std::string error;
    a = service(RtcConfig{}, relay(b, aOpen), error);
    b = service(RtcConfig{}, relay(a, bOpen), error);
    CHECK(a && b);
    if (!a || !b) return;
    RtcVideoConfig small;
    small.width = 160;
    small.height = 120;
    small.fps = 15;
    small.kbps = 200;
    uint64_t aTrack = 0, bTrack = 0;
    CHECK(a->AddTrack(true, "video", UC_RTC_SENDRECV, small, aTrack) == UC_OK);
    CHECK(b->AddTrack(true, "video", UC_RTC_SENDRECV, small, bTrack) == UC_OK);
    CHECK(b->ShowVideo(bTrack, [&](const uint8_t*, int w, int) { width = w; ++received; }) == UC_OK);
    CHECK(a->Offer() == UC_OK);
    CHECK(Pump([&] { return aOpen && bOpen; }, 30));
    // Pictures from a thread of their own, as a camera's would come.
    std::atomic<bool> sending{true};
    std::thread feeder([&] {
        UltraRtcVideoFrame frame;
        frame.width = 160;
        frame.height = 120;
        frame.rgba.assign(size_t(160) * 120 * 4, 128);
        for (int i = 0; sending && i < 400; ++i) {
            UltraRtc_SendVideoFrame(UltraRtcHandle(aTrack), frame);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    CHECK(Pump([&] { return received >= 3; }, 20) || received >= 3);
    sending = false;
    feeder.join();
    CHECK(received >= 3);
    CHECK(width == 160);
    CHECK(b->ShowVideo(bTrack, nullptr) == UC_OK);
    a->Close();
    b->Close();
#else
    std::printf("  (built without UltraRTC)\n");
#endif
}

} // namespace

int main() {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
    std::printf("UltraWebRealtimeTest: %s\n", UltraCanvasWasm_EngineDescription().c_str());
    TestSocketRules();
    TestRtcConfig();
    if (UltraCanvasWasm_IsAvailable()) {
        TestSocketsThroughGuest();
        TestSocketEndings();
        TestPeersThroughGuest();
        TestPeerTeardown();
        TestCallsThroughGuest();
        TestCallsWithoutAsking();
    } else {
        std::printf("guests: skipped (no WebAssembly engine)\n");
    }
    UltraNet_Initialize();
#ifndef _WIN32
    TestNetworkSockets();
#else
    std::printf("the UltraNet socket service: skipped (POSIX sockets)\n");
#endif
    TestPermissionStore();
    TestEnginePeers();
    TestEngineVideo();
#ifdef ULTRAWEB_HAS_RTC
    UltraRtc_Shutdown();
#endif
    UltraNet_Shutdown();
    std::printf("UltraWebRealtimeTest: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
