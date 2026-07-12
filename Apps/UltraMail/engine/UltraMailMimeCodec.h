// Apps/UltraMail/engine/UltraMailMimeCodec.h
// Thin UltraMail-side wrapper over the UltraNet MIME utility: turns a raw
// RFC 5322 message (as fetched by IMAP) into a display body plus a list of
// attachments the UI can render. The heavy lifting (multipart parsing,
// transfer-decoding, header decoding) lives in UltraNet_MimeParse; this adds
// only the app-facing shapes and the presentation-neutral attachment view.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraMail {

// A decoded attachment ready to hand to a viewer or save to disk.
struct Attachment {
    std::string filename;
    std::string mediaType;              // e.g. "application/pdf", "image/png"
    std::string contentId;             // for inline parts
    bool isInline = false;
    std::vector<uint8_t> data;

    std::size_t Size() const { return data.size(); }
};

// A parsed message: the chosen display body plus attachments.
struct ParsedMessage {
    std::string subject;
    std::string from;
    std::string body;                   // UTF-8 (HTML or plain per bodyIsHtml)
    bool bodyIsHtml = false;
    std::vector<Attachment> attachments;
};

class MimeCodec {
public:
    // Parse a raw RFC 5322 message. Never throws; a message with no text part
    // yields an empty body, and one with no attachments yields an empty list.
    static ParsedMessage Parse(const std::string& rawMessage);
};

} // namespace UltraMail
