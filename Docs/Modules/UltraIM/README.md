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

A sign-in that takes more than one round (Telegram: the phone number, the
code Telegram sends, the two-step password, a name for a new account)
answers `NeedsInput` until the last step. `UltraIMProviderInfo::authStep`
says what comes next (`PhoneNumber`, `Code`, `Password`, `Registration`) and
`authHint` how to say it (where the code went, the password's hint), and
`AuthStepChange` reports each change. Call `UltraIM_SignIn` again with that
one field set (`otpCode`, `password`, `displayName`); the rest of the
credentials need not be repeated. While a code is awaited,
`UltraIMProviderInfo::authResendHint` names the other way the service can
send it, as a button's text ("Send it by SMS instead"; empty when it offers
none), and `UltraIM_ResendAuthCode(provider, cb)` asks for it - the new hint
arrives as `AuthStepChange`. A step's `NeedsInput` answer always precedes
its `AuthStepChange`, so a `SignIn` made in answer to the event is never
resolved by the step that raised it.

A sign-in by a QR code (`credentials.loginType = UltraIMLoginType::QRCode`,
no phone number; Telegram) has the step `OtherDevice`: nothing to type, the
hint is a link (`tg://login?token=...`) to show as a QR code
(`UltraCanvasQRCode`) that an app already signed in scans, the service
replaces the link every half minute or so (`AuthStepChange`, the new link in
its text) and, once one is scanned, goes on by itself - `Password` when the
account has one, else signed in - so those steps arrive as events with no
`SignIn` pending. It can be asked for in place of a code that never came
(`SignIn` with `QRCode` while the step is `Code`); a phone number given
while the QR code waits starts the sign-in over with the number.

What one provider's service keeps for another provider's sign-in comes
through `UltraIM_FetchSharedCredentials(provider, forProvider, cb)`: ChatCube
Direct's relay keeps the Telegram application's API ID and hash
(`GET /v1/telegram-credentials`), so `UltraIM_FetchSharedCredentials("direct",
"telegram", ...)` answers a `UltraIMCredentials` with `apiId` and `apiHash` to
hand `UltraIM_SignIn("telegram", ...)` - set once on the server, never built
into the application or typed by the user. `NotFound` when the service keeps
none, `CapabilityMissing` from a provider without a service.

A message with a picture, a video, a file, a voice note or a sticker carries
it as `UltraIMMessage::attachment` (`UltraIMAttachment`): the kind in the
message's `type`, the caption in `text`, the file's name, size, dimensions
and length in the attachment. A provider fetches what a message shows by
itself - the photo, the preview picture (`thumbnailPath`) of a video or a
file - and says so with a `MessageUpdate` carrying the same message with
`localPath` / `thumbnailPath` set; what may be large waits to be asked for:
`UltraIM_DownloadAttachment(provider, conversation, message, cb)` fetches a
video, a file or a voice note to disk, answers through the callback with the
attachment (`localPath`, `isDownloaded`) - at once when it is there already -
and raises the `MessageUpdate` for everyone else listening. Sending:
`UltraIM_SendImage(provider, conversation, path, caption)`,
`UltraIM_SendVideo(..., caption)` and `UltraIM_SendFile(...)` take a local
file; the provider's `ImageMessages` and `FileTransfer` capabilities say
which it takes, the optimistic row and the `MessageDelivered` events work as
for a text, and the echo's attachment points at the file sent. People's
pictures come the same way, fetched by the provider: `avatarPath` on a
conversation, a contact and a message's sender (`senderAvatarPath`), a local
file or empty.

A file sent goes with a preview picture when the application can make one:
`UltraIM_SetPreviewMaker(maker)` installs the application's way of writing a
small JPEG of a picture or a video (ChatCube: pictures through the image
pipeline, videos through VideoFX when it is built in), and a provider asks
through `UltraIM_MakePreview(path, mimeType, maxSide, w, h)` - a JPEG under
the cache directory, or `""` - and sends it along (Telegram's
`inputThumbnail`, Matrix's `thumbnail_url` / `thumbnail_file`, ChatCube
Direct's inline preview), so the other side sees something before the file
has arrived. Without a maker, files go without one.

End-to-end encrypted conversations say so (`UltraIMConversation::isEncrypted`).
Their keys travel as a passphrase-protected text:
`UltraIM_ExportEncryptionKeys(provider, passphrase, cb)` hands it back, to
save through `UltraCanvasFileLoader::SaveFileDialog`, and
`UltraIM_ImportEncryptionKeys(provider, text, passphrase, cb)` takes one in
(the count of keys new to it in the callback). A provider without
encryption answers `CapabilityMissing`; the one with it lists
`UltraIMCapabilityFlags::E2EEncryption`.

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
- **For the Telegram provider:** `FetchSharedCredentials("telegram")` asks
  the relay for the Telegram application's API ID and hash
  (`GET /v1/telegram-credentials`, from the relay's
  `CHATCUBE_RELAY_TELEGRAM_API_ID` / `_HASH`); `NotFound` when it keeps none.
- **Files:** a picture, a video or a file goes out as sealed envelopes like
  a text - a `file` body (kind, name, type, size, the caption, the preview
  picture inline, under 24 KiB) and then the bytes in `chunk` bodies of 32
  KiB (base64, sealed; under the relay's 64 KiB envelope) - peer to peer
  over the link when it is up, else through the relay's queue, so a file
  reaches someone offline, and a group (a copy per member), with nothing
  added to the relay. The other side keeps the parts in its store until all
  are here, puts the file together under the cache directory, and says so
  with `MessageUpdate`; `DownloadAttachment` answers from disk, or once the
  last part is here. Files up to 64 MiB; our own row points at the file
  sent. Only the relay's text queue is involved, so a relay in service
  needs no change.
- **Not yet:** group calls, several devices per identity, groups without a
  relay, a file asked for later rather than pushed.

Tests: `Tests/UltraIM/DirectProviderTests.cpp` - the envelope and the store,
then two providers against a relay started from `Server/ChatCubeRelay` (P2P
delivery, a picture over the link, read receipt, offline queue - a text and
a file - sign-in again with the stored
identity, a group with a picture to it, a video call - pictures across, the camera turned off,
hung up - and voice calls: declined, answered with audio both ways, muted,
hung up, and a call to a contact who is offline). It needs a `python3` with the relay's packages on `PATH`
and skips without one, unless `ULTRAIM_TEST_RELAY_REQUIRED` is set (CI).
Then two providers with no relay at all: reached by address, the nearby
list, adding each other, a message peer to peer, a call, groups refused, one
side away and back (the waiting message arrives), a picture over the local
connection - and two that find each
other over mDNS alone, which skips without a responder unless
`ULTRAIM_TEST_MDNS_REQUIRED` is set (CI starts Avahi).

## Provider: Matrix

`UltraCanvas/Plugins/UltraIM/matrix/` (module `ultraim_matrix`, provider id
`matrix`) speaks the Matrix client-server API (v3) to any homeserver.

- **Sign-in:** `UltraIMLoginType::UsernamePassword` - `server` is a server
  name (`matrix.org`, found through `/.well-known/matrix/client`) or a URL,
  or empty with a full `@name:server` user; or `accessToken` instead of a
  password. The session (homeserver, user, device, access token) is kept in
  the provider's `DeviceKeyVault`; a later `SignIn` with no credentials
  resumes it, and a token the server no longer accepts raises `AuthRequire`.
  `SignOut` disconnects and keeps the session.
- **Rooms:** `/sync` as a long poll (30 s) on the provider's thread. A
  direct chat is a room in `m.direct` (or a nameless room of two); the rest
  are groups, titled by `m.room.name`, the alias, or their members. An
  invitation is a conversation whose snippet says so; sending into it
  accepts it. `AddContact("@name:server")` opens a direct chat (or returns
  the one there is); `CreateGroup` makes a private room and invites.
- **Messages:** text with replies (`replyTo`, the quoted fallback taken off),
  emotes, pictures, videos, audio and files with their attachments (the
  `filename` and the body as the caption, MSC2530), edits (`m.replace` -
  `MessageUpdate`), redactions (`MessageDelete`), joins and leaves as system
  lines. Our own message is sent under the client
  message id as its transaction id, so its echo matches the optimistic row;
  `Sent` when the server took it, `Read` when someone's read receipt covers
  it. Typing, presence of the other person in a direct chat, unread counts.
  `FetchMessages` pages older history back with `/messages`.
- **End-to-end encryption** (`m.olm.v1.curve25519-aes-sha2`,
  `m.megolm.v1.aes-sha2`): the cryptography is
  [vodozemac](https://github.com/matrix-org/vodozemac), Matrix's audited
  Rust implementation, behind a C ABI shim (`matrix/olm-c`, built with
  Corrosion; `ULTRAIM_MATRIX_E2EE`, on, needs cargo). `MatrixCrypto` does
  the bookkeeping around it:
  - this device's identity and one-time keys signed (canonical JSON) and
    published, topped up as the sync's count runs low, with a fallback key;
  - other users' device lists (`/keys/query`), their signatures checked and
    each device's keys **trusted on first use** - a device whose keys change
    gets no more room keys, and what it sends is not believed;
  - an Olm session per device (claimed one-time keys, signature checked), the
    room key going to every device of the room's joined and invited members
    as an `m.room_key` to-device message;
  - a Megolm session per room, replaced after the room's
    `rotation_period_msgs` (100) or `rotation_period_ms` (a week), and when
    someone or a device is gone from the room;
  - decryption with a replay guard (one event per message index), the room
    and the key's sender checked; a message whose key has not come is shown
    as *waiting for the key* and becomes the message (`MessageUpdate`) when it
    does - by to-device message or by an import.
  New direct chats and groups are created encrypted. Signing in again with
  the password reuses the device while its keys are here, so its rooms stay
  readable. Everything is kept in one internal file per account and device,
  `crypto-<hash>.store` next to the vault, sealed (XChaCha20-Poly1305) with
  a key the vault holds. The key export is the format Element and other
  clients read and write (PBKDF2-HMAC-SHA-512 with 500 000 rounds,
  AES-256-CTR, HMAC-SHA-256 - UltraCrypt's). Built without cargo, encrypted
  rooms say they cannot be read and new rooms are not encrypted.
- **Attachments** live in the content repository under `mxc://` URLs. A
  picture, and the preview picture (`thumbnail_url` / `thumbnail_file`) of
  a video or a file, are fetched as the message arrives - through the
  authenticated endpoint (`/_matrix/client/v1/media/download`, Matrix
  1.11), else the one before it - into the cache directory under the URL's
  hash, and reported with `MessageUpdate`; a video, an audio or a file when
  `DownloadAttachment` asks. In an encrypted room the repository holds
  ciphertext and the message carries the key (the `m.file` object: AES-256-CTR
  with a JWK key, the IV, the SHA-256 of the ciphertext, checked before
  decrypting). `SendImage`, `SendVideo` and `SendFile` upload a local file
  (`/_matrix/media/v3/upload`; encrypted first in an encrypted room, with a
  fresh key per file) and send the message with `filename`, the caption as
  the body and the type and size in `info`; the echo's attachment points at
  the file sent. The capabilities list `ImageMessages` and `FileTransfer`.
- **Not yet:** cross-signing and device verification (emoji / QR), key
  backup on the server, key requests between our own devices; calls; SSO
  sign-in.

Tests: `Tests/UltraIM/MatrixCryptoTests.cpp` without a server (canonical
JSON against the spec's examples, vodozemac through the shim, the store,
replay and room checks, export files); `Tests/UltraIM/MatrixProviderTests.cpp`
against a real Synapse, which `Tests/UltraIM/matrix_test_server.py` sets up
(config, open registration, the test users) with the python in
`ULTRAIM_MATRIX_PYTHON` - one with `matrix-synapse` installed; it skips
without one unless `ULTRAIM_TEST_MATRIX_REQUIRED` is set (CI). It sends a
picture, a video and a file each way and reads them back byte for byte -
through the encrypted format when vodozemac is built in, and once more
through a room created unencrypted (`SetCreateEncrypted(false)`), so both
paths run in one build. Its
encryption part checks the server holds only ciphertext, a second device
(keys from then on, earlier ones by import), signing in again on the same
device, and an encrypted room with
[matrix-nio](https://github.com/matrix-nio/matrix-nio)
(`matrix_nio_peer.py`), key export files read both ways - with
`matrix-nio[e2e]` in that python (`ULTRAIM_TEST_MATRIX_NIO_REQUIRED` in CI).

## Provider: Telegram

`UltraCanvas/Plugins/UltraIM/telegram/` (module `ultraim_telegram`, provider
id `telegram`) is a client of TDLib (Telegram Database Library), Telegram's
own client library, through its JSON interface (`td_create_client_id` /
`td_send` / `td_receive` / `td_execute`). **TDLib is loaded at run time, not linked** (`TdJson.h`): the
provider builds on every platform but WebAssembly (which loads no native
library) and works wherever a libtdjson is found - the
`tdlib/` folder next to the provider modules, `ULTRAIM_TDJSON_PATH`, or the
system's `libtdjson.so` / `.dylib` / `tdjson.dll` (TDLib 1.7 or later). The
one in `tdlib/` is put there by the build: `cmake/UltraCanvasTdlib.cmake`
fetches TDLib 1.8.69 prebuilt at configure time (the `tdjson` wheel on PyPI,
pinned by SHA-256; Linux x86_64 and aarch64, macOS arm64, Windows x64 - on
macOS and Windows with the OpenSSL and zlib libraries it needs beside it)
into `<build>/Plugins/UltraIM/tdlib/`, and the packages ship that folder.
`ULTRAIM_TDJSON_FILE` ships a libtdjson of your own instead,
`ULTRAIM_FETCH_TDLIB=OFF` skips it, and a platform with no prebuilt one
(macOS Intel, Windows ARM) says so at configure time and relies on a
libtdjson found at run time. One thread receives for every client in the
process.

- **Sign-in:** `UltraIMLoginType::PhoneOTP`, in steps (above): the phone
  number with its country code, the code, the two-step password when the
  account has one, a name for a new number. Telegram also needs the
  *application's* API ID and hash (my.telegram.org): `credentials.apiId` /
  `apiHash` - ChatCube takes them from its relay through
  `UltraIM_FetchSharedCredentials` (above) - or the ones kept with the last
  session, or built in (CMake `ULTRAIM_TELEGRAM_API_ID` /
  `ULTRAIM_TELEGRAM_API_HASH`), or `ULTRAIM_TELEGRAM_API_ID` / `_HASH` in the
  environment; a pair given wins over the kept one, so a change on the relay
  reaches a stored session. `server = "test"` uses
  Telegram's test servers. TDLib keeps the session, the chats and the history
  in its own database under the provider's data directory, encrypted with a
  key the vault holds; a later `SignIn` with nothing given resumes it.
- **The code:** Telegram sends it as a message to the Telegram apps the
  number is already signed in on whenever there are any (the "Telegram"
  service chat there), and by SMS or a call only when it offers that as the
  code info's `next_type` - for many third-party API IDs it offers nothing
  else. The hint says where it went; `authResendHint` and `ResendAuthCode`
  (TDLib `resendAuthenticationCode`) take the next way when there is one.
- **A QR code instead:** `loginType = QRCode` with no number asks TDLib for
  a sign-in link (`requestQrCodeAuthentication`) - the step `OtherDevice`
  (above), its hint the `tg://login` link to show as a QR code that the
  phone's Telegram app scans (Settings, Devices, Link Desktop Device); no
  code is sent at all, which is the way in when Telegram says it sent the
  code to the app and the app never shows it (so for third-party API IDs
  since early 2026). The link is refreshed as `AuthStepChange`; after the
  scan, the password step when the account has one, else signed in. Also
  from the `Code` step, in place of the code; a number given while the QR
  code waits starts over (TDLib takes none in that state). The link holds
  the sign-in token: the provider never logs it.
- **While it signs in**, TDLib's connection state is reported as
  `ConnectionStateChange` with `Connecting` and a text (waiting for the
  network, connecting to Telegram's servers, ...), and a step TDLib does not
  answer within 45 s (`SetStepTimeout` for tests) fails with `Timeout`
  saying what TDLib was doing, instead of leaving the application waiting;
  the step's state change still arrives as an event if it completes later.
- **Diagnostics:** the provider writes what it sends and receives (object
  types and TDLib's errors, never a phone number, code, password or message)
  to the framework's debug stream (`ULTRACANVAS_DEBUG_LOG`, ChatCube
  `--debug` / `--log`), and TDLib's own log goes to `telegram/tdlib.log` in
  the data directory (warnings and errors; more when the debug stream is on).
- **Chats:** the main chat list (`loadChats`, in TDLib's order, pinned first);
  private chats, basic groups and supergroups, channels. A conversation's id
  is the chat id, a message's the message id, a contact's the user id.
  `AddContact` takes `@username`, a `t.me/` link or `+phone`; `CreateGroup`
  makes a basic group of such members.
- **Messages:** text with replies, photos, files, audio, voice notes,
  videos (and animations, video notes) and stickers with their captions and
  attachments, the group's service messages as system lines; edits
  (`MessageUpdate`), deletion for everyone (`MessageDelete`); our own message
  reported `Sent` under its client message id once TDLib has sent it, then
  `Read` when the other side's read mark passes it; typing, presence, unread
  counts; `FetchMessages` pages back with `getChatHistory`.
- **Files** are TDLib's: it downloads them on request into `files/` under
  the provider's data directory (`downloadFile`, the arrival as
  `updateFile`). A photo (the size whose longer side is at least 800 pixels,
  else the largest) and the preview pictures of videos, documents and
  stickers are asked for as the message is kept, and reported with
  `MessageUpdate`; a video, a document, an audio or a voice note is fetched
  by `DownloadAttachment` (priority over the rest), which answers from disk
  when TDLib has it. Profile photos and chat photos (the small size) are
  fetched as the user or chat is seen: `avatarPath` on conversations,
  contacts and a message's sender. `SendImage` sends a local file as
  `inputMessagePhoto`, `SendVideo` as `inputMessageVideo` (with the caption),
  `SendFile` as `inputMessageDocument`; TDLib uploads it, the message goes out
  under the client id like a text, and the echo's attachment points at the
  file sent. A still sticker (WebP) arrives as a picture; an animated one
  (TGS, WebM) as its emoji.
  A video or a document sent goes with the preview picture the application
  makes (`inputThumbnail`).
- **Not yet:** secret chats (end-to-end encrypted; listed, marked encrypted,
  not opened), calls, e-mail sign-in, playing animated stickers, voice notes
  recorded here.

Tests: `Tests/UltraIM/TelegramProviderTests.cpp` against `FakeTdJson`, a
libtdjson stand-in that keeps a small Telegram in memory (several providers
in one process talk through it; files - profile photos, photos, videos and
documents sent, `downloadFile` - as paths, nothing read or written;
`FAKE_TDJSON_ACCOUNTS` and an `echobot`
account for trying ChatCube on it); `Tests/UltraIM/TelegramTdlibTests.cpp`
hands every request the provider builds to the real TDLib, offline - it
answers a request it cannot parse at once, so anything else means TDLib's
names and fields are right. The library is the one the configure step
fetched, or the one `ULTRAIM_TDJSON_PATH` names; CI sets
`ULTRAIM_TEST_TDLIB_REQUIRED`, so no library there is a failure.

## Status

Implemented: the registry, the module loader, the event bus and dispatcher,
the merged conversation fetch and every forwarding call; the ChatCube
Direct, Matrix and Telegram providers (above). Next: XMPP and others.
