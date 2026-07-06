// Tests/UltraNet/test_mime.cpp
// Unit tests for the UltraNet MIME utility: transfer-encoding codecs, RFC 2047
// encoded-word headers, message parsing (multipart, attachments, display body)
// and building + a build->parse round-trip.
#include "test_framework.h"

#include <UltraNet/UltraNetMime.h>

#include <cstdint>
#include <string>
#include <vector>

// ---- transfer encodings ----------------------------------------------------

TEST(mime_base64_roundtrip) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 256; ++i) data.push_back(static_cast<uint8_t>(i));
    std::string enc = UltraNet_Base64Encode(data, true);
    std::vector<uint8_t> dec;
    REQUIRE(UltraNet_Base64Decode(enc, dec));
    REQUIRE(dec == data);

    // Decoding tolerates embedded whitespace / newlines.
    std::vector<uint8_t> d2;
    UltraNet_Base64Decode("SGVs\r\nbG8=", d2);
    REQUIRE_EQ(std::string(d2.begin(), d2.end()), std::string("Hello"));
}

TEST(mime_quoted_printable_decode) {
    REQUIRE_EQ(UltraNet_QuotedPrintableDecode("a=3Db"), std::string("a=b"));
    // "=20" is a space; "=\r\n" is a soft line break (removed).
    REQUIRE_EQ(UltraNet_QuotedPrintableDecode("Hello=20World=\r\nNext"),
               std::string("Hello WorldNext"));
}

TEST(mime_quoted_printable_roundtrip) {
    std::string original = "Grüße — café";   // non-ASCII UTF-8
    std::string enc = UltraNet_QuotedPrintableEncode(original);
    REQUIRE_EQ(UltraNet_QuotedPrintableDecode(enc), original);
}

// ---- RFC 2047 header words --------------------------------------------------

TEST(mime_decode_header_q_and_b) {
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("=?UTF-8?Q?Caf=C3=A9?="), std::string("Café"));
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("=?UTF-8?B?Q2Fmw6k=?="),  std::string("Café"));
    // ISO-8859-1 byte 0xE9 -> é, transcoded to UTF-8.
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("=?ISO-8859-1?Q?Caf=E9?="), std::string("Café"));
}

TEST(mime_decode_header_adjacent_words_collapse_ws) {
    // Whitespace between two adjacent encoded words is dropped (RFC 2047).
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("=?UTF-8?Q?Hello?= =?UTF-8?Q?World?="),
               std::string("HelloWorld"));
    // A plain word mixed in keeps its surrounding spaces.
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("A =?UTF-8?Q?b?= C"), std::string("A b C"));
}

TEST(mime_encode_header_roundtrip) {
    REQUIRE_EQ(UltraNet_MimeEncodeHeader("plain ascii"), std::string("plain ascii"));
    std::string enc = UltraNet_MimeEncodeHeader("Grüße");
    CHECK(enc.rfind("=?UTF-8?", 0) == 0);
    REQUIRE_EQ(UltraNet_MimeDecodeHeader(enc), std::string("Grüße"));
}

// ---- parsing ---------------------------------------------------------------

TEST(mime_parse_flat_message) {
    std::string raw =
        "From: a@b.com\r\n"
        "To: c@d.com\r\n"
        "Subject: Hi there\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "\r\n"
        "Hello body";
    UltraNetMimeMessage msg;
    REQUIRE(UltraNet_MimeParse(raw, msg));
    REQUIRE_EQ(msg.subject, std::string("Hi there"));
    REQUIRE_EQ(msg.to.size(), (size_t)1);

    std::string body; bool html = true;
    REQUIRE(UltraNet_MimeGetDisplayBody(msg, body, html));
    REQUIRE(!html);
    REQUIRE_EQ(body, std::string("Hello body"));
}

TEST(mime_parse_alternative_prefers_html) {
    std::string raw =
        "Subject: alt\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: multipart/alternative; boundary=\"X\"\r\n"
        "\r\n"
        "--X\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "\r\n"
        "the plain part\r\n"
        "--X\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "\r\n"
        "<p>the html part</p>\r\n"
        "--X--\r\n";
    UltraNetMimeMessage msg;
    REQUIRE(UltraNet_MimeParse(raw, msg));
    std::string body; bool html = false;
    REQUIRE(UltraNet_MimeGetDisplayBody(msg, body, html));
    REQUIRE(html);
    CHECK(body.find("the html part") != std::string::npos);
}

TEST(mime_parse_mixed_with_base64_attachment) {
    std::vector<uint8_t> bytes = {0x25, 0x50, 0x44, 0x46, 0x00, 0xFF, 0x10, 0x42};
    std::string b64 = UltraNet_Base64Encode(bytes, true);   // ends with CRLF
    std::string raw =
        "Subject: with attachment\r\n"
        "Content-Type: multipart/mixed; boundary=\"Y\"\r\n"
        "\r\n"
        "--Y\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "see attached\r\n"
        "--Y\r\n"
        "Content-Type: application/pdf; name=\"doc.pdf\"\r\n"
        "Content-Disposition: attachment; filename=\"doc.pdf\"\r\n"
        "Content-Transfer-Encoding: base64\r\n"
        "\r\n"
        + b64 +
        "--Y--\r\n";
    UltraNetMimeMessage msg;
    REQUIRE(UltraNet_MimeParse(raw, msg));

    std::string body; bool html = true;
    REQUIRE(UltraNet_MimeGetDisplayBody(msg, body, html));
    CHECK(body.find("see attached") != std::string::npos);

    std::vector<UltraNetMimeAttachmentView> atts;
    UltraNet_MimeCollectAttachments(msg, atts);
    REQUIRE_EQ(atts.size(), (size_t)1);
    REQUIRE_EQ(atts[0].filename, std::string("doc.pdf"));
    REQUIRE_EQ(atts[0].mediaType, std::string("application/pdf"));
    REQUIRE(atts[0].data == bytes);
}

TEST(mime_parse_quoted_printable_body) {
    std::string raw =
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Transfer-Encoding: quoted-printable\r\n"
        "\r\n"
        "Caf=C3=A9 time";
    UltraNetMimeMessage msg;
    REQUIRE(UltraNet_MimeParse(raw, msg));
    std::string body; bool html = true;
    REQUIRE(UltraNet_MimeGetDisplayBody(msg, body, html));
    REQUIRE_EQ(body, std::string("Café time"));
}

// ---- build + round-trip ----------------------------------------------------

TEST(mime_build_flat_roundtrip) {
    UltraNetMimeBuildInput in;
    in.from = "me@x.com";
    in.to = {"you@y.com"};
    in.subject = "Héllo wörld";          // non-ASCII -> encoded-word
    in.body = "Just a plain body.";
    in.boundary = "B"; in.date = "Tue, 01 Jan 2026 00:00:00 +0000"; in.messageId = "<id@x>";

    std::string raw = UltraNet_MimeBuild(in);
    UltraNetMimeMessage msg;
    REQUIRE(UltraNet_MimeParse(raw, msg));
    REQUIRE_EQ(msg.subject, std::string("Héllo wörld"));
    std::string body; bool html = true;
    REQUIRE(UltraNet_MimeGetDisplayBody(msg, body, html));
    REQUIRE(!html);
    CHECK(body.find("Just a plain body.") != std::string::npos);
}

TEST(mime_build_with_attachment_roundtrip) {
    UltraNetMimeBuildInput in;
    in.from = "me@x.com";
    in.to = {"you@y.com"};
    in.subject = "report";
    in.body = "body with file";
    in.date = "Tue, 01 Jan 2026 00:00:00 +0000"; in.messageId = "<id@x>";
    UltraNetMimeBuildAttachment a;
    a.filename = "data.bin";
    a.mediaType = "application/octet-stream";
    a.data = {1, 2, 3, 4, 250, 0, 99};
    in.attachments.push_back(a);

    std::string raw = UltraNet_MimeBuild(in);
    CHECK(raw.find("multipart/mixed") != std::string::npos);

    UltraNetMimeMessage msg;
    REQUIRE(UltraNet_MimeParse(raw, msg));
    std::vector<UltraNetMimeAttachmentView> atts;
    UltraNet_MimeCollectAttachments(msg, atts);
    REQUIRE_EQ(atts.size(), (size_t)1);
    REQUIRE_EQ(atts[0].filename, std::string("data.bin"));
    REQUIRE(atts[0].data == a.data);
}
