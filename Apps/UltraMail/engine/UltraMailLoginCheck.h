// Apps/UltraMail/engine/UltraMailLoginCheck.h
// The login check behind the server settings page: one folder listing against
// the incoming server with the account's credentials proves the host, the
// port, the TLS mode and the sign-in in one go — the same session a sync would
// open, so what passes here syncs. SMTP has no sign-in-only operation in the
// plug-in interface, so the outgoing server is not checked.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

namespace UltraMail {

class LoginCheck {
public:
    // The session options an IMAP session for `imap` uses: TLS mode from the
    // security setting, the credentials as given (the username defaults to
    // the settings' username when the credentials carry none).
    static UltraNetMailOptions OptionsFor(const MailServerSettings& imap,
                                          const UltraNetCredentials& credentials);

    // LIST once. Ok when the server answered the sign-in; otherwise the
    // plug-in's reason (host not found, refused, TLS, authentication).
    // Blocking on the network — run off the UI thread.
    static UltraNetResult Imap(IMailboxProtocolPlugin& mailbox,
                               const MailServerSettings& imap,
                               const UltraNetCredentials& credentials);
};

} // namespace UltraMail
