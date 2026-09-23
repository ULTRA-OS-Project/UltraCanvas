// Tests/UltraMail/test_feedpublisher.cpp
// The feed publisher's decisions: what qualifies for the desktop feed and
// the per-account rate limit an initial sync runs into. The bus itself is
// covered by Tests/UltraMessage.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailFeedPublisher.h"

#include <string>

using namespace UltraMail;

namespace {

MessageEnvelope Envelope(int64_t date, uint32_t flags = Flag_None) {
    MessageEnvelope m;
    m.accountId = "erika";
    m.folder = "INBOX";
    m.uid = 42;
    m.subject = "Hello";
    m.fromAddr = "ada@example.org";
    m.date = date;
    m.flags = flags;
    return m;
}

constexpr int64_t kNow = 1'800'000'000;

} // namespace

TEST(feed_qualifies_unread_recent_mail) {
    REQUIRE(FeedPublisher::Qualifies(Envelope(kNow - 60), kNow));
    REQUIRE(FeedPublisher::Qualifies(Envelope(kNow - 6 * 86400), kNow));
}

TEST(feed_rejects_seen_old_deleted_and_undated) {
    REQUIRE(!FeedPublisher::Qualifies(Envelope(kNow - 60, Flag_Seen), kNow));
    REQUIRE(!FeedPublisher::Qualifies(Envelope(kNow - 60, Flag_Deleted), kNow));
    REQUIRE(!FeedPublisher::Qualifies(Envelope(kNow - 60, Flag_Draft), kNow));
    REQUIRE(!FeedPublisher::Qualifies(Envelope(kNow - 8 * 86400), kNow));
    REQUIRE(!FeedPublisher::Qualifies(Envelope(0), kNow));
    FeedPolicy forever;
    forever.maxAgeDays = 0;
    REQUIRE(FeedPublisher::Qualifies(Envelope(kNow - 400 * 86400), kNow, forever));
    REQUIRE(FeedPublisher::Qualifies(Envelope(0), kNow, forever));
}

TEST(feed_policy_can_skip_automated_mail) {
    MessageEnvelope bulk = Envelope(kNow - 60);
    bulk.automated = true;
    REQUIRE(FeedPublisher::Qualifies(bulk, kNow));
    FeedPolicy quiet;
    quiet.skipAutomated = true;
    REQUIRE(!FeedPublisher::Qualifies(bulk, kNow, quiet));
}

TEST(feed_rate_limit_is_per_account_and_window) {
    FeedPublisher publisher;
    FeedPolicy policy;
    policy.maxPerAccountPerWindow = 3;
    policy.windowSeconds = 100;
    publisher.SetPolicy(policy);
    REQUIRE(publisher.Admit("erika", kNow));
    REQUIRE(publisher.Admit("erika", kNow + 1));
    REQUIRE(publisher.Admit("erika", kNow + 2));
    REQUIRE(!publisher.Admit("erika", kNow + 3));      // the window is full
    REQUIRE(publisher.Admit("work", kNow + 3));        // another account has its own
    REQUIRE(publisher.Admit("erika", kNow + 100));     // a new window opens
    REQUIRE(publisher.Admit("erika", kNow + 101));
}

TEST(feed_disabled_publisher_posts_nothing) {
    FeedPublisher publisher;
    publisher.SetEnabled(false);
    REQUIRE(!publisher.Enabled());
    REQUIRE(!publisher.Publish(Envelope(kNow - 60)));
    REQUIRE_EQ(publisher.PublishedCount(), (int64_t)0);
    // Disqualified mail never reaches the bus either, connected or not.
    publisher.SetEnabled(true);
    REQUIRE(!publisher.Publish(Envelope(kNow - 60, Flag_Seen)));
    REQUIRE_EQ(publisher.PublishedCount(), (int64_t)0);
}
