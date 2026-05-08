// UltraAI/include/UltraAITextLLM.h
// Provider-agnostic Large Language Model chat interface for UltraOS / UltraAI module
// Version: 0.2.0
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
#include <variant>
#include <vector>

namespace UltraAI {

// =====================================================================
// Multimodal content parts
// A Message::content can be plain text or a sequence of parts.
// =====================================================================

struct TextPart {
    std::string text;
};

struct ImagePart {
    // Either a remote URL or inline bytes; mimeType required for inline.
    std::string url;
    std::vector<uint8_t> bytes;
    std::string mimeType;       // e.g. "image/png"
    std::string detail;         // optional: "low" | "high" | "auto"
};

struct FilePart {
    std::string url;
    std::vector<uint8_t> bytes;
    std::string mimeType;
    std::string filename;
};

using ContentPart = std::variant<TextPart, ImagePart, FilePart>;

// =====================================================================
// Conversation messages
// =====================================================================

enum class Role {
    System,
    User,
    Assistant,
    Tool
};

struct ToolCall {
    std::string id;             // provider-assigned id, used to bind a Tool result
    std::string name;
    std::string argumentsJson;  // raw JSON string from the model
};

struct Message {
    Role role = Role::User;

    // Either set `text` for the simple case OR fill `parts` for multimodal.
    std::string text;
    std::vector<ContentPart> parts;

    // Assistant messages may carry tool calls; Tool messages reply to one.
    std::vector<ToolCall> toolCalls;
    std::string toolCallId;     // for role == Tool
    std::string name;           // optional speaker / tool name
};

// =====================================================================
// Tool / function calling
// =====================================================================

struct ToolDefinition {
    std::string name;
    std::string description;
    std::string parametersJsonSchema;  // JSON Schema for arguments
};

enum class ToolChoice {
    Auto,       // model decides
    None,       // never call a tool
    Required,   // must call some tool
    Specific    // see ChatRequest::forcedToolName
};

// =====================================================================
// Structured output
// =====================================================================

enum class ResponseFormat {
    Text,
    JsonObject,     // free-form JSON
    JsonSchema      // schema-constrained; see jsonSchema field
};

// =====================================================================
// Sampling / generation parameters
// =====================================================================

struct SamplingParams {
    std::optional<double>   temperature;
    std::optional<double>   topP;
    std::optional<int32_t>  maxOutputTokens;
    std::optional<uint64_t> seed;
    std::vector<std::string> stopSequences;
    std::optional<double>   presencePenalty;
    std::optional<double>   frequencyPenalty;
};

// =====================================================================
// Request / Response
// =====================================================================

struct ChatRequest {
    std::string model;                // empty -> adapter's default
    std::vector<Message> messages;

    SamplingParams sampling;

    // Tools
    std::vector<ToolDefinition> tools;
    ToolChoice toolChoice = ToolChoice::Auto;
    std::string forcedToolName;

    // Output shaping
    ResponseFormat responseFormat = ResponseFormat::Text;
    std::string jsonSchema;           // used when responseFormat == JsonSchema

    // Free-form provider-specific extras
    OptionsMap options;
};

enum class FinishReason {
    Unknown,
    Stop,             // natural end / stop sequence
    Length,           // hit max tokens
    ToolCalls,        // model wants to call tools
    ContentFiltered,
    Error
};

struct ChatResponse {
    std::string id;                   // provider-assigned, when available
    std::string model;
    std::string text;                 // concatenated assistant text
    std::vector<ContentPart> parts;   // populated for multimodal replies
    std::vector<ToolCall> toolCalls;
    FinishReason finishReason = FinishReason::Unknown;
    TokenUsage usage;
    Error error;
};

// =====================================================================
// Streaming
// =====================================================================

enum class StreamEventKind {
    TextDelta,        // incremental assistant text
    ToolCallDelta,    // incremental tool-call arguments
    ToolCallEnd,      // a tool call is finalized
    UsageUpdate,
    Done,             // terminal success event
    Error             // terminal failure event
};

struct StreamEvent {
    StreamEventKind kind = StreamEventKind::TextDelta;
    std::string textDelta;
    ToolCall toolCallDelta;
    TokenUsage usage;
    FinishReason finishReason = FinishReason::Unknown;
    Error error;
};

using StreamCallback = std::function<void(const StreamEvent&)>;

// =====================================================================
// Capability discovery
// =====================================================================

struct ModelInfo {
    std::string id;
    std::string displayName;
    int32_t contextWindow = 0;
    int32_t maxOutputTokens = 0;
    bool supportsVision = false;
    bool supportsAudioInput = false;
    bool supportsTools = false;
    bool supportsStreaming = false;
    bool supportsJsonSchema = false;
    bool runsLocally = false;
};

struct ProviderCapabilities {
    std::string providerId;           // "openai", "anthropic", "llama-cpp", ...
    std::vector<ModelInfo> models;
    bool supportsStreaming = false;
    bool supportsTools = false;
    bool supportsVision = false;
    bool supportsEmbeddings = false;  // separate interface, but reported here
};

// =====================================================================
// ITextLLM — the capability interface
//
// Implementations (adapters) live outside this header. The UltraAI module
// resolves a concrete implementation via UltraAI::CreateTextLLM(...) using
// OS-managed credentials and routing policy.
// =====================================================================

class ITextLLM {
public:
    virtual ~ITextLLM() = default;

    // Static metadata
    virtual ProviderCapabilities GetCapabilities() const = 0;

    // Synchronous (blocking) call. Convenience for scripts and CLI.
    virtual ChatResponse Chat(const ChatRequest& request) = 0;

    // Asynchronous call returning a future. The future resolves once the
    // full response is available; cancellation is not guaranteed.
    virtual std::future<ChatResponse> ChatAsync(const ChatRequest& request) = 0;

    // Streaming call. The callback is invoked on an implementation-defined
    // thread; the returned handle can be used to cancel.
    // The final event delivered is always Done or Error.
    virtual StreamHandle ChatStream(const ChatRequest& request,
                                    StreamCallback onEvent) = 0;

    // Token counting (best-effort; may estimate for non-local providers).
    virtual int32_t CountTokens(const std::string& model,
                                const std::vector<Message>& messages) = 0;

    // Escape hatch: returns the underlying provider SDK object so callers
    // can use vendor-specific features the abstraction does not expose.
    // The pointer is owned by the adapter and must not be deleted.
    // Callers must know the concrete type (documented per adapter).
    virtual void* RawProvider() { return nullptr; }
};

// =====================================================================
// Factory / configuration
// =====================================================================

using TextLLMConfig = ProviderConfig;

// Construct an ITextLLM instance. Returns nullptr and fills `outError` on
// failure (e.g. provider not registered, missing credentials).
std::unique_ptr<ITextLLM> CreateTextLLM(const TextLLMConfig& config,
                                        Error* outError = nullptr);

// Enumerate provider adapters compiled into / loaded by the module.
std::vector<std::string> ListTextLLMProviders();

} // namespace UltraAI
