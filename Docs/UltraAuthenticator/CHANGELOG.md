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
- Needs UltraCanvas 0.9.20 or later: the minimise lock relies on the
  framework's new `onWindowMinimize` / `onWindowRestore` notifications for
  window-manager-initiated minimises (see the framework changelog).

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
