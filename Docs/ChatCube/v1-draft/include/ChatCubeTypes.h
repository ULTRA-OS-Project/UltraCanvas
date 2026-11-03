// ChatCube/include/ChatCubeTypes.h
// Core data types for the ChatCube messenger framework
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <functional>

namespace UltraCanvas {
namespace ChatCube {

// ============================================================================
// VERSION
// ============================================================================

constexpr const char* CHATCUBE_VERSION     = "2.0.0";
constexpr const char* CHATCUBE_APP_ID      = "com.cloverleaf.chatcube";

// ============================================================================
// MESSAGE TYPES
// Determines how ChatBubbleDelegate renders each row in the ListView
// ============================================================================

enum class ChatMessageType {
    Text,           // Plain text message
    Image,          // Inline image (rendered via FileLoader_LoadImage)
    File,           // File attachment card (icon + filename + size)
    Sticker,        // Sticker image (full-row image, no bubble background)
    Audio,          // Voice note (waveform placeholder + duration)
    Video,          // Video attachment (thumbnail + play icon)
    DateSeparator,  // Non-message row: centred date label + horizontal line
    SystemEvent,    // Non-message row: centred italic text (e.g. "X joined chat")
    ForwardedText,  // Forwarded text with original sender attribution header
    ReplyText       // Text with quoted reply preview above message body
};

// ============================================================================
// DELIVERY STATUS
// Controls the tick marks rendered in own-message bubbles
// ============================================================================

enum class ChatDeliveryStatus {
    Sending,        // Pending (clock icon or single faint tick)
    Sent,           // Reached server (single grey tick  ✓)
    Delivered,      // Reached recipient device (double grey tick  ✓✓)
    Read,           // Recipient opened (double green tick  ✓✓)
    Failed          // Send error (red warning icon)
};

// ============================================================================
// PRESENCE / ONLINE STATUS
// Used in the contact list Template rows (presence dot colour)
// ============================================================================

enum class ChatPresenceStatus {
    Online,         // Green dot
    Away,           // Yellow/amber dot
    DoNotDisturb,   // Red dot
    Offline         // Grey dot (or no dot)
};

// ============================================================================
// PROVIDER CAPABILITY FLAGS
// Bitmask - plugins declare which features they support
// ============================================================================

enum class ChatCapabilityFlags : uint32_t {
    None            = 0,
    TextMessages    = 1 << 0,   // Basic 1:1 text
    GroupChats      = 1 << 1,   // Multi-user conversations
    FileTransfer    = 1 << 2,   // Arbitrary file attachments
    ImageMessages   = 1 << 3,   // Inline image sending
    VoiceCalls      = 1 << 4,   // 1:1 audio calls (uses IODeviceManager)
    VideoCalls      = 1 << 5,   // 1:1 video calls (uses IODeviceManager)
    GroupCalls      = 1 << 6,   // Multi-party calls
    Stickers        = 1 << 7,   // Sticker packs
    Reactions       = 1 << 8,   // Emoji reactions on messages
    EditMessage     = 1 << 9,   // Edit sent messages
    DeleteMessage   = 1 << 10,  // Delete sent messages
    ReadReceipts    = 1 << 11,  // Read/delivery status
    TypingIndicator = 1 << 12,  // "X is typing..." indicator
    VoiceNotes      = 1 << 13,  // Audio voice note messages
    VideoNotes      = 1 << 14,  // Short video note messages
    E2EEncryption   = 1 << 15,  // End-to-end encryption available
    OfflineMessages = 1 << 16,  // Stores messages while recipient offline
    Channels        = 1 << 17,  // Broadcast channels (read-only for most users)
    ForwardMessage  = 1 << 18,  // Forward messages between conversations
    ReplyToMessage  = 1 << 19,  // Threaded reply to specific message
    AnonymousLogin  = 1 << 20,  // No phone number required to register
    MultiDevice     = 1 << 21,  // Multiple simultaneous sessions
    StickerPicker   = 1 << 22,  // Sticker picker dialog supported
    Screenshot      = 1 << 23   // Screenshot capture and send (RISC OS feature)
};

// Bitwise helpers for ChatCapabilityFlags
inline ChatCapabilityFlags operator|(ChatCapabilityFlags a, ChatCapabilityFlags b) {
    return static_cast<ChatCapabilityFlags>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline ChatCapabilityFlags operator&(ChatCapabilityFlags a, ChatCapabilityFlags b) {
    return static_cast<ChatCapabilityFlags>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool HasCapability(ChatCapabilityFlags flags, ChatCapabilityFlags cap) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(cap)) != 0;
}

// ============================================================================
// LOGIN TYPE
// Dictates which authentication dialog/flow to present to the user
// ============================================================================

enum class ChatLoginType {
    PhoneOTP,           // Phone number + SMS/Telegram OTP  (Telegram, WhatsApp)
    QRCode,             // Scan QR code from another device (Telegram, WhatsApp)
    UsernamePassword,   // JID+password or Matrix user+password (XMPP, Matrix)
    ApiKey,             // api_id + api_hash (Telegram TDLib)
    SSO_OIDC,           // Single sign-on via OIDC provider (Matrix enterprise)
    KeyPair             // Asymmetric key pair, no central server (ChatCube P2P)
};

// ============================================================================
// FILE ATTACHMENT INFO
// Embedded in ChatMessage for file/image/audio/video messages
// ============================================================================

struct ChatAttachment {
    std::string LocalPath;      // Path on disk after download (empty if not yet downloaded)
    std::string RemoteUrl;      // Remote URL or platform-specific file ID
    std::string FileName;       // Display filename
    std::string MimeType;       // e.g. "image/jpeg", "application/zip"
    int64_t     FileSizeBytes = 0;  // File size in bytes
    int         ImageWidth    = 0;  // For images/videos: pixel dimensions
    int         ImageHeight   = 0;
    float       DurationSecs  = 0.0f;  // For audio/video: duration in seconds
    bool        IsDownloaded  = false; // True when LocalPath is valid and complete

    // Returns a human-readable size string e.g. "7.23 MB"
    std::string GetDisplaySize() const;
};

// ============================================================================
// REPLY CONTEXT
// Present in ReplyText messages - stores a snippet of the quoted message
// ============================================================================

struct ChatReplyContext {
    std::string OriginalMessageId;
    std::string OriginalSenderId;
    std::string OriginalSenderName;
    std::string OriginalTextSnippet;    // First ~80 chars of original message
    ChatMessageType OriginalType = ChatMessageType::Text;
};

// ============================================================================
// CHAT MESSAGE
// One row in the ChatMessageModel (IListModel)
// ============================================================================

struct ChatMessage {
    // Identity
    std::string MessageId;          // Provider-assigned unique ID
    std::string ConversationId;     // Parent conversation ID
    std::string SenderId;           // Contact/user ID of sender
    std::string SenderName;         // Display name (resolved from contacts)
    std::string SenderAvatarPath;   // Local cached avatar image path

    // Content
    ChatMessageType     Type        = ChatMessageType::Text;
    std::string         Text;       // Primary message text (may be empty for media)
    ChatAttachment      Attachment; // Populated for Image/File/Audio/Video types

    // Reply / Forward
    ChatReplyContext    ReplyContext;    // Populated for ReplyText messages
    std::string         ForwardedFrom;  // Original sender name for ForwardedText

    // Metadata
    std::chrono::system_clock::time_point Timestamp;
    std::string         TimestampDisplay;  // Pre-formatted "HH:MM" string for rendering
    std::string         DateLabel;         // "DD.MM.YYYY" used by DateSeparator rows

    // Status (own messages only)
    ChatDeliveryStatus  DeliveryStatus = ChatDeliveryStatus::Sent;
    bool                IsOwn         = false;  // True if sent by local user

    // Reactions (key = emoji string, value = count)
    std::vector<std::pair<std::string, int>> Reactions;

    // System event text (used when Type == SystemEvent)
    std::string         SystemEventText;
};

// ============================================================================
// CONTACT
// One entry in the contact/conversation list Template
// ============================================================================

struct ChatContact {
    std::string         ContactId;
    std::string         DisplayName;
    std::string         AvatarPath;         // Local cached avatar path
    ChatPresenceStatus  Presence    = ChatPresenceStatus::Offline;
    bool                IsGroup     = false;
    int                 MemberCount = 0;    // Populated for group conversations

    // Provider that owns this contact
    std::string         ProviderId;
    std::string         ProviderIconPath;   // Small provider icon path (e.g. Telegram logo)

    // Last message preview (shown in contact list row)
    std::string         LastMessageSnippet;
    std::string         LastMessageTime;    // Formatted display time
    int                 UnreadCount = 0;
};

// ============================================================================
// CONVERSATION
// A thread of messages between the local user and one or more contacts
// ============================================================================

struct ChatConversation {
    std::string             ConversationId;
    std::string             ProviderId;
    std::string             Title;          // Group name or contact display name
    std::string             AvatarPath;
    bool                    IsGroup = false;
    std::vector<std::string> ParticipantIds;
    ChatContact             Summary;        // Contact struct for list display
};

// ============================================================================
// CREDENTIALS
// Passed to IChatCubeProviderPlugin::Authenticate()
// The active fields depend on the plugin's LoginType
// ============================================================================

struct ChatCredentials {
    ChatLoginType   LoginType = ChatLoginType::UsernamePassword;

    // UsernamePassword / XMPP JID
    std::string     Username;
    std::string     Password;
    std::string     Server;     // Optional: explicit server hostname

    // PhoneOTP
    std::string     PhoneNumber;
    std::string     OTPCode;

    // ApiKey (Telegram TDLib)
    std::string     ApiId;
    std::string     ApiHash;

    // KeyPair (ChatCube P2P)
    std::string     PublicKey;
    std::string     PrivateKey; // Never transmitted - kept local only

    // SSO
    std::string     AccessToken;
    std::string     RefreshToken;
};

// ============================================================================
// PLUGIN EVENT
// Pushed from provider plugins to the ChatCubeProviderManager via callback
// ============================================================================

enum class ChatPluginEventType {
    MessageReceived,        // New incoming message
    MessageDelivered,       // Delivery status update for sent message
    TypingStarted,          // Remote user started typing
    TypingStopped,          // Remote user stopped typing
    ContactStatusChanged,   // Presence/online status changed
    ConversationUpdated,    // Unread count, title or avatar changed
    ConnectionStateChanged, // Plugin connected / disconnected / error
    AuthRequired            // Plugin needs re-authentication
};

enum class ChatConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Error
};

struct ChatPluginEvent {
    ChatPluginEventType     EventType;
    std::string             ProviderId;
    std::string             ConversationId; // May be empty for global events
    std::string             ContactId;      // May be empty
    ChatMessage             Message;        // Populated for MessageReceived
    ChatDeliveryStatus      DeliveryStatus = ChatDeliveryStatus::Sent; // For MessageDelivered
    ChatPresenceStatus      PresenceStatus = ChatPresenceStatus::Offline; // For ContactStatusChanged
    ChatConnectionState     ConnectionState = ChatConnectionState::Disconnected;
    std::string             ErrorText;
};

// ============================================================================
// CALLBACK TYPE ALIASES
// ============================================================================

using ChatPluginEventCallback   = std::function<void(const ChatPluginEvent&)>;
using ChatContactsCallback      = std::function<void(const std::vector<ChatContact>&)>;
using ChatMessagesCallback      = std::function<void(const std::vector<ChatMessage>&)>;
using ChatAuthResultCallback    = std::function<void(bool success, const std::string& errorText)>;

} // namespace ChatCube
} // namespace UltraCanvas
