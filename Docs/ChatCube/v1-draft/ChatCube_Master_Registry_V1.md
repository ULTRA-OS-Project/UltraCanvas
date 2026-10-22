# ChatCube Master Registry
## Complete AI Programming Reference for the ChatCube Application

```
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework / Cloverleaf
// Purpose: Complete registry of all ChatCube types, functions, callbacks,
//          architecture rules, and integration patterns.
//          Used by AI assistants as the authoritative reference for
//          all future ChatCube development sessions.
```

**SUPREMACY HIERARCHY (ChatCube-specific):**
`UltraCanvas_Code_Guidelines_V8 > master_types_V4 > master_functions_V4.1 > master_callbacks_V4.1 > ChatCube_Master_Registry (this file) > Existing ChatCube Code`

---

## SECTION 1 — APPLICATION IDENTITY

| Property | Value |
|---|---|
| App name | ChatCube |
| Version | 2.0.0 |
| Namespace | `UltraCanvas::ChatCube` |
| App ID | `com.cloverleaf.chatcube` |
| Framework | UltraCanvas (min v1.0.16) |
| Standard | C++20 |
| UOS bundle | `ChatCube.app/` |
| Entry point | `main.cpp` |
| Original project | github.com/riscoscloverleaf/chatcube (RISC OS) |

**Purpose:** Multi-protocol instant messenger. Two goals:
1. Plugin framework — any provider offering login+API integrates as a `.module`
2. Native P2P — ChatCube Direct plugin with server-backup option

---

## SECTION 2 — FILE MAP

```
ChatCube/
├── AppInfo.txt                         UOS bundle metadata
├── CMakeLists.txt                      Build system
├── main.cpp                            Entry point
├── DEVELOPER_GUIDE.md                  Human developer documentation
│
├── include/
│   ├── ChatCubeTypes.h                 ALL core enums and data structs
│   ├── IChatCubeProviderPlugin.h       Pure abstract plugin interface
│   ├── ChatCubeProviderManager.h       Plugin manager singleton
│   ├── ChatMessageModel.h              ListView IListModel subclass
│   ├── ChatBubbleDelegate.h            IItemDelegate + ChatBubbleStyle
│   └── ChatCubeWindow.h               Main UltraCanvasWindow subclass
│
├── core/
│   ├── ChatCubeTypes.cpp               ChatAttachment::GetDisplaySize()
│   ├── ChatMessageModel.cpp            Model CRUD + delivery update
│   ├── ChatBubbleDelegate.cpp          All 10 message type renderers
│   ├── ChatCubeProviderManager.cpp     Routing, merging, dispatch
│   └── ChatCubeWindow.cpp             All 8 layout sections + wiring
│
└── Plugins/
    ├── Matrix/
    │   ├── MatrixProviderPlugin.h
    │   └── MatrixProviderPlugin.cpp    Matrix CS API v3 (REFERENCE IMPL)
    ├── Telegram/                       PLANNED — Step 10
    ├── XMPP/                           PLANNED — Step 9
    └── Direct/                         PLANNED — Step 11 (P2P)
```

---

## SECTION 3 — ENUM REGISTRY
*Source: `include/ChatCubeTypes.h`*

### ChatMessageType
Controls how `ChatBubbleDelegate::RenderItem()` renders each ListView row.

```cpp
enum class ChatMessageType {
    Text,           // Plain text bubble
    Image,          // Inline image (FileLoader_LoadImage)
    File,           // File card: icon + filename + size
    Sticker,        // Full-row image, no bubble background
    Audio,          // Voice note: waveform bars + duration
    Video,          // Thumbnail + play circle overlay
    DateSeparator,  // Non-bubble: centred date pill + h-line
    SystemEvent,    // Non-bubble: centred italic text (e.g. "X joined")
    ForwardedText,  // Italic "Forwarded from X" header + text
    ReplyText       // Reply preview strip (blue left border) + text
};
```

**AI rule:** `GetRowHeight()` returns fixed heights for `DateSeparator` (SepHeight+pads)
and `SystemEvent` (SysEventHeight+pads). All others are variable.

### ChatDeliveryStatus
Rendered as tick marks in own-message bubbles (right side of timestamp).

```cpp
enum class ChatDeliveryStatus {
    Sending,    // Faint grey "o" (pending, not yet on server)
    Sent,       // Single grey ✓
    Delivered,  // Double grey ✓✓
    Read,       // Double green ✓✓
    Failed      // Red ! (send error)
};
```

### ChatPresenceStatus
Controls the coloured dot on contact list rows.

```cpp
enum class ChatPresenceStatus {
    Online,         // Green dot
    Away,           // Amber dot
    DoNotDisturb,   // Red dot
    Offline         // Grey dot
};
```

### ChatCapabilityFlags (bitmask)
Plugins declare supported features. Manager checks before calling optional methods.

```cpp
enum class ChatCapabilityFlags : uint32_t {
    None            = 0,
    TextMessages    = 1 << 0,   // Always required
    GroupChats      = 1 << 1,
    FileTransfer    = 1 << 2,
    ImageMessages   = 1 << 3,
    VoiceCalls      = 1 << 4,
    VideoCalls      = 1 << 5,
    GroupCalls      = 1 << 6,
    Stickers        = 1 << 7,
    Reactions       = 1 << 8,
    EditMessage     = 1 << 9,
    DeleteMessage   = 1 << 10,
    ReadReceipts    = 1 << 11,
    TypingIndicator = 1 << 12,
    VoiceNotes      = 1 << 13,
    VideoNotes      = 1 << 14,
    E2EEncryption   = 1 << 15,
    OfflineMessages = 1 << 16,
    Channels        = 1 << 17,
    ForwardMessage  = 1 << 18,
    ReplyToMessage  = 1 << 19,
    AnonymousLogin  = 1 << 20,
    MultiDevice     = 1 << 21,
    StickerPicker   = 1 << 22,
    Screenshot      = 1 << 23
};

// Helper function (non-member)
bool HasCapability(ChatCapabilityFlags flags, ChatCapabilityFlags cap);
// Usage: if (HasCapability(plugin->GetCapabilities(), ChatCapabilityFlags::VoiceCalls))
```

### ChatLoginType
Determines which authentication UI flow to show the user.

```cpp
enum class ChatLoginType {
    PhoneOTP,           // Phone number → SMS/Telegram OTP
    QRCode,             // Scan QR code from another device
    UsernamePassword,   // Username + password (XMPP, Matrix)
    ApiKey,             // api_id + api_hash (Telegram TDLib)
    SSO_OIDC,           // OIDC provider (Matrix enterprise)
    KeyPair             // Asymmetric key pair — no central server (P2P)
};
```

### ChatPluginEventType
Events pushed from plugin worker threads to the manager's `HandlePluginEvent()`.

```cpp
enum class ChatPluginEventType {
    MessageReceived,        // New incoming message
    MessageDelivered,       // Delivery status update
    TypingStarted,
    TypingStopped,
    ContactStatusChanged,
    ConversationUpdated,    // Unread count / title / avatar changed
    ConnectionStateChanged,
    AuthRequired            // Session expired
};
```

### ChatConnectionState

```cpp
enum class ChatConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Error
};
```

---

## SECTION 4 — STRUCT REGISTRY
*Source: `include/ChatCubeTypes.h`*

### ChatAttachment
Embedded in `ChatMessage` for all media/file types.

```cpp
struct ChatAttachment {
    std::string LocalPath;       // Local cache path after download
    std::string RemoteUrl;       // mxc:// URI or provider file ID
    std::string FileName;        // Display name
    std::string MimeType;        // e.g. "image/jpeg"
    int64_t     FileSizeBytes = 0;
    int         ImageWidth    = 0;
    int         ImageHeight   = 0;
    float       DurationSecs  = 0.0f;  // Audio/video duration
    bool        IsDownloaded  = false;

    std::string GetDisplaySize() const; // Returns "7.23 MB" etc.
};
```

### ChatReplyContext
Present in `ChatMessageType::ReplyText` messages.

```cpp
struct ChatReplyContext {
    std::string OriginalMessageId;
    std::string OriginalSenderId;
    std::string OriginalSenderName;
    std::string OriginalTextSnippet;  // First ~80 chars
    ChatMessageType OriginalType = ChatMessageType::Text;
};
```

### ChatMessage
One row in the `ChatMessageModel` (IListModel).

```cpp
struct ChatMessage {
    std::string MessageId;
    std::string ConversationId;
    std::string SenderId;
    std::string SenderName;
    std::string SenderAvatarPath;   // Local cached path

    ChatMessageType     Type       = ChatMessageType::Text;
    std::string         Text;
    ChatAttachment      Attachment;
    ChatReplyContext    ReplyContext;
    std::string         ForwardedFrom;

    std::chrono::system_clock::time_point Timestamp;
    std::string         TimestampDisplay;  // "HH:MM"
    std::string         DateLabel;         // "DD.MM.YYYY" for separators

    ChatDeliveryStatus  DeliveryStatus = ChatDeliveryStatus::Sent;
    bool                IsOwn         = false;

    std::vector<std::pair<std::string, int>> Reactions; // emoji → count
    std::string         SystemEventText; // For SystemEvent type only
};
```

### ChatContact
One entry in the contact/conversation list Template rows.

```cpp
struct ChatContact {
    std::string         ContactId;
    std::string         DisplayName;
    std::string         AvatarPath;
    ChatPresenceStatus  Presence    = ChatPresenceStatus::Offline;
    bool                IsGroup     = false;
    int                 MemberCount = 0;
    std::string         ProviderId;
    std::string         ProviderIconPath;
    std::string         LastMessageSnippet;
    std::string         LastMessageTime;
    int                 UnreadCount = 0;
};
```

### ChatConversation

```cpp
struct ChatConversation {
    std::string             ConversationId;
    std::string             ProviderId;
    std::string             Title;
    std::string             AvatarPath;
    bool                    IsGroup = false;
    std::vector<std::string> ParticipantIds;
    ChatContact             Summary;  // Drives the contact list template row
};
```

### ChatCredentials
Single struct covers all login types. Active fields depend on `LoginType`.

```cpp
struct ChatCredentials {
    ChatLoginType   LoginType = ChatLoginType::UsernamePassword;
    std::string     Username;
    std::string     Password;
    std::string     Server;        // Optional explicit homeserver
    std::string     PhoneNumber;
    std::string     OTPCode;
    std::string     ApiId;
    std::string     ApiHash;
    std::string     PublicKey;
    std::string     PrivateKey;    // Never transmitted
    std::string     AccessToken;
    std::string     RefreshToken;
};
```

### ChatPluginResult
Returned by all plugin send/action operations.

```cpp
enum class ChatPluginResultCode {
    Success,
    ErrorNotAuthenticated,
    ErrorNotConnected,
    ErrorAuthFailed,
    ErrorCapabilityMissing,
    ErrorNetworkTimeout,
    ErrorRateLimit,
    ErrorPermissionDenied,
    ErrorInvalidArgument,
    ErrorNotFound,
    ErrorGeneric
};

struct ChatPluginResult {
    ChatPluginResultCode Code    = ChatPluginResultCode::Success;
    std::string          ErrorMessage;

    bool IsSuccess() const;
    static ChatPluginResult Ok();
    static ChatPluginResult Fail(ChatPluginResultCode code,
                                 const std::string& msg = "");
};
```

### ChatPluginEvent
Pushed from plugin worker threads to manager via `SubscribeToEvents` callback.

```cpp
struct ChatPluginEvent {
    ChatPluginEventType     EventType;
    std::string             ProviderId;
    std::string             ConversationId;
    std::string             ContactId;
    ChatMessage             Message;          // MessageReceived
    ChatDeliveryStatus      DeliveryStatus;   // MessageDelivered
    ChatPresenceStatus      PresenceStatus;   // ContactStatusChanged
    ChatConnectionState     ConnectionState;  // ConnectionStateChanged
    std::string             ErrorText;
};
```

### ChatManagerConfig

```cpp
struct ChatManagerConfig {
    std::string PluginDirectory  = "Modules/";
    std::string CacheDirectory   = "Resources/Data/cache/";
    int         MessageCacheLimit = 500;
    bool        AutoReconnect    = true;
    int         ReconnectDelayMs = 5000;

    static ChatManagerConfig Default();
};
```

### ChatBubbleStyle
All visual constants for `ChatBubbleDelegate`. No magic numbers in render code.

```cpp
struct ChatBubbleStyle {
    // Fonts
    std::string FontFamily;
    float FontSizeBody, FontSizeMeta, FontSizeSender, FontSizeSep, FontSizeReply;

    // Geometry
    float BubbleRadius;
    int BubblePadH, BubblePadV, BubbleMaxWidth, BubbleMinWidth;
    int RowPadTop, RowPadBottom, RowPadLeft, RowPadRight;

    // Avatar
    int AvatarSize, AvatarGap;
    Color AvatarFallbackBg, AvatarFallbackTxt;

    // Own message colours
    Color OwnBubbleBg, OwnBubbleBorder, OwnTextColor, OwnMetaColor;

    // Other message colours
    Color OtherBubbleBg, OtherBubbleBorder, OtherTextColor, OtherMetaColor;
    Color SenderNameColor;

    // Reply preview strip
    Color ReplyStripColor, ReplyBgColor, ReplyTextColor;
    int ReplyStripWidth, ReplyPadH, ReplyPadV, ReplyHeight;

    // File card
    int FileCardHeight, FileIconSize, FileIconGap;
    Color FileCardBg, FileNameColor, FileSizeColor;

    // Media
    int ImageMaxHeight, StickerSize, VideoThumbHeight;

    // Audio
    int AudioBarHeight;
    Color AudioWaveColor;

    // Delivery ticks
    Color TickSending, TickSent, TickDelivered, TickRead, TickFailed;
    int TickSize;

    // Presence dot
    Color PresenceOnline, PresenceAway, PresenceDND, PresenceOffline;
    int PresenceDotSize;

    // Date separator
    Color SepLineColor, SepTextColor, SepBgColor;
    int SepHeight;
    float SepBgRadius;

    // System event
    Color SysEventTextColor;
    int SysEventHeight;
    float SysEventFontSize;
};
```

### ChatCubeWindowStyle
Visual configuration for the main application window.

```cpp
struct ChatCubeWindowStyle {
    int WindowWidth, WindowHeight;
    int LeftPanelWidth, LeftPanelMinWidth, LeftPanelMaxWidth;
    int TopToolbarHeight, ChatHeaderHeight;
    int InputToolbarHeight, InputAreaHeight;
    Color WindowBg, LeftPanelBg, RightPanelBg;
    Color ChatHeaderBg, InputAreaBg;
    Color SplitterColor, SelectedContactBg;
    float FontSizeContact, FontSizeSnippet, FontSizeHeader;
};
```

---

## SECTION 5 — CUSTOM DATA ROLES
*Source: `include/ChatMessageModel.h`*

Extended from `ListDataRole::UserRole` (= 256). Pattern matches `UltraCanvasDropdown.h`.

```cpp
// Standard roles (from IListModel):
// DisplayRole (0)      → ChatMessage::Text
// DecorationRole (1)   → ChatMessage::SenderAvatarPath
// ToolTipRole (2)      → ChatMessage::TimestampDisplay (full)

// Custom ChatCube roles (UserRole + N):
constexpr auto ChatMsgRole_SenderName       = UserRole + 0;  // string
constexpr auto ChatMsgRole_SenderId         = UserRole + 1;  // string
constexpr auto ChatMsgRole_MessageType      = UserRole + 2;  // int (ChatMessageType)
constexpr auto ChatMsgRole_TimestampDisplay = UserRole + 3;  // string "HH:MM"
constexpr auto ChatMsgRole_DeliveryStatus   = UserRole + 4;  // int (ChatDeliveryStatus)
constexpr auto ChatMsgRole_IsOwn            = UserRole + 5;  // int 0/1
constexpr auto ChatMsgRole_AttachPath       = UserRole + 6;  // string local path
constexpr auto ChatMsgRole_AttachFileName   = UserRole + 7;  // string
constexpr auto ChatMsgRole_AttachSize       = UserRole + 8;  // string "7.23 MB"
constexpr auto ChatMsgRole_AttachWidth      = UserRole + 9;  // int pixels
constexpr auto ChatMsgRole_AttachHeight     = UserRole + 10; // int pixels
constexpr auto ChatMsgRole_DateLabel        = UserRole + 11; // string "DD.MM.YYYY"
constexpr auto ChatMsgRole_SystemEventText  = UserRole + 12; // string
constexpr auto ChatMsgRole_ReplySnippet     = UserRole + 13; // string
constexpr auto ChatMsgRole_ReplySenderName  = UserRole + 14; // string
constexpr auto ChatMsgRole_ForwardedFrom    = UserRole + 15; // string
constexpr auto ChatMsgRole_MessageId        = UserRole + 16; // string
```

**AI rule:** When adding new data to ChatMessage, assign `UserRole + 17`, `+ 18` etc.
Never reuse existing offsets. Never use a role value below 256.

---

## SECTION 6 — CHATMESSAGEMODEL FUNCTIONS
*Source: `include/ChatMessageModel.h`, `core/ChatMessageModel.cpp`*

```cpp
class ChatMessageModel : public IListModel {
public:
    // ── IListModel (required overrides) ──────────────────────────────────
    int GetRowCount() const override;
    int GetColumnCount() const override;   // Always returns 1
    ListDataValue GetData(const ListIndex& index, ListDataRole role) const override;
    bool SetData(const ListIndex& index, ListDataRole role,
                 const ListDataValue& value) override;

    // ── Message management ───────────────────────────────────────────────
    void AppendMessage(const ChatMessage& message);
        // Fires onRowInserted → ListView auto-scrolls to bottom

    void PrependHistory(const std::vector<ChatMessage>& history);
        // Inserts at front. Fires onDataChanged + onHistoryLoaded.

    void SetMessages(const std::vector<ChatMessage>& messages);
        // Replace all. Fires onDataChanged.

    void Clear();
        // Fires onDataChanged.

    bool UpdateDeliveryStatus(const std::string& messageId,
                              ChatDeliveryStatus status);
        // Fires onRowChanged for the matched row only.
        // Returns false if messageId not found.

    // ── Accessors ────────────────────────────────────────────────────────
    const ChatMessage* GetMessage(int row) const;
        // Direct access by row index. Returns nullptr if out of range.

    int GetMessageCount() const;

    int FindRowByMessageId(const std::string& messageId) const;
        // Returns -1 if not found.

    // ── Additional callback ──────────────────────────────────────────────
    std::function<void(int insertedCount)> onHistoryLoaded;
        // Fired after PrependHistory. View uses this to re-anchor scroll.
};
```

**AI rule — SetData supported roles:**
- `ChatMsgRole_DeliveryStatus` → updates `ChatMessage::DeliveryStatus`
- `ChatMsgRole_AttachPath` → updates `Attachment.LocalPath`, sets `IsDownloaded = true`
- All other fields are read-only via SetData; use dedicated methods.

---

## SECTION 7 — CHATBUBBLEDELEGATE FUNCTIONS
*Source: `include/ChatBubbleDelegate.h`, `core/ChatBubbleDelegate.cpp`*

```cpp
class ChatBubbleDelegate : public IItemDelegate {
public:
    explicit ChatBubbleDelegate(const ChatBubbleStyle& style = {});

    // ── IItemDelegate (required overrides) ───────────────────────────────
    void RenderItem(IRenderContext* ctx, const IListModel* model,
                    int row, int column,
                    const ListItemStyleOption& option) override;
        // Dispatches to per-type render method based on ChatMsgRole_MessageType

    int GetRowHeight(const IListModel* model, int row) const override;
        // Returns variable height per message type and content.
        // Allocation-free — no IRenderContext needed.

    // ── Configuration ────────────────────────────────────────────────────
    void SetStyle(const ChatBubbleStyle& newStyle);
    const ChatBubbleStyle& GetStyle() const;

    void SetViewWidth(int width);
        // MUST be called on window resize. Updates max bubble width.
        // Default: 600px. Formula: min(BubbleMaxWidth, viewWidth * 0.70)
};
```

**AI rule — Private render methods (called by RenderItem):**

| Method | ChatMessageType(s) |
|---|---|
| `RenderTextBubble` | Text |
| `RenderImageBubble` | Image |
| `RenderFileBubble` | File |
| `RenderStickerRow` | Sticker |
| `RenderAudioBubble` | Audio |
| `RenderVideoBubble` | Video (delegates to RenderImageBubble + play circle) |
| `RenderReplyTextBubble` | ReplyText |
| `RenderForwardedTextBubble` | ForwardedText |
| `RenderDateSeparator` | DateSeparator |
| `RenderSystemEvent` | SystemEvent |

**AI rule — Shared sub-components called inside render methods:**

```cpp
int  RenderAvatar(ctx, avatarPath, senderName, x, y);
     // Returns x position after avatar + gap. Clips image to circle.

Rect2Df RenderBubbleBackground(ctx, bubbleRect, isOwn);
     // Draws rounded rect. Returns inner content Rect2Df (padded).

void RenderReplyPreview(ctx, contentRect, senderName, snippet);
     // Draws translucent block + blue left strip inside bubble.

void RenderTimestampAndTicks(ctx, contentRect, timestamp, status, isOwn);
     // Bottom-right of content area. Draws ticks for own messages.

void RenderFileCard(ctx, contentRect, fileName, fileSize, attachPath);
     // File icon + bold filename + muted file size inside bubble.
```

**AI rule — Height estimators (offline, no ctx):**

```cpp
int CalcTextBubbleHeight(text, bubbleWidth);
int CalcFileBubbleHeight();                   // Fixed
int CalcImageBubbleHeight(imgW, imgH, bubbleWidth);
int CalcReplyTextBubbleHeight(text, bubbleWidth);
```

---

## SECTION 8 — ICHATCUBEPROVIDER PLUGIN INTERFACE
*Source: `include/IChatCubeProviderPlugin.h`*

All 10 sections must be implemented by every plugin:

```
Section 1: Identity          GetPluginId, GetDisplayName, GetPluginVersion,
                             GetIconPath, GetCapabilities, GetLoginType

Section 2: Connection        Authenticate(async), RequestQRCode, Disconnect,
                             GetConnectionState, IsConnected(inline)

Section 3: Data retrieval    GetContacts, GetConversations,
                             GetMessages(convId, beforeId, limit, cb)

Section 4: Send (required)   SendTextMessage(convId, text, options)

Section 5: Send (optional)   SendFile, SendImage, SendSticker, SendVoiceNote

Section 6: Message actions   DeleteMessage, EditMessage,
                             MarkConversationRead, SetTypingStatus

Section 7: Calls             StartVoiceCall, StartVideoCall,
                             AcceptCall, EndCall

Section 8: Sticker packs     GetStickerPacks, DownloadStickerPack

Section 9: Event sub         SubscribeToEvents, UnsubscribeFromEvents

Section 10: Media download   DownloadAvatar, DownloadAttachment
```

**AI rule — For unsupported optional sections, return:**
```cpp
ChatPluginResult::Fail(ChatPluginResultCode::ErrorCapabilityMissing,
                       "Provider does not support X")
```

**AI rule — `ChatSendOptions` struct (reply threading + silent send):**
```cpp
struct ChatSendOptions {
    std::string ReplyToMessageId;  // "" = no reply
    bool        Silent = false;    // Send without notification (if supported)
};
```

**AI rule — `ChatStickerPack` struct:**
```cpp
struct ChatStickerPack {
    std::string PackId;
    std::string Title;
    std::string ThumbnailPath;
    std::vector<std::string> StickerIds;
    std::vector<std::string> StickerPaths;
};
```

---

## SECTION 9 — CHATCUBEPROVIDER MANAGER FUNCTIONS
*Source: `include/ChatCubeProviderManager.h`, `core/ChatCubeProviderManager.cpp`*

```cpp
class ChatCubeProviderManager {
public:
    static ChatCubeProviderManager& GetInstance();  // Singleton

    // ── Lifecycle ────────────────────────────────────────────────────────
    bool Initialize(const ChatManagerConfig& config = {});
    void Shutdown();

    // ── Plugin registration ──────────────────────────────────────────────
    void RegisterPlugin(std::shared_ptr<IChatCubeProviderPlugin> plugin);
    void UnregisterPlugin(const std::string& pluginId);
    std::vector<IChatCubeProviderPlugin*> GetAllPlugins() const;
    IChatCubeProviderPlugin* GetPlugin(const std::string& pluginId) const;

    // ── Authentication ───────────────────────────────────────────────────
    void AuthenticateProvider(const std::string& pluginId,
                              const ChatCredentials& credentials,
                              ChatAuthResultCallback onResult);
    void DisconnectProvider(const std::string& pluginId);
    void DisconnectAll();
    std::vector<std::string> GetConnectedProviderIds() const;

    // ── Contact & conversation lists ─────────────────────────────────────
    void RefreshContacts();        // Fans out to all connected plugins
    void RefreshConversations();   // Fans out to all connected plugins
    const std::vector<ChatContact>& GetContacts() const;
    const std::vector<ChatConversation>& GetConversations() const;

    // ── Message history ──────────────────────────────────────────────────
    void LoadConversation(const std::string& providerId,
                          const std::string& conversationId,
                          ChatMessageModel* targetModel);
        // Clears model, loads recent messages.

    void LoadOlderMessages(const std::string& providerId,
                           const std::string& conversationId,
                           const std::string& beforeMessageId,
                           ChatMessageModel* targetModel);
        // Prepends history page. Triggers model->onHistoryLoaded.

    // ── Active conversation routing ──────────────────────────────────────
    void SetActiveConversation(const std::string& providerId,
                               const std::string& conversationId,
                               ChatMessageModel* model);
        // Incoming messages matching this conv are appended to model directly.

    void ClearActiveConversation();
    const ChatActiveConversation& GetActiveConversation() const;

    // ── Send operations (all route to correct plugin) ────────────────────
    ChatPluginResult SendTextMessage(providerId, conversationId, text, options);
    ChatPluginResult SendFile(providerId, conversationId, localPath, options);
    ChatPluginResult SendImage(providerId, conversationId, localPath,
                               caption, options);
    ChatPluginResult SendSticker(providerId, conversationId, packId, stickerId);
    ChatPluginResult SendVoiceNote(providerId, conversationId, localPath);
    ChatPluginResult DeleteMessage(providerId, messageId);
    ChatPluginResult EditMessage(providerId, messageId, newText);
    ChatPluginResult SetTypingStatus(providerId, conversationId, isTyping);
    ChatPluginResult MarkConversationRead(providerId, conversationId);
    ChatPluginResult StartVoiceCall(providerId, contactId);
    ChatPluginResult StartVideoCall(providerId, contactId);
    ChatPluginResult EndCall(providerId, callId);

    // ── Sticker packs ────────────────────────────────────────────────────
    void GetStickerPacks(providerId, callback);
    void DownloadStickerPack(providerId, packId, onProgress, onResult);

    // ── UI event callbacks (set by ChatCubeWindow) ───────────────────────
    std::function<void(providerId, ChatMessage)>        onMessageReceive;
    std::function<void(providerId, msgId, status)>      onMessageStatusChange;
    std::function<void(providerId, convId, contactId, isTyping)>
                                                        onTypingStatusChange;
    std::function<void(providerId, contactId, status)>  onContactStatusChange;
    std::function<void()>                               onContactListUpdate;
    std::function<void(ChatConversation)>               onConversationUpdate;
    std::function<void(providerId, ChatConnectionState)> onConnectionStateChange;
    std::function<void(providerId)>                     onAuthRequire;
    std::function<void(providerId, callId, contactId, isVideo)>
                                                        onCallReceive;
};
```

---

## SECTION 10 — CHATCUBEWINDOW FUNCTIONS
*Source: `include/ChatCubeWindow.h`, `core/ChatCubeWindow.cpp`*

```cpp
class ChatCubeWindow : public UltraCanvasWindow {
public:
    bool Initialize(const ChatCubeWindowStyle& style = {});
        // Builds all layout sections. Wires all callbacks.
        // Must be called once after construction.

    void OpenConversation(providerId, conversationId, title, avatarPath);
        // Switches the right panel to show the selected conversation.
        // Updates header, loads message history, marks as read,
        // sets active conversation in manager, rebuilds contact list.

    void RefreshContactList();
        // Fetches conversations from manager and rebuilds template rows.

    void ShowWelcomeScreen();
        // Hides chat area. Shows logo + version centred on right panel.
};
```

**AI rule — Layout section builders (private, called only from Initialize):**

```cpp
void BuildRootLayout();      // Splitter in VBox, wires onWindowResize
void BuildLeftPanel();       // Container + VBox
void BuildTopToolbar();      // Toolbar: Order-by dropdown + icons
void BuildContactList();     // Scrollable container + VBox for Template rows
void BuildRightPanel();      // Container + VBox
void BuildChatHeader();      // HBox: avatar + title/subtitle VBox + action toolbar
void BuildMessageArea();     // ListView + ChatBubbleDelegate + ChatMessageModel
void BuildInputSection();    // VBox: input toolbar + HBox(TextArea + send btn)
void BuildWelcomeScreen();   // Centred VBox: logo + app name + version
```

**AI rule — Contact row construction:**
```cpp
void AddContactRow(const ChatConversation& conv);
     // Creates one UltraCanvasTemplate per conversation.
     // Template contains: avatar image, provider icon, name label,
     //                    snippet label, timestamp label.
     // onClick → OpenConversation(providerId, convId, title, avatar)
     // Fixed height: 64px. Width: LeftPanelWidth.

void RebuildContactList();
     // Calls ClearChildren() then AddContactRow() for each conversation.
```

**AI rule — Element ID constants (namespace `ID::`):**
All element IDs are defined in the `ID::` anonymous namespace inside
`ChatCubeWindow.cpp`. Base values:
- Left panel: 1010–1050
- Right panel: 1060–1140
- Message area: 1200
- Input section: 1300–1370
- Welcome screen: 1400–1421
- Contact rows: `CONTACT_ROW_BASE` (2000) + row index

---

## SECTION 11 — MATRIX PLUGIN FUNCTIONS
*Source: `Plugins/Matrix/MatrixProviderPlugin.h/.cpp`*

### Protocol constants (namespace `MatrixAPI::`)

```cpp
constexpr const char* CLIENT_V3     = "/_matrix/client/v3";
constexpr const char* LOGIN         = "/_matrix/client/v3/login";
constexpr const char* LOGOUT        = "/_matrix/client/v3/logout";
constexpr const char* SYNC          = "/_matrix/client/v3/sync";
constexpr const char* WELLKNOWN     = "/.well-known/matrix/client";
constexpr int SYNC_TIMEOUT_MS       = 30000;
constexpr int RETRY_DELAY_MS        = 3000;
constexpr int MAX_RETRIES           = 5;
```

### HTTP integration points (4 stub methods to replace with libcurl)

```cpp
std::string HttpGet(const std::string& url, int& httpStatus) const;
    // GET with Authorization: Bearer <accessToken_>
    // Returns body. Sets httpStatus. Stub: returns "", status=503.

std::string HttpPost(const std::string& url,
                     const std::string& jsonBody,
                     int& httpStatus) const;
    // POST with Content-Type: application/json + Bearer auth.

std::string HttpPut(const std::string& url,
                    const std::string& jsonBody,
                    int& httpStatus) const;
    // PUT with Content-Type: application/json + Bearer auth.

std::string HttpUploadFile(const std::string& localPath,
                           const std::string& mimeType,
                           int& httpStatus) const;
    // POST binary body to /_matrix/media/v3/upload
    // Returns mxc:// URI from response JSON field "content_uri"
```

### Internal state

```cpp
std::atomic<ChatConnectionState> state_;
std::string homeserverUrl_;  // Resolved via .well-known
std::string userId_;         // @user:server
std::string accessToken_;
std::string deviceId_;
std::string nextBatch_;      // Long-poll sync pagination token
std::atomic<bool> syncRunning_;
std::thread syncThread_;
```

### JSON builder helpers

```cpp
static std::string JsonBuildLoginPassword(userId, password, deviceName);
static std::string JsonBuildTextMessage(body, replyToEventId = "");
static std::string JsonBuildMediaMessage(msgType, body, mxcUrl,
                                         mimeType, fileSize,
                                         imgW = 0, imgH = 0);
static std::string JsonBuildRedaction(reason = "");
static std::string JsonBuildTyping(isTyping, timeoutMs = 4000);
static std::string JsonBuildReadMarkers(eventId);
static std::string UrlEncode(str);
```

### msgtype → ChatMessageType mapping

| Matrix msgtype | ChatMessageType |
|---|---|
| m.text | Text (or ReplyText if InReplyToId set) |
| m.image | Image |
| m.file | File |
| m.audio | Audio |
| m.video | Video |
| m.sticker (event type) | Sticker |
| other | Text (raw body) |

---

## SECTION 12 — CALLBACK REGISTRY
*All callback names follow master_callbacks_V4.1 base-verb-form rule.*

### ChatMessageModel callbacks

```cpp
// Inherited from IListModel (existing framework callbacks):
std::function<void()>      onDataChanged;
std::function<void(int)>   onRowChanged;    // row index
std::function<void(int)>   onRowInserted;   // row index
std::function<void(int)>   onRowRemoved;    // row index

// ChatCube-specific addition:
std::function<void(int insertedCount)> onHistoryLoaded;
    // Fired after PrependHistory(). View re-anchors scroll position.
```

### ChatCubeProviderManager callbacks (set by ChatCubeWindow)

```cpp
onMessageReceive         (providerId, ChatMessage)
onMessageStatusChange    (providerId, messageId, ChatDeliveryStatus)
onTypingStatusChange     (providerId, convId, contactId, bool isTyping)
onContactStatusChange    (providerId, contactId, ChatPresenceStatus)
onContactListUpdate      ()
onConversationUpdate     (ChatConversation)
onConnectionStateChange  (providerId, ChatConnectionState)
onAuthRequire            (providerId)
onCallReceive            (providerId, callId, contactId, bool isVideo)
```

### ChatCubeWindow internal callbacks (wired in WireInputCallbacks)

```cpp
messageInput_->onTextChange    →  SetTypingStatus()
messageInput_->onTextSubmit    →  OnSendButtonClick()
sendButton_->onClick           →  OnSendButtonClick()
orderByDropdown_->onItemSelected →  OnOrderByChange()
messageFilterDropdown_->onItemSelected → OnMessageFilterChange()
```

---

## SECTION 13 — ARCHITECTURE RULES (AI enforcement)

### Rule A — UltraCanvas UI supremacy
Every visible element uses an existing UltraCanvas component. The approved list
for ChatCube is:
- Contact list rows: `UltraCanvasTemplate` with `TemplatePlacementRule::Flow`
- Message area: `UltraCanvasListView` + `ChatBubbleDelegate`
- Input: `UltraCanvasTextArea` (multiline, plain mode)
- Toolbars: `UltraCanvasToolbar` + `UltraCanvasToolbarBuilder`
- Header: `UltraCanvasImageElement` + `UltraCanvasLabel` + `UltraCanvasToolbar`
- Dialogs: `UltraCanvasDialogManager` (never custom modal containers)
- File open: `UltraCanvasDialogManager::ShowOpenFileDialog()`
- All media: `FileLoader_LoadImage`, `FileLoader_LoadAudio` etc.
- Camera/mic: `IODevice_OpenCamera`, `IODevice_OpenAudioInput`
- Screenshots: `IODevice_CaptureFrame`

### Rule B — No manual coordinates
All positioning via `CreateVBoxLayout`, `CreateHBoxLayout`, `CreateGridLayout`.
`SetWidthMode(SizeMode::Fill)`, `SetFixedHeight()`, `SetStretch()`,
`AddStretch()`, `SetCrossAlignment()` are the approved tools.
`AddSpacing(N)` for fixed gaps.

### Rule C — Data flow direction

```
Network (plugin worker thread)
    → ChatPluginEventCallback
    → ChatCubeProviderManager::HandlePluginEvent()  [thread boundary]
    → RouteIncomingMessage()  →  ChatMessageModel::AppendMessage()
    → onRowInserted           →  ListView::ScrollToRow()
    → onMessageReceive        →  ChatCubeWindow::OnIncomingMessage()
    → RebuildContactList()
```

**Never** call UltraCanvas UI methods from plugin worker threads.
The manager is the thread boundary. All `on*` callbacks on the manager
fire on the caller's thread — the plugin must document its threading model.

### Rule D — Optimistic send pattern
```
1. Build ChatMessage{IsOwn=true, DeliveryStatus=Sending}
2. model->AppendMessage(msg)  ← UI shows immediately
3. messageInput_->SetText("")
4. plugin->SendTextMessage(...)
5. On success: model->UpdateDeliveryStatus(msgId, Sent)
6. On failure: model->UpdateDeliveryStatus(msgId, Failed)
```

### Rule E — Plugin registration order
In `main.cpp`, register plugins after `mgr.Initialize()`, before
`window->Initialize()`. The window's `WireManagerCallbacks()` is called
inside `Initialize()` and expects the manager to already own its plugins.

### Rule F — Contact list rebuild triggers
`RebuildContactList()` is called from:
- `OnContactListUpdate()` — provider refreshed contacts
- `OpenConversation()` — to update selection highlight
- `OnIncomingMessage()` — to update snippet + unread badge
- `OnOrderByChange()` — sort order changed

Always set `conversations_` before calling `RebuildContactList()`.

### Rule G — Active conversation routing
Call `mgr.SetActiveConversation(providerId, convId, model)` every time
`OpenConversation()` is called. The manager uses this to route live
`MessageReceived` events directly into the model without going through
the window's callback.

---

## SECTION 14 — PROVIDER IMPLEMENTATION CHECKLIST

When implementing a new plugin (e.g. XMPP, Telegram):

```
□ Inherit IChatCubeProviderPlugin in Plugins/MyProvider/MyProviderPlugin.h
□ Implement all 10 interface sections (all pure virtual)
□ Use std::atomic<ChatConnectionState> for state_
□ Worker thread for network (never block UI thread)
□ Fire ChatPluginEvent from worker thread via SubscribeToEvents callback
□ GetCapabilities() returns only what is actually implemented
□ GetLoginType() returns correct ChatLoginType for this provider
□ GetIconPath() returns path relative to Resources/Media/Images/providers/
□ Authenticate() is async — calls onResult on worker thread
□ Disconnect() stops sync loop, calls logout endpoint, clears tokens
□ SubscribeToEvents() stores the callback; UnsubscribeFromEvents() nulls it
□ Lock callbackMutex_ when calling eventCallback_
□ Register in main.cpp: mgr.RegisterPlugin(make_shared<MyPlugin>())
□ Add CMake module target in CMakeLists.txt
□ Update AppInfo.txt Modules= list
```

---

## SECTION 15 — ROADMAP AND STEP NUMBERS

Steps already implemented (v1.0.0):
- Step 1: ChatCubeTypes, ChatMessageModel
- Step 2: ChatBubbleDelegate (all 10 message types)
- Step 3: IChatCubeProviderPlugin interface
- Step 4: ChatCubeProviderManager
- Step 5: MatrixProviderPlugin (HTTP stubs need libcurl)
- Step 6: ChatCubeWindow main layout
- Final: CMakeLists, AppInfo, Developer Guide, Master Registry (this file)

Planned future steps:
- Step 7:  Sticker picker dialog (UltraCanvasModalDialog subclass)
- Step 8:  Login / settings dialog (per-provider, uses ChatLoginType)
- Step 9:  XMPP plugin (libstrophe or gloox)
- Step 10: Telegram plugin (TDLib C wrapper)
- Step 11: ChatCube Direct P2P plugin (WebRTC + relay)
- Step 12: Voice/video call overlay window (IODeviceManager AV)
- Step 13: Push notifications (platform-specific)
- Step 14: Message search dialog (full-text across model)
- Step 15: HTTP layer completion (libcurl in MatrixProviderPlugin)

---

## SECTION 16 — RESERVED NAME SPACES

These prefixes are OWNED by ChatCube. Never use them for UltraCanvas
framework additions:

| Prefix | Owned by |
|---|---|
| `ChatCube*` | ChatCube application |
| `ChatMsg*` | ChatMessageModel custom roles |
| `IChatCube*` | Plugin interfaces |
| `Matrix*` | Matrix plugin internals |
| `Telegram*` | Telegram plugin internals (future) |
| `XMPP*` | XMPP plugin internals (future) |
| `ChatDirect*` | P2P Direct plugin internals (future) |

---

## SECTION 17 — DEPENDENCY MAP

```
main.cpp
  ├── ChatCubeWindow          ← UltraCanvasWindow
  │   ├── ChatMessageModel    ← IListModel
  │   ├── ChatBubbleDelegate  ← IItemDelegate
  │   └── ChatCubeProviderManager (singleton reference)
  │
  └── ChatCubeProviderManager
      ├── ChatMessageModel    (active conversation pointer)
      └── IChatCubeProviderPlugin[]
          └── MatrixProviderPlugin
              ├── MatrixAPI:: constants
              ├── HttpGet/Post/Put/UploadFile (stub → libcurl)
              └── JsonBuild* helpers

ChatCubeTypes.h ← included by everything above
```

---

## VERSION HISTORY

| Version | Date | Changes |
|---|---|---|
| 1.0.0 | 2026-04-13 | Initial ChatCube Master Registry. Covers all 6 implementation steps. |

---

*This registry is the authoritative AI reference for all future ChatCube development.
Always load this document at the start of a ChatCube programming session.*
