// UltraAI/tests/test_routing.cpp
// Exercises the default-provider routing policy (UltraAIRouting.h): the
// resolution order on a plain provider list, explicit SetDefaultProvider,
// environment overrides, local-first, and the live behavior of
// CreateTextLLM with an empty providerId.
//
// Uses plain asserts so the test suite has no third-party dependency.

#include "UltraAI.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraAI;

namespace {

#define EXPECT_EQ(a, b) do { \
    if (!((a) == (b))) { std::cerr << "FAIL: " #a " == " #b " at " \
                  << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } \
} while (0)

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { std::cerr << "FAIL: " #cond " at " << __FILE__ << ":" \
                  << __LINE__ << std::endl; std::abort(); } \
} while (0)

void TestResolutionOrder() {
    ClearDefaultProviders();
    SetCloudFallbackAllowed(false);

    // Mock is the route of last resort...
    EXPECT_EQ(ResolveProviderId("textllm", {"mock"}), std::string("mock"));
    // ...and by default the route never falls back to a cloud provider:
    // a paid online service is only used when someone chose it.
    EXPECT_EQ(ResolveProviderId("textllm", {"mock", "anthropic"}),
              std::string("mock"));
    EXPECT_EQ(ResolveProviderId("texttospeech",
                                {"elevenlabs", "minimax", "mock"}),
              std::string("mock"));
    EXPECT_EQ(ResolveProviderId("textllm", {"openai", "anthropic"}),
              std::string(""));
    // A known local provider is the default.
    EXPECT_EQ(ResolveProviderId("textllm",
                                {"openai", "mock", "anthropic", "llama-cpp"}),
              std::string("llama-cpp"));
    EXPECT_EQ(ResolveProviderId("textllm", {"qwen", "llama-cpp"}),
              std::string("llama-cpp"));
    // Local-first is per capability, but any local provider still beats the
    // mock where the capability has no preference list of its own.
    EXPECT_EQ(ResolveProviderId("translator", {"mock", "llama-cpp"}),
              std::string("llama-cpp"));
    EXPECT_EQ(ResolveProviderId("speechtotext",
                                {"mock", "whisper-cpp", "deepgram"}),
              std::string("whisper-cpp"));
    // Cloud providers are not in the order at all.
    const std::vector<std::string> order = ResolveProviderOrder(
        "imagegen", {"minimax", "comfyui", "mock"});
    EXPECT_EQ(order.size(), size_t(2));
    EXPECT_EQ(order[0], std::string("comfyui"));
    EXPECT_EQ(order[1], std::string("mock"));
    // Empty registry resolves to nothing.
    EXPECT_EQ(ResolveProviderId("textllm", {}), std::string(""));

    EXPECT_TRUE(IsLocalProvider("llama-cpp"));
    EXPECT_TRUE(IsLocalProvider("comfyui"));
    EXPECT_TRUE(!IsLocalProvider("elevenlabs"));
    EXPECT_TRUE(!IsLocalProvider("mock"));
}

void TestCloudFallback() {
    ClearDefaultProviders();
    SetCloudFallbackAllowed(true);
    EXPECT_TRUE(IsCloudFallbackAllowed());
    // Allowed: cloud providers follow the local ones, sorted, before mock.
    EXPECT_EQ(ResolveProviderId("textllm", {"mock", "anthropic", "openai"}),
              std::string("anthropic"));
    EXPECT_EQ(ResolveProviderId("textllm",
                                {"openai", "mock", "anthropic", "llama-cpp"}),
              std::string("llama-cpp"));
    const std::vector<std::string> order = ResolveProviderOrder(
        "texttospeech", {"minimax", "elevenlabs", "mock"});
    EXPECT_EQ(order.size(), size_t(3));
    EXPECT_EQ(order[0], std::string("elevenlabs"));
    EXPECT_EQ(order[2], std::string("mock"));
    SetCloudFallbackAllowed(false);

    // The environment turns it on for a deployment.
    setenv("ULTRAAI_ALLOW_CLOUD_FALLBACK", "yes", 1);
    EXPECT_TRUE(IsCloudFallbackAllowed());
    EXPECT_EQ(ResolveProviderId("textllm", {"mock", "anthropic"}),
              std::string("anthropic"));
    setenv("ULTRAAI_ALLOW_CLOUD_FALLBACK", "0", 1);
    EXPECT_TRUE(!IsCloudFallbackAllowed());
    unsetenv("ULTRAAI_ALLOW_CLOUD_FALLBACK");
}

void TestExplicitDefault() {
    ClearDefaultProviders();
    SetDefaultProvider("textllm", "mock");
    EXPECT_EQ(GetDefaultProvider("textllm"), std::string("mock"));
    // The explicit default outranks local-first and real providers.
    EXPECT_EQ(ResolveProviderId("textllm", {"anthropic", "llama-cpp", "mock"}),
              std::string("mock"));
    // ...but only when actually registered.
    SetDefaultProvider("textllm", "not-built");
    EXPECT_EQ(ResolveProviderId("textllm", {"anthropic", "llama-cpp", "mock"}),
              std::string("llama-cpp"));
    // Naming a cloud provider explicitly is a choice, so it applies even
    // though cloud fallback is off.
    SetDefaultProvider("texttospeech", "elevenlabs");
    EXPECT_EQ(ResolveProviderId("texttospeech", {"elevenlabs", "mock"}),
              std::string("elevenlabs"));
    // Clearing restores the standard order.
    SetDefaultProvider("textllm", "");
    EXPECT_EQ(GetDefaultProvider("textllm"), std::string(""));
    ClearDefaultProviders();
}

void TestEnvironmentDefault() {
    ClearDefaultProviders();
    setenv("ULTRAAI_DEFAULT_TEXTLLM", "mock", 1);
    EXPECT_EQ(ResolveProviderId("textllm", {"anthropic", "mock"}),
              std::string("mock"));
    // An explicit SetDefaultProvider outranks the environment.
    SetDefaultProvider("textllm", "anthropic");
    EXPECT_EQ(ResolveProviderId("textllm", {"anthropic", "mock"}),
              std::string("anthropic"));
    ClearDefaultProviders();
    unsetenv("ULTRAAI_DEFAULT_TEXTLLM");
}

void TestLiveFactories() {
    ClearDefaultProviders();

    // An empty providerId yields a working instance: the route prefers
    // local providers and falls through ones that cannot construct in this
    // build down to the mock — never to a cloud provider by default.
    TextLLMConfig cfg;                          // empty providerId
    auto llm = CreateTextLLM(cfg);
    EXPECT_TRUE(llm != nullptr);
    const std::string routed = llm->GetCapabilities().providerId;
    EXPECT_TRUE(routed == "llama-cpp" || routed == "qwen" || routed == "mock");

    TextToSpeechConfig ttsCfg;
    auto tts = CreateTextToSpeech(ttsCfg);
    EXPECT_TRUE(tts != nullptr);
    EXPECT_TRUE(IsLocalProvider(tts->GetCapabilities().providerId) ||
                tts->GetCapabilities().providerId == "mock");

    // Routing can pin the mock back explicitly.
    SetDefaultProvider("textllm", "mock");
    auto mock = CreateTextLLM(cfg);
    EXPECT_TRUE(mock != nullptr);
    EXPECT_EQ(mock->GetCapabilities().providerId, std::string("mock"));
    ClearDefaultProviders();

    // Capabilities with only the mock registered still route to it.
    TranslatorConfig trCfg;
    auto translator = CreateTranslator(trCfg);
    EXPECT_TRUE(translator != nullptr);
}

} // namespace

int main() {
    TestResolutionOrder();
    TestCloudFallback();
    TestExplicitDefault();
    TestEnvironmentDefault();
    TestLiveFactories();
    std::cout << "test_routing: all checks passed" << std::endl;
    return 0;
}
