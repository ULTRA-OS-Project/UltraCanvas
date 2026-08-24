// UltraAI/include/UltraAIRouting.h
// Default-provider routing policy. When a caller leaves
// ProviderConfig::providerId empty, every Create<Capability> factory
// resolves the provider through this policy instead of hard-preferring
// the mock adapter:
//
//   1. SetDefaultProvider(capability, id)      — explicit app / OS setting
//   2. ULTRAAI_DEFAULT_<CAPABILITY> env var    — deployment configuration
//      (e.g. ULTRAAI_DEFAULT_TEXTLLM=anthropic)
//   3. a known local provider for the capability (local-first:
//      "llama-cpp" then "qwen" for textllm, "comfyui" for imagegen,
//      "whisper-cpp" for speechtotext, ...)
//   4. the first registered non-mock provider (sorted by id)
//   5. "mock" — the test double is the route of last resort
//
// Steps 1–3 only apply when the named provider is actually registered;
// otherwise resolution falls through. An explicit providerId always wins
// over all of this.
//
// Capability names: "textllm", "embeddings", "speechtotext",
// "texttospeech", "imagegen", "visionanalyzer", "translator", "videogen",
// "musicgen", "codeassist".
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module
#pragma once

#include <string>
#include <vector>

namespace UltraAI {

// Set (or clear, with an empty providerId) the preferred provider used
// when ProviderConfig::providerId is empty. Thread-safe.
void SetDefaultProvider(const std::string& capability,
                        const std::string& providerId);

// The explicitly configured default for a capability; empty when none set.
std::string GetDefaultProvider(const std::string& capability);

// Remove every explicitly configured default (mainly for tests).
void ClearDefaultProviders();

// The resolution described above, applied to a concrete registered-provider
// list. Used by the capability factories; public so apps can preview what
// an empty providerId would resolve to. Returns empty when `registered`
// is empty.
std::string ResolveProviderId(const std::string& capability,
                              const std::vector<std::string>& registered);

// The full preference order (deduplicated, every registered provider
// exactly once). The factories walk this list when the resolved provider
// fails to construct — e.g. a cloud adapter built without its transport —
// so an empty providerId always yields a working instance when any
// registered provider can construct one.
std::vector<std::string> ResolveProviderOrder(
    const std::string& capability,
    const std::vector<std::string>& registered);

} // namespace UltraAI
