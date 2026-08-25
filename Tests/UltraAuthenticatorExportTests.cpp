// Tests/UltraAuthenticatorExportTests.cpp
// Unit tests for the encrypted backup file (AccountExport.h, §3.5).
//
// A backup is the file most likely to leave the machine, so the properties
// under test are mostly about what it refuses: it must not open with the
// master password, must not be readable at all without its own passphrase,
// must not leak a seed to anyone who finds it, and must not silently overwrite
// a live second factor when restored.
//
// Self-contained: no test framework, no UI stack.
//
// Author: UltraCanvas Framework / ULTRA OS
#include "../Apps/UltraAuthenticator/AccountStore.h"
#include "../Apps/UltraAuthenticator/AccountExport.h"

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

static std::string TempPath(const char* name) {
    std::filesystem::path p = std::filesystem::temp_directory_path() /
                              ("ultraauth-export-" + std::string(name));
    std::error_code ec;
    std::filesystem::remove(p, ec);
    return p.string();
}

static const char* kSeedB32 = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";

static std::string TotpUri(const std::string& issuer,
                           const std::string& account) {
    return "otpauth://totp/" + issuer + ":" + account +
           "?secret=" + kSeedB32 + "&issuer=" + issuer;
}

// Builds a vault with three accounts and returns its path.
static std::string MakeVault(const char* name, const std::string& password) {
    const std::string path = TempPath(name);
    AccountStore store;
    if (!store.Create(path, Buf(password))) return std::string();
    std::string key;
    store.AddFromUri(TotpUri("Example", "alice@example.com"), key);
    store.AddFromUri(TotpUri("GitHub", "bob@example.com"), key);
    store.AddFromUri(TotpUri("Legacy", "carol@example.com"), key);
    store.Close();
    return path;
}

static std::vector<uint8_t> ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

// ===========================================================================

static void TestRoundTrip() {
    std::printf("Export and restore\n");
    const std::string vaultPath  = MakeVault("rt.vault", "master-pw");
    const std::string exportPath = TempPath("rt.ucaexport");
    Check(!vaultPath.empty(), "vault built");

    size_t exported = 0;
    {
        AccountStore store;
        Check(store.Open(vaultPath, Buf("master-pw")), "vault opens");
        Check(store.ExportAll(Buf("master-pw"), Buf("backup-pass"), exportPath,
                              exported),
              "export succeeds");
        Check(exported == 3, "all three accounts exported");
        store.Close();
    }

    // Restore into a brand-new, empty vault — the "new laptop" case.
    const std::string restorePath = TempPath("rt-restore.vault");
    {
        AccountStore store;
        Check(store.Create(restorePath, Buf("different-master")),
              "fresh vault created");

        AccountStore::ImportSummary summary;
        Check(store.ImportAll(Buf("backup-pass"), exportPath, summary),
              "import succeeds");
        Check(summary.added == 3, "three accounts restored");
        Check(summary.skippedExisting == 0, "nothing skipped");
        Check(summary.rejected == 0, "nothing rejected");
        Check(store.Count() == 3, "the vault holds three accounts");

        // The restored account must generate the same code as the original,
        // which is the only proof the seed survived the round trip intact.
        std::string code;
        uint32_t remaining = 0;
        Check(store.GenerateTotp("Example:alice@example.com", 59, code,
                                 remaining),
              "a restored account generates");
        Check(code == "287082",
              "and produces the RFC 6238 vector for this seed at t=59");
        store.Close();
    }

    std::filesystem::remove(vaultPath);
    std::filesystem::remove(exportPath);
    std::filesystem::remove(restorePath);
}

// The rule from §3.5: a backup must not open with the device password.
static void TestRefusesMasterPasswordAsPassphrase() {
    std::printf("The backup passphrase must differ from the master password\n");
    const std::string vaultPath  = MakeVault("same.vault", "master-pw");
    const std::string exportPath = TempPath("same.ucaexport");

    AccountStore store;
    Check(store.Open(vaultPath, Buf("master-pw")), "vault opens");

    size_t exported = 0;
    StoreResult res = store.ExportAll(Buf("master-pw"), Buf("master-pw"),
                                      exportPath, exported);
    Check(!res, "reusing the master password is refused");
    Check(res.code == StoreResultCode::InvalidArgument, "reported as InvalidArgument");
    Check(!std::filesystem::exists(exportPath), "and no file is written");

    store.Close();
    std::filesystem::remove(vaultPath);
}

static void TestWrongPassphraseAndTampering() {
    std::printf("Wrong passphrase and tampering\n");
    const std::string vaultPath  = MakeVault("bad.vault", "master-pw");
    const std::string exportPath = TempPath("bad.ucaexport");

    {
        AccountStore store;
        Check(store.Open(vaultPath, Buf("master-pw")), "vault opens");
        size_t exported = 0;
        Check(store.ExportAll(Buf("master-pw"), Buf("backup-pass"), exportPath,
                              exported),
              "export succeeds");
        store.Close();
    }

    std::vector<UltraCryptSecureBuffer> uris;
    StoreResult wrong = OpenAccountExport(exportPath, Buf("not-the-pass"), uris);
    Check(!wrong, "a wrong passphrase is refused");
    Check(wrong.code == StoreResultCode::AuthenticationFailed,
          "reported as AuthenticationFailed");
    Check(uris.empty(), "nothing comes back");

    // Flip one byte in the ciphertext: must be refused, and with the same
    // message as a wrong passphrase so neither can be distinguished.
    const std::vector<uint8_t> original = ReadFile(exportPath);
    Check(original.size() > 40, "the file has content");
    {
        std::vector<uint8_t> tampered = original;
        tampered[tampered.size() - 1] ^= 0x01;
        std::ofstream out(exportPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(tampered.data()),
                  static_cast<std::streamsize>(tampered.size()));
    }
    StoreResult altered = OpenAccountExport(exportPath, Buf("backup-pass"), uris);
    Check(!altered, "a modified file is refused");
    Check(altered.message == wrong.message,
          "and is indistinguishable from a wrong passphrase");

    // The header is associated data, so editing the version byte must also
    // fail the tag rather than being obeyed.
    {
        std::vector<uint8_t> tampered = original;
        tampered[8] = 99;              // format version
        std::ofstream out(exportPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(tampered.data()),
                  static_cast<std::streamsize>(tampered.size()));
    }
    Check(!OpenAccountExport(exportPath, Buf("backup-pass"), uris),
          "an edited header is refused");

    std::filesystem::remove(vaultPath);
    std::filesystem::remove(exportPath);
}

// The point of encrypting the backup: nothing readable on disk.
static void TestNoSeedOnDisk() {
    std::printf("Nothing readable in the file\n");
    const std::string vaultPath  = MakeVault("leak.vault", "master-pw");
    const std::string exportPath = TempPath("leak.ucaexport");

    AccountStore store;
    Check(store.Open(vaultPath, Buf("master-pw")), "vault opens");
    size_t exported = 0;
    Check(store.ExportAll(Buf("master-pw"), Buf("backup-pass"), exportPath,
                          exported),
          "export succeeds");
    store.Close();

    const std::vector<uint8_t> raw = ReadFile(exportPath);
    const std::string text(raw.begin(), raw.end());
    Check(text.find(kSeedB32) == std::string::npos, "the seed is not present");
    Check(text.find("otpauth://") == std::string::npos, "no URI is present");
    Check(text.find("alice@example.com") == std::string::npos,
          "not even an account name is present");
    Check(text.find("backup-pass") == std::string::npos,
          "and neither is the passphrase");

    std::filesystem::remove(vaultPath);
    std::filesystem::remove(exportPath);
}

// Restoring over a live vault must never destroy a working second factor.
static void TestImportNeverOverwrites() {
    std::printf("Import keeps existing accounts\n");
    const std::string vaultPath  = MakeVault("merge.vault", "master-pw");
    const std::string exportPath = TempPath("merge.ucaexport");

    {
        AccountStore store;
        Check(store.Open(vaultPath, Buf("master-pw")), "vault opens");
        size_t exported = 0;
        Check(store.ExportAll(Buf("master-pw"), Buf("backup-pass"), exportPath,
                              exported),
              "export succeeds");
        store.Close();
    }

    // Import the backup straight back into the vault it came from: every
    // account already exists, so all three must be skipped, not replaced.
    {
        AccountStore store;
        Check(store.Open(vaultPath, Buf("master-pw")), "vault reopens");
        AccountStore::ImportSummary summary;
        Check(store.ImportAll(Buf("backup-pass"), exportPath, summary),
              "import succeeds");
        Check(summary.added == 0, "nothing added");
        Check(summary.skippedExisting == 3, "all three skipped as existing");
        Check(store.Count() == 3, "the vault is unchanged in size");
        store.Close();
    }

    // A vault holding one of the three merges the other two.
    const std::string partialPath = TempPath("partial.vault");
    {
        AccountStore store;
        Check(store.Create(partialPath, Buf("other-pw")), "partial vault created");
        std::string key;
        Check(store.AddFromUri(TotpUri("GitHub", "bob@example.com"), key),
              "one account pre-existing");

        AccountStore::ImportSummary summary;
        Check(store.ImportAll(Buf("backup-pass"), exportPath, summary),
              "import succeeds");
        Check(summary.added == 2, "the two new accounts are added");
        Check(summary.skippedExisting == 1, "the existing one is skipped");
        Check(store.Count() == 3, "the vault now holds three");
        store.Close();
    }

    std::filesystem::remove(vaultPath);
    std::filesystem::remove(exportPath);
    std::filesystem::remove(partialPath);
}

// A hostile or truncated file must be refused rather than read past its end.
static void TestRejectsMalformedFiles() {
    std::printf("Malformed files are refused\n");
    const std::string path = TempPath("junk.ucaexport");
    std::vector<UltraCryptSecureBuffer> uris;

    Check(!OpenAccountExport("/no/such/file", Buf("pass"), uris),
          "a missing file is refused");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const char junk[] = "not a backup at all, just some bytes here";
        out.write(junk, sizeof(junk) - 1);
    }
    StoreResult notOurs = OpenAccountExport(path, Buf("pass"), uris);
    Check(!notOurs, "a file with the wrong magic is refused");
    Check(notOurs.code == StoreResultCode::Corrupt, "reported as Corrupt");

    // Correct magic, nothing after it.
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const char header[] = {'U','C','A','E','X','P','R','T', 1, 0};
        out.write(header, sizeof(header));
    }
    Check(!OpenAccountExport(path, Buf("pass"), uris),
          "a header with no envelope is refused");

    Check(!SealAccountExport({}, Buf(""), path),
          "an empty passphrase is refused when sealing");

    std::filesystem::remove(path);
}

// An empty vault is a legitimate thing to back up.
static void TestEmptyVaultRoundTrips() {
    std::printf("An empty vault exports and restores\n");
    const std::string vaultPath  = TempPath("empty.vault");
    const std::string exportPath = TempPath("empty.ucaexport");

    AccountStore store;
    Check(store.Create(vaultPath, Buf("master-pw")), "vault created");
    size_t exported = 0;
    Check(store.ExportAll(Buf("master-pw"), Buf("backup-pass"), exportPath,
                          exported),
          "export succeeds");
    Check(exported == 0, "zero accounts exported");

    AccountStore::ImportSummary summary;
    Check(store.ImportAll(Buf("backup-pass"), exportPath, summary),
          "import succeeds");
    Check(summary.added == 0 && summary.skippedExisting == 0 &&
              summary.rejected == 0,
          "and does nothing");
    store.Close();

    std::filesystem::remove(vaultPath);
    std::filesystem::remove(exportPath);
}

static void TestClosedStore() {
    std::printf("A closed store refuses both\n");
    AccountStore store;
    size_t exported = 0;
    AccountStore::ImportSummary summary;
    StoreResult e = store.ExportAll(Buf("a"), Buf("b"), TempPath("x"), exported);
    Check(!e && e.code == StoreResultCode::NotOpen, "export refuses");
    StoreResult i = store.ImportAll(Buf("b"), TempPath("x"), summary);
    Check(!i && i.code == StoreResultCode::NotOpen, "import refuses");
}

int main() {
    std::printf("UltraAuthenticator export tests — backend: %s\n\n",
                UltraCrypt_IsAvailable() ? UltraCrypt_GetBackendName().c_str()
                                         : "none");
    if (!UltraCrypt_IsAvailable()) {
        std::printf("FAIL: no crypto backend; backups cannot be written.\n");
        return 1;
    }

    TestRoundTrip();
    TestRefusesMasterPasswordAsPassphrase();
    TestWrongPassphraseAndTampering();
    TestNoSeedOnDisk();
    TestImportNeverOverwrites();
    TestRejectsMalformedFiles();
    TestEmptyVaultRoundTrips();
    TestClosedStore();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
