// Tests/EmailCleaner/test_ingest.cpp
// Parsing real RFC 5322 messages into the analysis database, and the walk over
// UltraMail's cached message bodies.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerIngest.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace EmailCleaner;

namespace {

int NextConnectionId() {
    static int counter = 0;
    return ++counter;
}

AnalysisStore OpenStore() {
    AnalysisStore store;
    const std::string name = "ec_ingest_" + std::to_string(NextConnectionId());
    const UltraDbResult r = store.Open(name, ":memory:");
    if (!r) throw emailcleaner_test::Failure{ "store.Open failed: " + r.message };
    return store;
}

// A scratch directory that cleans itself up.
class TempDir {
public:
    TempDir() {
        path_ = std::filesystem::temp_directory_path() /
                ("emailcleaner_test_" + std::to_string(NextConnectionId()) + "_" +
                 std::to_string(std::chrono::steady_clock::now()
                                    .time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    const std::filesystem::path& Path() const { return path_; }
    std::string String() const { return path_.string(); }

    void Write(const std::string& relative, const std::string& content) const {
        const std::filesystem::path full = path_ / relative;
        std::filesystem::create_directories(full.parent_path());
        std::ofstream out(full, std::ios::binary);
        out << content;
    }

private:
    std::filesystem::path path_;
};

const char* kPersonalMessage =
    "From: Jonas Meyer <jonas@partner.example>\r\n"
    "To: Erika Example <erika@example.com>\r\n"
    "Subject: Lunch on Thursday?\r\n"
    "Date: Fri, 14 Aug 2026 09:30:00 +0000\r\n"
    "Message-ID: <lunch-1@partner.example>\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "\r\n"
    "Hi Erika, are you free for lunch on Thursday around one?\r\n";

const char* kSpamMessage =
    "From: \"Deals\" <x7f3kq91zzt@bulk.example>\r\n"
    "To: undisclosed-recipients:;\r\n"
    "Subject: CHEAP PILLS - CANADIAN PHARMACY!!!\r\n"
    "Date: Sat, 15 Aug 2026 03:12:00 +0000\r\n"
    "Message-ID: <spam-1@bulk.example>\r\n"
    "Precedence: bulk\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "\r\n"
    "<html><body><p>V<b>1</b>AGRA and c1alis with no prescription. "
    "Order now, lowest price!</p></body></html>\r\n";

const char* kNewsletterMessage =
    "From: Museum News <news@museum.example>\r\n"
    "To: Erika Example <erika@example.com>\r\n"
    "Subject: This week at the museum\r\n"
    "Date: Sun, 16 Aug 2026 08:00:00 +0200\r\n"
    "List-Unsubscribe: <https://museum.example/unsubscribe>\r\n"
    "Message-ID: <news-1@museum.example>\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "\r\n"
    "Our programme for the coming week. You are receiving this email because "
    "you subscribed. Unsubscribe at any time.\r\n";

// A multipart message carrying an executable disguised as an invoice.
const char* kMalwareMessage =
    "From: Accounts <accounts@invoice.example>\r\n"
    "To: Erika Example <erika@example.com>\r\n"
    "Subject: Invoice 4451\r\n"
    "Date: Mon, 17 Aug 2026 11:00:00 +0000\r\n"
    "Message-ID: <inv-1@invoice.example>\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: multipart/mixed; boundary=\"BOUND\"\r\n"
    "\r\n"
    "--BOUND\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "\r\n"
    "Please see the attached invoice.\r\n"
    "--BOUND\r\n"
    "Content-Type: application/octet-stream; name=\"invoice_4451.pdf.exe\"\r\n"
    "Content-Disposition: attachment; filename=\"invoice_4451.pdf.exe\"\r\n"
    "Content-Transfer-Encoding: base64\r\n"
    "\r\n"
    "TVpQAAIAAAAEAA8A//8AALgAAAAAAAAAQAAaAAAAAAA=\r\n"
    "--BOUND--\r\n";

} // namespace

TEST(Ingest_AnalyzeReadsHeadersAndClassifies) {
    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);

    const AnalyzedMessage m =
        ingestor.Analyze(kPersonalMessage, "erika", "INBOX", 1, "erika@example.com");

    REQUIRE_EQ(m.senderAddr, std::string("jonas@partner.example"));
    REQUIRE_EQ(m.senderName, std::string("Jonas Meyer"));
    REQUIRE_EQ(m.senderDomain, std::string("partner.example"));
    REQUIRE_EQ(m.subject, std::string("Lunch on Thursday?"));
    REQUIRE_EQ(m.date, MakeUtcTime(2026, 8, 14, 9, 30, 0));
    REQUIRE(m.sizeBytes > 0);
    REQUIRE(!m.automated);
    REQUIRE(m.category == MessageCategory::Personal);
    REQUIRE_EQ(m.attachmentCount, 0);
}

TEST(Ingest_AnalyzeFlagsSpamThroughHtmlAndLeetSpelling) {
    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);

    const AnalyzedMessage m =
        ingestor.Analyze(kSpamMessage, "erika", "INBOX", 2, "erika@example.com");

    REQUIRE(m.category == MessageCategory::ProductSpam);
    REQUIRE(m.score > 50.0);
    REQUIRE(m.automated);            // Precedence: bulk
    REQUIRE(!m.hits.empty());
    REQUIRE_EQ(m.senderDomain, std::string("bulk.example"));
}

TEST(Ingest_AnalyzeRecognisesBulkHeadersAsNewsletter) {
    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);

    const AnalyzedMessage m =
        ingestor.Analyze(kNewsletterMessage, "erika", "INBOX", 3, "erika@example.com");

    REQUIRE(m.automated);            // List-Unsubscribe
    REQUIRE(m.category == MessageCategory::Newsletter);
    // The +0200 offset must be applied, not ignored.
    REQUIRE_EQ(m.date, MakeUtcTime(2026, 8, 16, 6, 0, 0));
}

TEST(Ingest_AnalyzeIndexesAttachmentsAndSpotsRiskyOnes) {
    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);

    const AnalyzedMessage m =
        ingestor.Analyze(kMalwareMessage, "erika", "INBOX", 4, "erika@example.com");

    REQUIRE_EQ(m.attachmentCount, 1);
    REQUIRE(m.attachmentBytes > 0);
    REQUIRE_EQ(m.attachments.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(m.attachments[0].filename, std::string("invoice_4451.pdf.exe"));
    REQUIRE(m.attachments[0].risky);
    REQUIRE(m.category == MessageCategory::MalwareRisk);
}

TEST(Ingest_AnalyzeSurvivesGarbageInput) {
    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);

    const AnalyzedMessage empty = ingestor.Analyze("", "erika", "INBOX", 1);
    REQUIRE_EQ(empty.uid, 1);
    REQUIRE(empty.senderAddr.empty());

    const AnalyzedMessage junk =
        ingestor.Analyze("this is not a message at all", "erika", "INBOX", 2);
    REQUIRE_EQ(junk.uid, 2);
    REQUIRE_EQ(junk.date, 0);        // no Date: header -> undated, not 1970
}

TEST(Ingest_RawWritesTheRowAndItsEvidence) {
    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);

    IngestOptions options;
    options.ownerAddress = "erika@example.com";
    AnalyzedMessage stored;
    REQUIRE(ingestor.IngestRaw(kSpamMessage, "erika", "INBOX", 2, options, &stored));

    std::vector<AnalyzedMessage> messages;
    REQUIRE(store.ListMessages(MessageFilter{}, messages));
    REQUIRE_EQ(messages.size(), static_cast<std::size_t>(1));
    REQUIRE(messages[0].category == MessageCategory::ProductSpam);

    std::vector<KeywordHit> hits;
    REQUIRE(store.GetHits("erika", "INBOX", 2, hits));
    REQUIRE(!hits.empty());
}

TEST(Ingest_StoreHitsCanBeTurnedOff) {
    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);

    IngestOptions options;
    options.storeHits = false;
    REQUIRE(ingestor.IngestRaw(kSpamMessage, "erika", "INBOX", 2, options));

    std::vector<KeywordHit> hits;
    REQUIRE(store.GetHits("erika", "INBOX", 2, hits));
    REQUIRE(hits.empty());

    // The verdict itself is still stored — only the evidence list is skipped.
    std::vector<AnalyzedMessage> messages;
    REQUIRE(store.ListMessages(MessageFilter{}, messages));
    REQUIRE(messages[0].category == MessageCategory::ProductSpam);
}

TEST(UidFromFileName_ReadsUltraMailCacheNames) {
    REQUIRE_EQ(UidFromFileName("1234.eml"), 1234);
    REQUIRE_EQ(UidFromFileName("7.eml"), 7);
    REQUIRE_EQ(UidFromFileName("notanumber.eml"), 0);
    REQUIRE_EQ(UidFromFileName(".eml"), 0);
    REQUIRE_EQ(UidFromFileName(""), 0);
}

TEST(Ingest_FolderDirectoryLoadsEveryMessage) {
    TempDir dir;
    dir.Write("1.eml", kPersonalMessage);
    dir.Write("2.eml", kSpamMessage);
    dir.Write("3.eml", kNewsletterMessage);
    dir.Write("notes.txt", "ignored, not an .eml file");

    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);

    IngestOptions options;
    options.ownerAddress = "erika@example.com";
    const IngestStats stats =
        ingestor.IngestFolderDirectory(dir.String(), "erika", "INBOX", options);

    REQUIRE_EQ(stats.filesSeen, 3);
    REQUIRE_EQ(stats.analysed, 3);
    REQUIRE_EQ(stats.stored, 3);
    REQUIRE_EQ(stats.failed, 0);
    REQUIRE_EQ(stats.unwanted, 1);

    std::vector<AnalyzedMessage> messages;
    REQUIRE(store.ListMessages(MessageFilter{}, messages));
    REQUIRE_EQ(messages.size(), static_cast<std::size_t>(3));

    // The ingest state records where the folder got to.
    std::vector<IngestState> state;
    REQUIRE(store.ListIngestState("erika", state));
    REQUIRE_EQ(state.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(state[0].folder, std::string("INBOX"));
    REQUIRE_EQ(state[0].lastUid, 3);
}

TEST(Ingest_SecondPassSkipsWhatIsAlreadyThere) {
    TempDir dir;
    dir.Write("1.eml", kPersonalMessage);
    dir.Write("2.eml", kSpamMessage);

    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);
    IngestOptions options;
    options.ownerAddress = "erika@example.com";

    ingestor.IngestFolderDirectory(dir.String(), "erika", "INBOX", options);

    dir.Write("3.eml", kNewsletterMessage);
    const IngestStats second =
        ingestor.IngestFolderDirectory(dir.String(), "erika", "INBOX", options);

    REQUIRE_EQ(second.filesSeen, 3);
    REQUIRE_EQ(second.skipped, 2);
    REQUIRE_EQ(second.analysed, 1);
    REQUIRE_EQ(second.stored, 1);

    // Turning the skip off re-analyses everything (the rule-change path).
    options.skipExisting = false;
    const IngestStats third =
        ingestor.IngestFolderDirectory(dir.String(), "erika", "INBOX", options);
    REQUIRE_EQ(third.skipped, 0);
    REQUIRE_EQ(third.analysed, 3);

    std::vector<AnalyzedMessage> messages;
    REQUIRE(store.ListMessages(MessageFilter{}, messages));
    REQUIRE_EQ(messages.size(), static_cast<std::size_t>(3));   // still no duplicates
}

TEST(Ingest_MaxMessagesStopsEarly) {
    TempDir dir;
    dir.Write("1.eml", kPersonalMessage);
    dir.Write("2.eml", kSpamMessage);
    dir.Write("3.eml", kNewsletterMessage);

    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);
    IngestOptions options;
    options.maxMessages = 2;

    const IngestStats stats =
        ingestor.IngestFolderDirectory(dir.String(), "erika", "INBOX", options);
    REQUIRE_EQ(stats.analysed, 2);
    REQUIRE_EQ(stats.stored, 2);
}

TEST(Ingest_MailCacheWalksEveryFolderOfAnAccount) {
    // UltraMail's layout: <cache>/<accountId>/<folder>/<uid>.eml
    TempDir cache;
    cache.Write("erika/INBOX/1.eml", kPersonalMessage);
    cache.Write("erika/INBOX/2.eml", kSpamMessage);
    cache.Write("erika/Archive/5.eml", kNewsletterMessage);
    cache.Write("jonas/INBOX/1.eml", kPersonalMessage);   // another account

    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);
    IngestOptions options;
    options.ownerAddress = "erika@example.com";

    const IngestStats stats = ingestor.IngestMailCache(cache.String(), "erika", options);
    REQUIRE_EQ(stats.analysed, 3);
    REQUIRE_EQ(stats.stored, 3);

    MessageFilter archive;
    archive.folder = "Archive";
    std::vector<AnalyzedMessage> messages;
    REQUIRE(store.ListMessages(archive, messages));
    REQUIRE_EQ(messages.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(messages[0].uid, 5);

    // The other account's mail was not touched.
    MessageFilter jonas;
    jonas.accountId = "jonas";
    REQUIRE(store.ListMessages(jonas, messages));
    REQUIRE(messages.empty());
}

TEST(Ingest_MissingDirectoriesAreNotAnError) {
    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);
    IngestOptions options;

    const IngestStats folder =
        ingestor.IngestFolderDirectory("/no/such/place", "erika", "INBOX", options);
    REQUIRE_EQ(folder.filesSeen, 0);
    REQUIRE_EQ(folder.failed, 0);

    const IngestStats cache = ingestor.IngestMailCache("/no/such/place", "erika", options);
    REQUIRE_EQ(cache.analysed, 0);
}

TEST(Ingest_ProgressCallbackReportsEveryMessage) {
    TempDir dir;
    dir.Write("1.eml", kPersonalMessage);
    dir.Write("2.eml", kSpamMessage);

    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);

    int calls = 0;
    std::string lastFolder;
    ingestor.onProgress = [&calls, &lastFolder](int done, const std::string& folder) {
        ++calls;
        lastFolder = folder;
        REQUIRE_EQ(done, calls);
    };

    ingestor.IngestFolderDirectory(dir.String(), "erika", "INBOX", IngestOptions{});
    REQUIRE_EQ(calls, 2);
    REQUIRE_EQ(lastFolder, std::string("INBOX"));
}

TEST(Ingest_ReClassifiesWithAChangedRuleSet) {
    AnalysisStore store = OpenStore();
    Ingestor ingestor(store);

    // A neutral message under a rule set that says nothing about it...
    RuleSet empty;
    empty.AddTerm(MessageCategory::ProductSpam, "flurgle", 5.0);
    ingestor.SetClassifier(Classifier(empty));
    REQUIRE(ingestor.IngestRaw(kSpamMessage, "erika", "INBOX", 2, IngestOptions{}));

    std::vector<AnalyzedMessage> messages;
    REQUIRE(store.ListMessages(MessageFilter{}, messages));
    REQUIRE(messages[0].category != MessageCategory::ProductSpam);

    // ...becomes spam once the built-in rules are back.
    ingestor.SetClassifier(Classifier());
    IngestOptions rescan;
    rescan.skipExisting = false;
    REQUIRE(ingestor.IngestRaw(kSpamMessage, "erika", "INBOX", 2, rescan));

    REQUIRE(store.ListMessages(MessageFilter{}, messages));
    REQUIRE_EQ(messages.size(), static_cast<std::size_t>(1));
    REQUIRE(messages[0].category == MessageCategory::ProductSpam);
}
