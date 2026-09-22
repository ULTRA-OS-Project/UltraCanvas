// Apps/UltraAuthenticator/AuthenticatorWindow.h
// The main window: one card per account, each showing its current code and the
// seconds left before it rolls over.
//
// Cards are built from catalogue elements (UltraCanvasContainer,
// UltraCanvasLabel, UltraCanvasButton) rather than painted, per the framework
// rule in AGENTS.md. That is not bureaucracy here: the code is text a user will
// want to select and copy while typing it into another window, and a painted
// one could not be.
//
// Layout: the account list lives in a scrolling container, so the number of
// accounts is not bounded by the window height. The first version capped the
// list at eight and said so in the status line, which was honest but not much
// use to anyone with nine.
//
// Refresh model: a single 1 Hz periodic timer re-reads the codes for every
// visible row. One timer for the window rather than one per account keeps the
// rows in step — codes that roll over at visibly different moments look
// broken, even when each is individually correct. The same tick drives the
// auto-lock, so there is exactly one clock in the window.
//
// Locking: the vault can be locked without quitting — by the Lock button, by a
// period without input, or by minimising the window (each configurable in
// Settings, except the button). Locking clears every card and drops the
// decrypted vault (AccountStore::Lock), then puts LockScreenDialog over the
// window until the master password is typed again. A locked window therefore
// holds no more than one that was never unlocked, which is the point: the
// codes are the second factor, and an unlocked authenticator left on a desk
// is a second factor left on a desk.
//
// Hidden codes: with "hide codes" on, a card shows "••• •••" until clicked,
// then its code for a few seconds. While hidden the seed is not even read —
// RefreshCodes skips the row — so the option reduces decryptions as well as
// what is on screen.
//
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef AUTHENTICATORWINDOW_H
#define AUTHENTICATORWINDOW_H

#include "AccountStore.h"
#include "LockScreenDialog.h"
#include "Preferences.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasWindow.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace Authenticator {

class AuthenticatorWindow {
public:
    // `preferencesPath` is where Settings are saved; `preferences` is what was
    // loaded from it (or the defaults when the file does not exist yet).
    AuthenticatorWindow(UltraCanvasApplication& app, AccountStore& store,
                        const Preferences& preferences,
                        const std::string& preferencesPath);
    ~AuthenticatorWindow();

    AuthenticatorWindow(const AuthenticatorWindow&) = delete;
    AuthenticatorWindow& operator=(const AuthenticatorWindow&) = delete;

    bool Create();
    void Show();

private:
    // One account's card. Held so the timer can update the code and countdown
    // in place instead of rebuilding the window every second.
    struct Row {
        std::string                           key;
        Otp::Parameters                       params;
        bool                                  isHotp = false;
        int64_t                               revealedUntil = 0;   // hidden-codes mode
        std::shared_ptr<UltraCanvasContainer>  card;
        std::shared_ptr<UltraCanvasLabel>      issuerLabel;
        std::shared_ptr<UltraCanvasLabel>      accountLabel;
        std::shared_ptr<UltraCanvasLabel>      codeLabel;
        std::shared_ptr<UltraCanvasLabel>      countdownLabel;
        std::shared_ptr<UltraCanvasButton>     actionButton;   // Next (HOTP)
        std::shared_ptr<UltraCanvasButton>     editButton;
        std::shared_ptr<UltraCanvasButton>     revealButton;
        std::shared_ptr<UltraCanvasButton>     removeButton;
    };

    void RebuildRows();
    void ClearRows();
    void RefreshCodes();
    void Tick();
    void OpenAddAccountDialog();
    void OpenScanAccountDialog();
    void OpenEditAccountDialog(const std::string& key);
    void OpenRevealSecretDialog(const std::string& key);
    void OpenChangePasswordDialog();
    void OpenBackupDialog();
    void OpenRestoreDialog();
    void OpenSettingsDialog();
    void RemoveAccount(const std::string& key);
    void AdvanceHotpRow(const std::string& key);
    void RevealRow(const std::string& key);
    void SetStatus(const std::string& text, bool isError = false);

    // --- locking ----------------------------------------------------------
    // Lock() is idempotent and safe to call from any of its triggers. The lock
    // screen is shown at once when the window is visible, otherwise on the
    // first tick after it is restored, so a minimised window does not pop a
    // dialog onto the desktop.
    void Lock(const std::string& reason);
    void ShowLockScreen();
    void NoteActivity();
    void ApplyPreferences(const Preferences& edited);

    // Every dialog goes through this so the window knows when one is up. The
    // auto-lock stands down while a dialog is open: a dialog is not idleness,
    // and a lock screen stacked under a half-filled form would be a mess.
    template <class Dialog>
    void ShowTracked(const std::shared_ptr<Dialog>& dialog);

    UltraCanvasApplication& app_;
    AccountStore&           store_;
    Preferences             prefs_;
    std::string             prefsPath_;

    std::shared_ptr<UltraCanvasWindow>    window_;
    std::shared_ptr<UltraCanvasContainer> listContainer_;
    std::shared_ptr<UltraCanvasLabel>     statusLabel_;
    std::shared_ptr<UltraCanvasLabel>     emptyLabel_;
    std::vector<Row>                      rows_;

    TimerId refreshTimer_ = 0;
    bool    timerRunning_ = false;

    bool                               locked_ = false;
    std::string                        lockReason_;
    std::shared_ptr<LockScreenDialog>  lockDialog_;
    int64_t                            lastActivity_ = 0;
    int                                modalDepth_ = 0;
    int                                ticksSinceRestore_ = 0;   // see Tick()

    static constexpr long kWindowWidth  = 720;
    static constexpr long kWindowHeight = 620;
    // Two button rows: account actions, then vault actions.
    static constexpr long kHeaderHeight = 148;
    // How long a click keeps a hidden code visible. Long enough to type it
    // into another window, short enough that a card left revealed does not
    // stay so.
    static constexpr int64_t kRevealSeconds = 15;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // AUTHENTICATORWINDOW_H
