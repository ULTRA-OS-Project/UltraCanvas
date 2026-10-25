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
