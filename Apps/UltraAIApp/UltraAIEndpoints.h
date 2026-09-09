// Apps/UltraAIApp/UltraAIEndpoints.h
// A reusable "endpoint" is one configured model/provider — a name, a
// provider id, an optional base URL and default model — together with the
// set of AI capabilities ("modes") it is allowed to serve. One endpoint can
// back several modes (e.g. an OpenAI key serving Chat, Vision and TTS), so
// the dashboard's Settings dialog configures endpoints once instead of
// repeating provider/model/key rows in every per-mode dialog.
//
// Endpoints persist to a JSON file under the platform config directory
// (UltraAIApp/endpoints.json). API keys are NOT stored here — they live in
// UltraVault under the per-endpoint ref ai.endpoint.<id>.api_key.
// Version: 0.1.0
// Author: UltraAI Module
#pragma once

#include "UltraAITextLLM.h"   // UltraAI::TextLLMConfig (= ProviderConfig)

#include <set>
#include <string>
#include <vector>

namespace UltraAIApp {

// The ten UltraAI capabilities, in dashboard display order.
enum class AICapability {
    Chat,
    Embeddings,
    SpeechToText,
    TextToSpeech,
    Translation,
    ImageGen,
    Vision,
    VideoGen,
    MusicGen,
    CodeAssist
};

// One entry per capability: the enum, its stable serialization id, and a
// human label for checkboxes/menus.
struct CapabilityInfo {
    AICapability cap;
    const char*  id;     // stable, stored in JSON (e.g. "chat")
    const char*  label;  // shown to the user (e.g. "Chat (LLM)")
};

// All capabilities in display order.
const std::vector<CapabilityInfo>& AllCapabilities();

// Serialization id for a capability (stable across releases).
const char* CapabilityId(AICapability cap);
// Parse a serialization id back to a capability; false when unrecognised.
bool ParseCapability(const std::string& id, AICapability& out);

// A single configured endpoint.
struct Endpoint {
    std::string id;            // stable slug; also keys the vault ref
    std::string name;          // friendly display name
    std::string providerId;    // "openai" | "anthropic" | "llama-cpp" | "mock" | ...
    std::string baseUrl;       // optional self-hosted override
    std::string defaultModel;  // model id / GGUF path / checkpoint
    std::set<AICapability> modes;

    bool Supports(AICapability c) const { return modes.count(c) != 0; }

    // UltraVault reference where this endpoint's API key is stored. Keyed by
    // the endpoint id (not the provider) so two endpoints sharing a provider
    // keep distinct keys.
    std::string VaultRef() const { return "ai.endpoint." + id + ".api_key"; }
};

// Process-wide store, backed by endpoints.json.
class EndpointStore {
public:
    static EndpointStore& Instance();

    // Read/write the JSON file. Load() on a missing file succeeds with an
    // empty set. Both return false only on a real parse/IO error.
    bool Load();
    bool Save() const;

    const std::vector<Endpoint>& All() const { return endpoints_; }
    // Endpoints whose modes include `c`, in stored order.
    std::vector<Endpoint> ForCapability(AICapability c) const;
    const Endpoint* FindById(const std::string& id) const;

    // Insert (by id) or replace an existing endpoint.
    void Upsert(const Endpoint& e);
    void Remove(const std::string& id);

    // A unique slug derived from a friendly name (falls back to "endpoint").
    std::string MakeId(const std::string& name) const;

    static std::string ConfigDir();   // platform config dir + "/UltraAI"
    static std::string ConfigPath();  // ConfigDir() + "/endpoints.json"

private:
    EndpointStore() = default;
    std::vector<Endpoint> endpoints_;
};

// Build a TextLLMConfig (a ProviderConfig) from an endpoint. Sets the
// provider id, base URL, default model and the vault ref for the key; the
// network adapter resolves the key from UltraVault at call time.
UltraAI::TextLLMConfig ToTextLLMConfig(const Endpoint& e);

} // namespace UltraAIApp
