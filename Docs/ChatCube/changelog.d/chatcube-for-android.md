- **ChatCube for Android.** ChatCube now builds as an Android app,
  `ChatCube-<abi>.apk` (arm64-v8a for phones, x86_64 for the emulator),
  from the same code as the desktop's: ChatCube Direct, Matrix (without
  end-to-end encryption for now), Mastodon, Mattermost, Zulip, XMPP and IRC
  are built into it. Telegram and Signal are not - Telegram's TDLib and
  signal-cli do not run on Android - and their buttons are not shown. The
  microphone and the camera are asked for the first time a voice or video
  message is recorded or a call made or answered. Video calls are sound
  only on Android for now. How to build and install it:
  `Apps/ChatCube/android/README.md`. The APK has not been run on a phone yet.
- **A narrow window shows one column at a time.** Below 640 pixels wide -
  a phone, or a desktop window made narrow - the chat list fills the
  window; opening a chat shows it in the list's place, with a back arrow
  before its name (on Android the back key does the same). A chat that is
  not on screen is not marked read when a message arrives in it.
- **The sidebar shows only the networks ChatCube can reach.** A network
  whose module is missing from this installation no longer has a button
  that fails when pressed.
- **A message longer than one line is shown whole.** A message that wrapped
  in its bubble showed only its first line, ending in "…", although the
  bubble had room for the rest. On a phone that was nearly every message.
