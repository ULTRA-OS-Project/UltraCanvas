// Apps/UltraAuthenticator/AuthenticatorWindow.cpp
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS

#include "AuthenticatorWindow.h"
#include "AddAccountDialog.h"
#include "BackupDialog.h"
#include "ChangePasswordDialog.h"
#include "EditAccountDialog.h"
#include "RevealSecretDialog.h"
#include "ScanAccountDialog.h"
#include "SettingsDialog.h"
#include "Theme.h"

#include "UltraCanvasFileLoader.h"
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

// What a hidden code looks like: one bullet per digit, grouped like the code.
// Built by hand because the bullet is three bytes in UTF-8 and GroupCode
// counts bytes.
std::string CodeMask(uint32_t digits) {
    const std::string bullet = "\xE2\x80\xA2";   // U+2022
    std::string out;
    const uint32_t groupAt = (digits == 8) ? 4 : 3;
    for (uint32_t i = 0; i < digits; ++i) {
        if (i == groupAt && (digits == 6 || digits == 8)) out += " ";
        out += bullet;
    }
    return out;
}

int64_t NowUnix() {
    return static_cast<int64_t>(std::time(nullptr));
}

std::string MinutesText(uint32_t seconds) {
    if (seconds % 60 == 0 && seconds >= 60) {
        const uint32_t minutes = seconds / 60;
        return std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
    }
    return std::to_string(seconds) + " seconds";
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
                                         AccountStore& store,
                                         const Preferences& preferences,
                                         const std::string& preferencesPath)
    : app_(app), store_(store), prefs_(preferences), prefsPath_(preferencesPath) {}

AuthenticatorWindow::~AuthenticatorWindow() {
    if (timerRunning_) {
        app_.StopTimer(refreshTimer_);
        timerRunning_ = false;
    }
}

template <class Dialog>
void AuthenticatorWindow::ShowTracked(const std::shared_ptr<Dialog>& dialog) {
    ++modalDepth_;
    auto previous = dialog->onResult;
    dialog->onResult = [this, previous](DialogResult result) {
        if (modalDepth_ > 0) --modalDepth_;
        // Time spent in the dialog was not idleness, even though none of its
        // input reached this window's filter.
        NoteActivity();
        if (previous) previous(result);
    };
    dialog->ShowModal(window_.get());
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

    // Idle means no input *to this window*. Anything the user does here —
    // clicks, keys, even moving the pointer — resets the clock. The filter
    // never consumes; it only takes the time.
    window_->InstallEventFilter(
        "auth-activity",
        [this](const UCEvent&) { NoteActivity(); return false; },
        {UCEventType::MouseDown, UCEventType::MouseUp, UCEventType::MouseMove,
         UCEventType::MouseWheel, UCEventType::KeyDown, UCEventType::KeyUp});

    // Minimising is a signal that the user has stopped looking; lock now so
    // that nothing is on the cards when the window comes back.
    window_->onWindowMinimize = [this]() {
        if (prefs_.lockOnMinimize && modalDepth_ == 0) Lock("The window was minimised.");
    };
    // No onWindowRestore handler on purpose: the lock screen is put up by the
    // next Tick() once IsMinimized() is false again. Showing it from inside
    // the restore notification centred it on where the window *had* been
    // before the window manager finished putting it back.

    const long margin = Theme::kMargin;
    const long width  = kWindowWidth - 2 * margin;

    auto title = std::make_shared<UltraCanvasLabel>(
        "auth-title", margin, 18, width, 26, "UltraAuthenticator");
    title->SetFont(Theme::kUiFont, Theme::kSizeTitle, FontWeight::Bold);
    title->SetTextColor(Theme::kTextPrimary);
    window_->AddChild(title);

    // Scanning first: it is how an account is normally added, and typing a
    // 32-character key is the fallback, not the default.
    auto scanBtn = std::make_shared<UltraCanvasButton>(
        "auth-scan", margin, 54, 130, 32);
    scanBtn->SetText("Scan QR code");
    scanBtn->onClick = [this]() { OpenScanAccountDialog(); };
    window_->AddChild(scanBtn);

    auto addBtn = std::make_shared<UltraCanvasButton>(
        "auth-add", margin + 140, 54, 130, 32);
    addBtn->SetText("Enter key");
    addBtn->onClick = [this]() { OpenAddAccountDialog(); };
    window_->AddChild(addBtn);

    // Lock sits alone at the right of the first row: it is the one action
    // that should be findable without reading, when someone is leaving the
    // desk.
    auto lockBtn = std::make_shared<UltraCanvasButton>(
        "auth-lock", kWindowWidth - margin - Theme::kButtonLock, 54,
        Theme::kButtonLock, 32);
    lockBtn->SetText("Lock");
    lockBtn->onClick = [this]() { Lock("Locked by request."); };
    window_->AddChild(lockBtn);

    // Second row: things that act on the vault rather than on one account.
    // Wide enough for the whole label — the button clips rather than shrinking
    // its text, and "Change master p..." reads as a truncated menu item.
    auto pwBtn = std::make_shared<UltraCanvasButton>(
        "auth-pw", margin, 94, 250, 32);
    pwBtn->SetText("Change master password");
    pwBtn->onClick = [this]() { OpenChangePasswordDialog(); };
    window_->AddChild(pwBtn);

    auto backupBtn = std::make_shared<UltraCanvasButton>(
        "auth-backup", margin + 260, 94, 110, 32);
    backupBtn->SetText("Back up…");
    backupBtn->onClick = [this]() { OpenBackupDialog(); };
    window_->AddChild(backupBtn);

    auto restoreBtn = std::make_shared<UltraCanvasButton>(
        "auth-restore", margin + 380, 94, 110, 32);
    restoreBtn->SetText("Restore…");
    restoreBtn->onClick = [this]() { OpenRestoreDialog(); };
    window_->AddChild(restoreBtn);

    auto settingsBtn = std::make_shared<UltraCanvasButton>(
        "auth-settings", kWindowWidth - margin - Theme::kButtonSettings, 94,
        Theme::kButtonSettings, 32);
    settingsBtn->SetText("Settings…");
    settingsBtn->onClick = [this]() { OpenSettingsDialog(); };
    window_->AddChild(settingsBtn);

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
        "No accounts yet — choose \"Scan QR code\" to begin, or \"Enter key\" to type one in.");
    emptyLabel_->SetFont(Theme::kUiFont, Theme::kSizeBody);
    emptyLabel_->SetTextColor(Theme::kTextMuted);
    listContainer_->AddChild(emptyLabel_);

    RebuildRows();
    NoteActivity();

    // One periodic timer drives every row and the auto-lock. 1 Hz is the
    // coarsest rate that still makes the countdown look live.
    refreshTimer_ = app_.StartTimer(1000, true, [this](TimerId) { Tick(); });
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

void AuthenticatorWindow::NoteActivity() {
    lastActivity_ = NowUnix();
}

// ---------------------------------------------------------------------------
// The clock
// ---------------------------------------------------------------------------

void AuthenticatorWindow::Tick() {
    const int64_t now = NowUnix();

    if (locked_) {
        if (lockDialog_) {
            lockDialog_->Tick();
        } else if (window_ && !window_->IsMinimized()) {
            // Wait one extra tick after a restore. The window manager can
            // park a window it is un-iconifying at a temporary position for
            // a few hundred milliseconds before moving it back; a dialog
            // centred during that moment lands wherever the parent was
            // parked, clamped to the screen edge.
            if (++ticksSinceRestore_ >= 2) ShowLockScreen();
        } else {
            ticksSinceRestore_ = 0;
        }
        return;
    }

    // No auto-lock while a dialog is open; see ShowTracked.
    if (modalDepth_ == 0) {
        if (prefs_.lockOnMinimize && window_ && window_->IsMinimized()) {
            // Belt and braces for a platform that never delivers the minimise
            // event: the state is polled as well.
            Lock("The window was minimised.");
            return;
        }
        if (prefs_.idleLockSeconds > 0 &&
            now - lastActivity_ >= static_cast<int64_t>(prefs_.idleLockSeconds)) {
            Lock("Locked after " + MinutesText(prefs_.idleLockSeconds) +
                 " without input.");
            return;
        }
    }

    RefreshCodes();
}

// ---------------------------------------------------------------------------
// Locking
// ---------------------------------------------------------------------------

void AuthenticatorWindow::Lock(const std::string& reason) {
    if (locked_) return;
    locked_     = true;
    lockReason_ = reason;

    // Order: take the codes off the screen, then drop the vault. Nothing on a
    // card survives the first step, and nothing in memory survives the second.
    ClearRows();
    if (emptyLabel_) emptyLabel_->SetText("");
    SetStatus("Locked.");
    store_.Lock();

    ticksSinceRestore_ = 0;
    if (window_ && !window_->IsMinimized()) ShowLockScreen();
}

void AuthenticatorWindow::ShowLockScreen() {
    if (lockDialog_) return;

    auto dialog = std::make_shared<LockScreenDialog>();
    lockDialog_ = dialog;

    dialog->onQueryWait = [this]() {
        return store_.SecondsUntilUnlockAllowed(NowUnix());
    };
    dialog->onUnlock = [this](const std::string& password) -> std::string {
        UltraCryptSecureBuffer pw(password.data(), password.size());
        StoreResult unlocked = store_.Unlock(pw, NowUnix());
        if (!unlocked) return unlocked.message;
        return std::string();
    };
    dialog->onQuit = [this]() {
        store_.Close();
        app_.RequestExit();
    };
    dialog->onResult = [this](DialogResult result) {
        lockDialog_.reset();
        if (result != DialogResult::OK) return;   // quit; the app is exiting
        locked_ = false;
        lockReason_.clear();
        NoteActivity();
        SetStatus("");
        RebuildRows();
    };

    dialog->CreateLockScreenDialog(lockReason_);
    dialog->ShowModal(window_.get());
}

// ---------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------

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
                ? "No accounts yet — choose \"Scan QR code\" to begin, or \"Enter key\" to type one in."
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
            id + "-issuer", pad, pad, cardWidth - 2 * pad - 290, 20, issuerText);
        row.issuerLabel->SetFont(Theme::kUiFont, Theme::kSizeBody,
                                 FontWeight::Bold);
        row.issuerLabel->SetTextColor(Theme::kTextPrimary);
        row.card->AddChild(row.issuerLabel);

        row.accountLabel = std::make_shared<UltraCanvasLabel>(
            id + "-account", pad, pad + 20, cardWidth - 2 * pad - 290, 18,
            SubtitleFor(account.params));
        row.accountLabel->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
        row.accountLabel->SetTextColor(Theme::kTextSecondary);
        row.card->AddChild(row.accountLabel);

        // --- code ---------------------------------------------------------
        // Bottom band, with a full card padding below it: at the previous
        // height this label's box ended exactly on the card border.
        row.codeLabel = std::make_shared<UltraCanvasLabel>(
            id + "-code", pad, pad + 42, 230, 34,
            CodePlaceholder(account.params.digits));
        row.codeLabel->SetFont(Theme::kCodeFont, Theme::kSizeCode,
                               FontWeight::Bold);
        row.codeLabel->SetTextColor(Theme::kTextPrimary);
        if (prefs_.hideCodes) {
            // A label with a click handler reports the hand cursor, which is
            // the affordance: the mask is something to click.
            const std::string key = account.key;
            row.codeLabel->onClick = [this, key]() { RevealRow(key); };
        }
        row.card->AddChild(row.codeLabel);

        // Wide enough for "click to show" in bold; at 110 it clipped to
        // "click to sh…", which reads as a cut-off menu entry.
        row.countdownLabel = std::make_shared<UltraCanvasLabel>(
            id + "-left", pad + 240, pad + 50, 130, 20, "");
        row.countdownLabel->SetFont(Theme::kUiFont, Theme::kSizeSecondary,
                                    FontWeight::Bold);
        row.countdownLabel->SetTextColor(Theme::kTextMuted);
        row.card->AddChild(row.countdownLabel);

        // --- actions ------------------------------------------------------
        // Right-aligned, laid out from the right edge inwards so the row does
        // not reflow when the HOTP-only button is absent.
        long bx = cardWidth - pad - Theme::kButtonRemove;
        row.removeButton = std::make_shared<UltraCanvasButton>(
            id + "-del", bx, pad, Theme::kButtonRemove, 28);
        row.removeButton->SetText("Remove");
        {
            const std::string key = account.key;
            row.removeButton->onClick = [this, key]() { RemoveAccount(key); };
        }
        row.card->AddChild(row.removeButton);

        bx -= Theme::kButtonReveal + Theme::kButtonGap;
        row.revealButton = std::make_shared<UltraCanvasButton>(
            id + "-reveal", bx, pad, Theme::kButtonReveal, 28);
        row.revealButton->SetText("Show key");
        {
            const std::string key = account.key;
            row.revealButton->onClick = [this, key]() {
                OpenRevealSecretDialog(key);
            };
        }
        row.card->AddChild(row.revealButton);

        bx -= Theme::kButtonEdit + Theme::kButtonGap;
        row.editButton = std::make_shared<UltraCanvasButton>(
            id + "-edit", bx, pad, Theme::kButtonEdit, 28);
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
                id + "-next", cardWidth - pad - Theme::kButtonNext, pad + 46,
                Theme::kButtonNext, 28);
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
        const bool hidden = prefs_.hideCodes && now >= row.revealedUntil;

        if (row.isHotp) {
            // Nothing to tick: an HOTP code changes only when "Next code" is
            // pressed, and showing one until then would imply it is still
            // valid. In hidden mode the code shown by that press is masked
            // again once its reveal window closes.
            if (hidden && row.revealedUntil != 0) {
                row.revealedUntil = 0;
                if (row.codeLabel) row.codeLabel->SetText(CodeMask(row.params.digits));
                if (row.countdownLabel) row.countdownLabel->SetText("");
            } else if (hidden && row.codeLabel &&
                       row.codeLabel->GetText() == CodePlaceholder(row.params.digits)) {
                row.codeLabel->SetText(CodeMask(row.params.digits));
            }
            continue;
        }

        if (hidden) {
            // Not even generated: a hidden code costs no decryption.
            if (row.codeLabel) row.codeLabel->SetText(CodeMask(row.params.digits));
            if (row.countdownLabel) {
                row.countdownLabel->SetText("click to show");
                row.countdownLabel->SetTextColor(Theme::kTextMuted);
            }
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

void AuthenticatorWindow::RevealRow(const std::string& key) {
    if (!prefs_.hideCodes) return;
    for (Row& row : rows_) {
        if (row.key != key) continue;
        // A hidden HOTP card has nothing to reveal: its code exists only once
        // "Next code" produces it, and that press shows it (AdvanceHotpRow).
        // Clicking the mask must not spend a counter, so it does nothing.
        if (row.isHotp) return;
        row.revealedUntil = NowUnix() + kRevealSeconds;
    }
    RefreshCodes();
}

void AuthenticatorWindow::AdvanceHotpRow(const std::string& key) {
    std::string code;
    StoreResult advanced = store_.AdvanceHotp(key, code);
    if (!advanced) {
        SetStatus(advanced.message, true);
        return;
    }
    const int64_t now = NowUnix();
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
        // In hidden mode the freshly produced code is on screen for the same
        // window a click would give, then masked again.
        row.revealedUntil = prefs_.hideCodes ? now + kRevealSeconds : 0;
    }
    SetStatus("");
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

void AuthenticatorWindow::RemoveAccount(const std::string& key) {
    // Deleting an account destroys a second factor, so it is confirmed rather
    // than done on a single click.
    ++modalDepth_;
    UltraCanvasDialogManager::ShowConfirmation(
        "Remove \"" + key + "\"?\n\n"
        "You will not be able to sign in with this account's codes again "
        "unless you still have its setup key or recovery codes.",
        "Remove account",
        [this, key](bool confirmed) {
            if (modalDepth_ > 0) --modalDepth_;
            NoteActivity();
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
    ShowTracked(dialog);
}

void AuthenticatorWindow::OpenScanAccountDialog() {
    auto dialog = std::make_shared<ScanAccountDialog>(app_);
    // Deliberately the same handler as manual entry: a scanned URI gets no
    // shortcut past the parser that typed keys go through.
    dialog->onScanned = [this](const std::string& uri) -> std::string {
        std::string key;
        StoreResult added = store_.AddFromUri(uri, key);
        if (!added) return added.message;
        RebuildRows();
        SetStatus("Added " + key + ".");
        return std::string();
    };
    dialog->CreateScanAccountDialog();
    ShowTracked(dialog);
}

void AuthenticatorWindow::OpenSettingsDialog() {
    auto dialog = std::make_shared<SettingsDialog>();
    dialog->onSave = [this](const Preferences& edited) { ApplyPreferences(edited); };
    dialog->CreateSettingsDialog(prefs_);
    ShowTracked(dialog);
}

void AuthenticatorWindow::ApplyPreferences(const Preferences& edited) {
    const bool hideChanged = (edited.hideCodes != prefs_.hideCodes);
    prefs_ = edited;
    if (!prefs_.Save(prefsPath_)) {
        SetStatus("Settings applied for this session but could not be saved to " +
                      prefsPath_,
                  true);
    } else {
        SetStatus("Settings saved.");
    }
    // The card click handlers exist only in hidden mode, so a change of that
    // setting is a rebuild, not a refresh.
    if (hideChanged) {
        RebuildRows();
    } else {
        RefreshCodes();
    }
}

void AuthenticatorWindow::OpenBackupDialog() {
    auto dialog = std::make_shared<BackupDialog>(BackupDialog::Mode::Export);
    dialog->onConfirm = [this](const std::string& master,
                               const std::string& passphrase) -> std::string {
        // Check this here, not only in ExportAll. The authoritative refusal
        // lives with the data, but it fires after the file picker — so relying
        // on it alone would make the user choose a save location, watch the
        // dialog close, and then find the rejection in the status line with
        // the fields gone.
        if (master == passphrase) {
            return "The backup passphrase must be different from your master "
                   "password.";
        }

        // Copy what the handler needs before the dialog wipes its widgets;
        // the file picker is asynchronous and returns after this call.
        auto masterCopy = std::make_shared<UltraCryptSecureBuffer>(
            master.data(), master.size());
        auto passCopy = std::make_shared<UltraCryptSecureBuffer>(
            passphrase.data(), passphrase.size());

        FileDialogOptions opts;
        opts.title = "Save account backup";
        opts.defaultFileName = "accounts.ucaexport";
        opts.AddFilter("Authenticator backup", "ucaexport");
        opts.parentWindow = window_.get();

        // The picker is a dialog too, as far as the auto-lock is concerned.
        ++modalDepth_;
        UltraCanvasFileLoader::SaveFileDialog(
            opts, [this, masterCopy, passCopy](DialogResult result,
                                               const std::string& path) {
                if (modalDepth_ > 0) --modalDepth_;
                NoteActivity();
                if (result != DialogResult::OK || path.empty()) {
                    masterCopy->Clear();
                    passCopy->Clear();
                    return;
                }
                size_t count = 0;
                StoreResult exported =
                    store_.ExportAll(*masterCopy, *passCopy, path, count);
                masterCopy->Clear();
                passCopy->Clear();
                if (!exported) {
                    SetStatus(exported.message, true);
                    return;
                }
                SetStatus("Backed up " + std::to_string(count) +
                          (count == 1 ? " account to " : " accounts to ") + path);
            });
        // The picker decides the outcome, so nothing to report here. Errors
        // from the export itself land in the status line above.
        return std::string();
    };
    dialog->CreateBackupDialog();
    ShowTracked(dialog);
}

void AuthenticatorWindow::OpenRestoreDialog() {
    auto dialog = std::make_shared<BackupDialog>(BackupDialog::Mode::Import);
    dialog->onConfirm = [this](const std::string&,
                               const std::string& passphrase) -> std::string {
        auto passCopy = std::make_shared<UltraCryptSecureBuffer>(
            passphrase.data(), passphrase.size());

        FileDialogOptions opts;
        opts.title = "Open account backup";
        opts.AddFilter("Authenticator backup", "ucaexport");
        opts.parentWindow = window_.get();

        ++modalDepth_;
        UltraCanvasFileLoader::OpenFileDialog(
            opts, [this, passCopy](DialogResult result,
                                   const std::string& path) {
                if (modalDepth_ > 0) --modalDepth_;
                NoteActivity();
                if (result != DialogResult::OK || path.empty()) {
                    passCopy->Clear();
                    return;
                }
                AccountStore::ImportSummary summary;
                StoreResult imported =
                    store_.ImportAll(*passCopy, path, summary);
                passCopy->Clear();
                if (!imported) {
                    SetStatus(imported.message, true);
                    return;
                }
                RebuildRows();

                // Report every category. "Restored 38" while silently dropping
                // two would be indistinguishable from restoring all forty.
                std::string message = "Restored " +
                                      std::to_string(summary.added);
                if (summary.skippedExisting > 0) {
                    message += ", kept " +
                               std::to_string(summary.skippedExisting) +
                               " already present";
                }
                if (summary.rejected > 0) {
                    message += ", could not read " +
                               std::to_string(summary.rejected);
                }
                message += ".";
                SetStatus(message, summary.rejected > 0);
            });
        return std::string();
    };
    dialog->CreateBackupDialog();
    ShowTracked(dialog);
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
    ShowTracked(dialog);
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
    ShowTracked(dialog);
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
    ShowTracked(dialog);
}

} // namespace Authenticator
} // namespace UltraCanvas
