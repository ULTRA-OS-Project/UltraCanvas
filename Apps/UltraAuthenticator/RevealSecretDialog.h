// Apps/UltraAuthenticator/RevealSecretDialog.h
// Shows an account's setup key, so it can be enrolled on another device.
//
// This is the one place in the app where a seed is put on screen, and it is a
// deliberate reversal of the rule the rest of the app follows. The reasoning,
// so nobody has to reconstruct it later: an authenticator that can only ever
// swallow secrets strands its users when they change phone, and they respond
// by keeping the original QR code in a photo album or an email — somewhere far
// worse than this vault. Every audited authenticator (Aegis, andOTP, 2FAS)
// offers export for exactly this reason.
//
// It is fenced rather than forbidden:
//
//  - The master password is required again. The vault being open is not
//    sufficient; unlocking happened at launch and the person at the screen now
//    may not be the person who unlocked it. AccountStore::Reveal enforces
//    this, not this dialog — the gate lives with the data, not with the UI.
//  - The dialog opens on the password step. Nothing about the account is
//    displayed until the password is accepted.
//  - The secret is shown only after an explicit second action, and the dialog
//    wipes it from its own widgets when it closes.
//
// What this cannot do is stop someone photographing the screen. That is the
// point of the operation, not a flaw in it.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef REVEALSECRETDIALOG_H
#define REVEALSECRETDIALOG_H

#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
namespace Authenticator {

class RevealSecretDialog : public UltraCanvasModalDialog {
public:
    RevealSecretDialog() = default;
    ~RevealSecretDialog() override;

    void CreateRevealSecretDialog(const std::string& displayName);

    // Given the typed password, returns either the account's otpauth:// URI in
    // `outUri`, or a non-empty error string. The caller owns the gate; this
    // dialog only presents whatever comes back.
    std::function<std::string(const std::string& password,
                              std::string& outUri)> onReveal;

private:
    void Attempt();
    void SetError(const std::string& text);
    void ShowSecret(const std::string& uri);
    void WipeShownSecret();

    std::shared_ptr<UltraCanvasTextInput> passwordInput_;
    std::shared_ptr<UltraCanvasLabel>     errorLabel_;
    std::shared_ptr<UltraCanvasLabel>     promptLabel_;

    // Built only once the password is accepted.
    std::shared_ptr<UltraCanvasLabel>     secretCaption_;
    std::shared_ptr<UltraCanvasTextInput> secretField_;
    std::shared_ptr<UltraCanvasLabel>     uriCaption_;
    std::shared_ptr<UltraCanvasTextInput> uriField_;
    std::shared_ptr<UltraCanvasLabel>     handlingWarning_;

    bool revealed_ = false;

    static constexpr long kDialogWidth  = 520;
    static constexpr long kDialogHeight = 380;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // REVEALSECRETDIALOG_H
