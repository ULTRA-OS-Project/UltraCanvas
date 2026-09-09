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
//
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraVault/UltraVault.h"
#include "UltraCrypt/UltraCryptCore.h"

#include <cstdio>
#include <fstream>
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

int main() {
    std::printf("UltraVault tests\n");

    TestMemoryBackend();

    UltraCrypt_Initialize();
    if (UltraCrypt_IsAvailable()) {
        TestFileBackend("ultravault_test.vault");
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
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
