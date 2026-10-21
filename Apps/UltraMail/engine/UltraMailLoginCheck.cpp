// Apps/UltraMail/engine/UltraMailLoginCheck.cpp
// Version: 0.2.0 - Incoming (was Imap)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailLoginCheck.h"

#include "UltraMailDiscovery.h"

#include <vector>

namespace UltraMail {

UltraNetMailOptions LoginCheck::OptionsFor(const MailServerSettings& incoming,
                                           const UltraNetCredentials& credentials) {
    UltraNetMailOptions opts;
    opts.credentials = credentials;
    if (opts.credentials.username.empty()) opts.credentials.username = incoming.username;
    ApplyConnection(incoming, opts);   // TLS mode + authentication method
    // A check should answer quickly: a wrong host is a wrong host.
    opts.connectTimeoutMs   = 10000;
    opts.operationTimeoutMs = 20000;
    return opts;
}

UltraNetResult LoginCheck::Incoming(IMailboxProtocolPlugin& mailbox,
                                    const MailServerSettings& incoming,
                                    const UltraNetCredentials& credentials) {
    if (!incoming.Valid())
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "the incoming server and port must be given");
    std::vector<UltraNetMailFolder> folders;
    return mailbox.ListFolders(AutoDiscovery::IncomingServerUrl(incoming), folders,
                               OptionsFor(incoming, credentials));
}

} // namespace UltraMail
