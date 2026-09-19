// Tests/UltraNet/test_mailaddr.cpp
// Unit tests for ultranet_mailaddr::EnvelopeAddr — the SMTP envelope reverse/
// forward-path builder. It must yield a bare addr-spec in angle brackets (no
// display name), because CURLOPT_MAIL_FROM / CURLOPT_MAIL_RCPT are RFC 5321
// paths. Regression cover for the Gmail "555 5.5.2 Syntax error" caused by
// passing "Name <addr>" straight through as "<Name <addr>>".
#include "test_framework.h"

#include <UltraNet/UltraNetMailAddr.h>

using ultranet_mailaddr::EnvelopeAddr;

TEST(mailaddr_strips_display_name) {
    // The exact input that broke Gmail send.
    REQUIRE_EQ(EnvelopeAddr("nat <nataly.mart0@gmail.com>"),
               std::string("<nataly.mart0@gmail.com>"));
    REQUIRE_EQ(EnvelopeAddr("Erika Example <erika@example.com>"),
               std::string("<erika@example.com>"));
}

TEST(mailaddr_passes_bare_and_angled) {
    REQUIRE_EQ(EnvelopeAddr("erika@example.com"),
               std::string("<erika@example.com>"));
    REQUIRE_EQ(EnvelopeAddr("<erika@example.com>"),
               std::string("<erika@example.com>"));
}

TEST(mailaddr_trims_whitespace) {
    REQUIRE_EQ(EnvelopeAddr("  erika@example.com  "),
               std::string("<erika@example.com>"));
    REQUIRE_EQ(EnvelopeAddr("Erika < erika@example.com >"),
               std::string("<erika@example.com>"));
}

TEST(mailaddr_handles_quoted_display_name_with_bracket) {
    // A display name containing '<' must not fool the extractor: the addr-spec is
    // between the LAST '<' and its following '>'.
    REQUIRE_EQ(EnvelopeAddr("\"weird <guy>\" <real@example.com>"),
               std::string("<real@example.com>"));
}

TEST(mailaddr_empty_input) {
    REQUIRE_EQ(EnvelopeAddr(""), std::string("<>"));
}
