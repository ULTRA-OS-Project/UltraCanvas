// Apps/UltraPassword/ui/EntryCard.h
// One password, as a card in the right-hand pane.
//
//   GitHub                                    [Passkey] [Very strong]
//   https://github.com/login
//   User name   erika@example.com                           [Copy]
//   Password    ••••••••••••                        [Show] [Copy]
//   A passkey (WebAuthn): no shared secret leaves your device, ...
//   Notes ...
//   Work / Cloud · changed 2026-10-03                   [Edit] [Delete]
//
// The two pills say how the website signs the user in and how well that
// resists account takeover; the line under them says why, in one sentence.
// The password is masked until Show is pressed, and the card never holds a
// copy of it: Show and Copy ask the owner for it by entry id.
//
// Built from catalogue elements only (container, labels, badges, buttons).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "core/PasswordVault.h"

#include "UltraCanvasContainer.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraPassword {

struct EntryCardCallbacks {
    // Returns the entry's current password (empty when gone).
    std::function<std::string(const std::string& entryId)> passwordOf;
    // Copy `text` to the clipboard; `what` names it for the status line.
    std::function<void(const std::string& text, const std::string& what)> copy;
    std::function<void(const std::string& entryId)> edit;
    std::function<void(const std::string& entryId)> remove;
    std::function<void(const std::string& url)> openWebsite;
};

std::shared_ptr<UltraCanvas::UltraCanvasContainer>
BuildEntryCard(const PasswordEntry& entry, const std::string& groupPath,
               bool highlighted, const EntryCardCallbacks& callbacks);

} // namespace UltraPassword
