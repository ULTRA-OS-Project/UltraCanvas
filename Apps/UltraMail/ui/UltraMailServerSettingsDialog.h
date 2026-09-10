// Apps/UltraMail/ui/UltraMailServerSettingsDialog.h
// The manual server settings page: incoming (IMAP) and outgoing (SMTP) host,
// port and security, plus the username — shown when neither the provider
// table nor the autoconfig lookup knew the address, prefilled with whatever
// was found or guessed, and reachable again for an account whose servers are
// unknown. Save validates in place; nothing is called on Cancel.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailDiscovery.h"

#include "UltraCanvasWindow.h"

#include <functional>
#include <string>

namespace UltraMail {

class ServerSettingsDialog {
public:
    // `intro` is the sentence above the fields (why the page is shown).
    // `prefill` seeds the fields (found is ignored). onSave receives the
    // validated settings with found = true and source = "manual".
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent,
                     const std::string& email,
                     const std::string& intro,
                     const DiscoveryResult& prefill,
                     std::function<void(const DiscoveryResult&)> onSave);
};

} // namespace UltraMail
