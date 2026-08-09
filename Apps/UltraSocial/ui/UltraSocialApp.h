// Apps/UltraSocial/ui/UltraSocialApp.h
// The UltraSocial application manager: owns the store, the credential
// vault and the main window, and wires the compose view and the account
// wizard together. Sign-in and publishing run on worker threads (they
// block on the network — the OAuth flow for as long as the user takes in
// the browser); results marshal back to the UI through a queue drained by
// a main-thread timer. Texter-style app-composition class.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSocialAccountWizard.h"
#include "UltraSocialComposeView.h"

#include "UltraSocialCredentialVault.h"
#include "UltraSocialStore.h"

#include "UltraCanvasWindow.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UltraSocial {

class UltraSocialApp {
public:
    // Open the store under `dataDir` (created if absent) and load accounts.
    UltraSocialApp() : vault_("") {}
    bool Initialize(const std::string& dataDir);

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> CreateMainWindow();

    // Reload accounts + history into the view.
    void Refresh();

private:
    void HandleAddAccount();
    void HandleWizardSubmit(const WizardInput& input);
    void HandleAddMedia();
    void HandlePost();
    void RefreshHistory();

    // Queue `action` for the main thread (drained by the UI timer).
    void RunOnUiThread(std::function<void()> action);
    void DrainUiQueue();

    Store store_;
    CredentialVault vault_;
    std::vector<Account> accounts_;
    std::string dataDir_;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    AccountWizard wizard_;
    ComposeView composeView_;

    std::mutex uiQueueMutex_;
    std::vector<std::function<void()>> uiQueue_;
    bool posting_ = false;
};

} // namespace UltraSocial
