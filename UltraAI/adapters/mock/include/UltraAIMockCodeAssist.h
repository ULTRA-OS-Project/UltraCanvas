// UltraAI/adapters/mock/include/UltraAIMockCodeAssist.h
// Version: 0.1.0
#pragma once

#include "UltraAICodeAssist.h"
#include <chrono>
#include <memory>
#include <mutex>

namespace UltraAI {

class MockCodeAssist : public ICodeAssist {
public:
    MockCodeAssist();
    ~MockCodeAssist() override;

    void EnqueueResponse(CodeAssistResponse response);
    void EnqueueError(Error error);
    void SetStreamDelay(std::chrono::milliseconds delay);  // default 0
    int CallCount() const;

    CodeAssistProviderCapabilities GetCapabilities() const override;
    CodeAssistResponse Run(const CodeAssistRequest& request) override;
    std::future<CodeAssistResponse> RunAsync(const CodeAssistRequest& request) override;
    StreamHandle RunStream(const CodeAssistRequest& request,
                           CodeStreamCallback onEvent) override;
    void* RawProvider() override { return this; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<ICodeAssist> CreateMockCodeAssist();

} // namespace UltraAI
