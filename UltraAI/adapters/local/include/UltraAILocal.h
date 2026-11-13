// UltraAI/adapters/local/include/UltraAILocal.h
// The `local` provider: chat, embeddings and speech to text on a model the
// local runtime (UltraAILocalRuntime.h) downloads, starts and supervises on
// this machine — nothing for the user to install. The wire protocol is the
// OpenAI adapter's (the engines are llama.cpp's server, and whisper.cpp's
// server answering on the OpenAI audio API's path); what lives here is
// asking the runtime for the engine's endpoint on first use, and
// reporting the catalogue's models, installed or not, as capabilities.
//
// config fields used:
//   defaultModel  — a catalogue id ("qwen3-4b-q4"), a user-added model's
//       id ("file:<path>", "hf:<repo>:<file>"), or empty for the runtime's
//       recommendation for this computer (LocalRuntime::Recommend).
//   providerOptions["topic"] — string: with no defaultModel, use the model
//       the user set for this topic ("chat", "code", "translation",
//       "summary", "vision", "search", or one the model server publishes;
//       LocalRuntime::ModelForTopic). A topic of another capability, or an
//       unknown one, falls back to the recommendation.
//   providerOptions["allow_download"] — bool: let the first call download
//       the model and its engine under the Ask policy (what the framework
//       dialog sets after the user agreed). Automatic needs nothing.
//   timeoutMs, providerOptions — passed through to the OpenAI delegate
//       (the keys the adapter reads itself are stripped).
//   apiKey, apiKeyVaultRef, baseUrl — ignored: the runtime's endpoint and
//       key are used.
//
// Speech to text (CreateLocalSpeechToText): the topic is "transcription"
// unless one is set; files only (no live transcription); WAV, MP3, FLAC
// and Ogg Vorbis as they are, with no size limit; translateToEnglish is
// whisper's own translate. The default 60 s timeout is raised to 30
// minutes: an hour of sound takes minutes on the CPU. whisper-server is
// built for Linux, macOS (Metal on Apple silicon) and Windows, x64 and ARM.
//
// Construction never performs I/O; the engine is started on first use.
// When nothing is installed and downloads are not allowed, calls fail
// with ErrorCode::ModelNotFound and providerCode "needs-install", which
// is the cue for the framework's "Get a local model" dialog.
//
// Registered as provider id "local" with runsLocally = true; first in
// UltraAI's local-first routing (UltraAIRouting.h).
// Version: 0.3.1 - whisper-server on every platform (comment)
// Version: 0.3.0 - speech to text on whisper-server
// Version: 0.2.0 - providerOptions["topic"]
// Version: 0.1.0
// Last Modified: 2026-11-13
// Author: UltraAI Module
#pragma once

#include "UltraAIEmbeddings.h"
#include "UltraAISpeechToText.h"
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

// Create a `local` ISpeechToText on the same terms.
std::unique_ptr<ISpeechToText> CreateLocalSpeechToText(
    const SpeechToTextConfig& config,
    Error* outError = nullptr,
    LocalRuntime* runtime = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

} // namespace UltraAI
