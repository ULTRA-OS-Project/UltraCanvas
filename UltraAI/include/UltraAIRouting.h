// UltraAI/include/UltraAIRouting.h
// Default-provider routing policy. When a caller leaves
// ProviderConfig::providerId empty, every Create<Capability> factory
// resolves the provider through this policy. The default route is LOCAL:
// a cloud (paid, online) provider is only ever used because someone chose
// it — never because nothing local happened to be registered.
//
//   1. SetDefaultProvider(capability, id)      — explicit app / OS setting
//   2. ULTRAAI_DEFAULT_<CAPABILITY> env var    — deployment configuration
//      (e.g. ULTRAAI_DEFAULT_TEXTLLM=anthropic)
//   3. a known local provider for the capability (local-first:
//      "llama-cpp" then "qwen" for textllm, "comfyui" for imagegen,
//      "whisper-cpp" for speechtotext, ...), then any other registered
//      local provider (sorted by id)
//   4. cloud providers (sorted by id) — ONLY when cloud fallback is
//      allowed (SetCloudFallbackAllowed(true) or
//      ULTRAAI_ALLOW_CLOUD_FALLBACK=1); off by default
//   5. "mock" — the in-process test double is the route of last resort
//
// Steps 1–2 name a provider explicitly, so they may name a cloud one
// whatever the fallback setting. Steps 1–3 only apply when the named
// provider is actually registered; otherwise resolution falls through. An
// explicit providerId always wins over all of this.
//
// Capability names: "textllm", "embeddings", "speechtotext",
// "texttospeech", "imagegen", "visionanalyzer", "translator", "videogen",
// "musicgen", "codeassist".
// Version: 0.2.0
// Last Modified: 2026-09-24
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

// Whether step 4 may route to cloud providers when no local one is
// registered (or constructible). Off by default; an application offers it
// as a setting. The ULTRAAI_ALLOW_CLOUD_FALLBACK environment variable
// ("1", "true", "yes", "on") turns it on for a deployment. Thread-safe.
void SetCloudFallbackAllowed(bool allowed);
// The effective setting: SetCloudFallbackAllowed(true) or the environment.
bool IsCloudFallbackAllowed();

// True for providers whose model runs on this machine — in-process
// ("llama-cpp") or behind a server the user runs ("qwen", "comfyui",
// "whisper-cpp", "piper", "stable-diffusion-cpp"). "mock" is neither local
// nor cloud; every other id counts as a cloud provider.
bool IsLocalProvider(const std::string& providerId);

// The resolution described above, applied to a concrete registered-provider
// list. Used by the capability factories; public so apps can preview what
// an empty providerId would resolve to. Returns empty when `registered`
// is empty.
std::string ResolveProviderId(const std::string& capability,
                              const std::vector<std::string>& registered);

// The full preference order (deduplicated; cloud providers appear only
// when named in steps 1–2 or when cloud fallback is allowed). The
// factories walk this list when the resolved provider fails to construct —
// e.g. a cloud adapter built without its transport — so an empty
// providerId yields a working instance whenever a provider the policy
// admits can construct one.
std::vector<std::string> ResolveProviderOrder(
    const std::string& capability,
    const std::vector<std::string>& registered);

} // namespace UltraAI
