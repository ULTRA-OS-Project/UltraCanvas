// UltraAI/adapters/mock/include/UltraAIMockImageGen.h
// Version: 0.1.0
#pragma once

#include "UltraAIImageGen.h"
#include <chrono>
#include <memory>
#include <mutex>

namespace UltraAI {

class MockImageGen : public IImageGen {
public:
    MockImageGen();
    ~MockImageGen() override;

    // Test programming
    void EnqueueImage(std::vector<uint8_t> bytes, std::string mimeType = "image/png");
    void EnqueueResponse(ImageGenResponse response);
    void EnqueueError(Error error);
    void SetJobStepInterval(std::chrono::milliseconds interval); // default 2ms
    void SetJobSteps(int steps);                                 // default 4
    int CallCount() const;

    // IImageGen
    ImageGenProviderCapabilities GetCapabilities() const override;
    ImageGenResponse Generate(const ImageGenRequest& request) override;
    std::future<ImageGenResponse> GenerateAsync(const ImageGenRequest& request) override;
    StreamHandle GenerateJob(const ImageGenRequest& request,
                             ImageJobCallback onEvent) override;
    void* RawProvider() override { return this; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<IImageGen> CreateMockImageGen();

} // namespace UltraAI
