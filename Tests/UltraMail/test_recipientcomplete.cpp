// Tests/UltraMail/test_recipientcomplete.cpp
// Address completion for the compose window's To / Cc / Bcc fields.
// Version: 0.2.0 - ranked by how often each address is written to
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraMailRecipientComplete.h"

#include <map>
#include <string>
#include <vector>

using namespace UltraMail;

namespace {

Contact MakeContact(const std::string& name, const std::vector<std::string>& addresses,
                    const std::string& org = "") {
    Contact c;
    c.displayName = name;
    c.organization = org;
    for (const auto& a : addresses) {
        ContactEmail e;
        e.address = a;
        c.emails.push_back(e);
    }
    return c;
}

} // namespace

TEST(recipient_query_is_the_part_after_the_last_comma) {
    REQUIRE_EQ(RecipientQuery("an"), std::string("an"));
    REQUIRE_EQ(RecipientQuery("bob@example.com,  an "), std::string("an"));
    REQUIRE(RecipientQuery("bob@example.com, ").empty());
}

TEST(complete_recipient_keeps_the_earlier_ones) {
    REQUIRE_EQ(CompleteRecipient("an", "Anna <anna@example.com>"),
               std::string("Anna <anna@example.com>, "));
    REQUIRE_EQ(CompleteRecipient("bob@example.com, an", "Anna <anna@example.com>"),
               std::string("bob@example.com, Anna <anna@example.com>, "));
}

TEST(suggest_recipients_matches_names_and_addresses) {
    const std::vector<Contact> contacts = {
        MakeContact("Anna Schmidt", {"anna@example.com", "anna.s@work.example"}),
        MakeContact("Hans Annaberg", {"hans@example.com"}),
        MakeContact("Joanna Lee", {"jo@example.com"}),
        MakeContact("Doe, John", {"john@example.com"}),
        MakeContact("Support", {"help@shop.example"}, "Anna's Shop"),
    };
    auto s = SuggestRecipients(contacts, "ann", "");
    // Word starts first (Anna's two addresses, Annaberg, the shop's
    // organization), then "contains" (Joanna).
    REQUIRE(s.size() == 5);
    REQUIRE_EQ(s[0].recipient, std::string("Anna Schmidt <anna@example.com>"));
    REQUIRE_EQ(s[1].address, std::string("anna.s@work.example"));
    REQUIRE_EQ(s[4].address, std::string("jo@example.com"));

    // A name with a comma would split the field: the address goes in alone.
    s = SuggestRecipients(contacts, "john", "");
    REQUIRE(s.size() == 1);
    REQUIRE_EQ(s[0].recipient, std::string("john@example.com"));

    // By address, case-insensitive.
    s = SuggestRecipients(contacts, "HELP", "");
    REQUIRE(s.size() == 1);
    REQUIRE_EQ(s[0].recipient, std::string("Support <help@shop.example>"));

    // Already in the field: left out.
    s = SuggestRecipients(contacts, "ann", "Anna Schmidt <ANNA@example.com>, ann");
    REQUIRE(s.size() == 4);
    REQUIRE_EQ(s[0].address, std::string("anna.s@work.example"));

    REQUIRE(SuggestRecipients(contacts, "", "").empty());
    REQUIRE(SuggestRecipients(contacts, "ann", "", 2).size() == 2);
}

TEST(suggest_recipients_puts_the_most_written_to_first) {
    const std::vector<Contact> contacts = {
        MakeContact("Anna Schmidt", {"anna@example.com"}),
        MakeContact("Hans Annaberg", {"hans@example.com"}),
        MakeContact("Joanna Lee", {"jo@example.com"}),
    };
    const std::map<std::string, int> written = {
        {"jo@example.com", 12}, {"hans@example.com", 3}};
    const auto s = SuggestRecipients(contacts, "ann", "", 8, &written);
    REQUIRE(s.size() == 3);
    REQUIRE_EQ(s[0].address, std::string("jo@example.com"));     // 12, though only "contains"
    REQUIRE_EQ(s[1].address, std::string("hans@example.com"));   // 3
    REQUIRE_EQ(s[2].address, std::string("anna@example.com"));   // never written to
    // The limit keeps the most written-to.
    REQUIRE(SuggestRecipients(contacts, "ann", "", 1, &written)[0].address == "jo@example.com");
}
