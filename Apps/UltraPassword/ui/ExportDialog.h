// Apps/UltraPassword/ui/ExportDialog.h
// "Export…" — three ways out of the vault, each a button with one line on
// what it produces:
//
//  - Vault file: a copy of the encrypted vault (the same .upwvault format the
//    passwords are kept in), opening with the current master password. The
//    normal backup, and how a vault moves to another computer.
//  - Vault file, own password: the same format under a separate password,
//    for a backup kept apart or a vault handed to someone else.
//  - Plain CSV: unencrypted, for importing into a browser or another password
//    manager. Marked as the dangerous choice, and confirmed again before any
//    file is written.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasWindow.h"

#include <functional>

namespace UltraPassword {

enum class ExportChoice { VaultCopy, VaultWithPassword, PlainCsv };

class ExportDialog {
public:
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent,
                     std::function<void(ExportChoice)> onChoice);
};

} // namespace UltraPassword
