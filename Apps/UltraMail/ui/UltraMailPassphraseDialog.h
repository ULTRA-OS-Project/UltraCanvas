// Apps/UltraMail/ui/UltraMailPassphraseDialog.h
// The master-password prompt guarding the credential vault. UltraMail's mail
// passwords are sealed with this passphrase (UltraVault's Argon2id-derived key
// + XChaCha20-Poly1305), and it is never stored, so it has to be asked for:
// once per session to unlock an existing vault, and once with confirmation to
// choose one when no vault exists yet.
// Version: 0.4.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasWindow.h"

#include <functional>
#include <string>

namespace UltraMail {

class PassphraseDialog {
public:
    // Ask for the master password. `firstRun` switches between "choose one"
    // (two fields, must match) and "enter yours" (one field). onSubmit gets
    // the passphrase; nothing is called on cancel, and the caller stays locked.
    // `errorText`, when set, is shown above the fields — used to re-prompt
    // after a wrong password without stacking an alert on top of the dialog.
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent,
                     bool firstRun,
                     const std::string& errorText,
                     std::function<void(const std::string&)> onSubmit);
};

} // namespace UltraMail
