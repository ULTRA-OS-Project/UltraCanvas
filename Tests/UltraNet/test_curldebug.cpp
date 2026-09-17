// Tests/UltraNet/test_curldebug.cpp
// Covers ultranet_curldebug: the opt-in libcurl trace used to debug a mail
// account that will not connect.
//
// Two properties are worth pinning. It is OFF unless asked for - a trace is a
// user's mail session. And what it prints is redacted: a trace pasted into a
// bug report must not carry the password, which for AUTH LOGIN arrives as a
// bare base64 line with no keyword on it to match.
#include "test_framework.h"

#include <UltraNet/UltraNetCurlDebug.h>

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

TEST(CurlDebug_RedactsTheBareBase64OfAuthLogin) {
    // The password arrives on a line of its own, with no keyword to match.
    REQUIRE_EQ(Redact("cGFzc3dvcmQxMjM="), std::string("<redacted>"));
}

TEST(CurlDebug_RedactsImapAndPop3Credentials) {
    REQUIRE_EQ(Redact("a003 LOGIN erika s3cret"),
               std::string("a003 LOGIN <redacted>"));
    REQUIRE_EQ(Redact("AUTHENTICATE XOAUTH2 dXNlcj1lcmlrYQE="),
               std::string("AUTHENTICATE XOAUTH2 <redacted>"));
    REQUIRE_EQ(Redact("PASS s3cret"), std::string("PASS <redacted>"));
}

TEST(CurlDebug_RedactsAnAuthorizationHeader) {
    REQUIRE_EQ(Redact("Authorization: Bearer ya29.a0AfH6"),
               std::string("Authorization: <redacted>"));
}

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
