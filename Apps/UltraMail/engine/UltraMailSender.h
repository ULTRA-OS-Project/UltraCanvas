// Apps/UltraMail/engine/UltraMailSender.h
// Sends a Draft through an SMTP mail plug-in (UltraNet's IMailProtocolPlugin).
// Kept behind the interface so it is testable with a fake plug-in — no live
// server required.
// Version: 0.2.0 - Send takes a raw message (signed and/or encrypted) to send as it is
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailComposer.h"

#include <UltraNet/UltraNetPlugins.h>

#include <string>

namespace UltraMail {

class MailSender {
public:
    explicit MailSender(IMailProtocolPlugin& smtp) : smtp_(smtp) {}

    // Build the RFC 5322 message from the draft and send it via SMTP. The
    // server URL ("smtp(s)://host:port/") and credentials come from `options`
    // (serverUrl is overwritten with `serverUrl`).
    UltraNetResult Send(const Draft& draft, const std::string& serverUrl,
                        const UltraNetMailOptions& options,
                        const std::string& rawMessage = std::string());

private:
    IMailProtocolPlugin& smtp_;
};

} // namespace UltraMail
