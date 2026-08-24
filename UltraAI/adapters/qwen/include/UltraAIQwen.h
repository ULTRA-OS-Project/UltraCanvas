// UltraAI/adapters/qwen/include/UltraAIQwen.h
// Qwen adapter for locally served models: Ollama, vLLM, llama.cpp's server,
// LM Studio — anything exposing the OpenAI-compatible /v1 surface. The wire
// format is identical to OpenAI's, so this adapter delegates the protocol
// to the OpenAI adapter and owns what actually differs: finding the local
// server, listing what it serves, and choosing a Qwen model without the
// caller having to name one.
//
// It registers as provider id "qwen" and reports runsLocally = true, which
// is what puts it ahead of cloud providers in UltraAI's local-first routing
// (UltraAIRouting.h).
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module
#pragma once

#include "UltraAIEmbeddings.h"
#include "UltraAITextLLM.h"
#include "UltraAITransport.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraAI {

// The endpoints probed, in order, when ProviderConfig::baseUrl is empty.
// Exposed so applications can show the user what was tried when nothing
// answered.
std::vector<std::string> QwenDefaultEndpoints();

// Create a Qwen ITextLLM against a locally served OpenAI-compatible
// endpoint.
//
// config fields used:
//   baseUrl       — the server root, without the "/v1" suffix (e.g.
//       "http://localhost:11434" for Ollama). Empty probes
//       QwenDefaultEndpoints() with GET {base}/v1/models and takes the
//       first that answers.
//   defaultModel  — used when ChatRequest::model is empty. Empty picks the
//       first model the server reports whose id contains "qwen", falling
//       back to the server's first model. A request that names a model
//       always wins.
//   apiKey / apiKeyVaultRef — optional. Local servers run keyless, so an
//       absent key is not an error; when present it is sent as
//       "Authorization: Bearer <key>" (LM Studio and locked-down vLLM
//       deployments want one).
//   timeoutMs, providerOptions — passed through to the OpenAI adapter.
//
// Discovery happens lazily on first use, not in this factory: constructing
// an adapter never performs network I/O. When no server answers, calls fail
// with ErrorCode::NetworkError naming the endpoints that were tried.
//
// RawProvider() returns the underlying OpenAI-adapter instance.
std::unique_ptr<ITextLLM> CreateQwenTextLLM(
    const TextLLMConfig& config,
    Error* outError = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

// Create a Qwen IEmbeddings against the same kind of endpoint
// (POST {baseUrl}/v1/embeddings). Same discovery and credential rules; the
// default model prefers a served model whose id contains both "qwen" and
// "embed".
std::unique_ptr<IEmbeddings> CreateQwenEmbeddings(
    const EmbeddingsConfig& config,
    Error* outError = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

} // namespace UltraAI
