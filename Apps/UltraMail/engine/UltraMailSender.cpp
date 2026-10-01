// Apps/UltraMail/engine/UltraMailSender.cpp
// Version: 0.3.0 - sends a reply's In-Reply-To and References
// Version: 0.2.0 - sends the text alternative and inline pictures of an HTML
//                  draft
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSender.h"

namespace UltraMail {

UltraNetResult MailSender::Send(const Draft& draft, const std::string& serverUrl,
                                const UltraNetMailOptions& options) {
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
    // A reply stays in its thread at the recipient.
    if (!draft.inReplyTo.empty())  m.headers["In-Reply-To"] = draft.inReplyTo;
    if (!draft.references.empty()) m.headers["References"]  = draft.references;

    UltraNetMailOptions opts = options;
    opts.serverUrl = serverUrl;
    return smtp_.SendMail(m, opts);
}

} // namespace UltraMail
