// Apps/UltraSocial/ui/UltraSocialApp.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialApp.h"

#include "UltraSocialComposer.h"
#include "UltraSocialConnector.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"           // OpenURL

#include <ctime>
#include <filesystem>
#include <thread>

using namespace UltraCanvas;

namespace UltraSocial {

bool UltraSocialApp::Initialize(const std::string& dataDir) {
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);

    if (!store_.Open("ultrasocial", dataDir + "/social.db")) return false;
    vault_ = CredentialVault(dataDir + "/vault");
    dataDir_ = dataDir;

    store_.ListAccounts(accounts_);
    return true;
}

std::shared_ptr<UltraCanvasWindow> UltraSocialApp::CreateMainWindow() {
    WindowConfig config;
    config.title  = "UltraSocial";
    config.width  = 990;
    config.height = 700;
    window_ = CreateWindow(config);

    window_->AddChild(CreateLabel("usTitle", 16, 8, 400, 28, "UltraSocial"));

    window_->AddChild(composeView_.Build());
    composeView_.onAddAccount = [this]() { HandleAddAccount(); };
    composeView_.onAddMedia   = [this]() { HandleAddMedia(); };
    composeView_.onPost       = [this]() { HandlePost(); };

    Refresh();

    // Worker threads (sign-in, publish) queue their results; this timer
    // applies them on the main thread.
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
        app->StartTimer(100, /*periodic=*/true,
                        [this](TimerId) { DrainUiQueue(); });
    }
    return window_;
}

void UltraSocialApp::Refresh() {
    store_.ListAccounts(accounts_);
    composeView_.SetAccounts(accounts_);
    RefreshHistory();
}

void UltraSocialApp::RefreshHistory() {
    std::vector<HistoryEntry> entries;
    store_.ListHistory("", 6, entries);

    std::vector<std::string> lines;
    for (const auto& entry : entries) {
        std::string handle = entry.accountId;
        for (const auto& account : accounts_) {
            if (account.accountId == entry.accountId) {
                handle = account.handle;
                break;
            }
        }
        std::string text = entry.text.substr(0, 60);
        if (entry.text.size() > 60) text += "…";
        if (entry.succeeded) {
            lines.push_back("✓  " + handle + " — “" + text + "”" +
                            (entry.url.empty() ? "" : "  " + entry.url));
        } else {
            lines.push_back("✗  " + handle + " — " + entry.error);
        }
    }
    composeView_.SetHistoryLines(lines);
}

void UltraSocialApp::RunOnUiThread(std::function<void()> action) {
    std::lock_guard<std::mutex> lock(uiQueueMutex_);
    uiQueue_.push_back(std::move(action));
}

void UltraSocialApp::DrainUiQueue() {
    std::vector<std::function<void()>> actions;
    {
        std::lock_guard<std::mutex> lock(uiQueueMutex_);
        actions.swap(uiQueue_);
    }
    for (auto& action : actions) action();
}

void UltraSocialApp::HandleAddAccount() {
    wizard_.Show(window_.get(),
                 [this](const WizardInput& input) { HandleWizardSubmit(input); });
}

void UltraSocialApp::HandleWizardSubmit(const WizardInput& input) {
    auto connector = CreateConnector(input.network);
    if (!connector) return;

    // Sign-in blocks — on the OAuth path for as long as the browser consent
    // takes — so it runs on a worker thread.
    std::thread([this, connector, input]() {
        AuthInput auth;
        auth.server     = input.server;
        auth.identifier = input.identifier;
        auth.secret     = input.secret;
        auth.onOpenUrl  = [](const std::string& url) { OpenURL(url); };

        Account account;
        std::string credentials;
        auto result = connector->Authenticate(auth, account, credentials);

        RunOnUiThread([this, result, account, credentials]() {
            if (!result) {
                UltraCanvasDialogManager::ShowError(
                    result.message, "Sign-in failed", nullptr, window_.get());
                return;
            }
            if (!vault_.Store(account.accountId, credentials)) {
                UltraCanvasDialogManager::ShowError(
                    "Could not save the account's credentials.",
                    "Sign-in failed", nullptr, window_.get());
                return;
            }
            store_.UpsertAccount(account);
            Refresh();
            UltraCanvasDialogManager::ShowInformation(
                "Connected " + account.handle + ".", "Account added",
                nullptr, window_.get());
        });
    }).detach();
}

void UltraSocialApp::HandleAddMedia() {
    FileDialogOptions options;
    options.title = "Attach image";
    options.parentWindow = window_.get();
    options.filters = {
        FileFilter("Images", {"png", "jpg", "jpeg", "gif", "webp"})};
    UltraCanvasFileLoader::OpenFileDialog(
        options, [this](DialogResult result, const std::string& path) {
            if (result == DialogResult::OK && !path.empty()) {
                composeView_.AddMedia(path);
            }
        });
}

void UltraSocialApp::HandlePost() {
    if (posting_) return;
    const PostDraft draft = composeView_.CollectDraft();
    const std::vector<std::string> targetIds = composeView_.SelectedAccountIds();

    if (draft.text.empty() && draft.media.empty()) {
        UltraCanvasDialogManager::ShowInformation("Write something first.",
                                              "Nothing to post", nullptr,
                                              window_.get());
        return;
    }
    if (targetIds.empty()) {
        UltraCanvasDialogManager::ShowInformation("Select at least one account.",
                                              "Nothing to post", nullptr,
                                              window_.get());
        return;
    }

    // Snapshot the selected accounts for the worker.
    std::vector<Account> targets;
    for (const auto& id : targetIds) {
        for (const auto& account : accounts_) {
            if (account.accountId == id) targets.push_back(account);
        }
    }

    posting_ = true;
    composeView_.SetBusy(true);

    // One network failing must not block the others: each target publishes
    // independently and gets its own history row.
    std::thread([this, draft, targets]() {
        std::string report;
        bool allOk = true;

        for (const auto& account : targets) {
            auto connector = CreateConnector(account.network);
            HistoryEntry entry;
            entry.accountId  = account.accountId;
            entry.network    = account.network;
            entry.mediaCount = static_cast<int>(draft.media.size());

            std::vector<std::string> warnings;
            AdaptedPost post = AdaptDraft(draft, account.network,
                                          connector->Capabilities(), warnings);
            entry.text = post.text;

            auto problems = connector->ValidateDraft(post);
            std::string credentials;
            UltraNetResult result;
            if (!problems.empty()) {
                result = UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                               problems.front());
            } else if (!vault_.Retrieve(account.accountId, credentials)) {
                result = UltraNetResult::Error(
                    UltraNetResultCode::AuthenticationFailed,
                    "no stored credentials — remove and re-add the account");
            } else {
                PostResult posted;
                result = connector->PublishPost(account, credentials, post,
                                                posted);
                if (result) {
                    entry.succeeded = true;
                    entry.postId    = posted.postId;
                    entry.url       = posted.url;
                    // Connectors may rotate tokens during publish.
                    vault_.Store(account.accountId, credentials);
                }
            }
            if (!result) {
                entry.error = result.message;
                allOk = false;
            }

            RunOnUiThread([this, entry]() mutable {
                store_.AddHistory(entry);
                RefreshHistory();
            });

            if (!report.empty()) report += '\n';
            report += (entry.succeeded ? "✓ " : "✗ ") + account.handle +
                      (entry.succeeded
                           ? (entry.url.empty() ? "" : " — " + entry.url)
                           : " — " + entry.error);
        }

        RunOnUiThread([this, report, allOk]() {
            posting_ = false;
            composeView_.SetBusy(false);
            if (allOk) {
                composeView_.ClearAfterPost();
                UltraCanvasDialogManager::ShowInformation(report, "Posted",
                                                          nullptr, window_.get());
            } else {
                UltraCanvasDialogManager::ShowError(report,
                                                    "Posting finished with errors",
                                                    nullptr, window_.get());
            }
        });
    }).detach();
}

} // namespace UltraSocial
