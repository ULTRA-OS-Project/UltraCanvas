// Apps/UltraMail/engine/UltraMailComposer.h
// Outgoing-message model and the reply / forward / new-message builders. Pure
// logic: subject prefixes (Re:/Fwd:), quoted bodies, In-Reply-To / References
// threading headers and reply-all recipient derivation. Sending is handled
// separately by MailSender.
// Version: 0.2.0 - drafts carry a formatted body (richBody), its text version
//                  and cid: parts; sources carry the HTML body and its pictures
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailMimeCodec.h"   // Attachment

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas { class UCRichDocument; }

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
    // An HTML body's plain-text version, sent beside it (multipart/
    // alternative), and the pictures it shows through cid: links.
    std::string textBody;
    std::vector<Attachment> inlineParts;   // isInline, contentId set
    // While composing: the formatted body (a reply or forward of an HTML
    // message). Null = the plain-text body above. RenderRichBody turns it
    // into body / textBody / inlineParts for sending.
    std::shared_ptr<UltraCanvas::UCRichDocument> richBody;
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
    // The HTML body, when the message has one: a reply or forward keeps its
    // formatting (see UltraMailRichComposer.h). Empty = plain text only.
    std::string bodyHtml;
    // The bytes of a picture bodyHtml shows, by its src (cid:, data:, a
    // remote address the reader already loaded). Empty = not available: the
    // picture is left out of the quote. Unset = no pictures.
    std::function<std::vector<uint8_t>(const std::string& src)> image;
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
