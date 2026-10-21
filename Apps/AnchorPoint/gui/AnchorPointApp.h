// Apps/AnchorPoint/gui/AnchorPointApp.h
// AnchorPoint - UltraCanvas GUI (M2, LAN direct transfer).
// Version: 0.2.0
// Author: AnchorPoint
#pragma once

#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
// Note: progress is shown as a Unicode bar in a Label rather than via
// UltraCanvasProgressBar.h, which is currently an uncompiled/latent-broken
// header (bare `Rect2D` and an X11 `Success` macro clash). Kept dependency-free.

#include "../net/Transport.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace AnchorPoint {

// A single-window front end over the M1 transfer core. All networking runs on
// worker threads; UI widgets are only ever touched from the main thread via a
// periodic timer that reads the mutex-guarded transfer state.
class AnchorPointGui {
public:
    explicit AnchorPointGui(UltraCanvas::UltraCanvasApplication& app);
    ~AnchorPointGui();

    bool Build();          // create window + widgets
    void Show();           // show the window

private:
    // ---- UI actions (main thread) ----
    void OnChooseFile();
    void OnBrowseSaveDir();
    void OnSend();
    void OnStartReceiving();
    void OnStop();
    void PollState(UltraCanvas::TimerId);   // periodic UI refresh

    // ---- worker bodies (background threads) ----
    void SendWorker(std::string filePath, std::string host, uint16_t port);
    void ReceiveWorker(uint16_t port, std::string saveDir);

    // ---- shared transfer state (guarded by stateMutex_) ----
    void SetStatus(const std::string& text);
    void ShowIdleMessage(const std::string& text);   // UI thread, nothing running: also empties the bar
    void SetProgress(uint64_t done, uint64_t total);
    void FinishBusy(const std::string& finalText);

    UltraCanvas::UltraCanvasApplication& app_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;

    std::shared_ptr<UltraCanvas::UltraCanvasButton>     chooseBtn_, sendBtn_, browseBtn_, recvBtn_, stopBtn_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>      fileLabel_, statusLabel_, barLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>  peerInput_, recvPortInput_, saveDirInput_;

    std::string chosenFile_;

    std::mutex  stateMutex_;
    std::string statusText_ = "Ready.";
    double      progressFrac_ = 0.0;
    bool        busy_ = false;
    bool        dirty_ = true;   // UI needs refresh

    std::thread worker_;
    std::atomic<bool> cancel_{false};
    // What the worker is blocked in, so OnStop() and the destructor can wake it:
    // Accept() on the listener, or a send / recv on the connection.
    std::mutex activeMutex_;
    IListener*   activeListener_ = nullptr;
    IConnection* activeConn_ = nullptr;
    void SetActiveConnection(IConnection* conn);
    void WakeWorker();   // close the listener, interrupt the connection

    UltraCanvas::TimerId uiTimer_ = UltraCanvas::InvalidTimerId;
};

} // namespace AnchorPoint
