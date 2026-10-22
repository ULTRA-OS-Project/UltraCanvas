// ChatCube/include/ChatCubeProviderManager.h
// Singleton manager that owns all loaded provider plugins.
// Mirrors the pattern of UltraCanvasGraphicsPluginRegistry.
// Responsibilities:
//   - Load / unload plugin modules at runtime
//   - Route send calls to the correct plugin by provider ID
//   - Aggregate push events from all plugins and dispatch to UI callbacks
//   - Maintain the merged contact and conversation lists across providers
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework
#pragma once

#include "IChatCubeProviderPlugin.h"
#include "ChatCubeTypes.h"
#include "ChatMessageModel.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <mutex>

namespace UltraCanvas {
namespace ChatCube {

// ============================================================================
// MANAGER CONFIGURATION
// ============================================================================

struct ChatManagerConfig {
    // Directory scanned for .module plugin files on startup
    std::string PluginDirectory = "Modules/";

    // Local cache directory for downloaded avatars and attachments
    std::string CacheDirectory  = "Resources/Data/cache/";

    // Maximum number of messages to keep in memory per conversation
    int         MessageCacheLimit = 500;

    // Automatically reconnect plugins on connection loss
    bool        AutoReconnect   = true;
    int         ReconnectDelayMs = 5000;

    static ChatManagerConfig Default() { return {}; }
};

// ============================================================================
// ACTIVE CONVERSATION CONTEXT
// Tracks which conversation is currently open in the UI
// ============================================================================

struct ChatActiveConversation {
    std::string ProviderId;
    std::string ConversationId;
    bool        IsValid() const { return !ProviderId.empty() && !ConversationId.empty(); }
};

// ============================================================================
// ChatCubeProviderManager
// ============================================================================

class ChatCubeProviderManager {
public:
    // ── Singleton access ──────────────────────────────────────────────────
    static ChatCubeProviderManager& GetInstance();

    // Deleted to enforce singleton
    ChatCubeProviderManager(const ChatCubeProviderManager&) = delete;
    ChatCubeProviderManager& operator=(const ChatCubeProviderManager&) = delete;

    // =========================================================================
    // LIFECYCLE
    // =========================================================================

    // Initialise the manager and scan PluginDirectory for .module files.
    // Must be called once before any other method.
    bool Initialize(const ChatManagerConfig& config = ChatManagerConfig::Default());

    // Cleanly disconnect all plugins and release resources.
    // Call before application shutdown.
    void Shutdown();

    // =========================================================================
    // PLUGIN REGISTRATION
    // =========================================================================

    // Register a pre-constructed plugin (used for built-in plugins
    // like ChatCubeDirect that are compiled into the application).
    void RegisterPlugin(std::shared_ptr<IChatCubeProviderPlugin> plugin);

    // Unregister a plugin by its ID (disconnects first if connected).
    void UnregisterPlugin(const std::string& pluginId);

    // Returns all registered plugins (connected or not)
    std::vector<IChatCubeProviderPlugin*> GetAllPlugins() const;

    // Returns a specific plugin by ID, or nullptr if not found
    IChatCubeProviderPlugin* GetPlugin(const std::string& pluginId) const;

    // =========================================================================
    // AUTHENTICATION
    // =========================================================================

    // Authenticate a specific provider with the given credentials.
    // Calls onResult(success, errorText) on the UI thread when done.
    void AuthenticateProvider(const std::string& pluginId,
                              const ChatCredentials& credentials,
                              ChatAuthResultCallback onResult);

    // Disconnect a specific provider.
    void DisconnectProvider(const std::string& pluginId);

    // Disconnect all providers.
    void DisconnectAll();

    // Returns all currently connected provider IDs.
    std::vector<std::string> GetConnectedProviderIds() const;

    // =========================================================================
    // CONTACT & CONVERSATION LIST
    // Merged across all connected providers.
    // =========================================================================

    // Refresh the contact list from all connected providers.
    // Results are merged and delivered via onContactListUpdate.
    void RefreshContacts();

    // Refresh the conversation list from all connected providers.
    // Results are merged and delivered via onConversationListUpdate.
    void RefreshConversations();

    // Get the merged contact list (last fetched snapshot).
    const std::vector<ChatContact>& GetContacts() const;

    // Get the merged conversation list (last fetched snapshot).
    const std::vector<ChatConversation>& GetConversations() const;

    // =========================================================================
    // MESSAGE HISTORY
    // =========================================================================

    // Load message history for a conversation into a ChatMessageModel.
    // The model is populated with recent messages, then older pages can be
    // loaded via LoadOlderMessages().
    void LoadConversation(const std::string& providerId,
                          const std::string& conversationId,
                          ChatMessageModel* targetModel);

    // Load an older page of messages (prepended to the model).
    // @param beforeMessageId  Load messages older than this ID
    void LoadOlderMessages(const std::string& providerId,
                           const std::string& conversationId,
                           const std::string& beforeMessageId,
                           ChatMessageModel* targetModel);

    // =========================================================================
    // ACTIVE CONVERSATION TRACKING
    // The manager uses this to route incoming messages to the open model.
    // =========================================================================

    void SetActiveConversation(const std::string& providerId,
                               const std::string& conversationId,
                               ChatMessageModel* model);

    void ClearActiveConversation();

    const ChatActiveConversation& GetActiveConversation() const;

    // =========================================================================
    // SEND OPERATIONS (routed to correct plugin)
    // =========================================================================

    ChatPluginResult SendTextMessage(const std::string& providerId,
                                     const std::string& conversationId,
                                     const std::string& text,
                                     const ChatSendOptions& options = {});

    ChatPluginResult SendFile(const std::string& providerId,
                              const std::string& conversationId,
                              const std::string& localPath,
                              const ChatSendOptions& options = {});

    ChatPluginResult SendImage(const std::string& providerId,
                               const std::string& conversationId,
                               const std::string& localPath,
                               const std::string& caption = "",
                               const ChatSendOptions& options = {});

    ChatPluginResult SendSticker(const std::string& providerId,
                                 const std::string& conversationId,
                                 const std::string& packId,
                                 const std::string& stickerId);

    ChatPluginResult SendVoiceNote(const std::string& providerId,
                                   const std::string& conversationId,
                                   const std::string& localPath);

    ChatPluginResult DeleteMessage(const std::string& providerId,
                                   const std::string& messageId);

    ChatPluginResult EditMessage(const std::string& providerId,
                                 const std::string& messageId,
                                 const std::string& newText);

    ChatPluginResult SetTypingStatus(const std::string& providerId,
                                     const std::string& conversationId,
                                     bool isTyping);

    ChatPluginResult MarkConversationRead(const std::string& providerId,
                                          const std::string& conversationId);

    // Call operations
    ChatPluginResult StartVoiceCall(const std::string& providerId,
                                    const std::string& contactId);

    ChatPluginResult StartVideoCall(const std::string& providerId,
                                    const std::string& contactId);

    ChatPluginResult EndCall(const std::string& providerId,
                             const std::string& callId);

    // =========================================================================
    // STICKER PACKS
    // =========================================================================

    void GetStickerPacks(const std::string& providerId,
                         std::function<void(const std::vector<ChatStickerPack>&)> onResult);

    void DownloadStickerPack(const std::string& providerId,
                             const std::string& packId,
                             std::function<void(float)> onProgress,
                             std::function<void(bool)> onResult);

    // =========================================================================
    // UI EVENT CALLBACKS
    // Set by the main ChatCube window. All callbacks are invoked on the UI
    // thread (the manager marshals from plugin worker threads).
    // Naming: on[Object][Action] base verb form — master_callbacks_V4.1 compliant.
    // =========================================================================

    // New message received in any conversation
    std::function<void(const std::string& providerId,
                       const ChatMessage& message)>            onMessageReceive;

    // Delivery / read status updated for a sent message
    std::function<void(const std::string& providerId,
                       const std::string& messageId,
                       ChatDeliveryStatus status)>             onMessageStatusChange;

    // Remote user typing status changed
    std::function<void(const std::string& providerId,
                       const std::string& conversationId,
                       const std::string& contactId,
                       bool isTyping)>                         onTypingStatusChange;

    // A contact's presence/online status changed
    std::function<void(const std::string& providerId,
                       const std::string& contactId,
                       ChatPresenceStatus status)>             onContactStatusChange;

    // The contact/conversation list was refreshed (UI should rebuild template)
    std::function<void()>                                      onContactListUpdate;

    // A conversation's unread count, title, or last message changed
    std::function<void(const ChatConversation&)>               onConversationUpdate;

    // A provider's connection state changed
    std::function<void(const std::string& providerId,
                       ChatConnectionState state)>             onConnectionStateChange;

    // A provider requires re-authentication (session expired)
    std::function<void(const std::string& providerId)>         onAuthRequire;

    // An incoming call was received
    std::function<void(const std::string& providerId,
                       const std::string& callId,
                       const std::string& contactId,
                       bool isVideo)>                          onCallReceive;

private:
    // Private constructor — singleton
    ChatCubeProviderManager() = default;

    // ── Internal helpers ──────────────────────────────────────────────────

    // Called by each plugin's SubscribeToEvents callback.
    // Routes the event to the correct UI callback, marshalled to UI thread.
    void HandlePluginEvent(const ChatPluginEvent& event);

    // Append a new message to the active conversation model (if it matches).
    void RouteIncomingMessage(const ChatPluginEvent& event);

    // Find a plugin; returns nullptr and logs if not found.
    IChatCubeProviderPlugin* FindPlugin(const std::string& pluginId) const;

    // ── State ─────────────────────────────────────────────────────────────
    ChatManagerConfig                                          config_;
    bool                                                       initialized_ = false;

    // Registered plugins keyed by plugin ID
    std::unordered_map<std::string,
                       std::shared_ptr<IChatCubeProviderPlugin>> plugins_;

    // Merged data caches
    std::vector<ChatContact>      contacts_;
    std::vector<ChatConversation> conversations_;

    // Active conversation routing
    ChatActiveConversation        activeConversation_;
    ChatMessageModel*             activeModel_ = nullptr;

    // Thread safety for plugin event callbacks (plugins call from worker threads)
    mutable std::mutex            pluginMutex_;

    // Pending UI-thread dispatches (simple queue, drained each frame)
    // In a full implementation this would use UltraCanvas's event queue.
    // For now events are dispatched synchronously for clarity.
};

} // namespace ChatCube
} // namespace UltraCanvas
