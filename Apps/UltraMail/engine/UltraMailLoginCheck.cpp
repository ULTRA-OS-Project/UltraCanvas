// Apps/UltraMail/engine/UltraMailLoginCheck.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailLoginCheck.h"

#include "UltraMailDiscovery.h"

#include <vector>

namespace UltraMail {

UltraNetMailOptions LoginCheck::OptionsFor(const MailServerSettings& imap,
                                           const UltraNetCredentials& credentials) {
    UltraNetMailOptions opts;
    opts.credentials = credentials;
    if (opts.credentials.username.empty()) opts.credentials.username = imap.username;
    opts.useTls      = imap.security != MailSecurity::Plain;
    opts.implicitTls = imap.security == MailSecurity::SslTls;
    // A check should answer quickly: a wrong host is a wrong host.
    opts.connectTimeoutMs   = 10000;
    opts.operationTimeoutMs = 20000;
    return opts;
}

UltraNetResult LoginCheck::Imap(IMailboxProtocolPlugin& mailbox, const MailServerSettings& imap,
                                const UltraNetCredentials& credentials) {
    if (!imap.Valid())
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "the incoming server and port must be given");
    std::vector<UltraNetMailFolder> folders;
    return mailbox.ListFolders(AutoDiscovery::ImapServerUrl(imap), folders,
                               OptionsFor(imap, credentials));
}

} // namespace UltraMail
