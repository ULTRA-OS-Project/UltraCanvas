// Apps/UltraAuthenticator/AuthenticatorWindow.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "AuthenticatorWindow.h"
#include "AddAccountDialog.h"

#include "UltraCanvasModalDialog.h"

#include <ctime>
#include <string>
#include <utility>

namespace UltraCanvas {
namespace Authenticator {

namespace {

// "123456" reads better as "123 456" at a glance, which is how every
// authenticator shows it and how people read it aloud while typing.
std::string GroupCode(const std::string& code) {
    if (code.size() == 6) return code.substr(0, 3) + " " + code.substr(3);
    if (code.size() == 8) return code.substr(0, 4) + " " + code.substr(4);
    return code;
}

std::string DisplayName(const Account& account) {
    if (account.params.issuer.empty()) return account.params.accountName;
    return account.params.issuer + " — " + account.params.accountName;
}

int64_t NowUnix() {
    return static_cast<int64_t>(std::time(nullptr));
}

} // namespace

AuthenticatorWindow::AuthenticatorWindow(UltraCanvasApplication& app,
                                         AccountStore& store)
    : app_(app), store_(store) {}

AuthenticatorWindow::~AuthenticatorWindow() {
    if (timerRunning_) {
        app_.StopTimer(refreshTimer_);
        timerRunning_ = false;
    }
}

bool AuthenticatorWindow::Create() {
    WindowConfig cfg;
    cfg.title     = "UltraAuthenticator";
    cfg.width     = kWindowWidth;
    cfg.height    = kWindowHeight;
    cfg.x         = 140;
    cfg.y         = 140;
    cfg.resizable = false;
    cfg.type      = WindowType::Standard;

    window_ = CreateWindow(cfg);
    if (!window_) return false;

    window_->onWindowClosing = [this]() {
        // Drop the decrypted vault on close rather than waiting for process
        // teardown: the seeds should not outlive the window that needed them.
        if (timerRunning_) {
            app_.StopTimer(refreshTimer_);
            timerRunning_ = false;
        }
        store_.Close();
        app_.RequestExit();
        return true;
    };

    auto title = std::make_shared<UltraCanvasLabel>(
        "auth-title", kMargin, 18, kWindowWidth - 2 * kMargin, 24,
        "UltraAuthenticator");
    window_->AddChild(title);

    auto addBtn = std::make_shared<UltraCanvasButton>(
        "auth-add", kMargin, 50, 140, 30);
    addBtn->SetText("Add account");
    addBtn->onClick = [this]() { OpenAddAccountDialog(); };
    window_->AddChild(addBtn);

    statusLabel_ = std::make_shared<UltraCanvasLabel>(
        "auth-status", kMargin + 156, 56,
        kWindowWidth - 2 * kMargin - 156, 20, "");
    window_->AddChild(statusLabel_);

    emptyLabel_ = std::make_shared<UltraCanvasLabel>(
        "auth-empty", kMargin, kHeaderHeight + 8,
        kWindowWidth - 2 * kMargin, 40,
        "No accounts yet — choose \"Add account\" to begin.");
    window_->AddChild(emptyLabel_);

    RebuildRows();

    // One periodic timer drives every row. 1 Hz is the coarsest rate that
    // still makes the countdown look live.
    refreshTimer_ = app_.StartTimer(1000, true, [this](TimerId) {
        RefreshCodes();
    });
    timerRunning_ = true;

    return true;
}

void AuthenticatorWindow::Show() {
    if (window_) window_->Show();
}

void AuthenticatorWindow::SetStatus(const std::string& text) {
    if (statusLabel_) statusLabel_->SetText(text);
}

void AuthenticatorWindow::ClearRows() {
    if (!window_) return;
    for (Row& row : rows_) {
        if (row.nameLabel)      window_->RemoveChild(row.nameLabel);
        if (row.codeLabel)      window_->RemoveChild(row.codeLabel);
        if (row.countdownLabel) window_->RemoveChild(row.countdownLabel);
        if (row.actionButton)   window_->RemoveChild(row.actionButton);
        if (row.removeButton)   window_->RemoveChild(row.removeButton);
    }
    rows_.clear();
}

void AuthenticatorWindow::RebuildRows() {
    if (!window_) return;
    ClearRows();

    std::vector<Account> accounts;
    StoreResult listed = store_.List(accounts);
    if (!listed) {
        SetStatus(listed.message);
        if (emptyLabel_) emptyLabel_->SetText("");
        return;
    }

    if (emptyLabel_) {
        emptyLabel_->SetText(
            accounts.empty()
                ? "No accounts yet — choose \"Add account\" to begin."
                : "");
    }

    size_t shown = accounts.size();
    if (shown > kMaxVisibleRows) {
        shown = kMaxVisibleRows;
        // Say so rather than silently dropping accounts off the bottom.
        SetStatus("Showing " + std::to_string(kMaxVisibleRows) + " of " +
                  std::to_string(accounts.size()) +
                  " accounts; scrolling is not implemented yet.");
    }

    for (size_t i = 0; i < shown; ++i) {
        const Account& account = accounts[i];
        const long y = kHeaderHeight + static_cast<long>(i) * kRowHeight;
        const std::string id = "row" + std::to_string(i);

        Row row;
        row.key    = account.key;
        row.isHotp = (account.params.type == Otp::Type::Hotp);

        row.nameLabel = std::make_shared<UltraCanvasLabel>(
            id + "-name", kMargin, y, 250, 20, DisplayName(account));
        window_->AddChild(row.nameLabel);

        row.codeLabel = std::make_shared<UltraCanvasLabel>(
            id + "-code", kMargin, y + 20, 130, 20, "------");
        window_->AddChild(row.codeLabel);

        row.countdownLabel = std::make_shared<UltraCanvasLabel>(
            id + "-left", kMargin + 140, y + 20, 100, 20, "");
        window_->AddChild(row.countdownLabel);

        if (row.isHotp) {
            // A counter-based code is only produced on demand: generating one
            // spends it, so it must be an explicit action, never a timer tick.
            row.actionButton = std::make_shared<UltraCanvasButton>(
                id + "-next", kWindowWidth - kMargin - 190, y + 6, 80, 28);
            row.actionButton->SetText("Next");
            const std::string key = account.key;
            row.actionButton->onClick = [this, key]() { AdvanceHotpRow(key); };
            window_->AddChild(row.actionButton);
        }

        row.removeButton = std::make_shared<UltraCanvasButton>(
            id + "-del", kWindowWidth - kMargin - 100, y + 6, 90, 28);
        row.removeButton->SetText("Remove");
        const std::string key = account.key;
        row.removeButton->onClick = [this, key]() { RemoveAccount(key); };
        window_->AddChild(row.removeButton);

        rows_.push_back(std::move(row));
    }

    RefreshCodes();
}

void AuthenticatorWindow::RefreshCodes() {
    if (!store_.IsOpen()) return;

    const int64_t now = NowUnix();
    for (Row& row : rows_) {
        if (row.isHotp) {
            // Nothing to tick: an HOTP code changes only when "Next" is
            // pressed, and showing one until then would imply it is still
            // valid.
            continue;
        }
        std::string code;
        uint32_t remaining = 0;
        StoreResult generated = store_.GenerateTotp(row.key, now, code, remaining);
        if (!generated) {
            if (row.codeLabel)      row.codeLabel->SetText("error");
            if (row.countdownLabel) row.countdownLabel->SetText("");
            SetStatus(generated.message);
            continue;
        }
        if (row.codeLabel)      row.codeLabel->SetText(GroupCode(code));
        if (row.countdownLabel) {
            row.countdownLabel->SetText(std::to_string(remaining) + "s");
        }
    }
}

void AuthenticatorWindow::AdvanceHotpRow(const std::string& key) {
    std::string code;
    StoreResult advanced = store_.AdvanceHotp(key, code);
    if (!advanced) {
        SetStatus(advanced.message);
        return;
    }
    for (Row& row : rows_) {
        if (row.key != key) continue;
        if (row.codeLabel)      row.codeLabel->SetText(GroupCode(code));
        if (row.countdownLabel) row.countdownLabel->SetText("used once");
    }
    SetStatus("");
}

void AuthenticatorWindow::RemoveAccount(const std::string& key) {
    // Deleting an account destroys a second factor, so it is confirmed rather
    // than done on a single click.
    UltraCanvasDialogManager::ShowConfirmation(
        "Remove \"" + key + "\"?\n\n"
        "You will not be able to sign in with this account's codes again "
        "unless you still have its setup key or recovery codes.",
        "Remove account",
        [this, key](bool confirmed) {
            if (!confirmed) return;
            StoreResult removed = store_.Remove(key);
            if (!removed) {
                SetStatus(removed.message);
                return;
            }
            SetStatus("");
            RebuildRows();
        },
        window_.get());
}

void AuthenticatorWindow::OpenAddAccountDialog() {
    auto dialog = std::make_shared<AddAccountDialog>();
    dialog->onAccept = [this](const std::string& uri) -> std::string {
        std::string key;
        StoreResult added = store_.AddFromUri(uri, key);
        if (!added) return added.message;
        RebuildRows();
        SetStatus("Added " + key + ".");
        return std::string();
    };
    dialog->CreateAddAccountDialog();
    dialog->ShowModal(window_.get());
}

} // namespace Authenticator
} // namespace UltraCanvas
