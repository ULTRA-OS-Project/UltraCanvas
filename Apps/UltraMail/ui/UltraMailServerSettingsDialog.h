// Apps/UltraMail/ui/UltraMailServerSettingsDialog.h
// The manual server settings page: incoming (IMAP) and outgoing (SMTP) host,
// port and security, plus the username — shown when neither the provider
// table nor the autoconfig lookup knew the address, prefilled with whatever
// was found or guessed, and reachable again for an account whose servers are
// unknown. Save validates in place and — when a verifier is given — checks the
// sign-in at the incoming server before the page closes; a failed check shows
// the reason and offers "Save anyway". Nothing is called on Cancel.
// With `AccountFields::edit` set it doubles as the account settings page: a
// display-name row above the servers, and - for a password account - a
// password row, or - for an OAuth account - a "Sign in again" button.
// Version: 0.3.0 - doubles as the account settings page (name + password / OAuth)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailDiscovery.h"

#include "UltraCanvasWindow.h"

#include <UltraNet/UltraNetCore.h>

#include <functional>
#include <string>
#include <utility>

namespace UltraMail {

class ServerSettingsDialog {
public:
    // Extra fields shown when the page is used to edit an existing account.
    // Left defaulted (edit = false) the page is server-only, as during setup.
    struct AccountFields {
        bool        edit = false;          // show the display-name + credential controls
        std::string displayName;           // prefill for the name field
        bool        acceptsPassword = true;// show the password field
        bool        canOAuth = false;      // show the "Sign in with <provider>" button
        std::string providerName;          // OAuth button label, e.g. "Google"
        // App-wide view option surfaced here (the only "Settings" page): the
        // current value seeds the checkbox; Save returns it in Result. Same
        // value regardless of which account's settings are open.
        bool        showReadingPane = true;
        // App-wide too: whether UltraMail may download the icons of the known
        // services in its sender registry into the sender-icon cache.
        bool        fetchSenderIcons = true;
    };

    // What Save hands back. `settings` is always filled; the rest are only
    // meaningful when AccountFields::edit was set.
    struct Result {
        DiscoveryResult settings;      // validated servers (found = true, source = "manual")
        std::string     displayName;   // edited name (empty -> caller falls back to the local part)
        std::string     newPassword;   // non-empty only when the user typed one
        bool            reauth = false;// the OAuth "Sign in again" button was used
        bool            showReadingPane = true;   // the reading-pane checkbox state
        bool            fetchSenderIcons = true;  // the sender-icon checkbox state
    };

    // The login check: run it for `candidate` (off the UI thread - it talks to
    // the server) and call `onResult` on the UI thread with the outcome.
    using Verifier = std::function<void(const Result& candidate,
                                        std::function<void(UltraNetResult)> onResult)>;

    // `intro` is the sentence above the fields (why the page is shown).
    // `prefill` seeds the server fields (found is ignored). onSave receives the
    // validated Result. `verify`, when given, runs before the page closes; null
    // saves unchecked. `account` turns on the display-name + credential rows.
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent,
                     const std::string& email,
                     const std::string& intro,
                     const DiscoveryResult& prefill,
                     std::function<void(const Result&)> onSave,
                     Verifier verify,
                     const AccountFields& account);

    // Server-only convenience (the setup / failed-sync path): no name or
    // credential rows. (A defaulted `account` argument cannot value-init the
    // still-incomplete nested type, so this is a separate overload.)
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent,
                     const std::string& email,
                     const std::string& intro,
                     const DiscoveryResult& prefill,
                     std::function<void(const Result&)> onSave,
                     Verifier verify = nullptr) {
        Show(parent, email, intro, prefill, std::move(onSave), std::move(verify),
             AccountFields{});
    }
};

} // namespace UltraMail
