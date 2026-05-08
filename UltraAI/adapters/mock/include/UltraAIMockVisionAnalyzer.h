// UltraAI/adapters/mock/include/UltraAIMockVisionAnalyzer.h
// Version: 0.1.0
#pragma once

#include "UltraAIVisionAnalyzer.h"
#include <memory>
#include <mutex>

namespace UltraAI {

class MockVisionAnalyzer : public IVisionAnalyzer {
public:
    MockVisionAnalyzer();
    ~MockVisionAnalyzer() override;

    // Test programming
    void EnqueueResponse(VisionAnalyzeResponse response);
    void EnqueueError(Error error);
    int CallCount() const;

    // IVisionAnalyzer
    VisionProviderCapabilities GetCapabilities() const override;
    VisionAnalyzeResponse Analyze(const VisionAnalyzeRequest& request) override;
    std::future<VisionAnalyzeResponse> AnalyzeAsync(
        const VisionAnalyzeRequest& request) override;
    void* RawProvider() override { return this; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<IVisionAnalyzer> CreateMockVisionAnalyzer();

} // namespace UltraAI
