// Apps/UltraMail/ui/UltraMailServerSettingsDialog.h
// The manual server settings page: the protocol of the incoming server (IMAP
// or JMAP), the incoming and outgoing (SMTP) host, port, security and
// authentication method, plus the username — shown when neither the provider
// table nor the autoconfig lookup knew the address, prefilled with whatever
// was found or guessed, and reachable again for an account whose servers are
// unknown. Save validates in place and — when a verifier is given — checks the
// sign-in at the incoming server before the page closes; a failed check shows
// the reason and offers "Save anyway". Nothing is called on Cancel.
// With `AccountFields::edit` set it doubles as the account settings page: a
// display-name row above the servers, and - for a password account - a
// password row, or - for an OAuth account - a "Sign in again" button - and a
// Signature row whose "Edit signature…" button opens the signature editor.
// Version: 0.8.0 - the Security row (summary, "Mail security..." - the security
//                  dialog) in place of the S/MIME row
// Version: 0.7.0 - askPassword / initialPassword / keepExisting: an editable
//                  password field on the setup and server-re-entry pages, so a
//                  failed sign-in can be fixed in place
// Version: 0.6.1 - a password account switched between IMAP and JMAP must
//                  enter its password again (AccountFields::signedInWithOAuth
//                  exempts an OAuth2 account)
// Version: 0.6.0 - the Protocol row: IMAP or JMAP for the incoming server
// Version: 0.6.0 - the S/MIME row: the account's certificate, Import... and Remove
// Version: 0.5.0 - the Signature row (account settings page)
// Version: 0.4.0 - an authentication method per server
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailDiscovery.h"
#include "UltraMailTypes.h"   // Signature

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
        // Show the password row on a page that is NOT the full `edit` settings
        // page - the add-account setup and the "servers unknown, re-enter them"
        // re-entry - so a wrong password can be fixed in place instead of
        // leaving the user stuck on a failed sign-in. `initialPassword` prefills
        // it (the password just typed in the add-account wizard); `keepExisting`
        // picks the "leave blank to keep the current password" wording for an
        // account that already has one stored (re-entry), rather than "Password".
        bool        askPassword = false;
        std::string initialPassword;
        bool        keepExisting = false;
        bool        canOAuth = false;      // show the "Sign in with <provider>" button
        std::string providerName;          // OAuth button label, e.g. "Google"
        // The account signs in with stored OAuth2 tokens, not its password:
        // a protocol switch then does not ask for the password again.
        bool        signedInWithOAuth = false;
        // When set (account settings only), a red "Delete account" button is
        // shown in the bottom row; clicking it closes this page and runs the
        // callback, which owns the confirm-and-remove flow.
        std::function<void()> onDelete;
        // When set (account settings only), a Signature row shows `signature`
        // in a few words and an "Edit signature…" button. The editor saves on
        // its own Save - not on this page's, which checks the sign-in first -
        // and hands the new signature to this callback.
        Signature signature;
        std::function<void(const Signature&)> onSaveSignature;
        // When set (account settings only), a Security row: the account's
        // mail security in a few words (`securitySummary`) and a "Mail
        // security..." button. The caller shows the security dialog over
        // `page`; it saves on its own Save and hands the row its new summary.
        std::string securitySummary;
        std::function<void(UltraCanvas::UltraCanvasWindowBase* page,
                           std::function<void(const std::string& summary)> done)> onEditSecurity;
    };

    // What Save hands back. `settings` is always filled; the rest are only
    // meaningful when AccountFields::edit was set.
    struct Result {
        DiscoveryResult settings;      // validated servers (found = true, source = "manual")
        std::string     displayName;   // edited name (empty -> caller falls back to the local part)
        std::string     newPassword;   // non-empty only when the user typed one
        bool            reauth = false;// the OAuth "Sign in again" button was used
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
