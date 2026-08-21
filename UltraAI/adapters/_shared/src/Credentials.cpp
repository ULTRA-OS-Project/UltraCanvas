// UltraAI/adapters/_shared/src/Credentials.cpp
// Implementation of UltraAI::ResolveApiKey.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module

#include "UltraAICredentials.h"

#ifdef ULTRAAI_HAS_ULTRAVAULT
#include <UltraVault/UltraVault.h>
#endif

namespace UltraAI {

std::string ResolveApiKey(const ProviderConfig& config, Error* outError) {
    if (!config.apiKey.empty()) {
        return config.apiKey;
    }

    if (!config.apiKeyVaultRef.empty()) {
#ifdef ULTRAAI_HAS_ULTRAVAULT
        UltraVault::SecretValue secret;
        UltraVault::Result r = UltraVault::Get(config.apiKeyVaultRef, secret);
        if (r.IsOk()) {
            return secret.AsString();
        }
        if (outError) {
            outError->code    = ErrorCode::AuthenticationFailed;
            outError->message = "UltraVault lookup failed for '" +
                                config.apiKeyVaultRef + "': " + r.message;
        }
        return {};
#else
        if (outError) {
            outError->code    = ErrorCode::AuthenticationFailed;
            outError->message = "apiKeyVaultRef '" + config.apiKeyVaultRef +
                                "' set but this build has no UltraVault "
                                "(ULTRAAI_USE_ULTRAVAULT=OFF); pass "
                                "ProviderConfig::apiKey instead";
        }
        return {};
#endif
    }

    if (outError) {
        outError->code    = ErrorCode::AuthenticationFailed;
        outError->message = "no credential configured: set ProviderConfig::"
                            "apiKey or apiKeyVaultRef";
    }
    return {};
}

} // namespace UltraAI
