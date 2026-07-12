// include/UltraNet/UltraNetMime.h
// MIME support for UltraNet — a shared, protocol-neutral utility (like
// UltraNetUrl / UltraNetCookies). MIME is used by SMTP/IMAP/POP3 (mail),
// HTTP (multipart/form-data, media types) and file formats (.eml/.mht), so it
// lives in UltraNet core rather than in any one plug-in or app.
//
// Provides: transfer-encoding codecs (base64, quoted-printable), RFC 2047
// encoded-word header decode/encode, full message parsing into a decoded part
// tree (with display-body selection and attachment extraction), and message
// building (flat or multipart/mixed).
//
// Owns structure + encodings only; presentation policy (which body to show,
// remote-image handling, mapping attachments to viewers) stays with the caller.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// ============================================================================
// Transfer-encoding codecs
// ============================================================================

// Base64 (RFC 4648). Encode wraps at 76 columns with CRLF when wrap76Cols.
std::string UltraNet_Base64Encode(const std::vector<uint8_t>& data,
                                  bool wrap76Cols = true);
// Returns false only on grossly malformed input; ignores whitespace/newlines.
bool UltraNet_Base64Decode(const std::string& text, std::vector<uint8_t>& out);

// Quoted-printable (RFC 2045 §6.7). Decode handles "=XX" and soft line breaks.
std::string UltraNet_QuotedPrintableEncode(const std::string& text);
std::string UltraNet_QuotedPrintableDecode(const std::string& text);

// ============================================================================
// RFC 2047 encoded-word header fields ("=?UTF-8?Q?...?=")
// ============================================================================

// Decode any encoded-words in a header value to UTF-8 (UTF-8, US-ASCII and
// ISO-8859-1 / Windows-1252 charsets are transcoded; others pass through).
std::string UltraNet_MimeDecodeHeader(const std::string& raw);

// Encode a UTF-8 header value as a single encoded-word if it contains
// non-ASCII; otherwise returns it unchanged. Uses Q-encoding by default.
std::string UltraNet_MimeEncodeHeader(const std::string& utf8Value,
                                      bool useBase64 = false);

// ============================================================================
// Message parsing
// ============================================================================

// One node in a parsed MIME tree. Leaf parts carry decoded `body` bytes;
// multipart parts carry `children`.
struct UltraNetMimePart {
    std::map<std::string, std::string> headers;  // header name -> value (as received)
    std::string mediaType;         // lowercased, e.g. "text/plain" / "multipart/mixed"
    std::string charset;           // lowercased content-type charset (may be empty)
    std::string transferEncoding;  // lowercased, e.g. "base64" / "quoted-printable"
    std::string disposition;       // "inline" | "attachment" | "" (lowercased)
    std::string filename;          // decoded attachment filename (may be empty)
    std::string contentId;         // Content-ID without angle brackets
    bool isMultipart = false;
    std::vector<uint8_t> body;     // decoded bytes (leaf parts only)
    std::vector<UltraNetMimePart> children;
};

// A parsed message: the part tree plus convenience top-level header fields,
// already RFC 2047-decoded to UTF-8.
struct UltraNetMimeMessage {
    UltraNetMimePart root;
    std::string subject;
    std::string from;
    std::string date;
    std::string messageId;
    std::string inReplyTo;
    std::vector<std::string> to;
    std::vector<std::string> cc;
};

// Parse a raw RFC 5322 message into `out`. Returns false only if the input has
// no header/body separation at all.
bool UltraNet_MimeParse(const std::string& rawMessage, UltraNetMimeMessage& out);

// Choose the best display body: prefers text/html then text/plain (within a
// multipart/alternative), else the first text part. `outText` is UTF-8;
// `outIsHtml` says whether it is HTML. Returns false if no text part exists.
bool UltraNet_MimeGetDisplayBody(const UltraNetMimeMessage& message,
                                 std::string& outText, bool& outIsHtml);

// A decoded attachment surfaced from a parsed message.
struct UltraNetMimeAttachmentView {
    std::string filename;
    std::string mediaType;
    std::string contentId;
    bool isInline = false;
    std::vector<uint8_t> data;
};

// Collect attachment parts (Content-Disposition: attachment, or any non-text
// leaf with a filename). Inline parts are included only when includeInline.
void UltraNet_MimeCollectAttachments(const UltraNetMimeMessage& message,
                                     std::vector<UltraNetMimeAttachmentView>& out,
                                     bool includeInline = false);

// ============================================================================
// Message building
// ============================================================================

struct UltraNetMimeBuildAttachment {
    std::string filename;
    std::string mediaType = "application/octet-stream";
    std::vector<uint8_t> data;
    bool isInline = false;
    std::string contentId;         // for inline parts (Content-ID)
};

struct UltraNetMimeBuildInput {
    std::string from;
    std::vector<std::string> to;
    std::vector<std::string> cc;
    std::vector<std::string> bcc;  // used for delivery, never written to headers
    std::string subject;
    std::map<std::string, std::string> extraHeaders;
    std::string body;                          // UTF-8 body text
    std::string bodyMediaType = "text/plain";  // "text/plain" | "text/html"
    std::string bodyCharset   = "utf-8";
    std::vector<UltraNetMimeBuildAttachment> attachments;
    // Optional; generated when empty (tests may pin these for determinism).
    std::string date;
    std::string messageId;
    std::string boundary;
};

// Build a complete raw RFC 5322 message. A message with no attachments is flat;
// otherwise a multipart/mixed wrapping the body and one part per attachment.
std::string UltraNet_MimeBuild(const UltraNetMimeBuildInput& input);
