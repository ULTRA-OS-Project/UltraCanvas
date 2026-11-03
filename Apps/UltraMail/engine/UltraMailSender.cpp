// Apps/UltraMail/engine/UltraMailSender.cpp
// Version: 0.5.0 - a raw message (signed and/or encrypted) sent as it is
// Version: 0.4.0 - sends the Message-ID the outbox gave the message
// Version: 0.3.0 - sends a reply's In-Reply-To and References
// Version: 0.2.0 - sends the text alternative and inline pictures of an HTML
//                  draft
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSender.h"

namespace UltraMail {

UltraNetResult MailSender::Send(const Draft& draft, const std::string& serverUrl,
                                const UltraNetMailOptions& options, const std::string& rawMessage) {
    UltraNetMailMessage m;
    m.from = draft.fromName.empty()
        ? draft.fromAddr
        : (draft.fromName + " <" + draft.fromAddr + ">");
    m.to  = draft.to;
    m.cc  = draft.cc;
    m.bcc = draft.bcc;
    m.subject = draft.subject;
    m.body = draft.body;
    m.contentType = draft.bodyIsHtml ? "text/html; charset=utf-8"
                                     : "text/plain; charset=utf-8";
    if (draft.bodyIsHtml) {
        m.alternativeText = draft.textBody;
        for (const auto& p : draft.inlineParts)
            m.inlineParts.push_back({p.contentId, p.filename, p.mediaType, p.data});
    }
    for (const auto& a : draft.attachments)
        m.attachments.emplace_back(a.filename, a.data);
    if (!draft.messageId.empty())  m.headers["Message-ID"] = draft.messageId;
    // A reply stays in its thread at the recipient.
    if (!draft.inReplyTo.empty())  m.headers["In-Reply-To"] = draft.inReplyTo;
    if (!draft.references.empty()) m.headers["References"]  = draft.references;

    // A signed or encrypted message goes out exactly as it was made: the
    // fields above give the envelope (sender, recipients - Bcc too).
    m.rawMessage = rawMessage;

    UltraNetMailOptions opts = options;
    opts.serverUrl = serverUrl;
    return smtp_.SendMail(m, opts);
}

} // namespace UltraMail
