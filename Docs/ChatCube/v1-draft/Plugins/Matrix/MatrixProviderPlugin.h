// ChatCube/Plugins/Matrix/MatrixProviderPlugin.h
// Concrete implementation of IChatCubeProviderPlugin for the Matrix protocol.
// Uses the Matrix Client-Server API v3 (JSON over HTTPS).
// Authentication: username + password → access_token, or SSO/OIDC.
// Real-time events: long-poll /sync with next_batch token.
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework
#pragma once

#include "IChatCubeProviderPlugin.h"
#include "ChatCubeTypes.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory>
#include <unordered_map>
#include <chrono>

namespace UltraCanvas {
namespace ChatCube {

// ============================================================================
// MATRIX API CONSTANTS
// ============================================================================

namespace MatrixAPI {
    constexpr const char* CLIENT_V3       = "/_matrix/client/v3";
    constexpr const char* MEDIA_V3        = "/_matrix/media/v3";
    constexpr const char* WELLKNOWN       = "/.well-known/matrix/client";

    // Auth endpoints
    constexpr const char* LOGIN           = "/_matrix/client/v3/login";
    constexpr const char* LOGOUT          = "/_matrix/client/v3/logout";

    // Sync
    constexpr const char* SYNC            = "/_matrix/client/v3/sync";

    // Rooms / messages
    // Note: room_id and event_type are URL-encoded in the actual requests
    // PUT /_matrix/client/v3/rooms/{roomId}/send/{eventType}/{txnId}
    // GET /_matrix/client/v3/rooms/{roomId}/messages
    // GET /_matrix/client/v3/joined_rooms
    // GET /_matrix/client/v3/rooms/{roomId}/members

    // Presence
    // PUT /_matrix/client/v3/presence/{userId}/status

    // Typing
    // PUT /_matrix/client/v3/rooms/{roomId}/typing/{userId}

    // Read receipts
    // POST /_matrix/client/v3/rooms/{roomId}/read_markers

    // Media upload / download
    // POST /_matrix/media/v3/upload
    // GET  /_matrix/media/v3/download/{serverName}/{mediaId}
    // GET  /_matrix/media/v3/thumbnail/{serverName}/{mediaId}

    // User profile
    // GET /_matrix/client/v3/profile/{userId}
    // GET /_matrix/client/v3/profile/{userId}/avatar_url

    constexpr int SYNC_TIMEOUT_MS   = 30000; // Long-poll timeout
    constexpr int RETRY_DELAY_MS    = 3000;  // Reconnect delay
    constexpr int MAX_RETRIES       = 5;
}

// ============================================================================
// MATRIX INTERNAL TYPES
// Intermediate representations parsed from JSON responses
// ============================================================================

struct MatrixRoom {
    std::string RoomId;
    std::string Name;
    std::string Topic;
    std::string AvatarMxc;      // mxc:// URI, resolved to HTTP for download
    bool        IsDirect = false;
    std::string DirectUserId;   // For 1:1 rooms: the other user's ID
    int         UnreadCount = 0;
    std::string LastMessageBody;
    std::string LastMessageTs;
};

struct MatrixEvent {
    std::string EventId;
    std::string Type;           // "m.room.message", "m.typing", etc.
    std::string Sender;         // @user:server
    std::string RoomId;
    int64_t     OriginTs = 0;   // origin_server_ts milliseconds
    std::string MsgType;        // content.msgtype: "m.text", "m.image", etc.
    std::string Body;           // content.body
    std::string Url;            // content.url (mxc://) for media events
    std::string MimeType;       // content.info.mimetype
    int64_t     FileSize = 0;   // content.info.size
    int         ImgWidth = 0;   // content.info.w
    int         ImgHeight = 0;  // content.info.h
    // Relates-to (reply / edit)
    std::string RelType;        // content.m.relates_to.rel_type
    std::string RelatesToId;    // content.m.relates_to.event_id
    std::string InReplyToId;    // content.m.relates_to.m.in_reply_to.event_id
};

// ============================================================================
// MatrixProviderPlugin
// ============================================================================

class MatrixProviderPlugin : public IChatCubeProviderPlugin {
public:
    MatrixProviderPlugin();
    ~MatrixProviderPlugin() override;

    // =========================================================================
    // SECTION 1 — PLUGIN IDENTITY
    // =========================================================================

    std::string GetPluginId()      const override { return "matrix"; }
    std::string GetDisplayName()   const override { return "Matrix"; }
    std::string GetPluginVersion() const override { return "1.0.0"; }
    std::string GetIconPath()      const override;
    ChatCapabilityFlags GetCapabilities() const override;
    ChatLoginType GetLoginType()   const override {
        return ChatLoginType::UsernamePassword;
    }

    // =========================================================================
    // SECTION 2 — CONNECTION LIFECYCLE
    // =========================================================================

    void Authenticate(const ChatCredentials& credentials,
                      ChatAuthResultCallback onResult) override;

    void RequestQRCode(
        std::function<void(bool, const std::string&)> onResult) override;

    void Disconnect() override;

    ChatConnectionState GetConnectionState() const override;

    // =========================================================================
    // SECTION 3 — DATA RETRIEVAL
    // =========================================================================

    void GetContacts(ChatContactsCallback onResult) override;

    void GetConversations(
        std::function<void(const std::vector<ChatConversation>&)> onResult) override;

    void GetMessages(const std::string& conversationId,
                     const std::string& beforeMessageId,
                     int limit,
                     ChatMessagesCallback onResult) override;

    // =========================================================================
    // SECTION 4 — SEND (required)
    // =========================================================================

    ChatPluginResult SendTextMessage(const std::string& conversationId,
                                     const std::string& text,
                                     const ChatSendOptions& options = {}) override;

    // =========================================================================
    // SECTION 5 — SEND (optional)
    // =========================================================================

    ChatPluginResult SendFile(const std::string& conversationId,
                              const std::string& localPath,
                              const ChatSendOptions& options = {}) override;

    ChatPluginResult SendImage(const std::string& conversationId,
                               const std::string& localPath,
                               const std::string& caption = "",
                               const ChatSendOptions& options = {}) override;

    ChatPluginResult SendSticker(const std::string& conversationId,
                                 const std::string& packId,
                                 const std::string& stickerId) override;

    ChatPluginResult SendVoiceNote(const std::string& conversationId,
                                   const std::string& localPath) override;

    // =========================================================================
    // SECTION 6 — MESSAGE ACTIONS
    // =========================================================================

    ChatPluginResult DeleteMessage(const std::string& messageId) override;
    ChatPluginResult EditMessage(const std::string& messageId,
                                 const std::string& newText) override;
    ChatPluginResult MarkConversationRead(
        const std::string& conversationId) override;
    ChatPluginResult SetTypingStatus(const std::string& conversationId,
                                     bool isTyping) override;

    // =========================================================================
    // SECTION 7 — CALLS
    // Matrix uses MSC2746 (VoIP) — signalling via room events
    // =========================================================================

    ChatPluginResult StartVoiceCall(const std::string& contactId) override;
    ChatPluginResult StartVideoCall(const std::string& contactId) override;
    ChatPluginResult AcceptCall(const std::string& callId) override;
    ChatPluginResult EndCall(const std::string& callId) override;

    // =========================================================================
    // SECTION 8 — STICKER PACKS
    // Matrix uses MSC2545 image packs stored as room account data
    // =========================================================================

    void GetStickerPacks(
        std::function<void(const std::vector<ChatStickerPack>&)> onResult) override;

    void DownloadStickerPack(
        const std::string& packId,
        std::function<void(float)> onProgress,
        std::function<void(bool)> onResult) override;

    // =========================================================================
    // SECTION 9 — EVENT SUBSCRIPTION
    // =========================================================================

    void SubscribeToEvents(ChatPluginEventCallback callback) override;
    void UnsubscribeFromEvents() override;

    // =========================================================================
    // SECTION 10 — AVATAR / MEDIA DOWNLOAD
    // =========================================================================

    void DownloadAvatar(const std::string& contactId,
                        std::function<void(const std::string&)> onResult) override;

    void DownloadAttachment(
        const std::string& remoteUrl,
        const std::string& destPath,
        std::function<void(float)> onProgress,
        std::function<void(bool, const std::string&)> onResult) override;

private:
    // ── Connection state ──────────────────────────────────────────────────
    std::atomic<ChatConnectionState>    state_{ ChatConnectionState::Disconnected };
    std::string                         homeserverUrl_;
    std::string                         userId_;         // @user:server
    std::string                         accessToken_;
    std::string                         deviceId_;
    std::string                         nextBatch_;      // Sync pagination token

    // ── Long-poll sync thread ─────────────────────────────────────────────
    std::thread                         syncThread_;
    std::atomic<bool>                   syncRunning_{ false };

    // ── Event callback ────────────────────────────────────────────────────
    ChatPluginEventCallback             eventCallback_;
    mutable std::mutex                  callbackMutex_;

    // ── Room / user caches ────────────────────────────────────────────────
    std::unordered_map<std::string, MatrixRoom>   rooms_;   // roomId → room
    std::unordered_map<std::string, std::string>  userDisplayNames_; // userId → name
    std::unordered_map<std::string, std::string>  userAvatarMxc_;   // userId → mxc://

    // ── Transaction ID (monotonic counter for PUT /send) ──────────────────
    std::atomic<uint64_t>               txnId_{ 0 };
    std::string                         NextTxnId();

    // ── HTTP helpers ──────────────────────────────────────────────────────

    // Perform a synchronous HTTP GET. Returns response body or "" on error.
    // Sets httpStatus to the HTTP status code.
    std::string HttpGet(const std::string& url, int& httpStatus) const;

    // Perform a synchronous HTTP POST with JSON body.
    std::string HttpPost(const std::string& url,
                         const std::string& jsonBody,
                         int& httpStatus) const;

    // Perform a synchronous HTTP PUT with JSON body.
    std::string HttpPut(const std::string& url,
                        const std::string& jsonBody,
                        int& httpStatus) const;

    // Perform multipart file upload. Returns mxc:// URI or "" on error.
    std::string HttpUploadFile(const std::string& localPath,
                               const std::string& mimeType,
                               int& httpStatus) const;

    // Build a fully-qualified Matrix API URL
    std::string ApiUrl(const std::string& path) const;

    // Convert mxc://server/mediaId to HTTPS download URL
    std::string MxcToHttpUrl(const std::string& mxcUri) const;

    // ── Discovery ─────────────────────────────────────────────────────────

    // Resolve the homeserver URL from a Matrix user ID via .well-known
    // Falls back to https://<serverpart> if .well-known is not found
    std::string DiscoverHomeserver(const std::string& userId) const;

    // ── Sync loop ─────────────────────────────────────────────────────────

    void StartSyncLoop();
    void StopSyncLoop();

    // One sync iteration; updates nextBatch_ and fires eventCallback_
    void PerformSync();

    // ── Event parsing ─────────────────────────────────────────────────────

    // Parse a Matrix timeline event JSON into a ChatMessage
    ChatMessage ParseMatrixEvent(const MatrixEvent& evt,
                                 const std::string& roomId) const;

    // Parse the full /sync JSON response; dispatch events via callback
    void ProcessSyncResponse(const std::string& jsonBody);

    // Parse room timeline events from the rooms.join section
    void ProcessRoomTimeline(const std::string& roomId,
                             const std::string& timelineJson);

    // Parse ephemeral events (typing)
    void ProcessEphemeralEvents(const std::string& roomId,
                                const std::string& ephemeralJson);

    // Resolve a Matrix user's display name (cached)
    std::string ResolveDisplayName(const std::string& matrixUserId);

    // Resolve a Matrix user's avatar mxc:// URI (cached)
    std::string ResolveAvatarMxc(const std::string& matrixUserId);

    // ── Simple JSON helpers ───────────────────────────────────────────────
    // Minimal JSON extraction without a full parse library.
    // In production this would use nlohmann/json or similar.

    static std::string JsonGetString(const std::string& json,
                                     const std::string& key);
    static int64_t     JsonGetInt64(const std::string& json,
                                    const std::string& key);
    static std::string JsonBuildLoginPassword(const std::string& userId,
                                              const std::string& password,
                                              const std::string& deviceName);
    static std::string JsonBuildTextMessage(const std::string& body,
                                            const std::string& replyToEventId = "");
    static std::string JsonBuildMediaMessage(const std::string& msgType,
                                             const std::string& body,
                                             const std::string& mxcUrl,
                                             const std::string& mimeType,
                                             int64_t fileSize,
                                             int imgW = 0, int imgH = 0);
    static std::string JsonBuildRedaction(const std::string& reason = "");
    static std::string JsonBuildTyping(bool isTyping, int timeoutMs = 4000);
    static std::string JsonBuildReadMarkers(const std::string& eventId);
    static std::string UrlEncode(const std::string& str);
};

} // namespace ChatCube
} // namespace UltraCanvas
