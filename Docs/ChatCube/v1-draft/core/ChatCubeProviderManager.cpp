// ChatCube/core/ChatCubeProviderManager.cpp
// Implementation of the ChatCube provider manager singleton.
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework

#include "ChatCubeProviderManager.h"
#include <algorithm>
#include <iostream>

namespace UltraCanvas {
namespace ChatCube {

// ============================================================================
// SINGLETON
// ============================================================================

ChatCubeProviderManager& ChatCubeProviderManager::GetInstance() {
    static ChatCubeProviderManager instance;
    return instance;
}

// ============================================================================
// LIFECYCLE
// ============================================================================

bool ChatCubeProviderManager::Initialize(const ChatManagerConfig& config) {
    std::lock_guard<std::mutex> lock(pluginMutex_);

    if (initialized_) {
        std::cerr << "[ChatCubeProviderManager] Already initialized." << std::endl;
        return true;
    }

    config_      = config;
    initialized_ = true;

    std::cout << "[ChatCubeProviderManager] Initialized. Plugin dir: "
              << config_.PluginDirectory << std::endl;
    return true;
}

void ChatCubeProviderManager::Shutdown() {
    DisconnectAll();

    std::lock_guard<std::mutex> lock(pluginMutex_);
    plugins_.clear();
    contacts_.clear();
    conversations_.clear();
    activeModel_      = nullptr;
    initialized_      = false;

    std::cout << "[ChatCubeProviderManager] Shutdown complete." << std::endl;
}

// ============================================================================
// PLUGIN REGISTRATION
// ============================================================================

void ChatCubeProviderManager::RegisterPlugin(
    std::shared_ptr<IChatCubeProviderPlugin> plugin) {
    if (!plugin) return;

    const std::string id = plugin->GetPluginId();
    std::lock_guard<std::mutex> lock(pluginMutex_);

    if (plugins_.count(id)) {
        std::cerr << "[ChatCubeProviderManager] Plugin already registered: "
                  << id << std::endl;
        return;
    }

    // Wire the event callback — plugin will call this from its worker thread
    plugin->SubscribeToEvents([this](const ChatPluginEvent& event) {
        HandlePluginEvent(event);
    });

    plugins_[id] = std::move(plugin);
    std::cout << "[ChatCubeProviderManager] Registered plugin: " << id << std::endl;
}

void ChatCubeProviderManager::UnregisterPlugin(const std::string& pluginId) {
    std::lock_guard<std::mutex> lock(pluginMutex_);
    auto it = plugins_.find(pluginId);
    if (it == plugins_.end()) return;

    it->second->UnsubscribeFromEvents();
    it->second->Disconnect();
    plugins_.erase(it);
    std::cout << "[ChatCubeProviderManager] Unregistered plugin: " << pluginId << std::endl;
}

std::vector<IChatCubeProviderPlugin*>
ChatCubeProviderManager::GetAllPlugins() const {
    std::lock_guard<std::mutex> lock(pluginMutex_);
    std::vector<IChatCubeProviderPlugin*> result;
    result.reserve(plugins_.size());
    for (const auto& kv : plugins_) {
        result.push_back(kv.second.get());
    }
    return result;
}

IChatCubeProviderPlugin*
ChatCubeProviderManager::GetPlugin(const std::string& pluginId) const {
    return FindPlugin(pluginId);
}

// ============================================================================
// AUTHENTICATION
// ============================================================================

void ChatCubeProviderManager::AuthenticateProvider(
    const std::string& pluginId,
    const ChatCredentials& credentials,
    ChatAuthResultCallback onResult) {

    auto* plugin = FindPlugin(pluginId);
    if (!plugin) {
        if (onResult) {
            onResult(false, "Plugin not found: " + pluginId);
        }
        return;
    }

    // Delegate to plugin — it calls onResult asynchronously
    plugin->Authenticate(credentials, onResult);
}

void ChatCubeProviderManager::DisconnectProvider(const std::string& pluginId) {
    auto* plugin = FindPlugin(pluginId);
    if (plugin) plugin->Disconnect();
}

void ChatCubeProviderManager::DisconnectAll() {
    std::lock_guard<std::mutex> lock(pluginMutex_);
    for (auto& kv : plugins_) {
        kv.second->Disconnect();
    }
}

std::vector<std::string>
ChatCubeProviderManager::GetConnectedProviderIds() const {
    std::lock_guard<std::mutex> lock(pluginMutex_);
    std::vector<std::string> ids;
    for (const auto& kv : plugins_) {
        if (kv.second->IsConnected()) {
            ids.push_back(kv.first);
        }
    }
    return ids;
}

// ============================================================================
// CONTACTS & CONVERSATIONS
// ============================================================================

void ChatCubeProviderManager::RefreshContacts() {
    std::lock_guard<std::mutex> lock(pluginMutex_);

    // Count connected plugins to know when merging is complete
    const int total = static_cast<int>(
        std::count_if(plugins_.begin(), plugins_.end(),
                      [](const auto& kv){ return kv.second->IsConnected(); }));

    if (total == 0) return;

    auto mergedContacts = std::make_shared<std::vector<ChatContact>>();
    auto remaining      = std::make_shared<int>(total);

    for (auto& kv : plugins_) {
        if (!kv.second->IsConnected()) continue;

        kv.second->GetContacts([this, mergedContacts, remaining]
                               (const std::vector<ChatContact>& batch) {
            // Merge batch (no lock needed — each plugin calls once)
            mergedContacts->insert(mergedContacts->end(),
                                   batch.begin(), batch.end());
            --(*remaining);

            if (*remaining == 0) {
                // All providers responded — commit and notify UI
                contacts_ = *mergedContacts;
                if (onContactListUpdate) onContactListUpdate();
            }
        });
    }
}

void ChatCubeProviderManager::RefreshConversations() {
    std::lock_guard<std::mutex> lock(pluginMutex_);

    const int total = static_cast<int>(
        std::count_if(plugins_.begin(), plugins_.end(),
                      [](const auto& kv){ return kv.second->IsConnected(); }));

    if (total == 0) return;

    auto merged    = std::make_shared<std::vector<ChatConversation>>();
    auto remaining = std::make_shared<int>(total);

    for (auto& kv : plugins_) {
        if (!kv.second->IsConnected()) continue;

        kv.second->GetConversations([this, merged, remaining]
                                    (const std::vector<ChatConversation>& batch) {
            merged->insert(merged->end(), batch.begin(), batch.end());
            --(*remaining);

            if (*remaining == 0) {
                conversations_ = *merged;
                // Sort by last message time (most recent first)
                // In a full implementation: parse LastMessageTime and sort by timestamp
                if (onContactListUpdate) onContactListUpdate();
            }
        });
    }
}

const std::vector<ChatContact>&
ChatCubeProviderManager::GetContacts() const {
    return contacts_;
}

const std::vector<ChatConversation>&
ChatCubeProviderManager::GetConversations() const {
    return conversations_;
}

// ============================================================================
// MESSAGE HISTORY
// ============================================================================

void ChatCubeProviderManager::LoadConversation(
    const std::string& providerId,
    const std::string& conversationId,
    ChatMessageModel* targetModel) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin || !targetModel) return;

    targetModel->Clear();

    // Load the most recent page (beforeMessageId = "" means latest)
    plugin->GetMessages(conversationId, "", config_.MessageCacheLimit,
                        [targetModel](const std::vector<ChatMessage>& messages) {
        targetModel->SetMessages(messages);
    });
}

void ChatCubeProviderManager::LoadOlderMessages(
    const std::string& providerId,
    const std::string& conversationId,
    const std::string& beforeMessageId,
    ChatMessageModel* targetModel) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin || !targetModel) return;

    plugin->GetMessages(conversationId, beforeMessageId, config_.MessageCacheLimit,
                        [targetModel](const std::vector<ChatMessage>& messages) {
        targetModel->PrependHistory(messages);
    });
}

// ============================================================================
// ACTIVE CONVERSATION TRACKING
// ============================================================================

void ChatCubeProviderManager::SetActiveConversation(
    const std::string& providerId,
    const std::string& conversationId,
    ChatMessageModel* model) {

    activeConversation_ = { providerId, conversationId };
    activeModel_        = model;
}

void ChatCubeProviderManager::ClearActiveConversation() {
    activeConversation_ = {};
    activeModel_        = nullptr;
}

const ChatActiveConversation&
ChatCubeProviderManager::GetActiveConversation() const {
    return activeConversation_;
}

// ============================================================================
// SEND OPERATIONS — all route through FindPlugin then delegate
// ============================================================================

ChatPluginResult ChatCubeProviderManager::SendTextMessage(
    const std::string& providerId,
    const std::string& conversationId,
    const std::string& text,
    const ChatSendOptions& options) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin) return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotFound,
                                               "Provider not found: " + providerId);
    return plugin->SendTextMessage(conversationId, text, options);
}

ChatPluginResult ChatCubeProviderManager::SendFile(
    const std::string& providerId,
    const std::string& conversationId,
    const std::string& localPath,
    const ChatSendOptions& options) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin) return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotFound);
    if (!plugin->HasCapability(ChatCapabilityFlags::FileTransfer))
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorCapabilityMissing,
                                     "Provider does not support file transfer");
    return plugin->SendFile(conversationId, localPath, options);
}

ChatPluginResult ChatCubeProviderManager::SendImage(
    const std::string& providerId,
    const std::string& conversationId,
    const std::string& localPath,
    const std::string& caption,
    const ChatSendOptions& options) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin) return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotFound);
    return plugin->SendImage(conversationId, localPath, caption, options);
}

ChatPluginResult ChatCubeProviderManager::SendSticker(
    const std::string& providerId,
    const std::string& conversationId,
    const std::string& packId,
    const std::string& stickerId) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin) return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotFound);
    if (!plugin->HasCapability(ChatCapabilityFlags::Stickers))
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorCapabilityMissing,
                                     "Provider does not support stickers");
    return plugin->SendSticker(conversationId, packId, stickerId);
}

ChatPluginResult ChatCubeProviderManager::SendVoiceNote(
    const std::string& providerId,
    const std::string& conversationId,
    const std::string& localPath) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin) return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotFound);
    if (!plugin->HasCapability(ChatCapabilityFlags::VoiceNotes))
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorCapabilityMissing,
                                     "Provider does not support voice notes");
    return plugin->SendVoiceNote(conversationId, localPath);
}

ChatPluginResult ChatCubeProviderManager::DeleteMessage(
    const std::string& providerId,
    const std::string& messageId) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin) return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotFound);
    if (!plugin->HasCapability(ChatCapabilityFlags::DeleteMessage))
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorCapabilityMissing);
    return plugin->DeleteMessage(messageId);
}

ChatPluginResult ChatCubeProviderManager::EditMessage(
    const std::string& providerId,
    const std::string& messageId,
    const std::string& newText) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin) return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotFound);
    if (!plugin->HasCapability(ChatCapabilityFlags::EditMessage))
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorCapabilityMissing);
    return plugin->EditMessage(messageId, newText);
}

ChatPluginResult ChatCubeProviderManager::SetTypingStatus(
    const std::string& providerId,
    const std::string& conversationId,
    bool isTyping) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin) return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotFound);
    if (!plugin->HasCapability(ChatCapabilityFlags::TypingIndicator))
        return ChatPluginResult::Ok(); // Silently skip — not an error
    return plugin->SetTypingStatus(conversationId, isTyping);
}

ChatPluginResult ChatCubeProviderManager::MarkConversationRead(
    const std::string& providerId,
    const std::string& conversationId) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin) return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotFound);
    if (!plugin->HasCapability(ChatCapabilityFlags::ReadReceipts))
        return ChatPluginResult::Ok();
    return plugin->MarkConversationRead(conversationId);
}

ChatPluginResult ChatCubeProviderManager::StartVoiceCall(
    const std::string& providerId,
    const std::string& contactId) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin) return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotFound);
    if (!plugin->HasCapability(ChatCapabilityFlags::VoiceCalls))
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorCapabilityMissing,
                                     "Provider does not support voice calls");
    return plugin->StartVoiceCall(contactId);
}

ChatPluginResult ChatCubeProviderManager::StartVideoCall(
    const std::string& providerId,
    const std::string& contactId) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin) return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotFound);
    if (!plugin->HasCapability(ChatCapabilityFlags::VideoCalls))
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorCapabilityMissing,
                                     "Provider does not support video calls");
    return plugin->StartVideoCall(contactId);
}

ChatPluginResult ChatCubeProviderManager::EndCall(
    const std::string& providerId,
    const std::string& callId) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin) return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotFound);
    return plugin->EndCall(callId);
}

// ============================================================================
// STICKER PACKS
// ============================================================================

void ChatCubeProviderManager::GetStickerPacks(
    const std::string& providerId,
    std::function<void(const std::vector<ChatStickerPack>&)> onResult) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin || !plugin->HasCapability(ChatCapabilityFlags::Stickers)) {
        if (onResult) onResult({});
        return;
    }
    plugin->GetStickerPacks(onResult);
}

void ChatCubeProviderManager::DownloadStickerPack(
    const std::string& providerId,
    const std::string& packId,
    std::function<void(float)> onProgress,
    std::function<void(bool)> onResult) {

    auto* plugin = FindPlugin(providerId);
    if (!plugin) {
        if (onResult) onResult(false);
        return;
    }
    plugin->DownloadStickerPack(packId, onProgress, onResult);
}

// ============================================================================
// INTERNAL EVENT DISPATCH
// Called from plugin worker threads — must be thread-safe.
// In a full implementation this would post to UltraCanvas's main event queue.
// For this version events are dispatched synchronously (plugins must ensure
// their callbacks are safe to call directly).
// ============================================================================

void ChatCubeProviderManager::HandlePluginEvent(const ChatPluginEvent& event) {
    switch (event.EventType) {

        case ChatPluginEventType::MessageReceived:
            RouteIncomingMessage(event);
            if (onMessageReceive) {
                onMessageReceive(event.ProviderId, event.Message);
            }
            break;

        case ChatPluginEventType::MessageDelivered:
            // Update delivery status in the active model if it matches
            if (activeModel_ &&
                activeConversation_.ProviderId == event.ProviderId &&
                activeConversation_.ConversationId == event.ConversationId) {
                activeModel_->UpdateDeliveryStatus(event.Message.MessageId,
                                                   event.DeliveryStatus);
            }
            if (onMessageStatusChange) {
                onMessageStatusChange(event.ProviderId,
                                      event.Message.MessageId,
                                      event.DeliveryStatus);
            }
            break;

        case ChatPluginEventType::TypingStarted:
        case ChatPluginEventType::TypingStopped:
            if (onTypingStatusChange) {
                onTypingStatusChange(
                    event.ProviderId,
                    event.ConversationId,
                    event.ContactId,
                    event.EventType == ChatPluginEventType::TypingStarted);
            }
            break;

        case ChatPluginEventType::ContactStatusChanged:
            if (onContactStatusChange) {
                onContactStatusChange(event.ProviderId,
                                      event.ContactId,
                                      event.PresenceStatus);
            }
            break;

        case ChatPluginEventType::ConversationUpdated:
            if (onConversationUpdate) {
                // Find and pass the updated conversation summary
                for (const auto& conv : conversations_) {
                    if (conv.ConversationId == event.ConversationId) {
                        onConversationUpdate(conv);
                        break;
                    }
                }
            }
            break;

        case ChatPluginEventType::ConnectionStateChanged:
            if (onConnectionStateChange) {
                onConnectionStateChange(event.ProviderId,
                                        event.ConnectionState);
            }
            break;

        case ChatPluginEventType::AuthRequired:
            if (onAuthRequire) {
                onAuthRequire(event.ProviderId);
            }
            break;

        default:
            break;
    }
}

void ChatCubeProviderManager::RouteIncomingMessage(const ChatPluginEvent& event) {
    // Only append to the active model if the message belongs to the
    // conversation currently open in the UI
    if (!activeModel_) return;
    if (activeConversation_.ProviderId     != event.ProviderId)     return;
    if (activeConversation_.ConversationId != event.ConversationId) return;

    activeModel_->AppendMessage(event.Message);
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

IChatCubeProviderPlugin*
ChatCubeProviderManager::FindPlugin(const std::string& pluginId) const {
    std::lock_guard<std::mutex> lock(pluginMutex_);
    auto it = plugins_.find(pluginId);
    if (it == plugins_.end()) {
        std::cerr << "[ChatCubeProviderManager] Plugin not found: "
                  << pluginId << std::endl;
        return nullptr;
    }
    return it->second.get();
}

} // namespace ChatCube
} // namespace UltraCanvas
