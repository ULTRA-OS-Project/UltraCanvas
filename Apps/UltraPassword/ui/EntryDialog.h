// Apps/UltraPassword/ui/EntryDialog.h
// Add or edit one password entry: title, website, user name, password (with a
// generator), the group it lives in, notes — and how the website signs the
// user in. The sign-in method picker shows, live, what the choice means and
// how it rates, so the dialog teaches as it records: picking "Password only"
// says plainly that the account rests on one secret.
//
// Same construction as the other dialogs (CreateVaultWizard, UltraMail's
// wizard): labelled rows in a flex column, Cancel / Save at the bottom right.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "core/PasswordVault.h"

#include "UltraCanvasWindow.h"

#include <functional>

namespace UltraPassword {

class EntryDialog {
public:
    // `entry` is the starting state: an empty entry with groupId set for
    // "Add", a copy of the stored one for "Edit" (its id says which).
    // onSubmit receives the edited entry; nothing is called on cancel.
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent,
                     const PasswordVault& vault, const PasswordEntry& entry,
                     std::function<void(PasswordEntry&)> onSubmit);
};

} // namespace UltraPassword
