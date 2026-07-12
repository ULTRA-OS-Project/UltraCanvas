// UltraAI/include/UltraAITranslator.h
// Provider-agnostic language translation interface.
// Version: 0.1.0
// Last Modified: 2026-05-08
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UltraAI {

// =====================================================================
// Request / Response
// =====================================================================

enum class TranslationFormality {
    Default,
    Formal,
    Informal
};

struct TranslateRequest {
    std::string model;                       // empty -> adapter default
    std::vector<std::string> texts;          // batched
    std::string sourceLanguage;              // BCP-47, empty -> auto-detect
    std::string targetLanguage;              // BCP-47, required
    TranslationFormality formality = TranslationFormality::Default;
    std::string domain;                      // "general", "technical", ...
    std::string glossaryId;                  // optional vendor glossary
    bool preserveFormatting = true;          // HTML / Markdown markup
    OptionsMap options;
};

struct TranslateResult {
    std::string text;
    std::string detectedSourceLanguage;
    double confidence = 0.0;                 // 0..1 when reported
};

struct TranslateResponse {
    std::string model;
    std::vector<TranslateResult> results;    // same order as TranslateRequest::texts
    TokenUsage usage;                        // characters in usage.units
    Error error;
};

// =====================================================================
// Language detection
// =====================================================================

struct LanguageGuess {
    std::string language;                    // BCP-47
    double confidence = 0.0;
};

struct DetectLanguageRequest {
    std::vector<std::string> texts;
    int32_t topN = 1;
    OptionsMap options;
};

struct DetectLanguageResponse {
    std::vector<std::vector<LanguageGuess>> guesses; // per input
    Error error;
};

// =====================================================================
// Capability discovery
// =====================================================================

struct LanguagePairInfo {
    std::string source;
    std::string target;
    bool supportsFormality = false;
};

struct TranslatorModelInfo {
    std::string id;
    std::string displayName;
    std::vector<std::string> supportedLanguages;     // BCP-47
    std::vector<LanguagePairInfo> pairs;             // empty -> all-to-all
    bool supportsAutoDetect = false;
    bool supportsBatching = true;
    bool supportsFormality = false;
    bool runsLocally = false;
};

struct TranslatorProviderCapabilities {
    std::string providerId;
    std::vector<TranslatorModelInfo> models;
    int32_t maxBatchSize = 0;                        // 0 -> unspecified
};

// =====================================================================
// ITranslator
// =====================================================================

class ITranslator {
public:
    virtual ~ITranslator() = default;

    virtual TranslatorProviderCapabilities GetCapabilities() const = 0;

    virtual TranslateResponse Translate(const TranslateRequest& request) = 0;

    virtual std::future<TranslateResponse> TranslateAsync(
        const TranslateRequest& request) = 0;

    virtual DetectLanguageResponse DetectLanguage(
        const DetectLanguageRequest& request) = 0;

    virtual void* RawProvider() { return nullptr; }
};

// =====================================================================
// Factory
// =====================================================================

using TranslatorConfig = ProviderConfig;

std::unique_ptr<ITranslator> CreateTranslator(const TranslatorConfig& config,
                                              Error* outError = nullptr);

std::vector<std::string> ListTranslatorProviders();

bool RegisterTranslatorProvider(
    const std::string& providerId,
    std::function<std::unique_ptr<ITranslator>(const TranslatorConfig&, Error*)> factory);

} // namespace UltraAI
