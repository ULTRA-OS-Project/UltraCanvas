// Tests/UltraMail/MessageImportTest.cpp
// Import's conversion of message files (UltraMailMessageImport.h): an .eml
// passes as it is, an .emlx gives the message inside it, an Outlook .msg is
// built again as MIME - sender, recipients, subject, Message-ID, In-Reply-To,
// the HTML body with its text alternative, the picture the HTML shows inline
// with its Content-ID, the attachment - and what is no message is refused
// with a reason. Headless: the .msg is built with UCCompoundFileWriter and
// the result read back with UltraNet's MIME reader.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMailMessageImport.h"

#include "UltraCanvasCompoundFile.h"
#include <UltraNet/UltraNetMime.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace UltraMail;

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& what) {
    std::printf("  %s %s\n", cond ? "ok:  " : "FAIL:", what.c_str());
    if (!cond) ++g_failures;
}

std::vector<uint8_t> Bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

std::vector<uint8_t> Utf16(const std::string& ascii) {
    std::vector<uint8_t> out;
    for (unsigned char c : ascii) { out.push_back(c); out.push_back(0); }
    out.push_back(0); out.push_back(0);
    return out;
}

const char* kEml =
    "From: Alice <alice@example.org>\r\nTo: bob@example.org\r\nSubject: Hello\r\n"
    "Message-ID: <hello@example.org>\r\n\r\nHi Bob.\r\n";

std::vector<uint8_t> OutlookMessage() {
    UltraCanvas::UCCompoundFileWriter w;
    w.AddStream("__properties_version1.0", std::vector<uint8_t>(32, 0));
    w.AddStream("__substg1.0_0037001F", Utf16("Board minutes"));
    w.AddStream("__substg1.0_1035001F", Utf16("<minutes@example.org>"));
    w.AddStream("__substg1.0_1042001F", Utf16("<agenda@example.org>"));
    w.AddStream("__substg1.0_0C1A001F", Utf16("Carol Chair"));
    w.AddStream("__substg1.0_5D01001F", Utf16("carol@example.org"));
    w.AddStream("__substg1.0_1000001F", Utf16("The minutes are attached."));
    w.AddStream("__substg1.0_10130102",
                Bytes("<html><body><p>The minutes are attached.</p><img src=\"cid:seal@x\"></body></html>"));
    w.AddStream("__recip_version1.0_#00000000/__properties_version1.0", std::vector<uint8_t>(8, 0));
    w.AddStream("__recip_version1.0_#00000000/__substg1.0_3001001F", Utf16("Dan"));
    w.AddStream("__recip_version1.0_#00000000/__substg1.0_39FE001F", Utf16("dan@example.org"));
    w.AddStream("__attach_version1.0_#00000000/__properties_version1.0", std::vector<uint8_t>(8, 0));
    w.AddStream("__attach_version1.0_#00000000/__substg1.0_3707001F", Utf16("minutes.pdf"));
    w.AddStream("__attach_version1.0_#00000000/__substg1.0_370E001F", Utf16("application/pdf"));
    w.AddStream("__attach_version1.0_#00000000/__substg1.0_37010102", Bytes("%PDF-1.4\n"));
    // A hidden picture the HTML names by cid: (PR_ATTACHMENT_HIDDEN = 1).
    std::vector<uint8_t> hidden(8, 0);
    const uint8_t entry[16] = { 0x0B, 0x00, 0xFE, 0x7F, 0x06, 0, 0, 0, 0x01, 0, 0, 0, 0, 0, 0, 0 };
    hidden.insert(hidden.end(), entry, entry + 16);
    w.AddStream("__attach_version1.0_#00000001/__properties_version1.0", hidden);
    w.AddStream("__attach_version1.0_#00000001/__substg1.0_3707001F", Utf16("seal.png"));
    w.AddStream("__attach_version1.0_#00000001/__substg1.0_370E001F", Utf16("image/png"));
    w.AddStream("__attach_version1.0_#00000001/__substg1.0_3712001F", Utf16("seal@x"));
    w.AddStream("__attach_version1.0_#00000001/__substg1.0_37010102", Bytes("\x89PNG\r\n"));
    return w.Build();
}

} // namespace

int main() {
    std::printf("which files Import reads\n");
    Check(IsImportableMessageFile("a.eml") && IsImportableMessageFile("B.EMLX") && IsImportableMessageFile("c.Msg"),
          ".eml, .emlx, .msg, any case");
    Check(!IsImportableMessageFile("notes.txt") && !IsImportableMessageFile("msg") &&
          !IsImportableMessageFile("dir.eml/file"), "not a .txt, a bare word, or a folder named .eml");

    std::string raw, error;
    std::printf("an .eml\n");
    Check(MessageFileToRfc822("hello.eml", Bytes(kEml), raw, error) && raw == kEml, "passes as it is");
    Check(!MessageFileToRfc822("notes.eml", Bytes("Just some words, no header at all."), raw, error) && !error.empty(),
          "text that is no message is refused, with a reason (" + error + ")");

    std::printf("an .emlx\n");
    const std::string message(kEml);
    const std::string emlx = std::to_string(message.size()) + "\n" + message +
                             "<?xml version=\"1.0\"?>\n<plist version=\"1.0\"><dict/></plist>\n";
    Check(MessageFileToRfc822("123.emlx", Bytes(emlx), raw, error) && raw == message,
          "the message inside, without Mail's property list");

    std::printf("an Outlook .msg\n");
    Check(MessageFileToRfc822("minutes.msg", OutlookMessage(), raw, error), "converts (" + error + ")");
    UltraNetMimeMessage parsed;
    Check(UltraNet_MimeParse(raw, parsed), "the result reads as MIME");
    Check(parsed.subject == "Board minutes", "its subject");
    Check(parsed.from.find("carol@example.org") != std::string::npos && parsed.from.find("Carol Chair") != std::string::npos,
          "its sender (" + parsed.from + ")");
    Check(parsed.to.size() == 1 && parsed.to[0].find("dan@example.org") != std::string::npos, "its recipient");
    Check(parsed.messageId.find("minutes@example.org") != std::string::npos, "its own Message-ID, kept");
    Check(parsed.inReplyTo.find("agenda@example.org") != std::string::npos, "In-Reply-To, kept");
    std::string body;
    bool isHtml = false;
    Check(UltraNet_MimeGetDisplayBody(parsed, body, isHtml) && isHtml &&
          body.find("The minutes are attached.") != std::string::npos && body.find("cid:seal@x") != std::string::npos,
          "the HTML body, its cid: reference intact");
    std::vector<UltraNetMimeAttachmentView> parts;
    UltraNet_MimeCollectAttachments(parsed, parts, /*includeInline=*/true);
    bool pdf = false, seal = false;
    for (const auto& p : parts) {
        if (p.filename == "minutes.pdf" && !p.isInline && std::string(p.data.begin(), p.data.end()) == "%PDF-1.4\n")
            pdf = true;
        if (p.contentId == "seal@x" && p.mediaType == "image/png") seal = true;
    }
    Check(pdf, "the attachment, its bytes as they were");
    Check(seal, "the picture the HTML shows, inline with its Content-ID");

    Check(!MessageFileToRfc822("fake.msg", Bytes("$set 1\n1 Hello\n"), raw, error) && !error.empty(),
          "a .msg that is no Outlook message is refused (" + error + ")");
    Check(!MessageFileToRfc822("photo.jpg", Bytes("\xFF\xD8\xFF"), raw, error), "another kind of file is refused");

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
