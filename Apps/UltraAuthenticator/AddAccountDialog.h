// Apps/UltraAuthenticator/AddAccountDialog.h
// Manual account entry: the "type the setup key" path every authenticator
// needs, and the only way to add an account until the camera/QR flow lands
// (Docs/UltraAuthenticator/UltraAuthenticator-Investigation.md §2.2c).
//
// The dialog collects issuer, account name and the Base32 setup key, builds an
// `otpauth://` URI from them and hands that to AccountStore. Building a URI
// rather than passing fields straight through means manual entry and a future
// QR scan converge on exactly one validated code path.
//
// The secret field is a real password-mode UltraCanvasTextInput, so the key is
// masked while typing — shoulder-surfing a setup key is as good as stealing
// the account.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ADDACCOUNTDIALOG_H
#define ADDACCOUNTDIALOG_H

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
namespace Authenticator {

class AddAccountDialog : public UltraCanvasModalDialog {
public:
    AddAccountDialog() = default;
    ~AddAccountDialog() override = default;

    void CreateAddAccountDialog();

    // Called with the assembled otpauth:// URI when the user confirms. The
    // handler returns an error message to display, or an empty string on
    // success (which closes the dialog). Returning the message rather than
    // closing regardless keeps a rejected key on screen for correction
    // instead of making the user retype it.
    std::function<std::string(const std::string& uri)> onAccept;

private:
    void Accept();
    void SetError(const std::string& text);

    std::shared_ptr<UltraCanvasTextInput> issuerInput_;
    std::shared_ptr<UltraCanvasTextInput> accountInput_;
    std::shared_ptr<UltraCanvasTextInput> secretInput_;
    std::shared_ptr<UltraCanvasLabel>     errorLabel_;

    static constexpr long kDialogWidth  = 460;
    static constexpr long kDialogHeight = 320;
    static constexpr long kMargin       = 20;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // ADDACCOUNTDIALOG_H
