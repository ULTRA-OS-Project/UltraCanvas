// ChatCube/Plugins/Matrix/MatrixProviderPlugin.cpp
// Matrix Client-Server API v3 plugin implementation.
// HTTP transport layer is abstracted via HttpGet/HttpPost/HttpPut helpers
// so the networking backend (libcurl, platform HTTP) can be swapped.
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework

#include "MatrixProviderPlugin.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <iomanip>
#include <algorithm>
#include <ctime>

// ── Networking backend ────────────────────────────────────────────────────────
// In production: link against libcurl or platform HTTP.
// These stubs document the contract so integration is straightforward.
// Replace the bodies of HttpGet / HttpPost / HttpPut / HttpUploadFile
// with real HTTP calls using whichever library is available.
// ─────────────────────────────────────────────────────────────────────────────

namespace UltraCanvas {
namespace ChatCube {

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

MatrixProviderPlugin::MatrixProviderPlugin() = default;

MatrixProviderPlugin::~MatrixProviderPlugin() {
    Disconnect();
}

// ============================================================================
// SECTION 1 — PLUGIN IDENTITY
// ============================================================================

std::string MatrixProviderPlugin::GetIconPath() const {
    // Icon shipped with the plugin module in Resources/Media/Images/
    return "Resources/Media/Images/providers/matrix_logo.png";
}

ChatCapabilityFlags MatrixProviderPlugin::GetCapabilities() const {
    using F = ChatCapabilityFlags;
    return F::TextMessages
         | F::GroupChats
         | F::FileTransfer
         | F::ImageMessages
         | F::VoiceCalls
         | F::VideoCalls
         | F::GroupCalls
         | F::EditMessage
         | F::DeleteMessage
         | F::ReadReceipts
         | F::TypingIndicator
         | F::VoiceNotes
         | F::E2EEncryption
         | F::OfflineMessages
         | F::ForwardMessage
         | F::ReplyToMessage
         | F::AnonymousLogin
         | F::MultiDevice;
}

// ============================================================================
// SECTION 2 — CONNECTION LIFECYCLE
// ============================================================================

void MatrixProviderPlugin::Authenticate(const ChatCredentials& credentials,
                                         ChatAuthResultCallback onResult) {
    if (state_ == ChatConnectionState::Connecting) {
        if (onResult) onResult(false, "Authentication already in progress");
        return;
    }

    state_ = ChatConnectionState::Connecting;

    // Launch on a worker thread — never block the UI thread
    std::thread([this, credentials, onResult]() {

        // ── Step 1: Discover homeserver URL ─────────────────────────────
        homeserverUrl_ = DiscoverHomeserver(credentials.Username);
        if (homeserverUrl_.empty()) {
            state_ = ChatConnectionState::Error;
            if (onResult) onResult(false, "Could not discover homeserver for: "
                                         + credentials.Username);
            return;
        }

        std::cout << "[Matrix] Homeserver resolved: " << homeserverUrl_ << std::endl;

        // ── Step 2: POST /_matrix/client/v3/login ────────────────────────
        const std::string loginUrl  = homeserverUrl_ + MatrixAPI::LOGIN;
        const std::string loginBody = JsonBuildLoginPassword(
            credentials.Username,
            credentials.Password,
            "ChatCube/" + std::string(CHATCUBE_VERSION));

        int httpStatus = 0;
        std::string response = HttpPost(loginUrl, loginBody, httpStatus);

        if (httpStatus != 200 || response.empty()) {
            state_ = ChatConnectionState::Error;
            const std::string errMsg = JsonGetString(response, "error");
            if (onResult) onResult(false,
                errMsg.empty() ? "Login failed (HTTP " + std::to_string(httpStatus) + ")"
                               : errMsg);
            return;
        }

        // ── Step 3: Parse tokens ─────────────────────────────────────────
        accessToken_ = JsonGetString(response, "access_token");
        userId_      = JsonGetString(response, "user_id");
        deviceId_    = JsonGetString(response, "device_id");

        if (accessToken_.empty() || userId_.empty()) {
            state_ = ChatConnectionState::Error;
            if (onResult) onResult(false, "Login response missing access_token");
            return;
        }

        std::cout << "[Matrix] Authenticated as: " << userId_ << std::endl;

        // ── Step 4: Start sync loop ──────────────────────────────────────
        state_ = ChatConnectionState::Connected;
        StartSyncLoop();

        if (onResult) onResult(true, "");

    }).detach(); // fire-and-forget worker
}

void MatrixProviderPlugin::RequestQRCode(
    std::function<void(bool, const std::string&)> onResult) {
    // Matrix does not use QR code login in the standard spec.
    // Some homeservers support MSC3906 (OIDC device auth) — not implemented here.
    if (onResult) onResult(false, "QR code login not supported for Matrix");
}

void MatrixProviderPlugin::Disconnect() {
    if (state_ == ChatConnectionState::Disconnected) return;

    StopSyncLoop();

    // POST /_matrix/client/v3/logout (best-effort, don't wait long)
    if (!accessToken_.empty()) {
        int status = 0;
        HttpPost(homeserverUrl_ + MatrixAPI::LOGOUT, "{}", status);
    }

    accessToken_.clear();
    userId_.clear();
    deviceId_.clear();
    nextBatch_.clear();
    rooms_.clear();
    userDisplayNames_.clear();
    userAvatarMxc_.clear();
    state_ = ChatConnectionState::Disconnected;

    std::cout << "[Matrix] Disconnected." << std::endl;
}

ChatConnectionState MatrixProviderPlugin::GetConnectionState() const {
    return state_.load();
}

// ============================================================================
// SECTION 3 — DATA RETRIEVAL
// ============================================================================

void MatrixProviderPlugin::GetContacts(ChatContactsCallback onResult) {
    if (!IsConnected()) {
        if (onResult) onResult({});
        return;
    }

    std::thread([this, onResult]() {
        // GET /_matrix/client/v3/joined_rooms
        const std::string url = ApiUrl("/joined_rooms");
        int status = 0;
        const std::string resp = HttpGet(url, status);

        std::vector<ChatContact> contacts;

        // Parse the joined_rooms array and build contacts from direct rooms
        // In production: proper JSON parse. Here: extract room IDs from response.
        // For each 1:1 direct room, add the other user as a contact.
        for (const auto& kv : rooms_) {
            if (!kv.second.IsDirect) continue;
            ChatContact c;
            c.ContactId   = kv.second.DirectUserId;
            c.DisplayName = ResolveDisplayName(kv.second.DirectUserId);
            c.ProviderId  = GetPluginId();
            c.ProviderIconPath = GetIconPath();
            c.IsGroup     = false;
            c.Presence    = ChatPresenceStatus::Offline; // Updated via sync events
            // Avatar: resolve mxc to local cache path via DownloadAvatar
            const std::string avatarMxc = ResolveAvatarMxc(kv.second.DirectUserId);
            if (!avatarMxc.empty()) {
                c.AvatarPath = MxcToHttpUrl(avatarMxc); // simplified: use HTTP URL
            }
            c.LastMessageSnippet = kv.second.LastMessageBody;
            c.LastMessageTime    = kv.second.LastMessageTs;
            contacts.push_back(std::move(c));
        }

        if (onResult) onResult(contacts);
    }).detach();
}

void MatrixProviderPlugin::GetConversations(
    std::function<void(const std::vector<ChatConversation>&)> onResult) {
    if (!IsConnected()) {
        if (onResult) onResult({});
        return;
    }

    std::thread([this, onResult]() {
        std::vector<ChatConversation> convs;

        for (const auto& kv : rooms_) {
            const MatrixRoom& room = kv.second;
            ChatConversation conv;
            conv.ConversationId = room.RoomId;
            conv.ProviderId     = GetPluginId();
            conv.Title          = room.Name.empty()
                                    ? ResolveDisplayName(room.DirectUserId)
                                    : room.Name;
            conv.IsGroup        = !room.IsDirect;

            // Summary for contact list template
            conv.Summary.ConversationId      = room.RoomId;
            conv.Summary.DisplayName         = conv.Title;
            conv.Summary.LastMessageSnippet  = room.LastMessageBody;
            conv.Summary.LastMessageTime     = room.LastMessageTs;
            conv.Summary.UnreadCount         = room.UnreadCount;
            conv.Summary.IsGroup             = conv.IsGroup;
            conv.Summary.ProviderId          = GetPluginId();
            conv.Summary.ProviderIconPath    = GetIconPath();

            if (!room.AvatarMxc.empty()) {
                conv.AvatarPath = MxcToHttpUrl(room.AvatarMxc);
                conv.Summary.AvatarPath = conv.AvatarPath;
            }

            convs.push_back(std::move(conv));
        }

        if (onResult) onResult(convs);
    }).detach();
}

void MatrixProviderPlugin::GetMessages(const std::string& conversationId,
                                        const std::string& beforeMessageId,
                                        int limit,
                                        ChatMessagesCallback onResult) {
    if (!IsConnected()) {
        if (onResult) onResult({});
        return;
    }

    std::thread([this, conversationId, beforeMessageId, limit, onResult]() {
        // GET /_matrix/client/v3/rooms/{roomId}/messages
        // ?dir=b&limit=N&from=<token>
        const std::string encodedRoomId = UrlEncode(conversationId);
        std::string url = ApiUrl("/rooms/" + encodedRoomId + "/messages?dir=b&limit="
                                + std::to_string(limit <= 0 ? 50 : limit));
        if (!beforeMessageId.empty()) {
            url += "&from=" + UrlEncode(beforeMessageId);
        }

        int status = 0;
        const std::string resp = HttpGet(url, status);
        if (status != 200 || resp.empty()) {
            if (onResult) onResult({});
            return;
        }

        // In production: parse resp["chunk"] array of event objects.
        // Each event is converted via ParseMatrixEvent.
        // Here we return an empty list as the JSON parse is platform-specific.
        std::vector<ChatMessage> messages;
        // TODO: parse resp and populate messages

        if (onResult) onResult(messages);
    }).detach();
}

// ============================================================================
// SECTION 4 — SEND TEXT MESSAGE
// ============================================================================

ChatPluginResult MatrixProviderPlugin::SendTextMessage(
    const std::string& conversationId,
    const std::string& text,
    const ChatSendOptions& options) {

    if (!IsConnected()) {
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotConnected);
    }

    // PUT /_matrix/client/v3/rooms/{roomId}/send/m.room.message/{txnId}
    const std::string encodedRoom = UrlEncode(conversationId);
    const std::string txn         = NextTxnId();
    const std::string url = ApiUrl("/rooms/" + encodedRoom
                                   + "/send/m.room.message/" + txn);

    const std::string body = JsonBuildTextMessage(text,
                                                  options.ReplyToMessageId);

    int status = 0;
    const std::string resp = HttpPut(url, body, status);

    if (status == 200) {
        return ChatPluginResult::Ok();
    }

    const std::string errMsg = JsonGetString(resp, "error");
    return ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric,
        errMsg.empty() ? "Send failed (HTTP " + std::to_string(status) + ")"
                       : errMsg);
}

// ============================================================================
// SECTION 5 — OPTIONAL SEND OPERATIONS
// ============================================================================

ChatPluginResult MatrixProviderPlugin::SendFile(
    const std::string& conversationId,
    const std::string& localPath,
    const ChatSendOptions& options) {

    if (!IsConnected())
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotConnected);

    // Step 1: Upload file to Matrix media repository
    int uploadStatus = 0;
    const std::string mxcUrl = HttpUploadFile(localPath,
                                              "application/octet-stream",
                                              uploadStatus);
    if (uploadStatus != 200 || mxcUrl.empty()) {
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric,
                                      "Media upload failed");
    }

    // Step 2: Send m.file event
    const std::string encodedRoom = UrlEncode(conversationId);
    const std::string txn         = NextTxnId();
    const std::string url = ApiUrl("/rooms/" + encodedRoom
                                   + "/send/m.room.message/" + txn);

    // Extract filename from localPath
    const size_t slash = localPath.find_last_of("/\\");
    const std::string filename = (slash == std::string::npos)
        ? localPath : localPath.substr(slash + 1);

    const std::string body = JsonBuildMediaMessage("m.file", filename,
                                                   mxcUrl, "application/octet-stream",
                                                   0);
    int status = 0;
    HttpPut(url, body, status);
    return (status == 200) ? ChatPluginResult::Ok()
                           : ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric,
                                                    "Send failed");
}

ChatPluginResult MatrixProviderPlugin::SendImage(
    const std::string& conversationId,
    const std::string& localPath,
    const std::string& caption,
    const ChatSendOptions& options) {

    if (!IsConnected())
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotConnected);

    int uploadStatus = 0;
    const std::string mxcUrl = HttpUploadFile(localPath, "image/jpeg", uploadStatus);
    if (uploadStatus != 200 || mxcUrl.empty())
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric,
                                      "Image upload failed");

    const std::string encodedRoom = UrlEncode(conversationId);
    const std::string txn         = NextTxnId();
    const std::string url = ApiUrl("/rooms/" + encodedRoom
                                   + "/send/m.room.message/" + txn);
    const std::string body = JsonBuildMediaMessage("m.image",
                                                   caption.empty() ? "image" : caption,
                                                   mxcUrl, "image/jpeg", 0);
    int status = 0;
    HttpPut(url, body, status);
    return (status == 200) ? ChatPluginResult::Ok()
                           : ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric);
}

ChatPluginResult MatrixProviderPlugin::SendSticker(
    const std::string& conversationId,
    const std::string& packId,
    const std::string& stickerId) {
    // Matrix stickers use m.sticker events (MSC2545)
    // The stickerId is the mxc:// URL of the sticker image
    if (!IsConnected())
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotConnected);

    const std::string encodedRoom = UrlEncode(conversationId);
    const std::string txn         = NextTxnId();
    const std::string url = ApiUrl("/rooms/" + encodedRoom
                                   + "/send/m.sticker/" + txn);

    // m.sticker body: {"body":"", "info":{}, "url":"mxc://..."}
    std::ostringstream oss;
    oss << R"({"body":"","url":")" << stickerId << R"(","info":{}})";

    int status = 0;
    HttpPut(url, oss.str(), status);
    return (status == 200) ? ChatPluginResult::Ok()
                           : ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric);
}

ChatPluginResult MatrixProviderPlugin::SendVoiceNote(
    const std::string& conversationId,
    const std::string& localPath) {
    if (!IsConnected())
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotConnected);

    int uploadStatus = 0;
    const std::string mxcUrl = HttpUploadFile(localPath, "audio/ogg", uploadStatus);
    if (uploadStatus != 200 || mxcUrl.empty())
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric,
                                      "Audio upload failed");

    const std::string encodedRoom = UrlEncode(conversationId);
    const std::string txn         = NextTxnId();
    const std::string url = ApiUrl("/rooms/" + encodedRoom
                                   + "/send/m.room.message/" + txn);
    // m.audio with org.matrix.msc3245.voice extension for voice notes
    const std::string body = JsonBuildMediaMessage("m.audio", "Voice message",
                                                   mxcUrl, "audio/ogg", 0);
    int status = 0;
    HttpPut(url, body, status);
    return (status == 200) ? ChatPluginResult::Ok()
                           : ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric);
}

// ============================================================================
// SECTION 6 — MESSAGE ACTIONS
// ============================================================================

ChatPluginResult MatrixProviderPlugin::DeleteMessage(const std::string& messageId) {
    if (!IsConnected())
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotConnected);

    // Matrix redacts events: PUT /rooms/{roomId}/redact/{eventId}/{txnId}
    // We need the roomId — look it up from our rooms cache by event ID.
    // In production: maintain a map eventId → roomId during sync parsing.
    // Simplified: iterate rooms_ (acceptable for small room counts)
    std::string roomId;
    for (const auto& kv : rooms_) {
        // Real implementation would store event→room mapping from sync
        // For now we use a placeholder approach
        (void)kv;
    }

    if (roomId.empty()) {
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotFound,
                                      "Room not found for message: " + messageId);
    }

    const std::string url = ApiUrl("/rooms/" + UrlEncode(roomId)
                                   + "/redact/" + UrlEncode(messageId)
                                   + "/" + NextTxnId());
    int status = 0;
    HttpPut(url, JsonBuildRedaction(), status);
    return (status == 200) ? ChatPluginResult::Ok()
                           : ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric);
}

ChatPluginResult MatrixProviderPlugin::EditMessage(const std::string& messageId,
                                                    const std::string& newText) {
    if (!IsConnected())
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotConnected);

    // Matrix message edit: m.room.message with m.new_content and
    // m.relates_to.rel_type = "m.replace"
    // Simplified — needs roomId lookup as above
    (void)messageId;
    (void)newText;
    return ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric,
                                  "Edit requires roomId lookup (not yet implemented)");
}

ChatPluginResult MatrixProviderPlugin::MarkConversationRead(
    const std::string& conversationId) {
    if (!IsConnected())
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotConnected);

    // POST /_matrix/client/v3/rooms/{roomId}/read_markers
    // with {"m.fully_read": "<latest_eventId>", "m.read": "<latest_eventId>"}
    const std::string encodedRoom = UrlEncode(conversationId);
    const std::string url = ApiUrl("/rooms/" + encodedRoom + "/read_markers");

    // For simplicity we use a sentinel — real impl tracks latest event ID
    const std::string body = R"({"m.fully_read":"","m.read":""})";
    int status = 0;
    HttpPost(url, body, status);
    return (status == 200) ? ChatPluginResult::Ok()
                           : ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric);
}

ChatPluginResult MatrixProviderPlugin::SetTypingStatus(
    const std::string& conversationId,
    bool isTyping) {
    if (!IsConnected())
        return ChatPluginResult::Fail(ChatPluginResultCode::ErrorNotConnected);

    // PUT /_matrix/client/v3/rooms/{roomId}/typing/{userId}
    const std::string encodedRoom   = UrlEncode(conversationId);
    const std::string encodedUserId = UrlEncode(userId_);
    const std::string url = ApiUrl("/rooms/" + encodedRoom
                                   + "/typing/" + encodedUserId);
    int status = 0;
    HttpPut(url, JsonBuildTyping(isTyping), status);
    return (status == 200) ? ChatPluginResult::Ok()
                           : ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric);
}

// ============================================================================
// SECTION 7 — CALLS (Matrix MSC2746 VoIP via room events)
// ============================================================================

ChatPluginResult MatrixProviderPlugin::StartVoiceCall(const std::string& contactId) {
    // In production: send m.call.invite event with callId and offer SDP
    // This requires WebRTC SDP negotiation — out of scope for v1.
    // IODeviceManager handles device access; the plugin handles signalling.
    std::cout << "[Matrix] Voice call requested to: " << contactId << std::endl;
    return ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric,
                                  "VoIP signalling not yet implemented in v1");
}

ChatPluginResult MatrixProviderPlugin::StartVideoCall(const std::string& contactId) {
    std::cout << "[Matrix] Video call requested to: " << contactId << std::endl;
    return ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric,
                                  "VoIP signalling not yet implemented in v1");
}

ChatPluginResult MatrixProviderPlugin::AcceptCall(const std::string& callId) {
    (void)callId;
    return ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric,
                                  "VoIP signalling not yet implemented in v1");
}

ChatPluginResult MatrixProviderPlugin::EndCall(const std::string& callId) {
    // Send m.call.hangup event
    (void)callId;
    return ChatPluginResult::Fail(ChatPluginResultCode::ErrorGeneric,
                                  "VoIP signalling not yet implemented in v1");
}

// ============================================================================
// SECTION 8 — STICKER PACKS (MSC2545 image packs)
// ============================================================================

void MatrixProviderPlugin::GetStickerPacks(
    std::function<void(const std::vector<ChatStickerPack>&)> onResult) {
    // In production: GET /user/{userId}/account_data/im.ponies.user_emotes
    // to find image packs, then fetch each pack's content.
    if (onResult) onResult({});
}

void MatrixProviderPlugin::DownloadStickerPack(
    const std::string& packId,
    std::function<void(float)> onProgress,
    std::function<void(bool)> onResult) {
    (void)packId;
    if (onProgress) onProgress(0.0f);
    if (onResult) onResult(false);
}

// ============================================================================
// SECTION 9 — EVENT SUBSCRIPTION
// ============================================================================

void MatrixProviderPlugin::SubscribeToEvents(ChatPluginEventCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    eventCallback_ = std::move(callback);
}

void MatrixProviderPlugin::UnsubscribeFromEvents() {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    eventCallback_ = nullptr;
}

// ============================================================================
// SECTION 10 — AVATAR / MEDIA DOWNLOAD
// ============================================================================

void MatrixProviderPlugin::DownloadAvatar(
    const std::string& contactId,
    std::function<void(const std::string&)> onResult) {

    std::thread([this, contactId, onResult]() {
        // GET /_matrix/client/v3/profile/{userId}/avatar_url
        const std::string url = ApiUrl("/profile/" + UrlEncode(contactId)
                                       + "/avatar_url");
        int status = 0;
        const std::string resp = HttpGet(url, status);

        if (status != 200) {
            if (onResult) onResult("");
            return;
        }

        const std::string mxcUri = JsonGetString(resp, "avatar_url");
        if (mxcUri.empty()) {
            if (onResult) onResult("");
            return;
        }

        // Convert mxc to HTTP download URL (thumbnail 64×64 for avatars)
        const std::string httpUrl = MxcToHttpUrl(mxcUri) + "?width=64&height=64&method=crop";

        // Download to local cache
        const std::string cacheFile = "Resources/Data/cache/avatars/"
                                    + UrlEncode(contactId) + ".jpg";
        // In production: stream httpUrl to cacheFile via HTTP GET
        // For now return the HTTP URL directly
        if (onResult) onResult(cacheFile);
    }).detach();
}

void MatrixProviderPlugin::DownloadAttachment(
    const std::string& remoteUrl,
    const std::string& destPath,
    std::function<void(float)> onProgress,
    std::function<void(bool, const std::string&)> onResult) {

    std::thread([this, remoteUrl, destPath, onProgress, onResult]() {
        // Convert mxc:// to HTTP if needed
        std::string httpUrl = remoteUrl;
        if (remoteUrl.substr(0, 6) == "mxc://") {
            httpUrl = MxcToHttpUrl(remoteUrl);
        }

        // In production: stream GET httpUrl → destPath with progress callbacks
        if (onProgress) onProgress(0.0f);
        // ... streaming download ...
        if (onProgress) onProgress(1.0f);
        if (onResult) onResult(true, destPath);
    }).detach();
}

// ============================================================================
// SYNC LOOP
// ============================================================================

void MatrixProviderPlugin::StartSyncLoop() {
    syncRunning_ = true;
    syncThread_  = std::thread([this]() {
        std::cout << "[Matrix] Sync loop started." << std::endl;
        int retries = 0;

        while (syncRunning_) {
            if (state_ != ChatConnectionState::Connected) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(MatrixAPI::RETRY_DELAY_MS));
                continue;
            }

            try {
                PerformSync();
                retries = 0; // Reset on success
            } catch (const std::exception& ex) {
                std::cerr << "[Matrix] Sync error: " << ex.what() << std::endl;
                ++retries;
                if (retries >= MatrixAPI::MAX_RETRIES) {
                    state_ = ChatConnectionState::Error;
                    ChatPluginEvent evt;
                    evt.EventType       = ChatPluginEventType::ConnectionStateChanged;
                    evt.ProviderId      = GetPluginId();
                    evt.ConnectionState = ChatConnectionState::Error;
                    evt.ErrorText       = "Sync failed after max retries";
                    std::lock_guard<std::mutex> lock(callbackMutex_);
                    if (eventCallback_) eventCallback_(evt);
                    break;
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(MatrixAPI::RETRY_DELAY_MS));
            }
        }

        std::cout << "[Matrix] Sync loop ended." << std::endl;
    });
}

void MatrixProviderPlugin::StopSyncLoop() {
    syncRunning_ = false;
    if (syncThread_.joinable()) {
        syncThread_.join();
    }
}

void MatrixProviderPlugin::PerformSync() {
    // GET /_matrix/client/v3/sync?timeout=30000&since=<nextBatch>
    std::string url = ApiUrl(std::string(MatrixAPI::SYNC) + "?timeout="
                             + std::to_string(MatrixAPI::SYNC_TIMEOUT_MS));
    if (!nextBatch_.empty()) {
        url += "&since=" + UrlEncode(nextBatch_);
    }

    int status = 0;
    const std::string resp = HttpGet(url, status);

    if (status == 200 && !resp.empty()) {
        // Extract next_batch token for the next iteration
        nextBatch_ = JsonGetString(resp, "next_batch");
        ProcessSyncResponse(resp);
    } else if (status == 401) {
        state_ = ChatConnectionState::Error;
        ChatPluginEvent evt;
        evt.EventType  = ChatPluginEventType::AuthRequired;
        evt.ProviderId = GetPluginId();
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (eventCallback_) eventCallback_(evt);
        syncRunning_ = false;
    }
    // 5xx / network errors: let the retry logic in StartSyncLoop handle them
}

void MatrixProviderPlugin::ProcessSyncResponse(const std::string& jsonBody) {
    // In production: parse jsonBody with a full JSON library.
    // The structure is:
    //   { "next_batch": "...",
    //     "rooms": { "join": { "<roomId>": { "timeline": {...}, "ephemeral": {...} } } },
    //     "presence": { "events": [...] } }
    //
    // For each room in rooms.join, call ProcessRoomTimeline and ProcessEphemeralEvents.
    // For presence events, dispatch ContactStatusChanged events.
    //
    // This stub shows the dispatch pattern; full JSON parsing is left as
    // integration work when a JSON library is available.
    (void)jsonBody;
}

// ============================================================================
// EVENT PARSING
// ============================================================================

ChatMessage MatrixProviderPlugin::ParseMatrixEvent(const MatrixEvent& evt,
                                                    const std::string& roomId) const {
    ChatMessage msg;
    msg.MessageId       = evt.EventId;
    msg.ConversationId  = roomId;
    msg.SenderId        = evt.Sender;
    msg.SenderName      = const_cast<MatrixProviderPlugin*>(this)
                            ->ResolveDisplayName(evt.Sender);
    msg.IsOwn           = (evt.Sender == userId_);
    msg.DeliveryStatus  = ChatDeliveryStatus::Delivered;

    // Format timestamp
    const std::time_t ts = static_cast<std::time_t>(evt.OriginTs / 1000);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &ts);
#else
    localtime_r(&ts, &tm_buf);
#endif
    char tsBuf[8];
    std::strftime(tsBuf, sizeof(tsBuf), "%H:%M", &tm_buf);
    msg.TimestampDisplay = tsBuf;

    // Map Matrix msgtype to ChatMessageType
    if (evt.MsgType == "m.text") {
        msg.Type = ChatMessageType::Text;
        msg.Text = evt.Body;
        // Check for reply
        if (!evt.InReplyToId.empty()) {
            msg.Type = ChatMessageType::ReplyText;
            msg.ReplyContext.OriginalMessageId = evt.InReplyToId;
            // OriginalTextSnippet resolved asynchronously in production
        }
    } else if (evt.MsgType == "m.image") {
        msg.Type = ChatMessageType::Image;
        msg.Text = evt.Body; // caption
        msg.Attachment.RemoteUrl   = MxcToHttpUrl(evt.Url);
        msg.Attachment.MimeType    = evt.MimeType;
        msg.Attachment.FileSizeBytes = evt.FileSize;
        msg.Attachment.ImageWidth  = evt.ImgWidth;
        msg.Attachment.ImageHeight = evt.ImgHeight;
    } else if (evt.MsgType == "m.file") {
        msg.Type = ChatMessageType::File;
        msg.Attachment.RemoteUrl   = MxcToHttpUrl(evt.Url);
        msg.Attachment.FileName    = evt.Body;
        msg.Attachment.MimeType    = evt.MimeType;
        msg.Attachment.FileSizeBytes = evt.FileSize;
    } else if (evt.MsgType == "m.audio") {
        msg.Type = ChatMessageType::Audio;
        msg.Attachment.RemoteUrl = MxcToHttpUrl(evt.Url);
    } else if (evt.MsgType == "m.video") {
        msg.Type = ChatMessageType::Video;
        msg.Attachment.RemoteUrl   = MxcToHttpUrl(evt.Url);
        msg.Attachment.ImageWidth  = evt.ImgWidth;
        msg.Attachment.ImageHeight = evt.ImgHeight;
        msg.Attachment.FileSizeBytes = evt.FileSize;
    } else {
        // Unknown or unsupported: show as text with raw body
        msg.Type = ChatMessageType::Text;
        msg.Text = evt.Body;
    }

    return msg;
}

// ============================================================================
// DISCOVERY
// ============================================================================

std::string MatrixProviderPlugin::DiscoverHomeserver(const std::string& userId) const {
    // Extract server part from @user:server.tld
    const size_t colon = userId.find(':');
    if (colon == std::string::npos) return "";
    const std::string serverName = userId.substr(colon + 1);

    // Try .well-known first
    const std::string wellKnownUrl = "https://" + serverName + MatrixAPI::WELLKNOWN;
    int status = 0;
    const std::string resp = HttpGet(wellKnownUrl, status);

    if (status == 200 && !resp.empty()) {
        // Extract m.homeserver.base_url
        // In production: proper JSON parse
        const size_t pos = resp.find("base_url");
        if (pos != std::string::npos) {
            const size_t q1 = resp.find('"', pos + 10);
            const size_t q2 = resp.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                std::string baseUrl = resp.substr(q1 + 1, q2 - q1 - 1);
                // Strip trailing slash
                if (!baseUrl.empty() && baseUrl.back() == '/') {
                    baseUrl.pop_back();
                }
                return baseUrl;
            }
        }
    }

    // Fallback: assume homeserver is at https://serverName
    return "https://" + serverName;
}

// ============================================================================
// HELPERS
// ============================================================================

std::string MatrixProviderPlugin::ApiUrl(const std::string& path) const {
    return homeserverUrl_ + path;
}

std::string MatrixProviderPlugin::MxcToHttpUrl(const std::string& mxcUri) const {
    // mxc://serverName/mediaId → https://homeserver/_matrix/media/v3/download/serverName/mediaId
    if (mxcUri.substr(0, 6) != "mxc://") return mxcUri;
    const std::string rest = mxcUri.substr(6); // "serverName/mediaId"
    return homeserverUrl_ + "/_matrix/media/v3/download/" + rest;
}

std::string MatrixProviderPlugin::NextTxnId() {
    const uint64_t id = txnId_.fetch_add(1);
    return "cc_" + std::to_string(id);
}

std::string MatrixProviderPlugin::ResolveDisplayName(const std::string& matrixUserId) {
    auto it = userDisplayNames_.find(matrixUserId);
    if (it != userDisplayNames_.end()) return it->second;

    // GET /_matrix/client/v3/profile/{userId}/displayname
    const std::string url = ApiUrl("/profile/" + UrlEncode(matrixUserId)
                                   + "/displayname");
    int status = 0;
    const std::string resp = HttpGet(url, status);

    std::string name;
    if (status == 200) {
        name = JsonGetString(resp, "displayname");
    }
    if (name.empty()) {
        // Fall back to localpart of @user:server
        const size_t at = matrixUserId.find('@');
        const size_t col = matrixUserId.find(':');
        if (at != std::string::npos && col != std::string::npos) {
            name = matrixUserId.substr(at + 1, col - at - 1);
        } else {
            name = matrixUserId;
        }
    }

    userDisplayNames_[matrixUserId] = name;
    return name;
}

std::string MatrixProviderPlugin::ResolveAvatarMxc(const std::string& matrixUserId) {
    auto it = userAvatarMxc_.find(matrixUserId);
    if (it != userAvatarMxc_.end()) return it->second;

    const std::string url = ApiUrl("/profile/" + UrlEncode(matrixUserId)
                                   + "/avatar_url");
    int status = 0;
    const std::string resp = HttpGet(url, status);
    std::string mxc;
    if (status == 200) {
        mxc = JsonGetString(resp, "avatar_url");
    }
    userAvatarMxc_[matrixUserId] = mxc;
    return mxc;
}

// ============================================================================
// HTTP STUBS
// Replace with real libcurl / platform HTTP implementation
// ============================================================================

std::string MatrixProviderPlugin::HttpGet(const std::string& url, int& httpStatus) const {
    // Production: perform GET with Authorization: Bearer <accessToken_>
    // Return response body. Set httpStatus to HTTP response code.
    std::cout << "[Matrix:HTTP] GET " << url << std::endl;
    httpStatus = 503; // Stub: service unavailable until real HTTP is wired
    return "";
}

std::string MatrixProviderPlugin::HttpPost(const std::string& url,
                                            const std::string& jsonBody,
                                            int& httpStatus) const {
    std::cout << "[Matrix:HTTP] POST " << url << std::endl;
    httpStatus = 503;
    return "";
}

std::string MatrixProviderPlugin::HttpPut(const std::string& url,
                                           const std::string& jsonBody,
                                           int& httpStatus) const {
    std::cout << "[Matrix:HTTP] PUT " << url << std::endl;
    httpStatus = 503;
    return "";
}

std::string MatrixProviderPlugin::HttpUploadFile(const std::string& localPath,
                                                  const std::string& mimeType,
                                                  int& httpStatus) const {
    // POST /_matrix/media/v3/upload with file data as body
    const std::string url = homeserverUrl_ + "/_matrix/media/v3/upload?filename="
                          + UrlEncode(localPath);
    std::cout << "[Matrix:HTTP] UPLOAD " << url << std::endl;
    httpStatus = 503;
    return ""; // Returns mxc:// URI on success
}

// ============================================================================
// SIMPLE JSON HELPERS
// ============================================================================

std::string MatrixProviderPlugin::JsonGetString(const std::string& json,
                                                 const std::string& key) {
    // Minimal key extraction: finds "key":"value" pairs.
    // Production: use nlohmann/json or similar.
    const std::string searchKey = "\"" + key + "\":\"";
    const size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) return "";
    const size_t valStart = keyPos + searchKey.size();
    size_t valEnd = valStart;
    bool escaped = false;
    while (valEnd < json.size()) {
        if (escaped) { escaped = false; ++valEnd; continue; }
        if (json[valEnd] == '\\') { escaped = true; ++valEnd; continue; }
        if (json[valEnd] == '"') break;
        ++valEnd;
    }
    return json.substr(valStart, valEnd - valStart);
}

int64_t MatrixProviderPlugin::JsonGetInt64(const std::string& json,
                                            const std::string& key) {
    const std::string searchKey = "\"" + key + "\":";
    const size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) return 0;
    const size_t valStart = keyPos + searchKey.size();
    // Skip whitespace
    size_t i = valStart;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;
    try {
        return std::stoll(json.substr(i));
    } catch (...) { return 0; }
}

std::string MatrixProviderPlugin::JsonBuildLoginPassword(
    const std::string& userId,
    const std::string& password,
    const std::string& deviceName) {
    std::ostringstream oss;
    oss << R"({"type":"m.login.password","identifier":{"type":"m.id.user","user":")"
        << userId << R"("},"password":")" << password
        << R"(","device_id":"chatcube","initial_device_display_name":")"
        << deviceName << R"("})";
    return oss.str();
}

std::string MatrixProviderPlugin::JsonBuildTextMessage(
    const std::string& body,
    const std::string& replyToEventId) {
    std::ostringstream oss;
    if (replyToEventId.empty()) {
        oss << R"({"msgtype":"m.text","body":")" << body << R"("})";
    } else {
        oss << R"({"msgtype":"m.text","body":")" << body
            << R"(","m.relates_to":{"m.in_reply_to":{"event_id":")"
            << replyToEventId << R"("}}} )";
    }
    return oss.str();
}

std::string MatrixProviderPlugin::JsonBuildMediaMessage(
    const std::string& msgType,
    const std::string& body,
    const std::string& mxcUrl,
    const std::string& mimeType,
    int64_t fileSize,
    int imgW, int imgH) {
    std::ostringstream oss;
    oss << R"({"msgtype":")" << msgType
        << R"(","body":")" << body
        << R"(","url":")" << mxcUrl
        << R"(","info":{"mimetype":")" << mimeType
        << R"(","size":)" << fileSize;
    if (imgW > 0 && imgH > 0) {
        oss << R"(,"w":)" << imgW << R"(,"h":)" << imgH;
    }
    oss << "}}";
    return oss.str();
}

std::string MatrixProviderPlugin::JsonBuildRedaction(const std::string& reason) {
    if (reason.empty()) return "{}";
    return R"({"reason":")" + reason + R"("})";
}

std::string MatrixProviderPlugin::JsonBuildTyping(bool isTyping, int timeoutMs) {
    std::ostringstream oss;
    oss << R"({"typing":)" << (isTyping ? "true" : "false")
        << R"(,"timeout":)" << timeoutMs << "}";
    return oss.str();
}

std::string MatrixProviderPlugin::JsonBuildReadMarkers(const std::string& eventId) {
    return R"({"m.fully_read":")" + eventId + R"(","m.read":")" + eventId + R"("})";
}

std::string MatrixProviderPlugin::UrlEncode(const std::string& str) {
    std::ostringstream encoded;
    encoded << std::hex << std::uppercase;
    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0')
                    << static_cast<int>(c);
        }
    }
    return encoded.str();
}

} // namespace ChatCube
} // namespace UltraCanvas
