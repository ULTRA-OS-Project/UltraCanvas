# Handoff: UltraIM API + ChatCube App

```
// Version: 1.0.0
// Date: 2026-10-19
// Author: ULTRA OS Project / Cloverleaf
// Purpose: Instructions for Claude Code to turn the ChatCube v1 draft
//          (18 files, written in chat, never compiled) into
//            1. UltraIM  - a UI-independent instant-messenger module
//                          on top of UltraNet
//            2. ChatCube - the UltraCanvas app that uses UltraIM
```

---

## 0. How to use this document

Read this file first, then in this order:

1. `UltraCanvas Code Guideline V8.md` — supreme rules (Rules 9–15)
2. UltraNet master registry + `include/UltraNet/*.h` — the **only** allowed
   network surface
3. `master_types_registry V4.md`, `Master_functions_V4.1.md`,
   `Master_callbacks_V4.1.md`
4. `ChatCube_v1/ChatCube_Master_Registry_V1.md` — what the draft contains
5. `ChatCube_v1/` sources

Rules for this work:

- **The v1 code is a draft.** Every UltraCanvas and UltraNet call in it was
  written without the real headers. Verify each call against the headers
  before keeping it (section 7.2 lists them).
- **Never invent an API.** If UltraCanvas or UltraNet lacks something, stop
  and ask Stefan (Guideline Rule 14).
- **Never open sockets or call libcurl directly** in UltraIM or ChatCube.
  All transport goes through `UltraNet_*` (UltraNet README, "Reserved").
- Open decisions in section 9 must be answered by Stefan before the parts
  that depend on them are written.

---

## 1. Goals and current state

**Goal 1 — messenger framework.** One app, many networks. Each messenger
that offers login + API (Matrix, XMPP, Telegram, WhatsApp Business, …) is a
provider plugin.

**Goal 2 — ChatCube Direct.** Own peer-to-peer messenger with server backup.

**v1 draft (in `ChatCube_v1/`)**

| Layer | Files |
|---|---|
| Types | `include/ChatCubeTypes.h`, `core/ChatCubeTypes.cpp` |
| List model | `include/ChatMessageModel.h`, `core/ChatMessageModel.cpp` |
| Bubble renderer | `include/ChatBubbleDelegate.h`, `core/ChatBubbleDelegate.cpp` |
| Plugin interface | `include/IChatCubeProviderPlugin.h` |
| Plugin manager | `include/ChatCubeProviderManager.h`, `core/ChatCubeProviderManager.cpp` |
| Matrix plugin | `Plugins/Matrix/MatrixProviderPlugin.h/.cpp` |
| Main window | `include/ChatCubeWindow.h`, `core/ChatCubeWindow.cpp`, `main.cpp` |
| Project | `CMakeLists.txt`, `AppInfo.txt`, `DEVELOPER_GUIDE.md`, `ChatCube_Master_Registry_V1.md` |

UI reference: the screenshots of the RISC OS original in
`Docs/reference/` (contact list, bubbles, file cards, sticker picker,
screenshot menu, date separators).

---

## 2. Target architecture

```
┌──────────────────────────────────────────────────────────────┐
│ ChatCube app  (UltraCanvas UI)                                │
│  ChatCubeWindow · ChatBubbleDelegate · ChatMessageModel       │
│  login / sticker / call dialogs                               │
└───────────────▲──────────────────────────┬───────────────────┘
                │ UltraIM events            │ UltraIM_* calls
                │ (on the UI thread, via    │
                │  the injected dispatcher) │
┌───────────────┴──────────────────────────▼───────────────────┐
│ UltraIM module  (no UltraCanvas UI dependency)                │
│  types · IUltraIMProviderPlugin · provider manager · events   │
│  provider plugins: Matrix | XMPP | Telegram | Direct | …       │
└───────────────┬──────────────────────────────────────────────┘
                │ UltraNet_* only
┌───────────────▼──────────────────────────────────────────────┐
│ UltraNet  HTTP/S · WebSocket · TCP/UDP · TLS · DNS · OAuth2   │
│           (+ WebRTC — see decision D2)                        │
└───────────────┬──────────────────────────────────────────────┘
                │
   ┌────────────┴──────────────┬───────────────────────────┐
   ▼                           ▼                           ▼
 Provider servers         ChatCube Relay Server         coturn
 (Matrix homeserver,      (Direct: accounts, contacts,  (STUN + TURN,
  XMPP, Telegram, …)       presence, signalling,         NAT traversal
                           offline + group messages)     only)
```

Dependency rule: **UltraNet ← UltraIM ← ChatCube.** UltraIM may use
header-only UltraCanvas helpers (as UltraNet does) and FileLoader for MIME
type / file info, but never UltraCanvas UI classes (`IListModel`,
`IRenderContext`, windows, widgets).

---

## 3. The UltraIM / ChatCube split

### 3.1 File moves

| v1 file | Goes to | Changes |
|---|---|---|
| `ChatCubeTypes.h/.cpp` | `UltraIM/include/UltraIM/UltraIMTypes.h`, `UltraIM/core/UltraIMTypes.cpp` | Rename types (3.2). Drop `#include "UltraCanvasCommonTypes.h"` unless only header-only types are used. Remove `CHATCUBE_VERSION`/`CHATCUBE_APP_ID` (they belong to the app). |
| `IChatCubeProviderPlugin.h` | `UltraIM/include/UltraIM/UltraIMPlugins.h` | Rename to `IUltraIMProviderPlugin`. Apply interface fixes (3.3). |
| `ChatCubeProviderManager.h/.cpp` | `UltraIM/core/UltraIMManager.h/.cpp` (internal) | Remove all model coupling (section 4). Public access via `UltraIM_*` functions (3.4). |
| `Plugins/Matrix/*` | `UltraIM/Plugins/matrix/` | Bind to UltraNet (section 6), JSON via nlohmann/json, fix bugs (7.3). |
| `ChatMessageModel.h/.cpp` | stays in `ChatCube/` | Becomes an adapter fed by UltraIM events (4.4). Keeps `ChatMsgRole_*`. |
| `ChatBubbleDelegate.h/.cpp` | stays in `ChatCube/` | Fix compile issues (7.1) and row-height mismatch (7.3). |
| `ChatCubeWindow.h/.cpp`, `main.cpp` | stays in `ChatCube/` | Uses `UltraIM_*` instead of the manager singleton. |
| `CMakeLists.txt` | split | `UltraIM` library target (+ plugin targets) and `ChatCube` app target. Remove `pkg_check_modules(CURL …)`; link `UltraNet`. |
| `AppInfo.txt` | stays in `ChatCube/` | `RequiredModules` gains `UltraNet, UltraIM`. |
| `ChatCube_Master_Registry_V1.md` | replaced | Write `UltraIM_Master_Registry_V1.md` and `ChatCube_Master_Registry_V2.md` after the refactor compiles (phase 5). |

Proposed layout (mirrors UltraNet):

```
UltraIM/
├── include/UltraIM/
│   ├── UltraIMCore.h        public UltraIM_* functions
│   ├── UltraIMTypes.h       messages, contacts, conversations, events
│   └── UltraIMPlugins.h     IUltraIMProviderPlugin
├── core/UltraIM/            implementations, internal manager
└── Plugins/UltraIM/
    ├── matrix/
    ├── direct/              ChatCube Direct client (after D1–D3)
    ├── xmpp/                later
    └── telegram/            later

ChatCube/
├── include/  core/          window, delegate, model, dialogs
├── main.cpp  AppInfo.txt  CMakeLists.txt
└── Resources/  (UOS bundle layout)

Server/ChatCubeRelay/        Relay Server (after D1)
```

### 3.2 Type renames (UltraNet naming style: `UltraIM<Type>`)

| v1 | UltraIM |
|---|---|
| `ChatMessage`, `ChatAttachment`, `ChatReplyContext` | `UltraIMMessage`, `UltraIMAttachment`, `UltraIMReplyContext` |
| `ChatContact`, `ChatConversation` | `UltraIMContact`, `UltraIMConversation` |
| `ChatMessageType`, `ChatDeliveryStatus`, `ChatPresenceStatus` | `UltraIMMessageType`, `UltraIMDeliveryStatus`, `UltraIMPresence` |
| `ChatCapabilityFlags`, `ChatLoginType`, `ChatCredentials` | `UltraIMCapabilityFlags`, `UltraIMLoginType`, `UltraIMCredentials` |
| `ChatConnectionState` | `UltraIMConnectionState` |
| `ChatPluginEvent`, `ChatPluginEventType` | `UltraIMEvent`, `UltraIMEventType` |
| `ChatPluginResult`, `ChatPluginResultCode` | `UltraIMResult`, `UltraIMResultCode` (same shape as `UltraNetResult`: code, message, operator bool) |
| `ChatSendOptions`, `ChatStickerPack` | `UltraIMSendOptions`, `UltraIMStickerPack` |
| `ChatManagerConfig` | `UltraIMConfig` |

Check every new name against `master_types_registry V4.md` and the
UltraNet registry before creating it. Callbacks keep base-verb form
(`onMessageReceive`, not `onMessageReceived`).

### 3.3 Interface changes in `IUltraIMProviderPlugin`

1. **Message actions take the conversation id.**
   `DeleteMessage(conversationId, messageId)`,
   `EditMessage(conversationId, messageId, newText)`. Matrix (and most
   protocols) address messages inside a room; v1 had no way to find the
   room.
2. **Client message id for the optimistic send** (section 5.4).
   `UltraIMSendOptions` gains `std::string clientMessageId`. Every send
   echoes it back in the `MessageDelivered` event together with the
   server-assigned `MessageId`.
3. **Per-conversation call capability.** Add
   `bool CanCall(const std::string& conversationId, bool video) const`
   so the UI can disable call buttons for group conversations on providers
   that cannot do group calls (section 5.3).
4. Keep `UltraIMResult` returns for synchronous rejections only (not
   connected, capability missing, bad argument). Network outcomes arrive
   as events.

### 3.4 Public API style (proposal — decision D4)

Sibling modules (UltraNet, FileLoader, PixelFX, IODeviceManager) expose
`Module_Action` free functions. Proposed for UltraIM:

```cpp
// Lifecycle
UltraIMResult UltraIM_Initialize(const UltraIMConfig& config);
void          UltraIM_Shutdown();

// Providers
void UltraIM_RegisterProvider(std::shared_ptr<IUltraIMProviderPlugin> p);
void UltraIM_RefreshProviders();                       // scan plugin DSOs
std::vector<UltraIMProviderInfo> UltraIM_ListProviders();

// Events (section 4)
void   UltraIM_SetEventDispatcher(UltraIMDispatcher dispatcher);
UltraIMSubscriptionId UltraIM_Subscribe(UltraIMEventCallback onEvent);
void   UltraIM_Unsubscribe(UltraIMSubscriptionId id);

// Sessions
void UltraIM_SignIn(const std::string& providerId,
                    const UltraIMCredentials& credentials,
                    UltraIMResultCallback onResult);
void UltraIM_SignOut(const std::string& providerId);

// Data
void UltraIM_FetchConversations(UltraIMConversationsCallback onResult);
void UltraIM_FetchMessages(const std::string& providerId,
                           const std::string& conversationId,
                           const std::string& beforeMessageId, int limit,
                           UltraIMMessagesCallback onResult);

// Sending (returns at once; outcome arrives as MessageDelivered event)
UltraIMResult UltraIM_SendText(const std::string& providerId,
                               const std::string& conversationId,
                               const std::string& text,
                               const UltraIMSendOptions& options);
// … SendFile, SendImage, SendSticker, SendVoiceNote,
//   DeleteMessage, EditMessage, MarkRead, SetTyping, StartCall, EndCall
```

Check each name against `Master_functions_V4.1.md` and get Stefan's
approval before creating them (Guideline Rule 10).

---

## 4. Manager decoupling

### 4.1 Coupling in v1 (all in `ChatCubeProviderManager`)

| Where | Problem |
|---|---|
| `SetActiveConversation(providerId, convId, ChatMessageModel*)` | Library holds a UI model pointer. |
| `LoadConversation(…, ChatMessageModel*)`, `LoadOlderMessages(…, ChatMessageModel*)` | Library writes into a UI model. |
| `RouteIncomingMessage()` → `activeModel_->AppendMessage()` | UI model mutated on a plugin worker thread. |
| `HandlePluginEvent()` → `activeModel_->UpdateDeliveryStatus()` | Same. |
| `#include "ChatMessageModel.h"` in the manager header | UltraIM would depend on UltraCanvas `IListModel`. |

### 4.2 New design

1. **UltraIM knows nothing about views.** Remove `activeModel_`,
   `activeConversation_`, `SetActiveConversation`, `ClearActiveConversation`
   and every `ChatMessageModel*` parameter.
2. **History is a request with a callback.**
   `UltraIM_FetchMessages(…, onResult)` hands back
   `std::vector<UltraIMMessage>`. The app decides where it goes.
3. **Everything else is an event.** One stream of `UltraIMEvent`
   (`MessageReceive`, `MessageDelivered`, `TypingChange`, `PresenceChange`,
   `ConversationUpdate`, `ConnectionStateChange`, `AuthRequire`,
   `CallReceive`). Subscribers filter by provider/conversation themselves.
4. **Threading contract via an injected dispatcher.**
   ```cpp
   using UltraIMDispatcher = std::function<void(std::function<void()>)>;
   ```
   - Plugins run network work on UltraNet's async machinery and raise
     events from worker threads.
   - The manager puts each event into the dispatcher. ChatCube installs a
     dispatcher that posts the closure to the UltraCanvas UI thread
     (**find the real UltraCanvas "run on main thread" API in the headers**;
     do not write one).
   - Default dispatcher (none installed) = call directly, for tests and
     headless tools.
   - Result: every `onEvent` and every `onResult` callback runs on the UI
     thread in ChatCube. No UI code ever needs a lock.
5. **Manager locking fixes** (see 7.3): never call into a plugin while
   holding `pluginMutex_`; hand out `std::shared_ptr` copies instead of raw
   pointers; merge fan-out results (`RefreshContacts`,
   `RefreshConversations`) through the dispatcher, not on worker threads.

### 4.3 Data flow after the change

```
UltraNet async callback (libcurl worker thread)
  → plugin parses → raises UltraIMEvent
  → UltraIM manager → dispatcher(post to UI thread)
  → ChatCube subscriber (UI thread)
       ├─ ChatMessageModel::OnUltraIMEvent()  → AppendMessage / UpdateDelivery
       └─ ChatCubeWindow                      → contact list, typing, title
```

### 4.4 ChatCube side

- `ChatMessageModel` owns `activeProviderId_` / `activeConversationId_` and
  subscribes to UltraIM. It appends only messages for its conversation.
- `ChatCubeWindow::OpenConversation()` sets the model's active conversation,
  calls `UltraIM_FetchMessages()` and fills the model in the callback.
- The model does the optimistic-send reconciliation (5.4).
- `ChatMsgRole_*` stay in ChatCube; they are UI roles, not library data.

---

## 5. Decisions carried forward from the design chat

### 5.1 TURN is not the offline backup

Two different things were both called "relay":

| | TURN (coturn) | ChatCube Relay Server offline store |
|---|---|---|
| Layer | Network (ICE / WebRTC) | Application |
| When | Only when a direct P2P path cannot be built (symmetric NAT, UDP blocked) | When the recipient is offline, and for group text |
| Stores messages | **Never** — forwards packets, stateless | Yes — queue in the Relay DB until delivered |
| Knows users | No (only short-lived credentials) | Yes — accounts, contacts, sessions |
| Visible to the app | No, ICE picks it transparently | Yes, explicit REST/WebSocket calls |

Consequences for code:
- No message storage, retry queue or user logic ever lives in anything
  called TURN.
- The Direct plugin only passes STUN/TURN URLs + credentials into the ICE
  configuration of the WebRTC library. It never talks to coturn otherwise.
- The Relay Server issues **time-limited TURN credentials** (coturn
  `use-auth-secret` / TURN REST scheme) so no static TURN password ships in
  the app.

### 5.2 ChatCube Relay Server — v1 scope

**Does:**
- User registration, login, logout, session tokens
- Contact list and contact discovery
- Presence
- WebSocket signalling for 1:1 P2P setup (SDP offer/answer, ICE candidates)
- Offline message queue: stores 1:1 messages while the recipient is offline,
  delivers on reconnect, deletes after acknowledgement
- Group text: stores group messages and fans them out to members
- Issues short-lived TURN credentials

**Does not:**
- Relay media or P2P data packets (that is TURN)
- Mix or forward group audio/video (that is an SFU, v2)
- Read message content if E2E is chosen (decision D5)

API sketch (to be reviewed with Stefan before building):

```
REST  POST /v1/register          POST /v1/login → token       POST /v1/logout
      GET|PUT|DELETE /v1/contacts                              PUT  /v1/presence
      GET  /v1/messages?since=…  (fetch offline queue)         POST /v1/messages/ack
      POST /v1/groups            POST|DELETE /v1/groups/{id}/members
      GET  /v1/turn-credentials  (short-lived)
WS    /v1/ws  frames: presence, typing, signal.offer, signal.answer,
                      signal.ice, message.new, message.ack
```

1:1 delivery rule for Direct:
- Both online and P2P DataChannel open → send P2P, nothing stored on the server.
- Recipient offline or no P2P path yet → `POST /v1/messages` to the queue.
- Every message carries a client-generated id, so a message that took both
  paths during a reconnect is shown once.

### 5.3 Group chat and calls

- Pure P2P does not work for groups: a full mesh needs n·(n−1)/2
  connections and every member uploads n−1 streams. It breaks down past 3–4
  members and gives no offline delivery.
- **v1:** group **text** through the Relay Server (store + fan-out).
  1:1 text and 1:1 audio/video P2P (WebRTC + STUN/TURN).
- **v1 UI:** call buttons disabled for group conversations whenever
  `CanCall()` says no (Direct: always no for groups in v1). Third-party
  providers with their own group calls (Matrix) may say yes.
- **v2:** SFU for group audio/video (mediasoup recommended). Each member
  sends one stream to the SFU, which forwards to the others. Not in v1.

### 5.4 Optimistic send

Pattern:
1. Create `UltraIMMessage` with `IsOwn = true`,
   `DeliveryStatus = Sending`, `clientMessageId = <new UUID>`.
2. Append it to `ChatMessageModel` at once; clear the input.
3. `UltraIM_SendText(…, options{ clientMessageId })`.
4. Synchronous rejection → mark `Failed`.
5. `MessageDelivered` event (same `clientMessageId`) → replace the local id
   with the server `MessageId`, set `Sent`/`Delivered`/`Read`.
6. When the server echoes our own message back (Matrix sync returns it with
   `unsigned.transaction_id`), match on `clientMessageId` and **update the
   existing row instead of appending a second one.**

v1 never did steps 5–6: the `local_…` id was never reconciled, so every
sent message would appear twice once sync works. For Matrix use the
`clientMessageId` as the `txnId` of `PUT …/send/m.room.message/{txnId}`.

---

## 6. Bind to UltraNet here

Every transport below must use UltraNet. Names come from the UltraNet
README; confirm exact signatures in the UltraNet master registry.

### 6.1 Matrix plugin

| v1 code | Bind to | Notes |
|---|---|---|
| `HttpGet()` stub | `UltraNet_HttpRequestAsync()` with `method = Get` | Header `authorization: Bearer <token>`. Callback runs on libcurl worker thread → raise UltraIM event, never touch UI. Check `IsComplete()` / `transferError`. |
| `HttpPost()` stub | `UltraNet_HttpRequestAsync()`, `Post` | `content-type: application/json`; body from nlohmann/json. |
| `HttpPut()` stub | `UltraNet_HttpRequestAsync()`, `Put` | Sends, typing, redactions. |
| `HttpUploadFile()` stub | `UltraNet_HttpRequestAsync()`, `Post` to `/_matrix/media/v3/upload` | Look up in the registry whether a request body can stream from a file; otherwise load via FileLoader. MIME type from `FileLoader_GetMimeType()`, not hard-coded. Returns `content_uri`. |
| Sync loop thread + `PerformSync()` | One async long-poll request at a time (`timeout=30000`), next request started from the completion callback | `Disconnect()` calls `UltraNet_CancelRequest(handle)` — no 30 s hang on join. Delete `syncThread_`. |
| 6 × `std::thread(...).detach()` | `UltraNet_HttpRequestAsync()` | Removes use-after-free on plugin destruction. |
| `DownloadAvatar()` (fake path) | `UltraNet_HttpRequestAsync()` with `options.outputFilePath` | Thumbnail URL `…/thumbnail/{server}/{id}?width=64&height=64&method=crop`. Return the path only after `IsComplete()`. |
| `DownloadAttachment()` (fake success) | same, plus `resumeOutputFile`, `stallTimeoutMs`, `onDownloadProgress` | Cancel with `UltraNet_CancelRequest`. |
| `DiscoverHomeserver()` hand-rolled JSON scan | `UltraNet_HttpRequestAsync()` on `/.well-known/matrix/client` + nlohmann/json | URL work via `UltraNet_ParseUrl` / `UltraNet_ResolveUrl`. |
| `UrlEncode()` | `UltraNet_UrlEncode()` | Delete own implementation. |
| `JsonGetString`, `JsonGetInt64`, `JsonBuild*` | nlohmann/json (not UltraNet) | Fixes missing escaping (7.3). |
| Login with password | unchanged endpoint, via UltraNet | If the homeserver uses OIDC-based auth (Matrix Authentication Service), use `UltraNet_OAuth2AuthorizeInteractive()` + `UltraNet_OAuth2Refresh()`. Check the homeserver's `/login` flows first. |
| `accessToken_` in memory only | UltraVault named credential | Restore session without re-login. |

### 6.2 ChatCube Direct plugin + Relay client

| Transport | Bind to | Notes |
|---|---|---|
| Relay REST (register, login, contacts, offline queue, TURN creds) | `UltraNet_HttpRequestAsync()` | Self-hosted relay with own cert: trust it by key with `UltraNetHttpOptions::pinnedPublicKey` (`UltraNet_PublicKeyPinOf`), never `acceptInvalidCert` alone. |
| Relay signalling + presence + `message.new` | `UltraNet_WebSocketConnect()` (wss), `UltraNet_WebSocketSendText()`, `onWebSocketText`, `UltraNet_WebSocketClose()` | Needs libcurl ≥ 7.86 with ws. Reconnect with backoff; on reconnect fetch offline queue. |
| P2P DataChannel + 1:1 audio/video, ICE, STUN, TURN | **Not in UltraNet today** | Decision D2. Do not use `UltraNet_Udp*` to hand-roll STUN/ICE. |
| LAN peer discovery (optional) | UltraNet `mDNS` plug-in | Only if Stefan wants serverless LAN chat. |
| Token storage | UltraVault | Same as Matrix. |

### 6.3 Later providers

| Provider | Bind to | Notes |
|---|---|---|
| XMPP | `UltraNet_Tcp*` + `UltraNet_Tls*` (STARTTLS), `UltraNet_DnsResolve(…, SRV)` for `_xmpp-client._tcp` | Or a new UltraNet XMPP plug-in — ask Stefan. |
| Telegram (TDLib) | **Cannot bind** — TDLib owns its MTProto sockets | **Resolved:** shipped as the documented exception (framework 0.9.376) — the one provider outside UltraNet; see D6 in [`ChatCubeDirectPlan.md`](../Research/ChatCubeDirectPlan.md). |
| WhatsApp Business | `UltraNet_HttpRequestAsync()` (HTTPS REST) | Incoming messages arrive by webhook → need a public endpoint (could be the Relay Server). |

### 6.4 Everywhere

- Proxy, TLS defaults, DNS cache: inherited from UltraNet — set nothing.
- Cancellation on sign-out/shutdown: `UltraNet_CancelRequest()` for async
  requests; `UltraNet_SetThreadAbortCheck()` if any synchronous call remains.
- Debug: `ULTRANET_CURL_VERBOSE=1` and `UltraNetResult::diagnostics`.

---

## 7. Known issues in the v1 draft

### 7.1 Certain compile / configure errors

| File | Issue | Fix |
|---|---|---|
| `include/ChatBubbleDelegate.h:134` vs `core/ChatBubbleDelegate.cpp:111` | `RenderItem` declared non-const, defined `const`. | Match the real `IItemDelegate::RenderItem` signature from the header. |
| `core/ChatBubbleDelegate.cpp` (32 uses) | `GetStringValue()` used, never defined. | Add a helper in an anonymous namespace that returns the string held in the `ListDataValue` or a static empty string. |
| `core/ChatBubbleDelegate.cpp:729` | `std::sin`, float `std::abs` without `<cmath>`. | `#include <cmath>`. |
| `core/ChatBubbleDelegate.cpp:63` | `int + float` return in `GetRowHeight` (Audio). | `static_cast<int>(…)`. |
| `CMakeLists.txt:175, 198` | Telegram and Direct targets list source files that do not exist; Direct defaults `ON` → configure fails. | Remove until written. |
| `CMakeLists.txt` | Matrix built as SHARED `.module` **and** linked into the executable. | Choose: compiled-in for v1, or a loadable DSO (decision D3). |
| `CMakeLists.txt` | Finds libcurl itself. | Link `UltraNet` instead. |

### 7.2 Unverified UltraCanvas API calls — check every one against the headers

| Area | Calls used in v1 |
|---|---|
| Application / window | `UltraCanvasApplication::Create/Initialize/RegisterWindow/Run`, `WindowConfig{title,width,height,resizable,backgroundColor}`, `Create()`, `Show()`, `CenterOnScreen()`, `onWindowResize`, `SetWindowTitle()`, `Hide()` |
| Layout | `CreateVBoxLayout/CreateHBoxLayout`, `AddUIElement(elem, stretch)->SetWidthMode/SetFixedHeight/SetFixedSize/SetFixedWidth/SetCrossAlignment`, `AddStretch`, `AddSpacing`, `SetPadding`, `SetDefaultMainAxisAlignment`, `GetLayout()` cast to `UltraCanvasBoxLayout*` |
| Splitter | `CreateSplitter(…, SplitterOrientation::Horizontal)`, `SetDividerPosition`, `SetMinimumLeftSize`, `SetMaximumLeftSize`, `SetLeftPanel`, `SetRightPanel`, `GetDividerPosition` |
| Toolbar | `CreateToolbar`, `ToolbarItemDescriptor{type,identifier,text,widget,stretch}`, `ToolbarItemType::Label/CustomWidget/Stretch/Separator`, `SetFixedHeight`. **`ChatCubeWindow.cpp:198–212` builds a throw-away `UltraCanvasToolbarBuilder` toolbar to steal one descriptor — replace with the real add-button API.** |
| Dropdown | `CreateDropdown`, `AddItem`, `SetSelectedIndex`, `onItemSelected(int, const DropdownItem&)` |
| ListView | `CreateListView`, `SetModel`, `SetDelegate`, `SetShowHeader`, `ListViewStyle{selectionBackgroundColor,hoverBackgroundColor}`, `SetStyle`, `ScrollToRow` |
| List model | `IListModel` virtuals, `NotifyRowChanged/NotifyRowInserted/NotifyDataChanged`, `ListDataValue` variant members, `ListIndex{row,col}`, `ListDataRole::UserRole` |
| TextArea | `SetPlaceholder`, `SetEditingMode(TextAreaEditingMode::Plain)`, `SetShowLineNumbers`, `SetWordWrap`, `onTextSubmit`, `onTextChange`, `GetText`, `SetText`, `SetFocus` |
| Template | `UltraCanvasTemplateBuilder`, `TemplateAppearance`, `TemplateDimensions::Fixed`, `TemplatePlacementRule::Flow`, `TemplateElementDescriptor{elementType,identifier,iconPath,constraints}`, `AddLabel`, `Build`, `onClick`, `SetBackgroundColor` |
| Container | `UltraCanvasContainer(name,id,x,y,w,h)`, `ContainerStyle{autoShowScrollbars,forceShowHorizontalScrollbar}`, `SetContainerStyle`, `SetBorders(…)` overloads, `AddChild`, `ClearChildren`, `SetVisible` |
| Image / label / button | `CreateImageElement`, `SetImagePath`, `SetScaleMode(ImageScaleMode::Uniform)`, `UltraCanvasLabel` `SetText/SetFontSize/SetFontWeight/SetTextColor/SetAlignment`, `UltraCanvasButton` `onClick` |
| Menus / dialogs | `CreateContextMenu`, `AddItem`, `AddSeparator`, `ShowAtMousePosition`; `UltraCanvasDialogManager::ShowOpenFileDialog(title, filters, path, callback, parent)`, `FileFilter`, `DialogResult::OK` |
| Render context | `ClipRoundedRectangle(rect, r1..r4)`, `DrawImage(path, rect, ImageFitMode)`, `DrawFilledRectangle(rect, fill, width, border, radius)`, `DrawFilledCircle`, `DrawLine`, `GetTextLineDimensions`, `GetTextDimensions(text, w, h, outW, outH)`, `SetTextWrap(TextWrap::WrapWordChar)`, `SetTextAlignment`, `SetTextVerticalAlignment`, `SetFontSlant`, `SetStrokePaint`, `SetStrokeWidth`, `PushState/PopState`, `Colors::Transparent` |
| Screenshot | IODeviceManager capture function name (v1 only logs) — look up in `IODeviceManager_Master_Registry V1.md`. |

### 7.3 Logic and runtime bugs

| File | Bug | Fix |
|---|---|---|
| Matrix `ApiUrl()` | Prepends only the homeserver. All relative paths (`/joined_rooms`, `/rooms/…`, `/profile/…`) miss `/_matrix/client/v3`; only `LOGIN/LOGOUT/SYNC` carry it. Every room call would 404. | Let `ApiUrl()` add `CLIENT_V3` and strip the prefix from the three constants (or vice versa) — consistently. |
| Matrix `ProcessSyncResponse()` | Empty. No incoming message, room, presence or typing ever arrives; `rooms_` stays empty, so contacts and conversations are empty. | Implement with nlohmann/json: `rooms.join.*.timeline`, `.ephemeral` (typing, receipts), `.state` (names, avatars), `presence`, `next_batch`. |
| Matrix `GetMessages()` | Returns an empty list (`TODO`). | Parse `chunk` via `ParseMatrixEvent`; keep the `end` token for paging. |
| Matrix `JsonBuild*` | String concatenation without escaping. A quote, backslash or newline in a message or password breaks the request; it is also an injection risk. | nlohmann/json for all bodies. |
| Matrix `DeleteMessage`, `EditMessage` | Always fail (no room lookup / not implemented). | Interface change 3.3.1; edit = `m.room.message` with `m.new_content` + `m.relates_to.rel_type = m.replace`. |
| Matrix `MarkConversationRead` | Sends empty event ids. | Track latest event id per room from sync. |
| Matrix caches | `rooms_`, `userDisplayNames_`, `userAvatarMxc_` written from several threads without a lock; `ParseMatrixEvent` uses `const_cast` and blocking HTTP inside parsing. | Guard with a mutex; resolve names from sync `m.room.member` state instead of per-event HTTP. |
| Matrix `SendImage/SendFile` | MIME hard-coded (`image/jpeg`, `application/octet-stream`), size 0, no width/height. | FileLoader for MIME, size and image dimensions. |
| Manager `RefreshContacts/Conversations` | `remaining` counter non-atomic; `contacts_` written from worker threads; `onContactListUpdate` fired on a worker thread. | Merge through the dispatcher (4.2). |
| Manager `UnregisterPlugin`, `DisconnectAll` | Call `Disconnect()` while holding `pluginMutex_` → deadlock if the plugin raises an event. | Copy the plugin pointers, release the lock, then call. |
| Manager `FindPlugin` | Returns a raw pointer after releasing the lock. | Return `std::shared_ptr`. |
| Window `OnVoiceCallButtonClick/OnVideoCallButtonClick` | Pass the conversation id as contact id. | Pass the contact id of a 1:1 conversation; disable for groups (5.3). |
| Window `OnSendButtonClick` | Local id never reconciled; marks `Sent` right after a stub returns. | Section 5.4. |
| Window `AddContactRow` | Row id from unread count + id length → collisions. | Use the row index. |
| Window contact rows | Presence dot and unread badge from `ChatContact` are never drawn. | Add to the template (screenshots show both). |
| Delegate `GetRowHeight` vs render | Height estimated as `bubbleWidth / 7` chars per line, while `RenderTextBubble` measures real text; the sender-name line of other people's messages is not counted. Bubbles will clip or overlap. | Measure with the real text metrics (or cache measured heights per row and width) and include the name line. |
| Delegate avatars | Matrix avatars passed as HTTP URLs into `DrawImage`. | Draw only downloaded local files; show initials until then. |
| Window screenshot menu | Hides the window but never captures or restores it. | IODeviceManager capture → `UltraIM_SendImage` → show window. |

---

## 8. Work order for Claude Code

Each phase ends with a clean build and a short report to Stefan.

1. **UltraIM skeleton.** Create the module (3.1 layout), move and rename
   types and the plugin interface, internal manager without any UI coupling,
   dispatcher + subscribe API. Builds with no UltraCanvas UI include.
2. **ChatCube compiles against real UltraCanvas.** Move window, delegate,
   model; fix 7.1; verify every call in 7.2 against the headers and fix;
   wire to UltraIM events. App starts and shows the welcome screen.
3. **Matrix on UltraNet.** Section 6.1 and the Matrix rows of 7.3. Test
   against a real homeserver (a local Synapse or Conduit, or a test account
   on a public one): sign in, list rooms, open a room, receive, send, see the
   echo reconciled, upload an image, download a file.
4. **Optimistic send end to end** (5.4) and row-height fix.
5. **Registries.** Write `UltraIM_Master_Registry_V1.md` and
   `ChatCube_Master_Registry_V2.md` in the format of the existing master
   registries; update `DEVELOPER_GUIDE.md`.
6. **Direct + Relay Server** — only after D1, D2, D5 are answered.

---

## 9. Open decisions — ask Stefan before the dependent phase

| Id | Question | Recommendation | Blocks |
|---|---|---|---|
| D1 | Relay Server language and stack (C++ vs Python/Node), database (SQLite vs PostgreSQL). | C++ keeps one toolchain; a small WebSocket server library is needed either way (UltraNet is a client library). SQLite for v1. | Phase 6 |
| D2 | Where WebRTC (ICE, STUN/TURN, DataChannel, media) lives. | libdatachannel (C++, small, own ICE via libjuice), wrapped as a new UltraNet plug-in so UltraIM still only calls `UltraNet_*`. | Phase 6 |
| D3 | Provider plugins as loadable DSOs (UltraNet-style host table + ABI) or compiled in for v1. | Compiled-in Matrix for v1; design the interface so a DSO host table can follow. | Phase 1 CMake |
| D4 | UltraIM public surface: `UltraIM_*` free functions (3.4) or a manager class. | Free functions, matching UltraNet/FileLoader/PixelFX. | Phase 1 |
| D5 | ChatCube Direct encryption: should the Relay Server store only ciphertext? Identity via key pair (challenge-signature login)? | Yes — E2E, server sees ciphertext only; key-pair identity as in `UltraIMLoginType::KeyPair`. | Phase 6 |
| D6 | Telegram via TDLib bypasses UltraNet (TDLib owns its sockets). Allowed exception? | Allow as a documented exception, or drop Telegram for v1. **Resolved:** the documented exception — the Telegram provider shipped in framework 0.9.376 (D6 in [`ChatCubeDirectPlan.md`](../Research/ChatCubeDirectPlan.md)). | Telegram plugin |
| D7 | Module location: inside the UltraCanvas repo next to UltraNet, or its own repo. | Next to UltraNet, same build conventions. | Phase 1 |

---

## 10. Done when

- `UltraIM` builds without any UltraCanvas UI header.
- No file in UltraIM or ChatCube includes `<curl/curl.h>` or opens a socket.
- ChatCube builds against the real UltraCanvas headers with no warnings
  from the items in 7.1.
- A Matrix account can sign in, read and send messages, upload and download
  files, and sent messages show once with ticks updating.
- Every UI update happens on the UI thread (dispatcher in place).
- Group conversations show call buttons disabled on Direct.
- New registries written; this handoff marked superseded.
