// UltraAI/adapters/openai/include/UltraAIOpenAI.h
// OpenAI-compatible ITextLLM + IEmbeddings adapters over the Chat
// Completions API (POST {baseUrl}/v1/chat/completions) and the embeddings
// endpoint (POST {baseUrl}/v1/embeddings). The wire format is the de-facto
// standard for local inference servers too, so pointing baseUrl at an
// Ollama / vLLM / llama.cpp-server instance works — and those servers run
// keyless, which the adapter allows for any non-default baseUrl.
// Network I/O goes through the UltraAI transport seam: production wiring
// uses UltraNetTransport, unit tests inject a ScriptedTransport.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module
#pragma once

#include "UltraAIEmbeddings.h"
#include "UltraAITextLLM.h"
#include "UltraAITransport.h"

#include <memory>

namespace UltraAI {

// Create an OpenAI-backed ITextLLM.
//
// config fields used:
//   apiKey / apiKeyVaultRef — resolved lazily on the first request and sent
//       as "Authorization: Bearer <key>". Against the default baseUrl an
//       unresolvable key surfaces ErrorCode::AuthenticationFailed; against
//       a custom baseUrl (self-hosted OpenAI-compatible server) the request
//       simply goes out without the header.
//   baseUrl       — default "https://api.openai.com".
//   defaultModel  — used when ChatRequest::model is empty. Unlike hosted
//       Anthropic there is no safe universal fallback (self-hosted servers
//       name models arbitrarily), so an empty model in both places fails
//       with ErrorCode::InvalidRequest.
//   timeoutMs     — per-request transport timeout.
//   providerOptions / ChatRequest::options — passed through as top-level
//       request fields (string values that parse as JSON objects/arrays are
//       embedded structurally).
//
// `transport` is the network seam. Pass nullptr to use the production
// UltraNetTransport (requires a build with ULTRAAI_USE_ULTRANET=ON;
// otherwise construction fails with ErrorCode::NetworkError).
std::unique_ptr<ITextLLM> CreateOpenAITextLLM(
    const TextLLMConfig& config,
    Error* outError = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

// Create an OpenAI-backed IEmbeddings (POST {baseUrl}/v1/embeddings).
// Same credential and baseUrl rules as the text adapter, but against the
// hosted API an empty model falls back to "text-embedding-3-small" (a
// custom baseUrl still requires one — self-hosted servers name models
// arbitrarily). EmbeddingRequest::dimensions maps to the API's
// "dimensions" field.
std::unique_ptr<IEmbeddings> CreateOpenAIEmbeddings(
    const EmbeddingsConfig& config,
    Error* outError = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

} // namespace UltraAI
