// Apps/UltraSocial/ui/UltraSocialApp.cpp
// Version: 0.2.0 - UltraMail's first-run start page; the compose view fills
//                  and follows the window; the account wizard's client id
//                  reaches the connector
// Version: 0.1.0 (Phase 1)
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialApp.h"

#include "UltraSocialComposer.h"
#include "UltraSocialConnector.h"
#include "UltraSocialPublisher.h"
#include "UltraSocialTheme.h"

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
#include "UltraCanvasPathUtf8.h"

// ULTRASOCIAL_VERSION comes from the build alone: CMake reads the first line of
// Docs/UltraSocial/CHANGELOG.md (cmake/UltraCanvasVersion.cmake) and passes it as a
// compile definition. No fallback, so a build that lost it fails instead of
// showing a wrong number in the window title.
#ifndef ULTRASOCIAL_VERSION
#error "ULTRASOCIAL_VERSION is not defined: build through CMake, which reads it from Docs/UltraSocial/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace UltraSocial {

bool UltraSocialApp::Initialize(const std::string& dataDir) {
    std::error_code ec;
    std::filesystem::create_directories(UltraCanvas::PathFromUtf8(dataDir), ec);

    if (!store_.Open("ultrasocial", dataDir + "/social.db")) return false;
    vault_ = CredentialVault(dataDir + "/vault");
    // Unlock the vault with the local device key so the user is not prompted
    // (Thunderbird-style; see UltraVault::DeviceKeyVault). A brand-new vault
    // is created here, and a 0.1-format one in the same folder is carried
    // across. Nothing reads or writes a secret while this fails — Store()
    // reports it, so a sign-in says so instead of losing the token.
    if (!vault_.TryAutoUnlock())
        std::fprintf(stderr, "UltraSocial: cannot open the credential vault in %s/vault: "
                             "%s; account credentials are unavailable\n", dataDir.c_str(),
                     UltraVault::DeviceKeyVault::DescribeUnlockStatus(
                             vault_.GetLastUnlockStatus()).c_str());
    dataDir_ = dataDir;

    store_.ListAccounts(accounts_);
    return true;
}

std::shared_ptr<UltraCanvasWindow> UltraSocialApp::CreateMainWindow() {
    WindowConfig config;
    config.title  = "UltraSocial " ULTRASOCIAL_VERSION;
    config.width  = 1000;
    config.height = 700;
    config.backgroundColor = Theme::kPageBackground;
    window_ = CreateWindow(config);

    // Start page - the only thing on screen until the first account exists:
    // logo, app name and the "Add social account" button, as in UltraMail.
    window_->AddChild(startPage_.Build());
    startPage_.onAddAccount = [this]() { HandleAddAccount(); };

    // Compose view - toolbar and cards, once an account exists.
    window_->AddChild(composeView_.Build());
    composeView_.onAddAccount = [this]() { HandleAddAccount(); };
    composeView_.onAddMedia   = [this]() { HandleAddMedia(); };
    composeView_.onPost       = [this]() { HandlePost(); };
    composeView_.onPostLater  = [this]() { HandlePostLater(); };
    composeView_.onCancelScheduled = [this](int64_t id) {
        store_.RemoveOutbox(id);
        RefreshScheduled();
    };
    composeView_.onOpenUrl = [](const std::string& url) { OpenURL(url); };

    // Both views are sized to the client area so their layouts follow the
    // window.
    ResizeViews(static_cast<float>(config.width), static_cast<float>(config.height));
    window_->onWindowResize = [this](int width, int height) {
        ResizeViews(static_cast<float>(width), static_cast<float>(height));
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

    // No account yet -> only the start page; otherwise only the compose view.
    const bool firstRun = accounts_.empty();
    if (auto page = startPage_.Container()) page->SetVisible(firstRun);
    if (auto view = composeView_.Container()) view->SetVisible(!firstRun);
}

void UltraSocialApp::ResizeViews(float width, float height) {
    startPage_.Resize(width, height);
    composeView_.Resize(width, height);
}

std::string UltraSocialApp::HandleFor(const std::string& accountId) const {
    for (const auto& account : accounts_) {
        if (account.accountId == accountId) return account.handle;
    }
    return accountId;
}

namespace {

std::tm LocalTime(int64_t epochSeconds) {
    std::tm local{};
    std::time_t at = static_cast<std::time_t>(epochSeconds);
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &at);
#else
    localtime_r(&at, &local);
#endif
    return local;
}

std::string FormatTime(const std::tm& when, const char* format) {
    char stamp[32];
    std::strftime(stamp, sizeof stamp, format, &when);
    return stamp;
}

} // namespace

void UltraSocialApp::RefreshScheduled() {
    std::vector<OutboxEntry> entries;
    store_.ListOutbox(entries);

    std::vector<ComposeView::ScheduledItem> items;
    for (const auto& entry : entries) {
        ComposeView::ScheduledItem item;
        item.id       = entry.id;
        item.network  = entry.network;
        item.handle   = HandleFor(entry.accountId);
        item.when     = FormatTime(LocalTime(entry.scheduledAt), "%d.%m. %H:%M");
        item.attempts = entry.attempts;
        item.lastError = entry.lastError;
        items.push_back(item);
    }
    composeView_.SetScheduled(items);
}

void UltraSocialApp::RefreshHistory() {
    std::vector<HistoryEntry> entries;
    store_.ListHistory("", 6, entries);

    // Today's posts say when; older ones say which day.
    const std::tm today = LocalTime(static_cast<int64_t>(std::time(nullptr)));

    std::vector<ComposeView::HistoryItem> items;
    for (const auto& entry : entries) {
        ComposeView::HistoryItem item;
        item.succeeded = entry.succeeded;
        item.network   = entry.network;
        item.handle    = HandleFor(entry.accountId);
        // Cut on a code point (and a word, where one is near), never inside
        // a UTF-8 sequence.
        item.text      = TruncateToPoints(entry.text, 90);
        item.url       = entry.url;
        item.error     = entry.error;
        if (entry.createdAt > 0) {
            const std::tm when = LocalTime(entry.createdAt);
            const bool sameDay = when.tm_year == today.tm_year &&
                                 when.tm_yday == today.tm_yday;
            item.when = FormatTime(when, sameDay ? "%H:%M" : "%d.%m.");
        }
        items.push_back(item);
    }
    composeView_.SetHistory(items);
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
        // Reddit, X and LinkedIn sign in with the user's own app's client id.
        // The wizard asked for it but it was never passed on, so all three
        // refused with "sign-in needs your app's client id" however it was
        // filled in.
        auth.clientId   = input.clientId;
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
    Theme::StyleInput(time);
    char hhmm[8];
    std::snprintf(hhmm, sizeof hhmm, "%02d:%02d",
                  (local.tm_hour + 1) % 24, 0);   // suggest the next full hour
    time->SetText(hhmm);
    row->AddChild(time);
    dialog->AddChild(row);
    row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto hintLabel = Theme::MakeWrapped("plHint",
        "The post goes out when UltraSocial is running at (or after) "
        "this local time.");
    dialog->AddChild(hintLabel);

    auto buttonRow = CreateContainer("plButtons", 0, 0, 0, 36);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(10)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);
    auto scheduleBtn = Theme::MakeButton("plSchedule", "Schedule", true, "clock-five.svg",
                                         110.0f, 28.0f);
    scheduleBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(scheduleBtn);
    auto cancelBtn = Theme::MakeButton("plCancel", "Cancel", false, "", 80.0f, 28.0f);
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
