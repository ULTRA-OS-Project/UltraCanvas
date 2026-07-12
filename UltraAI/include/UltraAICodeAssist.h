// UltraAI/include/UltraAICodeAssist.h
// Provider-agnostic programming-assistant interface (generation, explanation,
// refactoring, language translation, bug detection, test generation, FIM
// completion). Distinct from ITextLLM because the request shape, output
// shape, and capability metadata differ enough to deserve a dedicated API.
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
// Tasks
// =====================================================================

enum class CodeTask {
    Generate,              // produce code from a description
    Explain,               // natural-language explanation of code
    Refactor,              // restructure / clean up code
    TranslateLanguage,     // port between programming languages
    DetectBugs,            // diagnostics + suggested fixes
    GenerateTests,         // unit tests for the supplied code
    Document,              // generate doc comments
    Complete               // fill-in-the-middle (FIM) completion
};

// =====================================================================
// Context
// =====================================================================

struct CodeContextFile {
    std::string filename;
    std::string language;             // "cpp", "python", "ts", ...
    std::string content;
};

struct CodeDiagnostic {
    enum class Severity { Info, Warning, Error };
    Severity severity = Severity::Info;
    std::string message;
    int32_t line = 0;                 // 1-based
    int32_t column = 0;               // 1-based
    int32_t endLine = 0;
    int32_t endColumn = 0;
    std::string suggestedFix;         // optional replacement text
    std::string ruleId;               // optional analyzer rule id
};

// =====================================================================
// Request / Response
// =====================================================================

struct CodeAssistRequest {
    std::string model;
    CodeTask task = CodeTask::Generate;

    std::string instruction;          // user-facing description
    std::string language;             // primary language (e.g. "cpp")
    std::string targetLanguage;       // for TranslateLanguage

    std::string codeSnippet;          // input code (Explain/Refactor/...)

    // FIM (Complete)
    std::string prefix;               // text before the cursor
    std::string suffix;               // text after the cursor

    // Extra files for context (RAG-style)
    std::vector<CodeContextFile> contextFiles;

    // Sampling
    std::optional<double>   temperature;
    std::optional<int32_t>  maxOutputTokens;
    std::vector<std::string> stopSequences;
    std::optional<uint64_t> seed;

    // Output shaping
    bool requestExplanation = false;  // include natural-language explanation
    int32_t numSuggestions = 1;       // alternative completions

    OptionsMap options;
};

struct CodeAssistResponse {
    std::string model;
    std::string code;                              // primary output
    std::string explanation;                       // when requested / Explain
    std::vector<std::string> alternativeSuggestions;
    std::vector<CodeDiagnostic> diagnostics;       // for DetectBugs
    TokenUsage usage;
    Error error;
};

// =====================================================================
// Streaming (token-level deltas, mirrors ITextLLM)
// =====================================================================

enum class CodeStreamEventKind {
    CodeDelta,
    ExplanationDelta,
    Diagnostic,
    UsageUpdate,
    Done,
    Error
};

struct CodeStreamEvent {
    CodeStreamEventKind kind = CodeStreamEventKind::CodeDelta;
    std::string textDelta;
    CodeDiagnostic diagnostic;
    TokenUsage usage;
    Error error;
};

using CodeStreamCallback = std::function<void(const CodeStreamEvent&)>;

// =====================================================================
// Capability discovery
// =====================================================================

struct CodeAssistModelInfo {
    std::string id;
    std::string displayName;
    int32_t contextWindow = 0;
    int32_t maxOutputTokens = 0;
    std::vector<CodeTask> supportedTasks;
    std::vector<std::string> supportedLanguages;   // programming languages
    bool supportsFim = false;
    bool supportsStreaming = false;
    bool runsLocally = false;
};

struct CodeAssistProviderCapabilities {
    std::string providerId;
    std::vector<CodeAssistModelInfo> models;
};

// =====================================================================
// ICodeAssist
// =====================================================================

class ICodeAssist {
public:
    virtual ~ICodeAssist() = default;

    virtual CodeAssistProviderCapabilities GetCapabilities() const = 0;

    virtual CodeAssistResponse Run(const CodeAssistRequest& request) = 0;

    virtual std::future<CodeAssistResponse> RunAsync(
        const CodeAssistRequest& request) = 0;

    virtual StreamHandle RunStream(const CodeAssistRequest& request,
                                   CodeStreamCallback onEvent) = 0;

    virtual void* RawProvider() { return nullptr; }
};

// =====================================================================
// Factory
// =====================================================================

using CodeAssistConfig = ProviderConfig;

std::unique_ptr<ICodeAssist> CreateCodeAssist(const CodeAssistConfig& config,
                                              Error* outError = nullptr);

std::vector<std::string> ListCodeAssistProviders();

bool RegisterCodeAssistProvider(
    const std::string& providerId,
    std::function<std::unique_ptr<ICodeAssist>(const CodeAssistConfig&, Error*)> factory);

} // namespace UltraAI
