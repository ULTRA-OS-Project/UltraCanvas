// Tests/UltraOtpTests.cpp
// Unit tests for the HOTP / TOTP engine and the otpauth:// URI parser.
//
// The published vectors are the point of this suite: dynamic truncation
// (RFC 4226 §5.3) has well-known off-by-one and sign traps, and a subtly wrong
// implementation produces plausible six-digit numbers that simply never match
// the server. Only the vectors catch that.
//
// Self-contained: no test framework, no UI stack.
//
// Author: UltraCanvas Framework / ULTRA OS
#include "../Apps/UltraAuthenticator/otp/OtpAuthUri.h"
#include "../Apps/UltraAuthenticator/otp/UltraOtp.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::Otp;

static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

static void CheckEqual(const std::string& actual, const std::string& expected,
                       const std::string& what) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("  FAIL: %s\n    expected %s\n    got      %s\n",
                    what.c_str(), expected.c_str(), actual.c_str());
    }
}

static UltraCryptSecureBuffer SecretFromAscii(const std::string& text) {
    return UltraCryptSecureBuffer(text.data(), text.size());
}

// ===== RFC 4226 Appendix D — HOTP, secret "12345678901234567890", 6 digits =====
static void TestHotpVectors() {
    std::printf("HOTP (RFC 4226 Appendix D)\n");
    const UltraCryptSecureBuffer secret = SecretFromAscii("12345678901234567890");

    Parameters params;
    params.type      = Type::Hotp;
    params.algorithm = Algorithm::SHA1;
    params.digits    = 6;

    const char* expected[10] = {
        "755224", "287082", "359152", "969429", "338314",
        "254676", "287922", "162583", "399871", "520489"
    };

    for (uint64_t counter = 0; counter < 10; ++counter) {
        std::string code;
        auto r = GenerateHotp(secret, params, counter, code);
        Check(static_cast<bool>(r),
              "HOTP counter " + std::to_string(counter) + " generates: " + r.message);
        CheckEqual(code, expected[counter],
                   "RFC 4226 vector for counter " + std::to_string(counter));
    }
}

// ===== RFC 6238 Appendix B — TOTP, 8 digits, per-algorithm seeds =====
static void TestTotpVectors() {
    std::printf("TOTP (RFC 6238 Appendix B)\n");

    // The RFC's seeds are the ASCII digits repeated to each algorithm's block
    // size. Getting these wrong is the single most common reason an otherwise
    // correct implementation fails the SHA-256/SHA-512 vectors.
    const UltraCryptSecureBuffer seedSha1 =
        SecretFromAscii("12345678901234567890");
    const UltraCryptSecureBuffer seedSha256 =
        SecretFromAscii("12345678901234567890123456789012");
    const UltraCryptSecureBuffer seedSha512 =
        SecretFromAscii("1234567890123456789012345678901234567890"
                        "123456789012345678901234");

    struct Vector {
        int64_t time;
        const char* sha1;
        const char* sha256;
        const char* sha512;
    };
    const Vector vectors[] = {
        {         59, "94287082", "46119246", "90693936"},
        { 1111111109, "07081804", "68084774", "25091201"},
        { 1111111111, "14050471", "67062674", "99943326"},
        { 1234567890, "89005924", "91819424", "93441116"},
        { 2000000000, "69279037", "90698825", "38618901"},
        {20000000000, "65353130", "77737706", "47863826"},
    };

    Parameters params;
    params.type          = Type::Totp;
    params.digits        = 8;
    params.periodSeconds = 30;

    for (const auto& v : vectors) {
        const std::string at = "T=" + std::to_string(v.time);
        std::string code;

        params.algorithm = Algorithm::SHA1;
        auto r1 = GenerateTotp(seedSha1, params, v.time, code);
        Check(static_cast<bool>(r1), "TOTP-SHA1 " + at + " generates: " + r1.message);
        CheckEqual(code, v.sha1, "RFC 6238 SHA-1 vector at " + at);

        params.algorithm = Algorithm::SHA256;
        auto r2 = GenerateTotp(seedSha256, params, v.time, code);
        Check(static_cast<bool>(r2), "TOTP-SHA256 " + at + " generates: " + r2.message);
        CheckEqual(code, v.sha256, "RFC 6238 SHA-256 vector at " + at);

        params.algorithm = Algorithm::SHA512;
        auto r3 = GenerateTotp(seedSha512, params, v.time, code);
        Check(static_cast<bool>(r3), "TOTP-SHA512 " + at + " generates: " + r3.message);
        CheckEqual(code, v.sha512, "RFC 6238 SHA-512 vector at " + at);
    }
}

// ===== Time stepping =====
static void TestTimeStepping() {
    std::printf("Time stepping\n");
    uint64_t counter = 0;

    Check(TimeStepCounter(0, 30, counter) && counter == 0, "T=0 is step 0");
    Check(TimeStepCounter(29, 30, counter) && counter == 0, "T=29 is still step 0");
    Check(TimeStepCounter(30, 30, counter) && counter == 1, "T=30 is step 1");
    Check(TimeStepCounter(59, 30, counter) && counter == 1, "T=59 is step 1 (RFC vector)");
    Check(TimeStepCounter(1111111109, 30, counter) && counter == 0x23523EC,
          "T=1111111109 is step 0x23523EC");
    Check(!TimeStepCounter(-1, 30, counter), "a time before the epoch is refused");
    Check(!TimeStepCounter(100, 0, counter), "a zero period is refused");

    Check(SecondsRemaining(0, 30) == 30, "a fresh step has the full period left");
    Check(SecondsRemaining(1, 30) == 29, "one second in leaves 29");
    Check(SecondsRemaining(29, 30) == 1, "the last second leaves 1, never 0");
    Check(SecondsRemaining(30, 30) == 30, "the next step resets the countdown");
    Check(SecondsRemaining(0, 0) == 0, "an invalid period reports unknown");

    // A code must be stable across its whole step and change at the boundary.
    const UltraCryptSecureBuffer secret = SecretFromAscii("12345678901234567890");
    Parameters params;
    params.digits = 6;
    std::string a, b, c;
    GenerateTotp(secret, params, 30, a);
    GenerateTotp(secret, params, 59, b);
    GenerateTotp(secret, params, 60, c);
    CheckEqual(b, a, "the code is stable within a time step");
    Check(c != a, "the code changes at the step boundary");
}

// ===== Parameter and secret validation =====
static void TestValidation() {
    std::printf("Parameter validation\n");
    const UltraCryptSecureBuffer secret = SecretFromAscii("12345678901234567890");
    std::string code;

    Parameters params;
    params.digits = 5;
    Check(!GenerateTotp(secret, params, 0, code), "5 digits is refused");
    params.digits = 9;
    Check(!GenerateTotp(secret, params, 0, code), "9 digits is refused");

    params = Parameters();
    params.periodSeconds = 10;
    Check(!GenerateTotp(secret, params, 0, code), "a 10-second period is refused");
    params.periodSeconds = 121;
    Check(!GenerateTotp(secret, params, 0, code), "a 121-second period is refused");

    params = Parameters();
    Check(!GenerateTotp(secret, params, -1, code), "a negative time is refused");

    // A short secret must be refused: RFC 4226 §4 requires 128 bits.
    const UltraCryptSecureBuffer shortSecret = SecretFromAscii("short");
    Check(!GenerateTotp(shortSecret, params, 0, code),
          "a secret below 128 bits is refused");
    Check(code.empty(), "a refused generation produces no code");

    // Codes must carry their leading zeros.
    Check(static_cast<bool>(GenerateHotp(secret, [] {
              Parameters p; p.type = Type::Hotp; p.digits = 8; return p;
          }(), 0, code)), "8-digit HOTP generates");
    Check(code.size() == 8, "an 8-digit code is exactly 8 characters");

    Parameters sixDigit;
    sixDigit.type = Type::Hotp;
    GenerateHotp(secret, sixDigit, 1, code);
    CheckEqual(code, "287082", "a 6-digit code keeps its width");
}

// ===== otpauth:// parsing =====
static void TestUriParsing() {
    std::printf("otpauth:// parsing\n");
    Parameters params;
    UltraCryptSecureBuffer secret;

    // Canonical Key URI example.
    const std::string uri =
        "otpauth://totp/Example:alice@google.com"
        "?secret=JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP&issuer=Example";
    auto r = ParseOtpAuthUri(uri, params, secret);
    Check(static_cast<bool>(r), "the canonical example parses: " + r.message);
    Check(params.type == Type::Totp, "type is totp");
    CheckEqual(params.issuer, "Example", "issuer");
    CheckEqual(params.accountName, "alice@google.com", "account name");
    Check(params.digits == 6, "digits defaults to 6");
    Check(params.periodSeconds == 30, "period defaults to 30");
    Check(params.algorithm == Algorithm::SHA1, "algorithm defaults to SHA1");
    Check(secret.GetSize() == 20, "the 32-character Base32 secret decodes to 20 bytes");

    // Percent-encoded label, explicit parameters.
    auto r2 = ParseOtpAuthUri(
        "otpauth://totp/ACME%20Co%3Ajohn%40example.com"
        "?secret=JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP&issuer=ACME%20Co"
        "&algorithm=SHA256&digits=8&period=60",
        params, secret);
    Check(static_cast<bool>(r2), "a percent-encoded label parses: " + r2.message);
    CheckEqual(params.issuer, "ACME Co", "decoded issuer");
    CheckEqual(params.accountName, "john@example.com", "decoded account name");
    Check(params.algorithm == Algorithm::SHA256, "explicit algorithm");
    Check(params.digits == 8, "explicit digits");
    Check(params.periodSeconds == 60, "explicit period");

    // HOTP requires a counter.
    auto r3 = ParseOtpAuthUri(
        "otpauth://hotp/Example:alice?secret=JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP&counter=7",
        params, secret);
    Check(static_cast<bool>(r3), "an hotp URI with a counter parses: " + r3.message);
    Check(params.type == Type::Hotp && params.counter == 7, "counter is read");

    auto r4 = ParseOtpAuthUri(
        "otpauth://hotp/Example:alice?secret=JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP",
        params, secret);
    Check(!r4, "an hotp URI without a counter is refused");

    // A label with no issuer prefix.
    auto r5 = ParseOtpAuthUri(
        "otpauth://totp/alice@example.com?secret=JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP",
        params, secret);
    Check(static_cast<bool>(r5), "a bare account label parses");
    Check(params.issuer.empty(), "no issuer is reported when none is given");
    CheckEqual(params.accountName, "alice@example.com", "bare account name");
}

// ===== otpauth:// rejection =====
static void TestUriRejection() {
    std::printf("otpauth:// rejection\n");
    Parameters params;
    UltraCryptSecureBuffer secret;

    auto rejected = [&](const std::string& uri, const std::string& what) {
        Parameters p;
        UltraCryptSecureBuffer s;
        auto r = ParseOtpAuthUri(uri, p, s);
        Check(!r, what + " is refused");
        Check(s.IsEmpty(), what + " leaves no secret behind");
    };

    const std::string goodSecret = "secret=JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP";

    rejected("", "an empty URI");
    rejected("https://example.com/?" + goodSecret, "a non-otpauth scheme");
    // '1', '8', '9' and '0' are outside the RFC 4648 Base32 alphabet (A-Z, 2-7).
    // Note lowercase letters ARE accepted, deliberately — see below.
    rejected("otpauth://totp/a?" + goodSecret + "1", "a bad Base32 secret");
    rejected("otpauth://totp/a?secret=JBSWY3DP!HPK3PXPJBSWY3DPEHPK3PXP",
             "a punctuation character in the secret");
    rejected("otpauth://totp/alice", "a URI with no secret");
    rejected("otpauth://totp/alice?secret=", "an empty secret");
    rejected("otpauth://totp/alice?secret=JBSWY3DP", "a secret below 128 bits");
    rejected("otpauth://steam/alice?" + goodSecret, "an unknown OTP type");
    rejected("otpauth://totp?" + goodSecret, "a URI with no label");
    rejected("otpauth://totp/?" + goodSecret, "a URI with an empty label");
    rejected("otpauth://totp/alice?" + goodSecret + "&digits=9", "9 digits");
    rejected("otpauth://totp/alice?" + goodSecret + "&digits=0", "0 digits");
    rejected("otpauth://totp/alice?" + goodSecret + "&digits=six", "a non-numeric digit count");
    rejected("otpauth://totp/alice?" + goodSecret + "&period=0", "a zero period");
    rejected("otpauth://totp/alice?" + goodSecret + "&period=3600", "a one-hour period");
    rejected("otpauth://totp/alice?" + goodSecret + "&algorithm=MD5", "an MD5 algorithm");
    rejected("otpauth://hotp/alice?" + goodSecret + "&counter=abc", "a non-numeric counter");
    rejected("otpauth://totp/alice?secret=%2", "a truncated percent escape");

    // The migration format is a different encoding entirely and must not be
    // mistaken for a Key URI.
    rejected("otpauth-migration://offline?data=AAAA", "an account-migration URI");

    // A hostile label must not be able to inject control characters into the UI.
    rejected("otpauth://totp/alice%0Aevil?" + goodSecret, "a newline in the label");
    rejected("otpauth://totp/alice%00evil?" + goodSecret, "a NUL in the label");
    rejected("otpauth://totp/alice?" + goodSecret + "&issuer=A%0DB", "a CR in the issuer");

    // Ambiguity is refused rather than silently resolved.
    rejected("otpauth://totp/alice?" + goodSecret + "&" + goodSecret,
             "two secrets");
    rejected("otpauth://totp/Example:alice?" + goodSecret + "&issuer=Different",
             "an issuer that disagrees with the label");

    rejected("otpauth://totp/" + std::string(300, 'a') + "?" + goodSecret,
             "an over-long label");
    rejected("otpauth://totp/alice?" + goodSecret + "&extra=" +
                 std::string(5000, 'x'),
             "an over-long URI");

    // Lowercase and spaced secrets are accepted on purpose: issuers routinely
    // print seeds in lowercase, grouped for legibility, and users retype them.
    Parameters lower;
    UltraCryptSecureBuffer lowerSecret;
    auto rl = ParseOtpAuthUri(
        "otpauth://totp/alice?secret=jbswy3dpehpk3pxpjbswy3dpehpk3pxp",
        lower, lowerSecret);
    Check(static_cast<bool>(rl), "a lowercase secret is accepted: " + rl.message);
    Check(lowerSecret.GetSize() == 20, "a lowercase secret decodes identically");

    // An unknown parameter must be ignored, not rejected: a future extension
    // should still provision.
    auto r = ParseOtpAuthUri(
        "otpauth://totp/alice?" + goodSecret + "&futureoption=1", params, secret);
    Check(static_cast<bool>(r), "an unknown parameter is ignored: " + r.message);
}

// ===== Round trip =====
static void TestUriRoundTrip() {
    std::printf("otpauth:// round trip\n");
    Parameters original;
    original.type          = Type::Totp;
    original.algorithm     = Algorithm::SHA256;
    original.digits        = 8;
    original.periodSeconds = 60;
    original.issuer        = "ACME Co";
    original.accountName   = "john@example.com";

    const UltraCryptSecureBuffer secret = SecretFromAscii("12345678901234567890");

    std::string uri;
    auto built = BuildOtpAuthUri(original, secret, uri);
    Check(static_cast<bool>(built), "the URI builds: " + built.message);
    Check(uri.rfind("otpauth://totp/", 0) == 0, "the built URI has the right prefix");

    Parameters parsed;
    UltraCryptSecureBuffer parsedSecret;
    auto r = ParseOtpAuthUri(uri, parsed, parsedSecret);
    Check(static_cast<bool>(r), "the built URI parses back: " + r.message);
    Check(parsed.type == original.type, "type round-trips");
    Check(parsed.algorithm == original.algorithm, "algorithm round-trips");
    Check(parsed.digits == original.digits, "digits round-trip");
    Check(parsed.periodSeconds == original.periodSeconds, "period round-trips");
    CheckEqual(parsed.issuer, original.issuer, "issuer round-trips");
    CheckEqual(parsed.accountName, original.accountName, "account name round-trips");
    Check(UltraCrypt_ConstantTimeEquals(parsedSecret.Data(), parsedSecret.GetSize(),
                                        secret.Data(), secret.GetSize()),
          "the secret round-trips exactly");

    // The same code must come out of both ends of the round trip.
    std::string before, after;
    GenerateTotp(secret, original, 1234567890, before);
    GenerateTotp(parsedSecret, parsed, 1234567890, after);
    CheckEqual(after, before, "the round-tripped account yields the same code");

    // HOTP carries its counter.
    Parameters hotp;
    hotp.type        = Type::Hotp;
    hotp.counter     = 42;
    hotp.accountName = "alice";
    std::string hotpUri;
    Check(static_cast<bool>(BuildOtpAuthUri(hotp, secret, hotpUri)),
          "an hotp URI builds");
    Parameters parsedHotp;
    UltraCryptSecureBuffer hotpSecret;
    Check(static_cast<bool>(ParseOtpAuthUri(hotpUri, parsedHotp, hotpSecret)),
          "an hotp URI parses back");
    Check(parsedHotp.counter == 42, "the counter round-trips");

    // Building must refuse what parsing would refuse.
    Parameters bad;
    bad.accountName = "alice";
    bad.digits = 9;
    std::string unused;
    Check(!BuildOtpAuthUri(bad, secret, unused), "building refuses 9 digits");
    Parameters noAccount;
    Check(!BuildOtpAuthUri(noAccount, secret, unused),
          "building refuses a missing account name");
}

int main() {
    std::printf("UltraOtp tests — backend: %s\n\n",
                UltraCrypt_GetBackendName().c_str());

    if (!UltraCrypt_IsAvailable()) {
        std::printf("FAIL: no crypto backend; OTP codes cannot be generated.\n");
        return 1;
    }

    TestHotpVectors();
    TestTotpVectors();
    TestTimeStepping();
    TestValidation();
    TestUriParsing();
    TestUriRejection();
    TestUriRoundTrip();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
