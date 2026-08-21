// UltraAI/adapters/_shared/include/UltraAICredentials.h
// Credential resolution shared by every network-using UltraAI adapter.
// Resolution order (see Docs/UltraNetIntegration.md §9):
//   1. ProviderConfig::apiKey          — literal string, used verbatim
//   2. ProviderConfig::apiKeyVaultRef  — UltraVault lookup (when built in)
//   3. Error{ ErrorCode::AuthenticationFailed }
// Adapters never read environment variables directly.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"

#include <string>

namespace UltraAI {

// Resolve the API key for a provider. Returns the key, or an empty string
// with *outError filled (ErrorCode::AuthenticationFailed) when no source
// yields one. A configuration that names neither apiKey nor apiKeyVaultRef
// fails the same way — adapters for keyless providers (local models)
// simply do not call this.
std::string ResolveApiKey(const ProviderConfig& config, Error* outError);

} // namespace UltraAI
