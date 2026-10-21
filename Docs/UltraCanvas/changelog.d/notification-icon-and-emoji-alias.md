- **Windows notifications are the posting application's own.** A
  notification on the bus is put on screen by the process that hosts the
  broker, and the windows-presenter showed it as that process's balloon:
  UltraMail's "New mail" came headed "UltraFiler - Cross-Platform File
  Manager", with UltraFiler's folder icon, when UltraFiler hosted the broker.
  - It is now a toast under the posting application's own AppUserModelID
    (its `appId`, `org.ultraos.ultramail`), registered for the user under
    `HKCU\Software\Classes\AppUserModelId` with its name and icon, so
    Windows heads it "UltraMail" with UltraMail's logo. Windows 10 1903 and
    later take that from a program with no package identity and no Start-menu
    shortcut. New `UltraMessageWindowsToasts.cpp`, built with C++/WinRT where
    the notification listener is.
  - The toast carries the notification's buttons (up to three), is silent at
    `low` urgency, and changes in place on an update; a click, a button and a
    dismissal come back on the bus while the broker runs. An application the
    user turned off in Settings shows nothing.
  - Older Windows, a build without C++/WinRT, or a toast that cannot be shown
    get the balloon, whose picture is now the notification's icon file (a PNG,
    as UltraMail sends `media/appicon/UltraMail.png`, or an `.ico`) instead of
    the host executable's; its header still names the host.
  - The platform-neutral half - the ID, the toast XML, the activation
    arguments - is tested on every platform (`test_adapters.cpp`), and the
    adapter's mode says which it uses (`toast` or `balloon`).
- **An emoji goes to the colour emoji font.** Pango asks for the family
  `emoji` for a run of emoji, and the fonts.conf UltraCanvas writes where the
  system has none - always on Windows and Android - had no alias for it. Every
  font then matched it equally badly, and the emoji went to whichever font
  covering it fontconfig ranked first (on Linux, Unifont before Noto Color
  Emoji). The generated config now aliases `emoji` to Segoe UI Emoji and
  Segoe UI Symbol on Windows, Noto Color Emoji on Android, and Noto Color
  Emoji, Twemoji, JoyPixels and Symbola elsewhere.
