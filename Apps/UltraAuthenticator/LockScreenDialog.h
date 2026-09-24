// Apps/UltraAuthenticator/LockScreenDialog.h
// The screen shown over the account list once the vault has been locked —
// by the idle timer, by minimising the window, or by the Lock button — and
// the one shown at launch over an existing vault, so that the first unlock
// and every later one go through the same code and the same back-off. The
// launch prompt used to be a separate input dialog that quit the app on a
// wrong password; a typo then cost a restart, while a guesser paid nothing
// more than that.
//
// It cannot be dismissed. There is no Cancel, Escape does nothing, and the
// window manager's close button is refused: the only ways out are the master
// password or Quit. A lock screen that could be waved away would be a
// decoration, and the codes behind it would be one click from anyone passing.
//
// The password check and the back-off between attempts both live in
// AccountStore, not here. This dialog asks how long it must wait, greys the
// button and counts down, but if it did not, the store would still refuse.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef LOCKSCREENDIALOG_H
#define LOCKSCREENDIALOG_H

#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
namespace Authenticator {

class LockScreenDialog : public UltraCanvasModalDialog {
public:
    LockScreenDialog() = default;
    ~LockScreenDialog() override = default;

    // `message` is shown to the user in full, e.g. "Locked after 5 minutes
    // without input. Enter your master password to show the codes again."
    // The caller composes it because the same dialog is the launch-time
    // unlock, where nothing was "locked" and the wording differs.
    void CreateLockScreenDialog(const std::string& message);

    // Returns an error to display, or empty when the vault is open again. The
    // password is wiped by this dialog once the handler returns.
    std::function<std::string(const std::string& password)> onUnlock;

    // Seconds until the next attempt is accepted, 0 when one may be made now.
    // Polled from Tick() and after every failure.
    std::function<uint32_t()> onQueryWait;

    // The user chose to quit rather than unlock.
    std::function<void()> onQuit;

    // Called about once a second by the owner while the dialog is up, so the
    // "try again in N s" countdown stays live without a timer of its own.
    void Tick();

    // Puts the caret in the password field, so the person coming back to the
    // desk can just type. A lock screen that first needs a click is one that
    // gets a password typed into whatever was behind it.
    void ShowModal(UltraCanvasWindowBase* parent = nullptr) override;

private:
    void Attempt();
    void RefreshWaitState();
    void SetError(const std::string& text);

    std::shared_ptr<UltraCanvasLabel>     reasonLabel_;
    std::shared_ptr<UltraCanvasTextInput> passwordInput_;
    std::shared_ptr<UltraCanvasLabel>     errorLabel_;
    std::shared_ptr<UltraCanvasButton>    unlockButton_;

    bool unlocked_ = false;
    bool quitting_ = false;
    uint32_t lastWaitShown_ = 0;

    static constexpr long kDialogWidth  = 440;
    static constexpr long kDialogHeight = 300;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // LOCKSCREENDIALOG_H
