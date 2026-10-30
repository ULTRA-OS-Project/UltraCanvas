// Tests/OutlookMessageTest.cpp
// The Outlook .msg reader (UltraCanvasOutlookMessage.h) against messages
// built here with UCCompoundFileWriter, laid out as Outlook writes them:
// subject, sender, recipients (To / Cc / Bcc), the delivery date, the plain
// body, an HTML body kept only inside the compressed RTF ([MS-OXRTFCP] LZFu,
// with a dictionary reference) as [MS-OXRTFEX] encapsulated HTML - 8-bit
// text, \u escapes, \htmlrtf parts left out - a PR_HTML body when there is
// one, attachments (by value, hidden with a Content-ID, an attached
// message), and 8-bit string properties in the message's code page. Also
// the compound-file reader's paths below the root, and DecodeWindowsCodePage.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasOutlookMessage.h"
#include "UltraCanvasCompoundFile.h"
#include "UltraCanvasTextUtils.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& what) {
    std::printf("  %s %s\n", cond ? "ok:  " : "FAIL:", what.c_str());
    if (!cond) ++g_failures;
}

std::vector<uint8_t> Utf16(const std::string& ascii) {
    std::vector<uint8_t> out;
    for (unsigned char c : ascii) { out.push_back(c); out.push_back(0); }
    out.push_back(0); out.push_back(0);
    return out;
}

// "Quarterly résumé" in UTF-16LE.
std::vector<uint8_t> Utf16Resume() {
    std::vector<uint8_t> out = Utf16("Quarterly r");
    out.resize(out.size() - 2);
    const uint16_t rest[] = { 0xE9, 's', 'u', 'm', 0xE9, 0 };
    for (uint16_t u : rest) { out.push_back(u & 0xFF); out.push_back(u >> 8); }
    return out;
}

std::vector<uint8_t> Bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

void Put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// A properties stream: `header` zero bytes, then one 16-byte entry each.
std::vector<uint8_t> Properties(size_t header, const std::vector<std::pair<uint32_t, uint64_t>>& fixed) {
    std::vector<uint8_t> out(header, 0);
    for (const auto& [tag, value] : fixed) {
        Put32(out, tag);
        Put32(out, 0x06);                       // readable, writable
        Put32(out, static_cast<uint32_t>(value));
        Put32(out, static_cast<uint32_t>(value >> 32));
    }
    return out;
}

// LZFu as Outlook writes it, kept simple: the first six bytes a reference
// to "{\rtf1" in the initial dictionary, the rest literals.
std::vector<uint8_t> CompressRtf(const std::string& rtf) {
    std::vector<uint8_t> body;
    body.push_back(0x01);                       // first token a reference
    body.push_back(0x00);
    body.push_back(0x04);                       // offset 0, length 6
    size_t at = 6;
    for (int bit = 1; bit < 8 && at < rtf.size(); ++bit) body.push_back(static_cast<uint8_t>(rtf[at++]));
    while (at < rtf.size()) {
        body.push_back(0x00);
        for (int bit = 0; bit < 8 && at < rtf.size(); ++bit) body.push_back(static_cast<uint8_t>(rtf[at++]));
    }
    std::vector<uint8_t> out;
    Put32(out, static_cast<uint32_t>(body.size() + 12));
    Put32(out, static_cast<uint32_t>(rtf.size()));
    Put32(out, 0x75465A4Cu);                    // "LZFu"
    Put32(out, 0);                              // CRC (not checked)
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

const char* kRtf =
    "{\\rtf1\\ansi\\ansicpg1252\\fromhtml1 \\deff0{\\fonttbl{\\f0\\fswiss Arial;}}{\\colortbl;\\red0\\green0\\blue0;}\r\n"
    "{\\*\\htmltag19 <html>}{\\*\\htmltag50 <body>}{\\*\\htmltag64 <p>}Caf\\'e9 \\u8364?"
    "{\\*\\htmltag84 &amp;}\\htmlrtf  RTF only\\htmlrtf0  done{\\*\\htmltag72 </p>}\r\n"
    "{\\*\\htmltag84 <img src=\"cid:logo@x\">}{\\*\\mhtmltag84 <img src=\"logo.png\">}"
    "{\\*\\htmltag58 </body>}{\\*\\htmltag27 </html>}}";

std::vector<uint8_t> BuildMessage(bool withHtmlProperty) {
    UCCompoundFileWriter w;
    // 2026-10-29 09:00:00 UTC as a FILETIME; the code page 1252.
    w.AddStream("__properties_version1.0",
                Properties(32, { { 0x0E060040u, 134377380000000000ULL }, { 0x3FDE0003u, 1252 } }));
    w.AddStream("__substg1.0_0037001F", Utf16Resume());
    w.AddStream("__substg1.0_0C1A001F", Utf16("Alice Example"));
    w.AddStream("__substg1.0_0C1F001F", Utf16("/O=EXCHANGE/OU=FIRST/CN=ALICE"));   // not an address
    w.AddStream("__substg1.0_5D01001F", Utf16("alice@example.org"));
    w.AddStream("__substg1.0_1000001F", Utf16("Cafe EUR & done"));
    w.AddStream("__substg1.0_10090102", CompressRtf(kRtf));
    if (withHtmlProperty)
        w.AddStream("__substg1.0_10130102", Bytes("<html><body><p>From PR_HTML: na\xEFve</p></body></html>"));

    w.AddStream("__recip_version1.0_#00000000/__properties_version1.0", Properties(8, { { 0x0C150003u, 1 } }));
    w.AddStream("__recip_version1.0_#00000000/__substg1.0_3001001F", Utf16("Bob"));
    w.AddStream("__recip_version1.0_#00000000/__substg1.0_39FE001F", Utf16("bob@example.org"));
    w.AddStream("__recip_version1.0_#00000001/__properties_version1.0", Properties(8, { { 0x0C150003u, 2 } }));
    w.AddStream("__recip_version1.0_#00000001/__substg1.0_3001001F", Utf16("Carol"));
    w.AddStream("__recip_version1.0_#00000001/__substg1.0_3003001F", Utf16("carol@example.org"));
    w.AddStream("__recip_version1.0_#00000002/__properties_version1.0", Properties(8, { { 0x0C150003u, 3 } }));
    w.AddStream("__recip_version1.0_#00000002/__substg1.0_3001001F", Utf16("Secret"));

    w.AddStream("__attach_version1.0_#00000000/__properties_version1.0", Properties(8, { { 0x37050003u, 1 } }));
    w.AddStream("__attach_version1.0_#00000000/__substg1.0_3707001F", Utf16("report.pdf"));
    w.AddStream("__attach_version1.0_#00000000/__substg1.0_370E001F", Utf16("application/pdf"));
    w.AddStream("__attach_version1.0_#00000000/__substg1.0_37010102", Bytes("%PDF-1.4\n"));
    w.AddStream("__attach_version1.0_#00000001/__properties_version1.0",
                Properties(8, { { 0x37050003u, 1 }, { 0x7FFE000Bu, 1 } }));
    // An 8-bit name, in the message's code page: "logo-é.png".
    w.AddStream("__attach_version1.0_#00000001/__substg1.0_3704001E", Bytes(std::string("logo-\xE9.png") + '\0'));
    w.AddStream("__attach_version1.0_#00000001/__substg1.0_370E001F", Utf16("IMAGE/PNG"));
    w.AddStream("__attach_version1.0_#00000001/__substg1.0_3712001F", Utf16("<logo@x>"));
    w.AddStream("__attach_version1.0_#00000001/__substg1.0_37010102", Bytes("\x89PNG"));
    w.AddStream("__attach_version1.0_#00000002/__properties_version1.0", Properties(8, { { 0x37050003u, 5 } }));
    w.AddStream("__attach_version1.0_#00000002/__substg1.0_3001001F", Utf16("Forwarded note"));
    w.AddStream("__attach_version1.0_#00000002/__substg1.0_3701000D/__substg1.0_0037001F", Utf16("Inner"));
    return w.Build();
}

} // namespace

static void TestCodePages() {
    std::printf("DecodeWindowsCodePage\n");
    const uint8_t western[] = { 'C', 'a', 'f', 0xE9, ' ', 0x80 };
    Check(DecodeWindowsCodePage(western, sizeof(western), 1252) == "Caf\xC3\xA9 \xE2\x82\xAC", "1252: é and €");
    const uint8_t cyrillic[] = { 0xCF, 0xF0, 0xE8 };
    Check(DecodeWindowsCodePage(cyrillic, sizeof(cyrillic), 1251) == "\xD0\x9F\xD1\x80\xD0\xB8", "1251: При");
    const uint8_t central[] = { 0x8A, 0xB9 };
    Check(DecodeWindowsCodePage(central, sizeof(central), 1250) == "\xC5\xA0\xC4\x85", "1250: Šą");
    const std::string utf8 = "na\xC3\xAFve";
    Check(DecodeWindowsCodePage(reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size(), 65001) == utf8,
          "65001: UTF-8 as it is");
}

static void TestCompoundPaths() {
    std::printf("compound file: streams and storages below the root\n");
    UCCompoundFileReader file;
    Check(file.OpenFromMemory(BuildMessage(false)), "the message opens as a compound file");
    std::vector<uint8_t> bytes;
    Check(file.ReadStreamAt("__attach_version1.0_#00000000/__substg1.0_37010102", bytes) &&
          std::string(bytes.begin(), bytes.end()) == "%PDF-1.4\n", "ReadStreamAt: a stream in a storage");
    Check(file.ReadStreamAt("__ATTACH_version1.0_#00000000/__SUBSTG1.0_37010102", bytes),
          "names compare without regard to case");
    Check(!file.ReadStreamAt("__attach_version1.0_#00000000", bytes), "a storage is not a stream");
    Check(!file.HasStreamAt("__attach_version1.0_#00000009/__substg1.0_37010102"), "a missing storage");
    Check(file.HasStreamAt("__attach_version1.0_#00000002/__substg1.0_3701000D/__substg1.0_0037001F"),
          "three levels down");
    const auto storages = file.StorageNamesAt("");
    Check(storages.size() == 6, "StorageNamesAt(\"\"): three recipients, three attachments");
    const auto streams = file.StreamNamesAt("__recip_version1.0_#00000001");
    Check(streams.size() == 3, "StreamNamesAt: a recipient's streams");
    Check(file.StorageNamesAt("__attach_version1.0_#00000002").size() == 1, "an attached message's storage");
}

static void TestRtf() {
    std::printf("compressed RTF and the HTML it holds\n");
    std::string rtf;
    Check(UCDecompressRtf(CompressRtf(kRtf), rtf) && rtf == kRtf, "LZFu: a dictionary reference and literals");
    std::vector<uint8_t> stored;
    Put32(stored, static_cast<uint32_t>(12 + 5));
    Put32(stored, 5);
    Put32(stored, 0x414C454Du);                 // "MELA"
    Put32(stored, 0);
    for (char c : std::string("{\\rtf")) stored.push_back(static_cast<uint8_t>(c));
    Check(UCDecompressRtf(stored, rtf) && rtf == "{\\rtf", "MELA: stored uncompressed");
    Check(!UCDecompressRtf(std::vector<uint8_t>(16, 0), rtf), "another header is refused");

    std::string html;
    Check(UCHtmlFromEncapsulatedRtf(kRtf, html), "\\fromhtml1: encapsulated HTML");
    Check(html == "<html><body><p>Caf\xC3\xA9 \xE2\x82\xAC&amp; done</p><img src=\"cid:logo@x\"></body></html>",
          "tags as written, \\'e9 in the code page, \\u8364 with its fallback skipped, \\htmlrtf left out, "
          "\\mhtmltag ignored (" + html + ")");
    Check(!UCHtmlFromEncapsulatedRtf("{\\rtf1\\ansi Plain {\\b rich} text}", html), "rich text that never was HTML");
}

static void TestMessage() {
    std::printf("an Outlook message\n");
    UCOutlookMessage msg;
    std::string error;
    Check(msg.LoadFromMemory(BuildMessage(false), error), "it loads (" + error + ")");
    Check(msg.subject == "Quarterly r\xC3\xA9sum\xC3\xA9", "the subject, from UTF-16");
    Check(msg.from == "Alice Example <alice@example.org>", "From: the name and the SMTP address, not the Exchange one");
    Check(msg.to.size() == 1 && msg.to[0] == "Bob <bob@example.org>", "To");
    Check(msg.cc.size() == 1 && msg.cc[0] == "Carol <carol@example.org>", "Cc");
    Check(msg.date == "Thu, 29 Oct 2026 09:00:00 +0000", "the delivery date (" + msg.date + ")");
    Check(msg.bodyText == "Cafe EUR & done", "the plain body");
    Check(msg.bodyHtml.find("<p>Caf\xC3\xA9 \xE2\x82\xAC&amp; done</p>") != std::string::npos,
          "the HTML body, out of the compressed RTF");
    Check(msg.attachments.size() == 3, "three attachments");
    if (msg.attachments.size() == 3) {
        const auto& pdf = msg.attachments[0];
        Check(pdf.filename == "report.pdf" && pdf.mediaType == "application/pdf" &&
              std::string(pdf.data.begin(), pdf.data.end()) == "%PDF-1.4\n" && !pdf.hidden,
              "an attachment by value: name, type, bytes");
        const auto& logo = msg.attachments[1];
        Check(logo.filename == "logo-\xC3\xA9.png", "an 8-bit name, in the code page");
        Check(logo.hidden && logo.contentId == "logo@x" && logo.mediaType == "image/png",
              "a hidden picture: its Content-ID without <>, its type lower-case");
        const auto& inner = msg.attachments[2];
        Check(inner.embeddedMessage && inner.filename == "Forwarded note" && inner.data.empty(),
              "an attached message: listed by name, no bytes");
    }

    UCOutlookMessage withHtml;
    Check(withHtml.LoadFromMemory(BuildMessage(true), error) &&
          withHtml.bodyHtml == "<html><body><p>From PR_HTML: na\xC3\xAFve</p></body></html>",
          "PR_HTML wins over the RTF, read in the code page");

    UCOutlookMessage none;
    Check(!none.LoadFromMemory(std::vector<uint8_t>(600, 'x'), error) && !error.empty(),
          "not a compound file: refused, with a reason");
    UCCompoundFileWriter empty;
    empty.AddStream("Workbook", Bytes("not mail"));
    Check(!none.LoadFromMemory(empty.Build(), error), "a compound file that holds no message: refused");
}

int main() {
    TestCodePages();
    TestCompoundPaths();
    TestRtf();
    TestMessage();
    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
