// Apps/UltraAuthenticator/RecoveryDialog.h
// Turns recovery from a forgotten master password on and off
// (EncryptedFileStore.h, *container version 2*).
//
// Turning it on draws a random 32-byte share, gives the vault a recovery
// slot opened by that share together with a second share kept in the vault
// file, and puts the share where the person can get it back without the
// master password:
//
//  - with their UltraAccount (Apps/UltraAccount), as a recovery key the
//    account service releases only after a code sent to the account's
//    verified email - or, on UltraAccount Plus, by text message to its
//    verified phone; and
//  - always, as a recovery code shown once, here, to write down: it works
//    without the account and without a network.
//
// Both are gated on the master password, as Show key is: they add a way into
// the vault, and the person at the screen now may not be the one who
// unlocked it. Turning recovery off removes the slot and, when signed in, the
// recovery key from the account.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef RECOVERYDIALOG_H
#define RECOVERYDIALOG_H

#include "AccountStore.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
namespace Authenticator {

class RecoveryDialog : public UltraCanvasModalDialog {
public:
    explicit RecoveryDialog(AccountStore& store) : store_(store) {}
    ~RecoveryDialog() override;

    void CreateRecoveryDialog();

    // A line for the window's status bar after a change.
    std::function<void(const std::string&)> onChanged;

private:
    void TurnOn();
    void TurnOff();
    void Refresh();
    void SetError(const std::string& text);
    void ClearCode();

    AccountStore& store_;
    std::shared_ptr<UltraCanvasLabel>     stateLabel_;
    std::shared_ptr<UltraCanvasLabel>     accountLabel_;
    std::shared_ptr<UltraCanvasCheckbox>  useAccount_;
    std::shared_ptr<UltraCanvasTextInput> accountEmail_;
    std::shared_ptr<UltraCanvasTextInput> accountPassword_;
    std::shared_ptr<UltraCanvasTextInput> accountTotp_;
    std::shared_ptr<UltraCanvasLabel>     emailCaption_;
    std::shared_ptr<UltraCanvasLabel>     passwordCaption_;
    std::shared_ptr<UltraCanvasLabel>     totpCaption_;
    std::shared_ptr<UltraCanvasTextInput> masterInput_;
    std::shared_ptr<UltraCanvasLabel>     codeCaption_;
    std::shared_ptr<UltraCanvasLabel>     codeLabel_;
    std::shared_ptr<UltraCanvasLabel>     errorLabel_;
    std::shared_ptr<UltraCanvasButton>    onButton_;
    std::shared_ptr<UltraCanvasButton>    offButton_;

    static constexpr long kDialogWidth  = 520;
    static constexpr long kDialogHeight = 640;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // RECOVERYDIALOG_H
