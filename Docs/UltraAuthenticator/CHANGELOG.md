#### 2026-10-04 *0.2.6*
- **The owner-only permission is set on the store and export files in a
  folder named in any script.** `fs::permissions` on the temporary file was
  handed its UTF-8 path as a plain string, which Windows reads in the ANSI
  code page; in a folder the code page cannot spell, the permission was set
  on no file at all. It goes through `PathFromUtf8` now, like the rename
  beside it (framework changelog: `check_path_string` sees what a header
  declares).
- **The vault is found in a Windows profile named in any script.** Its
  folder came from the narrow `getenv("APPDATA")`, which answers in the ANSI
  code page, so for a user name outside it the vault was looked for in a
  folder that does not exist. It is read with the framework's `GetEnvUtf8`
  now (framework changelog: `env-narrow`).

#### 2026-09-30 *0.2.5*
- **A QR code can be read from an image file or from the screen.** The scan
  dialog has two buttons beside Cancel. *From image…* opens a picture — the
  QR the site offered to download, a photo copied off a phone — and decodes
  it; the file is read by the scanner only, never copied, thumbnailed or
  added to the recent-files list. *From screen* decodes whatever a browser
  window on this same machine is showing, which is the common desktop case:
  the enrolment page and the authenticator share one display and no camera
  can see either. The capture stays in memory (the framework's new
  `UltraCanvasDesktopShell::CaptureScreenImage`; the PNG-writing
  `CaptureScreen` would have put the seed in the Pictures folder) and is
  wiped as soon as it has been decoded. All three sources hand the URI to
  the same handler as before, so nothing bypasses the parser, and both new
  ones work when there is no camera or its access was refused — the status
  line now says so instead of sending the user to the setup key. A rejected
  code re-opens the camera only if one had opened in the first place.

#### 2026-09-29 *0.2.4*
- **Ctrl-C and SIGTERM exit in order.** The signal handler called
  `RequestExit()` (which logs and runs a callback) and then `std::exit`,
  running the static destructors under live threads. It now makes the one
  call a handler may, `UltraCanvasApplicationBase::RequestExitFromSignal()`,
  and the main loop turns it into the same shutdown as a closed window.

#### 2026-09-28 *0.2.3*
- **The version is in the window title** — `UltraAuthenticator 0.2.3` — so a screenshot or a
  bug report says which build it came from. The number is this changelog's
  first line, as everywhere else (`cmake/UltraCanvasVersion.cmake`).

#### 2026-09-24 *0.2.2*
- **The start screens show the app's logo.** The first-launch password screen
  and the lock screen now open with the UltraAuthenticator logo, centred, and
  the name "UltraAuthenticator" in small type beneath it (`BrandHeader.h`), so
  the first password prompt is recognisably this app's.
  The lock screen is also the sign-in popup that appears after 5 minutes
  without input (or on minimise, or the Lock button), so it carries the logo
  in every case.
- **The password screens say what makes a password safe.** Under the new
  password on the first-launch screen and in *Change master password*, a
  checklist ticks off as it is typed: 12 or more characters, an uppercase
  letter, a lowercase letter, a number, a symbol (`PasswordAdvice.h`, built
  from the catalogue's `UltraCanvasPasswordRuleLegend`). It is advice only:
  Create and Change never check it, an unmet rule is a grey circle rather
  than a red cross, and the heading says "recommended, not required" - a
  long passphrase of plain words is a good password that fails half of them.
- **Smaller fine print.** The explanation under "Choose a master password"
  and the lock screen's reason line are set in the small size (11 pt), so
  they read as notes under the title rather than competing with it.
- **Password fields have an eye button.** Every master-password, backup
  passphrase and setup-key field can now show what was typed, through the
  framework's password reveal button — which is now on by default for every
  password input (framework changelog, TextInput 1.6.0).

#### 2026-09-23 *0.2.1*
- **The launch unlock is the lock screen.** Opening an existing vault used a
  separate password prompt that quit the app on a wrong password — a typo
  cost a restart, and a guesser paid nothing more than that. The window now
  comes up locked over the attached vault (`AccountStore::Attach`) and the
  same `LockScreenDialog` as after an idle or minimise lock unlocks it, with
  the same back-off: three wrong passwords are free, then the wait doubles.
  Nothing about the accounts is rendered before the password, as before.
- **A new vault asks for its password twice.** The first-launch prompt was
  the framework's one-line input dialog, which took the password once - so a
  typo there silently became the master password, the one that cannot be
  recovered, and the user found out at the next launch. `NewVaultDialog`
  now has a confirmation field that must match, a strength meter that
  follows the first field as it is typed (advice, not a gate), and Quit as
  the only other way out.

#### 2026-09-22 *0.2.0*
- **The vault locks itself.** A new **Lock** button, a lock after a period
  without input to the window (Settings → *Lock after no input for*, default
  5 minutes, or *Never*), and a lock when the window is minimised (on by
  default) all do the same thing: clear every card and drop the decrypted
  vault from memory, then show a lock screen that only the master password or
  *Quit* gets past. Wrong passwords are throttled — three are free, then each
  one doubles the wait, up to five minutes — and the throttle lives in the
  account store, so it holds whatever the UI does.
- **Codes can be hidden until clicked** (Settings → *Hide codes until a card
  is clicked*, off by default). A hidden card shows "••• •••"; a click shows
  its code for 15 seconds. Hidden cards are not even computed, so this also
  stops the once-a-second decryption of every seed.
- **Settings dialog and settings file.** The three options above are kept in
  `settings.ini` beside the vault, in plain `key = value` lines — nothing in
  it is secret. Hand-edited values are clamped, never trusted.
- `--version` now reports the version from this file instead of a hard-coded
  string.
- **A user-facing page**, [`README.md`](README.md): what the app protects
  and how, and — stated plainly — what it cannot protect against (screen
  capture under X11, other programs running as the same user, phishing, a
  lost master password, a wrong clock).
- Needs UltraCanvas 0.9.26 or later: the minimise lock relies on the
  framework's new `onWindowMinimize` / `onWindowRestore` notifications for
  window-manager-initiated minimises (see the framework changelog).
- The two gaps 0.1.1's README named — no auto-lock, `--version` printing a
  literal — are the ones closed above.

#### 2026-09-22 *0.1.1*
- **UltraAuthenticator has an app icon.** The uploaded artwork — the ULTRA
  wordmark in five code tiles — is `media/appicon/UltraAuthenticator.svg`, and
  `media/appicon/UltraAuthenticator.png` is its 256 px render. The Xara export
  was a page, not an icon: a 2:1 `viewBox` in pt, a Times New Roman
  declaration no glyph in the file uses, an empty `<defs>` and an SVG 1.1 DTD
  reference a parser may try to fetch off the network. The drawing is kept
  verbatim; only the root element was rewritten, onto a square canvas with the
  mark centred — `share/icons/hicolor/256x256/apps` means 256x256, and a
  landscape render is not that.
- **It is drawn everywhere the app is shown.** `main.cpp` hands the PNG to
  `SetDefaultWindowIcon` for the window and the taskbar entry that follows it;
  `UCAPP_ICON_PATH` names the same file as the core's fallback, so a window
  never comes up unbranded; `ultracanvas_embed_app_icon` builds the `.exe`
  icon Explorer and the Windows taskbar read off the binary itself. A
  freedesktop entry (`Apps/UltraAuthenticator/UltraAuthenticator.desktop`)
  puts the authenticator in the application menu, and the install rules place
  the PNG in `share/icons/hicolor/256x256/apps` and the SVG in
  `share/icons/hicolor/scalable/apps` so `Icon=UltraAuthenticator` resolves —
  the route UltraFiler takes to show an application's own icon. The app now
  takes the build's asset copy as a dependency, so the PNG is in its resources
  dir when it runs out of the build tree. The entry claims no `MimeType`: an
  `otpauth://` URI is pasted into the Add-account dialog, and making this the
  system's handler for secrets it cannot vet is not something a desktop file
  should do quietly.
- **The app has a README** (`Apps/UltraAuthenticator/README.md`), which every
  other application under `Apps/` had and this one did not. It maps the
  directory, says where the vault lives and what protects it, walks the two
  ways in (camera, typed key) and the three ways out (reveal, back up,
  restore), and — because the investigation (§3.6) asks for it in so many
  words — states plainly what the app does *not* defend against: a live
  same-user attacker, X11 screen and input capture, relay phishing, and a
  forgotten master password. The two real gaps are named rather than left to
  be discovered: there is no auto-lock, and `--version` prints a literal
  instead of the changelog's number.

#### 2026-08-31 *0.1.0*
- **UltraAuthenticator keeps its own changelog from here.** Everything up to
  and including this version shipped as part of a framework release and is
  recorded in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) —
  nothing was rewritten or moved, so that history stays where it was published.
  From now on a change to the TOTP/HOTP authenticator
  (`Apps/UltraAuthenticator`) is described here and carries this file's
  version, and UltraAuthenticator no longer moves when the framework releases.
- A framework change UltraAuthenticator needs still belongs in the framework
  changelog. Cross-reference it from here when a release depends on it; never
  describe one change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
