// UltraAI/adapters/openai/include/UltraAIOpenAI.h
// OpenAI-compatible ITextLLM + IEmbeddings + ISpeechToText adapters over the Chat
// Completions API (POST {baseUrl}/v1/chat/completions) and the embeddings
// endpoint (POST {baseUrl}/v1/embeddings). The wire format is the de-facto
// standard for local inference servers too, so pointing baseUrl at an
// Ollama / vLLM / llama.cpp-server instance works — and those servers run
// keyless, which the adapter allows for any non-default baseUrl.
// Network I/O goes through the UltraAI transport seam: production wiring
// uses UltraNetTransport, unit tests inject a ScriptedTransport.
// Version: 0.3.0 - CreateOpenAISpeechToText (the audio API)
// Version: 0.2.0 - CreateOpenAITextBatch
// Version: 0.1.0
// Last Modified: 2026-11-10
// Author: UltraAI Module
#pragma once

#include "UltraAIBatch.h"
#include "UltraAIEmbeddings.h"
#include "UltraAISpeechToText.h"
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

// Create an OpenAI Batch API job runner (ITextBatch, UltraAIBatch.h): the
// requests go up as a JSONL file (POST {baseUrl}/v1/files) and run as one
// batch (POST {baseUrl}/v1/batches) at about half the price, answered
// within 24 hours. Same config fields as the text adapter. Only the hosted
// API has batches: with a custom baseUrl this returns nullptr
// (ErrorCode::UnsupportedFormat) and CreateTextBatch sends the requests
// one at a time instead.
std::unique_ptr<ITextBatch> CreateOpenAITextBatch(
    const TextBatchConfig& config,
    Error* outError = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

// Create an OpenAI-compatible ISpeechToText over the audio API: a file
// goes up as multipart/form-data to POST {baseUrl}/v1/audio/transcriptions
// (or /v1/audio/translations for TranscribeRequest::translateToEnglish,
// with whisper-1). Registered as "openai" for "speechtotext" - a cloud
// provider, which the default route uses only once chosen.
//
//   model         - TranscribeRequest::model, else ProviderConfig::defaultModel,
//                   else "gpt-4o-transcribe" against the hosted API and
//                   "whisper-1" against a custom baseUrl (a self-hosted
//                   OpenAI-compatible whisper server: speaches,
//                   faster-whisper-server, LocalAI - keyless, as for the
//                   text adapter).
//   key           - apiKey / apiKeyVaultRef; with neither, against the
//                   hosted API, the UltraVault entry "ai.openai.api_key".
//   audio         - inline bytes (a URL is refused: the API takes the file);
//                   the format goes by the file name's extension
//                   (MediaBlob::filename, else from AudioFormat / mimeType).
//                   The hosted API takes 25 MB at most, refused before upload.
//   timestamps    - segments and words come with whisper-1 (verbose_json);
//                   the gpt-4o models give the text alone.
//   language      - BCP-47, sent as its primary subtag ("de-CH" -> "de").
//   options       - providerOptions / request options as plain form fields
//                   ("temperature", "chunking_strategy", ...).
// StartLiveTranscribe is not supported (an Error event, nullptr).
std::unique_ptr<ISpeechToText> CreateOpenAISpeechToText(
    const SpeechToTextConfig& config,
    Error* outError = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

} // namespace UltraAI
