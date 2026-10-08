// Tests/UltraNet/test_curl_debug.cpp
// The redaction rule of the ULTRANET_CURL_VERBOSE mail trace
// (UltraNet/UltraNetCurlDebug.h). A trace is meant to be pasted into a bug
// report as it stands, so no line we send that carries a secret may be
// printed - and until 0.2.0 two did: POP3's "PASS secret" and IMAP's
// "A001 LOGIN user secret", the plain sign-ins libcurl uses when a server
// offers no SASL. Neither has "AUTH" in it, which was all the rule looked for.
#include "test_framework.h"

#include <UltraNet/UltraNetCurlDebug.h>

#include <string>

using ultranet_curldebug::ShouldRedact;

TEST(curl_debug_redacts_the_sasl_exchange) {
    REQUIRE(ShouldRedact(CURLINFO_HEADER_OUT, "AUTH PLAIN AGVyaWthAHNlY3JldA=="));
    REQUIRE(ShouldRedact(CURLINFO_HEADER_OUT, "A001 AUTHENTICATE XOAUTH2 dXNlcj1lcmlrYQ=="));
    // A bare base64 continuation has no keyword; its length gives it away.
    REQUIRE(ShouldRedact(CURLINFO_HEADER_OUT, std::string(160, 'Q')));
}

TEST(curl_debug_redacts_pop3_pass) {
    REQUIRE(ShouldRedact(CURLINFO_HEADER_OUT, "PASS hunter2"));
    REQUIRE(ShouldRedact(CURLINFO_HEADER_OUT, "pass hunter2"));
    // The user name is not a secret, and the step is worth seeing.
    REQUIRE(!ShouldRedact(CURLINFO_HEADER_OUT, "USER erika"));
}

TEST(curl_debug_redacts_imap_login) {
    REQUIRE(ShouldRedact(CURLINFO_HEADER_OUT, "A001 LOGIN erika hunter2"));
    REQUIRE(ShouldRedact(CURLINFO_HEADER_OUT, "a1 login \"erika\" \"hunter2\""));
    REQUIRE(ShouldRedact(CURLINFO_HEADER_OUT, "LOGIN erika hunter2"));
}

TEST(curl_debug_keeps_ordinary_commands_and_every_reply) {
    REQUIRE(!ShouldRedact(CURLINFO_HEADER_OUT, "A002 SELECT INBOX"));
    REQUIRE(!ShouldRedact(CURLINFO_HEADER_OUT, "LIST"));
    REQUIRE(!ShouldRedact(CURLINFO_HEADER_OUT, "MAIL FROM:<erika@example.org>"));
    // What the server says is what a trace is read for - the mechanisms it
    // offers and its answer to the sign-in included.
    REQUIRE(!ShouldRedact(CURLINFO_HEADER_IN, "250-AUTH PLAIN LOGIN XOAUTH2"));
    REQUIRE(!ShouldRedact(CURLINFO_HEADER_IN, "-ERR invalid password"));
    REQUIRE(!ShouldRedact(CURLINFO_HEADER_IN, "A001 NO [AUTHENTICATIONFAILED] LOGIN failed"));
    REQUIRE(!ShouldRedact(CURLINFO_TEXT, "Connected to mail.example.org port 993"));
}
