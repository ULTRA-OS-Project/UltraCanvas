// Tests/EmailCleaner/test_analytics.cpp
// The shaping between the store and the views: the sender map hierarchy, the
// "Other" pooling, bucket choice, the palette and the summary strings.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerAnalytics.h"

#include <string>

using namespace EmailCleaner;

namespace {

int NextConnectionId() {
    static int counter = 0;
    return ++counter;
}

AnalysisStore OpenStore() {
    AnalysisStore store;
    const std::string name = "ec_analytics_" + std::to_string(NextConnectionId());
    const UltraDbResult r = store.Open(name, ":memory:");
    if (!r) throw emailcleaner_test::Failure{ "store.Open failed: " + r.message };
    return store;
}

AnalyzedMessage Message(const std::string& sender, int64_t uid, int64_t date,
                        MessageCategory category, int64_t size = 1000) {
    AnalyzedMessage m;
    m.accountId    = "erika";
    m.folder       = "INBOX";
    m.uid          = uid;
    m.senderAddr   = sender;
    m.senderName   = sender;
    m.senderDomain = DomainOf(sender);
    m.subject      = "Subject";
    m.date         = date;
    m.sizeBytes    = size;
    m.category     = category;
    m.score        = IsUnwanted(category) ? 75.0 : 0.0;
    return m;
}

const MapNode* FindChild(const MapNode& parent, const std::string& key) {
    for (const MapNode& child : parent.children) {
        if (child.key == key) return &child;
    }
    return nullptr;
}

const int64_t kDay = 86400;

} // namespace

TEST(Analytics_SenderMapGroupsSendersUnderTheirDomain) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessages({
        Message("a@shared.example", 1, kDay * 10, MessageCategory::ProductSpam),
        Message("a@shared.example", 2, kDay * 11, MessageCategory::ProductSpam),
        Message("b@shared.example", 3, kDay * 12, MessageCategory::Newsletter),
        Message("c@other.example", 4, kDay * 13, MessageCategory::Personal),
    }));

    Analytics analytics(store);
    MapNode root;
    REQUIRE(analytics.BuildSenderMap(MessageFilter{}, SenderMetric::MessageCount,
                                     MapShape{}, root));

    REQUIRE_EQ(root.children.size(), static_cast<std::size_t>(2));
    REQUIRE_EQ(root.value, 4.0);

    const MapNode* shared = FindChild(root, "shared.example");
    REQUIRE(shared != nullptr);
    REQUIRE_EQ(shared->value, 3.0);
    REQUIRE_EQ(shared->children.size(), static_cast<std::size_t>(2));
    REQUIRE(shared->category == MessageCategory::ProductSpam);   // 2 of 3
    REQUIRE_EQ(shared->block.messageCount, 3);

    // The domain sorts ahead of the smaller one.
    REQUIRE_EQ(root.children[0].key, std::string("shared.example"));

    // Leaves carry the sender's own block and a tooltip.
    const MapNode& leaf = shared->children[0];
    REQUIRE(leaf.IsLeaf());
    REQUIRE_EQ(leaf.key, std::string("a@shared.example"));
    REQUIRE_EQ(leaf.value, 2.0);
    REQUIRE(!leaf.tooltip.empty());
}

TEST(Analytics_SenderMapCanStayFlat) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessages({
        Message("a@one.example", 1, kDay, MessageCategory::Personal),
        Message("b@two.example", 2, kDay, MessageCategory::Personal),
    }));

    Analytics analytics(store);
    MapShape shape;
    shape.groupByDomain = false;

    MapNode root;
    REQUIRE(analytics.BuildSenderMap(MessageFilter{}, SenderMetric::MessageCount,
                                     shape, root));
    REQUIRE_EQ(root.children.size(), static_cast<std::size_t>(2));
    for (const MapNode& child : root.children) REQUIRE(child.IsLeaf());
}

TEST(Analytics_SmallDomainsArePooledIntoOther) {
    AnalysisStore store = OpenStore();
    std::vector<AnalyzedMessage> batch;
    int uid = 1;
    // One dominant domain...
    for (int i = 0; i < 50; ++i)
        batch.push_back(Message("bulk@big.example", uid++, kDay * (i + 1),
                                MessageCategory::ProductSpam));
    // ...and a long tail of one-message domains, each well under minShare.
    for (int i = 0; i < 12; ++i) {
        batch.push_back(Message("someone@tail" + std::to_string(i) + ".example",
                                uid++, kDay * (i + 1), MessageCategory::Personal));
    }
    REQUIRE(store.UpsertMessages(batch));

    Analytics analytics(store);
    MapShape shape;
    shape.minShare = 0.05;      // 1 of 62 messages is well under 5%

    MapNode root;
    REQUIRE(analytics.BuildSenderMap(MessageFilter{}, SenderMetric::MessageCount,
                                     shape, root));

    const MapNode* other = FindChild(root, "other");
    REQUIRE(other != nullptr);
    REQUIRE(other->isAggregate);
    REQUIRE_EQ(other->value, 12.0);
    REQUIRE_EQ(other->block.messageCount, 12);
    REQUIRE(other->label.find("12 domains") != std::string::npos);

    // Everything is still accounted for.
    double total = 0.0;
    for (const MapNode& child : root.children) total += child.value;
    REQUIRE_EQ(total, 62.0);
    REQUIRE_EQ(root.value, 62.0);
}

TEST(Analytics_DomainCapPoolsTheRest) {
    AnalysisStore store = OpenStore();
    std::vector<AnalyzedMessage> batch;
    int uid = 1;
    for (int d = 0; d < 6; ++d) {
        for (int i = 0; i < 10; ++i) {
            batch.push_back(Message("s@d" + std::to_string(d) + ".example", uid++,
                                    kDay * uid, MessageCategory::Personal));
        }
    }
    REQUIRE(store.UpsertMessages(batch));

    Analytics analytics(store);
    MapShape shape;
    shape.maxDomains = 3;
    shape.minShare = 0.0;

    MapNode root;
    REQUIRE(analytics.BuildSenderMap(MessageFilter{}, SenderMetric::MessageCount,
                                     shape, root));
    REQUIRE_EQ(root.children.size(), static_cast<std::size_t>(4));   // 3 + "Other"
    const MapNode* other = FindChild(root, "other");
    REQUIRE(other != nullptr);
    REQUIRE_EQ(other->value, 30.0);
}

TEST(Analytics_SenderCapPoolsWithinADomain) {
    AnalysisStore store = OpenStore();
    std::vector<AnalyzedMessage> batch;
    int uid = 1;
    for (int s = 0; s < 5; ++s) {
        const int count = 5 - s;   // 5, 4, 3, 2, 1 messages
        for (int i = 0; i < count; ++i) {
            batch.push_back(Message("s" + std::to_string(s) + "@one.example", uid++,
                                    kDay * uid, MessageCategory::Personal));
        }
    }
    REQUIRE(store.UpsertMessages(batch));

    Analytics analytics(store);
    MapShape shape;
    shape.maxSendersPerDomain = 2;

    MapNode root;
    REQUIRE(analytics.BuildSenderMap(MessageFilter{}, SenderMetric::MessageCount,
                                     shape, root));
    REQUIRE_EQ(root.children.size(), static_cast<std::size_t>(1));

    const MapNode& domain = root.children[0];
    REQUIRE_EQ(domain.children.size(), static_cast<std::size_t>(3));   // 2 + tail
    REQUIRE(domain.children[2].isAggregate);
    REQUIRE_EQ(domain.children[2].value, 6.0);                         // 3 + 2 + 1
    // The domain's own numbers still cover every sender under it.
    REQUIRE_EQ(domain.block.messageCount, 15);
    REQUIRE_EQ(domain.value, 15.0);
}

TEST(Analytics_MapUsesTheChosenMetric) {
    AnalysisStore store = OpenStore();
    // "chatty" sends more messages; "heavy" sends more bytes.
    REQUIRE(store.UpsertMessages({
        Message("chatty@a.example", 1, kDay, MessageCategory::Personal, 100),
        Message("chatty@a.example", 2, kDay * 2, MessageCategory::Personal, 100),
        Message("chatty@a.example", 3, kDay * 3, MessageCategory::Personal, 100),
        Message("heavy@b.example", 4, kDay * 4, MessageCategory::Personal, 100000),
    }));

    Analytics analytics(store);
    MapShape shape;
    shape.minShare = 0.0;

    MapNode byCount;
    REQUIRE(analytics.BuildSenderMap(MessageFilter{}, SenderMetric::MessageCount,
                                     shape, byCount));
    REQUIRE_EQ(byCount.children[0].key, std::string("a.example"));

    MapNode byBytes;
    REQUIRE(analytics.BuildSenderMap(MessageFilter{}, SenderMetric::TotalBytes,
                                     shape, byBytes));
    REQUIRE_EQ(byBytes.children[0].key, std::string("b.example"));
}

TEST(Analytics_EmptyDatabaseYieldsAnEmptyMap) {
    AnalysisStore store = OpenStore();
    Analytics analytics(store);

    MapNode root;
    REQUIRE(analytics.BuildSenderMap(MessageFilter{}, SenderMetric::MessageCount,
                                     MapShape{}, root));
    REQUIRE(root.children.empty());
    REQUIRE_EQ(root.value, 0.0);
}

TEST(Analytics_TimetableAndTimelinePassThroughTheFilter) {
    AnalysisStore store = OpenStore();
    const int64_t friday = MakeUtcTime(2026, 8, 14, 9, 0, 0);
    REQUIRE(store.UpsertMessages({
        Message("a@x.example", 1, friday, MessageCategory::ProductSpam),
        Message("b@y.example", 2, friday + 3600, MessageCategory::Personal),
    }));

    Analytics analytics(store);
    MessageFilter oneSender;
    oneSender.senderAddr = "a@x.example";

    Timetable table;
    REQUIRE(analytics.BuildTimetable(oneSender, table));
    REQUIRE_EQ(table.total, 1);
    REQUIRE_EQ(table.At(4, 9), 1);

    std::vector<TimelinePoint> points;
    REQUIRE(analytics.BuildTimeline(oneSender, TimeBucket::Day, points));
    REQUIRE_EQ(points.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(points[0].messageCount, 1);
    REQUIRE_EQ(points[0].unwantedCount, 1);
}

TEST(Analytics_ChooseBucketScalesWithTheSpan) {
    const int64_t start = MakeUtcTime(2020, 1, 1, 0, 0, 0);
    REQUIRE(Analytics::ChooseBucket(start, start + 30 * kDay) == TimeBucket::Day);
    REQUIRE(Analytics::ChooseBucket(start, start + 200 * kDay) == TimeBucket::Week);
    REQUIRE(Analytics::ChooseBucket(start, start + 1500 * kDay) == TimeBucket::Month);
    REQUIRE(Analytics::ChooseBucket(start, start + 6000 * kDay) == TimeBucket::Year);
    // Degenerate spans fall back to something drawable rather than crashing.
    REQUIRE(Analytics::ChooseBucket(0, 0) == TimeBucket::Month);
    REQUIRE(Analytics::ChooseBucket(start, start) == TimeBucket::Month);
}

TEST(Analytics_CategoryLegendIsInTaxonomyOrder) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessages({
        Message("a@x.example", 1, kDay, MessageCategory::ProductSpam),
        Message("a@x.example", 2, kDay, MessageCategory::ProductSpam),
        Message("b@y.example", 3, kDay, MessageCategory::Personal),
    }));

    Analytics analytics(store);
    std::vector<CategoryTotal> legend;
    REQUIRE(analytics.BuildCategoryLegend(MessageFilter{}, legend));

    REQUIRE_EQ(legend.size(), static_cast<std::size_t>(2));
    // Personal comes before ProductSpam in the taxonomy, despite being rarer.
    REQUIRE(legend[0].category == MessageCategory::Personal);
    REQUIRE(legend[1].category == MessageCategory::ProductSpam);
    REQUIRE_EQ(legend[1].messageCount, 2);
}

TEST(Analytics_PaletteDistinguishesTheCategories) {
    // Every category needs its own colour, or the map cannot be read.
    std::vector<MapColor> seen;
    for (MessageCategory category : AllCategories()) {
        const MapColor c = Analytics::CategoryColor(category);
        for (const MapColor& other : seen)
            REQUIRE(!(other.r == c.r && other.g == c.g && other.b == c.b));
        seen.push_back(c);
    }
}

TEST(Analytics_BlockColourDeepensWithTheUnwantedShare) {
    SenderBlock clean;
    clean.topCategory  = MessageCategory::ProductSpam;
    clean.messageCount = 10;
    clean.unwantedCount = 1;

    SenderBlock dirty = clean;
    dirty.unwantedCount = 10;

    const MapColor a = Analytics::BlockColor(clean);
    const MapColor b = Analytics::BlockColor(dirty);
    REQUIRE(b.r != a.r || b.g != a.g || b.b != a.b);
    REQUIRE(b.g < a.g);   // shifted towards red
}

TEST(Analytics_DescribeBlockReadsAsASentence) {
    SenderBlock block;
    block.messageCount    = 312;
    block.unwantedCount   = 262;
    block.totalBytes      = 12 * 1024 * 1024;
    block.attachmentCount = 40;
    block.attachmentBytes = 4 * 1024 * 1024;
    block.firstSeen       = MakeUtcTime(2024, 3, 1, 0, 0, 0);
    block.lastSeen        = MakeUtcTime(2026, 8, 14, 0, 0, 0);
    block.topCategory     = MessageCategory::ProductSpam;

    const std::string text = Analytics::DescribeBlock(block);
    REQUIRE(text.find("312 messages") != std::string::npos);
    REQUIRE(text.find("84% unwanted") != std::string::npos);
    REQUIRE(text.find("12.0 MB") != std::string::npos);
    REQUIRE(text.find("40 attachments") != std::string::npos);
    REQUIRE(text.find("Mar 2024") != std::string::npos);
    REQUIRE(text.find("Product spam") != std::string::npos);
}

TEST(Analytics_DescribeTimetableNamesThePeak) {
    Timetable table;
    REQUIRE(Analytics::DescribeTimetable(table).find("No dated") != std::string::npos);

    table.Add(1, 9);
    table.Add(1, 9);
    table.Add(3, 14);
    const std::string text = Analytics::DescribeTimetable(table);
    REQUIRE(text.find("Tue 09:00") != std::string::npos);
    REQUIRE(text.find("2 messages") != std::string::npos);
    REQUIRE(text.find("3 total") != std::string::npos);
}

TEST(Analytics_DescribeOverviewCountsAndDates) {
    StoreOverview empty;
    REQUIRE(Analytics::DescribeOverview(empty).find("No messages") != std::string::npos);

    StoreOverview overview;
    overview.messages    = 1204;
    overview.senders     = 87;
    overview.unwanted    = 214;
    overview.attachments = 96;
    overview.attachmentBytes = 3 * 1024 * 1024;
    overview.firstDate   = MakeUtcTime(2024, 1, 5, 0, 0, 0);
    overview.lastDate    = MakeUtcTime(2026, 8, 14, 0, 0, 0);

    const std::string text = Analytics::DescribeOverview(overview);
    REQUIRE(text.find("1,204 messages") != std::string::npos);
    REQUIRE(text.find("87 senders") != std::string::npos);
    REQUIRE(text.find("214 unwanted (18%)") != std::string::npos);
    REQUIRE(text.find("96 attachments") != std::string::npos);
}
