// Tests/UltraNet/test_mailaddr.cpp
// Covers ultranet_mailaddr: turning an address as a user writes it into the
// addr-spec the SMTP envelope (MAIL FROM / RCPT TO) takes.
//
// The case that matters is the display name: passing "Erika <erika@x>" through
// to MAIL FROM produces "<Erika <erika@x>>", which Gmail answers with
// 555 5.5.2 Syntax error and the mail never leaves.
#include "test_framework.h"

#include <UltraNet/UltraNetMailAddr.h>

#include <string>

using ultranet_mailaddr::AddrSpec;
using ultranet_mailaddr::EnvelopeAddr;

TEST(MailAddr_StripsDisplayName) {
    REQUIRE_EQ(AddrSpec("Erika Froehling <erika@example.com>"),
               std::string("erika@example.com"));
    REQUIRE_EQ(EnvelopeAddr("Erika Froehling <erika@example.com>"),
               std::string("<erika@example.com>"));
}

TEST(MailAddr_PassesPlainAddressThrough) {
    REQUIRE_EQ(AddrSpec("erika@example.com"), std::string("erika@example.com"));
    REQUIRE_EQ(EnvelopeAddr("erika@example.com"),
               std::string("<erika@example.com>"));
}

TEST(MailAddr_DoesNotDoubleTheBrackets) {
    REQUIRE_EQ(EnvelopeAddr("<erika@example.com>"),
               std::string("<erika@example.com>"));
}

TEST(MailAddr_QuotedDisplayNameWithPunctuation) {
    // A comma in the phrase must be quoted, and the quotes must not end up in
    // the envelope.
    REQUIRE_EQ(AddrSpec("\"Froehling, Erika\" <erika@example.com>"),
               std::string("erika@example.com"));
}

TEST(MailAddr_DisplayNameHoldingAnAddress) {
    // Mail clients write this when the contact's name IS an address. The last
    // '<' starts the real angle-addr; an earlier one belongs to the phrase.
    REQUIRE_EQ(AddrSpec("\"erika@old.example\" <erika@example.com>"),
               std::string("erika@example.com"));
    REQUIRE_EQ(AddrSpec("Erika <erika@old.example> <erika@example.com>"),
               std::string("erika@example.com"));
}

TEST(MailAddr_TrimsSurroundingSpace) {
    REQUIRE_EQ(AddrSpec("  erika@example.com  "),
               std::string("erika@example.com"));
    REQUIRE_EQ(AddrSpec("Erika < erika@example.com >"),
               std::string("erika@example.com"));
}

TEST(MailAddr_EmptyIsTheNullReversePath) {
    // RFC 5321 4.5.5: "MAIL FROM:<>" is how a bounce is sent, so an empty
    // address must stay empty rather than become a malformed one.
    REQUIRE_EQ(AddrSpec(""), std::string(""));
    REQUIRE_EQ(EnvelopeAddr(""), std::string("<>"));
}

TEST(MailAddr_UnclosedBracketIsNotTreatedAsAnAngleAddr) {
    // Nothing sensible to extract: pass it on and let the server judge it,
    // rather than inventing an address from half a line.
    REQUIRE_EQ(AddrSpec("Erika <erika@example.com"),
               std::string("Erika <erika@example.com"));
}
