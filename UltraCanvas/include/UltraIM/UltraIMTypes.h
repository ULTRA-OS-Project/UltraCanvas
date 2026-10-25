// include/UltraIM/UltraIMTypes.h
// UltraIM - data types shared by the instant-messaging API and its providers
// Version: 0.1.1 - UltraIMSharedCredentialsCallback: credentials one provider's service keeps for another
// Version: 0.1.0
// Last Modified: 2026-10-25
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// ============================================================================
// MESSAGES
// ============================================================================

enum class UltraIMMessageType {
    Text,           // Plain text (also a reply or a forward: see the message's fields)
    Image,          // Inline image, optional caption in text
    File,           // File attachment
    Sticker,        // Sticker image
    Audio,          // Voice note or audio file
    Video,          // Video attachment
    SystemEvent     // "X joined", "Y changed the title" - text holds the sentence
};

enum class UltraIMDeliveryStatus {
    Sending,        // Accepted locally, not yet confirmed by the network
    Sent,           // Reached the provider (or the peer's channel)
    Delivered,      // Reached the recipient's device
    Read,           // The recipient opened it
    Failed          // Could not be sent
};

struct UltraIMAttachment {
    std::string localPath;          // UTF-8 path once downloaded (empty until then)
    std::string remoteUrl;          // Provider URL or file id
    std::string fileName;           // Display name
    std::string mimeType;
    int64_t     fileSizeBytes = 0;
    int         width = 0;          // Images and videos, pixels
    int         height = 0;
    double      durationSeconds = 0;// Audio and video
    bool        isDownloaded = false;
};

struct UltraIMReplyContext {
    std::string messageId;          // The message replied to (empty: not a reply)
    std::string senderId;
    std::string senderName;
    std::string textSnippet;        // First ~80 characters of the original
};

struct UltraIMMessage {
    std::string messageId;          // Provider-assigned id (the client id until confirmed)
    std::string clientMessageId;    // Id the sender generated; matches an optimistic row
    std::string providerId;
    std::string conversationId;
    std::string senderId;
    std::string senderName;
    std::string senderAvatarPath;   // Local file, never a URL

    UltraIMMessageType   type = UltraIMMessageType::Text;
    std::string          text;
    UltraIMAttachment    attachment;
    UltraIMReplyContext  replyTo;
    std::string          forwardedFrom; // Original sender's name for a forward

    int64_t              timestampMs = 0;   // Milliseconds since the Unix epoch, UTC
    bool                 isEdited = false;
    bool                 isOwn = false;
    UltraIMDeliveryStatus deliveryStatus = UltraIMDeliveryStatus::Sent;

    std::vector<std::pair<std::string, int>> reactions; // emoji -> count
};

// ============================================================================
// CONTACTS AND CONVERSATIONS
// ============================================================================

enum class UltraIMPresence {
    Unknown,
    Online,
    Away,
    DoNotDisturb,
    Offline
};

struct UltraIMContact {
    std::string     contactId;
    std::string     providerId;
    std::string     displayName;
    std::string     avatarPath;     // Local file, never a URL
    std::string     statusText;
    UltraIMPresence presence = UltraIMPresence::Unknown;
};

struct UltraIMConversation {
    std::string     conversationId;
    std::string     providerId;
    std::string     title;
    std::string     avatarPath;     // Local file, never a URL
    bool            isGroup = false;
    std::string     directContactId;    // The other person of a 1:1 conversation
    std::vector<std::string> memberIds; // Everyone in it, including the local user
    UltraIMPresence presence = UltraIMPresence::Unknown; // Of directContactId
    std::string     lastMessageSnippet;
    int64_t         lastMessageTimeMs = 0;
    int             unreadCount = 0;
    // End-to-end encrypted: only its members' devices can read what is in it.
    bool            isEncrypted = false;
};

// ============================================================================
// PROVIDERS
// ============================================================================

enum class UltraIMCapabilityFlags : uint32_t {
    NoCapabilities            = 0,
    TextMessages    = 1u << 0,
    GroupChats      = 1u << 1,
    FileTransfer    = 1u << 2,
    ImageMessages   = 1u << 3,
    VoiceCalls      = 1u << 4,
    VideoCalls      = 1u << 5,
    GroupCalls      = 1u << 6,
    Stickers        = 1u << 7,
    Reactions       = 1u << 8,
    EditMessage     = 1u << 9,
    DeleteMessage   = 1u << 10,
    ReadReceipts    = 1u << 11,
    TypingIndicator = 1u << 12,
    VoiceNotes      = 1u << 13,
    VideoNotes      = 1u << 14,
    E2EEncryption   = 1u << 15,
    OfflineMessages = 1u << 16,
    Channels        = 1u << 17,
    ForwardMessage  = 1u << 18,
    ReplyToMessage  = 1u << 19,
    AnonymousLogin  = 1u << 20,
    MultiDevice     = 1u << 21,
    PeerToPeer      = 1u << 22,  // 1:1 content travels directly between the devices
    LocalNetwork    = 1u << 23,  // Works on a LAN with no server at all
    AddContact      = 1u << 24,  // UltraIM_AddContact
    CreateGroup     = 1u << 25   // UltraIM_CreateGroup
};

inline UltraIMCapabilityFlags operator|(UltraIMCapabilityFlags a, UltraIMCapabilityFlags b) {
    return static_cast<UltraIMCapabilityFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline UltraIMCapabilityFlags operator&(UltraIMCapabilityFlags a, UltraIMCapabilityFlags b) {
    return static_cast<UltraIMCapabilityFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool UltraIM_HasCapability(UltraIMCapabilityFlags flags, UltraIMCapabilityFlags capability) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(capability)) != 0;
}

enum class UltraIMLoginType {
    UsernamePassword,   // Matrix, XMPP
    PhoneOTP,           // Telegram, WhatsApp
    QRCode,             // Scan from another device
    ApiKey,             // Telegram TDLib api_id + api_hash
    SsoOidc,            // Browser sign-in
    KeyPair             // ChatCube Direct: a key pair made on the device, no password
};

// Where a sign-in that takes more than one round stands (Telegram: the phone
// number, then the code Telegram sends, then the cloud password when the
// account has one; a new number registers with a name).
enum class UltraIMAuthStep {
    Idle,           // nothing asked (signed in, or not started); not `None`, which X11 #defines
    PhoneNumber,    // credentials.phoneNumber
    Code,           // credentials.otpCode: the code sent by SMS or to another device
    Password,       // credentials.password: the account's two-step password
    Registration    // credentials.displayName: a new account's name
};

struct UltraIMCredentials {
    UltraIMLoginType loginType = UltraIMLoginType::UsernamePassword;
    std::string server;             // Homeserver / relay URL ("" = the provider's default)
    std::string username;
    std::string password;
    std::string displayName;        // KeyPair: the name shown to contacts
    bool        createAccount = false; // KeyPair: register a new identity
    // Also find and reach contacts on the local network, with no server
    // between (ChatCube Direct; always on when there is no relay). Remembered.
    bool        localNetwork = false;
    std::string phoneNumber;
    std::string otpCode;
    std::string apiId;
    std::string apiHash;
    std::string accessToken;        // Restored session
    std::string refreshToken;
};

enum class UltraIMConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Error
};

struct UltraIMProviderInfo {
    std::string             providerId;
    std::string             displayName;
    std::string             version;
    std::string             iconPath;
    UltraIMCapabilityFlags  capabilities = UltraIMCapabilityFlags::NoCapabilities;
    UltraIMLoginType        loginType = UltraIMLoginType::UsernamePassword;
    UltraIMConnectionState  connectionState = UltraIMConnectionState::Disconnected;
    std::string             localUserId;   // Signed-in identity ("" when signed out)
    bool                    hasStoredSession = false; // Can sign in without asking the user
    UltraIMAuthStep         authStep = UltraIMAuthStep::Idle; // What SignIn needs next
    std::string             authHint;      // e.g. where the code went, the password's hint
};

struct UltraIMSendOptions {
    std::string clientMessageId;    // Echoed in MessageDelivered and the server echo
    std::string replyToMessageId;   // "" = not a reply
    bool        silent = false;     // No notification at the recipient, if supported
};

struct UltraIMStickerPack {
    std::string packId;
    std::string title;
    std::string thumbnailPath;
    std::vector<std::string> stickerIds;
    std::vector<std::string> stickerPaths;  // Parallel to stickerIds
};

// ============================================================================
// CALLS
// ============================================================================

enum class UltraIMCallState {
    Ringing,        // Incoming, not answered yet / outgoing, not answered yet
    Connecting,     // Answered, media path being set up
    Active,
    Ended,
    Failed
};

// ============================================================================
// EVENTS
// ============================================================================

enum class UltraIMEventType {
    MessageReceive,         // message: a new message (also the server echo of our own)
    MessageDelivered,       // message.clientMessageId / messageId, deliveryStatus
    MessageUpdate,          // message: edited text or reactions
    MessageDelete,          // message.messageId removed
    TypingChange,           // contactId, isTyping
    PresenceChange,         // contactId, presence
    ConversationUpdate,     // conversation: new or changed (title, unread, snippet)
    ConversationRemove,     // conversationId left or deleted
    ConnectionStateChange,  // connectionState, errorText
    AuthRequire,            // the session expired; sign in again
    AuthStepChange,         // authStep, errorText (the hint): SignIn wants that next
    CallReceive,            // callId, contactId, isVideo
    CallStateChange,        // callId, callState, errorText
    CallMediaChange,        // callId, isVideo: the other side turned its camera on or off
    NearbyChange            // the people on the local network changed: fetch them again
};

struct UltraIMEvent {
    UltraIMEventType        type = UltraIMEventType::MessageReceive;
    std::string             providerId;
    std::string             conversationId;
    std::string             contactId;
    UltraIMMessage          message;
    UltraIMConversation     conversation;
    UltraIMDeliveryStatus   deliveryStatus = UltraIMDeliveryStatus::Sent;
    UltraIMPresence         presence = UltraIMPresence::Unknown;
    UltraIMConnectionState  connectionState = UltraIMConnectionState::Disconnected;
    bool                    isTyping = false;
    std::string             callId;
    bool                    isVideo = false;
    UltraIMCallState        callState = UltraIMCallState::Ringing;
    UltraIMAuthStep         authStep = UltraIMAuthStep::Idle;
    std::string             errorText;
};

// ============================================================================
// RESULTS
// ============================================================================

enum class UltraIMResultCode {
    Ok,
    NotInitialized,
    ProviderNotFound,
    NotAuthenticated,
    NotConnected,
    AuthFailed,
    CapabilityMissing,
    InvalidArgument,
    NotFound,
    PermissionDenied,
    RateLimited,
    Timeout,
    NetworkError,
    PluginError,
    Unsupported,
    InvalidState,           // Not possible now (another call is in progress, ...)
    NeedsInput,             // SignIn: one more step - see UltraIMProviderInfo::authStep
    Unknown
};

struct UltraIMResult {
    UltraIMResultCode code = UltraIMResultCode::Unknown;
    bool              success = false;
    std::string       message;

    operator bool() const { return success; }

    static UltraIMResult Ok() { return {UltraIMResultCode::Ok, true, std::string()}; }
    static UltraIMResult Error(UltraIMResultCode code, const std::string& message) {
        return {code, false, message};
    }
};

// ============================================================================
// CONFIGURATION AND CALLBACKS
// ============================================================================

struct UltraIMConfig {
    std::string appId = "com.cloverleaf.chatcube"; // Vault key prefix, user agent
    std::string pluginDirectory;    // Provider modules ("" = none loaded from disk)
    std::string dataDirectory;      // Per-provider state (contacts, queues), UTF-8
    std::string cacheDirectory;     // Avatars and downloaded attachments, UTF-8
};

using UltraIMSubscriptionId = uint64_t;

// Runs a task on the application's chosen thread (ChatCube: the UI thread).
using UltraIMDispatcher           = std::function<void(std::function<void()> task)>;
using UltraIMEventCallback        = std::function<void(const UltraIMEvent& event)>;
// A call's picture: RGBA, 8 bits per channel, rows top first, width * 4
// bytes per row, valid only during the call. Runs on a media or capture
// thread, not through the dispatcher: hand it to something thread-safe
// (UltraCanvasVideoTile::PushFrame) and return.
using UltraIMVideoFrameCallback   = std::function<void(const uint8_t* rgba, int width, int height)>;
using UltraIMResultCallback       = std::function<void(const UltraIMResult& result)>;
using UltraIMConversationsCallback = std::function<void(const UltraIMResult& result,
                                                        const std::vector<UltraIMConversation>& conversations)>;
using UltraIMConversationCallback = std::function<void(const UltraIMResult& result,
                                                       const UltraIMConversation& conversation)>;
using UltraIMContactsCallback     = std::function<void(const UltraIMResult& result,
                                                       const std::vector<UltraIMContact>& contacts)>;
using UltraIMMessagesCallback     = std::function<void(const UltraIMResult& result,
                                                       const std::vector<UltraIMMessage>& messages)>;
using UltraIMStickerPacksCallback = std::function<void(const UltraIMResult& result,
                                                       const std::vector<UltraIMStickerPack>& packs)>;
using UltraIMKeyExportCallback    = std::function<void(const UltraIMResult& result, const std::string& data)>;
using UltraIMKeyImportCallback    = std::function<void(const UltraIMResult& result, int imported)>;
// Credentials a provider's service keeps for another provider (the ChatCube
// relay: the Telegram application's API ID and hash), to hand that
// provider's SignIn.
using UltraIMSharedCredentialsCallback = std::function<void(const UltraIMResult& result,
                                                            const UltraIMCredentials& credentials)>;
