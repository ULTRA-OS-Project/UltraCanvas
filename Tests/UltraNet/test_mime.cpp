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

// ---- address headers -------------------------------------------------------

TEST(mime_encode_address_display_name_only) {
    // ASCII is left exactly as it was.
    REQUIRE_EQ(UltraNet_MimeEncodeAddress("Erika <erika@example.com>"),
               std::string("Erika <erika@example.com>"));

    // The display name becomes an encoded-word; the angle-addr must not,
    // or the message is undeliverable.
    std::string enc = UltraNet_MimeEncodeAddress("Erika Fröhling <erika@example.com>");
    CHECK(enc.find("=?UTF-8?") == 0);
    CHECK(enc.find("<erika@example.com>") != std::string::npos);
    CHECK(enc.find("Fröhling") == std::string::npos);
    REQUIRE_EQ(UltraNet_MimeDecodeHeader(enc),
               std::string("Erika Fröhling <erika@example.com>"));

    // A quoted display name loses the quotes it cannot keep inside an
    // encoded-word.
    std::string quoted = UltraNet_MimeEncodeAddress("\"Fröhling\" <e@x.com>");
    REQUIRE_EQ(UltraNet_MimeDecodeHeader(quoted), std::string("Fröhling <e@x.com>"));

    // A bare non-ASCII address has no display name to encode: left alone for
    // the server to accept or reject.
    REQUIRE_EQ(UltraNet_MimeEncodeAddress("frö@example.com"),
               std::string("frö@example.com"));

    // A comma inside the name must not survive as a literal - it would split
    // the address list it sits in.
    std::string comma = UltraNet_MimeEncodeAddress("Fröhling, Erika <e@x.com>");
    CHECK(comma.find(',') == std::string::npos);
    REQUIRE_EQ(UltraNet_MimeDecodeHeader(comma),
               std::string("Fröhling, Erika <e@x.com>"));
}

TEST(mime_build_encodes_address_headers) {
    UltraNetMimeBuildInput in;
    in.from = "Erika Fröhling <erika@example.com>";
    in.to = {"Jörg <joerg@y.com>", "plain@y.com"};
    in.cc = {"Süd <sued@y.com>"};
    in.subject = "hallo";
    in.body = "body";
    in.date = "Tue, 01 Jan 2026 00:00:00 +0000"; in.messageId = "<id@x>";

    std::string raw = UltraNet_MimeBuild(in);
    // No raw 8-bit byte may appear in the header block - that is the whole
    // point of an encoded-word.
    const std::size_t headerEnd = raw.find("\r\n\r\n");
    REQUIRE(headerEnd != std::string::npos);
    for (unsigned char c : raw.substr(0, headerEnd)) CHECK(c < 0x80);

    UltraNetMimeMessage msg;
    REQUIRE(UltraNet_MimeParse(raw, msg));
    REQUIRE_EQ(msg.from, std::string("Erika Fröhling <erika@example.com>"));
    REQUIRE_EQ(msg.to.size(), (size_t)2);
    REQUIRE_EQ(msg.to[0], std::string("Jörg <joerg@y.com>"));
    REQUIRE_EQ(msg.to[1], std::string("plain@y.com"));
    REQUIRE_EQ(msg.cc.size(), (size_t)1);
    REQUIRE_EQ(msg.cc[0], std::string("Süd <sued@y.com>"));
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

// ---- charsets beyond UTF-8 / Latin-1 (iconv) --------------------------------
#if defined(ULTRANET_HAS_ICONV)

namespace {
// "株式会社テレシア" - a real sender name that arrived as ISO-2022-JP.
const std::string kKabushikiUtf8 =
    "\xE6\xA0\xAA\xE5\xBC\x8F\xE4\xBC\x9A\xE7\xA4\xBE"
    "\xE3\x83\x86\xE3\x83\xAC\xE3\x82\xB7\xE3\x82\xA2";
}

TEST(mime_header_iso2022jp_encoded_word) {
    const std::string raw = "=?ISO-2022-JP?B?GyRCM3Q8MDJxPFIlRiVsJTclIhsoQg==?= <info@x.example>";
    REQUIRE_EQ(UltraNet_MimeDecodeHeader(raw), kKabushikiUtf8 + " <info@x.example>");
    // Lower-case charset label, as many mailers write it.
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("=?iso-2022-jp?B?GyRCM3Q8MDJxPFIlRiVsJTclIhsoQg==?="),
               kKabushikiUtf8);
}

TEST(mime_header_raw_iso2022jp_escape_sequences) {
    // No encoded-word at all: the JIS bytes straight in the header.
    const std::string raw = "\x1b$B3t<02q<R%F%l%7%\"\x1b(B";
    REQUIRE_EQ(UltraNet_MimeDecodeHeader(raw), kKabushikiUtf8);
    // Mixed with ASCII and with an ordinary encoded-word.
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("Re: \x1b$B$*CN$i$;\x1b(B =?UTF-8?Q?caf=C3=A9?="),
               std::string("Re: \xE3\x81\x8A\xE7\x9F\xA5\xE3\x82\x89\xE3\x81\x9B caf\xC3\xA9"));
    // Plain ASCII stays untouched.
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("Hello (B) $B"), std::string("Hello (B) $B"));
}

TEST(mime_body_iso2022jp_part) {
    const std::string raw =
        "From: a@x.example\r\n"
        "Subject: =?ISO-2022-JP?B?GyRCM3Q8MDJxPFIlRiVsJTclIhsoQg==?=\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: text/plain; charset=\"ISO-2022-JP\"\r\n"
        "Content-Transfer-Encoding: 7bit\r\n"
        "\r\n"
        "\x1b$B3t<02q<R%F%l%7%\"\x1b(B\r\n";
    UltraNetMimeMessage msg;
    REQUIRE(UltraNet_MimeParse(raw, msg));
    REQUIRE_EQ(msg.subject, kKabushikiUtf8);
    std::string body; bool html = true;
    REQUIRE(UltraNet_MimeGetDisplayBody(msg, body, html));
    REQUIRE(!html);
    REQUIRE(body.find(kKabushikiUtf8) != std::string::npos);
}

TEST(mime_header_other_charsets_via_iconv) {
    // Shift_JIS "日本" (Q-encoded bytes 93 FA 96 7B).
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("=?Shift_JIS?Q?=93=FA=96=7B?="),
               std::string("\xE6\x97\xA5\xE6\x9C\xAC"));
    // windows-1252 0x80 is the euro sign, not a C1 control.
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("=?windows-1252?Q?=80?="), std::string("\xE2\x82\xAC"));
    // KOI8-R "Привет".
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("=?koi8-r?Q?=F0=D2=C9=D7=C5=D4?="),
               std::string("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"));
    // An unknown charset keeps the bytes rather than dropping the text.
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("=?x-unknown-cs?Q?abc?="), std::string("abc"));
}

TEST(mime_header_charset_label_aliases) {
    // Hebrew "logical" label (what Outlook and Thunderbird send): shalom.
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("=?iso-8859-8-i?Q?=F9=EC=E5=ED?="),
               std::string("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"));
    // Classic Mac OS Roman: 0x8A is a-umlaut.
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("=?x-mac-roman?Q?=8A?="), std::string("\xC3\xA4"));
    // UTF-7 under its old IANA name.
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("=?unicode-1-1-utf-7?Q?Hi_+AKM-1?="),
               std::string("Hi \xC2\xA3" "1"));
}

TEST(mime_unknown_charset_never_yields_invalid_utf8) {
    // "unknown-8bit" with Latin-1 bytes: read as windows-1252, not passed on raw.
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("=?unknown-8bit?Q?caf=E9?="), std::string("caf\xC3\xA9"));
    // ... but text that already is UTF-8 stays as it is.
    REQUIRE_EQ(UltraNet_MimeDecodeHeader("=?x-user-defined?Q?caf=C3=A9?="),
               std::string("caf\xC3\xA9"));
}

#endif // ULTRANET_HAS_ICONV
