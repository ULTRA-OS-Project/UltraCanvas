// UltraAI/adapters/mock/src/MockEmbeddings.cpp
// Version: 0.1.0

#include "UltraAIMockEmbeddings.h"

#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>

namespace UltraAI {

namespace {

// Deterministic, normalized vector seeded by std::hash<std::string>.
EmbeddingVector HashedVector(const std::string& input, int32_t dims) {
    std::hash<std::string> h;
    uint64_t seed = h(input);
    EmbeddingVector v;
    v.values.resize(dims);
    double sumSq = 0.0;
    for (int32_t i = 0; i < dims; ++i) {
        // xorshift64*
        seed ^= seed >> 12; seed ^= seed << 25; seed ^= seed >> 27;
        uint64_t x = seed * 2685821657736338717ULL;
        // Map to [-1, 1]
        double f = (static_cast<double>(x >> 11) / static_cast<double>(1ULL << 53)) * 2.0 - 1.0;
        v.values[i] = static_cast<float>(f);
        sumSq += f * f;
    }
    double norm = std::sqrt(sumSq);
    if (norm > 0.0) {
        for (auto& f : v.values) f = static_cast<float>(f / norm);
    }
    return v;
}

} // namespace

struct MockEmbeddings::Impl {
    std::mutex mu;
    int32_t defaultDims = 8;
    std::deque<EmbeddingResponse> scripted;
    std::deque<Error> errors;
    int callCount = 0;
};

MockEmbeddings::MockEmbeddings() : impl_(std::make_unique<Impl>()) {}
MockEmbeddings::~MockEmbeddings() = default;

void MockEmbeddings::SetDimensions(int32_t dims) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->defaultDims = dims;
}
void MockEmbeddings::EnqueueResponse(EmbeddingResponse response) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->scripted.push_back(std::move(response));
}
void MockEmbeddings::EnqueueError(Error error) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->errors.push_back(std::move(error));
}
int MockEmbeddings::CallCount() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->callCount;
}

EmbeddingProviderCapabilities MockEmbeddings::GetCapabilities() const {
    EmbeddingProviderCapabilities caps;
    caps.providerId = "mock";
    caps.maxBatchSize = 1024;
    EmbeddingModelInfo m;
    m.id = "mock-embed-1";
    m.displayName = "UltraAI Mock Embeddings";
    m.maxInputTokens = 8192;
    m.outputDimensions = impl_->defaultDims;
    m.supportsVariableDimensions = true;
    m.runsLocally = true;
    caps.models.push_back(std::move(m));
    return caps;
}

EmbeddingResponse MockEmbeddings::Embed(const EmbeddingRequest& request) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    ++impl_->callCount;

    EmbeddingResponse resp;
    resp.model = request.model.empty() ? "mock-embed-1" : request.model;

    if (!impl_->errors.empty()) {
        resp.error = impl_->errors.front();
        impl_->errors.pop_front();
        return resp;
    }
    if (!impl_->scripted.empty()) {
        EmbeddingResponse out = std::move(impl_->scripted.front());
        impl_->scripted.pop_front();
        if (out.model.empty()) out.model = resp.model;
        return out;
    }

    int32_t dims = request.dimensions.value_or(impl_->defaultDims);
    resp.embeddings.reserve(request.input.size());
    for (const auto& s : request.input) {
        resp.embeddings.push_back(HashedVector(s, dims));
    }
    int32_t totalChars = 0;
    for (const auto& s : request.input) totalChars += static_cast<int32_t>(s.size());
    resp.usage.inputTokens = totalChars / 4;
    resp.usage.units = static_cast<int32_t>(request.input.size());
    return resp;
}

std::future<EmbeddingResponse> MockEmbeddings::EmbedAsync(
    const EmbeddingRequest& request) {
    return std::async(std::launch::async,
                      [this, request]() { return Embed(request); });
}

std::unique_ptr<IEmbeddings> CreateMockEmbeddings() {
    return std::make_unique<MockEmbeddings>();
}

} // namespace UltraAI
