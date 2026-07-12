// UltraAI/adapters/mock/include/UltraAIMockMusicGen.h
// Version: 0.1.0
#pragma once

#include "UltraAIMusicGen.h"
#include <chrono>
#include <memory>
#include <mutex>

namespace UltraAI {

class MockMusicGen : public IMusicGen {
public:
    MockMusicGen();
    ~MockMusicGen() override;

    void EnqueueResponse(MusicGenResponse response);
    void EnqueueError(Error error);
    void SetJobSteps(int steps);
    void SetJobStepInterval(std::chrono::milliseconds interval);
    int CallCount() const;

    MusicGenProviderCapabilities GetCapabilities() const override;
    MusicGenResponse Generate(const MusicGenRequest& request) override;
    std::future<MusicGenResponse> GenerateAsync(const MusicGenRequest& request) override;
    StreamHandle GenerateJob(const MusicGenRequest& request,
                             MusicJobCallback onEvent) override;
    void* RawProvider() override { return this; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<IMusicGen> CreateMockMusicGen();

} // namespace UltraAI
