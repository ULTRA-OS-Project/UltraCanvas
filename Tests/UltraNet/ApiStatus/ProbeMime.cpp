// Tests/UltraNet/ApiStatus/ProbeMime.cpp
// Probes for UltraNet/UltraNetMime.h — transfer-encoding codecs, RFC 2047
// header words, message parsing and building. All pure functions: the whole
// area is verifiable offline.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ApiStatus.h"

#include <UltraNet/UltraNetMime.h>

#include <string>
#include <vector>

using namespace ultranet_apistatus;

namespace {

constexpr const char* kArea = "MIME";

std::vector<uint8_t> Bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

std::string Text(const std::vector<uint8_t>& b) {
    return std::string(b.begin(), b.end());
}

// A small multipart/mixed message used by the parsing probes: an alternative
// body (plain + html) plus one base64 attachment.
const char* const kSampleMessage =
    "From: Alice <alice@example.com>\r\n"
    "To: Bob <bob@example.com>, Carol <carol@example.com>\r\n"
    "Cc: Dave <dave@example.com>\r\n"
    "Subject: =?UTF-8?Q?Gr=C3=BC=C3=9Fe?=\r\n"
    "Message-ID: <probe-1@example.com>\r\n"
    "Date: Mon, 1 Jan 2035 09:00:00 +0000\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: multipart/mixed; boundary=\"OUTER\"\r\n"
    "\r\n"
    "--OUTER\r\n"
    "Content-Type: multipart/alternative; boundary=\"INNER\"\r\n"
    "\r\n"
    "--INNER\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Content-Transfer-Encoding: quoted-printable\r\n"
    "\r\n"
    "plain gr=C3=BC=C3=9Fe\r\n"
    "--INNER\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "\r\n"
    "<p>html body</p>\r\n"
    "--INNER--\r\n"
    "--OUTER\r\n"
    "Content-Type: application/octet-stream\r\n"
    "Content-Transfer-Encoding: base64\r\n"
    "Content-Disposition: attachment; filename=\"notes.bin\"\r\n"
    "\r\n"
    "SGVsbG8gYXR0YWNobWVudA==\r\n"
    "--OUTER--\r\n";

} // namespace

ULTRANET_PROBE(kArea, UltraNet_Base64Encode) {
    PROBE_EXPECT(UltraNet_Base64Encode(Bytes("Man"), false) == "TWFu");
    PROBE_EXPECT(UltraNet_Base64Encode(Bytes("Ma"), false) == "TWE=");
    PROBE_EXPECT(UltraNet_Base64Encode(Bytes("M"), false) == "TQ==");
    PROBE_EXPECT(UltraNet_Base64Encode({}, false).empty());

    // Wrapping at 76 columns is what the MIME transfer encoding requires.
    const std::string wrapped = UltraNet_Base64Encode(std::vector<uint8_t>(200, 'x'), true);
    PROBE_EXPECT(wrapped.find("\r\n") != std::string::npos);
    const std::string unwrapped = UltraNet_Base64Encode(std::vector<uint8_t>(200, 'x'), false);
    PROBE_EXPECT(unwrapped.find("\r\n") == std::string::npos);
    PROBE_EXPECT(unwrapped.size() == 268);   // ceil(200/3)*4
    return Working("RFC 4648 output including both padding forms; 76-column "
                   "wrapping toggles as documented");
}

ULTRANET_PROBE(kArea, UltraNet_Base64Decode) {
    std::vector<uint8_t> out;
    PROBE_EXPECT(UltraNet_Base64Decode("TWFu", out));
    PROBE_EXPECT(Text(out) == "Man");

    PROBE_EXPECT(UltraNet_Base64Decode("TWE=", out) && Text(out) == "Ma");
    PROBE_EXPECT(UltraNet_Base64Decode("TQ==", out) && Text(out) == "M");

    // Embedded whitespace/newlines must be ignored (that is how it arrives on
    // the wire).
    PROBE_EXPECT(UltraNet_Base64Decode("SGVs\r\nbG8g\r\nd29ybGQ=", out));
    PROBE_EXPECT(Text(out) == "Hello world");

    // Round trip over every byte value.
    std::vector<uint8_t> all(256);
    for (int i = 0; i < 256; ++i) all[static_cast<std::size_t>(i)] = static_cast<uint8_t>(i);
    std::vector<uint8_t> back;
    PROBE_EXPECT(UltraNet_Base64Decode(UltraNet_Base64Encode(all, true), back));
    PROBE_EXPECT(back == all);
    return Working("padding handled, embedded whitespace ignored, and a "
                   "full 0..255 byte round trip is lossless");
}

ULTRANET_PROBE(kArea, UltraNet_QuotedPrintableEncode) {
    const std::string encoded = UltraNet_QuotedPrintableEncode("grüße");
    PROBE_EXPECT_MSG(encoded.find('=') != std::string::npos,
                     "non-ASCII input was not escaped: \"" + encoded + "\"");
    PROBE_EXPECT(UltraNet_QuotedPrintableEncode("plain ascii") == "plain ascii");
    // Whatever the exact escaping, decoding must give the input back.
    PROBE_EXPECT(UltraNet_QuotedPrintableDecode(encoded) == "grüße");
    return Working("non-ASCII escaped as =XX, plain ASCII passed through, and "
                   "the result decodes back to the original");
}

ULTRANET_PROBE(kArea, UltraNet_QuotedPrintableDecode) {
    PROBE_EXPECT(UltraNet_QuotedPrintableDecode("gr=C3=BC=C3=9Fe") == "grüße");
    // Soft line break: "=\r\n" disappears entirely.
    PROBE_EXPECT(UltraNet_QuotedPrintableDecode("abc=\r\ndef") == "abcdef");
    PROBE_EXPECT(UltraNet_QuotedPrintableDecode("plain") == "plain");
    PROBE_EXPECT(UltraNet_QuotedPrintableDecode("").empty());
    return Working("=XX escapes decoded and soft line breaks removed");
}

ULTRANET_PROBE(kArea, UltraNet_MimeDecodeHeader) {
    PROBE_EXPECT(UltraNet_MimeDecodeHeader("=?UTF-8?Q?Gr=C3=BC=C3=9Fe?=") == "grüße" ||
                 UltraNet_MimeDecodeHeader("=?UTF-8?Q?Gr=C3=BC=C3=9Fe?=") == "Grüße");
    PROBE_EXPECT(UltraNet_MimeDecodeHeader("=?UTF-8?B?SGVsbG8=?=") == "Hello");
    PROBE_EXPECT(UltraNet_MimeDecodeHeader("plain subject") == "plain subject");

    // A value made of several encoded words must come back concatenated.
    const std::string split =
        UltraNet_MimeDecodeHeader("=?UTF-8?B?SGVsbG8=?= =?UTF-8?B?d29ybGQ=?=");
    PROBE_EXPECT_MSG(split.find("Hello") != std::string::npos &&
                     split.find("world") != std::string::npos,
                     "multi-word decode produced \"" + split + "\"");
    return Working("RFC 2047 Q- and B-encoded words decoded to UTF-8; plain "
                   "values untouched; multiple words joined");
}

ULTRANET_PROBE(kArea, UltraNet_MimeEncodeHeader) {
    PROBE_EXPECT(UltraNet_MimeEncodeHeader("plain ascii") == "plain ascii");

    const std::string q = UltraNet_MimeEncodeHeader("grüße", false);
    PROBE_EXPECT(q.rfind("=?UTF-8?", 0) == 0);
    PROBE_EXPECT(UltraNet_MimeDecodeHeader(q) == "grüße");

    const std::string b = UltraNet_MimeEncodeHeader("grüße", true);
    PROBE_EXPECT(b.rfind("=?UTF-8?B?", 0) == 0);
    PROBE_EXPECT(UltraNet_MimeDecodeHeader(b) == "grüße");
    return Working("ASCII passes through; non-ASCII becomes a Q- or B-encoded "
                   "word that survives a decode round trip");
}

ULTRANET_PROBE(kArea, UltraNet_MimeParse) {
    UltraNetMimeMessage msg;
    PROBE_EXPECT(UltraNet_MimeParse(kSampleMessage, msg));

    PROBE_EXPECT(msg.from.find("alice@example.com") != std::string::npos);
    PROBE_EXPECT(msg.to.size() == 2);
    PROBE_EXPECT(msg.cc.size() == 1);
    PROBE_EXPECT(msg.messageId.find("probe-1@example.com") != std::string::npos);
    PROBE_EXPECT_MSG(msg.subject.find("üße") != std::string::npos,
                     "subject decoded as \"" + msg.subject + "\"");

    PROBE_EXPECT(msg.root.isMultipart);
    PROBE_EXPECT(msg.root.mediaType == "multipart/mixed");
    PROBE_EXPECT(msg.root.children.size() == 2);

    const UltraNetMimePart& alternative = msg.root.children[0];
    PROBE_EXPECT(alternative.isMultipart);
    PROBE_EXPECT(alternative.mediaType == "multipart/alternative");
    PROBE_EXPECT(alternative.children.size() == 2);

    // Leaf bodies must arrive decoded, not still quoted-printable/base64.
    const UltraNetMimePart& plain = alternative.children[0];
    PROBE_EXPECT(plain.mediaType == "text/plain");
    PROBE_EXPECT_MSG(Text(plain.body) == "plain grüße",
                     "text/plain body decoded as \"" + Text(plain.body) + "\"");

    const UltraNetMimePart& attachment = msg.root.children[1];
    PROBE_EXPECT(attachment.filename == "notes.bin");
    PROBE_EXPECT(attachment.disposition == "attachment");
    PROBE_EXPECT_MSG(Text(attachment.body) == "Hello attachment",
                     "attachment decoded as \"" + Text(attachment.body) + "\"");

    UltraNetMimeMessage empty;
    PROBE_EXPECT(!UltraNet_MimeParse("", empty));
    return Working("nested multipart tree built; headers RFC 2047-decoded; "
                   "quoted-printable and base64 part bodies decoded in place");
}

ULTRANET_PROBE(kArea, UltraNet_MimeGetDisplayBody) {
    UltraNetMimeMessage msg;
    PROBE_EXPECT(UltraNet_MimeParse(kSampleMessage, msg));

    std::string text;
    bool isHtml = false;
    PROBE_EXPECT(UltraNet_MimeGetDisplayBody(msg, text, isHtml));
    PROBE_EXPECT_MSG(isHtml, "HTML alternative was not preferred");
    PROBE_EXPECT(text.find("<p>html body</p>") != std::string::npos);

    // A flat text/plain message must come back as non-HTML.
    UltraNetMimeMessage flat;
    PROBE_EXPECT(UltraNet_MimeParse(
        "Subject: flat\r\nContent-Type: text/plain\r\n\r\njust text\r\n", flat));
    std::string flatText;
    bool flatIsHtml = true;
    PROBE_EXPECT(UltraNet_MimeGetDisplayBody(flat, flatText, flatIsHtml));
    PROBE_EXPECT(!flatIsHtml);
    PROBE_EXPECT(flatText.find("just text") != std::string::npos);
    return Working("HTML preferred inside multipart/alternative; plain "
                   "messages reported as non-HTML");
}

ULTRANET_PROBE(kArea, UltraNet_MimeCollectAttachments) {
    UltraNetMimeMessage msg;
    PROBE_EXPECT(UltraNet_MimeParse(kSampleMessage, msg));

    std::vector<UltraNetMimeAttachmentView> attachments;
    UltraNet_MimeCollectAttachments(msg, attachments, false);
    PROBE_EXPECT_MSG(attachments.size() == 1,
                     "collected " + std::to_string(attachments.size()) +
                     " attachments, expected 1");
    PROBE_EXPECT(attachments[0].filename == "notes.bin");
    PROBE_EXPECT(attachments[0].mediaType == "application/octet-stream");
    PROBE_EXPECT(Text(attachments[0].data) == "Hello attachment");
    PROBE_EXPECT(!attachments[0].isInline);

    // The text bodies must not be mistaken for attachments.
    std::vector<UltraNetMimeAttachmentView> withInline;
    UltraNet_MimeCollectAttachments(msg, withInline, true);
    PROBE_EXPECT(withInline.size() >= 1);
    return Working("attachment part extracted with decoded bytes, filename and "
                   "media type; text bodies excluded");
}

ULTRANET_PROBE(kArea, UltraNet_MimeBuild) {
    UltraNetMimeBuildInput input;
    input.from    = "alice@example.com";
    input.to      = {"bob@example.com"};
    input.cc      = {"carol@example.com"};
    input.bcc     = {"eve@example.com"};   // must never appear in the output
    input.subject = "grüße";
    input.body    = "hello body";
    input.boundary  = "PROBEBOUNDARY";
    input.messageId = "<probe-build@example.com>";
    input.date      = "Mon, 1 Jan 2035 09:00:00 +0000";
    input.extraHeaders["X-Probe"] = "build";

    UltraNetMimeBuildAttachment att;
    att.filename  = "data.bin";
    att.mediaType = "application/octet-stream";
    att.data      = Bytes("attached bytes");
    input.attachments.push_back(att);

    const std::string raw = UltraNet_MimeBuild(input);
    PROBE_EXPECT(!raw.empty());
    PROBE_EXPECT_MSG(raw.find("eve@example.com") == std::string::npos,
                     "Bcc leaked into the built headers");
    PROBE_EXPECT(raw.find("X-Probe: build") != std::string::npos);
    PROBE_EXPECT(raw.find("PROBEBOUNDARY") != std::string::npos);

    // The strongest check available: the builder's output must parse back.
    UltraNetMimeMessage back;
    PROBE_EXPECT_MSG(UltraNet_MimeParse(raw, back),
                     "UltraNet_MimeBuild produced a message UltraNet_MimeParse "
                     "cannot read");
    PROBE_EXPECT(back.subject == "grüße");
    PROBE_EXPECT(back.to.size() == 1 && back.to[0].find("bob@example.com") != std::string::npos);
    PROBE_EXPECT(back.root.isMultipart);

    std::vector<UltraNetMimeAttachmentView> attachments;
    UltraNet_MimeCollectAttachments(back, attachments, false);
    PROBE_EXPECT(attachments.size() == 1);
    PROBE_EXPECT(attachments[0].filename == "data.bin");
    PROBE_EXPECT(Text(attachments[0].data) == "attached bytes");

    std::string body;
    bool isHtml = false;
    PROBE_EXPECT(UltraNet_MimeGetDisplayBody(back, body, isHtml));
    PROBE_EXPECT(body.find("hello body") != std::string::npos);

    // No attachments => flat message.
    UltraNetMimeBuildInput flatInput = input;
    flatInput.attachments.clear();
    const std::string flat = UltraNet_MimeBuild(flatInput);
    PROBE_EXPECT(flat.find("multipart/mixed") == std::string::npos);
    return Working("multipart/mixed built and re-parsed losslessly (subject, "
                   "recipients, body, attachment); Bcc withheld; attachment-free "
                   "input produces a flat message");
}
