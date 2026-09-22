// Tests/UltraVaultTests.cpp
// Unit tests for the UltraVault module. Self-contained: no test framework, no
// UI stack, so it runs headless anywhere UltraVault builds.
//
// Coverage:
//  - memory backend: CRUD round trip, overwrite, NotFound, key validation,
//    prefix listing, lifecycle (Locked before Initialize, after Shutdown)
//  - file backend (skipped when UltraCrypt has no libsodium backend):
//    persistence across Shutdown/Initialize, wrong passphrase and a tampered
//    file both reported as AccessDenied with identical messages (no oracle),
//    fresh-vault creation, Delete persistence
//  - DeviceKeyVault (same skip rule): locked until unlocked, device-key
//    auto-unlock creates key + vault and a second instance reopens them,
//    profile-prefixed keys, no plaintext on disk, wrong / empty passphrase,
//    OAuth token sets beside the password slot, 0.1-format migration, and
//    the "vault exists but no device key" case that must prompt
//
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraVault/UltraVault.h"
#include "UltraVault/UltraVaultDeviceKeyVault.h"
#include "UltraCrypt/UltraCryptCore.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

static void TestMemoryBackend() {
    std::printf("memory backend\n");

    // Closed vault: operations report Locked, listing is empty.
    UltraVault::Shutdown();
    UltraVault::SecretValue value;
    Check(UltraVault::Get("ai.test.api_key", value).code ==
              UltraVault::ResultCode::Locked,
          "Get before Initialize is Locked");
    Check(!UltraVault::IsAvailable(), "closed vault is not available");
    Check(UltraVault::List().empty(), "closed vault lists nothing");

    UltraVault::Config config;
    config.backend = UltraVault::Backend::Memory;
    Check(UltraVault::Initialize(config).IsOk(), "memory Initialize");
    Check(UltraVault::IsAvailable(), "open vault is available");
    Check(UltraVault::GetBackendName() == "memory", "backend name");

    // Round trip with mime type.
    auto secret = UltraVault::SecretValue::FromString("sk-live-123");
    Check(UltraVault::Put("ai.anthropic.api_key", secret).IsOk(), "Put");
    Check(UltraVault::Get("ai.anthropic.api_key", value).IsOk(), "Get");
    Check(value.AsString() == "sk-live-123", "value round trip");
    Check(value.mimeType == "text/plain", "mime type round trip");

    // Overwrite.
    Check(UltraVault::Put("ai.anthropic.api_key",
                          UltraVault::SecretValue::FromString("sk-live-456"))
              .IsOk(),
          "overwrite Put");
    UltraVault::Get("ai.anthropic.api_key", value);
    Check(value.AsString() == "sk-live-456", "overwrite visible");

    // Missing keys and validation.
    Check(UltraVault::Get("ai.missing.key", value).code ==
              UltraVault::ResultCode::NotFound,
          "missing key is NotFound");
    Check(UltraVault::Put("has space", secret).code ==
              UltraVault::ResultCode::InvalidKey,
          "whitespace key rejected");
    Check(UltraVault::Put("", secret).code ==
              UltraVault::ResultCode::InvalidKey,
          "empty key rejected");

    // Prefix listing, sorted.
    UltraVault::Put("ai.openai.api_key",
                    UltraVault::SecretValue::FromString("sk-oa"));
    UltraVault::Put("ssh.id_ed25519",
                    UltraVault::SecretValue::FromString("key"));
    auto aiKeys = UltraVault::List("ai.");
    Check(aiKeys.size() == 2, "prefix listing count");
    Check(aiKeys.size() == 2 && aiKeys[0] == "ai.anthropic.api_key" &&
              aiKeys[1] == "ai.openai.api_key",
          "prefix listing sorted");
    Check(UltraVault::List().size() == 3, "full listing count");

    // Delete.
    Check(UltraVault::Delete("ssh.id_ed25519").IsOk(), "Delete");
    Check(UltraVault::Get("ssh.id_ed25519", value).code ==
              UltraVault::ResultCode::NotFound,
          "deleted key is NotFound");
    Check(UltraVault::Delete("ssh.id_ed25519").code ==
              UltraVault::ResultCode::NotFound,
          "double Delete is NotFound");

    // v0.1 stubs answer honestly instead of pretending.
    Check(!UltraVault::Import("/tmp/nothing").IsOk(), "Import is a stub");
    Check(!UltraVault::PromptUserForSecret("k", "p").IsOk(),
          "PromptUserForSecret is a stub");

    UltraVault::Shutdown();
    Check(UltraVault::Get("ai.anthropic.api_key", value).code ==
              UltraVault::ResultCode::Locked,
          "Get after Shutdown is Locked");
}

static void TestFileBackend(const std::string& vaultPath) {
    std::printf("file backend\n");
    std::remove(vaultPath.c_str());

    auto openVault = [&vaultPath](const std::string& passphrase) {
        UltraVault::Config config;
        config.backend    = UltraVault::Backend::File;
        config.filePath   = vaultPath;
        config.passphrase = passphrase;
        return UltraVault::Initialize(config);
    };

    // Fresh vault: opens with no file, persists on first Put.
    Check(openVault("correct horse").IsOk(), "fresh vault opens");
    Check(UltraVault::GetBackendName() == "file", "backend name");
    Check(UltraVault::Put("ai.anthropic.api_key",
                          UltraVault::SecretValue::FromString("sk-persist"))
              .IsOk(),
          "Put persists");
    UltraVault::Shutdown();

    // Reopen with the right passphrase: the secret survived.
    Check(openVault("correct horse").IsOk(), "reopen with right passphrase");
    UltraVault::SecretValue value;
    Check(UltraVault::Get("ai.anthropic.api_key", value).IsOk() &&
              value.AsString() == "sk-persist",
          "secret survives reopen");

    // Delete persists too.
    UltraVault::Put("ai.tmp.key", UltraVault::SecretValue::FromString("x"));
    Check(UltraVault::Delete("ai.tmp.key").IsOk(), "Delete");
    UltraVault::Shutdown();
    openVault("correct horse");
    Check(UltraVault::Get("ai.tmp.key", value).code ==
              UltraVault::ResultCode::NotFound,
          "Delete survives reopen");
    UltraVault::Shutdown();

    // Wrong passphrase: AccessDenied.
    UltraVault::Result wrongPass = openVault("wrong pass");
    Check(wrongPass.code == UltraVault::ResultCode::AccessDenied,
          "wrong passphrase is AccessDenied");
    Check(!UltraVault::IsAvailable(), "vault stays closed on AccessDenied");

    // Tampered file: same code, same message — no oracle.
    {
        std::fstream f(vaultPath,
                       std::ios::in | std::ios::out | std::ios::binary);
        f.seekg(0, std::ios::end);
        const long size = static_cast<long>(f.tellg());
        f.seekp(size - 1);
        char last = 0;
        f.seekg(size - 1);
        f.read(&last, 1);
        f.seekp(size - 1);
        last = static_cast<char>(last ^ 0x01);
        f.write(&last, 1);
    }
    UltraVault::Result tampered = openVault("correct horse");
    Check(tampered.code == UltraVault::ResultCode::AccessDenied,
          "tampered file is AccessDenied");
    Check(tampered.message == wrongPass.message,
          "wrong passphrase and tampering are indistinguishable");

    std::remove(vaultPath.c_str());
}

// ---- DeviceKeyVault ---------------------------------------------------------

namespace fs = std::filesystem;

static std::string ReadFileBytes(const fs::path& path) {
    std::ifstream is(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(is)),
                       std::istreambuf_iterator<char>());
}

// RFC 4648 Base64, enough to write a 0.1-format vault for the migration test.
static std::string TestBase64(const std::string& in) {
    static const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    while (i + 2 < in.size()) {
        const uint32_t n = (static_cast<uint8_t>(in[i]) << 16) |
                           (static_cast<uint8_t>(in[i + 1]) << 8) |
                           static_cast<uint8_t>(in[i + 2]);
        out += alphabet[(n >> 18) & 63]; out += alphabet[(n >> 12) & 63];
        out += alphabet[(n >> 6) & 63];  out += alphabet[n & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        const uint32_t n = static_cast<uint8_t>(in[i]) << 16;
        out += alphabet[(n >> 18) & 63]; out += alphabet[(n >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        const uint32_t n = (static_cast<uint8_t>(in[i]) << 16) |
                           (static_cast<uint8_t>(in[i + 1]) << 8);
        out += alphabet[(n >> 18) & 63]; out += alphabet[(n >> 12) & 63];
        out += alphabet[(n >> 6) & 63];  out += '=';
    }
    return out;
}

static const UltraVault::DeviceKeyVaultProfile kTestProfile{"test.vault", "test.app."};

static void TestDeviceKeyVault(const fs::path& dir) {
    using UltraVault::DeviceKeyVault;
    using UltraVault::UnlockStatus;
    std::printf("device-key vault\n");
    std::error_code ec;
    fs::remove_all(dir, ec);

    // Locked until unlocked, and a locked vault refuses rather than drops.
    {
        DeviceKeyVault vault(dir.string(), kTestProfile);
        Check(!vault.IsUnlocked(), "starts locked");
        Check(!vault.Store("erika", "pw"), "Store while locked is refused");
        std::string got;
        Check(!vault.Retrieve("erika", got) && got.empty(), "Retrieve while locked");
        Check(!vault.Has("erika"), "Has while locked");
        Check(vault.Unlock("") == UnlockStatus::WrongPassphrase,
              "empty passphrase is refused");
        Check(!vault.IsUnlocked(), "still locked after empty passphrase");
        Check(vault.KeyFor("erika") == "test.app.erika", "KeyFor uses the profile prefix");
        Check(vault.VaultPath() == (dir / "test.vault").string(), "VaultPath uses the profile");
    }

    // A default-constructed vault has nowhere to go.
    {
        DeviceKeyVault none;
        Check(!none.TryAutoUnlock(), "no directory: auto-unlock fails");
        Check(none.Unlock("x") == UnlockStatus::IoError, "no directory: Unlock is IoError");
        Check(!none.PersistDeviceKey("x"), "no directory: no device key");
    }

    // First run: auto-unlock generates a device key and creates the vault.
    std::string secretOnDisk;
    {
        DeviceKeyVault vault(dir.string(), kTestProfile);
        Check(vault.TryAutoUnlock(), "fresh directory auto-unlocks");
        Check(vault.IsUnlocked(), "unlocked after auto-unlock");
        Check(fs::exists(dir / "device.key", ec), "device.key written");
        Check(ReadFileBytes(dir / "device.key").size() == 64, "device key is 32 random bytes as hex");
        Check(vault.TryAutoUnlock(), "auto-unlock is idempotent while open");

        Check(!vault.Has("erika"), "empty vault has nothing");
        Check(vault.Store("erika", "s3cr3t-p@ss"), "Store");
        Check(vault.Has("erika"), "Has after Store");
        std::string got;
        Check(vault.Retrieve("erika", got) && got == "s3cr3t-p@ss", "Retrieve round trip");
        Check(vault.Exists(), "vault file exists after the first write");
        Check(ReadFileBytes(vault.VaultPath()).find("s3cr3t-p@ss") == std::string::npos,
              "plaintext secret is not on disk");
        Check(!fs::exists(dir / "vault.key", ec), "the 0.1 key file is not written");

        // Stored under the namespaced key, visible through UltraVault itself.
        UltraVault::SecretValue raw;
        Check(UltraVault::Get("test.app.erika", raw).IsOk() && raw.AsString() == "s3cr3t-p@ss",
              "secret lives under the profile-prefixed UltraVault key");
        vault.Lock();
        Check(!vault.IsUnlocked(), "locked after Lock");
        Check(!vault.Has("erika"), "nothing readable after Lock");
    }

    // Second instance: the stored device key unlocks it and the secret is there.
    {
        DeviceKeyVault vault(dir.string(), kTestProfile);
        Check(vault.Exists(), "vault file persists");
        Check(vault.TryAutoUnlock(), "device key reopens the vault");
        std::string got;
        Check(vault.Retrieve("erika", got) && got == "s3cr3t-p@ss", "secret survives reopen");

        // OAuth tokens beside the password slot: one sign-in method per account.
        Check(vault.MethodFor("erika") == UltraVault::SignInMethod::Password, "MethodFor password");
        Check(!vault.StoreOAuthTokens("erika", UltraVault::OAuthTokens{}), "empty token set refused");
        UltraVault::OAuthTokens t;
        t.accessToken = "acc"; t.refreshToken = "ref"; t.expiresAt = 1234567;
        Check(vault.StoreOAuthTokens("erika", t), "StoreOAuthTokens");
        Check(vault.MethodFor("erika") == UltraVault::SignInMethod::OAuth2, "MethodFor OAuth2");
        Check(!vault.Has("erika"), "token set drops the password slot");
        UltraVault::OAuthTokens back;
        Check(vault.RetrieveOAuthTokens("erika", back) && back.accessToken == "acc" &&
                  back.refreshToken == "ref" && back.expiresAt == 1234567,
              "token set round trip");
        Check(back.NeedsRefresh(1234567) && !back.NeedsRefresh(1234566), "NeedsRefresh at expiry");
        Check(vault.Store("erika", "pw2"), "Store password again");
        Check(!vault.HasOAuthTokens("erika"), "password drops the token set");
        Check(vault.StoreOAuthTokens("erika", t) && vault.RemoveOAuthTokens("erika") &&
                  vault.MethodFor("erika") == UltraVault::SignInMethod::None,
              "RemoveOAuthTokens");
        Check(!vault.RemoveOAuthTokens("erika"), "removing absent tokens reports false");

        Check(vault.Remove("erika") == false, "Remove of an absent password reports false");
        Check(vault.Store("erika", "pw3") && vault.Remove("erika") && !vault.Has("erika"), "Remove");
        vault.Lock();
    }

    // Wrong master password on an existing vault, no oracle beyond the status.
    {
        DeviceKeyVault vault(dir.string(), kTestProfile);
        Check(vault.Unlock("not the passphrase") == UnlockStatus::WrongPassphrase,
              "wrong passphrase is WrongPassphrase");
        Check(!vault.IsUnlocked(), "stays locked on wrong passphrase");
        vault.Lock();
    }

    // A vault that exists without a device key (made with a master password)
    // must not be replaced: auto-unlock reports false so the caller prompts,
    // and PersistDeviceKey() after a manual unlock makes the next run silent.
    {
        fs::remove(dir / "device.key", ec);
        DeviceKeyVault vault(dir.string(), kTestProfile);
        Check(!vault.TryAutoUnlock(), "existing vault without a device key asks for a prompt");
        Check(vault.Exists(), "the vault was not replaced");
        const std::string devicePass = ReadFileBytes(dir / "device.key");
        Check(devicePass.empty(), "no device key was invented");
    }
    fs::remove_all(dir, ec);

    // Migration: a 0.1-format vault (vault.key + creds.dat) is carried across
    // on the first unlock and its files removed; a stray key with no data is
    // dropped too.
    {
        fs::create_directories(dir, ec);
        const std::vector<uint8_t> key(32, 0x5A);
        {
            std::ofstream ks(dir / "vault.key", std::ios::binary);
            ks.write(reinterpret_cast<const char*>(key.data()),
                     static_cast<std::streamsize>(key.size()));
        }
        auto xorWith = [&key](const std::string& in) {
            std::string out = in;
            for (std::size_t i = 0; i < out.size(); ++i)
                out[i] = static_cast<char>(static_cast<uint8_t>(out[i]) ^ key[i % key.size()]);
            return out;
        };
        {
            std::ofstream os(dir / "creds.dat");
            os << TestBase64("legacy-acc") << '\t' << TestBase64(xorWith("old-password")) << '\n';
            os << TestBase64("json-acc") << '\t'
               << TestBase64(xorWith("{\"token\":\"t\"}")) << '\n';
            os << "not a record\n";
        }
        DeviceKeyVault vault(dir.string(), kTestProfile);
        Check(vault.TryAutoUnlock(), "legacy directory auto-unlocks (no vault file yet)");
        std::string got;
        Check(vault.Retrieve("legacy-acc", got) && got == "old-password", "legacy secret migrated");
        Check(vault.Retrieve("json-acc", got) && got == "{\"token\":\"t\"}", "legacy JSON blob migrated");
        Check(!fs::exists(dir / "creds.dat", ec), "creds.dat removed after migration");
        Check(!fs::exists(dir / "vault.key", ec), "vault.key removed after migration");
        Check(vault.Exists(), "migrated secrets are in the vault file");
        vault.Lock();
    }
    fs::remove_all(dir, ec);
    {
        fs::create_directories(dir, ec);
        { std::ofstream ks(dir / "vault.key", std::ios::binary); ks << "orphan"; }
        DeviceKeyVault vault(dir.string(), kTestProfile);
        Check(vault.TryAutoUnlock(), "orphan key directory auto-unlocks");
        Check(!fs::exists(dir / "vault.key", ec), "orphan 0.1 key file is dropped");
        vault.Lock();
    }
    fs::remove_all(dir, ec);
}

int main() {
    std::printf("UltraVault tests\n");

    TestMemoryBackend();

    UltraCrypt_Initialize();
    if (UltraCrypt_IsAvailable()) {
        TestFileBackend("ultravault_test.vault");
        TestDeviceKeyVault(fs::temp_directory_path() / "ultravault_devicekey_test");
    } else {
        std::printf("file backend: SKIPPED (UltraCrypt backend "
                    "unavailable — built without libsodium)\n");
        // Without crypto the file backend must refuse, not degrade.
        UltraVault::Config config;
        config.backend    = UltraVault::Backend::File;
        config.filePath   = "ultravault_test.vault";
        config.passphrase = "pass";
        Check(UltraVault::Initialize(config).code ==
                  UltraVault::ResultCode::BackendUnavailable,
              "file backend refuses without crypto");
        // And the device-key vault says so rather than pretending to open.
        const fs::path dir = fs::temp_directory_path() / "ultravault_devicekey_test";
        UltraVault::DeviceKeyVault vault(dir.string(), kTestProfile);
        Check(vault.Unlock("pass") == UltraVault::UnlockStatus::Unavailable,
              "device-key vault reports Unavailable without crypto");
        Check(!vault.TryAutoUnlock() && !fs::exists(dir / "device.key"),
              "no device key is left behind when the vault cannot be created");
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
