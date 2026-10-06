// Tests/UltraMail/test_contacts.cpp
// Exercises the contact store: sectioned storage (Friends/Work/Leisure/
// Services), emails/phones round-trip, section counts, search, update and
// remove. Each test uses its own in-memory database.
// Version: 0.2.0 - CollectSenders, SaveAll, KnownAddresses; the file store in WAL mode
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailContactCollector.h"
#include "UltraMailContactStore.h"
#include "UltraCanvasPathUtf8.h"

#include <UltraDatabase/UltraDatabase.h>

#include <cstdio>
#include <filesystem>
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
    REQUIRE(ContactCollector::CollectSender(s, "Kickstarter", "no-reply@kickstarter.com"));

    std::vector<Contact> services;
    REQUIRE(s.ListBySection(ContactSection::Services, services).success);
    REQUIRE_EQ(services.size(), (size_t)1);
    REQUIRE_EQ(services.front().organization, std::string("Kickstarter"));
    REQUIRE(services.front().notes.find("Crowdfunding platform") != std::string::npos);
    REQUIRE_EQ(services.front().PrimaryEmail(), std::string("no-reply@kickstarter.com"));

    // A robot display name is replaced by the service's own name only when it
    // is missing; a real one is kept.
    REQUIRE(ContactCollector::CollectSender(s, "", "hello@buymeacoffee.com"));
    REQUIRE(s.ListBySection(ContactSection::Services, services).success);
    bool named = false;
    for (const auto& c : services)
        if (c.PrimaryEmail() == "hello@buymeacoffee.com")
            named = (c.displayName == "Buy Me a Coffee");
    REQUIRE(named);
}

TEST(collect_sender_leaves_ordinary_addresses_in_other) {
    ContactStore s = FreshStore("collect-other");
    REQUIRE(ContactCollector::CollectSender(s, "Anna Schmidt", "anna@example.com"));

    std::vector<Contact> other;
    REQUIRE(s.ListBySection(ContactSection::Other, other).success);
    REQUIRE_EQ(other.size(), (size_t)1);
    REQUIRE_EQ(other.front().displayName, std::string("Anna Schmidt"));
    REQUIRE(other.front().organization.empty());

    // A personal mailbox is not a service, however big the provider is.
    REQUIRE(ContactCollector::CollectSender(s, "Uncle Bob", "bob@gmail.com"));
    REQUIRE(s.ListBySection(ContactSection::Other, other).success);
    REQUIRE_EQ(other.size(), (size_t)2);

    std::vector<Contact> services;
    REQUIRE(s.ListBySection(ContactSection::Services, services).success);
    REQUIRE(services.empty());
}

TEST(collect_sender_never_reclassifies_an_existing_contact) {
    ContactStore s = FreshStore("collect-existing");
    // The user filed this address under Friends themselves; a later collect of
    // the same address must not move it into Services.
    Contact mine = MakeContact("My Kickstarter account", ContactSection::Friends,
                               "no-reply@kickstarter.com");
    REQUIRE(s.Save(mine).success);

    REQUIRE(!ContactCollector::CollectSender(s, "Kickstarter", "no-reply@kickstarter.com"));
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
// contact per new address however often it came, none for an address the
// address book holds already, each filed as CollectSender files one.
TEST(collect_senders_adds_each_new_address_once) {
    ContactStore s = FreshStore("collect-batch");
    Contact mine = MakeContact("Anna (mine)", ContactSection::Friends, "anna@example.com");
    REQUIRE(s.Save(mine).success);

    const std::vector<std::pair<std::string, std::string>> senders = {
        {"Anna Schmidt", "Anna@Example.com"},          // in the book, in other letters
        {"Bob", "bob@example.org"},
        {"Robert", "BOB@example.org"},                 // Bob again: the first name stays
        {"Kickstarter", "no-reply@kickstarter.com"},   // a known service
        {"Nobody", ""},                                // no address: nothing to keep
        {"", "carol@example.net"},                     // no name: the address stands in
    };
    REQUIRE_EQ(ContactCollector::CollectSenders(s, senders), 3);
    REQUIRE_EQ(CountFor(s, ContactSection::Friends), 1);   // Anna untouched

    std::vector<Contact> services;
    REQUIRE(s.ListBySection(ContactSection::Services, services).success);
    REQUIRE_EQ(services.size(), (size_t)1);
    REQUIRE_EQ(services.front().organization, std::string("Kickstarter"));

    std::vector<Contact> other;
    REQUIRE(s.ListBySection(ContactSection::Other, other).success);
    REQUIRE_EQ(other.size(), (size_t)2);
    REQUIRE_EQ(other[0].displayName, std::string("Bob"));   // sorted by name
    REQUIRE_EQ(other[0].PrimaryEmail(), std::string("bob@example.org"));
    REQUIRE_EQ(other[1].displayName, std::string("carol@example.net"));

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
