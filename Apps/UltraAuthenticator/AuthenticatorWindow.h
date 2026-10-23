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
// Copying: the copy button beside a code puts that code on the clipboard,
// marked secret (ClipboardHint::Secret) so clipboard histories that honour
// the platform's marker do not record it, and takes it off again 30 seconds
// later — only if the clipboard still holds that code, so whatever the user
// copied since is left alone. Locking and closing the window clear it at
// once, under the same condition. The copy works in hidden-codes mode too
// (the code goes to the clipboard without being shown), and an HOTP card
// copies the code its last "Next code" produced, never spending a new one.
// The setup key is never offered for copying; see RevealSecretDialog.
//
// Look: a white header band (logo, name, how many accounts) with Settings and
// Lock at its right; one row of actions under it — the two ways to add an
// account first, the vault's own actions to the right; then the cards, and a
// status line at the foot. A card is a rounded white panel: a coloured
// monogram for the service, its name and account, the code in large
// monospaced digits, and a ring (UltraCanvasGaugeDiagramElement,
// CircularRing) that empties as the code's time step runs out, with the
// seconds left in its centre. The per-account actions are icon buttons with
// tooltips, so a list of twenty accounts is not a wall of "Edit Show key
// Remove".
//
// Version: 0.5.0 - copy button on every card, clipboard cleared after 30 s
// Version: 0.4.0 - header band, monogram, countdown ring, icon actions, status bar
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef AUTHENTICATORWINDOW_H
#define AUTHENTICATORWINDOW_H

#include "AccountStore.h"
#include "LockScreenDialog.h"
#include "Preferences.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasBadge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasWindow.h"
#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"

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
        int64_t                               copiedUntil = 0;     // the copy button's tick
        std::string                           hotpCode;            // the last "Next code"
        std::shared_ptr<UltraCanvasContainer>  card;
        std::shared_ptr<UltraCanvasBadge>      monogram;
        std::shared_ptr<UltraCanvasLabel>      issuerLabel;
        std::shared_ptr<UltraCanvasLabel>      accountLabel;
        std::shared_ptr<UltraCanvasLabel>      codeLabel;
        std::shared_ptr<UltraCanvasLabel>      countdownLabel;   // "click to show", "used once"
        std::shared_ptr<UltraCanvasGaugeDiagramElement> countdownRing;   // TOTP only
        std::shared_ptr<UltraCanvasButton>     actionButton;   // Next (HOTP)
        std::shared_ptr<UltraCanvasButton>     copyButton;
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
    void CopyCode(const std::string& key);
    // Empties the clipboard if it still holds the code this window put there,
    // and forgets that code either way.
    void ClearClipboardIfOurs();
    void RefreshCopyButtons(int64_t now);
    void SetStatus(const std::string& text, bool isError = false);
    // The line under the app name: how many accounts, or that it is locked.
    void SetSummary(const std::string& text);
    void ShowEmptyState(bool show);

    // --- locking ----------------------------------------------------------
    // Lock() is idempotent and safe to call from any of its triggers. The lock
    // screen is shown at once when the window is visible, otherwise on the
    // first tick after it is restored, so a minimised window does not pop a
    // dialog onto the desktop.
    void Lock(const std::string& reason);
    void ShowLockScreen();
    void NoteActivity();
    void ApplyPreferences(const Preferences& edited);

    // Create() also starts locked when the store is attached to a vault that
    // has not been opened yet (AccountStore::Attach): that is how the launch
    // unlock shares this path.
    //
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
    std::shared_ptr<UltraCanvasLabel>     summaryLabel_;
    std::shared_ptr<UltraCanvasLabel>     statusLabel_;
    std::shared_ptr<UltraCanvasLabel>     emptyLabel_;
    std::shared_ptr<UltraCanvasLabel>     emptyHint_;
    std::vector<Row>                      rows_;

    TimerId refreshTimer_ = 0;
    bool    timerRunning_ = false;

    bool                               locked_ = false;
    std::string                        lockMessage_;   // what the lock screen says
    std::shared_ptr<LockScreenDialog>  lockDialog_;
    int64_t                            lastActivity_ = 0;

    std::string                        clipboardCode_;   // what we copied, to clear only that
    int64_t                            clipboardClearAt_ = 0;
    int                                modalDepth_ = 0;
    int                                ticksSinceRestore_ = 0;   // see Tick()

    static constexpr long kWindowWidth  = 720;
    static constexpr long kWindowHeight = 640;
    // The white band with the logo, name, Settings and Lock.
    static constexpr long kBandHeight   = 76;
    // The band plus the row of actions under it: where the list starts.
    static constexpr long kHeaderHeight = 144;
    // The status line at the foot of the window.
    static constexpr long kFooterHeight = 32;
    // How long a click keeps a hidden code visible. Long enough to type it
    // into another window, short enough that a card left revealed does not
    // stay so.
    static constexpr int64_t kRevealSeconds = 15;
    // How long a copied code stays on the clipboard: one TOTP step, after
    // which the code is no longer valid anyway.
    static constexpr int64_t kClipboardSeconds = 30;
    // How long the copy button shows its tick after a copy.
    static constexpr int64_t kCopiedSeconds = 2;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // AUTHENTICATORWINDOW_H
