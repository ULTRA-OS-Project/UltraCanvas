// Apps/AnchorPoint/gui/AnchorPointApp.cpp
// AnchorPoint - UltraCanvas GUI implementation (M2).
// Version: 0.2.0
// Author: AnchorPoint
#include "AnchorPointApp.h"

#include "UltraCanvasNativeDialogs.h"
#include "../net/Protocol.h"

#include <cstdint>

using namespace UltraCanvas;

namespace AnchorPoint {

namespace {

constexpr uint16_t kDefaultPort = 5040;

std::string BaseName(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

// Parse "host" or "host:port". Falls back to kDefaultPort.
// Render a fixed-width progress bar as text, e.g. "██████░░░░░░  52%".
std::string RenderBar(double frac) {
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    const int width = 24;
    int filled = static_cast<int>(frac * width + 0.5);
    // UTF-8 bytes for U+2588 FULL BLOCK and U+2591 LIGHT SHADE.
    static const char* kFull  = "\xE2\x96\x88";
    static const char* kLight = "\xE2\x96\x91";
    std::string bar;
    for (int i = 0; i < width; ++i) bar += (i < filled) ? kFull : kLight;
    int pct = static_cast<int>(frac * 100.0 + 0.5);
    bar += "  " + std::to_string(pct) + "%";
    return bar;
}

bool ParsePeer(const std::string& in, std::string& host, uint16_t& port) {
    std::string s = in;
    // trim
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    if (s.empty()) return false;

    port = kDefaultPort;
    size_t colon = s.find_last_of(':');
    // single colon → host:port (avoid splitting bare IPv6, which we don't accept here)
    if (colon != std::string::npos && s.find(':') == colon) {
        host = s.substr(0, colon);
        try { port = static_cast<uint16_t>(std::stoi(s.substr(colon + 1))); }
        catch (...) { return false; }
    } else {
        host = s;
    }
    return !host.empty();
}

} // namespace

AnchorPointGui::AnchorPointGui(UltraCanvasApplication& app) : app_(app) {}

AnchorPointGui::~AnchorPointGui() {
    cancel_ = true;
    { // unblock a pending Accept()
        std::lock_guard<std::mutex> lk(listenerMutex_);
        if (activeListener_) activeListener_->Close();
    }
    if (uiTimer_ != InvalidTimerId) app_.StopTimer(uiTimer_);
    if (worker_.joinable()) worker_.join();
}

bool AnchorPointGui::Build() {
    WindowConfig config;
    config.title = "AnchorPoint";
    config.width = 560;
    config.height = 560;
    config.resizable = true;
    config.type = WindowType::Standard;

    window_ = CreateWindow(config);
    if (!window_ || !window_->IsCreated()) return false;

    const float pad = 24.0f;
    const float w = 512.0f;
    float y = 20.0f;

    auto title = CreateLabel("ap_title", pad, y, w, 34, "AnchorPoint");
    window_->AddChild(title);
    y += 40;

    auto subtitle = CreateLabel("ap_sub", pad, y, w, 22,
                                "Send files computer-to-computer. No server storage.");
    window_->AddChild(subtitle);
    y += 44;

    // ---- Send section ----
    auto sendHdr = CreateLabel("ap_send_hdr", pad, y, w, 24, "Send a file");
    window_->AddChild(sendHdr);
    y += 32;

    chooseBtn_ = CreateButton("ap_choose", pad, y, 140, 34, "Choose File…");
    chooseBtn_->SetOnClick([this]() { OnChooseFile(); });
    window_->AddChild(chooseBtn_);

    fileLabel_ = CreateLabel("ap_file", pad + 152, y + 6, w - 152, 22, "(no file selected)");
    window_->AddChild(fileLabel_);
    y += 46;

    auto peerHdr = CreateLabel("ap_peer_hdr", pad, y, w, 20, "Peer address (host or host:port)");
    window_->AddChild(peerHdr);
    y += 26;

    peerInput_ = CreateTextInput("ap_peer", (int)pad, (int)y, 300, 32);
    peerInput_->SetText("192.168.1.10:5040");
    window_->AddChild(peerInput_);

    sendBtn_ = CreateButton("ap_send", pad + 316, y, 140, 34, "Send");
    sendBtn_->SetOnClick([this]() { OnSend(); });
    window_->AddChild(sendBtn_);
    y += 56;

    // ---- Receive section ----
    auto recvHdr = CreateLabel("ap_recv_hdr", pad, y, w, 24, "Receive");
    window_->AddChild(recvHdr);
    y += 32;

    auto portHdr = CreateLabel("ap_port_hdr", pad, y, 120, 20, "Listen port");
    window_->AddChild(portHdr);
    auto dirHdr = CreateLabel("ap_dir_hdr", pad + 140, y, w - 140, 20, "Save to folder");
    window_->AddChild(dirHdr);
    y += 26;

    recvPortInput_ = CreateTextInput("ap_recvport", (int)pad, (int)y, 120, 32);
    recvPortInput_->SetText(std::to_string(kDefaultPort));
    window_->AddChild(recvPortInput_);

    saveDirInput_ = CreateTextInput("ap_savedir", (int)(pad + 140), (int)y, 200, 32);
    saveDirInput_->SetText(".");
    window_->AddChild(saveDirInput_);

    recvBtn_ = CreateButton("ap_recv", pad + 352, y, 104, 34, "Receive");
    recvBtn_->SetOnClick([this]() { OnStartReceiving(); });
    window_->AddChild(recvBtn_);
    y += 56;

    // ---- Progress + status ----
    barLabel_ = CreateLabel("ap_bar", pad, y, w, 22, RenderBar(0.0));
    window_->AddChild(barLabel_);
    y += 34;

    stopBtn_ = CreateButton("ap_stop", pad, y, 104, 32, "Stop");
    stopBtn_->SetOnClick([this]() { OnStop(); });
    window_->AddChild(stopBtn_);

    statusLabel_ = CreateLabel("ap_status", pad + 120, y + 4, w - 120, 24, "Ready.");
    window_->AddChild(statusLabel_);

    // Periodic UI refresh reads the mutex-guarded transfer state and pushes it
    // to the widgets. Timers fire on the main thread, so this is the only place
    // widgets are updated while a transfer runs.
    uiTimer_ = app_.StartTimer(150, true, [this](TimerId id) { PollState(id); });

    return true;
}

void AnchorPointGui::Show() { if (window_) window_->Show(); }

// ===========================================================================
// UI actions
// ===========================================================================
void AnchorPointGui::OnChooseFile() {
    std::string path = UltraCanvasNativeDialogs::OpenFile(
        "Choose a file to send", {}, "", window_.get());
    if (path.empty()) return;
    chosenFile_ = path;
    if (fileLabel_) fileLabel_->SetText(BaseName(path));
}

void AnchorPointGui::OnSend() {
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        if (busy_) return;
    }
    if (chosenFile_.empty()) { SetStatus("Choose a file first."); return; }

    std::string host;
    uint16_t port = kDefaultPort;
    if (!ParsePeer(peerInput_ ? peerInput_->GetText() : "", host, port)) {
        SetStatus("Enter a valid peer address (host or host:port).");
        return;
    }

    if (worker_.joinable()) worker_.join();
    cancel_ = false;
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        busy_ = true;
        progressFrac_ = 0.0;
        statusText_ = "Connecting to " + host + ":" + std::to_string(port) + " …";
        dirty_ = true;
    }
    std::string file = chosenFile_;
    worker_ = std::thread([this, file, host, port]() { SendWorker(file, host, port); });
}

void AnchorPointGui::OnStartReceiving() {
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        if (busy_) return;
    }
    uint16_t port = kDefaultPort;
    try { port = static_cast<uint16_t>(std::stoi(recvPortInput_ ? recvPortInput_->GetText() : "")); }
    catch (...) { SetStatus("Enter a valid listen port."); return; }

    std::string dir = saveDirInput_ ? saveDirInput_->GetText() : ".";
    if (dir.empty()) dir = ".";

    if (worker_.joinable()) worker_.join();
    cancel_ = false;
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        busy_ = true;
        progressFrac_ = 0.0;
        statusText_ = "Listening on port " + std::to_string(port) + " …";
        dirty_ = true;
    }
    worker_ = std::thread([this, port, dir]() { ReceiveWorker(port, dir); });
}

void AnchorPointGui::OnStop() {
    cancel_ = true;
    std::lock_guard<std::mutex> lk(listenerMutex_);
    if (activeListener_) activeListener_->Close();  // unblock Accept()
}

void AnchorPointGui::PollState(TimerId) {
    std::string text;
    double frac;
    bool needRefresh;
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        if (!dirty_) return;
        text = statusText_;
        frac = progressFrac_;
        dirty_ = false;
        needRefresh = true;
    }
    if (!needRefresh) return;
    if (barLabel_) barLabel_->SetText(RenderBar(frac));
    if (statusLabel_) statusLabel_->SetText(text);
}

// ===========================================================================
// shared-state helpers (called from worker threads)
// ===========================================================================
void AnchorPointGui::SetStatus(const std::string& text) {
    std::lock_guard<std::mutex> lk(stateMutex_);
    statusText_ = text;
    dirty_ = true;
}

void AnchorPointGui::SetProgress(uint64_t done, uint64_t total) {
    std::lock_guard<std::mutex> lk(stateMutex_);
    progressFrac_ = total ? static_cast<double>(done) / static_cast<double>(total) : 0.0;
    dirty_ = true;
}

void AnchorPointGui::FinishBusy(const std::string& finalText) {
    std::lock_guard<std::mutex> lk(stateMutex_);
    busy_ = false;
    statusText_ = finalText;
    dirty_ = true;
}

// ===========================================================================
// workers
// ===========================================================================
void AnchorPointGui::SendWorker(std::string filePath, std::string host, uint16_t port) {
    std::string err;
    auto conn = Connect(host, port, &err);
    if (!conn) { FinishBusy("Connect failed: " + err); return; }

    SetStatus("Sending " + BaseName(filePath) + " …");
    auto res = SendFile(*conn, filePath, "AnchorPoint",
                        [this](uint64_t d, uint64_t t) { SetProgress(d, t); });
    if (res.ok) FinishBusy("Sent " + BaseName(filePath) + " — verified OK.");
    else        FinishBusy("Send failed: " + res.error);
}

void AnchorPointGui::ReceiveWorker(uint16_t port, std::string saveDir) {
    std::string err;
    auto listener = Listen(port, &err);
    if (!listener) { FinishBusy("Cannot listen: " + err); return; }

    {
        std::lock_guard<std::mutex> lk(listenerMutex_);
        activeListener_ = listener.get();
    }
    auto conn = listener->Accept();
    {
        std::lock_guard<std::mutex> lk(listenerMutex_);
        activeListener_ = nullptr;
    }

    if (cancel_) { FinishBusy("Receiving cancelled."); return; }
    if (!conn) { FinishBusy("No connection."); return; }

    SetStatus("Peer connected: " + conn->PeerAddress());
    auto res = ReceiveFile(*conn,
        [&](const OfferInfo& offer, const std::string& peer) -> std::string {
            SetStatus("Receiving \"" + offer.fileName + "\" from " + peer + " …");
            std::string out = saveDir;
            if (!out.empty() && out.back() != '/' && out.back() != '\\') out += '/';
            out += offer.fileName;
            return out;
        },
        [this](uint64_t d, uint64_t t) { SetProgress(d, t); });

    if (res.ok) FinishBusy("Received " + std::to_string(res.bytes) + " bytes — integrity verified.");
    else        FinishBusy("Receive failed: " + res.error);
}

} // namespace AnchorPoint
