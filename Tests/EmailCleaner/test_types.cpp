// Tests/EmailCleaner/test_types.cpp
// Address parsing, RFC 5322 dates, UTC bucketing and the timetable grid.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerTypes.h"

using namespace EmailCleaner;

TEST(ParseAddress_AngleBracketForm) {
    std::string name, address;
    ParseAddress("Erika Example <Erika@Example.COM>", name, address);
    REQUIRE_EQ(name, std::string("Erika Example"));
    REQUIRE_EQ(address, std::string("erika@example.com"));
}

TEST(ParseAddress_QuotedNameAndBareAddress) {
    std::string name, address;
    ParseAddress("\"Example, Erika\" <erika@example.com>", name, address);
    REQUIRE_EQ(name, std::string("Example, Erika"));
    REQUIRE_EQ(address, std::string("erika@example.com"));

    ParseAddress("  erika@example.com  ", name, address);
    REQUIRE(name.empty());
    REQUIRE_EQ(address, std::string("erika@example.com"));

    ParseAddress("erika@example.com (Erika Example)", name, address);
    REQUIRE_EQ(name, std::string("Erika Example"));
    REQUIRE_EQ(address, std::string("erika@example.com"));
}

TEST(ParseAddress_EmptyAndNonAddress) {
    std::string name, address;
    ParseAddress("", name, address);
    REQUIRE(name.empty());
    REQUIRE(address.empty());

    ParseAddress("Mailer Daemon", name, address);
    REQUIRE(address.empty());
    REQUIRE_EQ(name, std::string("Mailer Daemon"));
}

TEST(DomainAndLocalPart) {
    REQUIRE_EQ(DomainOf("erika@Example.com"), std::string("example.com"));
    REQUIRE_EQ(DomainOf("nonsense"), std::string(""));
    REQUIRE_EQ(LocalPartOf("Erika@example.com"), std::string("erika"));
}

TEST(ParseRfc5322Date_CommonForms) {
    int64_t epoch = 0;
    REQUIRE(ParseRfc5322Date("Fri, 14 Aug 2026 09:30:00 +0000", epoch));
    REQUIRE_EQ(epoch, MakeUtcTime(2026, 8, 14, 9, 30, 0));

    // The same instant expressed in +0200 must land on the same epoch.
    int64_t shifted = 0;
    REQUIRE(ParseRfc5322Date("Fri, 14 Aug 2026 11:30:00 +0200", shifted));
    REQUIRE_EQ(shifted, epoch);

    // No day-of-week, no seconds, alphabetic zone.
    int64_t plain = 0;
    REQUIRE(ParseRfc5322Date("14 Aug 2026 09:30 GMT", plain));
    REQUIRE_EQ(plain, epoch);
}

TEST(ParseRfc5322Date_RejectsGarbage) {
    int64_t epoch = 123;
    REQUIRE(!ParseRfc5322Date("", epoch));
    REQUIRE(!ParseRfc5322Date("tomorrow afternoon", epoch));
    REQUIRE(!ParseRfc5322Date("32 Foo 2026 09:30:00 +0000", epoch));
}

TEST(UtcRoundTripAndWeekday) {
    const int64_t epoch = MakeUtcTime(2026, 8, 14, 9, 30, 15);
    const UtcParts p = BreakUtcTime(epoch);
    REQUIRE_EQ(p.year, 2026);
    REQUIRE_EQ(p.month, 8);
    REQUIRE_EQ(p.day, 14);
    REQUIRE_EQ(p.hour, 9);
    REQUIRE_EQ(p.minute, 30);
    REQUIRE_EQ(p.second, 15);
    REQUIRE_EQ(p.weekday, 4);   // 14 Aug 2026 is a Friday (0 = Monday)

    // The epoch itself was a Thursday.
    REQUIRE_EQ(BreakUtcTime(0).weekday, 3);
}

TEST(BucketStart_SnapsToDayWeekMonthYear) {
    const int64_t when = MakeUtcTime(2026, 8, 14, 9, 30, 15);   // Friday
    REQUIRE_EQ(BucketStart(when, TimeBucket::Day),   MakeUtcTime(2026, 8, 14, 0, 0, 0));
    REQUIRE_EQ(BucketStart(when, TimeBucket::Week),  MakeUtcTime(2026, 8, 10, 0, 0, 0));
    REQUIRE_EQ(BucketStart(when, TimeBucket::Month), MakeUtcTime(2026, 8, 1, 0, 0, 0));
    REQUIRE_EQ(BucketStart(when, TimeBucket::Year),  MakeUtcTime(2026, 1, 1, 0, 0, 0));
    REQUIRE_EQ(BucketLabel(BucketStart(when, TimeBucket::Month), TimeBucket::Month),
               std::string("Aug 2026"));
}

TEST(Timetable_TracksTotalAndPeak) {
    Timetable table;
    REQUIRE_EQ(table.total, 0);
    REQUIRE_EQ(table.peakDay, -1);

    table.Add(1, 9);
    table.Add(1, 9);
    table.Add(4, 17);
    REQUIRE_EQ(table.total, 3);
    REQUIRE_EQ(table.At(1, 9), 2);
    REQUIRE_EQ(table.At(4, 17), 1);
    REQUIRE_EQ(table.peakDay, 1);
    REQUIRE_EQ(table.peakHour, 9);
    REQUIRE_EQ(table.peakCount, 2);

    // Out-of-range cells are ignored rather than corrupting the grid.
    table.Add(9, 9);
    table.Add(1, 30);
    REQUIRE_EQ(table.total, 3);

    table.Recompute();
    REQUIRE_EQ(table.total, 3);
    REQUIRE_EQ(table.peakCount, 2);
}

TEST(CategoryStringsRoundTrip) {
    for (MessageCategory category : AllCategories())
        REQUIRE(CategoryFromString(ToString(category)) == category);

    REQUIRE(CategoryFromString("spam") == MessageCategory::ProductSpam);
    REQUIRE(CategoryFromString("what-even-is-this") == MessageCategory::Unclassified);
    REQUIRE(IsUnwanted(MessageCategory::DatingScam));
    REQUIRE(!IsUnwanted(MessageCategory::Newsletter));
}

TEST(MetricValuePicksTheRightField) {
    SenderBlock block;
    block.messageCount    = 7;
    block.totalBytes      = 1024;
    block.attachmentBytes = 512;
    block.unwantedCount   = 3;
    REQUIRE_EQ(MetricValue(block, SenderMetric::MessageCount), 7.0);
    REQUIRE_EQ(MetricValue(block, SenderMetric::TotalBytes), 1024.0);
    REQUIRE_EQ(MetricValue(block, SenderMetric::AttachmentBytes), 512.0);
    REQUIRE_EQ(MetricValue(block, SenderMetric::UnwantedCount), 3.0);
}

TEST(FormatBytesIsHumanReadable) {
    REQUIRE_EQ(FormatBytes(512), std::string("512 B"));
    REQUIRE_EQ(FormatBytes(2048), std::string("2.0 KB"));
    REQUIRE_EQ(FormatBytes(3ll * 1024 * 1024), std::string("3.0 MB"));
}
