# UltraAuthenticator

## Overview

UltraAuthenticator generates the six- or eight-digit sign-in codes (TOTP and
HOTP, the same codes Google Authenticator, Aegis and 2FAS produce) for
accounts you enrol by scanning a QR code or typing a setup key. It ships as
`Apps/UltraAuthenticator`, is built from UltraCanvas elements, works fully
offline, and keeps every account in one encrypted file that only your master
password opens.

This page is for the person using it. It says what the app protects, how,
and — at least as important — what it cannot protect against, so that nobody
relies on it for something it does not do. The engineering behind each
statement is in the
[feasibility investigation](UltraAuthenticator-Investigation.md); the
cryptography is [UltraCrypt](../Modules/UltraCrypt/README.md).

- Version: its own, from the first line of
  [`Docs/UltraAuthenticator/CHANGELOG.md`](CHANGELOG.md). `--version`
  prints it.
- Files: `accounts.vault` and `settings.ini` under
  `$XDG_DATA_HOME/UltraAuthenticator/` (`~/.local/share/UltraAuthenticator/`
  by default). `--vault PATH` uses another vault file; its settings live
  beside it.

## What the secret is

When a service shows you a QR code to enrol, the QR contains a **seed**: a
random key the service also keeps. Every code the app ever shows is computed
from that seed and the current time (or, for HOTP, a counter). That makes the
seed *the second factor itself*: anyone who copies it once can produce your
codes forever, without your phone, your password or your machine. Everything
below follows from treating the seed as the thing to protect, not the codes.

## How the app protects it

**On disk.** All accounts sit in one file, encrypted with
XChaCha20-Poly1305. The key is derived from your master password with
Argon2id, whose cost parameters and salt are stored in the file header — and
the header is authenticated along with the data, so a downgraded cost
parameter or an edited header is detected, not obeyed. The file is written
with owner-only permissions (`0600`) and replaced atomically, so a crash
mid-save leaves the previous vault, never a half-written one. Nothing about
the accounts — not the seeds, not the names, not the services — is readable
without the password; the file is one opaque block.

A wrong password and a tampered file are reported with the *same* message,
on purpose. Telling them apart would tell an attacker which of the two they
had achieved.

**There is no unprotected mode.** The app refuses to start without a crypto
backend and refuses to open a vault without a password. It never falls back
to storing seeds in the clear.

**In memory.** A seed is read out of the vault, used to compute one code, and
wiped, inside a single call; it does not sit in memory while the window is
open. Secrets live in buffers that are zeroed on release and, where the
platform allows, locked against being paged to swap. The window that shows
codes never holds a seed at all, with one deliberate exception (*Show key*,
below).

**Locking.** The vault locks — every code disappears from the screen and the
decrypted vault is dropped from memory — when you press **Lock**, after a
period without input to the window (5 minutes by default, or never; see
*Settings*), and when the window is minimised. The lock screen accepts only
the master password or *Quit*; it cannot be closed or escaped. After three
wrong passwords each further attempt waits twice as long as the last, up to
five minutes, and the wait is enforced by the vault code, not by the
dialog. The same screen, with the same back-off, is what opens the app: a
mistyped password at launch is retried there, not punished with a restart.

**Showing a code only on request.** *Settings → Hide codes until a card is
clicked* replaces every code with dots; clicking a card shows its code for
15 seconds. Hidden cards are not even computed. This is for working where
someone may be looking over your shoulder.

**Getting a seed back out.** Moving to a new phone means reading the seed
again, and an app that could never do that would push people into keeping
the original QR code in a photo album — far worse than this vault. So *Show
key* exists, and it is fenced: it asks for the master password again even
though the vault is open, shows the key and the `otpauth://` URI as text on
screen only, renders no QR code and writes nothing to disk, and wipes its
own widgets when closed.

**Backups.** *Back up…* writes every account to one encrypted file, sealed
with a passphrase that **must differ from the master password** — a backup
is the file most likely to reach a USB stick or a cloud folder, and one that
opened with your device password would make finding it as good as having
the machine. Export asks for the master password first. *Restore…* never
overwrites: an account already in the vault is kept and reported as skipped,
because a restore that replaced a re-enrolled seed or a newer HOTP counter
would destroy a working second factor.

**Untrusted input.** Scanned QR codes and typed keys go through the same
strict parser; a malformed seed is refused rather than silently turned into
a different one. Camera frames are decoded in memory and never written to
disk as images.

## What it does not protect against

Be clear-eyed about these. None of them is a defect the app can fix on its
own; each is stated here so the app is not credited with more than it does.

- **Anyone who can see your screen.** Under X11, any program running in
  your session can capture window contents and read keystrokes. Codes are
  visible for up to 30 seconds while shown; the setup key is visible for as
  long as *Show key* is open. Screenshot tools, screen recorders and malware
  in the same session see what you see. Prefer Wayland or a native ULTRA OS
  build, keep *Show key* open only as long as the other device needs, and
  use *Hide codes* on a shared screen.
- **Other programs running as you.** On a standard Linux host, a process
  running under your user account can read your files and, unless the
  kernel restricts `ptrace`, your programs' memory. The encryption defends
  the vault file against theft, backups and disk forensics; it does **not**
  defeat malware already running as you while the vault is unlocked. That
  needs the per-application isolation ULTRA OS is built for, which this app
  is designed to take advantage of but cannot provide by itself.
- **A compromised machine in general.** A keylogger captures the master
  password. A modified copy of the app can do anything. Verify the build you
  run.
- **Phishing.** A one-time code typed into a fake sign-in page is relayed
  to the real one in seconds. Time-based codes do not resist this; only
  phishing-resistant methods (hardware keys, passkeys) do. Check the address
  bar before you type a code.
- **A lost master password.** It is not stored anywhere and cannot be
  recovered. Without it, the vault is unreadable — by you and by everyone
  else. Keep a backup (encrypted, with its own passphrase) somewhere safe.
- **A wrong clock.** Time-based codes are computed from the system clock. If
  the clock is minutes off, every code is rejected by the service and the
  app cannot tell. Keep the system time synchronised.
- **Text you type into a password field.** The framework's text field keeps
  its own copy of what was typed until it is cleared; the app clears its
  fields after every use, but that copy is the widget's, not the app's.

## Settings

Three options, in *Settings…*, kept in `settings.ini` beside the vault. The
file is plain `key = value` text and contains nothing secret; hand-edited
values are clamped, never trusted.

| Setting | Default | Meaning |
|---|---|---|
| Lock after no input for | 5 minutes | Lock when the window has had no mouse or keyboard input for this long. *Never* disables it; the Lock button and the minimise lock still work. |
| Lock when the window is minimised | on | Lock the moment the window is minimised, so nothing is on the cards when it comes back. |
| Hide codes until a card is clicked | off | Show dots instead of codes; a click shows one code for 15 seconds. |

## Everyday advice

- Enrol by scanning where you can; typing a 32-character key is the
  fallback.
- Take a backup after enrolling anything important, with a passphrase you
  will still know in a year. Keep it off cloud folders unless you accept
  that the passphrase is then the only thing protecting it.
- Use *Show key* only to enrol another device, and close it as soon as that
  device shows the same code.
- Lock the app when you leave the desk. The button is at the top right for
  exactly that moment.
