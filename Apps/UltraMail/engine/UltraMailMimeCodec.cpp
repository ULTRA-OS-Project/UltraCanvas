// Apps/UltraMail/engine/UltraMailMimeCodec.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailMimeCodec.h"

#include <UltraNet/UltraNetMime.h>

namespace UltraMail {

ParsedMessage MimeCodec::Parse(const std::string& rawMessage) {
    ParsedMessage out;

    UltraNetMimeMessage msg;
    if (!UltraNet_MimeParse(rawMessage, msg))
        return out;

    out.subject = msg.subject;
    out.from    = msg.from;

    std::string body;
    bool isHtml = false;
    if (UltraNet_MimeGetDisplayBody(msg, body, isHtml)) {
        out.body = std::move(body);
        out.bodyIsHtml = isHtml;
    }

    std::vector<UltraNetMimeAttachmentView> atts;
    UltraNet_MimeCollectAttachments(msg, atts, /*includeInline=*/false);
    out.attachments.reserve(atts.size());
    for (auto& a : atts) {
        Attachment att;
        att.filename  = std::move(a.filename);
        att.mediaType = std::move(a.mediaType);
        att.contentId = std::move(a.contentId);
        att.isInline  = a.isInline;
        att.data      = std::move(a.data);
        out.attachments.push_back(std::move(att));
    }
    return out;
}

} // namespace UltraMail
