// Apps/UltraMail/ui/UltraMailSecurityDialog.h
// An account's mail security, in one dialog: its S/MIME certificate (import a
// .p12 / .pfx, remove), the OpenPGP key it signs with (from the user's GnuPG
// keyring; by default the key whose user id is the account's address), and
// how its new messages start out - Off, S/MIME or OpenPGP, signed, encrypted.
// The compose window's Security row starts from that and stays the writer's
// to change per message.
//
// Opened from Account Settings (the Security row) and at the end of adding an
// account. The certificate is imported and removed at once, as in the vault;
// the rest is saved on Save.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"   // AccountSecurity

#include "UltraCanvasWindow.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace UltraMail {

class SecurityDialog {
public:
    struct Options {
        std::string email;                 // the account's address
        AccountSecurity current;           // what Save starts from
        // Shown under the header, e.g. at the end of adding an account.
        std::string intro;

        // S/MIME: whether this build has it, the certificate in a few words
        // ("" for none), and the actions. Import shows its own file dialog
        // and password prompt over `page` and hands back the new summary.
        bool smimeBuilt = false;
        std::string smimeSummary;
        std::function<void(UltraCanvas::UltraCanvasWindowBase* page,
                           std::function<void(const std::string& summary)> done)> onImportSmime;
        std::function<std::string()> onRemoveSmime;

        // OpenPGP: whether this build has it (and GnuPG runs), and the secret
        // keys to choose from - fingerprint and how it reads.
        bool openPgpBuilt = false;
        std::vector<std::pair<std::string, std::string>> openPgpKeys;

        // Save: the new defaults. Cancel (or `cancelLabel`, "Skip" at the end
        // of adding an account) or the window closed: nothing, then onClosed.
        std::function<void(const AccountSecurity&)> onSave;
        std::string cancelLabel = "Cancel";
        // After the dialog is gone, whichever way it closed.
        std::function<void()> onClosed;
    };

    static void Show(UltraCanvas::UltraCanvasWindowBase* parent, Options options);

    // The account's security in a few words, for the Account Settings row:
    // how new messages start ("Off", "OpenPGP: signed, encrypted"), and
    // whether there is an S/MIME certificate.
    static std::string Describe(const AccountSecurity& security, bool hasCertificate);
};

} // namespace UltraMail
