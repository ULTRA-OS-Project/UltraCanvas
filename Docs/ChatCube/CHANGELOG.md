#### 2026-10-27 *0.1.6*
- **A new look.** The sign-in page is a card under the ChatCube logo; the
  sidebar has round avatars with a presence dot, a soft green selection, a
  pill for adding a contact and three icon buttons (Group, Matrix, Telegram)
  in place of the full-width ones; the open conversation shows its avatar in
  the header beside round call buttons; the composer is a rounded field with
  the send button inside it; the bubbles, the date separators and the call
  bar follow the same palette. Every colour comes from one theme
  (`ui/ChatCubeTheme.*`), the ChatCube green on warm neutrals, in a light
  and a dark version: `ChatCube --dark`.

#### 2026-10-27 *0.1.5*
- **The Telegram form says where the code went.** Telegram sends the
  sign-in code as a message to the Telegram apps your number is already
  signed in on, not by SMS, and the status line's one-line hint was easy to
  miss; the form itself now says so above the code field ("Telegram sent the
  5-digit code as a message to the Telegram apps where +… is signed in -
  type it here"), and when Telegram offers another way - SMS, a call - a
  button under the field asks for it.

#### 2026-10-25 *0.1.4*
- **"Send me the code" no longer stays grey forever.** When Telegram could
  not be reached, the button went grey, nothing was said, and nothing ever
  came back. The status line now says what Telegram is doing while you wait
  (starting, waiting for the network, connecting to Telegram's servers), a
  step Telegram does not answer within 45 seconds says so and gives the
  button back, and `ChatCube --debug` (or `--log <file>`) prints what the
  networks do - the relay's answers, every object sent to and received from
  TDLib, never your number, code or password. TDLib's own log is
  `telegram/tdlib.log` in the data folder.
- **The status line keeps one entry per network.** *ChatCube: connected ·
  Matrix: signed in as @you:matrix.org · Telegram: waiting for the network…*
  sits beside what your last action said, so the relay reconnecting no
  longer wipes out what Telegram or Matrix was saying.
- **The Telegram application's API ID and hash come from your relay.** Set
  `CHATCUBE_RELAY_TELEGRAM_API_ID` / `_HASH` in the relay's `.env` and
  ChatCube takes them after it signs in to the relay; the two fields in the
  Telegram form appear only when the relay has none, or when there is no
  relay. A change on the relay reaches a Telegram session already signed in.
- **TDLib ships with ChatCube.** The build fetches `libtdjson` (TDLib
  1.8.69) into `Plugins/UltraIM/tdlib/` and the Linux and Windows packages
  carry it, so nothing has to be installed or copied by hand to use the
  Telegram account.

#### 2026-10-25 *0.1.3*
- **ChatCube crashed at start in a folder holding older UltraCanvas DLLs**
  (Windows). It now says that the `libUltraCanvas.dll` it loaded is from
  another build - with its path and both versions - and exits instead of
  crashing; keep ChatCube with the DLLs from its own package.

#### 2026-10-22 *0.1.2*
- **ChatCube from the Windows package started without its networks.** The
  package left out `Plugins/UltraIM`, so the sign-in page said "The
  ChatCube Direct module was not found" and nothing could be done. The
  provider modules (ChatCube Direct, Matrix, Telegram) ship next to
  `ChatCube.exe` now, in the zip, the installer and the MSIX, and ChatCube
  is in the Linux package too, with its modules.
- **ChatCube crashed as it quit (SIGABRT).** Closing the window ended in an
  abort inside `free()` after ChatCube had shut down: the framework's
  database layer wrote into freed memory while the messaging service closed
  its journal at exit. Fixed in UltraDatabase; ChatCube quits cleanly.
- **Signing up on a relay behind an HTTPS redirect failed with HTTP 422.**
  A relay address typed as `http://…` or without a scheme reached a server
  that redirects to `https://`, and the redirected request arrived without
  its body. ChatCube now takes a bare host as `https://`, follows the
  redirect with the request intact, and remembers the address it ended at;
  a request the relay cannot read says why instead of "HTTP 422".

#### 2026-10-21 *0.1.1*
- **Telegram.** *Telegram account* in the sidebar signs in with your phone
  number, one step at a time: the code Telegram sends, then your two-step
  password if you have one (a new number asks for your name). Your
  Telegram chats and groups join the list with replies, edits, deletions,
  typing, read ticks and unread counts; add people by `@username`, `t.me/`
  link or phone number, and start groups with them. The sign-in is kept, so
  ChatCube reconnects by itself. Not yet: secret chats, pictures and files,
  calls.

#### 2026-10-21 *0.1.0*
- **ChatCube, the ULTRA OS native messenger, rebuilt on UltraIM.** One
  window: a sign-in page that makes your ChatCube ID - a key pair on this
  computer, no account, no password - and a chat view with your
  conversations on the left and the open one on the right. Add a contact by
  the ID they give you, or start a group. Messages are end-to-end encrypted
  and travel directly between the two computers when both are online (a
  WebRTC data channel), through the relay when the other side is offline,
  and through the relay for groups. Bubbles show their delivery ticks:
  … sending, ✓ queued on the relay, ✓✓ on the other computer, green ✓✓ read.
  Typing and presence show under the name.
- **Voice calls.** *Call* rings the open contact; a bar across the top
  shows the call - *Accept* / *Decline* when it rings, *Mute* and
  *Hang up* during it, with its duration. The voice goes directly between
  the two computers (Opus over WebRTC, encrypted), and the status line says
  why a call ended: declined, busy, no answer, offline, missed.
- **Video calls.** *Video* rings the open contact with a video call. The
  other person fills the panel under the call bar, you see yourself small
  and mirrored; *Camera off* stops your picture (the other side is told and
  shows "camera off"). The picture travels directly between the two
  computers like the voice (VP8 over WebRTC, encrypted); a lost packet costs
  a moment, not a broken picture - the receiver asks for a fresh key frame.
- **Chat on your local network, with no server at all.** Leave the relay
  empty and ChatCube finds the other ChatCubes on your network by itself
  (*On this network* in the sidebar, **Add**) and talks to them directly:
  messages, read ticks, voice and video calls. Each side proves its ChatCube
  ID with its key before anything is exchanged. Next to a relay, *Also find
  people on this network* keeps the conversation going when the relay is
  out of reach. Groups still need a relay.
- **Matrix.** *Matrix account* in the sidebar signs in to any Matrix
  homeserver; your direct chats and rooms join the conversation list next to
  ChatCube Direct, with replies, edits, deletions, typing, read ticks and
  unread counts. An invitation shows in the list; answering it accepts it.
  Add someone on Matrix by their `@name:server`. The sign-in is kept, so
  ChatCube reconnects by itself. Not yet: pictures and files, calls.
- **Encrypted Matrix rooms.** ChatCube reads and writes end-to-end encrypted
  Matrix rooms like Element does (Olm and Megolm), and the direct chats and
  groups it starts are encrypted - the header says *end-to-end encrypted*.
  Each of your other devices and each of the other person's gets the keys
  from the moment ChatCube sees it; a message whose key has not arrived yet
  says so and opens when it does. *Export keys* / *Import keys* (click the
  Matrix account button once signed in) write and read the passphrase
  protected key file Element uses, to read earlier messages on a new
  device. A device whose keys change is no longer trusted.
- History that arrives late (a Matrix invitation's messages, once it is
  accepted) takes its place by time instead of coming last.
- A call the other person hangs up now always ends as a hang-up: the closing
  connection used to arrive before the hang-up message and reported the
  call as lost.
