// Tests/UltraMail/test_scheduler.cpp
// The sync scheduler's due-account logic and the contact auto-collector.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailSyncScheduler.h"
#include "UltraMailContactCollector.h"
#include "UltraMailContactStore.h"

#include <string>
#include <vector>

using namespace UltraMail;

// ---- scheduler -------------------------------------------------------------

TEST(scheduler_never_synced_is_due) {
    SyncScheduler s;
    s.SetAccount("erika", "imaps://x/", 300);
    s.SetAccount("work",  "imaps://y/", 600);

    auto due = s.DueAccounts(1000);
    REQUIRE_EQ(due.size(), (size_t)2);   // neither synced yet
}

TEST(scheduler_respects_interval) {
    SyncScheduler s;
    s.SetAccount("erika", "imaps://x/", 300);
    s.MarkSynced("erika", 1000);

    REQUIRE_EQ(s.DueAccounts(1100).size(), (size_t)0);   // 100s < 300s interval
    REQUIRE_EQ(s.DueAccounts(1300).size(), (size_t)1);   // exactly at interval -> due
    REQUIRE_EQ(s.DueAccounts(2000).size(), (size_t)1);   // well past

    s.MarkSynced("erika", 2000);
    REQUIRE_EQ(s.DueAccounts(2100).size(), (size_t)0);   // reset
}

TEST(scheduler_remove) {
    SyncScheduler s;
    s.SetAccount("erika", "imaps://x/", 300);
    REQUIRE_EQ(s.Count(), (size_t)1);
    s.Remove("erika");
    REQUIRE_EQ(s.Count(), (size_t)0);
}

// ---- contact collector -----------------------------------------------------

namespace {
ContactStore FreshContacts(const std::string& tag) {
    ContactStore s;
    REQUIRE(s.Open("collect-" + tag, ":memory:").success);
    return s;
}
int TotalContacts(ContactStore& s) {
    std::vector<SectionCount> counts;
    s.GetSectionCounts(counts);
    int total = 0;
    for (auto& c : counts) total += c.count;
    return total;
}
} // namespace

TEST(collector_adds_new_and_skips_existing) {
    ContactStore store = FreshContacts("basic");

    REQUIRE(ContactCollector::Collect(store, "Anna Schmidt", "anna@example.com"));
    REQUIRE_EQ(TotalContacts(store), 1);

    // Same email again -> not added.
    REQUIRE(!ContactCollector::Collect(store, "Anna S.", "anna@example.com"));
    REQUIRE_EQ(TotalContacts(store), 1);

    // Different email -> added.
    REQUIRE(ContactCollector::Collect(store, "Max Weber", "max@example.com"));
    REQUIRE_EQ(TotalContacts(store), 2);
}

TEST(collector_ignores_blank_and_uses_email_when_no_name) {
    ContactStore store = FreshContacts("blank");
    REQUIRE(!ContactCollector::Collect(store, "Nobody", ""));   // blank email ignored
    REQUIRE_EQ(TotalContacts(store), 0);

    REQUIRE(ContactCollector::Collect(store, "", "noname@example.com"));
    std::vector<Contact> other;
    store.ListBySection(ContactSection::Other, other);
    REQUIRE_EQ(other.size(), (size_t)1);
    REQUIRE_EQ(other[0].displayName, std::string("noname@example.com"));  // falls back to email
}

TEST(collector_does_not_reclassify_existing) {
    ContactStore store = FreshContacts("reclass");
    Contact c; c.displayName = "Carol"; c.section = ContactSection::Work;
    ContactEmail e; e.address = "carol@acme.com"; e.primary = true; c.emails.push_back(e);
    REQUIRE(store.Save(c).success);

    // Collecting the same address must not move Carol out of Work.
    REQUIRE(!ContactCollector::Collect(store, "Carol", "carol@acme.com", ContactSection::Other));
    std::vector<Contact> work;
    store.ListBySection(ContactSection::Work, work);
    REQUIRE_EQ(work.size(), (size_t)1);
}
