// Apps/UltraMail/engine/UltraMailSender.cpp
// Version: 0.1.0 (Phase 2)
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
    for (const auto& a : draft.attachments)
        m.attachments.emplace_back(a.filename, a.data);

    UltraNetMailOptions opts = options;
    opts.serverUrl = serverUrl;
    return smtp_.SendMail(m, opts);
}

} // namespace UltraMail
