// Apps/UltraPassword/ui/CreateVaultWizard.h
// The "Create password vault" dialog, opened from the start page. It is
// built like UltraMail's "Add email account" wizard (UltraMailAccountWizard):
// a short intro line, labelled input rows, a hint, and Cancel / Create at the
// bottom right.
//
// What it asks for, and why:
//  - a name for the vault (shown at the top of the tree);
//  - the master password, twice: it cannot be recovered, so a typo on the
//    first entry must not silently become it;
//  - a strength meter under it, live, while the password is still cheap to
//    change;
//  - the key-protection profile: Argon2id at 1 GiB (Maximum, the default) or
//    256 MiB (Strong) for a machine that cannot spare the memory.
//
// Validation happens in the dialog — an empty or mismatched password keeps it
// open with a red line saying why, so nothing typed is lost. onSubmit runs
// only with a valid draft.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "core/VaultFile.h"

#include "UltraCanvasWindow.h"

#include <functional>
#include <string>

namespace UltraPassword {

struct VaultDraft {
    std::string name;
    std::string password;   // the caller wipes it once the vault is created
    KdfProfile  profile = KdfProfile::Maximum;
};

class CreateVaultWizard {
public:
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent,
                     std::function<void(VaultDraft&)> onSubmit);
};

// Minimum master-password length the wizard and Change password insist on.
constexpr size_t kMinMasterPasswordLength = 10;

} // namespace UltraPassword
