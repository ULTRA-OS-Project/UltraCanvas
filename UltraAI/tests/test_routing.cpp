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

    // Mock is the route of last resort...
    EXPECT_EQ(ResolveProviderId("textllm", {"mock"}), std::string("mock"));
    // ...real providers outrank it (sorted order among non-mock)...
    EXPECT_EQ(ResolveProviderId("textllm", {"mock", "anthropic"}),
              std::string("anthropic"));
    EXPECT_EQ(ResolveProviderId("textllm", {"openai", "mock", "anthropic"}),
              std::string("anthropic"));
    // ...and a known local provider outranks the cloud.
    EXPECT_EQ(ResolveProviderId("textllm",
                                {"openai", "mock", "anthropic", "llama-cpp"}),
              std::string("llama-cpp"));
    // Local-first is per capability: llama-cpp means nothing to a translator.
    EXPECT_EQ(ResolveProviderId("translator", {"mock", "llama-cpp"}),
              std::string("llama-cpp"));   // non-mock beats mock, not local rule
    EXPECT_EQ(ResolveProviderId("speechtotext",
                                {"mock", "whisper-cpp", "deepgram"}),
              std::string("whisper-cpp"));
    // Empty registry resolves to nothing.
    EXPECT_EQ(ResolveProviderId("textllm", {}), std::string(""));
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
    EXPECT_EQ(ResolveProviderId("textllm", {"anthropic", "mock"}),
              std::string("anthropic"));
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

    // An empty providerId must always yield a working instance: the route
    // prefers real providers and falls through ones that cannot construct
    // in this build (e.g. the Anthropic adapter without a transport) down
    // to the mock.
    TextLLMConfig cfg;                          // empty providerId
    auto llm = CreateTextLLM(cfg);
    EXPECT_TRUE(llm != nullptr);
    const std::string routed = llm->GetCapabilities().providerId;
    EXPECT_TRUE(routed == "anthropic" || routed == "llama-cpp" ||
                routed == "mock");

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
    TestExplicitDefault();
    TestEnvironmentDefault();
    TestLiveFactories();
    std::cout << "test_routing: all checks passed" << std::endl;
    return 0;
}
