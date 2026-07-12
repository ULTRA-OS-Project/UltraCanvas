// UltraAI/adapters/mock/include/UltraAIMockVideoGen.h
// Version: 0.1.0
#pragma once

#include "UltraAIVideoGen.h"
#include <chrono>
#include <memory>
#include <mutex>

namespace UltraAI {

class MockVideoGen : public IVideoGen {
public:
    MockVideoGen();
    ~MockVideoGen() override;

    void EnqueueResponse(VideoGenResponse response);
    void EnqueueError(Error error);
    void SetJobSteps(int steps);                                 // default 4
    void SetJobStepInterval(std::chrono::milliseconds interval); // default 2ms
    int CallCount() const;

    VideoGenProviderCapabilities GetCapabilities() const override;
    VideoGenResponse Generate(const VideoGenRequest& request) override;
    std::future<VideoGenResponse> GenerateAsync(const VideoGenRequest& request) override;
    StreamHandle GenerateJob(const VideoGenRequest& request,
                             VideoJobCallback onEvent) override;
    void* RawProvider() override { return this; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<IVideoGen> CreateMockVideoGen();

} // namespace UltraAI
