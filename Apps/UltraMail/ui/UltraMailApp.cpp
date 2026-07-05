// Apps/UltraMail/ui/UltraMailApp.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailApp.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasLabel.h"

#include <cctype>
#include <filesystem>
#include <string>

using namespace UltraCanvas;

namespace UltraMail {

std::string UltraMailApp::LocalPart(const std::string& email) {
    auto at = email.find('@');
    return at == std::string::npos ? email : email.substr(0, at);
}

std::string UltraMailApp::SlugFromEmail(const std::string& email) {
    std::string slug;
    for (char c : email) {
        if (std::isalnum(static_cast<unsigned char>(c)))
            slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        else
            slug.push_back('-');
    }
    return slug;
}

bool UltraMailApp::Initialize(const std::string& dataDir) {
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);
    const std::string dbPath = dataDir + "/mail.db";

    UltraDbResult opened = store_.Open("ultramail", dbPath);
    if (!opened) return false;

    store_.ListAccounts(accounts_);
    store_.GetAccountStatus(status_);
    return true;
}

std::shared_ptr<UltraCanvasWindow> UltraMailApp::CreateMainWindow() {
    WindowConfig config;
    config.title  = "UltraMail";
    config.width  = 960;
    config.height = 640;
    window_ = CreateWindow(config);

    // Header line.
    window_->AddChild(CreateLabel("umTitle", 16, 8, 400, 28, "UltraMail"));

    // Account info-tile bar (populated once accounts exist).
    auto bar = infoBar_.Build();
    window_->AddChild(bar);
    infoBar_.onAccountClicked     = [](const std::string&) { /* Phase 2: open account */ };
    infoBar_.onNeedsAnswerClicked = [](const std::string&) { /* Phase 2: needs-answer view */ };

    // Toolbox grid of account tiles + the "Add email account" tile.
    auto grid = toolbox_.Build();
    window_->AddChild(grid);
    toolbox_.onAddAccount  = [this]() { HandleAddAccount(); };
    toolbox_.onOpenAccount = [](const std::string&) { /* Phase 2: open the 3-pane view */ };

    // First-run hint below the grid.
    window_->AddChild(CreateLabel("umHint", 16, 600, 600, 24,
        "Welcome to UltraMail. Add an account to begin."));

    Refresh();
    return window_;
}

void UltraMailApp::Refresh() {
    store_.ListAccounts(accounts_);
    store_.GetAccountStatus(status_);
    infoBar_.Rebuild(status_);
    toolbox_.Rebuild(accounts_, status_);
}

void UltraMailApp::HandleAddAccount() {
    AccountWizard::Show(window_.get(),
        [this](const AccountDraft& draft) { HandleWizardSubmit(draft); });
}

void UltraMailApp::HandleWizardSubmit(const AccountDraft& draft) {
    // Phase 1: store the account locally so its tiles appear. The discovery +
    // login-verify pipeline (provider presets -> autoconfig -> DNS SRV ->
    // probing, over UltraNet) and the credential vault land in a later slice;
    // the password is intentionally not persisted here.
    Account a;
    a.accountId   = SlugFromEmail(draft.email);
    a.email       = draft.email;
    a.displayName = draft.displayName.empty() ? LocalPart(draft.email) : draft.displayName;
    a.shortName   = LocalPart(draft.email);

    if (!store_.UpsertAccount(a)) return;

    // Give the account an inbox so the Toolbox tile and rollups have somewhere
    // to hang; real folders arrive from the first IMAP sync.
    Folder inbox;
    inbox.accountId = a.accountId;
    inbox.name      = "INBOX";
    inbox.role      = FolderRole::Inbox;
    store_.UpsertFolder(inbox);

    Refresh();
}

} // namespace UltraMail
