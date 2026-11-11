// UltraAI/adapters/anthropic/include/UltraAIAnthropicVisionAnalyzer.h
// Anthropic (Claude) IVisionAnalyzer adapter: OCR over the Messages API.
// The picture goes to Claude as an image block with an instruction to
// transcribe every word in it, and the transcription comes back as
// structured output ({"text": ...}), so no commentary is mixed into it.
// Requests go through the Anthropic ITextLLM adapter (CreateAnthropicTextLLM)
// - its serialization, key resolution, error mapping and transport seam.
//
// Tasks: VisionTask::Ocr only; any other task in a request is answered with
// ErrorCode::InvalidRequest. Registered as provider "anthropic" for the
// "visionanalyzer" capability. A cloud provider: the default route uses it
// only when it is chosen (SetDefaultProvider("visionanalyzer", "anthropic"))
// or cloud fallback is allowed (UltraAIRouting.h).
//
// Version: 0.1.0
// Last Modified: 2026-11-06
// Author: UltraAI Module
#pragma once

#include "UltraAITransport.h"
#include "UltraAIVisionAnalyzer.h"

#include <memory>

namespace UltraAI {

// Create an Anthropic-backed IVisionAnalyzer.
//
// config fields used (as CreateAnthropicTextLLM's):
//   apiKey / apiKeyVaultRef - resolved on the first request; a missing key
//       gives ErrorCode::AuthenticationFailed then, not at construction.
//       Neither set (as when the default route builds it): the UltraVault
//       entry "ai.anthropic.api_key".
//   baseUrl       - default "https://api.anthropic.com".
//   defaultModel  - used when VisionAnalyzeRequest::model is empty
//                   (default "claude-opus-5-5").
//   timeoutMs     - per-request transport timeout.
//   providerOptions - passed through as top-level Messages API fields, as
//       for the text adapter. By default the request asks for low effort
//       (transcription needs little reasoning) and server-side refusal
//       fallbacks ("fallbacks": "default" with the
//       server-side-fallback-2026-07-01 beta); "output_config" or
//       "fallbacks" in providerOptions replaces that default (a configured
//       "fallbacks" brings its own "anthropic-beta" with it).
//
// `transport` is the network seam: nullptr uses the production
// UltraNetTransport (ULTRAAI_USE_ULTRANET=ON builds), tests inject a
// ScriptedTransport.
std::unique_ptr<IVisionAnalyzer> CreateAnthropicVisionAnalyzer(
    const VisionAnalyzerConfig& config,
    Error* outError = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

} // namespace UltraAI
