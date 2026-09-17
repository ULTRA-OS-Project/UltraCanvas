# UltraMessage — Cross-Platform Message Channel (Design Proposal)

**Date:** 2026-09-17
**Status:** Proposal — for review; no implementation yet
**Registry entry:** `Masterfile_modules.md` §13

---

## 1. Purpose

UltraMessage is the message channel of the ULTRA OS stack: **one API through
which applications send structured messages to each other, receive what other
applications and the platform publish, and expose commands that other
programs may invoke** — on Linux / ULTRA OS, Windows, macOS, and later
WebAssembly and Android, from a single codebase. It is a sibling module of
UltraNet / UltraDatabase / UltraVault: an UltraCanvas-owned API with a
platform backend per OS, never a third-party type in a public header.

The model is the RISC OS Wimp: `Wimp_SendMessage` delivers a typed data block
from one task to another, or to everyone, with *recorded* delivery that
bounces back when nobody acknowledged it. No modern OS has that single broker
(§3), so UltraMessage supplies it and uses each platform's native channels as
adapters at the edges.

Three jobs, one module:

| Job | What it means | First consumer |
|---|---|---|
| **A. Channel** | App ↔ app messages: post, recorded post with bounce, request/reply, subscribe by topic | UltraFiler ↔ UltraViewer ("open this", "file changed"), single-instance hand-off |
| **B. Feed** | Every message-like thing on the machine — chat messages, mail, system notifications — arrives on well-known topics with a fixed schema and is journaled | **UltraDesktop message centre**: all messengers and mail clients in one structured view |
| **C. Commands** | An app registers verbs with typed arguments; anyone may list and invoke them | Repeating tasks across apps (the AppleScript use case); UltraAI agents driving apps; the future **UltraScript** language, which is a client of this surface, not part of it |

Job B is the reason the module exists now. On ULTRA OS the desktop shell
should show, in one place and in a structured way, what Signal, Telegram,
WhatsApp, Thunderbird, UltraMail and the rest have to say. Each of those talks
to a *different* platform channel on each OS (§4). UltraMessage's job is to
normalise all of them onto the same topics so the desktop needs exactly one
subscriber.

### What it is not

- Not a network protocol. Everything here is local to one user session. Mail,
  Telegram, MQTT and the rest come in through **UltraNet** (and the app
  engines built on it); UltraMessage only redistributes what they fetched.
- Not a scripting language. UltraScript, when it comes, parses and runs
  scripts; the *verbs* it calls are UltraMessage commands (§6). Designing the
  channel first means the language later gets every app for free.
- Not a replacement for in-process callbacks. Widgets keep their `onXxx`
  callbacks; UltraMessage is for messages that cross a process boundary or
  that the desktop should see.

---

## 2. Vocabulary

| Term | Definition |
|---|---|
| **Message** | An envelope plus a payload. The envelope is fixed (§5.1); the payload is a `JSONValue` plus optional attachments. |
| **Topic** | Dotted, lower-case, hierarchical name that says what a message *is*: `messaging.message`, `mail.message`, `system.notification`, `app.command.invoke`. Subscriptions match patterns: `mail.*`, `*.message`, `#` (everything). |
| **Schema** | The documented JSON shape of one topic's payload (§5.3). Well-known topics ship with the module; apps may register their own under a vendor prefix (`com.example.myapp.*`). |
| **Endpoint** | One application's connection to the channel, identified by an **app id** (reverse-DNS, `org.ultraos.ultramail`) and an **instance id** (one per running process). |
| **Broker** | The per-user process that routes messages, keeps the endpoint directory, enforces delivery semantics and owns the journal. Hosted by the desktop shell on ULTRA OS; elected among running apps elsewhere (§7.1). |
| **Transport** | How an endpoint reaches the broker (§7). Same wire protocol on every platform. |
| **Adapter** | A bridge between a platform-native channel and UltraMessage topics — inbound (a native app's notification becomes a `system.notification`) or outbound (an UltraMessage command invokes a native app through Apple Events). |
| **Journal** | The persistent store of messages on topics marked *persistent* (§8): what the message centre lists, marks read, groups by conversation. |

---

## 3. Platform survey — what each OS already provides

Every desktop OS has two related things: a per-application event queue the OS
feeds, and one or more ways to route messages *between* applications. The
first is already wrapped by the UltraCanvas window backends. The second is
what UltraMessage abstracts.

### 3.1 App-to-app channels

| OS | Mechanism | Delivery model | Payload | Discovery | Notes |
|---|---|---|---|---|---|
| **RISC OS** | `Wimp_SendMessage` | Broadcast or to a task handle; *recorded* variant bounces if unacknowledged; reply by return | Word-aligned block ≤ 256 bytes, message number + data | Broadcast a message; whoever answers is there | The reference model. Everything the Wimp does, UltraMessage does. |
| **Windows** | `WM_COPYDATA`, `RegisterWindowMessage` + `HWND_BROADCAST`, `PostMessage`/`SendMessage` to another process's window | Synchronous (`SendMessage`) or queued; no reply channel beyond the return value | Arbitrary bytes (`COPYDATASTRUCT`) | Window enumeration / class name; broadcast a registered message | No broker. DDE is the legacy request/reply layer; COM/WinRT app services are the modern one, both heavy. Named pipes and mailslots for raw streams. |
| **Linux / ULTRA OS** | **D-Bus** session bus | Signals (broadcast), method calls (request/reply with errors), properties | Typed (`a{sv}` etc.), introspectable | Bus names (`org.freedesktop.Notifications`), `ListNames`, introspection XML | A real broker. GNOME, KDE, systemd, NetworkManager, MPRIS media players, notifications all speak it. X11 client messages and selections are the pre-D-Bus fallback. |
| **macOS** | Apple Events (`NSAppleEventManager`), XPC, `CFMessagePort`, `NSDistributedNotificationCenter`, Mach ports | Apple Events: request/reply with typed descriptors, the transport of AppleScript; XPC: request/reply to a service; distributed notifications: broadcast only, no reply | Apple Events: `NSAppleEventDescriptor`; XPC: property-list types; distributed notifications: a `userInfo` dictionary | Bundle identifier; `NSRunningApplication` | Several overlapping systems, none complete alone. Apple Events are the only way to *drive* a third-party app (Mail, Messages, Finder). Sandboxed apps need the Automation entitlement + user consent. |
| **Android** | Intents, Binder, broadcast receivers | Explicit or implicit target; broadcast; bound services for request/reply | `Bundle` (typed) | Intent filters in the manifest | System-brokered, permission-gated. |
| **WebAssembly** | `BroadcastChannel`, `postMessage`, service worker | Broadcast within one origin; request/reply by convention | Structured-clone JS values | Same-origin only | No cross-application channel at all; tabs of the same site only. |

### 3.2 Where messenger and mail notifications go

This is the row that matters for the desktop feed. A messenger does not
publish "a message arrived" on a bus; it *shows a notification*, and where
that notification goes is where the feed must listen.

| OS | Where every app's notifications go | Can a third party read them? | Category / structure available |
|---|---|---|---|
| **Linux / ULTRA OS** | The **`org.freedesktop.Notifications`** D-Bus service (the Desktop Notifications spec). Whoever owns that bus name *is* the notification server. | **Yes — by being the server.** The desktop shell implements the interface and receives every `Notify()` call: app name, icon, summary, body, actions, hints, expiry. Electron/Chromium wrappers (WhatsApp, Slack, Discord, Teams, Signal Desktop, Telegram Desktop, Thunderbird, Evolution) all call it. | The spec's `category` hint: `im.received`, `email.arrived`, `device.added`, … plus `desktop-entry`, `urgency`, `image-data`. |
| **Windows** | Windows Notification Platform (toasts in the Action Center) | **Yes** — `Windows.UI.Notifications.Management.UserNotificationListener` (Windows 10 1607+), after the user grants notification access in Settings. Returns app id, text elements, timestamp; the raw toast XML is not exposed. | App display name + text lines; no category. |
| **macOS** | Notification Center (the `usernoted` database) | **No public API.** Reading `~/Library/Group Containers/group.com.apple.usernoted/db2/db` needs Full Disk Access and breaks between releases. The sanctioned routes are **Apple Events / AppleScript to the source app** (`tell application "Mail" to get unread messages of inbox`; Messages exposes `chats` and `messages`; Messages' `chat.db` is readable with Full Disk Access) and per-app APIs. | Whatever the app's scripting dictionary exposes: rich for Mail and Messages, nothing for Electron apps. |
| **Android** | The notification shade | **Yes** — a `NotificationListenerService`, after the user grants it in Settings. This is how smartwatches and "notification history" apps work. | Package, title, text, `category` (`msg`, `email`, …), `MessagingStyle` with sender and conversation. |

**Conclusion.** On Linux and ULTRA OS the feed is *complete and cheap*: own
the notification service and every messenger reports in, with a category.
On Windows the listener API gives the same coverage with less structure. On
macOS there is no notification tap; the feed is built from source apps
directly, which is exactly where the earlier AppleScript investigation lands
(§9.3). Android has the cleanest structure of all through
`MessagingStyle`.

### 3.3 Existing pieces in this repository

| Need | Existing piece |
|---|---|
| Deliver work to the UI thread from any thread | `UltraCanvasApplication::PostToUIThread` (`UltraCanvasApplication.h`) |
| Fold a socket into the native event loop | `UltraCanvasApplication::AddFdWatch` (Linux `select()` path) |
| Hidden message-only window with its own pump (Windows) | `OS/MSWindows/UltraCanvasWindowsVolumeMonitor.cpp` — the `WM_COPYDATA` receiver is the same shape |
| Subscribe to an `NSWorkspace` notification centre (macOS) | `OS/MacOS/UltraCanvasMacOSVolumeMonitor.mm` |
| JSON payloads, no third-party type exposed | `UltraCanvasJSON` (`JSONValue`, `JSON::Parse` / `Serialize`) |
| Local persistent store with migrations | UltraDatabase (SQLite, `UltraDb_*`) |
| Tokens and passphrases for adapters | UltraVault |
| Mail fetching, Telegram, MQTT | UltraNet mail plugins (`IMailProtocolPlugin::FetchMessages`, `UltraNetMailMessage`), `Apps/UltraSocial/engine/connectors/`, `Plugins/UltraNet/mqtt` |
| Message-list UI | `UltraCanvasListView`, `UltraCanvasTreeView`, `UltraCanvasBadge`, `UltraCanvasChip`, `UltraCanvasTabbedContainer` (element catalogue) |
| Desktop-entry / bundle identity of a native app | `UltraCanvasDesktopEntry` (Linux), `UltraCanvasFileAssociations` (all) |
| GLib / GIO already on the Linux link line (SmartHome's BlueZ path) | `Docs/Dependencies.md` — GDBus needs nothing new |

---

## 4. Design decisions

1. **One broker, one wire protocol, native buses at the edges.** Rather than
   "D-Bus on Linux, WM_COPYDATA on Windows, XPC on macOS" as the *core*
   transport — three sets of semantics that never quite agree — UltraMessage
   runs its own per-user broker over a local stream socket on every platform
   (§7). App-to-app semantics (recorded delivery, bounce, reply, ordering) are
   therefore identical everywhere and testable on CI without a desktop. The
   native buses are reached through **adapters**, which is where they belong:
   they are how the outside world gets in and out.
2. **Topics carry schemas.** A message centre can only be "structured" if
   `messaging.message` means the same fields from Telegram, Signal, UltraMail
   and a Linux notification. The well-known topics (§5.3) are part of the
   module, versioned, and validated on publish in debug builds.
3. **Commands are messages.** A verb invocation is a `request` on
   `app.command.invoke` with a reply; a command's description is data an app
   registers, so a directory, a script, or an AI agent can list and call
   verbs without the app author writing anything beyond the handler. Apple
   Events got this right in 1991; D-Bus introspection is the same idea.
4. **The journal belongs to the broker, not to the apps.** Messengers come
   and go; the desktop needs yesterday's messages listed even when Telegram
   is not running. Persistent topics are written once, by the broker, into
   an UltraDatabase file, and queried through the same API.
5. **Wimp semantics, modern envelope.** Post (fire and forget), recorded post
   (acknowledge or bounce), request/reply with timeout, broadcast or
   targeted. Message numbers become topics; the 256-byte block becomes JSON
   with attachments.
6. **Delivery on the UI thread by default.** Every callback runs through
   `PostToUIThread` unless the subscriber asks for a worker thread. Apps
   never see a message on a socket thread by accident.
7. **Consent for commands, not for reading.** Subscribing to the feed is a
   local, same-user operation. *Invoking* another app's command is the
   AppleScript/Automation situation: the first invocation from app X on app
   Y asks the user once (§10).
8. **C-style API with opaque handles, like UltraNet and UltraDatabase.**
   `UltraMsg_*` free functions, `UltraMsgResult` for blocking calls,
   `UltraMsgHandle` for endpoints and subscriptions; a thin C++ convenience
   layer in `namespace UltraMessage`.

---

## 5. Data model

### 5.1 Envelope

```cpp
enum class UltraMsgKind { Notice, RecordedNotice, Request, Reply, Bounce };

struct UltraMsgSender {
    std::string appId;        // "org.ultraos.ultramail" (reverse DNS)
    std::string instanceId;   // one per process, broker-assigned
    int         processId;    // verified by the broker on connect (§10)
    std::string displayName;  // "UltraMail" — for the message centre
};

struct UltraMsgEnvelope {
    std::string   id;           // ULID: time-ordered, unique per broker
    UltraMsgKind  kind;
    std::string   topic;        // "messaging.message"
    UltraMsgSender from;
    std::string   to;           // "*" broadcast | appId | instanceId
    std::string   correlationId;// set on Reply / Bounce: the id being answered
    std::string   conversation; // optional: groups messages in the feed
    int64_t       timestampMs;  // wall clock, UTC
    int           ttlSeconds;   // 0 = no expiry
    uint32_t      flags;        // Persistent | Urgent | Silent | NoJournal | Replace
    std::string   replaces;     // id of a message this one supersedes
};

struct UltraMsgAttachment {
    std::string name;
    std::string mimeType;
    std::vector<uint8_t> bytes;  // ≤ 1 MiB inline …
    std::string filePath;        // … or a path in the per-user spool (§7.3)
};

struct UltraMsgMessage {
    UltraMsgEnvelope envelope;
    JSONValue        body;       // validated against the topic's schema
    std::vector<UltraMsgAttachment> attachments;
};
```

### 5.2 Wire format

Frames on the transport are a 4-byte little-endian length prefix followed by
one UTF-8 JSON object. Attachments over the inline cap travel as spool file
references, never inline. Numbers are written dot-decimal through
`UltraCanvasJSON`, never through locale-sensitive formatting.

```json
{ "v": 1,
  "id": "01J8ZQ9K3R7Q4E4X0V9WQ6N2C1",
  "kind": "notice",
  "topic": "messaging.message",
  "from": { "app": "org.ultraos.ultrasocial", "instance": "i-7f3a", "pid": 4123,
            "name": "UltraSocial" },
  "to": "*",
  "conversation": "telegram:-1001234567890",
  "time": 1758100000123,
  "flags": ["persistent"],
  "body": { "...": "see §5.3" },
  "attachments": [ { "name": "photo.jpg", "mime": "image/jpeg",
                     "path": "/run/user/1000/ultramessage/spool/01J8ZQ….jpg" } ] }
```

Control frames (`hello`, `subscribe`, `unsubscribe`, `ack`, `ping`) use the
same envelope with topics under `ultramessage.control.*` and are never
journaled or forwarded.

### 5.3 Well-known topics

Shipped with the module, versioned as `topic@N`; the feed and the message
centre are written against these and nothing else.

| Topic | Kind | Persistent | Body |
|---|---|---|---|
| `messaging.message` | Notice | yes | `service` (`telegram`, `signal`, `whatsapp`, `matrix`, `sms`, …), `account`, `conversation` `{id, title, isGroup}`, `sender` `{id, name, avatar?}`, `text`, `html?`, `direction` (`in`/`out`), `read` (bool), `externalId`, `replyTo?`, `attachments[]` `{name, mime, size, path?}` |
| `mail.message` | Notice | yes | `account`, `folder`, `from` `{name, address}`, `to[]`, `subject`, `snippet` (first ~200 chars, plain), `hasAttachments`, `read`, `flagged`, `externalId` (Message-ID or UID), `threadId?` |
| `system.notification` | Notice | yes | `appId`, `appName`, `category` (freedesktop vocabulary: `im.received`, `email.arrived`, `transfer.complete`, …), `summary`, `body`, `icon?`, `urgency` (`low`/`normal`/`critical`), `actions[]` `{id, label}`, `origin` (`freedesktop`, `windows-listener`, `android-listener`, `ultramessage`) |
| `system.notification.action` | Request | no | `notificationId`, `actionId` — the message centre invoking a notification's button; routed back to the adapter that owns the notification |
| `system.notification.dismissed` | Notice | no | `notificationId`, `reason` |
| `feed.read` / `feed.dismissed` | Notice | journaled as state, not as rows | `messageId` — the message centre telling the source (and the journal) the user handled it |
| `app.lifecycle.started` / `app.lifecycle.stopping` | Notice | no | `appId`, `instanceId`, `displayName`, `commands` (bool: registers commands) |
| `app.open.request` | Request | no | `paths[]` or `urls[]`, `activate` — single-instance hand-off; UltraFiler → UltraViewer |
| `app.command.list` | Request | no | reply: `commands[]` per §6.1 |
| `app.command.invoke` | Request | no | `verb`, `args` (object), `dryRun?` — reply: `result` (object) or error |
| `file.changed` | Notice | no | `path`, `change` (`created`/`modified`/`deleted`/`renamed`), `by` (appId) — what the Filer's folder watcher already knows, made public |
| `clipboard.changed` | Notice | no | `formats[]` |
| `ultramessage.control.*` | — | never | broker ↔ endpoint handshake, subscribe, ack, ping |

A vendor prefix (`com.example.myapp.*`) is free for apps; the broker accepts
any topic, validates only the well-known ones, and journals only topics whose
schema says *persistent* or whose sender set the `Persistent` flag.

---

## 6. Public API

Header `<UltraMessage/UltraMessage.h>`, sources under
`UltraCanvas/{include,core}/UltraMessage/`, platform backends under
`UltraCanvas/OS/<Platform>/UltraMessage*`, library target `UltraMessage`.

### 6.1 Types

```cpp
struct UltraMsgResult { bool ok; int code; std::string message; };
using  UltraMsgHandle = uint64_t;          // endpoint, subscription, command
using  UltraMsgCallback = std::function<void(const UltraMsgMessage&)>;
using  UltraMsgReplyCallback = std::function<void(const UltraMsgResult&, const UltraMsgMessage& reply)>;

struct UltraMsgConnectOptions {
    std::string appId;             // required, reverse DNS
    std::string displayName;
    std::string iconPath;
    bool deliverOnUIThread = true; // false: callbacks on the transport thread
    bool startBrokerIfAbsent = true;
    int  connectTimeoutMs = 2000;
};

struct UltraMsgCommandArgument {
    std::string name; std::string type;   // "string" | "integer" | "number" | "boolean" | "path" | "object" | "array"
    bool required = false; std::string description; JSONValue defaultValue;
};
struct UltraMsgCommandInfo {
    std::string verb;                     // "open", "export", "send", "search"
    std::string description;
    std::vector<UltraMsgCommandArgument> arguments;
    std::string returns;                  // free-text or schema name
    bool requiresConsent = true;
};
using UltraMsgCommandHandler =
    std::function<UltraMsgResult(const JSONValue& args, JSONValue& outResult)>;

struct UltraMsgQuery {
    std::vector<std::string> topics;      // patterns; empty = all persistent
    std::string conversation; std::string appId; std::string service;
    int64_t sinceMs = 0, untilMs = 0;
    bool unreadOnly = false;
    std::string textContains;
    int limit = 100, offset = 0;
};
```

### 6.2 Functions

Lifecycle and endpoint:

- `UltraMsg_Initialize`, `UltraMsg_Shutdown`, `UltraMsg_IsAvailable`,
  `UltraMsg_GetBrokerInfo` (host, transport, uptime, journal path)
- `UltraMsg_Connect(options) → UltraMsgHandle`, `UltraMsg_Disconnect`
- `UltraMsg_ListEndpoints` — who is on the bus (app id, instance, name, pid)
- `UltraMsg_ResolveApp(appId)` — instance ids for a running app, or none

Sending (the Wimp set):

- `UltraMsg_Post(endpoint, topic, body, options)` — fire and forget, to `*`
  or one target
- `UltraMsg_PostRecorded(endpoint, topic, body, options, onBounce)` — the
  broker delivers to every matching subscriber and expects an `ack` from at
  least one; with none within `ttl`, `onBounce` runs with the original
  message (Wimp `User_Message_Recorded` semantics)
- `UltraMsg_Request(endpoint, target, topic, body, timeoutMs, outReply)` —
  blocking request/reply, returns `UltraMsgResult`
- `UltraMsg_RequestAsync(endpoint, target, topic, body, timeoutMs, onReply)`
- `UltraMsg_Reply(endpoint, const UltraMsgMessage& request, body)` and
  `UltraMsg_ReplyError(endpoint, request, code, message)`
- `UltraMsg_Acknowledge(endpoint, message)` — explicit ack for recorded
  notices (automatic when the subscriber callback returns normally unless the
  subscription was created with `manualAck`)
- `UltraMsg_Attach(message, name, mimeType, bytes)` /
  `UltraMsg_AttachFile(message, path)` — inline or spooled (§7.3)

Receiving:

- `UltraMsg_Subscribe(endpoint, topicPattern, callback, options) → UltraMsgHandle`
  — options: `manualAck`, `includeOwn`, `onWorkerThread`, `replayJournal`
  (deliver persisted messages since a timestamp before live ones)
- `UltraMsg_Unsubscribe(subscription)`
- `UltraMsg_ProcessPending(endpoint)` — for apps without an UltraCanvas event
  loop (tools, tests): drain callbacks on the calling thread

Commands (job C):

- `UltraMsg_RegisterCommand(endpoint, info, handler) → UltraMsgHandle`,
  `UltraMsg_UnregisterCommand`
- `UltraMsg_ListCommands(appId, outCommands)` — introspection, works for
  running apps and, from their manifests (§6.4), for installed ones
- `UltraMsg_Invoke(endpoint, appId, verb, args, timeoutMs, outResult)`,
  `UltraMsg_InvokeAsync(...)` — launches the target if installed but not
  running when `options.launch` is set; consent per §10
- `UltraMsg_InvokeNative(endpoint, nativeTarget, verb, args, ...)` — routed
  to the platform command adapter (Apple Events on macOS, D-Bus method calls
  on Linux, COM automation later on Windows); the argument is a bundle id, a
  bus name, or a ProgID

Journal (job B):

- `UltraMsg_Query(query, outMessages)`, `UltraMsg_Count(query)`
- `UltraMsg_MarkRead(ids)`, `UltraMsg_MarkUnread`, `UltraMsg_Dismiss(ids)`,
  `UltraMsg_Delete(ids)`
- `UltraMsg_ListConversations(query, outConversations)` — grouped view: id,
  title, service, last message, unread count
- `UltraMsg_SetRetention(topicPattern, days, maxRows)`
- `UltraMsg_Export(query, path)` — JSON lines, for backup and for UltraAI

Schemas:

- `UltraMsg_RegisterSchema(topic, version, JSONValue schema)`,
  `UltraMsg_GetSchema`, `UltraMsg_Validate(message)`

Adapters (§9), broker side:

- `UltraMsg_ListAdapters`, `UltraMsg_EnableAdapter(name, bool)`,
  `UltraMsg_GetAdapterState(name)` — enabled / needs permission / running /
  error, with the platform's remedy text ("grant Notification access in
  Settings › Privacy") for the message centre to show

### 6.3 C++ convenience layer

```cpp
namespace UltraMessage {
    class Endpoint {              // RAII over UltraMsg_Connect/Disconnect
    public:
        static std::shared_ptr<Endpoint> Connect(const UltraMsgConnectOptions&);
        Subscription Subscribe(std::string_view pattern, UltraMsgCallback cb);
        void Post(std::string_view topic, JSONValue body);
        std::future<UltraMsgMessage> Request(std::string_view target,
                                             std::string_view topic, JSONValue body,
                                             std::chrono::milliseconds timeout);
        Command RegisterCommand(UltraMsgCommandInfo info, UltraMsgCommandHandler h);
    };
    // Typed helpers for the well-known topics, so no app builds the JSON by hand:
    JSONValue MakeMessagingMessage(const MessagingMessage&);
    bool      ParseMessagingMessage(const JSONValue&, MessagingMessage&);
    JSONValue MakeMailMessage(const MailMessage&);          // from UltraNetMailMessage
    JSONValue MakeSystemNotification(const SystemNotification&);
}
```

### 6.4 App manifest

An app that wants to be launched for a command, or listed before it runs,
declares itself in its existing packaging (`.desktop` on Linux — read by
`UltraCanvasDesktopEntry` — `Info.plist` on macOS, the registry on Windows)
with one key, `X-UltraMessage-AppId`, plus an optional
`ultramessage.json` next to the binary listing its commands. The broker
indexes those on start, the same way "Open with" indexes handlers today.

### 6.5 Usage sketch

```cpp
#include <UltraMessage/UltraMessage.h>
using namespace UltraMessage;

// UltraMail, after a sync fetched new mail:
auto bus = Endpoint::Connect({.appId = "org.ultraos.ultramail", .displayName = "UltraMail"});
for (const auto& m : newMail)
    bus->Post("mail.message", MakeMailMessage(m));      // journaled, feed shows it

// The desktop message centre:
auto feed = Endpoint::Connect({.appId = "org.ultraos.desktop", .displayName = "UltraDesktop"});
feed->Subscribe("messaging.message", [&](const UltraMsgMessage& msg) { AddToFeed(msg); });
feed->Subscribe("mail.message",      [&](const UltraMsgMessage& msg) { AddToFeed(msg); });
feed->Subscribe("system.notification",[&](const UltraMsgMessage& msg) { AddToFeed(msg); });

// A repeating task: every evening export today's chart from UltraViewer.
// (UltraScript will write this line; today it is C++.)
JSONValue out;
UltraMsg_Invoke(ep, "org.ultraos.ultraviewer", "export",
                JSON::MakeObject({{"path", "~/Reports/today.pdf"}, {"format", "pdf"}}),
                30000, out);
```

---

## 7. Transport and broker

### 7.1 One broker per user session

| Platform | Transport | Broker host | Endpoint identity check |
|---|---|---|---|
| Linux / ULTRA OS / BSD | Unix domain socket `$XDG_RUNTIME_DIR/ultramessage/bus.sock` | **ULTRA OS:** the desktop shell, always. **Other Linux:** the first app to connect and find no socket starts an in-process broker thread and takes a lock file; when it exits the next app re-elects (the journal is on disk, nothing is lost). | `SO_PEERCRED` → pid, uid; executable path from `/proc/<pid>/exe` |
| Windows | Named pipe `\\.\pipe\UltraMessage-<session-id>` | First app, as above, guarded by a named mutex `Local\UltraMessage` | `GetNamedPipeClientProcessId` → `QueryFullProcessImageName` |
| macOS | Unix domain socket under `$TMPDIR/ultramessage/` | First app, as above (flock) | `LOCAL_PEERCRED` / `getpeereid`; `NSRunningApplication` for the bundle id |
| WebAssembly | `BroadcastChannel("ultramessage")`, in-tab broker | Each tab; no journal (IndexedDB later) | Same origin |
| Android | Bound service in the UltraCanvas host app | The host app | Binder uid |

Election rather than a daemon keeps the module usable on a stock Linux
desktop, on Windows and on macOS without installing anything. ULTRA OS
pins the broker to the shell so that the notification service (§9.1) and the
journal have a fixed owner.

### 7.2 Threading

The broker runs on one worker thread with a poll loop. Endpoints run one
reader thread each; every callback is marshalled through
`UltraCanvasApplication::PostToUIThread` unless the subscription asked for
`onWorkerThread`. On Linux the endpoint socket may instead be registered with
`AddFdWatch`, which avoids the reader thread entirely inside an UltraCanvas
app. `UltraMsg_ProcessPending` serves programs without an event loop.

### 7.3 Attachments and the spool

Inline attachments are capped at 1 MiB. Larger ones are written by the sender
to a per-user spool directory (`$XDG_RUNTIME_DIR/ultramessage/spool`,
`%LOCALAPPDATA%\UltraMessage\spool`, `$TMPDIR/ultramessage/spool`) and passed
by path; the broker copies journaled attachments into the journal directory
and deletes spool files when their message expires. Images in the feed are
therefore never re-encoded and never duplicated per subscriber.

### 7.4 Delivery guarantees

- Ordering: per sender, in send order; across senders, by broker arrival.
- Notice: at most once per subscriber; a slow subscriber is buffered up to a
  bounded queue, then dropped with a `ultramessage.control.overflow` notice.
- RecordedNotice: at least one ack or a Bounce to the sender within `ttl`.
- Request: exactly one Reply or error (timeout, no such target, target
  disconnected) — never both, never neither.
- Persistent topics are written to the journal *before* fan-out, so a
  message the desktop displays is always one it can find again.

---

## 8. Journal

An UltraDatabase (SQLite) file per user:
`~/.local/share/ultramessage/journal.db` and the platform equivalents.
Schema, migrated through `UltraDb_Migrate`:

```
messages      (id TEXT PK, topic, kind, app_id, instance_id, conversation,
               time_ms INTEGER, ttl_s, flags, replaces, body_json TEXT,
               read INTEGER, dismissed INTEGER)
attachments   (message_id, name, mime, size, stored_path)
conversations (id TEXT PK, service, title, is_group, last_time_ms,
               unread_count, muted)
schemas       (topic, version, schema_json)
endpoints     (app_id, display_name, icon_path, manifest_path, last_seen_ms)
consents      (caller_app_id, target_app_id, verb, granted, time_ms)
```

Full-text search over `body_json`'s `text`/`subject`/`snippet` uses SQLite's
FTS5 so `UltraMsgQuery::textContains` is instant on years of history.
Retention defaults: 90 days or 50 000 rows per topic, configurable. The
journal file may be encrypted at rest through UltraCrypt with a key from
UltraVault (off by default; on for ULTRA OS profiles that opt in).

---

## 9. Adapters

Adapters are broker-side plugins under `UltraCanvas/OS/<Platform>/UltraMessage/`
(platform code) or `UltraCanvas/Plugins/UltraMessage/<name>/` (portable ones
built on UltraNet). Each one implements:

```cpp
class IUltraMessageAdapter {
public:
    virtual std::string Name() const = 0;              // "freedesktop-notifications"
    virtual UltraMsgAdapterState Start(IAdapterHost& host) = 0;
    virtual void Stop() = 0;
    // Outbound: the feed acted on something this adapter produced.
    virtual UltraMsgResult HandleAction(const UltraMsgMessage& action) { return {true}; }
    // Outbound commands to native apps (§6.2 UltraMsg_InvokeNative).
    virtual bool CanInvoke(const std::string& nativeTarget) const { return false; }
    virtual UltraMsgResult Invoke(const std::string& nativeTarget, const std::string& verb,
                                  const JSONValue& args, JSONValue& out) { return {false}; }
};
```

### 9.1 Linux / ULTRA OS

| Adapter | Direction | Mechanism | Produces |
|---|---|---|---|
| **freedesktop-notifications** | in | Own the `org.freedesktop.Notifications` name on the session bus (GDBus, GIO is already linked) and implement `Notify`, `CloseNotification`, `GetCapabilities`, `GetServerInformation`; emit `ActionInvoked` / `NotificationClosed` when the feed acts. On a desktop that already has a notification server (GNOME, KDE), fall back to **monitor mode** (`org.freedesktop.DBus.Monitoring.BecomeMonitor` on the session bus with a match on the interface) — read-only, no actions. | `system.notification`, with `category` when the app set it; `im.received` and `email.arrived` are additionally *mirrored* to `messaging.message` / `mail.message` with the fields that can be recovered (app, summary as sender or subject, body as text) so the feed groups them with first-class sources |
| **dbus-export** | out | Export each UltraMessage endpoint that registered commands as a D-Bus object `/org/ultraos/<app>` with a generated introspection XML; commands become methods | Lets `busctl`, `gdbus`, shell scripts and other desktops' automation drive UltraCanvas apps |
| **dbus-invoke** | out | `UltraMsg_InvokeNative("org.mpris.MediaPlayer2.vlc", "Pause", …)` → method call | Native apps as command targets |
| **mpris** | in | Subscribe to `org.mpris.MediaPlayer2.*` `PropertiesChanged` | `media.playing` (later topic) |

### 9.2 Windows

| Adapter | Direction | Mechanism | Produces |
|---|---|---|---|
| **windows-notification-listener** | in | `UserNotificationListener::Current().RequestAccessAsync()`, then `NotificationChanged` events; read `AppInfo.DisplayInfo`, `Notification.Visual` text elements, `CreationTime`. Needs Windows 10 1607+; a Win32 app must be packaged (MSIX with identity) or use the `Windows.UI.Notifications.Management` contract through an identity-less path (documented per-build; the adapter reports `needs-permission` with the exact Settings page) | `system.notification`; messenger apps are recognised by app id (`TelegramDesktop`, `Signal`, `Slack`, `Microsoft.Teams`, `WhatsApp`) and mirrored to `messaging.message` with `sender` = first text line |
| **copydata** | in/out | A message-only window per endpoint (`OS/MSWindows/UltraCanvasWindowsVolumeMonitor.cpp` pattern) accepting `WM_COPYDATA` frames in the wire format; a registered broadcast message `UltraMessage.Announce` for discovery by non-UltraCanvas programs | Lets AutoHotkey, PowerShell and native tools post to the bus |
| **com-automation** | out | Late-bound `IDispatch` (`Outlook.Application`, `Word.Application`) behind `UltraMsg_InvokeNative("Outlook.Application", "…")` — Phase 3 | Native apps as command targets |

### 9.3 macOS — where the AppleScript investigation lands

macOS has no tap on other apps' notifications, so the feed is assembled from
the source applications through the mechanisms Apple sanctions: **Apple
Events** (what AppleScript is made of). The earlier UltraScript / AppleScript
investigation was about repeating tasks *in* applications; that is job C,
and it is the same transport.

| Adapter | Direction | Mechanism | Produces |
|---|---|---|---|
| **apple-mail** | in | `NSAppleScript` (or `NSAppleEventDescriptor` built directly, avoiding script compilation) polling `tell application "Mail" to get {subject, sender, date received, read status, id} of messages of inbox whose date received > …` on a timer, plus the `Mail` "new mail" folder action when configured | `mail.message` |
| **apple-messages** | in | Messages.app's scripting dictionary is thin; the reliable route is reading `~/Library/Messages/chat.db` (SQLite, through UltraDatabase) — requires **Full Disk Access**, requested once with the remedy text shown in the message centre | `messaging.message` with `service` = `imessage` / `sms` |
| **apple-events-invoke** | out | `UltraMsg_InvokeNative("com.apple.mail", "send", {…})` → an Apple Event to the bundle id; verbs map onto the target's scripting dictionary (`sdef`), which the adapter reads with `OSACopyScriptingDefinition` so `UltraMsg_ListCommands("com.apple.mail")` returns the app's real dictionary. Needs the `com.apple.security.automation.apple-events` entitlement and `NSAppleEventsUsageDescription`; the OS asks the user once per target app | Native apps as command targets — the "repeating task" use case |
| **apple-events-export** | in | Register an `NSAppleEventManager` handler for a UltraCanvas suite and ship an `sdef` describing each app's registered commands, so **AppleScript and Shortcuts can drive UltraCanvas apps** (`tell application "UltraViewer" to export …`) | Symmetry with dbus-export |
| **distributed-notifications** | in/out | `NSDistributedNotificationCenter` for discovery announcements and for apps that post there (a few do) | `system.notification` (sparse) |

Electron messengers on macOS (Signal, Telegram Desktop, WhatsApp, Slack)
expose no dictionary and no readable store; they remain out of reach on macOS
until they are driven through their own web or bot APIs (UltraSocial's
Telegram connector, Matrix, Signal-CLI) — which is a portable adapter (§9.5),
not a macOS one.

### 9.4 Android (later)

`NotificationListenerService` in the UltraCanvas host app; `MessagingStyle`
notifications carry sender, conversation and per-message text, which map onto
`messaging.message` losslessly. This is the richest platform source and the
reason the schema carries `conversation` and `sender` as objects.

### 9.5 Portable adapters (UltraNet-based, any platform)

These make the feed independent of what the platform exposes, and they are
the only route for services whose desktop clients cannot be read:

| Adapter | Source | Notes |
|---|---|---|
| **ultramail** | UltraMail's own engine publishes `mail.message` after each sync | No adapter code: the app is a first-class endpoint |
| **telegram** | Bot API `getUpdates` long-poll (existing connector's token handling), later MTProto for a user account | Reuses `Apps/UltraSocial/engine/connectors/UltraSocialTelegramConnector` credentials via UltraVault |
| **matrix** | `/sync` long-poll over UltraNet HTTP | Covers Element and bridged networks |
| **imap-idle** | `IMailboxProtocolPlugin` with IDLE for accounts not in UltraMail | Same `mail.message` output |
| **mqtt** | `IMessagingProtocolPlugin::Subscribe` on a configured topic | Smart-home and IoT notices onto `system.notification` |
| **rss / sse** | UltraNet SSE stream or feed poll | `feed.item` (later topic) |

---

## 10. Security and privacy

- **Session scope.** The socket, pipe and journal are created `0600` /
  owner-only; a different user on the same machine sees nothing.
- **Verified sender identity.** The broker fills `UltraMsgSender::processId`
  and checks the executable path against the manifest that claimed the app
  id; an app cannot impersonate `org.ultraos.ultramail`. Mismatches are
  delivered with `from.verified = false` and the message centre shows them
  as such.
- **Consent for commands.** The first `UltraMsg_Invoke` from caller A on
  target B (per verb, or per app when the command is marked
  `requiresConsent = false` for the app) prompts the user through the
  broker's host; the answer is stored in `consents`. Native invocations
  additionally go through the platform's own prompt (macOS Automation,
  Windows UAC where COM elevation is involved).
- **Reading the feed is not privileged** beyond what the platform already
  gates (Full Disk Access, Notification access, `NotificationListenerService`
  approval). The adapter surfaces the platform's remedy and never works
  around it.
- **No credentials on the bus.** Adapters fetch tokens from UltraVault and
  never place them in a message; the schema validator rejects fields named
  `password`, `token`, `secret` on well-known topics in debug builds.
- **Journal hygiene.** Retention is enforced by the broker; `Delete` is real
  deletion including attachments; an opt-in at-rest encryption exists (§8).
- **Rate limits.** Per endpoint, per second, with `overflow` notices, so a
  misbehaving app cannot flood the desktop.

---

## 11. The desktop message centre (first UI consumer)

Not part of the module, but the reason for it. Built entirely from catalogue
elements: `UltraCanvasTreeView` (sources → conversations) beside an
`UltraCanvasListView` of message rows (avatar, sender, snippet, time, unread
badge), `UltraCanvasTabbedContainer` for *All / Chats / Mail / System*,
`UltraCanvasChip` filters per service, a detail pane, and per-row action
buttons that post `system.notification.action` or `app.command.invoke`
("Reply in Telegram", "Open in UltraMail"). It subscribes to three topics
and calls `UltraMsg_Query` on open; it never talks to a messenger directly.

A `UltraCanvasMessageCenter` composite element in `Plugins/` (Phase 2) lets
any app embed the same view — UltraMail's Toolbox tile bar, the ULTRA OS
launcher, or a DemoApp page.

---

## 12. Relationship to UltraScript and to AI agents

UltraScript is the ULTRA OS answer to AppleScript: a language for repeating
tasks across applications. This proposal deliberately builds the part of
AppleScript that is *not* the language — Apple Events — first:

| AppleScript concept | UltraMessage equivalent |
|---|---|
| Apple Event (class, id, parameters, reply) | `app.command.invoke` request with `verb`, `args`, and a reply |
| Scripting dictionary (`sdef`) | `UltraMsg_ListCommands` / `ultramessage.json` manifest |
| `tell application "X"` | `UltraMsg_Invoke(ep, "org.example.x", …)`, launching if needed |
| Automation consent | `consents` table + prompt (§10) |
| Folder actions / `on idle` handlers | A subscriber on `file.changed`, or a timer in the script runtime |
| `osascript` from the shell | `ultramsg` command-line tool (Phase 1): `ultramsg post`, `ultramsg invoke`, `ultramsg query`, `ultramsg tail` |

When UltraScript arrives it needs a parser, a runtime and bindings to these
six things — no per-app work. The same command surface is what an UltraAI
agent uses to act on the user's behalf (`Docs/AI-Agent-Integration.md`,
surface B): `UltraMsg_ListCommands` is a tool list, `UltraMsg_Invoke` is a
tool call, and the journal export is context.

---

## 13. Delivery plan

| Phase | Deliverable | Depends on |
|---|---|---|
| **0** | This document; `Masterfile_modules.md` §13 | — |
| **1 — Channel** | `UltraMessage` target: types, envelope, wire format, local-socket transport with broker election on Linux/macOS/Windows, `Connect`/`Post`/`PostRecorded`/`Request`/`Reply`/`Subscribe`, UI-thread delivery, `AddFdWatch` path on Linux, journal on UltraDatabase with FTS, `Query`/`MarkRead`/`Dismiss`, schema registry with the well-known topics, `ultramsg` CLI, `Tests/UltraMessage` (two-process tests through the CLI, single-process tests through an in-process broker) | UltraDatabase, UltraCanvasJSON |
| **2 — Feed** | Adapters: freedesktop-notifications (server + monitor mode), windows-notification-listener, apple-mail, ultramail (UltraMail publishes), telegram; `UltraCanvasMessageCenter` element; DemoApp page; `Docs/Modules/UltraMessage/README.md` | Phase 1, UltraNet, UltraVault |
| **3 — Commands** | `RegisterCommand`/`ListCommands`/`Invoke`, manifests, consent store and prompt, dbus-export + dbus-invoke, apple-events-invoke + apple-events-export (`sdef` generation), copydata; UltraFiler/UltraViewer/UltraMail register their first verbs (`open`, `export`, `send`) | Phase 1 |
| **4** | Android listener, WASM transport, apple-messages, matrix, imap-idle, com-automation; at-rest journal encryption; UltraScript begins on top | Phases 2–3 |

Phase 1 alone already replaces ad-hoc single-instance hand-off and gives
tests a way to script apps; Phase 2 is the desktop feed; Phase 3 is the
AppleScript-class automation.

---

## 14. Open questions

1. **Name.** `UltraMessage` with the `UltraMsg_` prefix (as `UltraDb_` for
   UltraDatabase). Alternatives considered: UltraBus (accurate, but "bus"
   suggests hardware to consumers), UltraChannel (collides with chat-channel
   vocabulary in the feed).
2. **Broker on non-ULTRA Linux.** Election among apps (proposed) versus a
   user-session service started by an autostart entry. Election needs no
   installation; a service survives the last app closing and can keep
   adapters (Telegram polling) alive while no UltraCanvas app runs. Both can
   coexist: the service is simply an always-running endpoint that wins the
   election.
3. **Should ULTRA OS's shell own `org.freedesktop.Notifications` outright?**
   Yes is proposed: it makes the feed complete and lets the message centre
   *be* the notification UI. It also means the shell must render toasts,
   which is new work outside this module.
4. **Mirroring notifications into `messaging.message`.** Recovering sender
   and conversation from a toast's summary/body is heuristic per app. The
   alternative is to show `system.notification` rows only for apps without a
   first-class adapter. Proposed: mirror only when `category` is
   `im.received` / `email.arrived`, keep the original `system.notification`
   too, and let the feed de-duplicate by `replaces`.
5. **macOS Messages via `chat.db`.** Full Disk Access is a large ask for a
   feed. Proposed: ship the adapter disabled, with the remedy text; never
   prompt unprompted.
6. **Windows listener packaging.** The listener API is easiest from a
   packaged (MSIX) app. Whether ULTRA apps on Windows ship packaged is a
   distribution decision outside this document; the adapter must report
   `needs-packaging` clearly if not.
7. **Inline attachment cap.** 1 MiB proposed; images from messengers are
   typically larger and go through the spool either way.
