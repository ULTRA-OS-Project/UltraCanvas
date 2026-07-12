// UltraAI/include/UltraAIEmbeddings.h
// Provider-agnostic text embedding interface.
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

enum class EmbeddingTaskType {
    Default,
    SearchDocument,     // index-side
    SearchQuery,        // query-side
    Classification,
    Clustering,
    SemanticSimilarity
};

struct EmbeddingRequest {
    std::string model;                 // empty -> adapter default
    std::vector<std::string> input;    // batched
    std::optional<int32_t> dimensions; // truncated dims when supported
    EmbeddingTaskType taskType = EmbeddingTaskType::Default;
    OptionsMap options;
};

struct EmbeddingVector {
    std::vector<float> values;
};

struct EmbeddingResponse {
    std::string model;
    std::vector<EmbeddingVector> embeddings;  // one per input, same order
    TokenUsage usage;
    Error error;
};

// =====================================================================
// Capability discovery
// =====================================================================

struct EmbeddingModelInfo {
    std::string id;
    std::string displayName;
    int32_t maxInputTokens = 0;
    int32_t outputDimensions = 0;
    bool supportsVariableDimensions = false;
    bool runsLocally = false;
};

struct EmbeddingProviderCapabilities {
    std::string providerId;
    std::vector<EmbeddingModelInfo> models;
    int32_t maxBatchSize = 0;
};

// =====================================================================
// IEmbeddings — the capability interface
// =====================================================================

class IEmbeddings {
public:
    virtual ~IEmbeddings() = default;

    virtual EmbeddingProviderCapabilities GetCapabilities() const = 0;

    virtual EmbeddingResponse Embed(const EmbeddingRequest& request) = 0;

    virtual std::future<EmbeddingResponse> EmbedAsync(
        const EmbeddingRequest& request) = 0;

    // Convenience: cosine similarity between two equally-sized vectors.
    static double CosineSimilarity(const EmbeddingVector& a,
                                   const EmbeddingVector& b);

    virtual void* RawProvider() { return nullptr; }
};

// =====================================================================
// Factory
// =====================================================================

using EmbeddingsConfig = ProviderConfig;

std::unique_ptr<IEmbeddings> CreateEmbeddings(const EmbeddingsConfig& config,
                                              Error* outError = nullptr);

std::vector<std::string> ListEmbeddingsProviders();

bool RegisterEmbeddingsProvider(
    const std::string& providerId,
    std::function<std::unique_ptr<IEmbeddings>(const EmbeddingsConfig&, Error*)> factory);

} // namespace UltraAI
