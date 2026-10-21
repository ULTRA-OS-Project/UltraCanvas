// UltraAI/adapters/local/include/UltraAILocal.h
// The `local` provider: chat and embeddings on a model the local runtime
// (UltraAILocalRuntime.h) downloads, starts and supervises on this
// machine — nothing for the user to install. The wire protocol is the
// OpenAI adapter's (the engine is llama.cpp's server); what lives here is
// asking the runtime for the engine's endpoint on first use, and
// reporting the catalogue's models, installed or not, as capabilities.
//
// config fields used:
//   defaultModel  — a catalogue id ("qwen3-4b-q4"), a user-added model's
//       id ("file:<path>", "hf:<repo>:<file>"), or empty for the runtime's
//       recommendation for this computer (LocalRuntime::Recommend).
//   providerOptions["allow_download"] — bool: let the first call download
//       the model and its engine under the Ask policy (what the framework
//       dialog sets after the user agreed). Automatic needs nothing.
//   timeoutMs, providerOptions — passed through to the OpenAI delegate
//       (the two keys the adapter reads itself are stripped).
//   apiKey, apiKeyVaultRef, baseUrl — ignored: the runtime's endpoint and
//       key are used.
//
// Construction never performs I/O; the engine is started on first use.
// When nothing is installed and downloads are not allowed, calls fail
// with ErrorCode::ModelNotFound and providerCode "needs-install", which
// is the cue for the framework's "Get a local model" dialog.
//
// Registered as provider id "local" with runsLocally = true; first in
// UltraAI's local-first routing (UltraAIRouting.h).
// Version: 0.1.0
// Last Modified: 2026-10-20
// Author: UltraAI Module
#pragma once

#include "UltraAIEmbeddings.h"
#include "UltraAITextLLM.h"
#include "UltraAITransport.h"

#include <memory>

namespace UltraAI {

class LocalRuntime;

// Create a `local` ITextLLM. `runtime` null = LocalRuntime::Instance();
// `transport` null = the production transport (a test injects both).
std::unique_ptr<ITextLLM> CreateLocalTextLLM(
    const TextLLMConfig& config,
    Error* outError = nullptr,
    LocalRuntime* runtime = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

// Create a `local` IEmbeddings on the same terms.
std::unique_ptr<IEmbeddings> CreateLocalEmbeddings(
    const EmbeddingsConfig& config,
    Error* outError = nullptr,
    LocalRuntime* runtime = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

} // namespace UltraAI
