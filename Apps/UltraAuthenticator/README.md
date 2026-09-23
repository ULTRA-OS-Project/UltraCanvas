# UltraAuthenticator

TOTP (RFC 6238) and HOTP (RFC 4226) one-time codes, kept in a single file
encrypted with a key derived from a master password. One card per account,
each showing its current code and the seconds left before it rolls over.

Accounts are enrolled by pointing the camera at the service's QR code, or by
typing the Base32 setup key when there is no camera. They can be taken out
again — one at a time, or all at once into an encrypted backup — because an
authenticator that can only swallow secrets strands its users.

The threat model, and the reasoning behind every decision below:
[`Docs/UltraAuthenticator/UltraAuthenticator-Investigation.md`](../../Docs/UltraAuthenticator/UltraAuthenticator-Investigation.md).
Changelog and version:
[`Docs/UltraAuthenticator/CHANGELOG.md`](../../Docs/UltraAuthenticator/CHANGELOG.md).

## Layout

| Path | What is in it |
|---|---|
| `otp/UltraOtp.*` | The HOTP/TOTP engine: code generation, the time-step counter, parameter bounds. Depends only on UltraCrypt |
| `otp/OtpAuthUri.*` | The `otpauth://` parser and builder — **the app's untrusted-input boundary**, since a provisioning URI arrives from whatever was pointed at the camera |
| `store/ISecretStore.h` | The Put/Get/Delete/Replace/List interface secrets are read and written through, shaped like UltraVault's so the backend can be swapped |
| `store/EncryptedFileStore.*` | The vault: XChaCha20-Poly1305 over an Argon2id-derived key, one AEAD blob for the whole file |
| `AccountStore.*` | The account layer, and the only thing that ever touches a seed. The UI gets accounts and codes, never secrets |
| `AccountExport.*` | The encrypted backup file: every account in one portable blob under its own passphrase |
| `LockPolicy.*` | `UnlockThrottle`: the exponential back-off between unlock attempts, time-injected so it is tested without sleeping |
| `Preferences.*` | The three user settings and the `settings.ini` file beside the vault they live in; every value clamped on load |
| `AuthenticatorWindow.*` | The main window: the scrolling card list, the 1 Hz refresh timer that also drives the auto-lock, the two button rows |
| `LockScreenDialog.*` | What covers the window once the vault is locked: master password or Quit, nothing else |
| `SettingsDialog.*` | Idle timeout, lock on minimise, hide codes |
| `AddAccountDialog.*` | Manual entry — issuer, account name, Base32 key, masked while typing |
| `ScanAccountDialog.*` | Camera enrolment: preview, poll, decode, hand the URI to the same parser manual entry uses |
| `EditAccountDialog.*` | Rewrites an account's label and its OTP parameters |
| `RevealSecretDialog.*` | Shows one account's setup key, so it can be enrolled elsewhere |
| `ChangePasswordDialog.*` | Re-derives the vault key from a new master password |
| `BackupDialog.*` | Both directions of the encrypted backup — export and restore |
| `Theme.h` | One place for the colours, type sizes and metrics, so the window and the dialogs cannot drift apart |
| `main.cpp` | The unlock gate, the vault path, and the `--vault` / `--help` / `--version` command line |
| `UltraAuthenticator.desktop` | The freedesktop shortcut; `make install` places it with the app icon (`media/appicon/UltraAuthenticator.png` / `.svg`, the PNG rendered from the SVG) in the `hicolor` icon theme, which is how the application menu and UltraFiler find the app and its icon |

Everything above `AuthenticatorWindow` is headless: `UltraAuthenticatorCore`
(the static library this directory also builds) links only UltraCrypt, with no
UI, no document model and no storage beyond the vault file. That is why the
whole security-relevant surface is unit-tested in CI without a display.

## The vault

One file, `accounts.vault`, in the per-user data directory —
`$XDG_DATA_HOME/UltraAuthenticator/` or `~/.local/share/UltraAuthenticator/`
on Linux, `%APPDATA%\UltraAuthenticator\` on Windows — created 0600, or
wherever `--vault` points.

An account is stored as exactly one entry whose value is its `otpauth://`
URI. That is the format every authenticator already agrees on and the one this
app already has a tested parser for, so there is no second on-disk shape to
validate and keep in step.

The properties worth knowing, each of which the store's header explains at
length:

- **There is no plaintext path.** Without a crypto backend, or without a
  password, the store refuses to open. There is no "encryption unavailable"
  fallback that writes readable seeds — that silent fallback is the classic
  hole, and the app exits before drawing any UI if `UltraCrypt_IsAvailable()`
  is false.
- **The whole file is one AEAD blob**, so integrity covers every entry
  together: an attacker cannot splice, reorder or drop a single account. The
  60-byte header, including the declared Argon2id cost, is authenticated as
  associated data, so a downgraded cost is detected rather than obeyed.
- **Cost parameters travel with the file**, so raising the recommended
  Argon2id cost later cannot orphan an existing vault.
- **Writes are atomic** — temp file, flush, rename — so an interrupted save
  cannot truncate the vault.
- **A wrong password and a modified file report the same error.**
  Distinguishing them would tell an attacker which of the two they achieved.

The master password is asked for at launch, as a modal gate: nothing about the
accounts, not even how many there are, is rendered before it is accepted.
There is no "skip" and no "remember me".

## Getting an account in

*Scan QR code* opens the camera for preview only — recording is never
started, so no frame ever reaches a file. A preview frame containing an
enrolment QR **is** the seed, and one written to a temp file or picked up by a
thumbnailer would leak the second factor permanently. Frames are polled by the
dialog's own timer on the UI thread rather than through the recorder's
callback, whose thread is not documented.

*Enter key* is the same destination by hand: issuer, account name and the
Base32 setup key, masked while typing. It builds an `otpauth://` URI and hands
that over, so manual entry and the scanner converge on one validated path.

Both go through `AccountStore::AddFromUri`, which means a hostile QR code
faces exactly the parser `Tests/UltraOtpTests.cpp` exercises. There is no
second, laxer way into the vault. A duplicate is refused rather than silently
replaced: overwriting an account destroys a second factor, so it has to be a
deliberate *Remove* followed by an add.

## Getting an account out again

*Show key* puts one account's setup key on screen. It is the single place in
the app where a seed is visible, and the only `AccountStore` method that hands
back a secret buffer — so "who can see a seed" stays a one-line grep. It
demands the master password again: unlocking happened at launch, and the
person now at the screen may not be the person who unlocked it.

*Back up…* seals every account into one file under **its own passphrase**,
which must differ from the master password. A backup is the file most likely
to end up on a USB stick or in a cloud drive, and one that opened with the
device password would make finding it as good as having the machine. It uses
`UCDCrypto::Seal` — the framework's vetted Argon2id + XChaCha20-Poly1305
envelope — rather than a third hand-rolled container.

*Restore…* merges a backup into the open vault. Existing accounts are kept,
never overwritten, and the result is reported per account (`added`,
`skippedExisting`, `rejected`) rather than as a single success: "restored 38
of 40" is the interesting case, and silently dropping two would look identical
to restoring all.

*Edit* rewrites an account's label and its OTP parameters in one write, since
both live in the same stored URI and the label also determines the key. The
seed is carried across untouched and never leaves the call. Note that changing
digits, period, algorithm or type re-negotiates nothing with the service — it
only changes what this app computes.

## Command line

```
UltraAuthenticator [--vault PATH] [-h|--help] [-v|--version]
```

`--vault` selects a vault file other than the default. There are no headless
OTP modes: printing a code to a terminal would put it in the shell history and
the scrollback, and the app has no reason to want that.

`--version` prints the version on the first line of
`Docs/UltraAuthenticator/CHANGELOG.md`: `cmake/UltraCanvasVersion.cmake`
reads it into `ULTRAAUTHENTICATOR_VERSION` and this directory's
`CMakeLists.txt` passes it to the target.

## Building and testing

Built by default with the rest of the tree; `-DBUILD_ULTRAAUTHENTICATOR=OFF`
skips it. It needs **libsodium** — UltraCrypt refuses every operation without
a backend, and the app exits rather than starting unprotected. Camera
enrolment additionally needs **libzbar**; built without it, the scan dialog
says so plainly instead of showing a preview that could never decode.

This directory builds two targets: `UltraAuthenticatorCore` (the static
library above) and `UltraAuthenticator` (the GUI, only when the framework is
available).

Four headless suites cover the security-relevant surface — they link only
UltraCrypt, so they run in CI without a display:

```bash
cmake -S . -B build -DBUILD_TESTS=ON && cmake --build build
ctest --test-dir build -R 'UltraOtp|UltraAuthenticator'
```

| Target | What it covers |
|---|---|
| `UltraOtpTests` | RFC 4226 App. D and RFC 6238 App. B vectors, plus the provisioning-URI rejection cases |
| `UltraAuthenticatorStoreTests` | Confidentiality on disk, wrong-password refusal, tamper detection over the whole container, durability of each mutation |
| `UltraAuthenticatorAccountTests` | Duplicate refusal, HOTP counter durability across a reopen, type-mismatch refusal, that no seed, URI or label reaches the disk in the clear, and lock/unlock with the throttled refusal |
| `UltraAuthenticatorLockTests` | The back-off schedule as a pure function of failures and time, the throttle state machine, and the settings-file round trip with clamping of hand-edited values |
| `UltraAuthenticatorExportTests` | Backup round trip, refusal to reuse the master password, tamper detection, and that a restore never overwrites a live account |

## What it does not defend against

Stated plainly, because the investigation (§3.6) asks for it to be documented
rather than left to be inferred:

- **A live attacker running as the same user.** On a stock Linux host any
  process of your user can read this app's files and — unless YAMA restricts
  ptrace — its memory, and can simply wait for the vault to be unlocked.
  Encrypting at rest raises the bar against disk theft and stray backups; it
  cannot defeat local malware. That needs the per-app entitlement model
  UltraVault's native backend anticipates, not application-level encryption.
- **X11 screen and input capture.** Any client of the same X server can
  capture window contents and sniff the keyboard. There is no `FLAG_SECURE`
  equivalent: screenshot tools, screen recorders and malware all see the codes
  (30 s of exposure) and, far worse, the provisioning QR during enrolment,
  which is permanent. Prefer the Wayland path; on X11, keep a setup key on
  screen as briefly as possible.
- **Real-time relay phishing.** TOTP does not resist it. Nothing here changes
  that, and this file will not pretend otherwise.
- **A forgotten master password.** There is no recovery path, by design. The
  key is derived from the password and nothing else; losing it loses the
  accounts. That is what *Back up…* is for.

Rotating the master password re-keys the vault in place. It cannot reach a
copy of the old file somebody already took — that copy stays readable with the
old password, as it must.

## Locking

The vault does not stay unlocked for the life of the process. Three things
lock it — the **Lock** button, a period without mouse or keyboard input to
the window (`Preferences::idleLockSeconds`, default 5 minutes, 0 = never),
and minimising the window (`Preferences::lockOnMinimize`, default on) — and
each does the same two things in order: every card is cleared, then
`AccountStore::Lock()` drops the derived key and the decrypted entries while
remembering the vault path. `LockScreenDialog` then sits over the window
until `AccountStore::Unlock()` accepts the master password; it has no Cancel,
ignores Escape and refuses the window manager's close.

Back-off is enforced by the store, not the dialog (`LockPolicy.h`,
`UnlockThrottle`): three failures are free, then each doubles the wait from
2 s up to 5 minutes, and an attempt inside the wait is refused with
`StoreResultCode::TooManyAttempts` *before* the password is checked, so the
refusal costs no Argon2id work and is not an oracle. The dialog polls the
remaining wait once a second to grey its button; if it did not, the store
would still refuse.

The minimise trigger needed the framework to report a user-initiated
minimise, which on X11 it did not (UltraCanvas 0.9.26: `WM_STATE` is watched,
`onWindowMinimize` / `onWindowRestore` fire). The lock screen is put up on
the second tick after a restore rather than the first, because the window
manager can park an un-iconifying window at a temporary position for a few
hundred milliseconds and a dialog centred during that moment lands there.

`Preferences::hideCodes` ("Hide codes until a card is clicked", off by
default) masks every code; a click shows one for 15 s, and a hidden TOTP card
is not computed at all. The three settings are edited in `SettingsDialog` and
kept in `settings.ini` beside the vault (`Preferences.h`): plain
`key = value`, nothing secret, every value clamped on load.

The user-facing statement of all this, including what it does not defend
against, is `Docs/UltraAuthenticator/README.md`.

## Known gaps

There is deliberately **no code verification function** and **no
copy-to-clipboard**. An authenticator displays codes; it never checks them,
and a verifier would need a look-ahead window, single-use enforcement per time
step and constant-time comparison — a materially different security surface.
Copying is a gap rather than a refusal: it would need the ~30 s auto-clear
§3.7 describes, and offering it without that on an X11 clipboard every app can
read, with a manager that persists history, would be worse than not offering
it.
