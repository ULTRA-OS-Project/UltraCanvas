// Tests/UltraAuthenticatorAccountTests.cpp
// Unit tests for the authenticator's account layer.
//
// This layer sits between the UI and the vault, so the properties under test
// are about what it refuses and what it never exposes: no seed reaches the
// caller, an existing account is never silently replaced, an HOTP counter
// advances exactly once per code and survives a reopen, and a stored account
// round-trips through the otpauth:// canonical form without losing anything.
//
// Self-contained: no test framework, no UI stack.
//
// Author: UltraCanvas Framework / ULTRA OS
#include "../Apps/UltraAuthenticator/AccountStore.h"
#include "../Apps/UltraAuthenticator/otp/OtpAuthUri.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::Authenticator;

static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

static UltraCryptSecureBuffer Buf(const std::string& text) {
    return UltraCryptSecureBuffer(text.data(), text.size());
}

static std::string TempVaultPath(const char* name) {
    std::filesystem::path p =
        std::filesystem::temp_directory_path() /
        ("ultraauth-acct-" + std::string(name) + ".vault");
    std::error_code ec;
    std::filesystem::remove(p, ec);
    return p.string();
}

// RFC 6238's test seed, "12345678901234567890" in Base32.
static const char* kSeedB32 = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";

static std::string TotpUri(const std::string& issuer,
                           const std::string& account) {
    return "otpauth://totp/" + issuer + ":" + account +
           "?secret=" + kSeedB32 + "&issuer=" + issuer;
}

// ===========================================================================

static void TestAddAndList() {
    std::printf("Add and list\n");
    const std::string path = TempVaultPath("addlist");

    AccountStore store;
    Check(store.Create(path, Buf("correct horse battery staple")),
          "vault created");
    Check(store.IsOpen(), "store reports open");

    std::string key;
    Check(store.AddFromUri(TotpUri("Example", "alice@example.com"), key),
          "first account added");
    Check(key == "Example:alice@example.com", "key is issuer:account");

    Check(store.AddFromUri(TotpUri("Acme", "bob"), key), "second account added");
    Check(store.Count() == 2, "two accounts stored");

    std::vector<Account> accounts;
    Check(store.List(accounts), "list succeeded");
    Check(accounts.size() == 2, "list returned both accounts");
    // Sorted by key: "Acme:bob" before "Example:alice@example.com".
    Check(accounts[0].key == "Acme:bob", "list is sorted by key");
    Check(accounts[0].params.issuer == "Acme", "issuer survived the round trip");
    Check(accounts[0].params.accountName == "bob", "account name survived");
    Check(accounts[1].params.digits == Otp::kDefaultDigits,
          "digits defaulted to 6");
    Check(accounts[1].params.periodSeconds == Otp::kDefaultPeriod,
          "period defaulted to 30");
    Check(accounts[1].params.type == Otp::Type::Totp, "type is TOTP");

    store.Close();
    std::filesystem::remove(path);
}

static void TestNoSilentReplace() {
    std::printf("Duplicate rejection\n");
    const std::string path = TempVaultPath("dup");

    AccountStore store;
    Check(store.Create(path, Buf("pw")), "vault created");

    std::string key;
    Check(store.AddFromUri(TotpUri("Example", "alice"), key), "added once");

    // A second add under the same label must fail rather than overwrite: an
    // overwrite destroys a second factor.
    StoreResult again = store.AddFromUri(TotpUri("Example", "alice"), key);
    Check(!again, "duplicate add refused");
    Check(again.code == StoreResultCode::AlreadyExists,
          "duplicate reported as AlreadyExists");
    Check(store.Count() == 1, "the original account is still the only one");

    // Remove then add is the deliberate path, and it works.
    Check(store.Remove("Example:alice"), "removed");
    Check(store.Count() == 0, "removal took effect");
    Check(store.AddFromUri(TotpUri("Example", "alice"), key),
          "re-added after explicit removal");

    store.Close();
    std::filesystem::remove(path);
}

static void TestCodeGeneration() {
    std::printf("Code generation\n");
    const std::string path = TempVaultPath("codes");

    AccountStore store;
    Check(store.Create(path, Buf("pw")), "vault created");
    std::string key;
    Check(store.AddFromUri(TotpUri("Example", "alice"), key), "account added");

    // RFC 6238 appendix B: T = 59 with the SHA-1 seed gives 94287082, whose
    // low 6 digits are 287082.
    std::string code;
    uint32_t remaining = 0;
    Check(store.GenerateTotp(key, 59, code, remaining), "generated at T=59");
    Check(code == "287082", "matches the RFC 6238 vector at T=59");
    Check(remaining == 1, "1 second left in the step at T=59");

    Check(store.GenerateTotp(key, 1111111109, code, remaining),
          "generated at T=1111111109");
    Check(code == "081804", "matches the RFC 6238 vector at T=1111111109");

    // The same step yields the same code; the next step does not.
    std::string a, b;
    uint32_t ra = 0, rb = 0;
    Check(store.GenerateTotp(key, 1111111109, a, ra), "generated again in-step");
    Check(store.GenerateTotp(key, 1111111140, b, rb), "generated in next step");
    Check(a == code, "code is stable within a time step");
    Check(a != b, "code changes across a time step");

    // A missing account is NotFound, not a crash or an empty code.
    StoreResult missing = store.GenerateTotp("nope", 59, code, remaining);
    Check(!missing, "unknown account rejected");
    Check(missing.code == StoreResultCode::NotFound, "reported as NotFound");

    store.Close();
    std::filesystem::remove(path);
}

static void TestHotpCounterAdvances() {
    std::printf("HOTP counter\n");
    const std::string path = TempVaultPath("hotp");

    AccountStore store;
    Check(store.Create(path, Buf("pw")), "vault created");

    std::string key;
    Check(store.AddFromUri("otpauth://hotp/Example:carol?secret=" +
                               std::string(kSeedB32) + "&counter=0",
                           key),
          "HOTP account added");

    // RFC 4226 appendix D, counters 0..2 with the same seed.
    std::string code;
    Check(store.AdvanceHotp(key, code), "generated at counter 0");
    Check(code == "755224", "matches the RFC 4226 vector at counter 0");
    Check(store.AdvanceHotp(key, code), "generated at counter 1");
    Check(code == "287082", "matches the RFC 4226 vector at counter 1");
    Check(store.AdvanceHotp(key, code), "generated at counter 2");
    Check(code == "359152", "matches the RFC 4226 vector at counter 2");

    // The advanced counter must be durable: a spent code must not come back
    // after the app restarts.
    store.Close();
    AccountStore reopened;
    Check(reopened.Open(path, Buf("pw")), "vault reopened");
    Check(reopened.AdvanceHotp(key, code), "generated after reopen");
    Check(code == "969429", "counter survived the reopen (vector at counter 3)");

    reopened.Close();
    std::filesystem::remove(path);
}

static void TestTypeMismatch() {
    std::printf("Type mismatch\n");
    const std::string path = TempVaultPath("mismatch");

    AccountStore store;
    Check(store.Create(path, Buf("pw")), "vault created");

    std::string totpKey, hotpKey;
    Check(store.AddFromUri(TotpUri("Example", "alice"), totpKey), "TOTP added");
    Check(store.AddFromUri("otpauth://hotp/Example:carol?secret=" +
                               std::string(kSeedB32) + "&counter=0",
                           hotpKey),
          "HOTP added");

    std::string code;
    uint32_t remaining = 0;
    // Asking for the wrong kind of code must be refused, not silently
    // answered with a meaningless one.
    Check(!store.GenerateTotp(hotpKey, 59, code, remaining),
          "TOTP generation refused for an HOTP account");
    Check(!store.AdvanceHotp(totpKey, code),
          "HOTP advance refused for a TOTP account");

    store.Close();
    std::filesystem::remove(path);
}

static void TestRejectsBadUris() {
    std::printf("URI rejection\n");
    const std::string path = TempVaultPath("badüri");

    AccountStore store;
    Check(store.Create(path, Buf("pw")), "vault created");

    std::string key;
    const char* bad[] = {
        "",
        "https://example.com",
        "otpauth://totp/Example:alice",                       // no secret
        "otpauth://totp/Example:alice?secret=1111",           // not Base32
        "otpauth://totp/Example:alice?secret=GEZDGNBV",       // too short
        "otpauth://hotp/Example:carol?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ",
                                                              // hotp, no counter
        "otpauth-migration://offline?data=AAAA",              // different format
    };
    for (const char* uri : bad) {
        StoreResult res = store.AddFromUri(uri, key);
        Check(!res, std::string("rejected: '") + uri + "'");
    }
    Check(store.Count() == 0, "nothing hostile was stored");

    store.Close();
    std::filesystem::remove(path);
}

static void TestSeedNeverOnDisk() {
    std::printf("Seed confidentiality\n");
    const std::string path = TempVaultPath("ondisk");

    AccountStore store;
    Check(store.Create(path, Buf("pw")), "vault created");
    std::string key;
    Check(store.AddFromUri(TotpUri("Example", "alice@example.com"), key),
          "account added");
    store.Close();

    std::ifstream in(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    Check(!bytes.empty(), "vault file is non-empty");
    // Neither the Base32 seed, nor the URI, nor even the label may be legible.
    Check(bytes.find(kSeedB32) == std::string::npos,
          "the Base32 seed does not appear in the file");
    Check(bytes.find("otpauth://") == std::string::npos,
          "the provisioning URI does not appear in the file");
    Check(bytes.find("alice@example.com") == std::string::npos,
          "the account label does not appear in the file");

    std::filesystem::remove(path);
}

static void TestClosedStore() {
    std::printf("Closed store\n");
    AccountStore store;
    std::string key, code;
    uint32_t remaining = 0;
    std::vector<Account> accounts;

    // Every entry point must fail closed before a vault is open, rather than
    // returning empty results that look like "no accounts".
    Check(!store.IsOpen(), "starts closed");
    Check(!store.AddFromUri(TotpUri("Example", "alice"), key), "add refused");
    Check(!store.List(accounts), "list refused");
    Check(!store.GenerateTotp("Example:alice", 59, code, remaining),
          "generate refused");
    Check(!store.AdvanceHotp("Example:alice", code), "advance refused");
}

int main() {
    std::printf("UltraAuthenticator account-layer tests — backend: %s\n\n",
                UltraCrypt_IsAvailable() ? UltraCrypt_GetBackendName().c_str()
                                         : "none");
    if (!UltraCrypt_IsAvailable()) {
        std::printf("FAIL: no crypto backend; the account layer cannot operate.\n");
        return 1;
    }

    TestAddAndList();
    TestNoSilentReplace();
    TestCodeGeneration();
    TestHotpCounterAdvances();
    TestTypeMismatch();
    TestRejectsBadUris();
    TestSeedNeverOnDisk();
    TestClosedStore();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
