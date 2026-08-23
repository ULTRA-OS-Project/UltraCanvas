// Apps/UltraAuthenticator/ChangePasswordDialog.h
// Changes the master password the vault key is derived from.
//
// The vault could already do this — EncryptedFileStore::ChangePassword has
// existed and been tested since the store was written — but nothing in the app
// could reach it, so in practice the password chosen at first launch was
// permanent. This is the missing plumbing.
//
// The current password is asked for even though the vault is open. Being
// unlocked proves somebody knew the password at launch; it does not prove the
// person now at the keyboard does, and a walk-up attacker who could silently
// re-key the vault would lock the owner out of their own second factors.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef CHANGEPASSWORDDIALOG_H
#define CHANGEPASSWORDDIALOG_H

#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
namespace Authenticator {

class ChangePasswordDialog : public UltraCanvasModalDialog {
public:
    ChangePasswordDialog() = default;
    ~ChangePasswordDialog() override = default;

    void CreateChangePasswordDialog();

    // Returns an error to display, or empty on success. Both passwords are
    // wiped by this dialog once the handler returns, whatever the outcome.
    std::function<std::string(const std::string& current,
                              const std::string& next)> onAccept;

private:
    void Accept();
    void SetError(const std::string& text);

    std::shared_ptr<UltraCanvasTextInput> currentInput_;
    std::shared_ptr<UltraCanvasTextInput> nextInput_;
    std::shared_ptr<UltraCanvasTextInput> confirmInput_;
    std::shared_ptr<UltraCanvasLabel>     errorLabel_;

    static constexpr long kDialogWidth  = 440;
    static constexpr long kDialogHeight = 340;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // CHANGEPASSWORDDIALOG_H
