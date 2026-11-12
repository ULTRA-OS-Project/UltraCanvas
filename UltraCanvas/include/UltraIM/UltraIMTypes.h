// include/UltraIM/UltraIMTypes.h
// UltraIM - data types shared by the instant-messaging API and its providers
// Version: 0.1.9 - groups managed after they are made: UltraIMCapabilityFlags ManageGroups and
//                  LeaveConversation; UltraIMConversation::ownerId
// Version: 0.1.8 - deleting a message for oneself or for everyone: UltraIMDeleteScope, UltraIMDeleteChoices
// Version: 0.1.7 - searching messages: UltraIMSearchKind, UltraIMMessageSearch, UltraIMSearchPage,
//                  UltraIMSearchCallback, the SearchMessages capability
// Version: 0.1.6 - account recovery by a link e-mailed to the confirmed address (UltraIMRecoveryAnswer
//                  without codes); the e-mail address confirmed by a link, the phone by a code
// Version: 0.1.5 - UltraIMPreviewMaker: the application's way of making a preview picture of a file to send
// Version: 0.1.4 - UltraIMAttachment::thumbnailPath (a video's or a file's preview picture); UltraIMAttachmentCallback
// Version: 0.1.3 - UltraIMAuthStep::OtherDevice: a sign-in confirmed on another device (a QR code scanned there)
// Version: 0.1.2 - UltraIMProviderInfo::authResendHint: a sign-in code can be asked for another way
// Version: 0.1.3 - the profile's verified flags and recovery state; UltraIMContactChannel, UltraIMRecoveryChallenge,
//                  UltraIMRecoveryAnswer: account recovery by codes to the e-mail and the phone
// Version: 0.1.2 - UltraIMAccountProfile (name, e-mail, phone), UltraIMSignInProtection, credentials.email
// Version: 0.1.1 - UltraIMSharedCredentialsCallback: credentials one provider's service keeps for another
// Version: 0.1.0
// Last Modified: 2026-11-12
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

// A picture, a video, a file, a voice note or a sticker with a message. The
// provider downloads a picture (and a video's or a file's preview picture)
// by itself and says so with a MessageUpdate carrying localPath /
// thumbnailPath; a video, a file or a voice note is fetched when asked for
// (UltraIM_DownloadAttachment), since it may be large.
struct UltraIMAttachment {
    std::string localPath;          // UTF-8 path once downloaded (empty until then)
    std::string thumbnailPath;      // UTF-8 path of a small preview picture, once downloaded ("" when none)
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
// DELETING MESSAGES
// ============================================================================

// Whom a message is deleted for (UltraIM_DeleteMessage).
enum class UltraIMDeleteScope {
    ForEveryone,    // gone from every member's device (and the service, where it keeps one)
    ForMe           // gone from this account's own history; the others keep it
};

// What can be done to one message now (UltraIM_GetDeleteChoices). Every
// messenger has rules of its own, so the provider says: ChatCube Direct
// lets the sender delete a text for everyone until the other person has read
// it and a picture, video or file at any time, in a group always, and
// anyone delete anything for themselves; Matrix knows only "for everyone".
// A menu offers what is true here and nothing else.
struct UltraIMDeleteChoices {
    bool forMe = false;
    bool forEveryone = false;
};

// ============================================================================
// SEARCHING MESSAGES
// ============================================================================

// What a search is for (UltraIM_SearchMessages): everything, or one kind of
// message - the "Media / Files / Links" tabs of a conversation.
enum class UltraIMSearchKind {
    AnyKind,        // every message a person wrote or sent (not the SystemEvent lines)
    Images,         // pictures
    Videos,
    Media,          // pictures and videos
    Files,          // documents: file attachments
    Links           // messages whose text holds a link (UltraIM_ExtractLinks)
};

// One search. The words are found without regard to case, anywhere in a
// message's text or its attachment's file name; several words must all be
// there. No words at all asks for everything of the kind.
struct UltraIMMessageSearch {
    // "" = every conversation of the provider (ChatCube Direct; a provider
    // that can search one conversation at a time answers CapabilityMissing).
    std::string       conversationId;
    std::string       text;
    UltraIMSearchKind kind = UltraIMSearchKind::AnyKind;
    // "" = the newest matches; UltraIMSearchPage::nextCursor of the page
    // before = the ones older than it. A cursor belongs to the search that
    // produced it: the same provider, conversation, text and kind.
    std::string       cursor;
    int               limit = 50;   // at most this many per page (<= 0: 50)
};

// One page of what a search found, newest first.
struct UltraIMSearchPage {
    std::vector<UltraIMMessage> messages;
    // Where the next, older page starts; "" when the search has reached the
    // beginning of the history. A provider that reads history from its
    // service as it searches (Matrix) stops after a stretch of it, so a page
    // can hold fewer messages than asked for - even none - and still have a
    // cursor: ask again with it to search further back.
    std::string nextCursor;
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
    // A group's creator, who alone may remove other members ("" when the
    // network does not say, or any member may).
    std::string     ownerId;
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
    CreateGroup     = 1u << 25,  // UltraIM_CreateGroup
    AccountProfile  = 1u << 26,  // UltraIM_FetchAccountProfile / UltraIM_UpdateAccountProfile
    SignInProtection = 1u << 27, // UltraIM_SetSignInProtection: a password, an authenticator code
    AccountRecovery = 1u << 28,  // UltraIM_StartRecovery / FinishRecovery: back by a link e-mailed to the owner
    SearchMessages  = 1u << 29,  // UltraIM_SearchMessages: words, pictures, files and links in the history
    ManageGroups    = 1u << 30,  // UltraIM_RenameGroup, UltraIM_AddGroupMembers, UltraIM_RemoveGroupMember
    LeaveConversation = 1u << 31 // UltraIM_LeaveConversation: leave a group, delete a 1:1 conversation
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
// account has one; a new number registers with a name; or a QR code scanned
// from a signed-in app instead of the number and the code).
enum class UltraIMAuthStep {
    Idle,           // nothing asked (signed in, or not started); not `None`, which X11 #defines
    PhoneNumber,    // credentials.phoneNumber
    Code,           // credentials.otpCode: the code sent by SMS or to another device
    Password,       // credentials.password: the account's two-step password
    Registration,   // credentials.displayName: a new account's name
    // Nothing to type: the hint is a link (Telegram: tg://login?token=...) to
    // show as a QR code for a signed-in app to scan; the service refreshes
    // it (AuthStepChange, the new link), and goes on by itself - to Password
    // when the account has one, else signed in - once it is scanned. Asked
    // for with credentials.loginType QRCode (at the start, or in place of a
    // code that never came); a phone number given here starts over with it.
    OtherDevice
};

struct UltraIMCredentials {
    // QRCode: sign in by a QR code scanned from a signed-in app (Telegram),
    // with no phoneNumber - see UltraIMAuthStep::OtherDevice.
    UltraIMLoginType loginType = UltraIMLoginType::UsernamePassword;
    std::string server;             // Homeserver / relay URL ("" = the provider's default)
    std::string username;
    std::string password;
    std::string displayName;        // KeyPair: the name shown to contacts
    // KeyPair, createAccount: the account's e-mail address, kept by the
    // provider's service for its owner alone (to recover the account) -
    // with phoneNumber, which is the same for it. Never shown to contacts.
    std::string email;
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

// ---- The account's own details ---------------------------------------------
// The signed-in account as its owner sees it in an "Edit profile" window:
// the name its contacts see, and the e-mail address and phone number its
// service keeps for the owner alone, to recover the account with (ChatCube
// Direct: the relay; never part of what contacts or anyone else are told).
struct UltraIMAccountProfile {
    std::string displayName;
    std::string email;
    std::string phoneNumber;        // international form, "+49 151 ..." as typed
    // What the service says, read by FetchAccountProfile (an update ignores
    // them): whether the address is confirmed (the link e-mailed to it was
    // opened) and the number (the code sent to it came back,
    // UltraIM_VerifyContact), whether the service offers recovery at all
    // (and can text the phone a code), and whether this account has it on
    // (UltraIM_SetRecoveryEnabled).
    bool emailVerified = false;
    bool phoneVerified = false;
    bool recoveryAvailable = false;
    bool phoneCodesAvailable = false;
    bool recoveryEnabled = false;
};

// The two ways the service reaches the owner: a link by e-mail, a code by SMS.
enum class UltraIMContactChannel { Email, Phone };

// ---- Account recovery ------------------------------------------------------
// A new computer gets an account back without the old one: the service
// e-mails a link to the account's confirmed address, and once the owner has
// opened it and said yes, the computer that asked gets the account back
// (ChatCube Direct: the relay hands back the copy of the key it keeps while
// recovery is on). The asking computer finds out by asking again
// (FinishRecovery) until then; only it can - the provider keeps what proves
// it asked.
struct UltraIMRecoveryChallenge {
    std::string recoveryId;         // what FinishRecovery names
    int         expiresInSeconds = 0;
};
struct UltraIMRecoveryAnswer {
    std::string server;             // the same service StartRecovery asked
    std::string recoveryId;
};

// What signing in asks of the user on this device (ChatCube Direct, whose
// identity is a key kept on the device). The password encrypts the key
// vault (Argon2id, XChaCha20-Poly1305 - UltraCrypt through UltraVault), so
// without it the key cannot be read; the authenticator code is a TOTP code
// (RFC 6238) from an authenticator app - UltraAuthenticator, or any other -
// checked by the provider, once per code. Stored by number: append only.
enum class UltraIMSignInProtection {
    DeviceKey = 0,                      // nothing to type: the key on this device signs in
    Password = 1,                       // the password, at every start
    AuthenticatorCode = 2,              // a code from the authenticator, at every start
    PasswordAndAuthenticatorCode = 3    // both
};
inline bool UltraIM_ProtectionHasPassword(UltraIMSignInProtection p) {
    return p == UltraIMSignInProtection::Password || p == UltraIMSignInProtection::PasswordAndAuthenticatorCode;
}
inline bool UltraIM_ProtectionHasCode(UltraIMSignInProtection p) {
    return p == UltraIMSignInProtection::AuthenticatorCode || p == UltraIMSignInProtection::PasswordAndAuthenticatorCode;
}

// A change of the sign-in protection (UltraIM_SetSignInProtection): what it
// becomes, proven with what it is now.
struct UltraIMProtectionChange {
    UltraIMSignInProtection protection = UltraIMSignInProtection::DeviceKey;
    std::string currentPassword;    // when there is a password now
    std::string currentCode;        // when a code is asked for now: the authenticator's code
    // When the new protection has a password: the new one ("" keeps the
    // current one, when there is one).
    std::string newPassword;
    // When the new protection asks for a code and an authenticator was set
    // up for it (UltraIM_StartAuthenticatorSetup): the code the
    // authenticator shows for it now, which proves it was added. Without a
    // set-up the current authenticator stays.
    std::string newCode;
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
    // Code step: the other way the code can be sent, in words for a button
    // ("Send it by SMS instead"); "" when the service offers none. Asked for
    // with UltraIM_ResendAuthCode.
    std::string             authResendHint;
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
using UltraIMSearchCallback       = std::function<void(const UltraIMResult& result,
                                                       const UltraIMSearchPage& page)>;
// For providers: one page of a conversation's history, as FetchMessages
// gives it (UltraIM_SearchByFetching reads history through this).
using UltraIMFetchPage            = std::function<void(const std::string& conversationId,
                                                       const std::string& beforeMessageId, int limit,
                                                       UltraIMMessagesCallback onResult)>;
using UltraIMStickerPacksCallback = std::function<void(const UltraIMResult& result,
                                                       const std::vector<UltraIMStickerPack>& packs)>;
using UltraIMKeyExportCallback    = std::function<void(const UltraIMResult& result, const std::string& data)>;
using UltraIMKeyImportCallback    = std::function<void(const UltraIMResult& result, int imported)>;
// A message's attachment once UltraIM_DownloadAttachment has it on disk
// (localPath set, isDownloaded true).
using UltraIMAttachmentCallback   = std::function<void(const UltraIMResult& result,
                                                       const UltraIMAttachment& attachment)>;
// Makes a preview picture of a picture or video file about to be sent: a
// JPEG, its longer side at most maxSide pixels, written to outJpegPath, its
// size in outWidth / outHeight. The application installs one
// (UltraIM_SetPreviewMaker) with its image and video libraries; a provider
// asks through UltraIM_MakePreview and sends the preview along with the
// file, so the other side sees something before the file has arrived.
// Returns false when it cannot read the file.
using UltraIMPreviewMaker = std::function<bool(const std::string& path, const std::string& mimeType, int maxSide,
                                               const std::string& outJpegPath, int& outWidth, int& outHeight)>;
using UltraIMRecoveryChallengeCallback = std::function<void(const UltraIMResult& result,
                                                            const UltraIMRecoveryChallenge& challenge)>;
using UltraIMAccountProfileCallback = std::function<void(const UltraIMResult& result,
                                                         const UltraIMAccountProfile& profile)>;
// Credentials a provider's service keeps for another provider (the ChatCube
// relay: the Telegram application's API ID and hash), to hand that
// provider's SignIn.
using UltraIMSharedCredentialsCallback = std::function<void(const UltraIMResult& result,
                                                            const UltraIMCredentials& credentials)>;
