// Apps/UltraMail/ui/UltraMailMessageImport.cpp
// A message file as the RFC 5322 message Import uploads - see
// UltraMailMessageImport.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMailMessageImport.h"

#include "UltraCanvasOutlookMessage.h"
#include <UltraNet/UltraNetMime.h>

#include <algorithm>
#include <cctype>

namespace UltraMail {

namespace {

std::string LowerExtension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    const size_t slash = name.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return std::string();
    std::string ext = name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

// The message an .emlx holds: a line with its length in bytes, the message,
// then Mail's own property list.
std::string MessageOfEmlx(const std::string& raw) {
    const size_t lineEnd = raw.find('\n');
    if (lineEnd == std::string::npos) return raw;
    size_t length = 0;
    bool digits = false;
    for (size_t i = 0; i < lineEnd; ++i) {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        if (std::isdigit(c)) {
            if (length <= raw.size()) length = length * 10 + static_cast<size_t>(c - '0');
            digits = true;
        } else if (!std::isspace(c)) {
            return raw;   // no length line: not an .emlx after all
        }
    }
    return digits ? raw.substr(lineEnd + 1, length) : raw;
}

// Whether text reads as a message: a header block (at least one "Name:"
// line naming a field every message has) before its blank line.
bool LooksLikeMessage(const std::string& raw) {
    UltraNetMimeMessage message;
    if (!UltraNet_MimeParse(raw, message)) return false;
    return !message.from.empty() || !message.subject.empty() || !message.to.empty() ||
           !message.date.empty() || !message.messageId.empty();
}

std::string OutlookToRfc822(const UltraCanvas::UCOutlookMessage& msg) {
    UltraNetMimeBuildInput in;
    in.from = msg.from;
    in.to = msg.to;
    in.cc = msg.cc;
    in.subject = msg.subject;
    in.date = msg.date;              // generated when the file has none (a draft)
    in.messageId = msg.messageId;    // generated when it has none
    if (!msg.inReplyTo.empty()) in.extraHeaders["In-Reply-To"] = msg.inReplyTo;
    if (!msg.bodyHtml.empty()) {
        in.body = msg.bodyHtml;
        in.bodyMediaType = "text/html";
        in.alternativeText = msg.bodyText;
    } else {
        in.body = msg.bodyText;
        in.bodyMediaType = "text/plain";
    }
    for (const auto& a : msg.attachments) {
        if (a.embeddedMessage || a.data.empty()) continue;
        UltraNetMimeBuildAttachment part;
        part.filename = a.filename.empty() ? std::string("attachment") : a.filename;
        if (!a.mediaType.empty()) part.mediaType = a.mediaType;
        part.data = a.data;
        // A picture the HTML body shows travels with it (multipart/related).
        part.isInline = a.hidden && !a.contentId.empty() && !msg.bodyHtml.empty();
        part.contentId = a.contentId;
        in.attachments.push_back(std::move(part));
    }
    return UltraNet_MimeBuild(in);
}

} // namespace

const std::vector<std::string>& ImportableMessageExtensions() {
    static const std::vector<std::string> extensions = { "eml", "emlx", "msg" };
    return extensions;
}

bool IsImportableMessageFile(const std::string& fileName) {
    const std::string ext = LowerExtension(fileName);
    const auto& known = ImportableMessageExtensions();
    return std::find(known.begin(), known.end(), ext) != known.end();
}

bool MessageFileToRfc822(const std::string& fileName, const std::vector<uint8_t>& bytes,
                         std::string& raw, std::string& error) {
    raw.clear();
    error.clear();
    const std::string ext = LowerExtension(fileName);
    if (ext == "msg") {
        UltraCanvas::UCOutlookMessage msg;
        std::string why;
        if (!msg.LoadFromMemory(bytes, why)) {
            error = "It is not an Outlook message (" + why + ").";
            return false;
        }
        raw = OutlookToRfc822(msg);
        return true;
    }
    if (ext != "eml" && ext != "emlx") {
        error = "UltraMail imports .eml, .emlx and .msg files.";
        return false;
    }
    std::string text(bytes.begin(), bytes.end());
    if (ext == "emlx") text = MessageOfEmlx(text);
    if (!LooksLikeMessage(text)) {
        error = "It holds no e-mail message.";
        return false;
    }
    raw = std::move(text);
    return true;
}

} // namespace UltraMail
