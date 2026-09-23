// Apps/UltraAuthenticator/NewVaultDialog.h
// First launch: choose the master password for a vault that does not exist yet.
//
// This used to be the framework's one-line input dialog, which asked for the
// password once. Once is not enough for the password that cannot be recovered:
// a finger slipping on the first launch silently became the master password,
// and the user found out at the second launch, with the vault unreadable and
// nothing to fall back on. So the password is typed twice and the two must
// match, and a strength meter shows what the vault will be resting on while
// it is still cheap to change.
//
// Like the lock screen it cannot be waved away: the only ways out are a
// password or Quit. There is no vault yet, so "cancel" can only mean quit.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef NEWVAULTDIALOG_H
#define NEWVAULTDIALOG_H

#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPasswordStrengthMeter.h"
#include "UltraCanvasTextInput.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
namespace Authenticator {

class NewVaultDialog : public UltraCanvasModalDialog {
public:
    NewVaultDialog() = default;
    ~NewVaultDialog() override = default;

    void CreateNewVaultDialog();

    // Called with the confirmed password. Returns an error to display, or
    // empty when the vault was created. The dialog wipes its copies once the
    // handler returns, whatever the outcome.
    std::function<std::string(const std::string& password)> onAccept;

    // The user chose to quit rather than set a password.
    std::function<void()> onQuit;

    // Puts the caret in the first field so the person can just start typing.
    void ShowModal(UltraCanvasWindowBase* parent = nullptr) override;

private:
    void Accept();
    void SetError(const std::string& text);

    std::shared_ptr<UltraCanvasTextInput>             passwordInput_;
    std::shared_ptr<UltraCanvasTextInput>             confirmInput_;
    std::shared_ptr<UltraCanvasPasswordStrengthMeter> strengthMeter_;
    std::shared_ptr<UltraCanvasLabel>                 errorLabel_;

    bool created_  = false;
    bool quitting_ = false;

    static constexpr long kDialogWidth  = 460;
    static constexpr long kDialogHeight = 430;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // NEWVAULTDIALOG_H
