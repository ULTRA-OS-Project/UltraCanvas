# UltraMessage — The Message Channel

**Status:** Phase 1 implemented (channel and journal); Phase 2 started —
adapter framework, the Linux `freedesktop-notifications` adapter and
UltraMail publishing to the feed. The rest of Phases 2–4 is in the proposal.
**Version:** 0.2.0
**Author:** UltraCanvas Framework / ULTRA OS
**Last Modified:** 2026-09-20

UltraMessage is the message channel of the ULTRA OS stack: one API through
which applications send structured messages to each other, receive what other
applications publish, and — with Phase 3 — expose commands others may invoke.
Its model is the RISC OS Wimp: a message is posted to everyone or to one
application, a *recorded* post bounces back when nobody acknowledged it, and a
request gets exactly one reply. A per-user **broker** routes everything and
keeps a **journal** of the persistent topics, which is what the ULTRA OS
desktop's message centre will list.

The research and the full design — the platform survey, the wire format, the
adapters that will feed messenger and mail notifications onto the bus, the
security model and the delivery plan — is the proposal,
`Docs/Research/UltraMessageDesignProposal.md`. This document is the reference
for what is built. The registry entry is `Masterfile_modules.md` §13.

**It is not a network protocol** (everything is local to one user session;
mail and messengers come in through UltraNet) and **not a scripting
language** (UltraScript is a separate module, §14, and a client of this one).

---

## 1. What is built

| Piece | Where |
|---|---|
| Public API (`UltraMsg_*`) | `UltraCanvas/include/UltraMessage/UltraMessage.h`, `UltraMessageTypes.h` |
| C++ layer and typed topic helpers | `UltraCanvas/include/UltraMessage/UltraMessageEndpoint.h` |
| UltraCanvas UI-thread bridge (header-only) | `UltraCanvas/include/UltraMessage/UltraMessageUltraCanvas.h` |
| Codec, identifiers, topic matching, default paths | `UltraCanvas/core/UltraMessage/UltraMessageCodec.cpp` |
| Schema registry and the well-known topics | `UltraCanvas/core/UltraMessage/UltraMessageSchemas.cpp` |
| Transport: Unix domain socket / Windows named pipe | `UltraCanvas/core/UltraMessage/UltraMessageTransport.cpp` |
| Broker: sessions, routing, bounce, request/reply, control RPC | `UltraCanvas/core/UltraMessage/UltraMessageBroker.cpp` |
| Journal on UltraDatabase | `UltraCanvas/core/UltraMessage/UltraMessageJournal.cpp` |
| Endpoint client and the API implementation | `UltraCanvas/core/UltraMessage/UltraMessageEndpoint.cpp` |
| Adapter interface (`IAdapter`, `IAdapterHost`) and registry | `UltraCanvas/core/UltraMessage/UltraMessageAdapter.h`, `UltraMessageAdapters.cpp` |
| `freedesktop-notifications` adapter (GDBus) | `UltraCanvas/OS/Linux/UltraMessage/UltraMessageFreedesktopNotifications.cpp` |
| UltraMail → `mail.message` | `Apps/UltraMail/engine/UltraMailFeedPublisher.{h,cpp}` |
| `ultramsg` command line | `Apps/UltraMessageCli/main.cpp` |
| Tests (32 cases; the adapter ones on a private D-Bus session) | `Tests/UltraMessage/` |

Library target `UltraMessage` (`libultramessage.a`), built whenever
UltraDatabase is (`ULTRACANVAS_ENABLE_ULTRAMESSAGE`, on by default). It links
UltraDatabase and nothing of the widget layer, so tools and tests stay
headless; `Tests/UltraMessage/CMakeLists.txt` also configures on its own
where the UI library cannot be built.

## 2. Ten-line tour

```cpp
#include <UltraMessage/UltraMessageEndpoint.h>
#include <UltraMessage/UltraMessageUltraCanvas.h>   // only in an UltraCanvas app
using namespace UltraMessage;

UltraMsg_UseUltraCanvasApplication();   // callbacks on the UI thread from now on

auto bus = Endpoint::Connect({.appId = "org.ultraos.ultramail", .displayName = "UltraMail"});
bus->Subscribe("app.open.request", [&](const UltraMsgMessage& m) { OpenPaths(m.body); });

MailMessage mail;                        // typed helper, see §5
mail.account = "me@example.org"; mail.from = {"Ada", "ada@example.org"}; mail.subject = "Hi";
bus->Post("mail.message", MakeMailMessage(mail));   // journaled: the feed shows it

UltraMsgMessage reply;                   // request / reply, 3 s timeout
bus->RequestBlocking("org.ultraos.ultraviewer", "app.command.invoke",
                     UltraCanvas::JSON::Parse(R"({"verb":"export","args":{"format":"pdf"}})"),
                     std::chrono::seconds(3), reply);
```

The first application to connect and find no broker hosts one in-process;
every later one connects to it. When the hosting process exits, the next
`Connect` elects a new host (a lock file beside the socket decides; no two
processes ever both win). On ULTRA OS the shell will host it permanently.

## 3. The API

Everything is a free function with the `UltraMsg_` prefix; blocking calls
return `UltraMsgResult` (`ok`, `code`, `message`; `explicit operator bool`),
endpoints and subscriptions are opaque `UltraMsgHandle`s.

### 3.1 Endpoint

| Call | Purpose |
|---|---|
| `UltraMsg_Connect(options, &error)` | Join the bus as `options.appId` (reverse DNS). Hosts a broker when none answers and `startBrokerIfAbsent` is set. `busPath` / `journalPath` override the platform defaults. |
| `UltraMsg_Disconnect(endpoint)` | Leave; every subscription of the endpoint ends. |
| `UltraMsg_IsConnected`, `UltraMsg_GetEndpointInfo`, `UltraMsg_GetBrokerInfo` | State: instance id, verified process id, whether the broker is in this process, its bus and journal paths. |
| `UltraMsg_ListEndpoints`, `UltraMsg_ResolveApp` | Who is on the bus; the instance ids of one application, first-connected first. |

### 3.2 Sending — the Wimp set

| Call | Semantics |
|---|---|
| `UltraMsg_Post(endpoint, topic, body, options)` | Fire and forget, to `*` or `options.to` (an app id or instance id). |
| `UltraMsg_PostRecorded(…, onBounce)` | The broker expects an acknowledgement from at least one subscriber within `ttlSeconds` (default 5); with none, `onBounce` runs with the original message. |
| `UltraMsg_Request(endpoint, target, topic, body, timeoutMs, outReply)` | Blocking request / reply. Fails with `NoSuchTarget`, `NotHandled` (the target has no subscription for the topic) or `Timeout`; an error reply carries its code and message. Never depends on the UI dispatcher, so it is safe from the UI thread. |
| `UltraMsg_RequestAsync(…, onReply)` | The same, with a callback. |
| `UltraMsg_Reply`, `UltraMsg_ReplyError` | Answer a request received through a subscription. Replies are not validated against the topic's schema. |
| `UltraMsg_Acknowledge` | Explicit acknowledgement for subscriptions created with `manualAck`; otherwise the return of the callback acknowledges. |

`UltraMsgSendOptions` carries `to`, `conversation` (the feed groups by it),
`ttlSeconds`, `flags` (`Persistent`, `Urgent`, `Silent`, `NoJournal`,
`Replace` + `replaces`), and inline `attachments` (≤ 1 MiB each; larger
payloads travel as a file path).

### 3.3 Receiving

`UltraMsg_Subscribe(endpoint, pattern, callback, options)` — patterns are an
exact topic, one wildcard segment (`mail.*`, `*.message`), a trailing `#`
(`app.#`) or `#` alone. Options: `manualAck`, `includeOwn` (also see what
this endpoint posts), `onWorkerThread` (bypass the UI dispatcher),
`replaySinceMs` (journaled history first, oldest to newest, then live).
`UltraMsg_Unsubscribe(subscription)` ends it.

**Threading.** Callbacks run through the dispatcher installed with
`UltraMsg_SetUIDispatcher`; an UltraCanvas application installs
`PostToUIThread` by calling `UltraMsg_UseUltraCanvasApplication()` once. With
no dispatcher, callbacks wait until `UltraMsg_ProcessPending()` drains them on
the caller's thread — what tools and tests do. A subscription with
`onWorkerThread`, or an endpoint connected with `deliverOnUIThread = false`,
runs its callbacks on the transport thread as frames arrive; such a callback
must not make a blocking call (`UltraMsg_Request`, journal queries) on the same
endpoint, because the thread it would wait on is the one it runs on. `Post`,
`Reply` and `Acknowledge` are fine there.

### 3.4 Journal

Served by the broker, which alone opens the database, so one process holds the
SQLite file at a time:

| Call | Purpose |
|---|---|
| `UltraMsg_Query(endpoint, query, out)` | Filter by topic patterns, conversation, sender app id, `service`, time range, unread, dismissed, `textContains`; newest first, `limit` / `offset`. |
| `UltraMsg_Count`, `UltraMsg_GetMessage` | The same filter as a count; one message by id. |
| `UltraMsg_MarkRead`, `UltraMsg_MarkUnread`, `UltraMsg_Dismiss`, `UltraMsg_Delete` | State changes by id list; dismissing also marks read; deletion is real. |
| `UltraMsg_ListConversations` | Conversation rows with unread and total counts, newest first. |
| `UltraMsg_SetRetention(pattern, days, maxRows)` | Per-pattern retention; the defaults are 90 days and 50 000 rows for everything. Applied when the broker starts and every ten minutes. |
| `UltraMsg_Export(query, path)` | JSON lines, one message per line. |

What is journaled: every `Notice` or `RecordedNotice` whose topic's schema says
*persistent* (`messaging.message`, `mail.message`, `system.notification`) or
whose sender set `UltraMsgFlag_Persistent`, unless it set `NoJournal`. The
broker writes before it fans out, so what a feed shows is always something it
can find again. Text search is a `LIKE` over the extracted text (`text`,
`subject` + `snippet`, `summary` + `body`) in this phase.

Default locations: bus `$XDG_RUNTIME_DIR/ultramessage/bus.sock` (Linux),
`$TMPDIR/ultramessage/bus.sock` (macOS), `\\.\pipe\UltraMessage-<session>`
(Windows); journal `~/.local/share/ultramessage/journal.db`,
`~/Library/Application Support/UltraMessage/journal.db`,
`%LOCALAPPDATA%\UltraMessage\journal.db`. Socket, lock file and directories
are owner-only.

### 3.5 Schemas and topics

`UltraMsg_RegisterSchema`, `UltraMsg_GetSchema`, `UltraMsg_ListSchemas`,
`UltraMsg_Validate`, `UltraMsg_IsPersistentTopic`, plus
`UltraMsg_TopicMatches`, `UltraMsg_IsValidTopic`, `UltraMsg_IsValidAppId`.
A schema names a topic or pattern, its fields with a type and whether they
are required, and whether the topic is persistent; `Post` refuses a body that
violates it. The well-known topics ship registered; applications add their
own under a vendor prefix (`com.example.myapp.*`). Their names are in
`namespace UltraMsgTopics`.

| Topic | Persistent | Body (see the proposal §5.3 for every field) |
|---|---|---|
| `messaging.message` | yes | `service`, `conversation{id,title,isGroup}`, `sender{id,name}`, `text`, `direction`, `read`, `attachments[]` |
| `mail.message` | yes | `account`, `folder`, `from{name,address}`, `to[]`, `subject`, `snippet`, `read`, `flagged` |
| `system.notification` | yes | `appName`, `category` (freedesktop: `im.received`, `email.arrived`, …), `summary`, `body`, `urgency`, `actions[]` |
| `system.notification.action` / `.dismissed`, `feed.read` / `feed.dismissed` | no | feed ↔ adapter signalling |
| `app.lifecycle.started` / `.stopping` | no | published by the broker for every endpoint |
| `app.open.request` | no | `paths[]` / `urls[]`, `activate` — single-instance hand-off |
| `app.command.list` / `.invoke` / `.echo` | no | the Phase 3 command surface; the topics exist, the consent and manifests do not yet |
| `file.changed`, `clipboard.changed` | no | |

### 3.6 Adapters

`UltraMsg_ListAdapters`, `UltraMsg_EnableAdapter`, `UltraMsg_GetAdapterState`.
An adapter is a broker-side plugin (`Internal::IAdapter` in
`core/UltraMessage/UltraMessageAdapter.h`) that bridges a platform channel onto
the bus: it publishes under its own identity
(`org.ultraos.ultramessage.adapter.<name>`, verified) through the host the
broker hands it, and receives back the `system.notification.action` /
`.dismissed` messages the feed posts about notifications it produced. The
broker starts every adapter of its build when it starts, stops them when it
stops, and keeps the on/off switch in the journal (`adapters` table), so
`UltraMsg_EnableAdapter(ep, name, false)` holds across restarts.
`UltraMsgAdapterInfo` lists name, description, platform, the switch and the
`UltraMsgAdapterState`: `status` (`disabled`, `starting`, `running`,
`needs-permission`, `unavailable`, `error`), a `message`, a `remedy` where the
user can do something, and a `mode` the adapter defines.

**`freedesktop-notifications`** (Linux, built where `gio-2.0` is found; the
`UltraMessage` target exports `ULTRAMESSAGE_HAVE_GIO` then). It owns
`org.freedesktop.Notifications` on the session bus and serves `Notify`,
`CloseNotification`, `GetCapabilities` and `GetServerInformation` — mode
`server`: every desktop application's toast becomes a `system.notification`
(`appName`, `appId` from the `desktop-entry` hint, `category`, `summary`,
`body`, `urgency`, `actions[]`, `origin: "freedesktop"`, plus `adapter`,
`nativeId` and `senderPid`); a `replaces_id` becomes a replace of the earlier
message; `CloseNotification` publishes `system.notification.dismissed`; a
`system.notification.action` from the feed raises `ActionInvoked` and
`NotificationClosed` towards the application. Where another server already
owns the name (GNOME Shell, Plasma, dunst) the adapter takes **monitor mode**
(`org.freedesktop.DBus.Monitoring.BecomeMonitor` on a private connection):
the same `Notify` calls are read passively (`origin: "freedesktop-monitor"`,
no `nativeId`), and actions are not available; when the bus refuses
monitoring the state is `needs-permission` with the remedy. Toasts with
category `im.received` are additionally mirrored to `messaging.message`
(service from the desktop entry or app name, conversation and sender from the
summary, text from the body), `email*` ones to `mail.message` (sender from the
summary, subject from the body's first line); each mirror carries `mirrorOf`
with the notification's id. Note for a session without any notification
daemon: the adapter then *is* the server and, until the message centre
renders toasts, nothing pops up on screen — the feed and `ultramsg tail` show
them; `ultramsg adapters disable freedesktop-notifications` hands the name
back.

**UltraMail** publishes new mail itself (`UltraMail::FeedPublisher`,
`Apps/UltraMail/engine/`): the sync workers pass every stored envelope to it,
and it posts a `mail.message` (endpoint `org.ultraos.ultramail`) for the ones
that are news — unread, not deleted or draft, dated within the last 7 days —
at most 100 per account per ten minutes so an initial sync never floods the
feed. Built with the engine wherever `UltraMessage` is (`ULTRAMAIL_HAVE_ULTRAMESSAGE`);
without it the publisher compiles to a no-op.

## 4. The C++ layer

`UltraMessage::Endpoint` (`Connect`, `Subscribe`, `Post`, `PostRecorded`,
`Request` returning a `std::future` completed on the transport thread,
`RequestBlocking`, `Reply`, `ReplyError`, `Acknowledge`, `Query`, `MarkRead`,
`Dismiss`) disconnects in its destructor; `UltraMessage::Subscription`
unsubscribes in its.

## 5. Typed helpers

`MessagingMessage`, `MailMessage` and `SystemNotification` with
`MakeMessagingMessage` / `ParseMessagingMessage` and their siblings build and
read the well-known bodies; `ConversationKey(m)` is the `<service>:<id>` the
feed groups by. A messenger adapter or UltraMail fills the struct and posts
the JSON; the message centre parses it back.

## 6. `ultramsg`

```
ultramsg post <topic> [json-body] [--to <app>] [--conversation <id>] [--persistent]
ultramsg tail [pattern] [--json]        follow the bus
ultramsg query [--topic <pattern>] [--unread] [--text <s>] [--service <s>] [--limit <n>] [--json]
ultramsg conversations | endpoints | info
ultramsg adapters [enable <name> | disable <name>]     the broker's adapters, their state, the switch
ultramsg mark-read <id>... | dismiss <id>... | delete <id>...
ultramsg export <path> [--topic <pattern>]
```

`--bus`, `--journal`, `--app` and `--no-broker` (fail rather than host a
broker) apply everywhere. `ultramsg tail` in one shell and `ultramsg post` in
another is the quickest two-process check of an installation.

## 7. Wire format and delivery guarantees

Frames are a 4-byte little-endian length and one UTF-8 JSON object with a
`t` field: `hello` / `welcome`, `ctl` / `ctlr` (control RPC: subscriptions,
directory, journal), `msg`, `deliver`, `ack`, `bounce`, `overflow`, `ping` /
`pong`, `bye`. The message envelope is the proposal's §5.2. The broker fills
`from` from the session that sent a message (an app cannot claim another's
identity) and marks it *verified* when the operating system's peer credentials
match the connecting process (Linux `SO_PEERCRED`, macOS `LOCAL_PEERPID`,
Windows `GetNamedPipeClientProcessId`).

- Per sender, messages arrive in send order.
- A notice is delivered at most once per subscription; a receiver whose
  outbound queue is full (4096 frames) loses messages and is told so with an
  `overflow` frame.
- A recorded notice is acknowledged by at least one receiver or bounced to
  its sender when the ttl passes; with no matching subscriber it bounces at
  once.
- A request gets exactly one reply or error — including when its target
  disconnects first.
- Persistent topics are journaled before fan-out.

## 8. Building and testing

```
cmake -B build                       # UltraMessage builds with UltraDatabase
cmake -B build -DULTRACANVAS_BUILD_ULTRAMESSAGE_TESTS=ON && cmake --build build --target UltraMessageTests
ctest --test-dir build -R UltraMessage

cmake -S Tests/UltraMessage -B build-um && cmake --build build-um && build-um/UltraMessageTests   # standalone
```

The suite hosts an in-process broker on a private bus path (`/tmp/ultramsg-test-<pid>/bus.sock`,
a per-process pipe name on Windows) and drives it over the real transport with
several endpoints; the journal is in memory. On Linux `test_main.cpp` also
starts a private `dbus-daemon --session` (its address goes to
`DBUS_SESSION_BUS_ADDRESS` for the test process only) so the adapter tests
drive the freedesktop adapter over real D-Bus — a client sending `Notify`, a
rival owning the name to force monitor mode — without touching the user's
bus; without `dbus-daemon` those tests skip. CI enables the suite on every
platform and runs it on Linux. The standalone tree takes UltraDatabase's
source list from `cmake/UltraDatabaseSources.cmake`, the same list the
in-tree build uses.

## 9. Not in this phase

- **Event-loop integration on Linux** through `AddFdWatch`: a reader thread
  serves every endpoint on every platform for now.
- **Reconnection** after the process hosting the broker exits: endpoints
  report `NotConnected`; the application reconnects. The ULTRA OS shell will
  host the broker permanently, which removes the case there.
- **FTS5** for text search.
- **The spool** for attachments over 1 MiB (a file path is passed instead).
- **The other Phase 2 adapters** (the Windows notification listener, Apple
  Mail, Telegram) and the `UltraCanvasMessageCenter` element; the adapter
  framework, `freedesktop-notifications` and UltraMail publishing are built
  (§3.6).
- **Commands** (Phase 3: `RegisterCommand` / `ListCommands` / `Invoke`,
  manifests, consent) — the `app.command.*` topics are reserved for them.
