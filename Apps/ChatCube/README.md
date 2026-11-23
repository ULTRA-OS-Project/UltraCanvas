# ChatCube

The ULTRA OS native messenger. Chat directly with the people you know -
end-to-end encrypted, peer to peer when both of you are online, through a
relay server when the other person is offline and for groups. Your ChatCube
ID is a key made on your computer; a password and an authenticator code
protect it when you want them to.

ChatCube is the window; the networks are UltraIM providers
([`Docs/Modules/UltraIM`](../../Docs/Modules/UltraIM/README.md)). This release
uses ChatCube Direct (`UltraCanvas/Plugins/UltraIM/direct`), Matrix
(`.../matrix`), Telegram (`.../telegram`), the direct messages of
Mastodon (`.../mastodon`), the team chats Mattermost (`.../mattermost`)
and Zulip (`.../zulip`), Signal (`.../signal`, through signal-cli), XMPP
(`.../xmpp`) and IRC (`.../irc`).
Plan and decisions: [`Docs/Research/ChatCubeDirectPlan.md`](../../Docs/Research/ChatCubeDirectPlan.md).
The first ChatCube draft, which this replaces, is kept under
[`Docs/ChatCube/v1-draft`](../../Docs/ChatCube/v1-draft/README.md).

ChatCube also builds for Android, from the same code: the networks are
linked into the app instead of loaded as modules, Telegram and Signal are
left out, and on a narrow window - a phone, or a desktop window made
narrower than 640 pixels - the chat list and the open chat take turns, with
a back arrow. Building, packaging and what is not there yet:
[`android/README.md`](android/README.md).

## Using it

1. Start ChatCube, type your name, your e-mail address and your phone number
   (with `+` and the country code), and press **Create my ChatCube ID**.
   The relay is not asked for: it is a setting, *Settings > Networks >
   Relay*, `https://relay-chatcube.ultraos.eu` unless you change it (any
   ChatCube relay will do - `Server/ChatCubeRelay`). The e-mail
   address and the phone number are how you recover the account: they are
   kept in the encrypted key vault on your computer and on the relay, for you
   alone - your contacts see your name, never these. The relay e-mails you a
   link to confirm the address right away: open it, and the page that opens
   says it is confirmed. *Signing in on this
   computer* chooses what a start asks for:
   - **No password** - the start signs in by itself (the key is still
     encrypted on disk, with a key kept beside it);
   - **A password** - the key to your ID is encrypted with it (Argon2id,
     XChaCha20-Poly1305), so nobody can read the key without it, not even
     from a copy of the files; nobody can recover it for you either;
   - **A code from UltraAuthenticator** - the 6-digit code that
     UltraAuthenticator (or any authenticator app) shows: ChatCube shows a
     QR code to add it, and asks for a code at every start;
   - **Both** - the password, then the code. The strongest: the code alone
     keeps someone at your computer out of ChatCube, the password is what
     keeps the key unreadable.

   A protected ID asks at every start: the password, then the code, each on
   the sign-in card. Each code works once; after five wrong codes ChatCube
   waits 30 seconds before it takes another, twice as long after each
   further one. Without a password, later starts sign in by themselves.
   While ChatCube connects - at every start with an ID on the computer, and
   after *Create* or *Unlock* - a **Connecting** page of its own shows the
   relay it is reaching; when that fails, it says why and offers *Try
   again* and *Settings* (to change the relay). **No relay?** Empty the
   field in *Settings > Networks > Relay* before your ID is made: ChatCube
   then works on your local network alone - the ChatCubes there find each other (mDNS) and
   talk directly. *Also find people on this network* does that next to a
   relay.
2. **Edit profile** - the person button beside your ID - changes your name,
   e-mail address and phone number, and how signing in is protected.
   Changing the protection asks for what protects it now (the current
   password, the code UltraAuthenticator shows); a new authenticator is a QR
   code - UltraAuthenticator's *Add account* scans it from the screen, a
   phone's authenticator app from across the desk, or type the key shown
   beside it - proven with the first code it shows.

   **Getting your ID back.** Your relay confirms the e-mail address by the
   link it e-mails there (under the field in Edit profile: *Check* once you
   have opened it, *Send again* for a new one) and, if it can send SMS, the
   phone number by a code it texts - type it under that field. With the
   address confirmed, *Turn recovery on* (under *Getting this ID back*) lets
   the relay keep a copy of your key. On another computer, *Recover my
   ChatCube ID* under the sign-in card asks for your e-mail address (the
   relay is the one in *Settings > Networks > Relay*), and the relay e-mails a link there. Open it, press *Yes - sign
   that computer in as my ChatCube ID* on the page, and ChatCube - which
   has been waiting - signs in as you by itself: same ID, same contacts. You
   get an e-mail whenever it happens. The window says the price plainly:
   whoever runs the relay can read that copy, and whoever can read your
   e-mail can recover your ID - so turn it on only with a relay you trust,
   and keep the mailbox safe. Without it, an ID lost with its computer
   cannot come back (keep the data folder in your backups). The relay must
   be set up for it - an SMTP server and a recovery key
   ([`Server/ChatCubeRelay`](../../Server/ChatCubeRelay/README.md), *Account
   recovery*); one that is not says so. A recovered ID starts with
   no password and no authenticator on the new computer: those are each
   computer's to choose.
3. **Copy** your ID and send it to the people who should reach you; paste
   theirs into *Contact's ChatCube ID* and press **Add**. People on your
   network are listed under *On this network* - **Add** takes them over
   without typing anything. The person you add is told: your chat appears in
   their list at once (or at their next sign-in), before you write.
4. **New group** takes a name and the members' IDs, separated by commas.
   **Public groups** under it lists the ChatCube groups anyone may join -
   the biggest first, with how many are in each - and **Join** puts you in
   one at once, no invitation needed (what was written before you joined is
   not shown). A group of your own becomes one with *Make public* in its
   menu, and *Make private* takes it off the list; its messages stay
   end-to-end encrypted either way. A public group holds up to 256 people,
   and someone you take out of it cannot join again by themselves. Needs a
   relay of version 1.5.0 or later.
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
   **Mastodon** (beside Telegram) asks for your server (`mastodon.social`,
   or any other) and opens its own sign-in page in your browser: you sign
   in there and allow ChatCube, and ChatCube is signed in - your password
   never passes through it. Your direct messages (statuses "only for the
   people mentioned") join the list, one chat per set of people however
   many threads they are; `@user@server` or a profile's address
   (`https://server/@user`) in *Add* or *New group* goes to Mastodon.
   Messages are text, pictures, videos and sound - Mastodon takes no other
   file - and come every few seconds (ChatCube asks the server; Mastodon
   has no typing, read ticks or calls).
   **Mattermost** and **Zulip**, the team chats, have the third row of
   buttons: your server, your user name or e-mail (Zulip: the e-mail) and
   your password; a Mattermost account with two-factor sign-in then asks
   for the code from your authenticator app. Mattermost brings your direct
   and group messages and your teams' channels (`Town Square (Team A)`),
   replies as threads; Zulip your direct and group messages and every topic
   of your streams (`#general > greetings`) as a chat of its own, news the
   moment it is sent. Files of any kind go both ways. An address in *Add*
   that more than one signed-in network takes - `@user` on Telegram and
   Mattermost, an e-mail address on Zulip and Mattermost - asks which; when
   none of them is signed in it names them.
   **Signal** shows a QR code: scan it with the Signal app on your phone
   (*Settings > Linked devices*) and ChatCube becomes a linked device, as
   Signal Desktop is. It needs [signal-cli](https://github.com/AsamK/signal-cli)
   installed (or the address of a signal-cli daemon you run); ChatCube sees
   messages from the moment it is linked. Numbers (`+49 ...`) and Signal
   usernames (`name.01`) can be added.
   **XMPP** takes your address (`erin@example.org`) and password - the server
   is found from the address, or typed in its own field (`chat.example.org`,
   `xmpps://chat.example.org`, or a `wss://` address). ChatCube connects
   over TLS, or through WebSocket where the server offers only that. Your contacts and rooms come along, with the server's history.
   **IRC** takes the server (`irc.libera.chat`; TLS unless it starts with
   `irc://`), your nickname and, for a registered nickname, the password.
   Add `#channel` to join a channel or a nickname for a private chat; the
   network's own conversation shows its notices and takes IRC commands.
   IRC has no files - share a link.
5. The icons beside the message field are its features: the paperclip,
   smileys & stickers, a screenshot, a location, a voice and a video
   message. Some are in the field's row, the rest behind the **+** at its
   left, which opens a menu of them (the + is not shown while nothing is
   behind it); *Settings > Display > Feature icons* puts each one in the
   field, behind the +, or away. The smiley opens a grid of the set used
   last - an emoji goes into the text at the caret, a sticker into the
   strip as a 256 x 256 picture - and the arrow beside it lists the sets,
   each by one example (Smileys, Hands, Hearts, Animals, Food & drink,
   Celebrations & things, and the example sticker set *Cubes*). Location
   opens a window with an OpenStreetMap map (UltraCanvasMapView): search a
   place by name or address (Nominatim), click the map to put the pin there
   (it is named by what is there), or press *My position* - where the
   system's location service says this computer is (UltraCanvasGeolocation:
   GeoClue, Location Services, Windows' location; hidden where there is
   none); the wheel and +/- zoom, a drag moves the map. Insert writes
   "📍 the place" and its openstreetmap.org link into the message. In the
   chat any message holding an openstreetmap.org link with a marker shows
   as a map card - the map, its pin and the place's name - and a click on
   the card opens the place in the browser; other programs show the text.
   The map and the search need tile.openstreetmap.org and
   nominatim.openstreetmap.org.
6. Enter sends, Shift+Enter starts a new line (or Ctrl+Enter sends - see
   *Settings* below). A bubble's ticks:
   `…` sending, `✓` waiting on the relay, `✓✓` on the other computer,
   blue `✓✓` read, *Not sent* when it could not go - and, hovering over the
   message, the same in words (drawn, not typed, so they show on a Windows whose fonts
   have no check mark). The paperclip puts a picture, a video or any file
   in a strip above the message field - so does Ctrl+V with a picture on
   the clipboard (a screenshot, a copy from a browser or a drawing program)
   or a file copied in a file manager, the Screenshot feature (the window
   hides, a window is clicked or an area dragged on the frozen screen), a
   sticker, and files dropped on the
   window (on a chat in the list, that chat opens and takes them; anywhere
   else, the open chat does) - each with a × to take it out again;
   Enter or the send button sends them to the open chat (every network but
   IRC; Mastodon takes pictures, videos and sound only), with what you typed as
   the caption of the first picture or video. A photo shows as its picture,
   a video as its preview under a play mark with its length, a file as its
   name and size - an SVG drawing, which goes as a file, with its picture
   over them, as a photo; a click on a picture opens it full size, on a
   video or a file fetches it and opens it. People's Telegram profile
   pictures are their avatars in the sidebar, the header and, in a group,
   beside their bubbles.

   A right click on a message opens its menu. **Reply** answers it: a bar
   over the field shows what you answer (× or Escape gives it up), and your
   message shows it, quoted, at the top of its bubble. **Edit** - or a
   double click on one of your own messages - puts its words back in the
   field to change; Enter keeps the change, Escape gives it up, and the
   bubble says *edited* on both sides. **Copy text** copies its words. A
   picture, a video or a file has **Open** and **Save as** (a copy where you say, fetched first when it
   is not on this computer yet). **Delete for me** removes a message from
   your side only; **Delete for everyone** removes it from the other side
   too and asks first, since it cannot be taken back. The menu offers what
   the chat's messenger allows for that message. In a ChatCube chat you can
   delete a text you sent for everyone until the other person has read it -
   after that only for yourself - while a picture, a video or a file you
   sent can be taken back at any time, and in a group so can any message of
   yours. A file deleted for everyone is removed from the other person's
   ChatCube as well; a copy they saved elsewhere is theirs. Matrix deletes
   for everyone only; Telegram has both.

   A file on its way shows a bar with how far it is (*Sending 42 %*,
   *Receiving 42 %*). Sending never holds up the window: the field is free at
   once and the file goes in the background. A video shows its preview
   picture and length - one ChatCube makes itself when the sender sent none -
   and plays in a player over the field when clicked; a song (MP3, OGG, M4A,
   FLAC, WAV ...) plays in its bubble like a voice message.

   The field over the messages searches the chat: what you type finds the
   messages with those words in all of its history, and the list shows only
   them (with how many) until the × shows the whole chat again. The chips
   under it - *All*, *Pictures*, *Videos*, *Audio*, *Files*, *Links* - show
   only that kind of message, with or without words.

   The microphone beside the field records a **voice message** - the bar in
   its place shows how long, × throws it away, the send button sends it -
   and a voice message plays in its bubble with a click on it (again
   pauses). The camera button records a **video message**: the camera
   opens over the field, its red button records and stops, **Send** sends
   it, **Cancel** throws it away. Both need a microphone (and a camera) the
   system lets ChatCube use. On Matrix and Telegram a voice message is
   theirs too (Telegram's needs the recording in Ogg Opus, which ChatCube
   writes when this build has the Opus encoder; otherwise it goes as a file);
   the other networks get it as a sound file, and IRC takes neither.

7. **Call** rings the open contact, **Video** makes it a video call (1:1;
   the buttons are off for groups and while not connected). A bar across
   the top shows the call: *Accept* / *Decline* when it rings here, *Mute*,
   *Camera off* (video) and *Hang up* during it, with its duration. A video
   call shows the other person large and you, mirrored, small; when one of
   you turns the camera off the other sees "camera off" instead of a frozen
   picture.

8. **A right-click on a chat** in the list opens its menu, and leaves the
   open chat as it is. Every chat: *Open*, *Mark as read* / *Mark as
   unread* (a dot until you open it) and *Pin to top* / *Unpin* (pinned
   chats stay first, with a pin). A person: *Voice call*, *Video call*,
   *Copy ChatCube ID* (*Copy address* on the other networks), *Add to
   group* and *Delete chat* - the chat and its messages go from this
   computer; they keep theirs. A group: *Members*, each with *Send a
   message*, *Copy ID* and, for the group's owner, *Remove from group*;
   *Group settings…*, *Add members*, *Rename group*, *Make public* /
   *Make private* (the owner's, ChatCube groups) and *Leave group*.
   **Group settings…** opens the group's editor: its name and, for a
   ChatCube group, a description (what it is about, up to 500 characters -
   shown when its name is hovered, and under *Public groups*), *Make public*
   / *Make private*, and every member with **Message** and, for the owner,
   **Remove** and **Block**. Someone removed may come back (*Join*, or a
   member adds them); someone blocked is taken out and kept out until the
   owner unblocks them in the editor's *Blocked* list. In a private group
   any member changes the name and description; in a public one, the owner. What the chat's network
   cannot do is not offered, and what it refuses the account (in most
   Telegram groups only administrators rename or take people out) says so
   on the status line. Pins and unread marks are kept in the settings file.

9. **Settings** - the gear beside the copy button at the top of the sidebar
   - opens a window laid out as UltraFiler's: the sections on the left, the
   chosen page on the right, *Restore defaults* and *Close* at the foot.
   *Appearance*: the look - System, Light or Dark, shown at once - and how
   times and dates are written (`14:05` or `2:05 PM`; `31.10.2026`,
   `2026-10-31` or `10/31/2026`). *Display*: the feature icons - for each,
   in the message field, behind the + button, or not shown. *Messages*: whether Enter or Ctrl+Enter
   sends, and whether typing is shown to the other side and to you. Someone
   typing shows in the chat's header and in its row of the list, in the
   accent colour - in a group with their name ("Anna is typing…", "Anna and
   Ben are typing…").
   *Networks*: where each network stands (and the version of its module),
   your ChatCube ID to copy, signing out of any other network you are signed
   in to, and whether the people on this network are
   listed in the sidebar. *Extras*: the data folder, to open or copy, and the
   version. A change applies at once and is kept in
   `<settings folder>/ChatCube/settings.json` (`~/.config/UltraCanvas`,
   `%APPDATA%\UltraCanvas`, `~/Library/Application Support/UltraCanvas`).

## The networks at a glance

What each network does in ChatCube - ✓ yes, – not there (the network has
no such thing, or ChatCube does not do it yet):

| Network | Sign-in | Files | Edit, delete | Replies | Typing | Read ticks | Groups | Calls | End-to-end | Search |
|---|---|---|---|---|---|---|---|---|---|---|
| ChatCube Direct | your key on this computer | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ voice, video | ✓ | ✓ |
| Matrix | homeserver, user, password | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ rooms | – | ✓ encrypted rooms | ✓ |
| Telegram | phone number, code | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | – | – | ✓ |
| Mastodon | the server's page in the browser | pictures, videos, sound | ✓ | ✓ | – | – | ✓ several people | – | – | ✓ |
| Mattermost | server, user, password (MFA) | ✓ | ✓ | ✓ threads | ✓ | – | ✓ channels | – | – | ✓ |
| Zulip | organization, e-mail, password | ✓ | ✓ | – (quotes) | ✓ | – | ✓ stream topics | – | – | ✓ |
| Signal | QR code (signal-cli) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | what this ChatCube saw |
| XMPP | address, password | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ rooms | – | – (TLS to the server) | ✓ |
| IRC | server, nickname | – | – | ✓ | ✓ | – | ✓ channels | – | – | ✓ |

What to type in *Add* for each: a ChatCube ID; `@name:server` (Matrix);
`@user`, `+49 …` or a t.me link (Telegram); `@user@server` (Mastodon);
`@user` or an e-mail (Mattermost); an e-mail (Zulip); `+49 …` or a
username `name.01` (Signal); `name@domain` (XMPP); `#channel` or a nickname
(IRC). An address more than one signed-in network takes asks which.

## Building and running

ChatCube builds with the framework (`BUILD_CHATCUBE`, on by default) and needs
UltraIM, UltraRTC (OpenSSL) and UltraNet; the provider modules are built next
to it in `<build>/Plugins/UltraIM/`. Only the modules in use are loaded:
ChatCube Direct, and the networks signed in to (UltraIM keeps them in
`ultraim-providers.txt` in ChatCube's data folder); another network's module
is loaded when its button is pressed, and one signed out of is not loaded at
the next start. Matrix encryption needs a Rust toolchain
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

`ChatCube --minimized` starts with the window minimized, so messages
arrive in the background - what UltraDesktop's *Start with the desktop*
asks for (a running copy is left alone; a second start would only bring
its window forward).

`ChatCube --dark` is the dark look whatever Settings chose; the light one is
the default. Both are
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
Alice, add Bob, two messages each way, open Edit profile - and writes
`signin.png`, `empty.png`, `chat.png`, `message-menu.png`, `reply-bar.png`,
`reply.png`, `edit.png`, `edited.png`, `search.png` and `profile.png` (and the
list's menus) to `<build>/chatcube-screenshots/<look>/`
(`--out` elsewhere). For looking at a change to the window by eye and for
pictures in the docs. It needs `xvfb-run`, ImageMagick's `import`, a C
compiler with the libX11 and libXtst headers, and the relay's `.venv`.
The clicks are at fixed places in the window, so a change to the sign-in
card or the sidebar moves them; the pictures then show where they landed.

## Files

| | |
|---|---|
| `main.cpp` | start-up: UltraNet, UltraRTC, UltraIM with the UI-thread dispatcher, provider modules |
| `ui/ChatCubeWindow.*` | the window: sign-in page (a new ID, or the password and code a protected one asks for), sidebar, conversation pane, UltraIM events |
| `ui/ChatCubeProfileWindow.*` | Edit profile: the name, e-mail and phone, and how signing in is protected (the UltraAuthenticator QR code) |
| `ui/ChatCubeControls.*` | the elements in ChatCube's look, shared by both windows |
| `ui/ChatCubeViews.*` | list models and delegates: conversation rows, chat bubbles, date separators |
| `ui/ChatCubeSettings.*` | the preferences the Settings window sets, in `settings.json` |
| `ui/ChatCubeSettingsWindow.*` | the Settings window: the tree of pages, each page, Restore defaults |
| `ui/ChatCubeFeatures.*` | the feature icons around the message field and where each one is (the field, behind the +, off) |
| `ui/ChatCubeEmoji.*` | the smiley button's sets and picker: the emoji grids, the sticker grid, the list of sets by example |
| `ui/ChatCubeLocationWindow.*` | Insert a location: Nominatim search, the map with its pin (UltraCanvasMapView), My position (UltraCanvasGeolocation); the location text and its parsing for the map cards |
| `ui/ChatCubeTheme.*` | the look: the light and the dark theme, avatars, icons, the colours the views paint with |
| `peer/main.cpp` | `chatcube-peer` |
| `tools/screenshot.sh`, `tools/xinput.c` | screenshots of the app under Xvfb, driven through XTest |

Version: `Docs/ChatCube/CHANGELOG.md` (first line).
