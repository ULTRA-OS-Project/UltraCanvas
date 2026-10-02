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
#include "UltraCanvasPathUtf8.h"

#include <UltraNet/UltraNetMime.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

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
    REQUIRE(fs::exists(UltraCanvas::PathFromUtf8(p1)));

    // Bytes on disk match.
    std::ifstream is(UltraCanvas::PathFromUtf8(p1), std::ios::binary);
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
    REQUIRE(fs::exists(UltraCanvas::PathFromUtf8(p3)));

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

TEST(codec_counts_attachments_like_parse) {
    const std::vector<uint8_t> bytes = {1, 2, 3, 4};
    const std::string one = RawWithAttachment("a.pdf", "application/pdf", bytes);
    REQUIRE_EQ(MimeCodec::CountAttachments(one), (int)MimeCodec::Parse(one).attachments.size());
    REQUIRE_EQ(MimeCodec::CountAttachments(one), 1);

    UltraNetMimeBuildInput in;
    in.from = "a@x.com"; in.to = {"b@y.com"}; in.subject = "two";
    in.body = "text"; in.date = "d"; in.messageId = "<t>";
    for (const char* name : {"x.txt", "y.zip"}) {
        UltraNetMimeBuildAttachment a;
        a.filename = name; a.mediaType = "application/octet-stream"; a.data = bytes;
        in.attachments.push_back(a);
    }
    REQUIRE_EQ(MimeCodec::CountAttachments(UltraNet_MimeBuild(in)), 2);

    in.attachments.clear();
    REQUIRE_EQ(MimeCodec::CountAttachments(UltraNet_MimeBuild(in)), 0);
    REQUIRE_EQ(MimeCodec::CountAttachments(""), 0);
}

// ---- Pruning: the cache holds copies for the viewer, so it must not only grow

namespace {

Attachment Bytes(const std::string& name, size_t size, char fill = 'x') {
    Attachment att;
    att.filename  = name;
    att.mediaType = "application/octet-stream";
    att.data.assign(size, static_cast<uint8_t>(fill));
    return att;
}

void Age(const std::string& path, int days) {
    fs::last_write_time(UltraCanvas::PathFromUtf8(path),
                        fs::file_time_type::clock::now() - std::chrono::hours(24 * days));
}

} // namespace

TEST(cache_prune_drops_what_was_not_opened_for_a_while) {
    fs::path dir = fs::temp_directory_path() / "ultramail_prune_age";
    fs::remove_all(dir);
    AttachmentCache cache(dir.string());

    const std::string old   = cache.Write(Bytes("old.bin", 10));
    const std::string fresh = cache.Write(Bytes("fresh.bin", 10));
    Age(old, 10);
    // A subdirectory is not the cache's to prune (UltraMail keeps the sender
    // icons in one beside the attachments' folder).
    fs::create_directories(dir / "sender-icons");
    std::ofstream(dir / "sender-icons" / "icon.png") << "png";
    Age((dir / "sender-icons" / "icon.png").string(), 30);

    const AttachmentPruneStats stats = cache.Prune(7 * 24 * 3600, 0);
    REQUIRE_EQ(stats.removed, 1);
    REQUIRE_EQ(stats.bytesRemoved, uint64_t(10));
    REQUIRE(!fs::exists(old));
    REQUIRE(fs::exists(fresh));
    REQUIRE(fs::exists(dir / "sender-icons" / "icon.png"));
    fs::remove_all(dir);
}

TEST(cache_prune_keeps_the_newest_within_the_size_cap) {
    fs::path dir = fs::temp_directory_path() / "ultramail_prune_cap";
    fs::remove_all(dir);
    AttachmentCache cache(dir.string());

    const std::string a = cache.Write(Bytes("a.bin", 100, 'a'));
    const std::string b = cache.Write(Bytes("b.bin", 100, 'b'));
    const std::string c = cache.Write(Bytes("c.bin", 100, 'c'));
    Age(a, 3); Age(b, 2); Age(c, 1);

    // 300 bytes, a cap of 150: the two oldest go, the newest stays.
    const AttachmentPruneStats stats = cache.Prune(0, 150);
    REQUIRE_EQ(stats.removed, 2);
    REQUIRE(!fs::exists(a));
    REQUIRE(!fs::exists(b));
    REQUIRE(fs::exists(c));

    // No limits: nothing goes. -1: everything does.
    REQUIRE_EQ(cache.Prune(0, 0).removed, 0);
    REQUIRE_EQ(cache.Prune(-1, 0).removed, 1);
    REQUIRE(!fs::exists(c));
    fs::remove_all(dir);
}

TEST(cache_reopening_an_attachment_keeps_it_fresh) {
    fs::path dir = fs::temp_directory_path() / "ultramail_prune_reuse";
    fs::remove_all(dir);
    AttachmentCache cache(dir.string());

    const Attachment att = Bytes("report.pdf", 20);
    const std::string first = cache.Write(att);
    Age(first, 10);
    // Opened again today: the identical file is reused, and counts as new.
    REQUIRE_EQ(cache.Write(att), first);
    REQUIRE_EQ(cache.Prune(7 * 24 * 3600, 0).removed, 0);
    REQUIRE(fs::exists(first));
    fs::remove_all(dir);
}

TEST(cache_handles_non_ascii_names) {
    // File names are UTF-8 on every platform (AGENTS.md): a Thai or emoji
    // attachment name must round-trip and prune like any other.
    fs::path dir = fs::temp_directory_path() / "ultramail_prune_utf8";
    fs::remove_all(dir);
    AttachmentCache cache(dir.string());

    const std::string path = cache.Write(Bytes("\xe0\xb8\xa3\xe0\xb8\xb2\xe0\xb8\x87\xe0\xb8\xb2\xe0\xb8\x99 \xf0\x9f\x93\x8e.txt", 5));
    REQUIRE(!path.empty());
    REQUIRE(fs::exists(UltraCanvas::PathFromUtf8(path)));
    REQUIRE_EQ(cache.Prune(-1, 0).removed, 1);
    REQUIRE(!fs::exists(UltraCanvas::PathFromUtf8(path)));
    fs::remove_all(dir);
}
