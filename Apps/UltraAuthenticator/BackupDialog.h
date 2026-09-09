// Apps/UltraAuthenticator/BackupDialog.h
// Writing and restoring the encrypted backup file (§3.5).
//
// One dialog for both directions, because they are the same form with
// different fields and splitting them would duplicate the passphrase handling
// — the part most worth getting right once.
//
// Export asks for three things: the master password (this reads every seed in
// the vault, so it re-authenticates exactly as the reveal and password-change
// flows do), a *separate* backup passphrase, and a confirmation of it. A
// mistyped backup passphrase is a uniquely cruel failure — nothing looks wrong
// until the day the backup is needed — so it is typed twice, and the mismatch
// is caught here rather than after the file is written.
//
// Import asks only for the backup passphrase. Restoring adds accounts to an
// already-unlocked vault, which is what the Add and Scan paths do without a
// second prompt; nothing is extracted, so the argument for re-authenticating
// does not apply.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef BACKUPDIALOG_H
#define BACKUPDIALOG_H

#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
namespace Authenticator {

class BackupDialog : public UltraCanvasModalDialog {
public:
    enum class Mode { Export, Import };

    explicit BackupDialog(Mode mode) : mode_(mode) {}
    ~BackupDialog() override = default;

    void CreateBackupDialog();

    // Export: called with the master password and the chosen backup
    // passphrase. Import: `masterPassword` is empty and only `passphrase` is
    // meaningful. Returns an error to display, or empty on success.
    //
    // Choosing the file is the handler's job, not this dialog's: the native
    // save/open dialog belongs to the window that owns the store.
    std::function<std::string(const std::string& masterPassword,
                              const std::string& passphrase)> onConfirm;

private:
    void Accept();
    void SetError(const std::string& text);

    Mode mode_;

    std::shared_ptr<UltraCanvasTextInput> masterInput_;    // export only
    std::shared_ptr<UltraCanvasTextInput> passphraseInput_;
    std::shared_ptr<UltraCanvasTextInput> confirmInput_;   // export only
    std::shared_ptr<UltraCanvasLabel>     errorLabel_;

    static constexpr long kDialogWidth = 460;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // BACKUPDIALOG_H
