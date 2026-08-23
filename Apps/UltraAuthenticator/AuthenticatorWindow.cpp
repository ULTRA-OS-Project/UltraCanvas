// Apps/UltraAuthenticator/AuthenticatorWindow.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS

#include "AuthenticatorWindow.h"
#include "AddAccountDialog.h"
#include "ChangePasswordDialog.h"
#include "EditAccountDialog.h"
#include "RevealSecretDialog.h"
#include "Theme.h"

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

// The placeholder must occupy the same width as a real code, or every card
// jumps sideways the moment the first refresh lands.
std::string CodePlaceholder(uint32_t digits) {
    return GroupCode(std::string(digits, '-'));
}

int64_t NowUnix() {
    return static_cast<int64_t>(std::time(nullptr));
}

// The second line of a card: the account name, plus the details worth knowing
// at a glance. SHA-1 is the default every service uses, so naming it on every
// card would be noise; anything else is unusual enough to show.
std::string SubtitleFor(const Otp::Parameters& params) {
    std::string text = params.accountName;
    if (params.type == Otp::Type::Hotp) {
        text += "  ·  counter " + std::to_string(params.counter);
    }
    if (params.algorithm != Otp::Algorithm::SHA1) {
        text += "  ·  " + std::string(Otp::AlgorithmName(params.algorithm));
    }
    if (params.digits != Otp::kDefaultDigits) {
        text += "  ·  " + std::to_string(params.digits) + " digits";
    }
    return text;
}

std::string DisplayName(const Otp::Parameters& params) {
    if (params.issuer.empty()) return params.accountName;
    return params.issuer + " — " + params.accountName;
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

    window_->SetBackgroundColor(Theme::kPageBackground);

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

    const long margin = Theme::kMargin;
    const long width  = kWindowWidth - 2 * margin;

    auto title = std::make_shared<UltraCanvasLabel>(
        "auth-title", margin, 18, width, 26, "UltraAuthenticator");
    title->SetFont(Theme::kUiFont, Theme::kSizeTitle, FontWeight::Bold);
    title->SetTextColor(Theme::kTextPrimary);
    window_->AddChild(title);

    auto addBtn = std::make_shared<UltraCanvasButton>(
        "auth-add", margin, 54, 130, 32);
    addBtn->SetText("Add account");
    addBtn->onClick = [this]() { OpenAddAccountDialog(); };
    window_->AddChild(addBtn);

    auto pwBtn = std::make_shared<UltraCanvasButton>(
        "auth-pw", margin + 140, 54, 170, 32);
    pwBtn->SetText("Change master password");
    pwBtn->onClick = [this]() { OpenChangePasswordDialog(); };
    window_->AddChild(pwBtn);

    statusLabel_ = std::make_shared<UltraCanvasLabel>(
        "auth-status", margin, kHeaderHeight - 22, width, 18, "");
    statusLabel_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    statusLabel_->SetTextColor(Theme::kTextMuted);
    window_->AddChild(statusLabel_);

    // The list scrolls, so the account count is not capped by window height.
    listContainer_ = CreateScrollableContainer(
        "auth-list", margin, kHeaderHeight, width,
        kWindowHeight - kHeaderHeight - margin);
    window_->AddChild(listContainer_);

    emptyLabel_ = std::make_shared<UltraCanvasLabel>(
        "auth-empty", 0, 8, width, 40,
        "No accounts yet — choose \"Add account\" to begin.");
    emptyLabel_->SetFont(Theme::kUiFont, Theme::kSizeBody);
    emptyLabel_->SetTextColor(Theme::kTextMuted);
    listContainer_->AddChild(emptyLabel_);

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

void AuthenticatorWindow::SetStatus(const std::string& text, bool isError) {
    if (!statusLabel_) return;
    statusLabel_->SetText(text);
    statusLabel_->SetTextColor(isError ? Theme::kDanger : Theme::kTextMuted);
}

void AuthenticatorWindow::ClearRows() {
    if (!listContainer_) return;
    for (Row& row : rows_) {
        // Removing the card takes its children with it; the card is the only
        // thing the container knows about.
        if (row.card) listContainer_->RemoveChild(row.card);
    }
    rows_.clear();
}

void AuthenticatorWindow::RebuildRows() {
    if (!window_ || !listContainer_) return;
    ClearRows();

    std::vector<Account> accounts;
    StoreResult listed = store_.List(accounts);
    if (!listed) {
        SetStatus(listed.message, true);
        if (emptyLabel_) emptyLabel_->SetText("");
        return;
    }

    if (emptyLabel_) {
        emptyLabel_->SetText(
            accounts.empty()
                ? "No accounts yet — choose \"Add account\" to begin."
                : "");
    }

    const long cardWidth = kWindowWidth - 2 * Theme::kMargin - 18;  // scrollbar
    const long pad       = Theme::kCardPadding;

    for (size_t i = 0; i < accounts.size(); ++i) {
        const Account& account = accounts[i];
        const long y = static_cast<long>(i) *
                       (Theme::kCardHeight + Theme::kCardGap);
        const std::string id = "row" + std::to_string(i);

        Row row;
        row.key    = account.key;
        row.params = account.params;
        row.isHotp = (account.params.type == Otp::Type::Hotp);

        row.card = std::make_shared<UltraCanvasContainer>(
            id + "-card", 0, y, cardWidth, Theme::kCardHeight);
        row.card->SetBackgroundColor(Theme::kCardBackground);
        listContainer_->AddChild(row.card);

        // --- identity -----------------------------------------------------
        const std::string issuerText = account.params.issuer.empty()
                                           ? account.params.accountName
                                           : account.params.issuer;
        row.issuerLabel = std::make_shared<UltraCanvasLabel>(
            id + "-issuer", pad, pad, cardWidth - 2 * pad - 200, 20, issuerText);
        row.issuerLabel->SetFont(Theme::kUiFont, Theme::kSizeBody,
                                 FontWeight::Bold);
        row.issuerLabel->SetTextColor(Theme::kTextPrimary);
        row.card->AddChild(row.issuerLabel);

        row.accountLabel = std::make_shared<UltraCanvasLabel>(
            id + "-account", pad, pad + 20, cardWidth - 2 * pad - 200, 18,
            account.params.issuer.empty() ? SubtitleFor(account.params)
                                          : SubtitleFor(account.params));
        row.accountLabel->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
        row.accountLabel->SetTextColor(Theme::kTextSecondary);
        row.card->AddChild(row.accountLabel);

        // --- code ---------------------------------------------------------
        row.codeLabel = std::make_shared<UltraCanvasLabel>(
            id + "-code", pad, pad + 40, 220, 32,
            CodePlaceholder(account.params.digits));
        row.codeLabel->SetFont(Theme::kCodeFont, Theme::kSizeCode,
                               FontWeight::Bold);
        row.codeLabel->SetTextColor(Theme::kTextPrimary);
        row.card->AddChild(row.codeLabel);

        row.countdownLabel = std::make_shared<UltraCanvasLabel>(
            id + "-left", pad + 228, pad + 50, 110, 20, "");
        row.countdownLabel->SetFont(Theme::kUiFont, Theme::kSizeSecondary,
                                    FontWeight::Bold);
        row.countdownLabel->SetTextColor(Theme::kTextMuted);
        row.card->AddChild(row.countdownLabel);

        // --- actions ------------------------------------------------------
        // Right-aligned, laid out from the right edge inwards so the row does
        // not reflow when the HOTP-only button is absent.
        long bx = cardWidth - pad - 84;
        row.removeButton = std::make_shared<UltraCanvasButton>(
            id + "-del", bx, pad, 84, 28);
        row.removeButton->SetText("Remove");
        {
            const std::string key = account.key;
            row.removeButton->onClick = [this, key]() { RemoveAccount(key); };
        }
        row.card->AddChild(row.removeButton);

        bx -= 90;
        row.revealButton = std::make_shared<UltraCanvasButton>(
            id + "-reveal", bx, pad, 84, 28);
        row.revealButton->SetText("Show key");
        {
            const std::string key = account.key;
            row.revealButton->onClick = [this, key]() {
                OpenRevealSecretDialog(key);
            };
        }
        row.card->AddChild(row.revealButton);

        bx -= 74;
        row.editButton = std::make_shared<UltraCanvasButton>(
            id + "-edit", bx, pad, 68, 28);
        row.editButton->SetText("Edit");
        {
            const std::string key = account.key;
            row.editButton->onClick = [this, key]() {
                OpenEditAccountDialog(key);
            };
        }
        row.card->AddChild(row.editButton);

        if (row.isHotp) {
            // A counter-based code is only produced on demand: generating one
            // spends it, so it must be an explicit action, never a timer tick.
            row.actionButton = std::make_shared<UltraCanvasButton>(
                id + "-next", cardWidth - pad - 84, pad + 44, 84, 28);
            row.actionButton->SetText("Next code");
            const std::string key = account.key;
            row.actionButton->onClick = [this, key]() { AdvanceHotpRow(key); };
            row.card->AddChild(row.actionButton);
        }

        rows_.push_back(std::move(row));
    }

    RefreshCodes();
}

void AuthenticatorWindow::RefreshCodes() {
    if (!store_.IsOpen()) return;

    const int64_t now = NowUnix();
    for (Row& row : rows_) {
        if (row.isHotp) {
            // Nothing to tick: an HOTP code changes only when "Next code" is
            // pressed, and showing one until then would imply it is still
            // valid.
            continue;
        }
        std::string code;
        uint32_t remaining = 0;
        StoreResult generated = store_.GenerateTotp(row.key, now, code, remaining);
        if (!generated) {
            if (row.codeLabel) {
                row.codeLabel->SetText(CodePlaceholder(row.params.digits));
            }
            if (row.countdownLabel) row.countdownLabel->SetText("");
            SetStatus(generated.message, true);
            continue;
        }
        if (row.codeLabel) row.codeLabel->SetText(GroupCode(code));
        if (row.countdownLabel) {
            row.countdownLabel->SetText(std::to_string(remaining) + "s left");
            row.countdownLabel->SetTextColor(Theme::CountdownColor(remaining));
        }
    }
}

void AuthenticatorWindow::AdvanceHotpRow(const std::string& key) {
    std::string code;
    StoreResult advanced = store_.AdvanceHotp(key, code);
    if (!advanced) {
        SetStatus(advanced.message, true);
        return;
    }
    for (Row& row : rows_) {
        if (row.key != key) continue;
        if (row.codeLabel) row.codeLabel->SetText(GroupCode(code));
        if (row.countdownLabel) {
            row.countdownLabel->SetText("used once");
            row.countdownLabel->SetTextColor(Theme::kTextMuted);
        }
        // The stored counter has moved on; keep the subtitle honest.
        row.params.counter += 1;
        if (row.accountLabel) {
            row.accountLabel->SetText(SubtitleFor(row.params));
        }
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
                SetStatus(removed.message, true);
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

void AuthenticatorWindow::OpenEditAccountDialog(const std::string& key) {
    // Seed the form from what is on screen rather than re-reading the vault:
    // the row already holds the parameters, and re-reading would decrypt an
    // entry for no reason.
    const Otp::Parameters* current = nullptr;
    for (const Row& row : rows_) {
        if (row.key == key) { current = &row.params; break; }
    }
    if (!current) {
        SetStatus("That account is no longer in the list.", true);
        return;
    }

    auto dialog = std::make_shared<EditAccountDialog>();
    dialog->onAccept = [this, key](const Otp::Parameters& edited) -> std::string {
        std::string newKey;
        StoreResult updated = store_.Update(key, edited, newKey);
        if (!updated) return updated.message;
        RebuildRows();
        SetStatus(newKey == key ? "Updated " + newKey + "."
                                : "Renamed to " + newKey + ".");
        return std::string();
    };
    dialog->CreateEditAccountDialog(*current);
    dialog->ShowModal(window_.get());
}

void AuthenticatorWindow::OpenRevealSecretDialog(const std::string& key) {
    const Otp::Parameters* current = nullptr;
    for (const Row& row : rows_) {
        if (row.key == key) { current = &row.params; break; }
    }
    if (!current) {
        SetStatus("That account is no longer in the list.", true);
        return;
    }

    auto dialog = std::make_shared<RevealSecretDialog>();
    dialog->onReveal = [this, key](const std::string& password,
                                   std::string& outUri) -> std::string {
        // AccountStore::Reveal is the gate: it re-derives the vault key from
        // this password before it will hand anything back.
        UltraCryptSecureBuffer pw(password.data(), password.size());
        UltraCryptSecureBuffer uri;
        StoreResult revealed = store_.Reveal(key, pw, uri);
        if (!revealed) return revealed.message;
        outUri.assign(reinterpret_cast<const char*>(uri.Data()), uri.GetSize());
        return std::string();
    };
    dialog->CreateRevealSecretDialog(DisplayName(*current));
    dialog->ShowModal(window_.get());
}

void AuthenticatorWindow::OpenChangePasswordDialog() {
    auto dialog = std::make_shared<ChangePasswordDialog>();
    dialog->onAccept = [this](const std::string& current,
                              const std::string& next) -> std::string {
        UltraCryptSecureBuffer currentPw(current.data(), current.size());
        UltraCryptSecureBuffer nextPw(next.data(), next.size());
        StoreResult changed = store_.ChangePassword(currentPw, nextPw);
        if (!changed) return changed.message;
        SetStatus("Master password changed.");
        return std::string();
    };
    dialog->CreateChangePasswordDialog();
    dialog->ShowModal(window_.get());
}

} // namespace Authenticator
} // namespace UltraCanvas
