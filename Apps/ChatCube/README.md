# ChatCube

The ULTRA OS native messenger. Chat directly with the people you know -
end-to-end encrypted, peer to peer when both of you are online, through a
relay server when the other person is offline and for groups. There is no
account and no password: your ChatCube ID is a key made on your computer.

ChatCube is the window; the networks are UltraIM providers
([`Docs/Modules/UltraIM`](../../Docs/Modules/UltraIM/README.md)). This release
uses ChatCube Direct (`UltraCanvas/Plugins/UltraIM/direct`), Matrix
(`.../matrix`) and Telegram (`.../telegram`); XMPP and other messengers with
an open API come later as further providers.
Plan and decisions: [`Docs/Research/ChatCubeDirectPlan.md`](../../Docs/Research/ChatCubeDirectPlan.md).
The first ChatCube draft, which this replaces, is kept under
[`Docs/ChatCube/v1-draft`](../../Docs/ChatCube/v1-draft/README.md).

## Using it

1. Start ChatCube, type your name and the address of a ChatCube relay
   (`Server/ChatCubeRelay`), and press **Create my ChatCube ID**. Later starts
   sign in by themselves. **No relay?** Leave it empty: ChatCube then works on
   your local network alone - the ChatCubes there find each other (mDNS) and
   talk directly. *Also find people on this network* does that next to a
   relay.
2. **Copy** your ID and send it to the people who should reach you; paste
   theirs into *Contact's ChatCube ID* and press **Add**. People on your
   network are listed under *On this network* - **Add** takes them over
   without typing anything.
3. **New group** takes a name and the members' IDs, separated by commas.
   **Matrix account** signs in to a Matrix homeserver (`matrix.org`, or your
   own) with your user name and password; your Matrix rooms then sit in the
   same list, and a Matrix ID (`@name:server`) in *Add* or *New group* goes
   there. Encrypted rooms work as in Element, and the rooms ChatCube starts
   are encrypted (*end-to-end encrypted* under the name). Signed in, the
   Matrix account button shows **Export keys** / **Import keys**: a
   passphrase-protected file of the room keys, the one Element reads and
   writes, so a new device can read the messages from before it.
   **Telegram account** signs in with your phone number: ChatCube asks for
   the code Telegram sends, then your two-step password if you have one (a
   new number asks for your name). Telegram also wants this application's
   API ID and hash from my.telegram.org: your relay hands them out when it
   has them (`CHATCUBE_RELAY_TELEGRAM_API_ID` / `_HASH` in its `.env`), and
   the two fields appear only when it does not, or when there is no relay.
   The status line at the bottom keeps one entry per network - *ChatCube:
   connected · Matrix: signed in as @you:matrix.org · Telegram: waiting for
   the network…* - beside what your last action said, so it shows what
   Telegram is doing while you wait, and a step Telegram does not answer
   within 45 seconds says so instead of waiting forever. The form says where the code went: as a
   rule Telegram sends it as a message to the Telegram apps where your
   number is already signed in - look in the "Telegram" chat on your phone,
   not for an SMS - and only offers SMS or a call for some numbers; when it
   does, a button under the code field asks for it that way. When no code
   comes at all - Telegram has delivered none to third-party applications
   for stretches since early 2026 - *Scan a QR code instead* (also *Sign in
   with a QR code instead* at the start) shows a QR code: scan it with the
   Telegram app on your phone (Settings, Devices, Link Desktop Device) and
   ChatCube is signed in with no code, after your two-step password if you
   have one. Your
   Telegram chats join the list; `@username`, `t.me/name` or `+phone` in
   *Add* or *New group* goes to Telegram.
4. Enter sends, Shift+Enter starts a new line. A bubble's ticks:
   `…` sending, `✓` waiting on the relay, `✓✓` on the other computer,
   green `✓✓` read. The paperclip sends a picture, a video or any file to
   the open chat (ChatCube Direct, Telegram and Matrix, encrypted rooms
   included), with what you typed as its caption. A photo
   shows as its picture, a video as its preview under a play mark with its
   length, a file as its name and size; a click on a picture opens it full
   size, on a video or a file fetches it and opens it. People's Telegram
   profile pictures are their avatars in the sidebar, the header and, in a
   group, beside their bubbles.

5. **Call** rings the open contact, **Video** makes it a video call (1:1;
   the buttons are off for groups and while not connected). A bar across
   the top shows the call: *Accept* / *Decline* when it rings here, *Mute*,
   *Camera off* (video) and *Hang up* during it, with its duration. A video
   call shows the other person large and you, mirrored, small; when one of
   you turns the camera off the other sees "camera off" instead of a frozen
   picture.

## Building and running

ChatCube builds with the framework (`BUILD_CHATCUBE`, on by default) and needs
UltraIM, UltraRTC (OpenSSL) and UltraNet; the provider modules are built next
to it in `<build>/Plugins/UltraIM/`. Matrix encryption needs a Rust toolchain
(cargo) at build time; without one the Matrix module cannot open encrypted
rooms. Telegram needs `libtdjson` from TDLib (Telegram Database Library) at
run time, not at build time: the configure step fetches TDLib 1.8.69 prebuilt into
`<build>/Plugins/UltraIM/tdlib/` (`cmake/UltraCanvasTdlib.cmake`; Linux
x86_64 and aarch64, macOS arm64, Windows x64; `ULTRAIM_TDJSON_FILE` for one
of your own, `ULTRAIM_FETCH_TDLIB=OFF` to skip) and the packages ship it;
elsewhere ChatCube looks on the system's library path, or where
`ULTRAIM_TDJSON_PATH` points. To try the Telegram side without Telegram, point
`ULTRAIM_TDJSON_PATH` at the test build's `libfaketdjson`
(`<build>/Tests/UltraIM/fake-tdjson/`): any number, code `12345`, and
`FAKE_TDJSON_ACCOUNTS="15550001:Echo:echobot:"` gives you someone who
answers. `CHATCUBE_PLUGIN_DIR` points it elsewhere.
Its data - the identity's key vault, contacts, history - is in the per-user
data folder (`~/.local/share/ChatCube`, `%APPDATA%\ChatCube`,
`~/Library/Application Support/ChatCube`).

`ChatCube --dark` is the dark look; the light one is the default. Both are
one theme (`ui/ChatCubeTheme.h`: the ChatCube green on warm neutrals, the
avatar palette, the bubble and presence colours), so every element of the
window and everything the lists paint themselves take their colours from
the same place.

When a sign-in does not go through, `ChatCube --debug` says on stderr what
the networks do - the relay's answers, every object sent to and received
from TDLib (types and errors, never a number, code, password or message) -
and `ChatCube --log <file>` appends the same to a file
(`ULTRACANVAS_DEBUG_LOG` does either without the option). TDLib's own log
is `telegram/tdlib.log` in the data folder, always.

### Trying it alone

`chatcube-peer` (built with the app) is a second ChatCube identity on the
command line:

```bash
# a relay on this machine
cd Server/ChatCubeRelay && python3 -m venv .venv && .venv/bin/pip install -e .
CHATCUBE_RELAY_DB=/tmp/relay.sqlite3 .venv/bin/chatcube-relay &      # http://127.0.0.1:8790

# Bob, who answers everything for ten minutes
build/chatcube-peer --relay http://127.0.0.1:8790 --data /tmp/bob --name Bob --create --print-id
build/chatcube-peer --data /tmp/bob --add <your ChatCube ID> --echo 600
```

Then add Bob's ID in ChatCube and write to him.

### Screenshots without a display

`Apps/ChatCube/tools/screenshot.sh [--dark]` does the above by itself under
Xvfb: it starts a relay and Bob on this machine, drives ChatCube through
XTest (`tools/xinput.c`, built once into the build directory) - sign in as
Alice, add Bob, two messages each way - and writes `signin.png`,
`empty.png` and `chat.png` to `<build>/chatcube-screenshots/<look>/`
(`--out` elsewhere). For looking at a change to the window by eye and for
pictures in the docs. It needs `xvfb-run`, ImageMagick's `import`, a C
compiler with the libX11 and libXtst headers, and the relay's `.venv`.
The clicks are at fixed places in the window, so a change to the sign-in
card or the sidebar moves them; the pictures then show where they landed.

## Files

| | |
|---|---|
| `main.cpp` | start-up: UltraNet, UltraRTC, UltraIM with the UI-thread dispatcher, provider modules |
| `ui/ChatCubeWindow.*` | the window: sign-in page, sidebar, conversation pane, UltraIM events |
| `ui/ChatCubeViews.*` | list models and delegates: conversation rows, chat bubbles, date separators |
| `ui/ChatCubeTheme.*` | the look: the light and the dark theme, avatars, icons, the colours the views paint with |
| `peer/main.cpp` | `chatcube-peer` |
| `tools/screenshot.sh`, `tools/xinput.c` | screenshots of the app under Xvfb, driven through XTest |

Version: `Docs/ChatCube/CHANGELOG.md` (first line).
