// Tests/EmailCleaner/test_contacts.cpp
// The contacts review: outgoing mail recorded by whom it went to, the sent and
// received counts per address, and the verdict each address-book contact gets.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerContacts.h"
#include "EmailCleanerIngest.h"
#include "UltraCanvasPathUtf8.h"

#include <UltraDatabase/UltraDatabase.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace EmailCleaner;
using UltraCanvas::PathFromUtf8;

namespace {

int NextId() {
    static int counter = 0;
    return ++counter;
}

AnalysisStore OpenStore() {
    AnalysisStore store;
    const std::string name = "ec_contacts_" + std::to_string(NextId());
    const UltraDbResult r = store.Open(name, ":memory:");
    if (!r) throw emailcleaner_test::Failure{ "store.Open failed: " + r.message };
    return store;
}

class TempDir {
public:
    TempDir() {
        path_ = std::filesystem::temp_directory_path() /
                ("emailcleaner_contacts_" + std::to_string(NextId()) + "_" +
                 std::to_string(std::chrono::steady_clock::now()
                                    .time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    std::string String() const { return UltraCanvas::PathToUtf8(path_); }

    void Write(const std::string& relative, const std::string& content) const {
        const std::filesystem::path full = path_ / PathFromUtf8(relative);
        std::filesystem::create_directories(full.parent_path());
        std::ofstream out(full, std::ios::binary);
        out << content;
    }

private:
    std::filesystem::path path_;
};

std::string Message(const std::string& from, const std::string& to, const std::string& cc,
                    const std::string& id, int day) {
    std::string raw = "From: " + from + "\r\n" + "To: " + to + "\r\n";
    if (!cc.empty()) raw += "Cc: " + cc + "\r\n";
    raw += "Subject: Hello\r\n"
           "Date: " + std::to_string(day) + " Sep 2026 10:00:00 +0000\r\n"
           "Message-ID: <" + id + ">\r\n"
           "Content-Type: text/plain; charset=utf-8\r\n"
           "\r\n"
           "See you soon.\r\n";
    return raw;
}

UltraMail::Contact MakeContact(int64_t id, const std::string& name,
                               std::vector<std::string> emails,
                               UltraMail::ContactSection section = UltraMail::ContactSection::Other,
                               const std::string& group = "") {
    UltraMail::Contact c;
    c.id          = id;
    c.displayName = name;
    c.section     = section;
    c.group       = group;
    for (const std::string& e : emails) c.emails.push_back({ e, "", false });
    return c;
}

} // namespace

TEST(Contacts_SentFolderNamesAreRecognised) {
    REQUIRE(LooksLikeSentDirectory("Sent"));
    REQUIRE(LooksLikeSentDirectory("Sent Items"));
    REQUIRE(LooksLikeSentDirectory("[Gmail]_Sent Mail"));
    REQUIRE(LooksLikeSentDirectory("INBOX.Sent"));
    REQUIRE(LooksLikeSentDirectory("Gesendete Objekte"));
    REQUIRE(!LooksLikeSentDirectory("INBOX"));
    REQUIRE(!LooksLikeSentDirectory("Sentimental"));
    REQUIRE(!LooksLikeSentDirectory("Trash"));
}

TEST(Contacts_AnalyzeNamesRecipientsAndSpotsTheOwnersMail) {
    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);

    const AnalyzedMessage mine = ingestor.Analyze(
        Message("Erika <Erika@Example.com>", "Jonas <jonas@partner.example>, ana@x.example",
                "JONAS@partner.example", "m1@example.com", 1),
        "erika", "INBOX", 1, "erika@example.com");
    REQUIRE(mine.outgoing);
    REQUIRE_EQ(mine.recipients.size(), static_cast<std::size_t>(2));   // Cc repeats To
    REQUIRE_EQ(mine.recipients[0], std::string("jonas@partner.example"));
    REQUIRE_EQ(mine.recipients[1], std::string("ana@x.example"));

    const AnalyzedMessage theirs = ingestor.Analyze(
        Message("jonas@partner.example", "erika@example.com", "", "t1@x", 2),
        "erika", "INBOX", 2, "erika@example.com");
    REQUIRE(!theirs.outgoing);
}

TEST(Contacts_OutgoingMailIsCountedNotAnalysed) {
    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);
    TempDir cache;

    // Sent folder by name: outgoing whatever the From line says.
    cache.Write("erika/Sent/1.eml",
                Message("Erika <erika@example.com>", "jonas@partner.example", "", "s1@e", 1));
    cache.Write("erika/Sent/2.eml",
                Message("erika@example.com", "jonas@partner.example",
                        "ana@x.example", "s2@e", 3));
    // The same message again in an archive folder: counts once.
    cache.Write("erika/Archive/9.eml",
                Message("erika@example.com", "jonas@partner.example",
                        "ana@x.example", "s2@e", 3));
    // Received mail.
    cache.Write("erika/INBOX/5.eml",
                Message("jonas@partner.example", "erika@example.com", "", "r1@p", 2));
    cache.Write("erika/INBOX/6.eml",
                Message("shop@shop.example", "erika@example.com", "", "r2@s", 4));

    IngestOptions options;
    options.ownerAddress = "erika@example.com";
    options.sentByName   = true;
    IngestStats stats = ingestor.IngestMailCache(cache.String(), "erika", options);
    REQUIRE_EQ(stats.sent, 3);
    REQUIRE_EQ(stats.analysed, 2);
    REQUIRE(store.HasSentMessage("erika", "Sent", 1));
    REQUIRE(!store.HasMessage("erika", "Sent", 1));

    // The owner is not a block on the map.
    std::vector<SenderBlock> blocks;
    REQUIRE(store.ListSenderBlocks(MessageFilter{}, SenderMetric::MessageCount, 0, blocks));
    for (const SenderBlock& block : blocks)
        REQUIRE(block.senderAddr != "erika@example.com");

    std::map<std::string, Correspondence> counts;
    REQUIRE(store.GetCorrespondence(counts));
    REQUIRE_EQ(counts["jonas@partner.example"].sent, 2);
    REQUIRE_EQ(counts["jonas@partner.example"].received, 1);
    REQUIRE_EQ(counts["jonas@partner.example"].lastSent, MakeUtcTime(2026, 9, 3, 10, 0, 0));
    REQUIRE_EQ(counts["ana@x.example"].sent, 1);
    REQUIRE_EQ(counts["shop@shop.example"].sent, 0);
    REQUIRE_EQ(counts["shop@shop.example"].received, 1);

    // A second scan skips what it has seen, the sent mail included.
    stats = ingestor.IngestMailCache(cache.String(), "erika", options);
    REQUIRE_EQ(stats.sent, 0);
    REQUIRE_EQ(stats.skipped, 5);

    // Removing the account takes its sent mail with it.
    REQUIRE(store.RemoveAccount("erika"));
    REQUIRE(store.GetCorrespondence(counts));
    REQUIRE(counts.empty());
}

TEST(Contacts_SentMailAnalysedBeforeIsMovedOver) {
    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);
    TempDir cache;
    cache.Write("erika/Sent/1.eml",
                Message("Erika <erika@example.com>", "jonas@partner.example", "", "s1@e", 1));

    // What an earlier version stored: the owner's mail, analysed.
    IngestOptions before;
    before.ownerAddress = "";
    REQUIRE_EQ(ingestor.IngestMailCache(cache.String(), "erika", before).analysed, 1);
    REQUIRE(store.HasMessage("erika", "Sent", 1));

    IngestOptions options;
    options.ownerAddress = "erika@example.com";
    options.sentByName   = true;
    const IngestStats stats = ingestor.IngestMailCache(cache.String(), "erika", options);
    REQUIRE_EQ(stats.sent, 1);
    REQUIRE(!store.HasMessage("erika", "Sent", 1));
    REQUIRE(store.HasSentMessage("erika", "Sent", 1));
}

TEST(Contacts_ReviewTagsAndOrdersByMailSent) {
    std::map<std::string, Correspondence> counts;
    counts["jonas@partner.example"] = { "jonas@partner.example", 2, 7, 100, 200 };
    counts["jonas@home.example"]    = { "jonas@home.example", 3, 0, 300, 0 };
    counts["ana@x.example"]         = { "ana@x.example", 9, 1, 50, 60 };
    counts["shop@shop.example"]     = { "shop@shop.example", 0, 40, 0, 500 };

    const std::vector<UltraMail::Contact> contacts = {
        MakeContact(1, "Shop", { "shop@shop.example" }, UltraMail::ContactSection::Services),
        MakeContact(2, "Jonas", { "Jonas@Partner.example", "jonas@home.example" }),
        MakeContact(3, "Ana", { "ana@x.example" }),
        MakeContact(4, "Stranger", { "nobody@x.example" }),
        MakeContact(5, "Grandma", { "oma@family.example" }, UltraMail::ContactSection::Family),
        MakeContact(6, "Club", { "club@x.example" }, UltraMail::ContactSection::Other, "Sports"),
        MakeContact(7, "Phone only", {}),
    };
    const std::vector<ContactReview> reviews = ReviewContacts(contacts, counts);
    REQUIRE_EQ(reviews.size(), contacts.size());

    // Sent first: Ana 9, Jonas 2 + 3 over two addresses; then received.
    REQUIRE_EQ(reviews[0].contactId, 3);
    REQUIRE_EQ(reviews[1].contactId, 2);
    REQUIRE_EQ(reviews[1].sent, 5);
    REQUIRE_EQ(reviews[1].received, 7);
    REQUIRE_EQ(reviews[1].lastSent, 300);
    REQUIRE(reviews[1].verdict == ContactVerdict::Keep);
    REQUIRE_EQ(reviews[2].contactId, 1);   // never written to, 40 received
    REQUIRE(reviews[2].verdict == ContactVerdict::Delete);

    std::map<int64_t, ContactVerdict> verdicts;
    for (const ContactReview& r : reviews) verdicts[r.contactId] = r.verdict;
    REQUIRE(verdicts[4] == ContactVerdict::Delete);   // collected, never written to
    REQUIRE(verdicts[5] == ContactVerdict::Review);   // filed under Family
    REQUIRE(verdicts[6] == ContactVerdict::Review);   // in a group of the user's
    REQUIRE(verdicts[7] == ContactVerdict::Review);   // no address at all

    const ContactReviewSummary summary = Summarize(reviews);
    REQUIRE_EQ(summary.total, 7);
    REQUIRE_EQ(summary.keep, 2);
    REQUIRE_EQ(summary.toDelete, 2);
    REQUIRE_EQ(summary.review, 3);
    REQUIRE_EQ(DescribeReview(summary),
               std::string("7 contacts: you wrote to 2 (keep), 2 only collected and never "
                           "written to (marked for deletion), 3 to review."));
}

TEST(Contacts_RemoveDeletesFromTheAddressBook) {
    UltraMail::ContactStore book;
    REQUIRE(book.Open("ec_contacts_book_" + std::to_string(NextId()), ":memory:"));
    UltraMail::Contact keep = MakeContact(0, "Ana", { "ana@x.example" });
    UltraMail::Contact drop = MakeContact(0, "Shop", { "shop@shop.example" });
    REQUIRE(book.Save(keep));
    REQUIRE(book.Save(drop));

    int removed = 0;
    REQUIRE(RemoveContacts(book, { drop.id }, removed));
    REQUIRE_EQ(removed, 1);

    std::vector<UltraMail::Contact> left;
    REQUIRE(book.ListAll(left));
    REQUIRE_EQ(left.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(left[0].displayName, std::string("Ana"));
}
