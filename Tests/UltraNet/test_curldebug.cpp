// Tests/UltraNet/test_curldebug.cpp
// Unit tests for the opt-in curl wire-trace redaction rule
// (ultranet_curldebug::ShouldRedact). The security property under test: an
// outbound AUTH / SASL line — which carries the XOAUTH2 bearer token or the
// account password — must never be logged, while ordinary commands and every
// inbound server response are kept so the trace is actually useful for
// diagnosing where an SMTP / IMAP session fails.
#include "test_framework.h"

#include <UltraNet/UltraNetCurlDebug.h>

using ultranet_curldebug::ShouldRedact;
#include <cstdlib>
#include <string>

using ultranet_curldebug::Redact;
using ultranet_curldebug::Requested;

namespace {

// setenv/unsetenv are POSIX; _putenv_s is the Windows spelling.
void SetDebugEnv(const char* value) {
#ifdef _WIN32
    _putenv_s("ULTRANET_CURL_DEBUG", value ? value : "");
#else
    if (value) setenv("ULTRANET_CURL_DEBUG", value, 1);
    else       unsetenv("ULTRANET_CURL_DEBUG");
#endif
}

struct EnvGuard {
    std::string saved;
    bool had = false;
    EnvGuard() {
        if (const char* v = std::getenv("ULTRANET_CURL_DEBUG")) {
            saved = v;
            had = true;
        }
    }
    ~EnvGuard() { SetDebugEnv(had ? saved.c_str() : nullptr); }
};

} // namespace

TEST(CurlDebug_OffUnlessAskedFor) {
    EnvGuard guard;
    SetDebugEnv(nullptr);
    REQUIRE(!Requested());
    SetDebugEnv("");
    REQUIRE(!Requested());
}

TEST(CurlDebug_RespectsOffSpellings) {
    EnvGuard guard;
    for (const char* off : {"0", "false", "no", "off"}) {
        SetDebugEnv(off);
        CHECK(!Requested());
    }
    for (const char* on : {"1", "true", "yes", "verbose"}) {
        SetDebugEnv(on);
        CHECK(Requested());
    }
}

TEST(CurlDebug_RedactsSmtpAuth) {
    // The mechanism stays: it says which step failed.
    REQUIRE_EQ(Redact("AUTH PLAIN AGVyaWthAHMzY3JldA=="),
               std::string("AUTH PLAIN <redacted>"));
    // Nothing secret on this one - it is the mechanism handshake.
    REQUIRE_EQ(Redact("AUTH LOGIN"), std::string("AUTH LOGIN"));
}

TEST(curldebug_redacts_outbound_auth) {
    // The AUTH command and its base64 SASL initial response (client -> server).
    CHECK(ShouldRedact(CURLINFO_HEADER_OUT,
                       "AUTH XOAUTH2 dXNlcj1mb29AYmFyLmNvbQFhdXRoPUJlYXJlciB4"));
    CHECK(ShouldRedact(CURLINFO_HEADER_OUT, "AUTH LOGIN"));
    CHECK(ShouldRedact(CURLINFO_HEADER_OUT, "A001 AUTHENTICATE XOAUTH2"));  // IMAP form
    // A bare base64 continuation has no keyword, but is far longer than any real
    // SMTP/IMAP command — the length rule catches it.
    const std::string blob(200, 'x');
    CHECK(ShouldRedact(CURLINFO_HEADER_OUT, blob));
TEST(CurlDebug_RedactsTheBareBase64OfAuthLogin) {
    // The password arrives on a line of its own, with no keyword to match.
    REQUIRE_EQ(Redact("cGFzc3dvcmQxMjM="), std::string("<redacted>"));
}

TEST(curldebug_keeps_outbound_commands) {
    CHECK(!ShouldRedact(CURLINFO_HEADER_OUT, "EHLO client.example.com"));
    CHECK(!ShouldRedact(CURLINFO_HEADER_OUT, "MAIL FROM:<erika@example.com>"));
    CHECK(!ShouldRedact(CURLINFO_HEADER_OUT, "RCPT TO:<bob@example.net>"));
    CHECK(!ShouldRedact(CURLINFO_HEADER_OUT, "DATA"));
    CHECK(!ShouldRedact(CURLINFO_HEADER_OUT, "QUIT"));
TEST(CurlDebug_RedactsImapAndPop3Credentials) {
    REQUIRE_EQ(Redact("a003 LOGIN erika s3cret"),
               std::string("a003 LOGIN <redacted>"));
    REQUIRE_EQ(Redact("AUTHENTICATE XOAUTH2 dXNlcj1lcmlrYQE="),
               std::string("AUTHENTICATE XOAUTH2 <redacted>"));
    REQUIRE_EQ(Redact("PASS s3cret"), std::string("PASS <redacted>"));
}

TEST(curldebug_keeps_all_inbound_responses) {
    // Server responses are never redacted — even when they mention AUTH — because
    // they are exactly what a trace is read for. The EHLO capability line and the
    // "334" AUTH error challenge must survive.
    CHECK(!ShouldRedact(CURLINFO_HEADER_IN, "220 smtp.gmail.com ESMTP ready"));
    CHECK(!ShouldRedact(CURLINFO_HEADER_IN, "250-smtp.gmail.com at your service"));
    CHECK(!ShouldRedact(CURLINFO_HEADER_IN, "250-AUTH LOGIN PLAIN XOAUTH2"));
    CHECK(!ShouldRedact(CURLINFO_HEADER_IN, "334 eyJzdGF0dXMiOiI0MDEiLCJzY2hlbWVz"));
    CHECK(!ShouldRedact(CURLINFO_HEADER_IN, "535-5.7.8 Username and Password not accepted"));
TEST(CurlDebug_RedactsAnAuthorizationHeader) {
    REQUIRE_EQ(Redact("Authorization: Bearer ya29.a0AfH6"),
               std::string("Authorization: <redacted>"));
}

TEST(curldebug_keeps_curl_commentary) {
    // curl's own text (connect / TLS handshake notes) — the most useful lines for
    // spotting a TLS-vs-plaintext-port mismatch — are kept.
    CHECK(!ShouldRedact(CURLINFO_TEXT, "Trying 142.250.1.108:465..."));
    CHECK(!ShouldRedact(CURLINFO_TEXT, "SSL connection using TLSv1.3 / AEAD-CHACHA20"));
TEST(CurlDebug_KeepsTheServerReplyThatExplainsTheFailure) {
    // The whole point of the trace: these lines must survive intact.
    const std::string syntax = "555 5.5.2 Syntax error, goodbye";
    const std::string auth   = "535 5.7.8 Username and Password not accepted";
    const std::string greet  = "220 smtp.example.com ESMTP ready";
    REQUIRE_EQ(Redact(syntax), syntax);
    REQUIRE_EQ(Redact(auth), auth);
    REQUIRE_EQ(Redact(greet), greet);
}

TEST(CurlDebug_KeepsOrdinaryCommands) {
    const std::string mailFrom = "MAIL FROM:<erika@example.com>";
    const std::string rcptTo   = "RCPT TO:<info@example.com>";
    const std::string select   = "a004 SELECT INBOX";
    REQUIRE_EQ(Redact(mailFrom), mailFrom);
    REQUIRE_EQ(Redact(rcptTo), rcptTo);
    REQUIRE_EQ(Redact(select), select);
}

TEST(CurlDebug_EnableIsANoOpWithoutAHandle) {
    EnvGuard guard;
    SetDebugEnv("1");
    ultranet_curldebug::EnableIfRequested(nullptr);   // must not crash
    REQUIRE(true);
}
