// UltraAI/adapters/mock/include/UltraAIMockEmbeddings.h
// In-process mock IEmbeddings: deterministic vectors hashed from input.
// Version: 0.1.0
#pragma once

#include "UltraAIEmbeddings.h"
#include <memory>
#include <mutex>
#include <vector>

namespace UltraAI {

class MockEmbeddings : public IEmbeddings {
public:
    MockEmbeddings();
    ~MockEmbeddings() override;

    // Test programming
    void SetDimensions(int32_t dims);                    // default 8
    void EnqueueResponse(EmbeddingResponse response);
    void EnqueueError(Error error);
    int CallCount() const;

    // IEmbeddings
    EmbeddingProviderCapabilities GetCapabilities() const override;
    EmbeddingResponse Embed(const EmbeddingRequest& request) override;
    std::future<EmbeddingResponse> EmbedAsync(const EmbeddingRequest& request) override;
    void* RawProvider() override { return this; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<IEmbeddings> CreateMockEmbeddings();

} // namespace UltraAI
