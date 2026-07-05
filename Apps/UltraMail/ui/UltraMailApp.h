// Apps/UltraMail/ui/UltraMailApp.h
// The UltraMail application manager: owns the local store, the account list and
// the main window, and wires the Toolbox, the account info-tile bar and the
// account-setup wizard together. Texter-style app-composition class.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailToolbox.h"
#include "UltraMailInfoTileBar.h"
#include "UltraMailAccountWizard.h"

#include "UltraMailLocalStore.h"

#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraMail {

class UltraMailApp {
public:
    // Open the local store under `dataDir` (created if absent) and load the
    // account list. Returns false if the store cannot be opened.
    bool Initialize(const std::string& dataDir);

    // Create the main window with the info-tile bar and the Toolbox grid.
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> CreateMainWindow();

    // Reload accounts + status and rebuild the info-tile bar and Toolbox.
    void Refresh();

private:
    void HandleAddAccount();
    void HandleWizardSubmit(const AccountDraft& draft);
    static std::string SlugFromEmail(const std::string& email);
    static std::string LocalPart(const std::string& email);

    LocalStore store_;
    std::vector<Account> accounts_;
    std::vector<AccountStatus> status_;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    Toolbox     toolbox_;
    InfoTileBar infoBar_;
};

} // namespace UltraMail
