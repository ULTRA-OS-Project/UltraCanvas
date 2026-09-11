// Apps/UltraMail/ui/UltraMailServerSettingsDialog.h
// The manual server settings page: incoming (IMAP) and outgoing (SMTP) host,
// port and security, plus the username — shown when neither the provider
// table nor the autoconfig lookup knew the address, prefilled with whatever
// was found or guessed, and reachable again for an account whose servers are
// unknown. Save validates in place and — when a verifier is given — checks the
// sign-in at the incoming server before the page closes; a failed check shows
// the reason and offers "Save anyway". Nothing is called on Cancel.
// Version: 0.2.0 - login check before saving
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailDiscovery.h"

#include "UltraCanvasWindow.h"

#include <UltraNet/UltraNetCore.h>

#include <functional>
#include <string>

namespace UltraMail {

class ServerSettingsDialog {
public:
    // The login check: run it for `settings` (off the UI thread — it talks to
    // the server) and call `onResult` on the UI thread with the outcome.
    using Verifier = std::function<void(const DiscoveryResult& settings,
                                        std::function<void(UltraNetResult)> onResult)>;

    // `intro` is the sentence above the fields (why the page is shown).
    // `prefill` seeds the fields (found is ignored). onSave receives the
    // validated settings with found = true and source = "manual".
    // `verify`, when given, runs before the page closes; null saves unchecked.
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent,
                     const std::string& email,
                     const std::string& intro,
                     const DiscoveryResult& prefill,
                     std::function<void(const DiscoveryResult&)> onSave,
                     Verifier verify = nullptr);
};

} // namespace UltraMail
