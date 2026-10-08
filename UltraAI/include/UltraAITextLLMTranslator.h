// UltraAI/include/UltraAITextLLMTranslator.h
// ITranslator served by any ITextLLM: the translator sends the texts to a
// chat model with a translation instruction and reads the translations
// back. It is how translation works through anthropic, openai, qwen and
// llama-cpp without a dedicated translation provider, and it is always
// built — it has no dependency beyond the capability interfaces.
//
// Registration: CreateTranslator / ListTranslatorProviders know one
// translator provider for every registered text-LLM provider except
// "mock" (which has its own MockTranslator), under the same id. So
//
//   TranslatorConfig cfg; cfg.providerId = "qwen";
//   auto tr = CreateTranslator(cfg);
//
// translates through a local Qwen server, and an empty providerId follows
// the routing policy — local LLMs first, cloud only when allowed.
//
// Configuration (TranslatorConfig): apiKey, apiKeyVaultRef, baseUrl,
// defaultModel, timeoutMs and providerOptions are handed to the text LLM
// unchanged, so whatever configures the LLM provider configures the
// translator. The translator reads these providerOptions of its own:
//
//   "textllm.provider"    string  the text-LLM provider to translate with
//                                 (default: the translator's own
//                                 providerId; empty -> the textllm route)
//   "textllm.batchSize"   int     texts per chat request (default 20)
//   "textllm.temperature" double  sampling temperature (default 0)
//   "textllm.maxOutputTokens" int cap on the reply (default: none)
//
// Version: 0.1.0
// Last Modified: 2026-10-07
// Author: UltraAI Module
#pragma once

#include "UltraAITextLLM.h"
#include "UltraAITranslator.h"

#include <memory>

namespace UltraAI {

// Wrap a text LLM the caller already holds. The translator owns it. The
// providerId reported by GetCapabilities() is `config.providerId` when set,
// else the LLM's own provider id. Returns nullptr (and fills outError) only
// when `llm` is null.
std::unique_ptr<ITranslator> CreateTextLLMTranslator(
    std::unique_ptr<ITextLLM> llm,
    const TranslatorConfig& config = {},
    Error* outError = nullptr);

// Create the text LLM from the configuration (see the header comment for
// which fields and options choose it) and wrap it. Fails the way
// CreateTextLLM fails — unknown provider, missing credentials, no transport.
std::unique_ptr<ITranslator> CreateTextLLMTranslator(
    const TranslatorConfig& config,
    Error* outError = nullptr);

// The two JSON exchanges the translator has with the model, exposed so a
// test or an application can see — or reuse — exactly what is sent and how
// the reply is read. Both parse functions return false and fill `error`
// when the reply does not carry the expected shape.
std::string BuildTranslationPrompt(const TranslateRequest& request,
                                   size_t first, size_t count);
bool ParseTranslationReply(const std::string& reply, size_t count,
                           std::vector<TranslateResult>& results,
                           Error& error);
std::string BuildDetectionPrompt(const DetectLanguageRequest& request,
                                 size_t first, size_t count);
bool ParseDetectionReply(const std::string& reply, size_t count,
                         int32_t topN,
                         std::vector<std::vector<LanguageGuess>>& guesses,
                         Error& error);

} // namespace UltraAI
