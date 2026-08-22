// Tests/UltraAIVaultIntegrationTests.cpp
// Cross-module check: UltraAI's credential resolution actually reads from
// UltraVault when the module is built with ULTRAAI_USE_ULTRAVAULT — the
// path that was "wired, awaits UltraVault module" until Phase 3.
//
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraAICredentials.h"
#include "UltraVault/UltraVault.h"

#include <cstdio>
#include <string>

static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

int main() {
    std::printf("UltraAI <-> UltraVault integration\n");

#ifndef ULTRAAI_HAS_ULTRAVAULT
    std::printf("SKIPPED: UltraAI built without ULTRAAI_USE_ULTRAVAULT\n");
    return 0;
#else
    UltraVault::Config config;
    config.backend = UltraVault::Backend::Memory;
    Check(UltraVault::Initialize(config).IsOk(), "vault opens");
    Check(UltraVault::Put("ai.test.api_key",
                          UltraVault::SecretValue::FromString("sk-from-vault"))
              .IsOk(),
          "vault Put");

    // A vault reference resolves through UltraVault::Get.
    UltraAI::ProviderConfig cfg;
    cfg.apiKeyVaultRef = "ai.test.api_key";
    UltraAI::Error err;
    Check(UltraAI::ResolveApiKey(cfg, &err) == "sk-from-vault",
          "ResolveApiKey reads the vault");
    Check(err.IsOk(), "no error on vault hit");

    // A literal key still takes precedence over the reference.
    cfg.apiKey = "sk-literal";
    Check(UltraAI::ResolveApiKey(cfg, nullptr) == "sk-literal",
          "literal apiKey outranks the vault reference");

    // A missing reference fails with AuthenticationFailed and the vault's
    // explanation in the message.
    cfg.apiKey.clear();
    cfg.apiKeyVaultRef = "ai.test.missing";
    err = {};
    Check(UltraAI::ResolveApiKey(cfg, &err).empty(), "missing ref yields no key");
    Check(err.code == UltraAI::ErrorCode::AuthenticationFailed,
          "missing ref is AuthenticationFailed");
    Check(err.message.find("ai.test.missing") != std::string::npos,
          "error names the reference");

    UltraVault::Shutdown();

    // With the vault closed, the reference fails cleanly too.
    cfg.apiKeyVaultRef = "ai.test.api_key";
    err = {};
    Check(UltraAI::ResolveApiKey(cfg, &err).empty() &&
              err.code == UltraAI::ErrorCode::AuthenticationFailed,
          "closed vault fails closed");

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
#endif
}
