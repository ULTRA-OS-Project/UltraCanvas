// Apps/UltraMail/engine/UltraMailComposer.h
// Outgoing-message model and the reply / forward / new-message builders. Pure
// logic: subject prefixes (Re:/Fwd:), quoted bodies, In-Reply-To / References
// threading headers and reply-all recipient derivation. Sending is handled
// separately by MailSender.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailMimeCodec.h"   // Attachment

#include <string>
#include <vector>

namespace UltraMail {

// An editable outgoing message.
struct Draft {
    std::string fromName;
    std::string fromAddr;
    std::vector<std::string> to;
    std::vector<std::string> cc;
    std::vector<std::string> bcc;
    std::string subject;
    std::string body;                 // UTF-8
    bool        bodyIsHtml = false;
    std::vector<Attachment> attachments;
    std::string inReplyTo;            // Message-ID being answered
    std::string references;           // References header chain
};

// The message a reply / forward is derived from.
struct SourceMessage {
    std::string messageId;
    std::string references;
    std::string fromName;
    std::string fromAddr;
    std::vector<std::string> to;
    std::vector<std::string> cc;
    std::string subject;
    std::string body;                 // already decoded (via MimeCodec)
    std::string date;                 // for the quote attribution line
    std::vector<Attachment> attachments;
};

class Composer {
public:
    // A blank message from the given identity.
    static Draft NewMessage(const std::string& selfName, const std::string& selfAddr);

    // Reply (or reply-all) to `src`. Recipients exclude the user's own address.
    static Draft Reply(const SourceMessage& src, const std::string& selfName,
                       const std::string& selfAddr, bool replyAll);

    // Forward `src` (carries its attachments; recipients left empty).
    static Draft Forward(const SourceMessage& src, const std::string& selfName,
                         const std::string& selfAddr);
};

} // namespace UltraMail
