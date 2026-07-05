// Tests/UltraMail/test_contacts.cpp
// Exercises the contact store: sectioned storage (Friends/Work/Leisure/
// Services), emails/phones round-trip, section counts, search, update and
// remove. Each test uses its own in-memory database.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailContactStore.h"

#include <string>

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
    REQUIRE(ContactSectionFromString("services") == ContactSection::Services);
    REQUIRE(ContactSectionFromString("nonsense") == ContactSection::Other);
    REQUIRE_EQ(DisplayName(ContactSection::Leisure), std::string("Leisure"));
    REQUIRE_EQ(PrimarySections().size(), (size_t)4);
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
