// UltraAI/adapters/mock/include/UltraAIMockTranslator.h
// Version: 0.1.0
#pragma once

#include "UltraAITranslator.h"
#include <memory>
#include <mutex>

namespace UltraAI {

class MockTranslator : public ITranslator {
public:
    MockTranslator();
    ~MockTranslator() override;

    void EnqueueResponse(TranslateResponse response);
    void EnqueueError(Error error);
    int CallCount() const;

    TranslatorProviderCapabilities GetCapabilities() const override;
    TranslateResponse Translate(const TranslateRequest& request) override;
    std::future<TranslateResponse> TranslateAsync(const TranslateRequest& request) override;
    DetectLanguageResponse DetectLanguage(const DetectLanguageRequest& request) override;
    void* RawProvider() override { return this; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<ITranslator> CreateMockTranslator();

} // namespace UltraAI
