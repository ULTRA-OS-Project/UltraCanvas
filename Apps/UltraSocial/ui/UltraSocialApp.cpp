// Apps/UltraSocial/ui/UltraSocialApp.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialApp.h"

#include "UltraSocialComposer.h"
#include "UltraSocialConnector.h"
#include "UltraSocialPublisher.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDatePicker.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"           // OpenURL

#include <cstdio>
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
    composeView_.onPostLater  = [this]() { HandlePostLater(); };
    composeView_.onCancelScheduled = [this](int64_t id) {
        store_.RemoveOutbox(id);
        RefreshScheduled();
    };

    Refresh();

    // Worker threads (sign-in, publish) queue their results; this timer
    // applies them on the main thread.
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
        app->StartTimer(100, /*periodic=*/true,
                        [this](TimerId) { DrainUiQueue(); });
        // The scheduler: fire due outbox entries. 15 s granularity is
        // plenty for minute-precision scheduling.
        app->StartTimer(15000, /*periodic=*/true,
                        [this](TimerId) { FlushOutbox(); });
    }
    FlushOutbox();   // anything that came due while the app was closed
    return window_;
}

void UltraSocialApp::Refresh() {
    store_.ListAccounts(accounts_);
    composeView_.SetAccounts(accounts_);
    RefreshHistory();
    RefreshScheduled();
}

void UltraSocialApp::RefreshScheduled() {
    std::vector<OutboxEntry> entries;
    store_.ListOutbox(entries);

    std::vector<ComposeView::ScheduledItem> items;
    for (const auto& entry : entries) {
        std::string handle = entry.accountId;
        for (const auto& account : accounts_) {
            if (account.accountId == entry.accountId) {
                handle = account.handle;
                break;
            }
        }
        std::tm when{};
        std::time_t at = static_cast<std::time_t>(entry.scheduledAt);
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&when, &at);
#else
        localtime_r(&at, &when);
#endif
        char stamp[24];
        std::strftime(stamp, sizeof stamp, "%d.%m. %H:%M", &when);
        std::string label = handle + " · " + stamp;
        if (entry.attempts > 0) {
            label += " · retry " + std::to_string(entry.attempts);
        }
        items.push_back({entry.id, label});
    }
    composeView_.SetScheduled(items);
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
    // independently (through the same PublishOne path the outbox uses) and
    // gets its own history row.
    std::thread([this, draft, targets]() {
        std::string report;
        bool allOk = true;

        for (const auto& account : targets) {
            HistoryEntry entry = PublishOne(store_, vault_, account, draft);
            if (!entry.succeeded) allOk = false;

            RunOnUiThread([this]() { RefreshHistory(); });

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

void UltraSocialApp::HandlePostLater() {
    const PostDraft draft = composeView_.CollectDraft();
    const std::vector<std::string> targetIds = composeView_.SelectedAccountIds();
    if (draft.text.empty() && draft.media.empty()) {
        UltraCanvasDialogManager::ShowInformation("Write something first.",
                                                  "Nothing to schedule", nullptr,
                                                  window_.get());
        return;
    }
    if (targetIds.empty()) {
        UltraCanvasDialogManager::ShowInformation("Select at least one account.",
                                                  "Nothing to schedule", nullptr,
                                                  window_.get());
        return;
    }

    // Date + time picker dialog.
    DialogConfig config;
    config.title      = "Post later";
    config.width      = 400;
    config.height     = 220;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    auto* dlg = dialog.get();

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(12)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);

    std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif

    auto row = CreateContainer("plRow", 0, 0, 0, 32);
    row->layout.SetFlexRow()
               .SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto date = CreateDatePicker("plDate", 0, 0, 180, 28);
    date->SetSelectedDate(UCDate(local.tm_year + 1900, local.tm_mon + 1,
                                 local.tm_mday));
    row->AddChild(date);
    auto time = CreateTextInput("plTime", 0, 0, 90, 28);
    char hhmm[8];
    std::snprintf(hhmm, sizeof hhmm, "%02d:%02d",
                  (local.tm_hour + 1) % 24, 0);   // suggest the next full hour
    time->SetText(hhmm);
    row->AddChild(time);
    dialog->AddChild(row);
    row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto hintLabel = CreateLabel("plHint", 0, 0, 360, 40,
        "The post goes out when UltraSocial is running at (or after) "
        "this local time.");
    hintLabel->SetWrap(TextWrap::WrapWord);
    dialog->AddChild(hintLabel);

    auto buttonRow = CreateContainer("plButtons", 0, 0, 0, 36);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(10)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);
    auto scheduleBtn = CreateButton("plSchedule", 0, 0, 110, 28, "Schedule");
    scheduleBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(scheduleBtn);
    auto cancelBtn = CreateButton("plCancel", 0, 0, 80, 28, "Cancel");
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);
    dialog->AddChild(buttonRow);

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [this, date, time, draft, targetIds](DialogResult result) {
            if (result != DialogResult::OK) return;
            UCDate day = date->GetSelectedDate();
            int hour = 0, minute = 0;
            std::sscanf(time->GetText().c_str(), "%d:%d", &hour, &minute);

            std::tm when{};
            when.tm_year  = day.year - 1900;
            when.tm_mon   = day.month - 1;
            when.tm_mday  = day.day;
            when.tm_hour  = hour;
            when.tm_min   = minute;
            when.tm_isdst = -1;
            std::time_t at = std::mktime(&when);
            if (at == -1) {
                UltraCanvasDialogManager::ShowError(
                    "That date/time could not be understood.", "Post later",
                    nullptr, window_.get());
                return;
            }
            ScheduleDraft(draft, targetIds, static_cast<int64_t>(at));
        },
        window_.get());
}

void UltraSocialApp::ScheduleDraft(const PostDraft& draft,
                                   const std::vector<std::string>& targetIds,
                                   int64_t when) {
    for (const auto& id : targetIds) {
        for (const auto& account : accounts_) {
            if (account.accountId != id) continue;
            OutboxEntry entry;
            entry.accountId   = account.accountId;
            entry.network     = account.network;
            entry.draft       = draft;
            entry.scheduledAt = when;
            store_.EnqueuePost(entry);
        }
    }
    composeView_.ClearAfterPost();
    RefreshScheduled();
}

void UltraSocialApp::FlushOutbox() {
    if (posting_ || flushing_.exchange(true)) return;
    std::thread([this]() {
        FlushStats stats = FlushDueOutbox(store_, vault_,
                                          static_cast<int64_t>(std::time(nullptr)));
        RunOnUiThread([this, stats]() {
            flushing_ = false;
            if (stats.published || stats.rescheduled || stats.abandoned) {
                RefreshHistory();
                RefreshScheduled();
            }
        });
    }).detach();
}

} // namespace UltraSocial
