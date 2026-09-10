// Tests/EmailCleaner/test_store.cpp
// The analysis database: schema, writes, filters and the aggregate queries the
// map view, the timetable and the timeline are built on.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerStore.h"

#include <UltraDatabase/UltraDatabase.h>

#include <string>

using namespace EmailCleaner;

namespace {

// Each test gets its own in-memory connection so they cannot see each other.
int NextConnectionId() {
    static int counter = 0;
    return ++counter;
}

AnalysisStore OpenStore() {
    AnalysisStore store;
    const std::string name = "ec_test_" + std::to_string(NextConnectionId());
    const UltraDbResult r = store.Open(name, ":memory:");
    if (!r) throw emailcleaner_test::Failure{ "store.Open failed: " + r.message };
    return store;
}

AnalyzedMessage MakeMessage(const std::string& sender, int64_t uid, int64_t date,
                            MessageCategory category = MessageCategory::Personal,
                            const std::string& folder = "INBOX") {
    AnalyzedMessage m;
    m.accountId    = "erika";
    m.folder       = folder;
    m.uid          = uid;
    m.messageId    = "<" + std::to_string(uid) + "@example>";
    m.senderAddr   = sender;
    m.senderName   = "Sender " + sender;
    m.senderDomain = DomainOf(sender);
    m.subject      = "Subject " + std::to_string(uid);
    m.date         = date;
    m.sizeBytes    = 1000 + uid;
    m.category     = category;
    m.score        = IsUnwanted(category) ? 80.0 : 0.0;
    return m;
}

const int64_t kDay = 86400;

} // namespace

TEST(Store_OpenIsIdempotentAndMigrates) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.IsOpen());

    int version = 0;
    REQUIRE(UltraDb_GetSchemaVersion(store.Connection(), version));
    REQUIRE_EQ(version, AnalysisStore::kSchemaVersion);

    // Re-opening the same connection must keep what is already there. This is
    // the property that matters: re-registering the name would drop the
    // physical connection, and an in-memory database with it.
    REQUIRE(store.UpsertMessage(MakeMessage("a@x.example", 1, kDay)));
    REQUIRE(store.Open(store.Connection(), ":memory:"));
    REQUIRE(UltraDb_GetSchemaVersion(store.Connection(), version));
    REQUIRE_EQ(version, AnalysisStore::kSchemaVersion);

    std::vector<AnalyzedMessage> messages;
    REQUIRE(store.ListMessages(MessageFilter{}, messages));
    REQUIRE_EQ(messages.size(), static_cast<std::size_t>(1));
}

TEST(Store_AccountsRoundTrip) {
    AnalysisStore store = OpenStore();
    StoredAccount account{ "erika", "Erika Example", "erika@example.com", "erika" };
    REQUIRE(store.UpsertAccount(account));

    std::vector<StoredAccount> accounts;
    REQUIRE(store.ListAccounts(accounts));
    REQUIRE_EQ(accounts.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(accounts[0].email, std::string("erika@example.com"));

    // Upserting again updates rather than duplicating.
    account.shortName = "e";
    REQUIRE(store.UpsertAccount(account));
    REQUIRE(store.ListAccounts(accounts));
    REQUIRE_EQ(accounts.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(accounts[0].shortName, std::string("e"));

    // An account without an id is rejected rather than written as "".
    REQUIRE(!store.UpsertAccount(StoredAccount{}));
}

TEST(Store_MessageUpsertReplacesDerivedRows) {
    AnalysisStore store = OpenStore();

    AnalyzedMessage m = MakeMessage("spam@bad.example", 1, kDay * 100,
                                    MessageCategory::ProductSpam);
    m.attachments.push_back(AttachmentRecord{ "a.pdf", "application/pdf", 100, false, false });
    m.hits.push_back(KeywordHit{ MessageCategory::ProductSpam, MatchField::Subject,
                                 "cheap pills", 3.0 });
    REQUIRE(store.UpsertMessage(m));
    REQUIRE(store.HasMessage("erika", "INBOX", 1));

    std::vector<AttachmentRecord> attachments;
    REQUIRE(store.GetAttachments("erika", "INBOX", 1, attachments));
    REQUIRE_EQ(attachments.size(), static_cast<std::size_t>(1));

    // Re-analysing the same message must replace its evidence, not append to it.
    m.attachments.clear();
    m.hits.clear();
    m.hits.push_back(KeywordHit{ MessageCategory::AdultContent, MatchField::Body, "xxx", 2.5 });
    m.category = MessageCategory::AdultContent;
    REQUIRE(store.UpsertMessage(m));

    REQUIRE(store.GetAttachments("erika", "INBOX", 1, attachments));
    REQUIRE(attachments.empty());

    std::vector<KeywordHit> hits;
    REQUIRE(store.GetHits("erika", "INBOX", 1, hits));
    REQUIRE_EQ(hits.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(hits[0].term, std::string("xxx"));

    std::vector<AnalyzedMessage> messages;
    REQUIRE(store.ListMessages(MessageFilter{}, messages));
    REQUIRE_EQ(messages.size(), static_cast<std::size_t>(1));
    REQUIRE(messages[0].category == MessageCategory::AdultContent);
}

TEST(Store_RejectsMessageWithoutKeyAndLeavesNothingBehind) {
    AnalysisStore store = OpenStore();
    AnalyzedMessage broken = MakeMessage("a@b.example", 1, kDay);
    broken.folder.clear();
    REQUIRE(!store.UpsertMessage(broken));

    std::vector<AnalyzedMessage> messages;
    REQUIRE(store.ListMessages(MessageFilter{}, messages));
    REQUIRE(messages.empty());
}

TEST(Store_BulkUpsertIsAtomic) {
    AnalysisStore store = OpenStore();
    std::vector<AnalyzedMessage> batch = {
        MakeMessage("a@x.example", 1, kDay),
        MakeMessage("b@x.example", 2, kDay * 2),
    };
    batch[1].accountId.clear();   // makes the batch invalid halfway through

    REQUIRE(!store.UpsertMessages(batch));

    std::vector<AnalyzedMessage> messages;
    REQUIRE(store.ListMessages(MessageFilter{}, messages));
    REQUIRE(messages.empty());   // the good row was rolled back with the bad one
}

TEST(Store_ListMessagesHonoursFilters) {
    AnalysisStore store = OpenStore();
    std::vector<AnalyzedMessage> batch = {
        MakeMessage("friend@partner.example", 1, kDay * 10, MessageCategory::Personal),
        MakeMessage("spam@bad.example", 2, kDay * 20, MessageCategory::ProductSpam),
        MakeMessage("spam@bad.example", 3, kDay * 30, MessageCategory::AdultContent),
        MakeMessage("news@list.example", 4, kDay * 40, MessageCategory::Newsletter, "Archive"),
    };
    batch[2].attachmentCount = 2;
    batch[2].attachmentBytes = 4096;
    REQUIRE(store.UpsertMessages(batch));

    std::vector<AnalyzedMessage> got;

    MessageFilter bySender;
    bySender.senderAddr = "SPAM@BAD.EXAMPLE";      // matching is case-insensitive
    REQUIRE(store.ListMessages(bySender, got));
    REQUIRE_EQ(got.size(), static_cast<std::size_t>(2));
    REQUIRE_EQ(got[0].uid, 3);                     // most recent first

    MessageFilter byDomain;
    byDomain.senderDomain = "bad.example";
    REQUIRE(store.ListMessages(byDomain, got));
    REQUIRE_EQ(got.size(), static_cast<std::size_t>(2));

    MessageFilter byFolder;
    byFolder.folder = "Archive";
    REQUIRE(store.ListMessages(byFolder, got));
    REQUIRE_EQ(got.size(), static_cast<std::size_t>(1));

    MessageFilter unwanted;
    unwanted.unwantedOnly = true;
    REQUIRE(store.ListMessages(unwanted, got));
    REQUIRE_EQ(got.size(), static_cast<std::size_t>(2));

    MessageFilter category;
    category.category = MessageCategory::Newsletter;
    category.categorySet = true;
    REQUIRE(store.ListMessages(category, got));
    REQUIRE_EQ(got.size(), static_cast<std::size_t>(1));

    MessageFilter withAttachments;
    withAttachments.withAttachmentsOnly = true;
    REQUIRE(store.ListMessages(withAttachments, got));
    REQUIRE_EQ(got.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(got[0].uid, 3);

    MessageFilter window;
    window.since = kDay * 20;
    window.until = kDay * 40;
    REQUIRE(store.ListMessages(window, got));
    REQUIRE_EQ(got.size(), static_cast<std::size_t>(2));   // uid 2 and 3, not 4

    MessageFilter limited;
    limited.limit = 1;
    REQUIRE(store.ListMessages(limited, got));
    REQUIRE_EQ(got.size(), static_cast<std::size_t>(1));
}

TEST(Store_SearchMatchesSubjectAndSenderCaseInsensitively) {
    AnalysisStore store = OpenStore();
    AnalyzedMessage m = MakeMessage("Marketing@Shop.Example", 1, kDay);
    m.senderAddr = "marketing@shop.example";
    m.subject    = "Your SPECIAL Offer";
    REQUIRE(store.UpsertMessage(m));

    std::vector<AnalyzedMessage> got;
    MessageFilter bySubject;
    bySubject.search = "special";
    REQUIRE(store.ListMessages(bySubject, got));
    REQUIRE_EQ(got.size(), static_cast<std::size_t>(1));

    MessageFilter bySender;
    bySender.search = "SHOP.EXAMPLE";
    REQUIRE(store.ListMessages(bySender, got));
    REQUIRE_EQ(got.size(), static_cast<std::size_t>(1));

    MessageFilter noMatch;
    noMatch.search = "nothing here";
    REQUIRE(store.ListMessages(noMatch, got));
    REQUIRE(got.empty());

    // A wildcard in the needle is a literal, not a LIKE pattern.
    MessageFilter wildcard;
    wildcard.search = "%";
    REQUIRE(store.ListMessages(wildcard, got));
    REQUIRE(got.empty());
}

TEST(Store_SenderBlocksRollUpTheMapView) {
    AnalysisStore store = OpenStore();
    std::vector<AnalyzedMessage> batch;
    // Three spam messages from one sender, one personal from another.
    for (int i = 0; i < 3; ++i) {
        AnalyzedMessage m = MakeMessage("spam@bad.example", i + 1, kDay * (10 + i),
                                        MessageCategory::ProductSpam);
        m.attachmentCount = 1;
        m.attachmentBytes = 500;
        batch.push_back(m);
    }
    batch.push_back(MakeMessage("friend@partner.example", 9, kDay * 50,
                                MessageCategory::Personal));
    REQUIRE(store.UpsertMessages(batch));

    std::vector<SenderBlock> blocks;
    REQUIRE(store.ListSenderBlocks(MessageFilter{}, SenderMetric::MessageCount, 0, blocks));
    REQUIRE_EQ(blocks.size(), static_cast<std::size_t>(2));

    const SenderBlock& top = blocks[0];
    REQUIRE_EQ(top.senderAddr, std::string("spam@bad.example"));
    REQUIRE_EQ(top.messageCount, 3);
    REQUIRE_EQ(top.unwantedCount, 3);
    REQUIRE_EQ(top.attachmentCount, 3);
    REQUIRE_EQ(top.attachmentBytes, 1500);
    REQUIRE_EQ(top.domain, std::string("bad.example"));
    REQUIRE(top.topCategory == MessageCategory::ProductSpam);
    REQUIRE_EQ(top.firstSeen, kDay * 10);
    REQUIRE_EQ(top.lastSeen, kDay * 12);
    REQUIRE(top.UnwantedRatio() > 0.99);
    REQUIRE(!top.displayName.empty());

    // The limit keeps the largest blocks.
    REQUIRE(store.ListSenderBlocks(MessageFilter{}, SenderMetric::MessageCount, 1, blocks));
    REQUIRE_EQ(blocks.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(blocks[0].senderAddr, std::string("spam@bad.example"));
}

TEST(Store_DomainBlocksAggregateAcrossSenders) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessages({
        MakeMessage("one@shared.example", 1, kDay * 10, MessageCategory::ProductSpam),
        MakeMessage("two@shared.example", 2, kDay * 11, MessageCategory::ProductSpam),
        MakeMessage("solo@other.example", 3, kDay * 12, MessageCategory::Personal),
    }));

    std::vector<SenderBlock> blocks;
    REQUIRE(store.ListDomainBlocks(MessageFilter{}, SenderMetric::MessageCount, 0, blocks));
    REQUIRE_EQ(blocks.size(), static_cast<std::size_t>(2));
    REQUIRE_EQ(blocks[0].domain, std::string("shared.example"));
    REQUIRE_EQ(blocks[0].messageCount, 2);
    REQUIRE(blocks[0].topCategory == MessageCategory::ProductSpam);
}

TEST(Store_SenderBlocksRespectTheFilter) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessages({
        MakeMessage("spam@bad.example", 1, kDay * 10, MessageCategory::ProductSpam),
        MakeMessage("friend@partner.example", 2, kDay * 11, MessageCategory::Personal),
    }));

    MessageFilter unwanted;
    unwanted.unwantedOnly = true;
    std::vector<SenderBlock> blocks;
    REQUIRE(store.ListSenderBlocks(unwanted, SenderMetric::MessageCount, 0, blocks));
    REQUIRE_EQ(blocks.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(blocks[0].senderAddr, std::string("spam@bad.example"));
}

TEST(Store_TimetableBucketsByUtcWeekdayAndHour) {
    AnalysisStore store = OpenStore();
    // 14 Aug 2026 is a Friday (weekday index 4).
    const int64_t friday0930 = MakeUtcTime(2026, 8, 14, 9, 30, 0);
    const int64_t monday1700 = MakeUtcTime(2026, 8, 17, 17, 5, 0);

    std::vector<AnalyzedMessage> batch = {
        MakeMessage("a@x.example", 1, friday0930),
        MakeMessage("a@x.example", 2, friday0930 + 600),   // same hour
        MakeMessage("a@x.example", 3, monday1700),
        MakeMessage("a@x.example", 4, 0),                  // undated: excluded
    };
    REQUIRE(store.UpsertMessages(batch));

    Timetable table;
    REQUIRE(store.GetTimetable(MessageFilter{}, table));
    REQUIRE_EQ(table.total, 3);
    REQUIRE_EQ(table.At(4, 9), 2);
    REQUIRE_EQ(table.At(0, 17), 1);
    REQUIRE_EQ(table.peakDay, 4);
    REQUIRE_EQ(table.peakHour, 9);

    // ... and honours a filter, which is how the detail view scopes it.
    MessageFilter oneSender;
    oneSender.senderAddr = "nobody@x.example";
    REQUIRE(store.GetTimetable(oneSender, table));
    REQUIRE_EQ(table.total, 0);
}

TEST(Store_TimelineFillsEmptyBuckets) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessages({
        MakeMessage("a@x.example", 1, MakeUtcTime(2026, 1, 10, 12, 0, 0)),
        MakeMessage("a@x.example", 2, MakeUtcTime(2026, 4, 2, 12, 0, 0),
                    MessageCategory::ProductSpam),
        MakeMessage("a@x.example", 3, MakeUtcTime(2026, 4, 20, 12, 0, 0)),
    }));

    std::vector<TimelinePoint> points;
    REQUIRE(store.GetTimeline(MessageFilter{}, TimeBucket::Month, points));
    // January through April inclusive, with February and March at zero.
    REQUIRE_EQ(points.size(), static_cast<std::size_t>(4));
    REQUIRE_EQ(points[0].label, std::string("Jan 2026"));
    REQUIRE_EQ(points[0].messageCount, 1);
    REQUIRE_EQ(points[1].messageCount, 0);
    REQUIRE_EQ(points[2].messageCount, 0);
    REQUIRE_EQ(points[3].label, std::string("Apr 2026"));
    REQUIRE_EQ(points[3].messageCount, 2);
    REQUIRE_EQ(points[3].unwantedCount, 1);
}

TEST(Store_TimelineIsEmptyWithoutDatedMessages) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessage(MakeMessage("a@x.example", 1, 0)));

    std::vector<TimelinePoint> points;
    REQUIRE(store.GetTimeline(MessageFilter{}, TimeBucket::Day, points));
    REQUIRE(points.empty());
}

TEST(Store_CategoryTotalsAndOverview) {
    AnalysisStore store = OpenStore();
    std::vector<AnalyzedMessage> batch = {
        MakeMessage("a@x.example", 1, kDay * 10, MessageCategory::Personal),
        MakeMessage("b@y.example", 2, kDay * 20, MessageCategory::ProductSpam),
        MakeMessage("b@y.example", 3, kDay * 30, MessageCategory::ProductSpam),
    };
    batch[1].attachmentCount = 1;
    batch[1].attachmentBytes = 2048;
    REQUIRE(store.UpsertMessages(batch));

    std::vector<CategoryTotal> totals;
    REQUIRE(store.GetCategoryTotals(MessageFilter{}, totals));
    REQUIRE_EQ(totals.size(), static_cast<std::size_t>(2));
    REQUIRE(totals[0].category == MessageCategory::ProductSpam);   // ordered by count
    REQUIRE_EQ(totals[0].messageCount, 2);

    StoreOverview overview;
    REQUIRE(store.GetOverview(MessageFilter{}, overview));
    REQUIRE_EQ(overview.messages, 3);
    REQUIRE_EQ(overview.senders, 2);
    REQUIRE_EQ(overview.accounts, 1);
    REQUIRE_EQ(overview.unwanted, 2);
    REQUIRE_EQ(overview.attachments, 1);
    REQUIRE_EQ(overview.attachmentBytes, 2048);
    REQUIRE_EQ(overview.firstDate, kDay * 10);
    REQUIRE_EQ(overview.lastDate, kDay * 30);
}

TEST(Store_OverviewOnAnEmptyDatabase) {
    AnalysisStore store = OpenStore();
    StoreOverview overview;
    REQUIRE(store.GetOverview(MessageFilter{}, overview));
    REQUIRE_EQ(overview.messages, 0);
    REQUIRE_EQ(overview.senders, 0);
    REQUIRE_EQ(overview.firstDate, 0);
}

TEST(Store_AttachmentTypeTotals) {
    AnalysisStore store = OpenStore();
    AnalyzedMessage m = MakeMessage("a@x.example", 1, kDay);
    m.attachments = {
        AttachmentRecord{ "a.pdf", "application/pdf", 1000, false, false },
        AttachmentRecord{ "b.pdf", "application/pdf", 2000, false, false },
        AttachmentRecord{ "c.exe", "application/x-msdownload", 5000, false, true },
    };
    REQUIRE(store.UpsertMessage(m));

    std::vector<AttachmentTypeTotal> totals;
    REQUIRE(store.GetAttachmentTypeTotals(MessageFilter{}, totals));
    REQUIRE_EQ(totals.size(), static_cast<std::size_t>(2));
    REQUIRE_EQ(totals[0].mediaType, std::string("application/x-msdownload"));
    REQUIRE_EQ(totals[0].riskyCount, 1);
    REQUIRE_EQ(totals[1].count, 2);
    REQUIRE_EQ(totals[1].totalBytes, 3000);
}

TEST(Store_TopKeywordsRanksByFrequency) {
    AnalysisStore store = OpenStore();
    std::vector<AnalyzedMessage> batch;
    for (int i = 0; i < 3; ++i) {
        AnalyzedMessage m = MakeMessage("spam@bad.example", i + 1, kDay * (i + 1),
                                        MessageCategory::ProductSpam);
        m.hits.push_back(KeywordHit{ MessageCategory::ProductSpam, MatchField::Any,
                                     "cheap pills", 3.0 });
        if (i == 0)
            m.hits.push_back(KeywordHit{ MessageCategory::ProductSpam, MatchField::Any,
                                         "act now", 1.5 });
        batch.push_back(m);
    }
    REQUIRE(store.UpsertMessages(batch));

    std::vector<KeywordHit> top;
    REQUIRE(store.GetTopKeywords(MessageFilter{}, 10, top));
    REQUIRE_EQ(top.size(), static_cast<std::size_t>(2));
    REQUIRE_EQ(top[0].term, std::string("cheap pills"));
    REQUIRE_EQ(top[0].weight, 3.0);   // fired in three messages
    REQUIRE_EQ(top[1].term, std::string("act now"));
}

TEST(Store_ClearMessagesScopesToOneAccount) {
    AnalysisStore store = OpenStore();
    AnalyzedMessage mine = MakeMessage("a@x.example", 1, kDay);
    AnalyzedMessage other = MakeMessage("b@y.example", 2, kDay * 2);
    other.accountId = "jonas";
    REQUIRE(store.UpsertMessages({ mine, other }));

    REQUIRE(store.ClearMessages("erika"));

    std::vector<AnalyzedMessage> got;
    REQUIRE(store.ListMessages(MessageFilter{}, got));
    REQUIRE_EQ(got.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(got[0].accountId, std::string("jonas"));

    REQUIRE(store.ClearMessages(""));
    REQUIRE(store.ListMessages(MessageFilter{}, got));
    REQUIRE(got.empty());
}

TEST(Store_RemoveAccountTakesItsAnalysisWithIt) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertAccount(StoredAccount{ "erika", "Erika", "erika@example.com", "e" }));
    AnalyzedMessage m = MakeMessage("a@x.example", 1, kDay);
    m.hits.push_back(KeywordHit{ MessageCategory::ProductSpam, MatchField::Any, "x", 1.0 });
    REQUIRE(store.UpsertMessage(m));

    REQUIRE(store.RemoveAccount("erika"));

    std::vector<StoredAccount> accounts;
    REQUIRE(store.ListAccounts(accounts));
    REQUIRE(accounts.empty());

    std::vector<AnalyzedMessage> messages;
    REQUIRE(store.ListMessages(MessageFilter{}, messages));
    REQUIRE(messages.empty());

    std::vector<KeywordHit> hits;
    REQUIRE(store.GetHits("erika", "INBOX", 1, hits));
    REQUIRE(hits.empty());
}

TEST(Store_IngestStateTracksProgress) {
    AnalysisStore store = OpenStore();
    IngestState state{ "erika", "INBOX", 42, 1000, 7 };
    REQUIRE(store.UpsertIngestState(state));

    state.lastUid = 99;
    state.messages = 12;
    REQUIRE(store.UpsertIngestState(state));

    std::vector<IngestState> all;
    REQUIRE(store.ListIngestState("erika", all));
    REQUIRE_EQ(all.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(all[0].lastUid, 99);
    REQUIRE_EQ(all[0].messages, 12);

    REQUIRE(store.UpsertMessages({
        MakeMessage("a@x.example", 5, kDay),
        MakeMessage("a@x.example", 17, kDay * 2),
    }));
    int64_t lastUid = 0;
    REQUIRE(store.GetLastUid("erika", "INBOX", lastUid));
    REQUIRE_EQ(lastUid, 17);

    REQUIRE(store.GetLastUid("erika", "Nowhere", lastUid));
    REQUIRE_EQ(lastUid, 0);
}
