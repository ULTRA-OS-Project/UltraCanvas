// include/UltraCanvasOutlookMessage.h
// Reads an Outlook message file (.msg, [MS-OXMSG]) - what Outlook writes for
// "Save as" and for a message dragged out of it - into the parts a reader
// shows: subject, sender, recipients, date, the body and the attachments.
//
// A .msg is a compound file (UltraCanvasCompoundFile.h): each property of
// the message is a stream named after its tag (__substg1.0_0037001F is the
// subject in UTF-16), fixed-size ones (the date, the code page) sit in the
// __properties_version1.0 stream, and every recipient and attachment is a
// storage of its own with the same layout.
//
// The body: the HTML one (PR_HTML) when the file has it; else the HTML that
// Outlook wrapped in its compressed RTF body (PR_RTF_COMPRESSED, [MS-OXRTFCP],
// holding HTML per [MS-OXRTFEX] - "\fromhtml1"), which is how most messages
// that arrived as HTML are stored; else the plain text (PR_BODY). An RTF body
// that never was HTML (a message written in Outlook's rich text) is shown
// through its plain text, which Outlook always stores beside it.
//
// 8-bit strings and RTF text are read in the message's code page through
// DecodeWindowsCodePage (UltraCanvasTextUtils.h): 1250, 1251, 1252 and
// UTF-8; other code pages as Latin-1. Text stored as UTF-16 - what Outlook
// writes since 2003 - is read whatever the code page.
//
// Headless and read-only; no UI. The media viewer shows a .msg through it.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

struct UCOutlookAttachment {
    std::string filename;          // the long name, else the 8.3 one, else the display name
    std::string mediaType;         // PR_ATTACH_MIME_TAG ("image/png"); may be empty
    std::string contentId;         // PR_ATTACH_CONTENT_ID: what an HTML body's cid: names
    bool hidden = false;           // PR_ATTACHMENT_HIDDEN: a picture the body shows, not a file sent
    // An attached message (Outlook's "attach item"): a storage, not bytes -
    // listed by name, `data` empty.
    bool embeddedMessage = false;
    std::vector<uint8_t> data;
};

struct UCOutlookMessage {
    std::string subject;
    std::string from;              // "Name <address>", or whichever of the two the file has
    std::vector<std::string> to;   // the same form, one per recipient
    std::vector<std::string> cc;
    // RFC 5322 form, UTC: "Thu, 29 Oct 2026 09:00:00 +0000" - when it was
    // delivered, else sent; a draft's when it was last changed. "" if none.
    std::string date;
    std::string bodyText;          // UTF-8
    std::string bodyHtml;          // UTF-8; empty when the message has no HTML body
    std::vector<UCOutlookAttachment> attachments;

    // Reads `path` (UTF-8). False, with `error` saying why, when it is not
    // a compound file or holds no message.
    bool Load(const std::string& path, std::string& error);
    bool LoadFromMemory(std::vector<uint8_t> data, std::string& error);
};

// [MS-OXRTFCP]: a compressed RTF body (PR_RTF_COMPRESSED) - "LZFu", or
// "MELA" for one stored uncompressed - back to its RTF. False when the
// header is not one of those. The CRC is not checked: a damaged stream
// gives what it holds, up to its stated size.
bool UCDecompressRtf(const std::vector<uint8_t>& compressed, std::string& rtf);

// [MS-OXRTFEX]: the HTML an RTF body encapsulates ("\fromhtml1"), as
// UTF-8: the \htmltag groups and the text outside \htmlrtf, the RTF's own
// formatting dropped. False when the RTF does not encapsulate HTML.
bool UCHtmlFromEncapsulatedRtf(const std::string& rtf, std::string& html);

} // namespace UltraCanvas
