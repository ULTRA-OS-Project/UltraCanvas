// Apps/UltraAuthenticator/RecoverVaultDialog.h
// "Forgot password" on the lock screen: opens the vault with its recovery
// key instead of the master password, and sets a new master password.
//
// The vault's header names where its recovery key is kept (it is readable
// without the password, VaultRecoveryInfo): the account service and the
// recovery key's id. The person chooses where the code goes - the
// account's email, or on UltraAccount Plus its phone - types the code, and
// the service releases the key. Or they type the recovery code they wrote
// down when recovery was turned on. Either way the key is used once: the
// vault's recovery slot is spent by the recovery, the account's copy is
// removed when this machine is signed in, and the person is asked to turn
// recovery on again.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef RECOVERVAULTDIALOG_H
#define RECOVERVAULTDIALOG_H

#include "AccountStore.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace Authenticator {

class RecoverVaultDialog : public UltraCanvasModalDialog {
public:
    RecoverVaultDialog(AccountStore& store, std::function<int64_t()> now) : store_(store), now_(std::move(now)) {}

    void CreateRecoverVaultDialog();

    // The vault is open again, with the new master password.
    std::function<void()> onRecovered;

private:
    void SendCode(const std::string& channel);
    void Recover();
    void SetError(const std::string& text, bool isError = true);

    AccountStore& store_;
    std::function<int64_t()> now_;
    VaultRecoveryInfo info_;
    std::shared_ptr<UltraCanvasTextInput> codeInput_;
    std::shared_ptr<UltraCanvasTextInput> recoveryCodeInput_;
    std::shared_ptr<UltraCanvasTextInput> newPassword_;
    std::shared_ptr<UltraCanvasTextInput> confirmPassword_;
    std::shared_ptr<UltraCanvasLabel>     errorLabel_;

    static constexpr long kDialogWidth  = 500;
    static constexpr long kDialogHeight = 600;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // RECOVERVAULTDIALOG_H
