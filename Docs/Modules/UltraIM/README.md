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
UltraIM_LoadUsedProviders();                // the networks in use (signed in to before)
UltraIM_LoadProvider("telegram");           // another one, when it is asked for

UltraIM_Subscribe([](const UltraIMEvent& e) {
    if (e.type == UltraIMEventType::MessageReceive) { /* append e.message */ }
    if (e.type == UltraIMEventType::MessageDelivered) { /* match e.message.clientMessageId */ }
    if (e.type == UltraIMEventType::TransferProgress) {
        // A file on its way: e.transferredBytes of e.totalBytes, e.isUpload
        // (ours going out) or not; the row is e.message.clientMessageId.
    }
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

A group after it is made (the `ManageGroups` capability):
`UltraIM_RenameGroup(provider, conversation, title, cb)`,
`UltraIM_AddGroupMembers(provider, conversation, {ids}, cb)` - both answer
the group as it is now - and `UltraIM_RemoveGroupMember(provider,
conversation, id, cb)`, which the network may allow to the group's owner
alone (`PermissionDenied`; `UltraIMConversation::ownerId` names the owner
where the network has one, `""` where it does not say;
`UltraIMConversation::memberNames` names the members it knows, and
`memberCount` counts everyone where `memberIds` lists only a first page). Every member hears
of the change as a `ConversationUpdate`. `UltraIM_LeaveConversation(provider,
conversation, cb)` (`LeaveConversation`) takes the local user out of a group,
or deletes a 1:1 conversation with its history on this device - the other
side keeps theirs - and a `ConversationRemove` follows. ChatCube offers all
of it from the chat list's right-click menu, as far as each network lists
the capability.

```cpp
UltraIM_AddGroupMembers("direct", groupId, {"3f2a9c01..."},
                        [](const UltraIMResult& r, const UltraIMConversation& group) {
    if (!r) { /* r.message says why */ }
});
UltraIM_LeaveConversation("direct", groupId, [](const UltraIMResult&) {});
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

A sign-in on the service's own web page (Mastodon: OAuth) has the step
`Browser`: nothing to type in the application, the hint
(`AuthStepChange`'s `errorText`) is the page's address for the application
to open in the browser (`UltraCanvas::OpenURL`). The person signs in there,
the page answers a loopback address the provider listens on, and the
sign-in goes on by itself; `SignIn`'s callback fires once it is done or
the wait ended.

What one provider's service keeps for another provider's sign-in comes
through `UltraIM_FetchSharedCredentials(provider, forProvider, cb)`: ChatCube
Direct's relay keeps the Telegram application's API ID and hash
(`GET /v1/telegram-credentials`), so `UltraIM_FetchSharedCredentials("direct",
"telegram", ...)` answers a `UltraIMCredentials` with `apiId` and `apiHash` to
hand `UltraIM_SignIn("telegram", ...)` - set once on the server, never built
into the application or typed by the user. `NotFound` when the service keeps
none, `CapabilityMissing` from a provider without a service.

**Deleting a message.** `UltraIM_DeleteMessage(provider, conversation, message, scope)`
deletes for everyone (`UltraIMDeleteScope::ForEveryone`, the default) or for
this account alone (`ForMe`); a `MessageDelete` event follows where the
message is gone. Who may do which, and until when, is each messenger's own
business, so an application does not decide it: it asks
`UltraIM_GetDeleteChoices(provider, conversation, message)` what is possible
for this message now and offers that - a delete outside it answers
`PermissionDenied`, a scope the messenger does not have `CapabilityMissing`.

```cpp
const UltraIMDeleteChoices choices = UltraIM_GetDeleteChoices("direct", message.conversationId, message.messageId);
if (choices.forMe)       { /* menu: Delete for me */ }
if (choices.forEveryone) { /* menu: Delete for everyone */ }
UltraIMResult r = UltraIM_DeleteMessage("direct", message.conversationId, message.messageId, UltraIMDeleteScope::ForMe);
```

| Provider | For me | For everyone |
|---|---|---|
| ChatCube Direct | any message, any time | the sender: a text until the other person has read it; a picture, video or file at any time; anything in a group at any time |
| Matrix | not available | the sender, any time (a redaction) |
| Telegram | any message | the sender (Telegram has the last word and may refuse) |
| Mastodon | not available | the sender, any time |

**Searching the history.** `UltraIM_SearchMessages(provider, search, cb)`
finds messages by their words, or everything of one kind - the
*Media / Files / Links* tabs of a conversation - for a provider with the
`SearchMessages` capability (all four have it):

```cpp
UltraIMMessageSearch search;
search.conversationId = conversation.conversationId;   // "" = every conversation (ChatCube Direct)
search.text = "lunch invoice";                         // every word, in any case; "" = everything of the kind
search.kind = UltraIMSearchKind::Files;                // AnyKind, Images, Videos, Media, Files, Links
search.limit = 50;
UltraIM_SearchMessages("direct", search, [](const UltraIMResult& result, const UltraIMSearchPage& page) {
    if (!result) return;                                // result.message says why
    for (const UltraIMMessage& message : page.messages) { /* newest first */ }
    // page.nextCursor: pass it as search.cursor for the older matches; "" = that was all.
});
```

The answer is a page (`UltraIMSearchPage`), newest first, with the cursor of
the next, older one. A page can hold fewer messages than `limit` - even
none - and still have a cursor: a provider that reads history from its
service as it searches stops after a stretch of it, and the cursor says
where to go on. Only an empty `nextCursor` means the beginning of the
history was reached. A cursor belongs to the search that produced it (the
same provider, conversation, text and kind).

Each provider answers from wherever its history lives, so one call serves
every network, and what differs is said here rather than discovered:

| Provider | Where it searches | Words match | Every conversation at once |
|---|---|---|---|
| ChatCube Direct | its database on this device | anywhere in the text, any case | yes (`conversationId` "") |
| Matrix | the room's history, read back page by page and matched on this device - so an encrypted room is searched like any other | anywhere in the text, any case | no: `CapabilityMissing` |
| Telegram | Telegram's own search (`searchChatMessages`) | whole words, as in Telegram's apps | no: `CapabilityMissing` |
| Mastodon | the chat's threads, read from the server and matched on this device | anywhere in the text, any case | no: `CapabilityMissing` |

The words are looked for in the message's text (a caption too) and its
attachment's file name; a `SystemEvent` line ("Bob joined") is never a
result. `Links` are messages whose text holds a link: `UltraIM_ExtractLinks(text)`
gives them (`http://`, `https://`, or `www.` at the start of a word, without
the full stop or bracket a sentence puts after), which is also how a list of
links gets the address to show for each result.
`UltraIM_MessageMatchesSearch(message, search)` is the matcher the
on-device providers share - use it to filter rows a list already holds, so
the list and a search agree - and `UltraIM_FoldCase` its case folding
(Latin, Greek and Cyrillic letters, one to one, never by the locale).

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

The signed-in account's own details are `UltraIMAccountProfile` - the name
its contacts see, and the e-mail address and phone number its service keeps
for the owner alone, to recover the account with:
`UltraIM_FetchAccountProfile(provider, cb)` and
`UltraIM_UpdateAccountProfile(provider, profile, cb)` (the
`AccountProfile` capability; `InvalidArgument` for an address or a number
that is plainly not one - the phone with `+` and the country code). A new
account's are `credentials.email` and `credentials.phoneNumber` with
`createAccount`.

How signing in on the device is protected is `UltraIMSignInProtection`
(the `SignInProtection` capability): `DeviceKey` (nothing to type),
`Password`, `AuthenticatorCode` (a TOTP code from an authenticator app -
UltraAuthenticator or any other) or `PasswordAndAuthenticatorCode`.
`UltraIM_GetSignInProtection(provider)` says which;
`UltraIM_StartAuthenticatorSetup(provider, link)` makes a new authenticator
secret and answers it as an `otpauth://` link to show as a QR code (it holds
the secret: show it, never log it); and
`UltraIM_SetSignInProtection(provider, change, cb)` changes it -
`UltraIMProtectionChange` carries the new protection, the current password
and code that prove the change (`AuthFailed` when one is wrong), the new
password (`""` keeps the current one) and the first code of a new
authenticator, which proves it was added. A protected sign-in goes in steps
as Telegram's does: `SignIn` answers `NeedsInput` (or `AuthFailed` for a
wrong one) with the provider's `authStep` `Password`
(`credentials.password`), then `Code` (`credentials.otpCode`), an
`AuthStepChange` event each, the hint in `authHint`.

**Confirming the e-mail and phone; account recovery** (the `AccountRecovery`
capability). `UltraIMAccountProfile` also says what the service says:
`emailVerified` (the link e-mailed there was opened) / `phoneVerified` (the
code texted there came back), `recoveryAvailable`, `phoneCodesAvailable`
and `recoveryEnabled`. A changed address gets a link that confirms it when
opened, a changed number a code: `UltraIM_VerifyContact(provider,
UltraIMContactChannel::Phone, code, cb)` sends the code back,
`UltraIM_ResendContactCode(provider, channel, cb)` asks for another link or
code. `UltraIM_SetRecoveryEnabled(provider, on, cb)` turns recovery on (the
address confirmed) or off. On a computer without the account,
`UltraIM_StartRecovery(provider, server, address, cb)` has a link e-mailed
to the account's address - the `UltraIMRecoveryChallenge` looks the same for
an address the service does not know - and
`UltraIM_FinishRecovery(provider, {server, recoveryId}, cb)` asks whether
the owner opened it and approved: `NeedsInput` while not yet (ask again in a
few seconds), `AuthFailed` once it expired, `Ok` with the account on this
computer; an ordinary `UltraIM_SignIn` follows. Only the computer that
started the recovery can finish it. A computer that has an account refuses
both.

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

`SearchMessages` (the `SearchMessages` capability) is answered from wherever
the provider's history is: a query on its own database, its service's
search, or - for any provider, with nothing but its `FetchMessages` -
`UltraIM_SearchByFetching(fetchPage, search, onResult)`, which reads one
conversation's history newest page first, keeps what
`UltraIM_MessageMatchesSearch` accepts, and stops when the page is full, the
history ends, or after about two thousand messages (the cursor then says
where to go on):

```cpp
class MyProvider : public IUltraIMProviderPlugin {
public:
    void SearchMessages(const UltraIMMessageSearch& search, UltraIMSearchCallback onResult) override {
        UltraIM_SearchByFetching(
            [this](const std::string& conversationId, const std::string& beforeMessageId, int limit,
                   UltraIMMessagesCallback done) { FetchMessages(conversationId, beforeMessageId, limit, done); },
            search, std::move(onResult));
    }
    // (the rest of the interface)
};
```

A provider ships as a module that links the core and exports two
functions - the version it was built for, and its entry point:

```cpp
ULTRAIM_DEFINE_PLUGIN_ABI_VERSION()     // UltraIM_PluginAbiVersion()

extern "C" ULTRAIM_PLUGIN_EXPORT void UltraIM_PluginInit(const UltraIMPluginHost* host) {
    if (!host || host->abiVersion != ULTRAIM_PLUGIN_HOST_ABI_VERSION || !host->RegisterProvider) return;
    host->RegisterProvider(std::make_shared<MyProvider>());
}
```

A module is named `ultraim_<provider id>` (`.so`, `.dll`, `.dylib`) - the
build's `OUTPUT_NAME` with no prefix - so a provider is found by its id
without loading anything. Modules are loaded **on demand**:

- `UltraIM_ListProviderModules()` - the ids of the modules in
  `pluginDirectory`, loaded or not (to offer them);
- `UltraIM_LoadProvider(id)` - that one module, when its network is asked
  for (a button, a sign-in); `ProviderNotFound` when there is none;
- `UltraIM_LoadUsedProviders()` - at start, the networks in use: those
  listed in `<dataDirectory>/ultraim-providers.txt`, one provider id a line
  (`#` a comment). UltraIM adds a provider there when it connects
  (`ConnectionStateChange` to `Connected`) and takes it out on
  `UltraIM_SignOut`; `UltraIM_GetUsedProviders()` reads it. With no list
  yet, a first start (the data folder empty) begins an empty one; a data
  folder used before - an application that loaded every module until now -
  has every module loaded once and those with a sign-in kept written down,
  so an update signs no one out;
- `UltraIM_RefreshProviders` - every module, as before.

A module is loaded once and never unloaded (queued callbacks may still point
into it); after `UltraIM_Shutdown` and a new `UltraIM_Initialize` it is asked
to register its provider again. A provider compiled into the application
uses `UltraIM_RegisterProvider` instead.

**The module ABI.** `ULTRAIM_PLUGIN_HOST_ABI_VERSION` covers everything a
module and the host share in binary form: the host table, the
`IUltraIMProviderPlugin` interface (its virtual functions and their order)
and every type in `UltraIMTypes.h`. The host asks a module for
`UltraIM_PluginAbiVersion()` before it calls anything else in it, and skips
one that answers another version - or does not answer, as modules built
before version 2 do (they also accepted any host) - saying on stderr to
rebuild it; the module refuses a host of another version too. So a stale
module in a plug-in folder is skipped instead of being called through a
vtable that no longer matches. Changing the interface or a type in
`UltraIMTypes.h` therefore bumps the version (with a word on what changed
in the comment above it), and `python3 scripts/check_plugin_abi.py
--update` records the new fingerprint in `scripts/plugin_abi.txt`; CI
(`plugin-abi.yml`) fails a change to those headers that leaves the version
as it was. A change that is not part of the contract - a free inline
function, a constant, the text of a default body's message - is recorded
with `--update --no-abi-change "why"`. Version 1 was the first modules;
version 2 (2026-11) added the account profile and the sign-in protection,
`UltraIMCredentials::email`, and the check itself (version 1 had grown
three interface methods with no bump); version 3 confirming the e-mail (by
a link) and the phone (by a code), and account recovery by an e-mailed link;
version 4 `SearchMessages` and its types; version 5 `DeleteMessage`'s
scope and `GetDeleteChoices`.

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
  empty: the relay URL is remembered). `email` and `phoneNumber` go with a
  new identity (both optional to the provider; ChatCube asks for both). The user id is 32 hex characters,
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
- **Groups** are kept by the relay (`/v1/groups`), which tells every member
  of a change (`group.update`). `RenameGroup` (relay 1.3.0 and later) and
  `AddGroupMembers` are for any member, `RemoveGroupMember` for the group's
  owner (`ownerId`); `LeaveConversation` leaves a group, and deletes a 1:1
  conversation and its messages on this device while keeping the contact,
  so a new message from them starts it again.
- **Public groups** (relay 1.5.0): a group's owner makes it public
  (`SetGroupPublic`, `UltraIMConversation::isPublic`), and it is listed for
  everyone on the relay (`FetchPublicGroups`: `UltraIMPublicGroup` - its
  conversation id, title, owner, member count, whether one is in it) and
  joined by anyone (`JoinGroup`). It stays end-to-end encrypted: a message
  is sealed for each member as in any group (the members' keys are fetched
  and checked against their ids as they join), so a public group holds at
  most 256 (the relay's setting). The relay limits how much one member posts
  into one.
- **A group's description and blocks** (relay 1.5.0): `SetGroupDescription`
  (`UltraIMConversation::description`, up to 500 characters; any member of a
  private group, the owner of a public one - else `PermissionDenied`), shown
  in the directory too (`UltraIMPublicGroup::description`). The owner blocks
  (`BlockGroupMember`: out of the group, and kept out - no *Join*, no one
  else adding them) and unblocks (`UnblockGroupMember`); `FetchBlockedMembers`
  lists them. `RemoveGroupMember` alone only takes someone out. A newcomer sees what
  is written after they join. On an older relay the three answer
  `CapabilityMissing` ("needs ChatCube relay 1.5 or later").
- **Local record:** contacts and their keys, conversations, history and the
  outbox in `<dataDirectory>/direct/direct.sqlite3` (UltraDatabase).
  `FetchMessages` answers from it; nothing waits for the network.
- **Deleting** (`DeleteMessage`, `GetDeleteChoices`): anyone deletes any
  message for themselves. For everyone, its sender deletes a text until
  the other person has read it, a picture, video or file at any time, and
  anything in a group at any time (a group has no read receipts). The
  message leaves the record with its note in the `links` table, the copies
  and file parts still in the outbox and the file it brought into the cache
  (never a file of the user's own that was sent from where it lies); the
  conversation's last line and unread count follow. The others are told
  with a `delete` body - sealed and signed like a message, over the link or
  through the relay's queue, so someone away hears of it when back. The
  receiving side holds the request to the same rules: only what the asker
  sent, and a text already read here stays here - reading that happened
  while the read receipt could not travel still counts. A message or a file
  deleted before all of it arrived is noted (`deleted`), and dropped when
  the rest comes.
- **Editing** (`EditMessage`): its sender changes a text, or a picture's,
  video's or file's caption, at any time (a text never to nothing - that
  is a delete). The record keeps the new words and marks the message
  edited (`isEdited`; schema 5); the others are told with an `edit` body on
  the message's path, and take it only from the message's own sender.
  `MessageUpdate` follows on both sides.
- **Replies:** `replyToMessageId` rides in the body as `reply_to`; the
  quote (`replyTo.senderName`, `textSnippet`) is filled in from the record
  where the message replied to is - on arrival, on `FetchMessages` and in
  search results.
- **Contacts:** adding someone puts us in their list too (relay 1.4.0
  `contact.added`, or a `contact` frame on the local network), so their
  chat appears before our first message.
- **Progress:** a file's parts are counted as they leave (the relay took
  them, or the link acknowledged them) and as they arrive, and reported as
  `TransferProgress` at each percent.
- **Search:** `SearchMessages` is a query on that record - one conversation
  or all of them, narrowed in the database by conversation and kind (an
  index each) and matched by words on the way out, where upper and lower
  case are known beyond ASCII. Which messages hold a link is noted in a
  table of its own, `links`, by the message's id and nothing else: the
  address stays in the message's text and is read out of it when shown, so
  it is never stored twice. The table is filled as messages are stored, and
  once for the history already there when a store from before it is opened.
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
- **Profile and sign-in protection.** The e-mail address and the phone
  number are kept in the identity's vault and sent to the relay
  (`PUT /v1/profile`) at every sign-in, which keeps them for the owner alone
  (`GET /v1/profile`; no other answer carries them). A password re-encrypts
  the vault under it (`DeviceKeyVault::ChangePassphrase`: Argon2id,
  XChaCha20-Poly1305) and drops the device key, so the identity's key
  cannot be read without it; the provider then opens the vault with
  `UseWithPassphrase` and keeps the password in memory while it runs, as it
  keeps the key. A typed password is stretched on the provider's worker
  thread. The authenticator's secret is kept in the vault too and checked
  with UltraCrypt's `VerifyTotp`: a step either side, each code once (the
  step accepted last is remembered), and after five wrong codes a pause of
  30 s that doubles with each further one up to 15 minutes, kept across
  starts. The code is a gate ChatCube keeps, not encryption: without a
  password, whoever can read the files and the device key can read the
  identity - so the strongest choice is both.
- **Recovery.** The relay e-mails the link that confirms the address and
  texts and checks the code that confirms the phone. Recovery on sends the identity's 64-byte secret key to the relay
  (`PUT /v1/recovery/escrow`), which keeps it encrypted with its own
  recovery key - so the relay operator can read it, which is the price the
  user agrees to; off deletes it. Recovery talks to the relay with a client
  of its own (there is no session yet): `POST /v1/recovery/start` answers a
  poll token the provider keeps, `POST /v1/recovery/poll` with it answers 202
  until the e-mailed link is approved and then the key. The provider checks
  that the key returned is the
  ID's (the user id is the hash of its public key), and stores it with the
  device key as if it had been made on that computer, with the relay, name,
  e-mail and phone - no password and no authenticator, which are per
  computer. The account's REST calls need the relay session, not its socket.
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
  sent. A voice note (`SendVoiceNote`) is such a file of kind `audio`; its
  length (`UltraIMSendOptions::durationSeconds`, for a video too) rides
  in the body as `ms`. Only the relay's text queue is involved, so a relay in service
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
  `RenameGroup` sets `m.room.name`, `AddGroupMembers` invites,
  `RemoveGroupMember` kicks (the homeserver's power levels decide whether
  we may: `PermissionDenied` when not), and `LeaveConversation` leaves the
  room and forgets it - a direct chat as much as a group.
- **Voice notes:** `SendVoiceNote` sends `m.audio` marked as a voice
  message (`org.matrix.msc3245.voice`, with the `org.matrix.msc1767.audio`
  block) and its length (`info.duration`, milliseconds - set for a video
  too when `UltraIMSendOptions::durationSeconds` says it).
- **Messages:** text with replies (`replyTo`, the quoted fallback taken off),
  emotes, pictures, videos, audio and files with their attachments (the
  `filename` and the body as the caption, MSC2530), edits (`m.replace` -
  `MessageUpdate`), redactions (`MessageDelete`), joins and leaves as system
  lines. Our own message is sent under the client
  message id as its transaction id, so its echo matches the optimistic row;
  `Sent` when the server took it, `Read` when someone's read receipt covers
  it. Typing, presence of the other person in a direct chat, unread counts.
  `FetchMessages` pages older history back with `/messages`.
- **Search:** one room at a time, by reading its history back (`/messages`,
  decrypted here) and matching on this device - the homeserver's own search
  cannot read an encrypted room and finds whole words only, so this gives
  one answer for every room. A long history is read a stretch at a time:
  a page with a cursor and few or no messages means "further back, ask
  again".
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
- **The proxy:** TDLib connects by itself - nothing of UltraNet's is
  between it and Telegram - so the provider tells it the proxy every
  application shares (`UltraCanvasProxySettings.h`; what
  `UltraNet_ResolveTunnelProxy` answers for `telegram.org` on port 443,
  where Telegram's servers are reached - the rule IRC and XMPP follow too):
  the proxy chosen in the settings, the operating system's SOCKS proxy or,
  where it names no other, its web proxy (by CONNECT), or none - and none
  for Telegram when the exceptions name `telegram.org`. A SOCKS 5 or an
  HTTP proxy goes to TDLib as `addProxy`
  with its sign-in, no proxy as `disableProxy`, and both **before**
  `setTdlibParameters`: TDLib holds them until the parameters are in, so its
  first connection is already the right one. While the session is open the
  setting is looked at again every three seconds (`SetProxyCheckInterval`
  for tests) and TDLib told when it has changed - it reconnects by itself,
  and the state reads `Reconnecting` with "Connecting to the proxy
  socks5h://127.0.0.1:10808" until it is through. TDLib keeps the proxy it
  uses in its own database (encrypted with the vault's key), password
  included, also one the proxy settings were told not to remember; every
  other proxy it kept is removed (`getProxies`, `removeProxy`) each time it
  is told one. TDLib has no SOCKS 4 and no proxy reached over TLS: with
  such a setting a sign-in fails at once with `Unsupported`, naming the
  proxy, and nothing is started; set while signed in - or when TDLib
  refuses the proxy it is given - Telegram is taken off the network
  (`setNetworkType` none, state `Error` saying why) rather than left to
  connect directly, and comes back by itself once the setting names a proxy
  it can use. `addProxy` is written in both forms it has had (`proxy` as an
  object, which TDLib 1.8.69 takes, and its fields beside `enable`, as
  before), since TDLib ignores the fields it does not know.
- **Diagnostics:** the provider writes what it sends and receives (object
  types and TDLib's errors, never a phone number, code, password or message)
  to the framework's debug stream (`ULTRACANVAS_DEBUG_LOG`, ChatCube
  `--debug` / `--log`), and TDLib's own log goes to `telegram/tdlib.log` in
  the data directory (warnings and errors; more when the debug stream is on).
- **Chats:** the main chat list (`loadChats`, in TDLib's order, pinned first);
  private chats, basic groups and supergroups, channels. A conversation's id
  is the chat id, a message's the message id, a contact's the user id.
  `AddContact` takes `@username`, a `t.me/` link or `+phone`; `CreateGroup`
  makes a basic group of such members. `RenameGroup` (`setChatTitle`),
  `AddGroupMembers` (`addChatMembers`, the same addresses; those whose
  privacy settings refuse it come back as `PermissionDenied`),
  `RemoveGroupMember` (`setChatMemberStatus` to left: a kick, also in a
  supergroup) and `LeaveConversation` (`leaveChat`; a private chat's history
  deleted for us alone with `deleteChatHistory`) - what an administrator
  may do in most groups answers `PermissionDenied` to anyone else. A basic
  group's members, their names and its creator come from its full info
  (`getBasicGroupFullInfo`, then `updateBasicGroupFullInfo`). A supergroup
  lists its 200 most recently active members (`getSupergroupMembers`) and
  its creator, with everyone counted in `UltraIMConversation::memberCount`;
  the page is asked for again when someone joins or leaves. A channel lists
  none (its subscribers are not members to manage).
- **Voice notes:** `SendVoiceNote` sends Telegram's own voice message
  (`inputMessageVoiceNote`, the length in whole seconds) for an Ogg Opus
  recording - the only kind its apps play as one; any other recording goes
  as a document.
- **Messages:** text with replies, photos, files, audio, voice notes,
  videos (and animations, video notes) and stickers with their captions and
  attachments, the group's service messages as system lines; edits
  (`MessageUpdate`), deletion for everyone (`MessageDelete`); our own message
  reported `Sent` under its client message id once TDLib has sent it, then
  `Read` when the other side's read mark passes it; typing, presence, unread
  counts; `FetchMessages` pages back with `getChatHistory`.
- **Search:** one chat at a time, by Telegram's own search
  (`searchChatMessages`, with its filters for photos, videos, documents and
  links): the whole history, without downloading it, and matched as
  Telegram's apps match - whole words.
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
account for trying ChatCube on it; the proxy requests kept per database
directory, `fakeProxyState` to read them); `Tests/UltraIM/TelegramTdlibTests.cpp`
hands every request the provider builds to the real TDLib, offline - it
answers a request it cannot parse at once, so anything else means TDLib's
names and fields are right - and holds the proxy requests to more than
that, since a wrongly shaped `addProxy` parses and adds nothing: a proxy
told before the parameters is the one in use after them, and is listed,
replaced, turned off and removed. The library is the one the configure step
fetched, or the one `ULTRAIM_TDJSON_PATH` names; CI sets
`ULTRAIM_TEST_TDLIB_REQUIRED`, so no library there is a failure.

## Provider: Mastodon

`UltraCanvas/Plugins/UltraIM/mastodon/` (module `ultraim_mastodon`, provider
id `mastodon`) brings the **direct messages** of a Mastodon server - or one
that speaks its REST API (GoToSocial, Akkoma, Pleroma) - over UltraNet. It
links nothing beyond UltraNet, UltraVault and UltraCrypt, so it builds
everywhere the others do.

Mastodon has no messenger of its own: a "chat" is a thread of statuses whose
visibility is `direct`, seen only by the accounts they mention, and the
server lists each thread as a conversation (`GET /api/v1/conversations`).
The provider turns that into chats:

- **A chat is a set of people.** Its id is the other accounts' full
  addresses (`user@server`, lower case, sorted, space-joined), so a chat
  opened before its first message (`AddContact`, `CreateGroup`) and the one
  its first message makes are the same, and every thread with the same
  people is one chat - a status that answers nothing starts a thread, and
  the history shows the threads together (the newest eight).
- **A message is a status:** its HTML content as text
  (`HTML::ExtractPlainText`, the layout `Lines`), the mentions it starts
  with left out (everyone in a direct chat is mentioned in each), a content
  warning above the text; its first attachment as the picture, video or
  sound (more than one: "+n" after the text). Sending posts a direct status
  that mentions everyone in the chat and answers the chat's last status, so
  the thread stays one; a reply answers the message quoted, and shows as a
  reply only when that is not simply the message before it. The client
  message id goes as the `Idempotency-Key`, so a retry never posts twice.
- **News comes by polling** the conversations every few seconds
  (`MastodonTimings::pollMs`, 4 s): a thread whose last status changed
  brings what it holds that was not seen yet, in order; a last status
  changed in place is an edit (`MessageUpdate`), one that went back to an
  older one a delete (`MessageDelete`). There is no push here (the
  streaming API is not used yet).
- **Sign-in** is the server's own page in the browser (`UltraIMLoginType::SsoOidc`,
  the auth step `Browser`): the provider registers the application with the
  server once (`POST /api/v1/apps`, kept per server), then runs OAuth with
  PKCE on a loopback address (`http://127.0.0.1:47819/callback`,
  `SetRedirectUri`). The access token is kept in UltraVault, so the next
  start signs in by itself (`SignIn` with no server); `credentials.accessToken`
  signs in with a token made in the server's settings instead.
- **People:** `AddContact` takes `@user@server`, `user@server`, `@user`
  (this server) or a profile's page (`https://server/@user`): the server
  looks them up (`/api/v1/accounts/lookup`), or asks theirs
  (`/api/v2/search` with `resolve`) for someone it has not met.
- **Media:** pictures, videos and sound, uploaded (`POST /api/v2/media`,
  `TransferProgress` as it goes), waited for while the server processes
  them, then sent; a picture received comes by itself, the rest when asked
  (`DownloadAttachment`). Mastodon takes no other file: `SendFile` with one
  answers `CapabilityMissing`.
- **Edit** (`PUT /api/v1/statuses/:id`, Mastodon 4.0+), **delete** (one's
  own, for everyone), **read** (`POST /api/v1/conversations/:id/read`),
  **leave** (the conversations taken off the list - the statuses stay).
- **Not here:** typing, read receipts, presence and calls (Mastodon has
  none); history beyond the eight newest threads of a chat.

Tests: `Tests/UltraIM/MastodonProviderTests.cpp` against
`mastodon_fake_server.py`, a small Mastodon in memory in standard-library
python (the part of the API the provider uses; threads as conversations,
OAuth that approves at once, media processed after one look): a token
sign-in and a refused one, the browser sign-in with PKCE and the session
kept, a chat opened before its first message, texts both ways, several
statuses between two looks in order, a reply, an edit, a delete seen by both
sides, a picture and a video byte for byte, a file refused, search, read, a
group, a second thread in the same chat, leaving. Skips without python3.

## Provider: Mattermost

`UltraCanvas/Plugins/UltraIM/mattermost/` (module `ultraim_mattermost`,
provider id `mattermost`) is a client of a Mattermost server's REST API
(v4) over UltraNet; like Mastodon it links only UltraNet, UltraVault and
UltraCrypt.

- **Sign-in** (`UltraIMLoginType::UsernamePassword`): `credentials.server`
  (a bare host is taken as https), `username` (the user name or the e-mail)
  and `password` (`POST /api/v4/users/login`; the session token comes in
  the `Token` header). An account with multi-factor authentication answers
  `NeedsInput` with the step `Code` (`authHint`: what to type) and an
  `AuthStepChange`; the next `SignIn` needs only `otpCode` - a wrong code
  answers `AuthFailed` and stays on the step. `accessToken` (a personal
  access token) signs in directly. The session is kept in UltraVault and
  resumed by `SignIn` with no credentials; a token the server no longer
  takes drops it (`AuthFailed` at the start, `AuthRequire` while connected).
  `FetchAccountProfile` answers the account's name (`Erin Smith (@erin)`).
- **Conversations** are channels, the id the channel's: a direct message
  (1:1, `directContactId` the other user), a group message (2-7 others), and
  the public and private channels of the user's teams as groups titled
  `Channel (Team)`, their creator as `ownerId`. Unread counts are the
  channel's messages minus those read.
- **News by polling** every few seconds (`MattermostTimings::pollMs`, 3 s):
  a channel whose last post moved, one open in the window, or one busy in
  the last 15 minutes is read with `posts?since=` - new posts in order
  (`MessageReceive`), edits (`MessageUpdate`), deletes (`MessageDelete`); a
  real server does not move a channel's time on an edit or a delete, hence
  the open and busy ones. System posts are left out.
- **Messages**: the post's markdown as it is; a thread reply has `replyTo`
  its root post (a reply to a reply goes into the root's thread). Files of
  any type, uploaded with `TransferProgress` and fetched with the token;
  pictures come by themselves. The client id goes as `pending_post_id`, so
  our own post is not received back.
- **Edit, delete** (one's own, for everyone), **read**, **people** -
  `AddContact` takes `@user`, `user`, an e-mail or a user id; `CreateGroup`
  2-7 people (a group message, its title the members') - **leave** (a
  channel; a direct or group message is hidden as Mattermost hides it and
  comes back with its next post), **search** one conversation at a time.
- **Typing**: `SetTyping(true)` posts `/api/v4/users/me/typing` (Mattermost
  has no "stopped"; `false` sends nothing). Typing is heard on a WebSocket
  (`/api/v4/websocket`) opened for that alone - polling stays as it was - and
  closed with the sign-out; it reconnects with a growing pause after a drop.
  Mattermost never says "stopped" either: the provider sends the stop itself
  `MattermostTimings::typingStopMs` (5 s) after the last word of it, when
  that person's post arrives, or when the socket drops. Needs Mattermost 5.26.
- **Not here**: presence, read receipts, calls, reactions; only a post's
  first file is its attachment.

Tests: `Tests/UltraIM/MattermostProviderTests.cpp` against
`mattermost_fake_server.py` (standard-library python: a team with two
channels, passwords, personal access tokens, an MFA user): every sign-in
path including MFA and a session the server ended, channels and direct
messages, several posts between two looks in order, thread replies, edit and
delete seen by the other side, a picture, a PDF and a voice note byte for
byte, search, unread and read, group messages, hiding and leaving. Skips
without python3 unless `ULTRAIM_TEST_MATTERMOST_REQUIRED` is set.

## Provider: Zulip

`UltraCanvas/Plugins/UltraIM/zulip/` (module `ultraim_zulip`, provider id
`zulip`) is a client of a Zulip organization's REST API over UltraNet.

- **Sign-in** (`UltraIMLoginType::UsernamePassword`): `credentials.server`
  (the organization, `chat.zulip.org`), `username` (the account's e-mail)
  and `password` (`POST /api/v1/fetch_api_key`), or `accessToken` = the
  account's API key with the e-mail in `username`; then HTTP Basic with the
  e-mail and the key. Kept in UltraVault and resumed by `SignIn` with no
  credentials. `FetchAccountProfile` answers the name and the e-mail.
- **Conversations**: a direct message `dm:<id>`, a group direct message
  `dm:<id>,<id>` (the others' user ids in order), and every topic of a
  subscribed stream as a group `stream:<stream id>:<topic>`, titled
  `#stream > topic`. The list is the streams' newest topics and the newest
  direct messages; unread counts those without the `read` flag.
- **News from the event queue** (`/api/v1/register`, then `/events`
  long-polled on a thread of its own): new messages, edits and deletes at
  once; a queue the server forgot is registered again, and a network error
  shows as Reconnecting.
- **Messages**: the raw markdown (`apply_markdown=false`). An upload's link
  (`/user_uploads/...`) is the attachment - of any type, sent with
  `TransferProgress`, fetched with the key; pictures come by themselves.
- **Edit, delete** (one's own, for everyone), **read** (the `read` flag),
  **people** (`AddContact` an e-mail or a user id, `CreateGroup` a group
  direct message), **leave** (a topic muted, a direct message hidden on this
  device), **search** one conversation at a time.
- **Typing**: `SetTyping` posts `/api/v1/typing` (a direct message, or a
  stream topic on Zulip 8.0 and later; an older server's refusal turns
  stream typing off for the session), and typing events come from the event
  queue. Zulip's own clients say "typing" only every ten seconds, so the
  provider repeats it every `ZulipTimings::typingRefreshMs` (4 s) until the
  stop, that person's message, or `typingExpireMs` (15 s) without a word.
- **Not here**: replies to a message (Zulip quotes instead), presence, read
  receipts, calls; moved or renamed topics. Needs Zulip 7.0 or later.

Tests: `Tests/UltraIM/ZulipProviderTests.cpp` against `zulip_fake_server.py`
(standard-library python, with a real long-poll): password and API-key
sign-in and the session kept, direct, group direct and stream-topic
messages both ways, edit and delete seen by the other side, uploads byte
for byte, read, search, a lost event queue registered again without losing
a message, leaving, and a shutdown that does not wait for an open long-poll.
Skips without python3 unless `ULTRAIM_TEST_ZULIP_REQUIRED` is set.

## Provider: Signal

`UltraCanvas/Plugins/UltraIM/signal/` (module `ultraim_signal`, provider id
`signal`) talks to [signal-cli](https://github.com/AsamK/signal-cli)
(GPL-3.0, built on Signal's own libsignal) running as a daemon with its HTTP
JSON-RPC interface: Signal has no API for other applications, and
signal-cli is a linked device of the user's phone, as Signal Desktop is.
signal-cli is not bundled.

- **The daemon**: `credentials.server` names one the user runs
  (`http://127.0.0.1:7583`; `signal-cli daemon --http 127.0.0.1:7583
  --receive-mode=manual`, no `-a`). Without one the provider starts its own
  (`SignalDaemon`): signal-cli is found from `ULTRAIM_SIGNAL_CLI`,
  `signal-cli/bin/signal-cli` beside the provider modules, or the `PATH`,
  started with its data under `<dataDirectory>/signal/signal-cli` on a free
  loopback port, and stopped at `Shutdown`. Neither: `CapabilityMissing`.
  The provider's own daemon gone while signed in (it ended, it was killed)
  is started again. When that fails - Java no longer there, say - the state
  is `Error` with what the start said (signal-cli's own lines of that
  start), told once for one reason; the tries go on behind it, each failure
  in a row doubling the wait before the next (`SignalTimings::retryMs`, up
  to a minute), and the state is `Connected` again once one works.
- **The proxy**: signal-cli connects to Signal by itself - nothing of
  UltraNet's is between it and Signal's servers - so the provider's own
  daemon is started for the proxy every application shares
  (`UltraCanvasProxySettings.h`; what `UltraNet_ResolveProxy` answers for
  `https://chat.signal.org:443/`), handed to it where signal-cli reads one:
  Java's proxy properties (`https.proxyHost` / `socksProxyHost` and their
  ports), which signal-cli 0.13.13 and later passes on to its network
  library as well. They go in `JAVA_TOOL_OPTIONS` in the daemon's
  environment, after what that variable already holds, so the setting wins
  over a proxy named there; options the environment gives signal-cli's own
  start script (`JAVA_OPTS`, `SIGNAL_CLI_OPTS`) come later still and are
  left alone. An HTTP proxy (it tunnels) or a SOCKS 5 one is passed, "no
  proxy" as options that take out one the environment names, and nothing
  when the setting is the operating system's and it names none for Signal
  (`DaemonProxy`, `DaemonProxyFor`). When the setting changes while signed
  in the daemon is started again for it within a receive cycle (seconds;
  the state reads `Reconnecting`, then `Connected`) - the link and the
  account are signal-cli's files and stay. Nothing is left to connect
  directly: a proxy signal-cli has no means for - one that asks for a user
  name and password (signal-cli passes none on), SOCKS 4, one reached over
  TLS - and a signal-cli that would not send everything through one - no
  Java runtime starts it (its native build: the runtime's `Picked up
  JAVA_TOOL_OPTIONS` line is missing from the daemon's log, so nothing
  shows the options were read), or its `version` call names one older than
  0.13.13 (`SignalCliTakesProxy`) - answer a sign-in with `Unsupported`,
  saying why, and while signed in stop the daemon with that as its `Error`
  state until the setting changes. With no proxy to use, either is started
  as before. A daemon the user
  runs (`credentials.server`) connects as the user started it - with a
  proxy, by the same Java options.
- **Sign-in** (`UltraIMLoginType::QRCode`): an account linked before and
  still in the daemon signs in at once. Otherwise `startLink` makes a
  `sgnl://linkdevice?...` link: `SignIn` answers `NeedsInput`, the step is
  `OtherDevice` and `GetAuthHint()` the link, shown as a QR code for the
  phone's Signal app (*Settings > Linked devices*). Once scanned the
  provider is Connected (`ConnectionStateChange`); a link that times out is
  replaced by a new one. `SignOut` keeps the link.
- **Conversations**: a 1:1 chat's id is the other person's number
  (`+4915112345678`), or their ACI when Signal gives none; a group's is
  `group:` + its id. A message's id is `<author>|<timestamp>`.
- **News by polling** `receive`: texts, attachments, replies (quotes),
  edits, remote deletes, delivery and read receipts, typing, and what the
  phone sends (sync copies, `isOwn`).
- **History**: signal-cli keeps none - `FetchMessages` and `SearchMessages`
  cover what this session received and sent.
- **People**: `AddContact` takes an international number or a Signal
  username (`name.01`); `CreateGroup` makes a group; `LeaveConversation`
  leaves a group or hides a 1:1 chat until its next message.
- **Not here**: calls, stickers, reactions; no upload progress (signal-cli
  tells none).

Tests: `Tests/UltraIM/SignalProviderTests.cpp` against
`signal_fake_daemon.py` (standard-library python speaking signal-cli's
JSON-RPC): no signal-cli (`CapabilityMissing`), linking by QR (a link that
expires and a new one), the link kept, texts both ways and one typed on the
phone, a reply, an edit, a remote delete, receipts, typing, a picture, a
file and a voice note byte for byte, history and search, a group made and
left, and a daemon the provider starts and stops itself; the proxy setting
as Java options for every kind of setting, the daemon started again when
the setting changes while signed in (another proxy, none) with the account
kept, stopped with the reason for a proxy signal-cli cannot use, and
stand-ins for the native build and for signal-cli 0.13.12 refused for a
proxy. Skips without python3 unless
`ULTRAIM_TEST_SIGNAL_REQUIRED` is set. The real signal-cli is not part of
the tests: that it takes its proxy from Java's properties is its own source
(`Utils.getHttpsProxy`, `SignalDependencies.setSignalNetworkProxy`).

`Tests/UltraIM/SignalDaemonTests.cpp` is `SignalDaemon` on its own - the one
part of the provider written per platform - and the Signal test that runs
on Windows: the fake daemon above is a shell script's child, so there the
whole of `SignalProviderTests` is skipped. The test is its own child program
(no python): started through `SignalDaemon::Start`, it reports the
environment and arguments it was given. Checked on every platform: the
variables handed to `Start` arrive exactly (Java options with spaces,
quotes, `|`, `&`, Thai), one the environment already has is replaced and
not doubled (on Windows whatever its spelling: names there have no case),
the rest is kept; arguments arrive exactly (a folder with spaces and a
trailing backslash, quotes, an empty one, Thai); standard output and error
are appended to a log named in Thai; the same through a wrapper script as
signal-cli is one (a `.bat` run by `cmd.exe` on Windows, a shell script
elsewhere, in a folder with a space and Thai in its name); `Stop` ends the
script *and* the program it started (the job object, the process group); a
program that ends by itself is let go of (`ProcessId()` 0), one that is not
there never shows as running; `FindSignalCli`, `JavaToolOptionsWith`,
`JvmTookOptions`.

CI runs the chat-network suites on Linux in the full pass, and since
2026-11 on the other rows too ("Run the chat-network tests
(macOS/Windows)" in `.github/workflows/build.yml`): `UltraIMTests`,
`MatrixCryptoTests`, `SignalDaemonTests` and `TelegramProviderTests` on
every Windows and macOS row; `TelegramTdlibTests` where the packages ship a
TDLib (Windows x64, macOS arm64 - no library there is a failure);
`DirectProviderTests`' envelopes and store on Windows; and the suites that
sign in to a python stand-in (Signal, IRC, XMPP, Mastodon, Mattermost,
Zulip) on the Apple silicon row, where no python3 or openssl is a failure.
Windows cannot run those (they fork), and the Intel macOS runner is too
slow for suites that wait on clocks. The stand-ins listen through
`Tests/UltraIM/local_http_server.py`: Python's own `HTTPServer` looks up the
name of `127.0.0.1` at every start, which stalls for fifteen seconds and
more on a macOS runner.

## Provider: XMPP

`UltraCanvas/Plugins/UltraIM/xmpp/` (module `ultraim_xmpp`, provider id
`xmpp`) is an XMPP (Jabber) client over XMPP's TCP binding (RFC 6120:
STARTTLS on 5222, direct TLS on 5223 - XEP-0368) or its WebSocket binding
(RFC 7395), through UltraNet (`UltraNet_TlsWrap` for TLS); the XML is
tinyxml2's, cut into elements from the TCP byte stream by `XmppSplitter`.

- **The server**, with `credentials.server` empty, is looked for in this
  order, the first that connects kept: the SRV records
  `_xmpps-client._tcp` (direct TLS) and `_xmpp-client._tcp` (STARTTLS) of the
  address's domain, by priority and weight; the WebSocket that host-meta
  announces (XEP-0156); `<domain>:5222`; last `wss://<domain>/xmpp-websocket`.
  `credentials.server` names it instead: `xmpp://host[:port]`, `host:port`
  or a bare host (STARTTLS, 5222), `xmpps://host[:port]` (direct TLS, 5223),
  `wss://...` (WebSocket), `https://...` (host-meta there). The certificate
  is checked against the address's domain; a server that offers no STARTTLS,
  and `ws://`, are refused unless they are on this computer. The endpoint
  that connected is kept with the keys and tried first next time.
  `SetTlsOptions` takes a CA bundle (tests). Over TCP a single space keeps a
  quiet connection open, and a server that has said nothing for
  `XmppTimings::pingIntervalMs` is pinged (XEP-0199): no answer, nor anything
  else, within `pingTimeoutMs` and the connection counts as gone and is made
  again - a server that hangs, or a path that drops packets without a word,
  is found this way, where a write never fails.
- **Sign-in** (`UltraIMLoginType::UsernamePassword`): the bare JID
  (`username`, `erin@example.org`) and the password, by SASL SCRAM-SHA-256,
  else SCRAM-SHA-1, else PLAIN (only over TLS or to this computer).
  UltraVault keeps SCRAM's ClientKey and ServerKey with their salt and
  iteration count - not the password - and `SignIn` with no credentials
  uses them; a server with PLAIN alone keeps nothing. A changed password:
  `AuthRequire`.
- **The proxy**: the TCP connection goes through the proxy the applications
  share (`UltraNet_ResolveTunnelProxy`): the one chosen in the settings,
  the operating system's SOCKS proxy, or - where it names no other - its
  web proxy, as a CONNECT tunnel with STARTTLS or TLS inside it; a proxy
  that refuses the tunnel fails the sign-in naming the proxy, it is not
  gone around. Never for this computer or an exception. The WebSocket
  binding takes the web's proxy as any web address does.
- **Conversations**: a 1:1 chat's id is the contact's bare JID, the
  contacts the roster (RFC 6121) with their presence; a group is a
  multi-user chat room (XEP-0045), its id the room's JID, the rooms the
  account's bookmarks (XEP-0402, else XEP-0048), joined at sign-in.
- **Messages**: ids by origin-id (XEP-0359), history from the server's
  archive (XEP-0313 MAM, paged with RSM); receipts (XEP-0184), displayed
  markers (XEP-0333, sent by `MarkRead`), chat states (XEP-0085), edits
  (XEP-0308), retractions (XEP-0424), replies (XEP-0461), carbons
  (XEP-0280), files by HTTP File Upload (XEP-0363).
- **Kept simple**: a subscription request is approved and asked back; a
  room invitation is accepted and bookmarked.
- **Not here**: OMEMO (messages are protected between each client and its
  server, not end to end), Jingle calls, stream management (a dropped
  connection signs in again).

Tests: `Tests/UltraIM/XmppProviderTests.cpp` against `xmpp_fake_server.py`
(standard-library python, a WebSocket XMPP server, and with `--cert` a TCP
one with STARTTLS and a direct-TLS one, sharing users and archives): the
splitter on awkward reads, server-field forms, SRV ordering, an untrusted
certificate refused, STARTTLS and WebSocket clients talking both ways,
direct TLS with the endpoint kept and resumed, a dropped TCP connection, a
quiet server pinged and kept, one fallen silent with its socket open found
by the unanswered ping;
SCRAM-SHA-256,
SCRAM-SHA-1 and PLAIN (nothing kept), plain `ws://` to another computer
refused, the WebSocket found from host-meta, the kept keys and a changed
password, 1:1 and room messages both ways, carbons, receipts, markers,
typing, a correction, a retraction, a reply, the archive paged and searched,
uploads with progress, a dropped connection reconnecting. Skips without
python3 unless `ULTRAIM_TEST_XMPP_REQUIRED` is set (the TLS parts without
openssl).

## Provider: IRC

`UltraCanvas/Plugins/UltraIM/irc/` (module `ultraim_irc`, provider id
`irc`) is an IRC client with IRCv3 over UltraNet TCP, with TLS by
`UltraNet_TlsWrap`.

- **Sign-in** (`UltraIMLoginType::UsernamePassword`): `credentials.server`
  (`irc.libera.chat`, `host:port` and `ircs://` are TLS, port 6697;
  `irc://host[:port]` plain, 6667), `username` the nickname, `password`
  optional - by SASL PLAIN (the account `displayName`, else the nick), only
  over TLS or to this computer; a server without SASL gets NickServ
  IDENTIFY, its echo kept out of the chats. A nickname in use becomes
  `nick_`, then `nick__` - `SignIn`'s `message` says so. Server, nick,
  password, channels and open private chats are kept in UltraVault;
  `SignIn` with no credentials joins them again. Silence pings after 90 s;
  a lost connection reconnects with a doubling delay (2 s to 120 s).
  `SetTlsOptions` takes a CA bundle or a client certificate. The connection
  goes through the proxy the applications share
  (`UltraNet_ResolveTunnelProxy`): the one chosen in the settings, the
  operating system's SOCKS proxy, or - where it names no other - its web
  proxy, as a CONNECT tunnel; a proxy that refuses the tunnel (one that
  lets only port 443 through, say) fails the sign-in naming the proxy, it
  is not gone around - an exception for the server in the proxy settings
  connects directly.
- **Conversations**: a channel's id is its name folded by the server's
  CASEMAPPING (`#ultracanvas`), its topic the `description`, its members
  nicks; a private chat's id the folded nick; `*server`, titled by the
  network's name, holds server and services notices and invitations, and
  text sent to it is a raw IRC command.
- **Messages**: the server's `msgid`, history by `CHATHISTORY` (else what
  this session saw); mIRC formatting removed, Latin-1 read when it is not
  UTF-8; `/me` as an ACTION; long messages split at 512 bytes (one
  `MessageUpdate` once every part has echoed); replies by `+draft/reply`,
  typing by `+typing`, echo-message and labeled-response matched; CTCP
  VERSION, PING, TIME, CLIENTINFO answered.
- **People**: `AddContact` takes `#chan` (with a key), `irc:`/`irc://`
  forms, or a nick (checked by WHOIS); `CreateGroup` joins
  `#<title>` and invites; `LeaveConversation` parts a channel or hides a
  private chat until its next message.
- **Not here**: files (`CapabilityMissing`: share a link), edits, deletes,
  read marks on the server, draft/multiline, regaining the wanted nick.

Tests: `Tests/UltraIM/IrcProviderTests.cpp` against `irc_fake_server.py`
(standard-library asyncio: a plain IRCv3 listener, a TLS one and a basic
one with no IRCv3): line parsing, case folding and splitting, TLS with the
test's CA file and an untrusted certificate refused, SASL, a wrong password,
anonymous sign-in, nicks in use, channels and private chats both ways,
echo-message, server-time, formatting, Latin-1, ACTION, a long message
split, a reply, typing, away, CHATHISTORY and paging, WHOIS, an invite-only
channel refused, the stored session resumed, a dropped connection rejoined
with what came meanwhile. Skips without python3 and openssl
unless `ULTRAIM_TEST_IRC_REQUIRED` is set.

## Status

Implemented: the registry, the module loader, the event bus and dispatcher,
the merged conversation fetch and every forwarding call; the ChatCube
Direct, Matrix, Telegram, Mastodon, Mattermost, Zulip, Signal, XMPP and IRC
providers (above).

The providers' `GetCapabilities()` (`UltraIMCapabilityFlags`), as they stand -
every one has TextMessages, AddContact, CreateGroup, GroupChats,
LeaveConversation, OfflineMessages and SearchMessages:

| Provider (module) | Files, images, voice notes | Edit, delete | Reply | Typing | Read receipts | E2E | Calls | Other |
|---|---|---|---|---|---|---|---|---|
| `direct` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | Voice, Video | PeerToPeer, ManageGroups, AccountProfile, SignInProtection, AccountRecovery, AnonymousLogin |
| `matrix` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | | ManageGroups, MultiDevice |
| `telegram` | ✓ | ✓ | ✓ | ✓ | ✓ | | | ManageGroups, MultiDevice, Channels, Stickers |
| `mastodon` | ✓ (pictures, video, sound) | ✓ | ✓ | | | | | MultiDevice |
| `mattermost` | ✓ | ✓ | ✓ | ✓ | | | | MultiDevice |
| `zulip` | ✓ | ✓ | | ✓ | | | | MultiDevice |
| `signal` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | | MultiDevice |
| `xmpp` | ✓ | ✓ | ✓ | ✓ | ✓ | | | MultiDevice |
| `irc` | | | ✓ | ✓ | | | | |

Modules are loaded on demand (*Writing a provider* above): an application
loads the networks in use at start and the others when they are asked for.
