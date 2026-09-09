// UltraAI/adapters/qwen/src/QwenDiscovery.h
// Local-endpoint discovery shared by the Qwen text and embeddings adapters:
// probe the candidate endpoints, read GET {base}/v1/models, and pick a
// default model. Cached per adapter instance behind a mutex, because both
// GetCapabilities() (const) and the request paths need it.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"
#include "UltraAITransport.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UltraAI {
namespace qwen_detail {

struct ServedModel {
    std::string id;
    bool looksLikeQwen = false;
    bool looksLikeEmbedding = false;
};

struct Discovery {
    std::string baseUrl;                  // resolved server root
    std::vector<ServedModel> models;      // what GET /v1/models reported
    bool serverAnswered = false;          // false -> models is empty
};

// Lazily resolve the local server once per adapter instance. Thread-safe:
// concurrent callers block on the first probe and share its result.
class DiscoveryCache {
public:
    // `explicitBaseUrl` is ProviderConfig::baseUrl (may be empty).
    // Returns false with *outError only when no endpoint could be reached
    // AND no explicit baseUrl was configured — an explicit endpoint that
    // does not answer /v1/models is still usable for requests that name a
    // model, so it resolves successfully with an empty model list.
    bool Resolve(const std::string& explicitBaseUrl,
                 const std::string& apiKey,
                 const std::shared_ptr<ITransport>& transport,
                 int timeoutMs, Discovery& out, Error* outError);

private:
    std::mutex mutex_;
    bool resolved_ = false;
    bool ok_ = false;
    Discovery value_;
    Error error_;
};

// The first served model whose id contains "qwen" (and, when
// `preferEmbedding`, also an embedding marker), else the first model of the
// right kind, else empty.
std::string PickDefaultModel(const std::vector<ServedModel>& models,
                             bool preferEmbedding);

} // namespace qwen_detail
} // namespace UltraAI
