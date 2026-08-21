// UltraAI/adapters/anthropic/include/UltraAIAnthropicTextLLM.h
// Anthropic (Claude) ITextLLM adapter over the Messages API
// (POST {baseUrl}/v1/messages, anthropic-version 2023-06-01).
// Network I/O goes through the UltraAI transport seam: production wiring
// uses UltraNetTransport, unit tests inject a ScriptedTransport.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module
#pragma once

#include "UltraAITextLLM.h"
#include "UltraAITransport.h"

#include <memory>

namespace UltraAI {

// Create an Anthropic-backed ITextLLM.
//
// config fields used:
//   apiKey / apiKeyVaultRef — resolved lazily on the first request (so a
//       keyless build can still construct the adapter and list models);
//       an unresolvable key surfaces ErrorCode::AuthenticationFailed.
//   baseUrl       — default "https://api.anthropic.com".
//   defaultModel  — used when ChatRequest::model is empty
//                   (default "claude-opus-5").
//   timeoutMs     — per-request transport timeout.
//   providerOptions / ChatRequest::options — passed through as top-level
//       Messages API fields (values that parse as JSON are embedded as
//       JSON, so e.g. {"thinking", "{\"type\":\"adaptive\"}"} works); the
//       key "anthropic-beta" becomes the beta header instead.
//
// `transport` is the network seam. Pass nullptr to use the production
// UltraNetTransport (requires a build with ULTRAAI_USE_ULTRANET=ON;
// otherwise construction fails with ErrorCode::NetworkError).
std::unique_ptr<ITextLLM> CreateAnthropicTextLLM(
    const TextLLMConfig& config,
    Error* outError = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

} // namespace UltraAI
