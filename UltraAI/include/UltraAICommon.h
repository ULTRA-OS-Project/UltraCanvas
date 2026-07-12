// UltraAI/include/UltraAICommon.h
// Shared types used across all UltraAI capability interfaces.
// Version: 0.1.1
// Last Modified: 2026-07-12
// Author: UltraAI Module
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

// When this module is used alongside UltraCanvas on Linux, the framework pulls
// in X11/Xlib.h, whose `None` macro (0L) collides with the `None` enumerator
// used across the UltraAI interfaces. Neutralise it before our declarations —
// same approach as UltraCanvasEBookTypes.h. This is the umbrella-included
// header, so the #undef covers every UltraAI capability header in the TU.
#ifdef None
#undef None
#endif

namespace UltraAI {

// =====================================================================
// Errors
// =====================================================================

enum class ErrorCode {
    None = 0,
    InvalidRequest,
    AuthenticationFailed,
    RateLimited,
    QuotaExceeded,
    ModelNotFound,
    ContextLengthExceeded,
    ContentFiltered,
    UnsupportedFormat,
    NetworkError,
    Timeout,
    ProviderError,
    Cancelled,
    Unknown
};

struct Error {
    ErrorCode code = ErrorCode::None;
    std::string message;
    std::string providerCode;
    bool IsOk() const { return code == ErrorCode::None; }
    explicit operator bool() const { return IsOk(); }
};

// =====================================================================
// Free-form options bag (escape hatch for vendor-specific knobs)
// Routed verbatim to the active provider adapter.
// =====================================================================

using OptionValue = std::variant<bool, int64_t, double, std::string>;
using OptionsMap  = std::map<std::string, OptionValue>;

// =====================================================================
// Media payload — image, audio, video, or arbitrary file content.
// Either `url` or `bytes` is populated; `mimeType` is required for inline.
// =====================================================================

struct MediaBlob {
    std::string url;
    std::vector<uint8_t> bytes;
    std::string mimeType;
    std::string filename;

    bool IsInline() const { return !bytes.empty(); }
    bool IsRemote() const { return !url.empty() && bytes.empty(); }
};

// =====================================================================
// Token / unit usage reporting (LLM, embeddings, vision, etc.)
// Audio capabilities use audioSeconds; image/video capabilities use units.
// =====================================================================

struct TokenUsage {
    int32_t inputTokens       = 0;
    int32_t outputTokens      = 0;
    int32_t cachedInputTokens = 0;
    int32_t reasoningTokens   = 0;
    double  audioSeconds      = 0.0;
    int32_t units             = 0;   // images / videos / characters etc.
};

// =====================================================================
// Cancellable async/streaming handle
// =====================================================================

class IStreamHandle {
public:
    virtual ~IStreamHandle() = default;
    virtual void Cancel() = 0;
    virtual bool IsDone() const = 0;
};
using StreamHandle = std::shared_ptr<IStreamHandle>;

// =====================================================================
// Shared provider configuration. Each capability factory accepts a
// specialization (e.g. TextLLMConfig) but they all carry these fields.
// =====================================================================

struct ProviderConfig {
    std::string providerId;     // "" -> use UltraOS default route
    std::string apiKey;         // literal key (escape hatch / tests)
    std::string apiKeyVaultRef; // canonical UltraVault key, e.g. "ai.anthropic.api_key"
                                // Resolution order in network adapters:
                                //   1. apiKey (if non-empty, used verbatim)
                                //   2. apiKeyVaultRef looked up via UltraVault
                                //   3. fail with ErrorCode::AuthenticationFailed
    std::string baseUrl;        // optional override (e.g. self-hosted)
    std::string defaultModel;
    int32_t timeoutMs = 60000;
    OptionsMap providerOptions;
};

} // namespace UltraAI
