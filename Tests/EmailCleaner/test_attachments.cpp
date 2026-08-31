// Tests/EmailCleaner/test_attachments.cpp
// Getting an attachment back out of the mail cache — and, mostly, refusing to.
// Driven over real RFC 5322 messages written to a temporary tree; no network.
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerAttachments.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace EmailCleaner;

namespace {

// A unique scratch directory per test, cleaned up by the caller.
std::filesystem::path TempDir() {
    static int counter = 0;
    // Counter plus a clock reading, so two runs in the same second cannot
    // collide on a shared temp directory.
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path p = std::filesystem::temp_directory_path() /
        ("ec_att_" + std::to_string(++counter) + "_" + std::to_string(stamp));
    std::filesystem::remove_all(p);
    std::filesystem::create_directories(p);
    return p;
}

// A multipart message carrying one base64 attachment. "Hello!" is
// SGVsbG8h in base64.
std::string MessageWith(const std::string& filename, const std::string& mediaType,
                        const std::string& base64Body = "SGVsbG8h") {
    return
        "From: Someone <someone@example.com>\r\n"
        "To: Erika <erika@example.com>\r\n"
        "Subject: Here you go\r\n"
        "Date: Tue, 03 Mar 2026 08:00:00 +0100\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: multipart/mixed; boundary=\"BOUND\"\r\n"
        "\r\n"
        "--BOUND\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "\r\n"
        "See attached.\r\n"
        "--BOUND\r\n"
        "Content-Type: " + mediaType + "; name=\"" + filename + "\"\r\n"
        "Content-Disposition: attachment; filename=\"" + filename + "\"\r\n"
        "Content-Transfer-Encoding: base64\r\n"
        "\r\n"
        + base64Body + "\r\n"
        "--BOUND--\r\n";
}

// Put a message where the ingest would have cached it.
void WriteCached(const std::filesystem::path& mailCacheDir, const std::string& account,
                 const std::string& folder, int64_t uid, const std::string& raw) {
    const std::filesystem::path dir = mailCacheDir / account / folder;
    std::filesystem::create_directories(dir);
    std::ofstream out(dir / (std::to_string(uid) + ".eml"), std::ios::binary);
    out << raw;
}

AttachmentRecord Record(const std::string& filename, const std::string& mediaType,
                        bool risky = false) {
    AttachmentRecord a;
    a.filename  = filename;
    a.mediaType = mediaType;
    a.risky     = risky;
    return a;
}

std::string AsText(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

} // namespace

// ---- Where the cache keeps a message ---------------------------------------

TEST(Attachment_CachePathFollowsUltraMailsLayout) {
    REQUIRE_EQ(CachedMessagePath("/data/mail", "erika", "INBOX", 42),
               std::string("/data/mail/erika/INBOX/42.eml"));
    // Anything missing is not a path.
    REQUIRE(CachedMessagePath("", "erika", "INBOX", 42).empty());
    REQUIRE(CachedMessagePath("/data/mail", "erika", "INBOX", 0).empty());
}

// ---- The refusal, which is the point ---------------------------------------

TEST(Attachment_RiskyTypesAreRefusedBeforeAnythingIsRead) {
    // No cache directory at all: the refusal must not depend on finding the
    // message, or a risky file would be "refused" only by accident.
    std::vector<uint8_t> bytes;
    const AttachmentFetch status = FetchAttachment(
        "/nonexistent", "erika", "INBOX", 1,
        Record("invoice.exe", "application/octet-stream"), bytes);
    REQUIRE(status == AttachmentFetch::RefusedRisky);
    REQUIRE(bytes.empty());
}

TEST(Attachment_RefusalIgnoresAStaleHarmlessStamp) {
    // The index says risky = false; the name says otherwise. The name wins,
    // because an index written before a rule change must not open a door.
    const std::filesystem::path root = TempDir();
    WriteCached(root, "erika", "INBOX", 1,
                MessageWith("payload.js", "application/javascript"));

    std::vector<uint8_t> bytes;
    AttachmentRecord record = Record("payload.js", "application/javascript",
                                     /*risky=*/false);
    REQUIRE(FetchAttachment(root.string(), "erika", "INBOX", 1, record, bytes) ==
            AttachmentFetch::RefusedRisky);
    REQUIRE(bytes.empty());
    std::filesystem::remove_all(root);
}

TEST(Attachment_RefusalCatchesADoubleExtension) {
    std::vector<uint8_t> bytes;
    REQUIRE(FetchAttachment("/nonexistent", "erika", "INBOX", 1,
                            Record("invoice.pdf.exe", "application/pdf"), bytes) ==
            AttachmentFetch::RefusedRisky);
}

TEST(Attachment_TheMessagesRealTypeIsWhatDecides) {
    // The index row is entirely harmless; the part in the message is not.
    // Matching by name and then checking the part is what closes that gap.
    const std::filesystem::path root = TempDir();
    WriteCached(root, "erika", "INBOX", 1,
                MessageWith("notes.docm", "application/vnd.ms-word.document.macroEnabled.12"));

    std::vector<uint8_t> bytes;
    AttachmentRecord record;
    record.filename  = "notes.docm";
    record.mediaType = "text/plain";     // what the index wrongly believes
    record.risky     = false;
    REQUIRE(FetchAttachment(root.string(), "erika", "INBOX", 1, record, bytes) ==
            AttachmentFetch::RefusedRisky);
    std::filesystem::remove_all(root);
}

TEST(Attachment_RefusalExplainsItselfAsADecision) {
    const std::string text = DescribeFetch(AttachmentFetch::RefusedRisky, "invoice.exe");
    REQUIRE(text.find("invoice.exe") != std::string::npos);
    REQUIRE(text.find("will not open") != std::string::npos);
    REQUIRE(text.find("untouched") != std::string::npos);
}

// ---- The ordinary case -----------------------------------------------------

TEST(Attachment_HarmlessOneComesBackDecoded) {
    const std::filesystem::path root = TempDir();
    WriteCached(root, "erika", "INBOX", 7, MessageWith("photo.png", "image/png"));

    std::vector<uint8_t> bytes;
    REQUIRE(FetchAttachment(root.string(), "erika", "INBOX", 7,
                            Record("photo.png", "image/png"), bytes) ==
            AttachmentFetch::Ok);
    REQUIRE_EQ(AsText(bytes), std::string("Hello!"));
    std::filesystem::remove_all(root);
}

TEST(Attachment_AnEmptyNameTakesTheFirstPart) {
    const std::filesystem::path root = TempDir();
    WriteCached(root, "erika", "INBOX", 7, MessageWith("photo.png", "image/png"));

    std::vector<uint8_t> bytes;
    REQUIRE(FetchAttachment(root.string(), "erika", "INBOX", 7,
                            Record("", "image/png"), bytes) == AttachmentFetch::Ok);
    REQUIRE_EQ(AsText(bytes), std::string("Hello!"));
    std::filesystem::remove_all(root);
}

TEST(Attachment_MissingMessageAndMissingPartAreDistinguished) {
    const std::filesystem::path root = TempDir();
    WriteCached(root, "erika", "INBOX", 7, MessageWith("photo.png", "image/png"));

    std::vector<uint8_t> bytes;
    REQUIRE(FetchAttachment(root.string(), "erika", "INBOX", 99,
                            Record("photo.png", "image/png"), bytes) ==
            AttachmentFetch::NoSuchMessage);
    REQUIRE(FetchAttachment(root.string(), "erika", "INBOX", 7,
                            Record("other.png", "image/png"), bytes) ==
            AttachmentFetch::NoSuchPart);
    std::filesystem::remove_all(root);
}

// ---- Writing it somewhere a viewer can open --------------------------------

TEST(Attachment_NameSanitiserRefusesToLeaveTheDirectory) {
    REQUIRE_EQ(SafeAttachmentName("../../.bashrc", ""), std::string("bashrc"));
    REQUIRE_EQ(SafeAttachmentName("/etc/passwd", ""), std::string("passwd"));
    REQUIRE_EQ(SafeAttachmentName("..\\..\\win.ini", ""), std::string("win.ini"));
    REQUIRE_EQ(SafeAttachmentName("..", ""), std::string("attachment"));
    REQUIRE_EQ(SafeAttachmentName("", "image/png"), std::string("attachment.png"));
    REQUIRE_EQ(SafeAttachmentName("holiday photo.png", ""),
               std::string("holiday photo.png"));
}

TEST(Attachment_WriteLandsInsideTheCacheDirectory) {
    const std::filesystem::path root = TempDir();
    const std::vector<uint8_t> bytes = { 'a', 'b', 'c' };

    const std::string path = WriteToCache((root / "cache").string(),
                                          "report.pdf", "application/pdf", bytes);
    REQUIRE(!path.empty());
    REQUIRE(std::filesystem::exists(path));
    REQUIRE_EQ(std::filesystem::file_size(path), static_cast<std::uintmax_t>(3));
    REQUIRE(path.find("cache") != std::string::npos);

    // Even a hostile name stays put.
    const std::string escaped = WriteToCache((root / "cache").string(),
                                             "../../escaped.txt", "text/plain", bytes);
    REQUIRE(!escaped.empty());
    REQUIRE(std::filesystem::path(escaped).parent_path() ==
            std::filesystem::path((root / "cache").string()));
    REQUIRE(!std::filesystem::exists(root / "escaped.txt"));
    std::filesystem::remove_all(root);
}

TEST(Attachment_StatusStringsRoundTrip) {
    REQUIRE_EQ(ToString(AttachmentFetch::Ok), std::string("ok"));
    REQUIRE_EQ(ToString(AttachmentFetch::RefusedRisky), std::string("refused-risky"));
    REQUIRE_EQ(ToString(AttachmentFetch::NoSuchMessage), std::string("no-such-message"));
    REQUIRE_EQ(ToString(AttachmentFetch::NoSuchPart), std::string("no-such-part"));
    REQUIRE_EQ(ToString(AttachmentFetch::Unreadable), std::string("unreadable"));
}
