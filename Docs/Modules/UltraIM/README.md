# UltraIM — instant messaging

One UI-free API through which an application signs in to messengers, lists
conversations, sends and receives messages and places calls, whatever the
network. Every network is a **provider**: ChatCube Direct (the ULTRA OS
peer-to-peer messenger) first, then Matrix, Telegram, XMPP and other
services with an open API. The first application on it is ChatCube.
Plan and decisions: [`Docs/Research/ChatCubeDirectPlan.md`](../../Research/ChatCubeDirectPlan.md).

| | |
|---|---|
| Headers | `<UltraIM/UltraIM.h>` (umbrella), `UltraIMTypes.h`, `UltraIMPlugins.h`, `UltraIMCore.h` |
| Sources | `UltraCanvas/core/UltraIM/` |
| Target | `UltraIM` (archive `uc-im`, absorbed into the shared core) |
| Option | `ULTRACANVAS_ENABLE_ULTRAIM` (ON) |
| Tests | `Tests/UltraIM` (`-DULTRACANVAS_BUILD_ULTRAIM_TESTS=ON`, binary `UltraIMTests`) |
| Depends on | UltraCrypt (client message ids); providers bring UltraNet / UltraRTC |

## Rules

- No UltraCanvas UI type in the module or in a provider: no element, window,
  list model or render context. The application turns events into UI.
- Networking only through UltraNet and UltraRTC.
- A returned `UltraIMResult` is a synchronous rejection only (not connected,
  capability missing, bad argument). What the network does arrives later as
  an `UltraIMEvent` or through the call's callback.
- **Threading:** every event and every callback goes through the dispatcher
  the application installs. A UI application installs one that posts to its
  UI thread, and then never needs a lock:

  ```cpp
  UltraIM_Initialize(config);
  UltraIM_SetEventDispatcher([](std::function<void()> task) {
      UltraCanvas::UltraCanvasApplication::GetCurrent()->PostToUIThread(std::move(task));
  });
  ```

  Without a dispatcher, callbacks run on whatever thread the provider
  finished on (tests, command-line tools). A task queued before
  `UltraIM_Shutdown` is dropped, never delivered after it.
- The manager never calls a provider while holding its lock, so a provider
  may raise events from inside any call.

## Using it

```cpp
UltraIMConfig config;
config.pluginDirectory = "<exe dir>/Plugins/UltraIM";
config.dataDirectory   = "<user data dir>/ChatCube";
UltraIM_Initialize(config);
UltraIM_RefreshProviders();                 // load the provider modules

UltraIM_Subscribe([](const UltraIMEvent& e) {
    if (e.type == UltraIMEventType::MessageReceive) { /* append e.message */ }
    if (e.type == UltraIMEventType::MessageDelivered) { /* match e.message.clientMessageId */ }
});

UltraIM_SignIn("direct", credentials, [](const UltraIMResult& r) { /* ... */ });
UltraIM_FetchConversations([](const UltraIMResult& r, const std::vector<UltraIMConversation>& list) {});

// Optimistic send: show the row at once with this id, Sending; the
// MessageDelivered event and the provider's echo carry the same id.
UltraIMSendOptions options;
options.clientMessageId = UltraIM_NewClientMessageId();
UltraIMResult r = UltraIM_SendText("direct", conversationId, text, options);
if (!r) { /* mark the row Failed */ }
```

Calls: ask `UltraIM_CanCall(provider, conversation, video)` before offering
the buttons — ChatCube Direct offers 1:1 voice and video calls while its
relay socket or a local connection to the contact is up, not group calls yet. `UltraIM_StartCall` gives the call id;
the other side gets `CallReceive` and answers with `UltraIM_AcceptCall` or
declines with `UltraIM_EndCall`. Both then follow `CallStateChange`:
`Ringing` → `Connecting` → `Active` → `Ended` (or `Failed`), the final
state's `errorText` saying why: `declined`, `busy`, `no answer`, `missed`,
`offline`, `crossed` (both called at once - the other call replaces it, and
its `CallReceive` follows), empty for a plain hang-up.
`UltraIM_SetCallMuted` mutes the microphone. One call at a time:
`StartCall` during another answers `InvalidState`.

A video call (`StartCall(..., true, ...)`; `CallReceive` with `isVideo`)
hands its pictures to the sinks `UltraIM_SetCallVideoSinks` installs - the
other side's and the self view - as RGBA on a media thread, not through the
dispatcher: push them into an `UltraCanvasVideoTile`, which takes frames from
any thread. Once the call returns, the sinks it replaced are not running, so
clear them (`nullptr`) before the tiles go. `UltraIM_AcceptCall(..., video)`
says whether our camera goes on; `UltraIM_SetCallCamera` turns it off and on
during the call, and the other side gets `CallMediaChange` (`isVideo` = the
camera is on), so it can show a caption instead of a frozen picture.

## Writing a provider

Implement `IUltraIMProviderPlugin` (`UltraIMPlugins.h`). Identity, the
session, `FetchConversations`, `FetchMessages` and `SendText` are required;
everything else has a default that answers `CapabilityMissing`. Raise events
through the sink `Initialize` hands you; every method must return at once.

A provider ships as a module that links the core and exports one function:

```cpp
extern "C" ULTRAIM_PLUGIN_EXPORT void UltraIM_PluginInit(const UltraIMPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterProvider) return;
    host->RegisterProvider(std::make_shared<MyProvider>());
}
```

`UltraIM_RefreshProviders` loads each module in `pluginDirectory` once and
never unloads it (queued callbacks may still point into it). A provider
compiled into the application uses `UltraIM_RegisterProvider` instead.

## Provider: ChatCube Direct

The ULTRA OS native messenger, provider id `direct`, module
`<build>/Plugins/UltraIM/ultraim_direct.{so,dll,dylib}`, sources
`UltraCanvas/Plugins/UltraIM/direct/`. Plan and protocol:
[`Docs/Research/ChatCubeDirectPlan.md`](../../Research/ChatCubeDirectPlan.md);
server: [`Server/ChatCubeRelay`](../../../Server/ChatCubeRelay/README.md).

- **Identity:** `UltraIMLoginType::KeyPair`. `SignIn` with
  `createAccount = true` and a `displayName` makes an Ed25519 key pair on the
  device, kept in the provider's `DeviceKeyVault` under
  `<dataDirectory>/direct/`; later sign-ins reuse it (`server` may then be
  empty: the relay URL is remembered). The user id is 32 hex characters,
  derived from the public key - it is what a contact types into
  `UltraIM_AddContact`.
- **Messages** are end-to-end encrypted envelopes (X25519 + HKDF +
  XChaCha20-Poly1305, header authenticated). Between two online contacts
  they go over a WebRTC data channel (UltraRTC) whose set-up is signed with
  the identity keys; otherwise, and for groups, through the relay's queue.
  Every message keeps its client id on every path, so one that arrived twice
  is shown once.
- **Delivery ticks:** `Sending` → `Sent` (in the relay's queue) →
  `Delivered` (the peer's device has it: data-channel ack or receipt) →
  `Read` (`UltraIM_MarkRead` on the other side). Groups stop at `Sent`.
- **Local record:** contacts and their keys, conversations, history and the
  outbox in `<dataDirectory>/direct/direct.sqlite3` (UltraDatabase).
  `FetchMessages` answers from it; nothing waits for the network.
- **Calls** (1:1, voice and video): the invite, accept, reject, camera
  update and hang-up are signalling messages signed like the data channel's
  set-up and carried by the relay socket (session `call:<uuid>`); the media
  is a separate UltraRTC peer per call with an Opus track and, for video, a
  VP8 track (640 x 480, 24 fps, 700 kbit/s - `SetCallVideoConfig`), the
  microphone, the speaker and the camera (`SetCallUseDevices(false)` leaves
  them closed - tests feed the tracks, `GetCallAudioTrack` /
  `GetCallVideoTrack`). An unanswered call ends after 45 s, one that does
  not connect fails after 20 s (`SetCallTimings`). A media connection that
  drops waits 1.5 s for the hang-up that may explain it (the hang-up comes
  through the relay, the close directly) before the call counts as lost.
- **The local network, with or without a relay.** `SignIn` with no relay
  at all (none given, none remembered) works on the local network only;
  `credentials.localNetwork` adds it next to a relay (both remembered). The
  provider listens on a free TCP port and announces itself over mDNS
  (`_chatcube._tcp`, TXT `id=` and `name=`; UltraNet's mDNS plug-in), browses
  for the others, and keeps a connection to every contact it sees there.
  Both ends of a connection prove their identity - each signs the other's
  fresh nonce, and the id must be the hash of the key - and it then carries
  the same signed signalling as the relay socket, so chat links and calls are
  set up over it unchanged (preferred over the relay when both are there).
  `UltraIM_FetchNearbyContacts` lists the ChatCubes nearby that are not
  contacts yet (`NearbyChange` when that changes); `AddContact` on one
  connects and finishes once the handshake has proven its key. A contact is
  online while the relay says so or its local connection is up. Without a
  relay a message waits in the outbox until the other side is back, and
  groups (which the relay keeps) are refused. `AddLanAddress(host, port)`
  reaches a ChatCube by address where multicast does not get through.
- **Not yet:** group calls, files and images, several devices per identity,
  groups without a relay.

Tests: `Tests/UltraIM/DirectProviderTests.cpp` - the envelope and the store,
then two providers against a relay started from `Server/ChatCubeRelay` (P2P
delivery, read receipt, offline queue, sign-in again with the stored
identity, a group, a video call - pictures across, the camera turned off,
hung up - and voice calls: declined, answered with audio both ways, muted,
hung up, and a call to a contact who is offline). It needs a `python3` with the relay's packages on `PATH`
and skips without one, unless `ULTRAIM_TEST_RELAY_REQUIRED` is set (CI).
Then two providers with no relay at all: reached by address, the nearby
list, adding each other, a message peer to peer, a call, groups refused, one
side away and back (the waiting message arrives) - and two that find each
other over mDNS alone, which skips without a responder unless
`ULTRAIM_TEST_MDNS_REQUIRED` is set (CI starts Avahi).

## Status

Implemented: the registry, the module loader, the event bus and dispatcher,
the merged conversation fetch and every forwarding call; the ChatCube
Direct provider (above). Next: the ChatCube application on top of it.
