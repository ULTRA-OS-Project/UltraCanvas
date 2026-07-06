// Tests/UltraMail/test_attachments.cpp
// Exercises the attachment path end-to-end at the engine level: build a MIME
// message with an attachment, parse it back through MimeCodec (extracting the
// display body + attachment bytes), and materialise the attachment to a cache
// file with a sanitised name.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailMimeCodec.h"
#include "UltraMailAttachmentCache.h"

#include <UltraNet/UltraNetMime.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraMail;

namespace {

std::string RawWithAttachment(const std::string& fname, const std::string& mime,
                              const std::vector<uint8_t>& bytes) {
    UltraNetMimeBuildInput in;
    in.from = "a@x.com"; in.to = {"b@y.com"}; in.subject = "with file";
    in.body = "the body text";
    in.date = "Tue, 01 Jan 2026 00:00:00 +0000"; in.messageId = "<id@x>";
    UltraNetMimeBuildAttachment a;
    a.filename = fname; a.mediaType = mime; a.data = bytes;
    in.attachments.push_back(a);
    return UltraNet_MimeBuild(in);
}

} // namespace

TEST(codec_extracts_body_and_attachment) {
    std::vector<uint8_t> bytes = {0x89, 'P', 'N', 'G', 0x00, 0xFF, 0x10, 0x42};
    std::string raw = RawWithAttachment("photo.png", "image/png", bytes);

    ParsedMessage msg = MimeCodec::Parse(raw);
    REQUIRE_EQ(msg.subject, std::string("with file"));
    REQUIRE(msg.body.find("the body text") != std::string::npos);
    REQUIRE_EQ(msg.attachments.size(), (size_t)1);
    REQUIRE_EQ(msg.attachments[0].filename, std::string("photo.png"));
    REQUIRE_EQ(msg.attachments[0].mediaType, std::string("image/png"));
    REQUIRE(msg.attachments[0].data == bytes);
}

TEST(codec_handles_message_without_attachments) {
    UltraNetMimeBuildInput in;
    in.from = "a@x.com"; in.to = {"b@y.com"}; in.subject = "plain";
    in.body = "just text"; in.date = "d"; in.messageId = "<i>";
    ParsedMessage msg = MimeCodec::Parse(UltraNet_MimeBuild(in));
    REQUIRE(msg.attachments.empty());
    REQUIRE(msg.body.find("just text") != std::string::npos);
}

TEST(sanitize_filename_blocks_traversal_and_keeps_extension) {
    REQUIRE_EQ(AttachmentCache::SanitizeFilename("../../etc/passwd", "text/plain"),
               std::string("passwd.txt"));            // path stripped, ext added
    REQUIRE_EQ(AttachmentCache::SanitizeFilename("report.pdf", "application/pdf"),
               std::string("report.pdf"));            // already good
    REQUIRE_EQ(AttachmentCache::SanitizeFilename("", "image/png"),
               std::string("attachment.png"));        // empty -> default + ext
    std::string weird = AttachmentCache::SanitizeFilename("a/b:c*?.txt", "text/plain");
    REQUIRE(weird.find('/') == std::string::npos);
    REQUIRE(weird.find(':') == std::string::npos);
}

TEST(cache_write_roundtrip_and_dedup) {
    fs::path dir = fs::temp_directory_path() / "ultramail_att_test";
    fs::remove_all(dir);
    AttachmentCache cache(dir.string());

    Attachment att;
    att.filename = "doc.txt";
    att.mediaType = "text/plain";
    std::string content = "hello attachment";
    att.data.assign(content.begin(), content.end());

    std::string p1 = cache.Write(att);
    REQUIRE(!p1.empty());
    REQUIRE(fs::exists(p1));

    // Bytes on disk match.
    std::ifstream is(p1, std::ios::binary);
    std::string got((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    REQUIRE_EQ(got, content);

    // Identical content reuses the same file.
    std::string p2 = cache.Write(att);
    REQUIRE_EQ(p1, p2);

    // Different content, same name -> a distinct file.
    Attachment att2 = att;
    att2.data.push_back('!');
    std::string p3 = cache.Write(att2);
    REQUIRE(p3 != p1);
    REQUIRE(fs::exists(p3));

    fs::remove_all(dir);
}

TEST(cache_save_as_explicit_path) {
    fs::path dir = fs::temp_directory_path() / "ultramail_saveas_test";
    fs::remove_all(dir);
    AttachmentCache cache(dir.string());

    Attachment att;
    att.filename = "x.bin";
    att.data = {1, 2, 3, 4, 5};
    fs::path dest = dir / "sub" / "chosen-name.bin";
    REQUIRE(cache.SaveAs(att, dest.string()));
    REQUIRE(fs::exists(dest));
    REQUIRE_EQ(fs::file_size(dest), (uintmax_t)5);

    fs::remove_all(dir);
}
