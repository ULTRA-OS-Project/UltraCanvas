// Tests/UltraMail/test_contacts.cpp
// Exercises the contact store: sectioned storage (Friends/Work/Leisure/
// Services), emails/phones round-trip, section counts, search, update and
// remove. Each test uses its own in-memory database.
// Version: 0.5.0 - a service is collected only with its domain proven;
//                  NeverWrittenTo; RemoveAll
// Version: 0.4.0 - the collector's rules: incoming mail adds known services
//                  only, never blocked or forgotten addresses; Collect reads
//                  "Name <address>"; EditableEmailIndex, ReplaceContactEmail
// Version: 0.2.0 - CollectSenders, SaveAll, KnownAddresses; the file store in WAL mode
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailContactCollector.h"
#include "UltraMailContactStore.h"
#include "UltraMailThreatScan.h"
#include "UltraCanvasPathUtf8.h"

#include <UltraDatabase/UltraDatabase.h>

#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace UltraMail;

namespace {

ContactStore FreshStore(const std::string& tag) {
    ContactStore s;
    UltraDbResult r = s.Open("contacts-" + tag, ":memory:");
    REQUIRE(r.success);
    return s;
}

Contact MakeContact(const std::string& name, ContactSection section,
                    const std::string& email) {
    Contact c;
    c.displayName = name;
    c.section = section;
    ContactEmail e; e.address = email; e.label = "home"; e.primary = true;
    c.emails.push_back(e);
    return c;
}

int CountFor(ContactStore& s, ContactSection section) {
    std::vector<SectionCount> counts;
    REQUIRE(s.GetSectionCounts(counts).success);
    for (auto& c : counts) if (c.section == section) return c.count;
    return -1;
}

} // namespace

TEST(section_string_mapping) {
    REQUIRE_EQ(ToString(ContactSection::Friends), std::string("friends"));
    REQUIRE_EQ(ToString(ContactSection::Family),  std::string("family"));
    REQUIRE(ContactSectionFromString("services") == ContactSection::Services);
    REQUIRE(ContactSectionFromString("family")   == ContactSection::Family);
    REQUIRE(ContactSectionFromString("nonsense") == ContactSection::Other);
    REQUIRE_EQ(DisplayName(ContactSection::Family), std::string("Family"));
    REQUIRE_EQ(PrimarySections().size(), (size_t)5);   // Family + Friends/Work/Leisure/Services
}

TEST(save_assigns_id_and_get_roundtrip) {
    ContactStore s = FreshStore("save");
    Contact c = MakeContact("Anna Schmidt", ContactSection::Friends, "anna@example.com");
    c.organization = "";
    ContactPhone p; p.number = "+49 170 1234567"; p.label = "mobile";
    c.phones.push_back(p);

    REQUIRE(s.Save(c).success);
    REQUIRE(c.id > 0);

    Contact got;
    REQUIRE(s.Get(c.id, got).success);
    REQUIRE_EQ(got.displayName, std::string("Anna Schmidt"));
    REQUIRE(got.section == ContactSection::Friends);
    REQUIRE_EQ(got.emails.size(), (size_t)1);
    REQUIRE_EQ(got.PrimaryEmail(), std::string("anna@example.com"));
    REQUIRE_EQ(got.phones.size(), (size_t)1);
    REQUIRE_EQ(got.phones[0].number, std::string("+49 170 1234567"));
}

TEST(list_by_section_and_counts) {
    ContactStore s = FreshStore("sections");
    Contact a = MakeContact("Bob",   ContactSection::Work,     "bob@work.com");
    Contact b = MakeContact("Carol", ContactSection::Work,     "carol@work.com");
    Contact c = MakeContact("Max",   ContactSection::Friends,  "max@x.com");
    Contact d = MakeContact("Plumber", ContactSection::Services, "info@plumb.com");
    REQUIRE(s.Save(a).success);
    REQUIRE(s.Save(b).success);
    REQUIRE(s.Save(c).success);
    REQUIRE(s.Save(d).success);

    std::vector<Contact> work;
    REQUIRE(s.ListBySection(ContactSection::Work, work).success);
    REQUIRE_EQ(work.size(), (size_t)2);
    REQUIRE_EQ(work[0].displayName, std::string("Bob"));    // alphabetical
    REQUIRE_EQ(work[1].displayName, std::string("Carol"));

    REQUIRE_EQ(CountFor(s, ContactSection::Work), 2);
    REQUIRE_EQ(CountFor(s, ContactSection::Friends), 1);
    REQUIRE_EQ(CountFor(s, ContactSection::Services), 1);
    REQUIRE_EQ(CountFor(s, ContactSection::Leisure), 0);
}

TEST(search_across_name_org_email) {
    ContactStore s = FreshStore("search");
    Contact a = MakeContact("Anna Schmidt", ContactSection::Friends, "anna@example.com");
    a.organization = "Acme GmbH";
    Contact b = MakeContact("Bob Jones", ContactSection::Work, "bob@acme.com");
    REQUIRE(s.Save(a).success);
    REQUIRE(s.Save(b).success);

    std::vector<Contact> byName;
    REQUIRE(s.Search("schmidt", byName).success);
    REQUIRE_EQ(byName.size(), (size_t)1);

    std::vector<Contact> byEmail;
    REQUIRE(s.Search("acme.com", byEmail).success);
    REQUIRE_EQ(byEmail.size(), (size_t)1);
    REQUIRE_EQ(byEmail[0].displayName, std::string("Bob Jones"));

    std::vector<Contact> byOrg;
    REQUIRE(s.Search("Acme", byOrg).success);   // matches org and email
    REQUIRE_EQ(byOrg.size(), (size_t)2);
}

TEST(update_moves_section_and_replaces_children) {
    ContactStore s = FreshStore("update");
    Contact c = MakeContact("Dana", ContactSection::Leisure, "dana@x.com");
    REQUIRE(s.Save(c).success);

    // Move to Work and swap the email.
    c.section = ContactSection::Work;
    c.emails.clear();
    ContactEmail e; e.address = "dana@work.com"; e.primary = true;
    c.emails.push_back(e);
    REQUIRE(s.Save(c).success);        // same id -> update

    REQUIRE_EQ(CountFor(s, ContactSection::Leisure), 0);
    REQUIRE_EQ(CountFor(s, ContactSection::Work), 1);
    Contact got;
    REQUIRE(s.Get(c.id, got).success);
    REQUIRE_EQ(got.emails.size(), (size_t)1);
    REQUIRE_EQ(got.PrimaryEmail(), std::string("dana@work.com"));
}

TEST(remove_deletes_contact_and_children) {
    ContactStore s = FreshStore("remove");
    Contact c = MakeContact("Temp", ContactSection::Friends, "temp@x.com");
    REQUIRE(s.Save(c).success);
    REQUIRE_EQ(CountFor(s, ContactSection::Friends), 1);

    REQUIRE(s.Remove(c.id).success);
    REQUIRE_EQ(CountFor(s, ContactSection::Friends), 0);
    Contact got;
    REQUIRE(s.Get(c.id, got).code == UltraDbResultCode::NotFound);
}

// --- Auto-collection --------------------------------------------------------

TEST(collect_sender_files_a_known_service_as_a_business_contact) {
    ContactStore s = FreshStore("collect-brand");

    // A crowdfunding platform the user backed a project on: the registry knows
    // the domain, so the contact is filed under Services with the service as
    // its organization rather than as a loose address in Other.
    REQUIRE(ContactCollector::CollectSender(s, "Kickstarter", "no-reply@kickstarter.com", "kickstarter.com"));

    std::vector<Contact> services;
    REQUIRE(s.ListBySection(ContactSection::Services, services).success);
    REQUIRE_EQ(services.size(), (size_t)1);
    REQUIRE_EQ(services.front().organization, std::string("Kickstarter"));
    REQUIRE(services.front().notes.find("Crowdfunding platform") != std::string::npos);
    REQUIRE_EQ(services.front().PrimaryEmail(), std::string("no-reply@kickstarter.com"));

    // A robot display name is replaced by the service's own name only when it
    // is missing; a real one is kept.
    REQUIRE(ContactCollector::CollectSender(s, "", "hello@buymeacoffee.com", "buymeacoffee.com"));
    REQUIRE(s.ListBySection(ContactSection::Services, services).success);
    bool named = false;
    for (const auto& c : services)
        if (c.PrimaryEmail() == "hello@buymeacoffee.com")
            named = (c.displayName == "Buy Me a Coffee");
    REQUIRE(named);
}

TEST(collect_sender_does_not_add_ordinary_senders) {
    ContactStore s = FreshStore("collect-other");
    // A stranger who writes is no contact: not suggested, still a "New sender".
    REQUIRE(!ContactCollector::CollectSender(s, "Anna Schmidt", "anna@example.com", "example.com"));
    REQUIRE(!ContactCollector::CollectSender(s, "BILLHANEY", "billhaney.55@outlook.com", "outlook.com"));
    {
        std::vector<Contact> other;
        REQUIRE(s.ListBySection(ContactSection::Other, other).success);
        REQUIRE(other.empty());
    }
    REQUIRE_EQ(CountFor(s, ContactSection::Services), 0);
}

TEST(collect_sender_skips_blocked_and_forgotten_services) {
    ContactStore s = FreshStore("collect-skips");
    SenderLists lists;
    lists.blocked.insert("@kickstarter.com");
    SetSenderLists(lists);
    REQUIRE(!ContactCollector::CollectSender(s, "Kickstarter", "no-reply@kickstarter.com", "kickstarter.com"));
    SetSenderLists(SenderLists{});

    REQUIRE(s.Forget({"Hello@BuyMeACoffee.com"}).success);
    REQUIRE(!ContactCollector::CollectSender(s, "", "hello@buymeacoffee.com", "buymeacoffee.com"));
    REQUIRE_EQ(ContactCollector::CollectSenders(s, {{"", "hello@buymeacoffee.com", "buymeacoffee.com"}}), 0);
    REQUIRE_EQ(CountFor(s, ContactSection::Services), 0);

    // Writing to a forgotten address is the user's own act: it is added.
    REQUIRE(ContactCollector::Collect(s, "", "hello@buymeacoffee.com"));
}

TEST(collect_reads_a_recipient_with_its_name) {
    ContactStore s = FreshStore("collect-recipient");
    REQUIRE(ContactCollector::Collect(s, "", "Anna Schmidt <anna@example.com>"));
    REQUIRE(ContactCollector::Collect(s, "", "\"Bob\" <bob@example.org>"));
    std::vector<Contact> other;
    REQUIRE(s.ListBySection(ContactSection::Other, other).success);
    REQUIRE_EQ(other.size(), (size_t)2);
    REQUIRE_EQ(other[0].displayName, std::string("Anna Schmidt"));
    REQUIRE_EQ(other[0].PrimaryEmail(), std::string("anna@example.com"));
    REQUIRE_EQ(other[1].displayName, std::string("Bob"));
    REQUIRE_EQ(other[1].PrimaryEmail(), std::string("bob@example.org"));
    // The bare address is the same person: nothing new.
    REQUIRE(!ContactCollector::Collect(s, "", "ANNA@example.com"));
}

TEST(collect_sender_never_reclassifies_an_existing_contact) {
    ContactStore s = FreshStore("collect-existing");
    // The user filed this address under Friends themselves; a later collect of
    // the same address must not move it into Services.
    Contact mine = MakeContact("My Kickstarter account", ContactSection::Friends,
                               "no-reply@kickstarter.com");
    REQUIRE(s.Save(mine).success);

    REQUIRE(!ContactCollector::CollectSender(s, "Kickstarter", "no-reply@kickstarter.com", "kickstarter.com"));
    REQUIRE_EQ(CountFor(s, ContactSection::Friends), 1);
    REQUIRE_EQ(CountFor(s, ContactSection::Services), 0);
}

TEST(list_batches_children_for_every_contact) {
    ContactStore s = FreshStore("batch");
    for (int i = 0; i < 30; ++i) {
        Contact c = MakeContact("P" + std::to_string(i), ContactSection::Other,
                                "p" + std::to_string(i) + "@x.example");
        ContactEmail extra; extra.address = "alt" + std::to_string(i) + "@x.example";
        c.emails.push_back(extra);
        ContactPhone ph; ph.number = std::to_string(1000 + i); c.phones.push_back(ph);
        REQUIRE(s.Save(c).success);
    }
    std::vector<Contact> all;
    REQUIRE(s.ListBySection(ContactSection::Other, all).success);
    REQUIRE_EQ(all.size(), (size_t)30);
    for (const auto& c : all) {
        REQUIRE_EQ(c.emails.size(), (size_t)2);
        REQUIRE(c.emails.front().primary);                 // primary first
        REQUIRE_EQ(c.phones.size(), (size_t)1);
        // Each contact got its own children, not a neighbour's.
        REQUIRE_EQ(c.emails.front().address,
                   "p" + c.displayName.substr(1) + "@x.example");
    }
}

TEST(groups_add_move_count_and_remove) {
    ContactStore s = FreshStore("groups");
    Contact a = MakeContact("Anna", ContactSection::Friends, "anna@x.example");
    Contact b = MakeContact("Bert", ContactSection::Work, "bert@x.example");
    REQUIRE(s.Save(a).success);
    REQUIRE(s.Save(b).success);

    REQUIRE(s.AddGroup("Choir").success);
    REQUIRE(!s.AddGroup("choir").success);   // taken, whatever the case
    REQUIRE(!s.AddGroup("Work").success);    // a section's name
    REQUIRE(!s.AddGroup("").success);

    REQUIRE(s.MoveToGroup(a.id, "Choir").success);
    std::vector<GroupCount> groups;
    REQUIRE(s.ListGroups(groups).success);
    REQUIRE_EQ(groups.size(), (size_t)1);
    REQUIRE_EQ(groups[0].name, std::string("Choir"));
    REQUIRE_EQ(groups[0].count, 1);
    REQUIRE_EQ(CountFor(s, ContactSection::Friends), 0);   // filed in the group now

    std::vector<Contact> inGroup;
    REQUIRE(s.ListByGroup("Choir", inGroup).success);
    REQUIRE_EQ(inGroup.size(), (size_t)1);
    REQUIRE(inGroup[0].section == ContactSection::Friends);   // kind unchanged
    std::vector<Contact> all;
    REQUIRE(s.ListAll(all).success);
    REQUIRE_EQ(all.size(), (size_t)2);

    // Saving an edited contact keeps its group.
    Contact edited = inGroup[0];
    edited.notes = "tenor";
    REQUIRE(s.Save(edited).success);
    REQUIRE(s.ListByGroup("Choir", inGroup).success);
    REQUIRE_EQ(inGroup.size(), (size_t)1);

    // Moving to a section leaves the group; deleting the group returns the rest.
    REQUIRE(s.MoveToSection(b.id, ContactSection::Leisure).success);
    REQUIRE_EQ(CountFor(s, ContactSection::Leisure), 1);
    REQUIRE(s.RemoveGroup("Choir").success);
    REQUIRE(s.ListGroups(groups).success);
    REQUIRE(groups.empty());
    REQUIRE_EQ(CountFor(s, ContactSection::Friends), 1);
}

#if defined(ULTRANET_HAS_ICONV)
TEST(open_repairs_names_stored_as_raw_bytes) {
    const std::string path = "contacts-jis-repair.db";
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
    {
        ContactStore s;
        REQUIRE(s.Open("contacts-jis-a", path).success);
        Contact c = MakeContact("\x1b$B3t<02q<R%F%l%7%\"\x1b(B", ContactSection::Other,
                                "wordpress@www.tereshia.com");
        REQUIRE(s.Save(c).success);   // Save leaves the name as given
        // A Latin-1 name stored raw, and a UTF-8 one that must stay as it is.
        Contact latin = MakeContact("Andr\xE9 M\xFCller", ContactSection::Other, "am@x.example");
        REQUIRE(s.Save(latin).success);
        Contact ok = MakeContact("J\xC3\xBCrgen", ContactSection::Other, "j@x.example");
        REQUIRE(s.Save(ok).success);
    }
    ContactStore again;
    REQUIRE(again.Open("contacts-jis-b", path).success);
    std::vector<Contact> all;
    REQUIRE(again.ListAll(all).success);
    REQUIRE_EQ(all.size(), (size_t)3);
    REQUIRE_EQ(all[0].displayName, std::string("Andr\xC3\xA9 M\xC3\xBCller"));   // sorted by name
    REQUIRE_EQ(all[1].displayName, std::string("J\xC3\xBCrgen"));
    REQUIRE_EQ(all[2].displayName,
               std::string("\xE6\xA0\xAA\xE5\xBC\x8F\xE4\xBC\x9A\xE7\xA4\xBE"
                           "\xE3\x83\x86\xE3\x83\xAC\xE3\x82\xB7\xE3\x82\xA2"));
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}
#endif

// After a sync the senders of all its new mail are collected at once: one
// contact per new known service however often it came, none for an address
// the address book holds already, and none for an ordinary sender.
TEST(collect_senders_adds_each_new_service_once) {
    ContactStore s = FreshStore("collect-batch");
    Contact mine = MakeContact("Kickstarter (mine)", ContactSection::Friends,
                               "no-reply@kickstarter.com");
    REQUIRE(s.Save(mine).success);

    const std::vector<SenderSeen> senders = {
        {"Kickstarter", "No-Reply@Kickstarter.com", "kickstarter.com"},   // in the book, in other letters
        {"Bob", "bob@example.org", "example.org"},                        // a person: not added
        {"", "hello@buymeacoffee.com", "buymeacoffee.com"},               // a known service
        {"BMC", "HELLO@buymeacoffee.com", "buymeacoffee.com"},            // again: the first name stays
        {"Nobody", "", ""},                                               // no address: nothing to keep
    };
    REQUIRE_EQ(ContactCollector::CollectSenders(s, senders), 1);
    REQUIRE_EQ(CountFor(s, ContactSection::Friends), 1);   // untouched
    {
        std::vector<Contact> other;
        REQUIRE(s.ListBySection(ContactSection::Other, other).success);
        REQUIRE(other.empty());
    }

    std::vector<Contact> services;
    REQUIRE(s.ListBySection(ContactSection::Services, services).success);
    REQUIRE_EQ(services.size(), (size_t)1);
    REQUIRE_EQ(services.front().displayName, std::string("Buy Me a Coffee"));

    // The same batch again finds every address known; an empty one is nothing.
    REQUIRE_EQ(ContactCollector::CollectSenders(s, senders), 0);
    REQUIRE_EQ(ContactCollector::CollectSenders(s, {}), 0);
}

TEST(save_all_saves_a_batch_or_nothing) {
    ContactStore s = FreshStore("save-all");
    std::vector<Contact> batch = {
        MakeContact("Ada", ContactSection::Friends, "Ada@X.example"),
        MakeContact("Ben", ContactSection::Work, "ben@x.example"),
    };
    REQUIRE(s.SaveAll(batch).success);
    REQUIRE(batch[0].id > 0);
    REQUIRE(batch[1].id > 0);
    REQUIRE(batch[0].id != batch[1].id);
    Contact got;
    REQUIRE(s.Get(batch[1].id, got).success);
    REQUIRE_EQ(got.PrimaryEmail(), std::string("ben@x.example"));

    // One contact that cannot be saved: none of the batch is.
    std::vector<Contact> bad = {
        MakeContact("Cleo", ContactSection::Friends, "cleo@x.example"),
        MakeContact("", ContactSection::Friends, "nameless@x.example"),
    };
    REQUIRE(!s.SaveAll(bad).success);
    REQUIRE_EQ(bad[0].id, (int64_t)0);

    // The stored addresses, lower-cased.
    std::set<std::string> known;
    REQUIRE(s.KnownAddresses(known).success);
    REQUIRE_EQ(known.size(), (size_t)2);
    REQUIRE(known.count("ada@x.example") == 1);
    REQUIRE(known.count("ben@x.example") == 1);
    REQUIRE(known.count("cleo@x.example") == 0);
}

// The address book on disk is in WAL mode, as the mail index is: in the
// default rollback journal every saved contact created, flushed and deleted a
// journal file, which on Windows cost tens of milliseconds a contact.
TEST(file_contact_store_uses_wal) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "ultramail_contacts_wal_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    {
        ContactStore s;
        REQUIRE(s.Open("contacts-wal", UltraCanvas::PathToUtf8(dir / "contacts.db")).success);
        UltraDbResultSet rs;
        REQUIRE(UltraDb_Query("contacts-wal", "PRAGMA journal_mode", rs).success);
        REQUIRE_EQ(rs.Size(), (size_t)1);
        REQUIRE_EQ(rs.Row(0)[0].AsString(), std::string("wal"));
        Contact c = MakeContact("Dora", ContactSection::Other, "dora@x.example");
        REQUIRE(s.Save(c).success);
    }
    UltraDb_CloseConnection("contacts-wal");
    fs::remove_all(dir, ec);
}

namespace {

std::vector<ContactEmail> ThreeEmails() {
    std::vector<ContactEmail> v(3);
    v[0].address = "anna@home.example";  v[0].label = "home"; v[0].primary = true;
    v[1].address = "anna@work.example";  v[1].label = "work";
    v[2].address = "anna@club.example";
    return v;
}

} // namespace

TEST(editable_email_is_the_one_named_else_the_primary) {
    const auto v = ThreeEmails();
    REQUIRE(EditableEmailIndex(v, "ANNA@work.example") == 1);
    REQUIRE(EditableEmailIndex(v, "") == 0);
    REQUIRE(EditableEmailIndex(v, "someone@else.example") == 0);
    REQUIRE(EditableEmailIndex({}, "anna@work.example") == std::string::npos);
}

TEST(replace_contact_email_edits_only_that_address) {
    auto v = ThreeEmails();
    ReplaceContactEmail(v, 1, "anna@job.example");
    REQUIRE(v.size() == 3);
    REQUIRE_EQ(v[0].address, std::string("anna@home.example"));
    REQUIRE(v[0].primary);
    REQUIRE_EQ(v[1].address, std::string("anna@job.example"));
    REQUIRE_EQ(v[1].label, std::string("work"));   // kept
    REQUIRE(!v[1].primary);
    REQUIRE_EQ(v[2].address, std::string("anna@club.example"));
}

TEST(replace_contact_email_removes_an_emptied_address) {
    auto v = ThreeEmails();
    ReplaceContactEmail(v, 0, "");   // the primary goes
    REQUIRE(v.size() == 2);
    REQUIRE_EQ(v[0].address, std::string("anna@work.example"));
    REQUIRE(v[0].primary);           // another one takes the mark
}

TEST(replace_contact_email_merges_with_a_duplicate) {
    auto v = ThreeEmails();
    ReplaceContactEmail(v, 2, "Anna@Home.example");   // the club one becomes the home one
    REQUIRE(v.size() == 2);
    REQUIRE_EQ(v[0].address, std::string("anna@work.example"));
    REQUIRE_EQ(v[1].address, std::string("Anna@Home.example"));
    REQUIRE(v[1].primary);           // the merged one was primary
    REQUIRE(!v[0].primary);
}

TEST(replace_contact_email_adds_to_a_contact_without_one) {
    std::vector<ContactEmail> v;
    ReplaceContactEmail(v, std::string::npos, "new@example.com");
    REQUIRE(v.size() == 1);
    REQUIRE(v[0].primary);
    ReplaceContactEmail(v, std::string::npos, "");
    REQUIRE(v.size() == 1);
}

TEST(domain_proven_takes_the_domain_its_subdomains_and_its_parent) {
    REQUIRE(DomainProven("no-reply@kickstarter.com", "kickstarter.com"));
    REQUIRE(DomainProven("a@Mail.Kickstarter.com", "kickstarter.com"));   // subdomain
    REQUIRE(DomainProven("a@kickstarter.com", "email.kickstarter.com"));  // parent
    REQUIRE(!DomainProven("a@kickstarter.com", ""));                      // nothing proved
    REQUIRE(!DomainProven("a@kickstarter.com", "kickstarter-mail.com"));
    REQUIRE(!DomainProven("a@evilkickstarter.com", "kickstarter.com"));
    REQUIRE(!DomainProven("a@kickstarter.com", "com"));                   // no top level
}

TEST(collect_sender_needs_the_service_domain_proven) {
    ContactStore s = FreshStore("collect-proven");
    // Anyone can write no-reply@kickstarter.com in a From line.
    REQUIRE(!ContactCollector::CollectSender(s, "Kickstarter", "no-reply@kickstarter.com", ""));
    REQUIRE(!ContactCollector::CollectSender(s, "Kickstarter", "no-reply@kickstarter.com",
                                             "scam.example"));
    REQUIRE_EQ(ContactCollector::CollectSenders(s, {{"Kickstarter", "no-reply@kickstarter.com", ""}}), 0);
    REQUIRE_EQ(CountFor(s, ContactSection::Services), 0);
    REQUIRE(ContactCollector::CollectSender(s, "Kickstarter", "no-reply@kickstarter.com",
                                            "kickstarter.com"));
}

TEST(never_written_to_finds_bare_collected_contacts) {
    std::vector<Contact> v;
    v.push_back(MakeContact("BILLHANEY", ContactSection::Other, "BILLHANEY.55@outlook.com"));
    v.push_back(MakeContact("Bio pharm", ContactSection::Other, "lua.hol@uzbecair.com"));
    v.push_back(MakeContact("Written to", ContactSection::Other, "Friend@Example.com"));
    Contact phone = MakeContact("Has a phone", ContactSection::Other, "p@example.com");
    ContactPhone ph; ph.number = "123"; phone.phones.push_back(ph);
    v.push_back(phone);
    Contact org = MakeContact("Has an org", ContactSection::Other, "o@example.com");
    org.organization = "ACME";
    v.push_back(org);
    Contact two = MakeContact("Two addresses", ContactSection::Other, "t1@example.com");
    ContactEmail e2; e2.address = "t2@example.com"; two.emails.push_back(e2);
    v.push_back(two);
    v.push_back(MakeContact("A friend", ContactSection::Friends, "f@example.com"));
    Contact grouped = MakeContact("In a group", ContactSection::Other, "g@example.com");
    grouped.group = "Club";
    v.push_back(grouped);

    const std::map<std::string, double> written = {{"friend@example.com", 1.0}};
    const auto found = ContactCollector::NeverWrittenTo(v, written);
    REQUIRE_EQ(found.size(), (size_t)2);
    REQUIRE_EQ(found[0].displayName, std::string("BILLHANEY"));
    REQUIRE_EQ(found[1].displayName, std::string("Bio pharm"));
}

TEST(remove_all_removes_the_given_contacts_at_once) {
    ContactStore s = FreshStore("remove-all");
    Contact a = MakeContact("A", ContactSection::Other, "a@example.com");
    Contact b = MakeContact("B", ContactSection::Other, "b@example.com");
    Contact c = MakeContact("C", ContactSection::Other, "c@example.com");
    REQUIRE(s.Save(a).success);
    REQUIRE(s.Save(b).success);
    REQUIRE(s.Save(c).success);
    REQUIRE(s.RemoveAll({a.id, c.id}).success);
    std::vector<Contact> other;
    REQUIRE(s.ListBySection(ContactSection::Other, other).success);
    REQUIRE_EQ(other.size(), (size_t)1);
    REQUIRE_EQ(other[0].displayName, std::string("B"));
    std::set<std::string> known;
    REQUIRE(s.KnownAddresses(known).success);
    REQUIRE(known.count("a@example.com") == 0);
    REQUIRE(s.RemoveAll({}).success);
}
