// Apps/UltraMail/ui/UltraMailPassphraseDialog.h
// The Primary Password prompt guarding the credential vault. UltraMail's mail
// passwords - and the key its stored mail is encrypted with - are sealed with
// this passphrase (UltraVault's Argon2id-derived key + XChaCha20-Poly1305),
// and it is never stored, so it has to be asked for: at every start when one
// is set, with confirmation when one is chosen, and to prove it before it is
// changed or removed (Settings > Privacy > Primary Password).
// Version: 0.7.0 - PassphrasePrompt: one dialog for unlocking, choosing,
//                  changing and removing; a mistake asks again in place
// Version: 0.4.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasWindow.h"

#include <functional>
#include <string>

namespace UltraMail {

// What one prompt asks for, and in which words.
struct PassphrasePrompt {
    std::string title = "Primary Password";       // the dialog's window title
    std::string heading = "Unlock UltraMail";
    std::string text = "Enter your Primary Password.";
    std::string fieldLabel = "Password";
    std::string placeholder = "Your Primary Password";
    std::string okLabel = "Unlock";
    std::string okIcon = "unlock";
    // A second field that must match the first: for choosing a new password,
    // where a typo would become the one password that cannot be recovered.
    bool        confirm = false;
    // A previous attempt's mistake, shown above the fields.
    std::string errorText;
    // Cancel or the window closed; nothing else is called then.
    std::function<void()> onCancel;
};

class PassphraseDialog {
public:
    // Ask as `prompt` says. onSubmit gets the passphrase. An empty one, or
    // two that differ, ask again with the reason in the dialog, so a gate
    // that waits for an answer (UltraMail's start) is never left without one.
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent, PassphrasePrompt prompt,
                     std::function<void(const std::string&)> onSubmit);

    // The Primary Password asked for: `firstRun` chooses one (two fields),
    // otherwise the one set is asked for. `errorText` as in PassphrasePrompt.
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent,
                     bool firstRun,
                     const std::string& errorText,
                     std::function<void(const std::string&)> onSubmit);
};

} // namespace UltraMail
