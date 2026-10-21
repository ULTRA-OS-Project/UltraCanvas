// core/UltraIM/UltraIMManager.cpp
// UltraIM - provider registry, event bus and the UltraIM_* entry points
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "UltraIM/UltraIMCore.h"
#include "UltraCrypt/UltraCryptCore.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <system_error>
#include <tuple>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

namespace fs = std::filesystem;

using ProviderPtr = std::shared_ptr<IUltraIMProviderPlugin>;

// ============================================================================
// STATE
// One mutex guards everything below. It is never held while a provider, a
// subscriber or the dispatcher runs: a provider may raise an event from
// inside any call, and that must not deadlock.
// ============================================================================

struct ManagerState {
    std::mutex mutex;
    bool initialized = false;
    UltraIMConfig config;
    std::map<std::string, ProviderPtr> providers;
    UltraIMDispatcher dispatcher;
    std::map<UltraIMSubscriptionId, std::shared_ptr<UltraIMEventCallback>> subscribers;
    UltraIMSubscriptionId nextSubscriptionId = 1;
    std::set<std::string> loadedModules;     // canonical UTF-8 paths
    // Bumped by Shutdown: a task queued in the dispatcher before it drops out.
    uint64_t generation = 1;
};

ManagerState& State() {
    static ManagerState* state = new ManagerState();   // never destroyed: tasks may outlive main()
    return *state;
}

// Runs task through the installed dispatcher, or right here without one.
void Dispatch(std::function<void()> task) {
    UltraIMDispatcher dispatcher;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        dispatcher = State().dispatcher;
    }
    if (dispatcher) {
        dispatcher(std::move(task));
    } else {
        task();
    }
}

uint64_t CurrentGeneration() {
    std::lock_guard<std::mutex> lock(State().mutex);
    return State().generation;
}

bool IsGeneration(uint64_t generation) {
    std::lock_guard<std::mutex> lock(State().mutex);
    return State().initialized && State().generation == generation;
}

void DeliverEvent(const UltraIMEvent& event) {
    const uint64_t generation = CurrentGeneration();
    Dispatch([event, generation]() {
        std::vector<std::shared_ptr<UltraIMEventCallback>> targets;
        {
            std::lock_guard<std::mutex> lock(State().mutex);
            if (!State().initialized || State().generation != generation) return;
            for (const auto& entry : State().subscribers) targets.push_back(entry.second);
        }
        for (const auto& target : targets) {
            if (target && *target) (*target)(event);
        }
    });
}

// Wraps a provider callback so it reaches the application through the
// dispatcher, and not at all once the manager was shut down meanwhile.
template <typename... Args>
std::function<void(const Args&...)> Redispatch(std::function<void(const Args&...)> callback) {
    if (!callback) return {};
    const uint64_t generation = CurrentGeneration();
    return [callback, generation](const Args&... args) {
        auto task = [callback, generation, values = std::make_tuple(args...)]() {
            if (!IsGeneration(generation)) return;
            std::apply(callback, values);
        };
        Dispatch(std::move(task));
    };
}

ProviderPtr FindProvider(const std::string& providerId) {
    std::lock_guard<std::mutex> lock(State().mutex);
    if (!State().initialized) return nullptr;
    auto it = State().providers.find(providerId);
    return it == State().providers.end() ? nullptr : it->second;
}

std::vector<ProviderPtr> AllProviders() {
    std::lock_guard<std::mutex> lock(State().mutex);
    std::vector<ProviderPtr> result;
    for (const auto& entry : State().providers) result.push_back(entry.second);
    return result;
}

UltraIMResult NotFound(const std::string& providerId) {
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        if (!State().initialized)
            return UltraIMResult::Error(UltraIMResultCode::NotInitialized, "UltraIM is not initialized");
    }
    return UltraIMResult::Error(UltraIMResultCode::ProviderNotFound, "No provider '" + providerId + "'");
}

UltraIMProviderInfo DescribeProvider(const IUltraIMProviderPlugin& provider) {
    UltraIMProviderInfo info;
    info.providerId = provider.GetProviderId();
    info.displayName = provider.GetDisplayName();
    info.version = provider.GetVersion();
    info.iconPath = provider.GetIconPath();
    info.capabilities = provider.GetCapabilities();
    info.loginType = provider.GetLoginType();
    info.connectionState = provider.GetConnectionState();
    info.localUserId = provider.GetLocalUserId();
    return info;
}

// ============================================================================
// PROVIDER MODULES
// ============================================================================

std::atomic<int> g_modulesRegistered{0};   // counted by HostRegisterProvider during one refresh

void HostRegisterProvider(std::shared_ptr<IUltraIMProviderPlugin> provider) {
    if (UltraIM_RegisterProvider(std::move(provider))) ++g_modulesRegistered;
}

const UltraIMPluginHost g_pluginHost = {ULTRAIM_PLUGIN_HOST_ABI_VERSION, &HostRegisterProvider};

bool IsModuleFile(const fs::path& path) {
    const std::string ext = UltraCanvas::PathToUtf8(path.extension());
#if defined(_WIN32)
    return ext == ".dll" || ext == ".DLL";
#elif defined(__APPLE__)
    return ext == ".dylib" || ext == ".so";
#else
    return ext == ".so";
#endif
}

// Loads one module and calls its entry point. Modules are never unloaded:
// a provider's code (its vtable, the callbacks it handed out) must outlive
// every task still queued in the dispatcher.
void LoadModule(const fs::path& path) {
#if defined(_WIN32)
    HMODULE module = LoadLibraryW(path.c_str());   // path-string-ok: wide path
    if (!module) {
        std::fprintf(stderr, "[UltraIM] Cannot load %s (error %lu)\n",
                     UltraCanvas::PathToUtf8(path).c_str(), GetLastError());
        return;
    }
    auto init = reinterpret_cast<UltraIM_PluginInitFn>(
        reinterpret_cast<void*>(GetProcAddress(module, "UltraIM_PluginInit")));
#else
    void* module = dlopen(UltraCanvas::PathToUtf8(path).c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        std::fprintf(stderr, "[UltraIM] Cannot load %s: %s\n",
                     UltraCanvas::PathToUtf8(path).c_str(), dlerror());
        return;
    }
    auto init = reinterpret_cast<UltraIM_PluginInitFn>(dlsym(module, "UltraIM_PluginInit"));
#endif
    if (!init) {
        std::fprintf(stderr, "[UltraIM] %s has no UltraIM_PluginInit; skipped\n",
                     UltraCanvas::PathToUtf8(path).c_str());
        return;
    }
    init(&g_pluginHost);
}

std::string FallbackUuid() {
    std::random_device device;
    std::mt19937_64 engine((static_cast<uint64_t>(device()) << 32) ^ device());
    uint8_t bytes[16];
    for (auto& b : bytes) b = static_cast<uint8_t>(engine());
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out += '-';
        out += hex[bytes[i] >> 4];
        out += hex[bytes[i] & 0x0F];
    }
    return out;
}

UltraIMSendOptions WithClientId(const UltraIMSendOptions& options) {
    UltraIMSendOptions result = options;
    if (result.clientMessageId.empty()) result.clientMessageId = UltraIM_NewClientMessageId();
    return result;
}

} // namespace

// ============================================================================
// LIFECYCLE
// ============================================================================

UltraIMResult UltraIM_Initialize(const UltraIMConfig& config) {
    std::lock_guard<std::mutex> lock(State().mutex);
    if (State().initialized) return UltraIMResult::Ok();
    State().config = config;
    State().initialized = true;
    return UltraIMResult::Ok();
}

void UltraIM_Shutdown() {
    std::vector<ProviderPtr> providers;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        if (!State().initialized) return;
        for (auto& entry : State().providers) providers.push_back(entry.second);
        State().providers.clear();
        State().subscribers.clear();
        State().initialized = false;
        ++State().generation;
    }
    for (auto& provider : providers) {
        provider->SignOut();
        provider->Shutdown();
    }
    std::lock_guard<std::mutex> lock(State().mutex);
    State().dispatcher = nullptr;
}

bool UltraIM_IsInitialized() {
    std::lock_guard<std::mutex> lock(State().mutex);
    return State().initialized;
}

const UltraIMConfig& UltraIM_GetConfig() {
    return State().config;
}

// ============================================================================
// PROVIDERS
// ============================================================================

UltraIMResult UltraIM_RegisterProvider(std::shared_ptr<IUltraIMProviderPlugin> provider) {
    if (!provider) return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "No provider");
    const std::string id = provider->GetProviderId();
    if (id.empty()) return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Provider has no id");

    UltraIMConfig config;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        if (!State().initialized)
            return UltraIMResult::Error(UltraIMResultCode::NotInitialized, "UltraIM is not initialized");
        if (State().providers.count(id))
            return UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                        "Provider '" + id + "' is already registered");
        config = State().config;
    }

    UltraIMResult init = provider->Initialize(config, [](const UltraIMEvent& event) { DeliverEvent(event); });
    if (!init) return init;

    {
        std::lock_guard<std::mutex> lock(State().mutex);
        if (State().initialized && !State().providers.count(id)) {
            State().providers[id] = provider;
            return UltraIMResult::Ok();
        }
    }
    // Lost a race with another registration of the same id, or a shutdown.
    provider->Shutdown();
    return UltraIMResult::Error(UltraIMResultCode::InvalidArgument,
                                "Provider '" + id + "' could not be registered");
}

UltraIMResult UltraIM_UnregisterProvider(const std::string& providerId) {
    ProviderPtr provider;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        auto it = State().providers.find(providerId);
        if (it == State().providers.end())
            return UltraIMResult::Error(UltraIMResultCode::ProviderNotFound, "No provider '" + providerId + "'");
        provider = it->second;
        State().providers.erase(it);
    }
    provider->SignOut();
    provider->Shutdown();
    return UltraIMResult::Ok();
}

int UltraIM_RefreshProviders() {
    std::string directory;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        if (!State().initialized) return 0;
        directory = State().config.pluginDirectory;
    }
    if (directory.empty()) return 0;

    std::error_code ec;
    const fs::path base = UltraCanvas::PathFromUtf8(directory);
    if (!fs::is_directory(base, ec)) return 0;

    std::vector<fs::path> candidates;
    for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && IsModuleFile(it->path())) candidates.push_back(it->path());
    }
    std::sort(candidates.begin(), candidates.end());

    g_modulesRegistered = 0;
    for (const auto& candidate : candidates) {
        const std::string key = UltraCanvas::PathToUtf8(fs::weakly_canonical(candidate, ec));
        {
            std::lock_guard<std::mutex> lock(State().mutex);
            if (!State().loadedModules.insert(key).second) continue;
        }
        LoadModule(candidate);
    }
    return g_modulesRegistered;
}

std::vector<UltraIMProviderInfo> UltraIM_ListProviders() {
    std::vector<UltraIMProviderInfo> result;
    for (const auto& provider : AllProviders()) result.push_back(DescribeProvider(*provider));
    return result;
}

bool UltraIM_GetProviderInfo(const std::string& providerId, UltraIMProviderInfo& outInfo) {
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) return false;
    outInfo = DescribeProvider(*provider);
    return true;
}

// ============================================================================
// EVENTS
// ============================================================================

void UltraIM_SetEventDispatcher(UltraIMDispatcher dispatcher) {
    std::lock_guard<std::mutex> lock(State().mutex);
    State().dispatcher = std::move(dispatcher);
}

UltraIMSubscriptionId UltraIM_Subscribe(UltraIMEventCallback onEvent) {
    if (!onEvent) return 0;
    std::lock_guard<std::mutex> lock(State().mutex);
    if (!State().initialized) return 0;
    const UltraIMSubscriptionId id = State().nextSubscriptionId++;
    State().subscribers[id] = std::make_shared<UltraIMEventCallback>(std::move(onEvent));
    return id;
}

void UltraIM_Unsubscribe(UltraIMSubscriptionId subscriptionId) {
    std::lock_guard<std::mutex> lock(State().mutex);
    State().subscribers.erase(subscriptionId);
}

// ============================================================================
// SESSIONS
// ============================================================================

void UltraIM_SignIn(const std::string& providerId,
                    const UltraIMCredentials& credentials,
                    UltraIMResultCallback onResult) {
    auto callback = Redispatch<UltraIMResult>(std::move(onResult));
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) {
        if (callback) callback(NotFound(providerId));
        return;
    }
    provider->SignIn(credentials, callback ? callback : [](const UltraIMResult&) {});
}

void UltraIM_SignOut(const std::string& providerId) {
    if (ProviderPtr provider = FindProvider(providerId)) provider->SignOut();
}

UltraIMConnectionState UltraIM_GetConnectionState(const std::string& providerId) {
    ProviderPtr provider = FindProvider(providerId);
    return provider ? provider->GetConnectionState() : UltraIMConnectionState::Disconnected;
}

// ============================================================================
// DATA
// ============================================================================

void UltraIM_FetchConversations(UltraIMConversationsCallback onResult) {
    auto callback = Redispatch<UltraIMResult, std::vector<UltraIMConversation>>(std::move(onResult));
    if (!callback) return;

    std::vector<ProviderPtr> connected;
    for (const auto& provider : AllProviders()) {
        if (provider->GetConnectionState() == UltraIMConnectionState::Connected) connected.push_back(provider);
    }
    if (connected.empty()) {
        callback(UltraIMResult::Ok(), {});
        return;
    }

    struct Merge {
        std::mutex mutex;
        size_t remaining = 0;
        std::vector<UltraIMConversation> conversations;
        std::string errors;
    };
    auto merge = std::make_shared<Merge>();
    merge->remaining = connected.size();

    for (const auto& provider : connected) {
        const std::string id = provider->GetProviderId();
        provider->FetchConversations([merge, callback, id](const UltraIMResult& result,
                                                           const std::vector<UltraIMConversation>& batch) {
            bool last = false;
            {
                std::lock_guard<std::mutex> lock(merge->mutex);
                if (merge->remaining == 0) return;     // a provider answered twice
                if (result) {
                    merge->conversations.insert(merge->conversations.end(), batch.begin(), batch.end());
                } else {
                    if (!merge->errors.empty()) merge->errors += "; ";
                    merge->errors += id + ": " + result.message;
                }
                last = (--merge->remaining == 0);
            }
            if (!last) return;
            std::sort(merge->conversations.begin(), merge->conversations.end(),
                      [](const UltraIMConversation& a, const UltraIMConversation& b) {
                          return a.lastMessageTimeMs > b.lastMessageTimeMs;
                      });
            UltraIMResult summary = UltraIMResult::Ok();
            summary.message = merge->errors;
            callback(summary, merge->conversations);
        });
    }
}

void UltraIM_FetchMessages(const std::string& providerId,
                           const std::string& conversationId,
                           const std::string& beforeMessageId,
                           int limit,
                           UltraIMMessagesCallback onResult) {
    auto callback = Redispatch<UltraIMResult, std::vector<UltraIMMessage>>(std::move(onResult));
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) {
        if (callback) callback(NotFound(providerId), {});
        return;
    }
    provider->FetchMessages(conversationId, beforeMessageId, limit,
                            callback ? callback : [](const UltraIMResult&, const std::vector<UltraIMMessage>&) {});
}

void UltraIM_FetchContacts(const std::string& providerId, UltraIMContactsCallback onResult) {
    auto callback = Redispatch<UltraIMResult, std::vector<UltraIMContact>>(std::move(onResult));
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) {
        if (callback) callback(NotFound(providerId), {});
        return;
    }
    provider->FetchContacts(callback ? callback : [](const UltraIMResult&, const std::vector<UltraIMContact>&) {});
}

void UltraIM_AddContact(const std::string& providerId, const std::string& address,
                        UltraIMConversationCallback onResult) {
    auto callback = Redispatch<UltraIMResult, UltraIMConversation>(std::move(onResult));
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) {
        if (callback) callback(NotFound(providerId), {});
        return;
    }
    provider->AddContact(address, callback ? callback : [](const UltraIMResult&, const UltraIMConversation&) {});
}

void UltraIM_CreateGroup(const std::string& providerId, const std::string& title,
                         const std::vector<std::string>& memberIds,
                         UltraIMConversationCallback onResult) {
    auto callback = Redispatch<UltraIMResult, UltraIMConversation>(std::move(onResult));
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) {
        if (callback) callback(NotFound(providerId), {});
        return;
    }
    provider->CreateGroup(title, memberIds,
                          callback ? callback : [](const UltraIMResult&, const UltraIMConversation&) {});
}

// ============================================================================
// SENDING
// ============================================================================

UltraIMResult UltraIM_SendText(const std::string& providerId, const std::string& conversationId,
                               const std::string& text, const UltraIMSendOptions& options) {
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) return NotFound(providerId);
    if (conversationId.empty() || text.empty())
        return UltraIMResult::Error(UltraIMResultCode::InvalidArgument, "Empty conversation or text");
    return provider->SendText(conversationId, text, WithClientId(options));
}

UltraIMResult UltraIM_SendFile(const std::string& providerId, const std::string& conversationId,
                               const std::string& localPath, const UltraIMSendOptions& options) {
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) return NotFound(providerId);
    return provider->SendFile(conversationId, localPath, WithClientId(options));
}

UltraIMResult UltraIM_SendImage(const std::string& providerId, const std::string& conversationId,
                                const std::string& localPath, const std::string& caption,
                                const UltraIMSendOptions& options) {
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) return NotFound(providerId);
    return provider->SendImage(conversationId, localPath, caption, WithClientId(options));
}

UltraIMResult UltraIM_SendSticker(const std::string& providerId, const std::string& conversationId,
                                  const std::string& packId, const std::string& stickerId,
                                  const UltraIMSendOptions& options) {
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) return NotFound(providerId);
    return provider->SendSticker(conversationId, packId, stickerId, WithClientId(options));
}

UltraIMResult UltraIM_SendVoiceNote(const std::string& providerId, const std::string& conversationId,
                                    const std::string& localPath, const UltraIMSendOptions& options) {
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) return NotFound(providerId);
    return provider->SendVoiceNote(conversationId, localPath, WithClientId(options));
}

UltraIMResult UltraIM_DeleteMessage(const std::string& providerId, const std::string& conversationId,
                                    const std::string& messageId) {
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) return NotFound(providerId);
    return provider->DeleteMessage(conversationId, messageId);
}

UltraIMResult UltraIM_EditMessage(const std::string& providerId, const std::string& conversationId,
                                  const std::string& messageId, const std::string& newText) {
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) return NotFound(providerId);
    return provider->EditMessage(conversationId, messageId, newText);
}

UltraIMResult UltraIM_MarkRead(const std::string& providerId, const std::string& conversationId,
                               const std::string& upToMessageId) {
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) return NotFound(providerId);
    return provider->MarkRead(conversationId, upToMessageId);
}

UltraIMResult UltraIM_SetTyping(const std::string& providerId, const std::string& conversationId,
                                bool isTyping) {
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) return NotFound(providerId);
    return provider->SetTyping(conversationId, isTyping);
}

void UltraIM_FetchStickerPacks(const std::string& providerId, UltraIMStickerPacksCallback onResult) {
    auto callback = Redispatch<UltraIMResult, std::vector<UltraIMStickerPack>>(std::move(onResult));
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) {
        if (callback) callback(NotFound(providerId), {});
        return;
    }
    provider->FetchStickerPacks(callback ? callback
                                         : [](const UltraIMResult&, const std::vector<UltraIMStickerPack>&) {});
}

// ============================================================================
// CALLS
// ============================================================================

bool UltraIM_CanCall(const std::string& providerId, const std::string& conversationId, bool video) {
    ProviderPtr provider = FindProvider(providerId);
    return provider && provider->CanCall(conversationId, video);
}

UltraIMResult UltraIM_StartCall(const std::string& providerId, const std::string& conversationId,
                                bool video, std::string& outCallId) {
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) return NotFound(providerId);
    if (!provider->CanCall(conversationId, video))
        return UltraIMResult::Error(UltraIMResultCode::CapabilityMissing,
                                    "Calls are not available in this conversation");
    return provider->StartCall(conversationId, video, outCallId);
}

UltraIMResult UltraIM_AcceptCall(const std::string& providerId, const std::string& callId, bool video) {
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) return NotFound(providerId);
    return provider->AcceptCall(callId, video);
}

UltraIMResult UltraIM_EndCall(const std::string& providerId, const std::string& callId) {
    ProviderPtr provider = FindProvider(providerId);
    if (!provider) return NotFound(providerId);
    return provider->EndCall(callId);
}

// ============================================================================
// HELPERS
// ============================================================================

std::string UltraIM_NewClientMessageId() {
    std::string id;
    if (UltraCrypt_GenerateUuidV4(id)) return id;
    return FallbackUuid();
}
