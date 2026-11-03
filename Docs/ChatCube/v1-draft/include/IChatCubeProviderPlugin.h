// ChatCube/include/IChatCubeProviderPlugin.h
// Pure abstract interface for ChatCube messenger provider plugins.
// Every provider (Matrix, XMPP, Telegram, WhatsApp Business, ChatCube P2P)
// implements this interface. The ChatCubeProviderManager loads, registers,
// and routes between plugin instances at runtime.
//
// Pattern mirrors IFileLoaderPlugin from the FileLoader module.
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework
#pragma once

#include "ChatCubeTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace UltraCanvas {
namespace ChatCube {

// ============================================================================
// PLUGIN RESULT
// Returned by all plugin operations that can fail.
// Mirrors FileLoaderResult pattern from FileLoader module.
// ============================================================================

enum class ChatPluginResultCode {
    Success,
    ErrorNotAuthenticated,  // Operation requires authentication first
    ErrorNotConnected,      // Network connection is not established
    ErrorAuthFailed,        // Authentication credentials rejected
    ErrorCapabilityMissing, // Plugin does not support this operation
    ErrorNetworkTimeout,    // Network request timed out
    ErrorRateLimit,         // Provider rate-limited the request
    ErrorPermissionDenied,  // User or server denied the operation
    ErrorInvalidArgument,   // A parameter value is invalid
    ErrorNotFound,          // Message, contact or conversation not found
    ErrorGeneric            // Unspecified error — see ErrorMessage
};

struct ChatPluginResult {
    ChatPluginResultCode Code    = ChatPluginResultCode::Success;
    std::string          ErrorMessage;

    bool IsSuccess() const { return Code == ChatPluginResultCode::Success; }

    static ChatPluginResult Ok() {
        return { ChatPluginResultCode::Success, "" };
    }
    static ChatPluginResult Fail(ChatPluginResultCode code,
                                 const std::string& msg = "") {
        return { code, msg };
    }
};

// ============================================================================
// STICKER PACK INFO
// Returned by GetStickerPacks() for providers that support stickers
// ============================================================================

struct ChatStickerPack {
    std::string PackId;
    std::string Title;
    std::string ThumbnailPath;   // Local cached thumbnail
    std::vector<std::string> StickerIds;
    std::vector<std::string> StickerPaths; // Local cached paths (parallel to StickerIds)
};

// ============================================================================
// SEND OPTIONS
// Optional parameters for SendTextMessage and variants
// ============================================================================

struct ChatSendOptions {
    std::string       ReplyToMessageId; // Leave empty for no reply
    bool              Silent = false;   // Send without notification (if supported)
};

// ============================================================================
// IChatCubeProviderPlugin
// Pure abstract base — zero implementation here, only the contract.
// ============================================================================

class IChatCubeProviderPlugin {
public:
    virtual ~IChatCubeProviderPlugin() = default;

    // =========================================================================
    // SECTION 1 — PLUGIN IDENTITY
    // Called by ChatCubeProviderManager during registration.
    // Must be available before authentication.
    // =========================================================================

    // Unique stable identifier, e.g. "matrix", "telegram", "xmpp", "chatcube_p2p"
    virtual std::string GetPluginId() const = 0;

    // Human-readable name shown in the provider picker UI, e.g. "Telegram"
    virtual std::string GetDisplayName() const = 0;

    // Semver string e.g. "1.0.0"
    virtual std::string GetPluginVersion() const = 0;

    // Path to the provider's small icon (shown in contact list rows)
    // Loaded via FileLoader_LoadImage by the contact list template
    virtual std::string GetIconPath() const = 0;

    // Bitmask of features this plugin actually supports at runtime
    virtual ChatCapabilityFlags GetCapabilities() const = 0;

    // Login flow required for authentication
    virtual ChatLoginType GetLoginType() const = 0;

    // =========================================================================
    // SECTION 2 — CONNECTION LIFECYCLE
    // Called in order: Authenticate → [use] → Disconnect
    // =========================================================================

    // Begin authentication using the provided credentials.
    // This call is ASYNCHRONOUS — the plugin calls onResult when done.
    // The plugin must not block; return immediately.
    virtual void Authenticate(const ChatCredentials& credentials,
                              ChatAuthResultCallback onResult) = 0;

    // Request a QR code image path for QR-code login flows.
    // Plugin generates / fetches the QR code and calls onResult with the
    // local image path once ready. Only valid when GetLoginType() == QRCode.
    virtual void RequestQRCode(std::function<void(bool, const std::string&)> onResult) = 0;

    // Cleanly disconnect from the provider. May be called from any state.
    virtual void Disconnect() = 0;

    // Current connection state — polled by the manager for status display
    virtual ChatConnectionState GetConnectionState() const = 0;

    // True if the plugin is authenticated and the connection is active
    bool IsConnected() const {
        return GetConnectionState() == ChatConnectionState::Connected;
    }

    // =========================================================================
    // SECTION 3 — DATA RETRIEVAL (async, callback-based)
    // All retrieval methods are non-blocking.
    // Results are delivered via the callback on the plugin's worker thread;
    // ChatCubeProviderManager marshals them back to the UI thread.
    // =========================================================================

    // Fetch the full contact list.
    // Calls onResult once with all known contacts.
    virtual void GetContacts(ChatContactsCallback onResult) = 0;

    // Fetch all conversations (direct + groups).
    // Calls onResult once with all conversations.
    virtual void GetConversations(
        std::function<void(const std::vector<ChatConversation>&)> onResult) = 0;

    // Fetch message history for a conversation.
    // @param conversationId  Conversation to load
    // @param beforeMessageId Load messages older than this ID ("" = most recent)
    // @param limit           Max messages to return (0 = provider default)
    virtual void GetMessages(const std::string& conversationId,
                             const std::string& beforeMessageId,
                             int limit,
                             ChatMessagesCallback onResult) = 0;

    // =========================================================================
    // SECTION 4 — REQUIRED SEND OPERATIONS
    // Supported by all plugins (ChatCapabilityFlags::TextMessages always set)
    // =========================================================================

    // Send a plain text message.
    // Returns a provisional ChatPluginResult synchronously (queued/failed).
    // The actual delivery status is pushed via onMessageDelivered event.
    virtual ChatPluginResult SendTextMessage(const std::string& conversationId,
                                             const std::string& text,
                                             const ChatSendOptions& options = {}) = 0;

    // =========================================================================
    // SECTION 5 — OPTIONAL SEND OPERATIONS
    // Guarded by capability flags — call HasCapability() before invoking.
    // =========================================================================

    // Send a file attachment.
    // Requires ChatCapabilityFlags::FileTransfer.
    // @param localPath Full path to the file on disk (loaded via FileLoader)
    virtual ChatPluginResult SendFile(const std::string& conversationId,
                                      const std::string& localPath,
                                      const ChatSendOptions& options = {}) = 0;

    // Send an image (may use optimised image path vs generic file).
    // Requires ChatCapabilityFlags::ImageMessages.
    // @param localPath Path to image file (FileLoader_LoadImage was already called)
    virtual ChatPluginResult SendImage(const std::string& conversationId,
                                       const std::string& localPath,
                                       const std::string& caption = "",
                                       const ChatSendOptions& options = {}) = 0;

    // Send a sticker.
    // Requires ChatCapabilityFlags::Stickers.
    virtual ChatPluginResult SendSticker(const std::string& conversationId,
                                         const std::string& packId,
                                         const std::string& stickerId) = 0;

    // Send a voice note.
    // Requires ChatCapabilityFlags::VoiceNotes.
    // @param localPath Path to audio file recorded via IODeviceManager
    virtual ChatPluginResult SendVoiceNote(const std::string& conversationId,
                                           const std::string& localPath) = 0;

    // =========================================================================
    // SECTION 6 — MESSAGE ACTIONS
    // =========================================================================

    // Delete a sent message.
    // Requires ChatCapabilityFlags::DeleteMessage.
    virtual ChatPluginResult DeleteMessage(const std::string& messageId) = 0;

    // Edit a sent message's text content.
    // Requires ChatCapabilityFlags::EditMessage.
    virtual ChatPluginResult EditMessage(const std::string& messageId,
                                         const std::string& newText) = 0;

    // Mark all messages in a conversation as read.
    // Requires ChatCapabilityFlags::ReadReceipts.
    virtual ChatPluginResult MarkConversationRead(
        const std::string& conversationId) = 0;

    // Send typing started/stopped notification.
    // Requires ChatCapabilityFlags::TypingIndicator.
    virtual ChatPluginResult SetTypingStatus(const std::string& conversationId,
                                             bool isTyping) = 0;

    // =========================================================================
    // SECTION 7 — CALLS
    // Uses IODeviceManager for microphone / camera access at the app level.
    // The plugin handles signalling; the app handles device I/O.
    // =========================================================================

    // Initiate a voice call.
    // Requires ChatCapabilityFlags::VoiceCalls.
    virtual ChatPluginResult StartVoiceCall(const std::string& contactId) = 0;

    // Initiate a video call.
    // Requires ChatCapabilityFlags::VideoCalls.
    virtual ChatPluginResult StartVideoCall(const std::string& contactId) = 0;

    // Accept an incoming call (push-notified via onCallReceive event).
    virtual ChatPluginResult AcceptCall(const std::string& callId) = 0;

    // Reject or hang up a call.
    virtual ChatPluginResult EndCall(const std::string& callId) = 0;

    // =========================================================================
    // SECTION 8 — STICKER PACKS
    // Requires ChatCapabilityFlags::Stickers
    // =========================================================================

    // Return all available sticker packs for this provider.
    // Called when the user opens the sticker picker dialog.
    virtual void GetStickerPacks(
        std::function<void(const std::vector<ChatStickerPack>&)> onResult) = 0;

    // Download a sticker pack locally (thumbnails + all sticker images).
    // Progress reported via onProgress(0.0..1.0). Calls onResult when done.
    virtual void DownloadStickerPack(
        const std::string& packId,
        std::function<void(float)> onProgress,
        std::function<void(bool success)> onResult) = 0;

    // =========================================================================
    // SECTION 9 — EVENT SUBSCRIPTION
    // The plugin calls the registered callback whenever a push event arrives
    // from the server/network (new message, typing, presence change, etc.).
    // The manager registers a single callback; the plugin must call it
    // from a thread-safe context or document its threading model.
    // =========================================================================

    // Register the manager's event sink.
    // Replaces any previously registered callback.
    virtual void SubscribeToEvents(ChatPluginEventCallback callback) = 0;

    // Unregister the event callback (called on Disconnect).
    virtual void UnsubscribeFromEvents() = 0;

    // =========================================================================
    // SECTION 10 — AVATAR / MEDIA DOWNLOAD
    // The manager calls these to populate local cache paths used by
    // ChatMessageModel (DecorationRole) and the contact list Template.
    // =========================================================================

    // Download a contact's avatar to the local cache.
    // Calls onResult with the local file path on success, "" on failure.
    virtual void DownloadAvatar(
        const std::string& contactId,
        std::function<void(const std::string& localPath)> onResult) = 0;

    // Download a file attachment to the local cache.
    // Progress reported via onProgress(0.0..1.0).
    virtual void DownloadAttachment(
        const std::string& remoteUrl,
        const std::string& destPath,
        std::function<void(float)> onProgress,
        std::function<void(bool success, const std::string& localPath)> onResult) = 0;

    // =========================================================================
    // HELPER — capability guard (non-virtual, inlined)
    // Usage: if (!plugin->HasCapability(ChatCapabilityFlags::VoiceCalls)) return;
    // =========================================================================

    bool HasCapability(ChatCapabilityFlags cap) const {
        return UltraCanvas::ChatCube::HasCapability(GetCapabilities(), cap);
    }
};

} // namespace ChatCube
} // namespace UltraCanvas
