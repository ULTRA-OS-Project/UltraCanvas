# UltraPassword

The password vault for ULTRA OS: every password in one encrypted file,
organised in a tree of groups, with a card per password that says how the
website signs you in and how safe that is.

Source: [`Apps/UltraPassword/`](../../Apps/UltraPassword/). Changelog:
[`CHANGELOG.md`](CHANGELOG.md).

## Using it

**First run.** The window opens on a start page built like UltraMail's: the
logo, the name and one button, *Create password vault*. It opens a short
wizard — a name for the vault, the master password twice, a strength meter,
and the key-protection profile — and the vault is created with two empty
groups, *Personal* and *Work*. *Open existing vault…* opens a vault file from
elsewhere instead (an export carried over from another computer).

**Unlocking.** Once a vault exists the start page shows a master-password
field. Three wrong passwords in a row start a wait — 1 s, then 2, 4, … up to a
minute — on top of the cost of the key derivation itself.

**Groups.** The tree on the left shows the vault at the root, its groups —
which nest to any depth — and, under each group, its passwords. A group holds
sub-groups and passwords at the same time. *New group* adds a group inside the
selected one, *Rename* renames it (or the vault, at the root), *Delete group*
removes it with everything inside after saying how much that is.

**Password cards.** Selecting a group lists its passwords on the right as
cards; selecting a password shows its card alone; typing in the search field
searches title, website, user name, notes and single-sign-on provider across
the whole vault (never the passwords themselves). A card shows the website
(with *Open*), the user name (*Copy*), the password masked (*Show*, *Copy*),
notes, the group and the date it last changed. A copied password is cleared
from the clipboard after 30 seconds if it is still there.

**How the site signs you in.** Every password records the website's sign-in
method, shown as two pills at the top of its card — the method and a security
rating — with one sentence on why:

| Method | Rating | Why |
|---|---|---|
| Password only | Weak | Phishable, guessable, reusable after a breach |
| Password + SMS code | Fair | SIM swapping and phishing can take the code |
| Password + e-mail code | Fair | Whoever controls the mailbox gets in |
| E-mail link or code | Fair | No password; the mailbox is the key |
| Password + authenticator app | Good | TOTP stops a leaked password, not a fake login page |
| Single sign-on (Google, Apple, …) | Good | As safe as the account it delegates to |
| Passkey + password fallback | Strong | The password still works, so it is the weak point |
| Password + security key | Very strong | FIDO2 checks the site's address: not phishable |
| Passkey | Very strong | No shared secret, bound to the real site |

The summary line over the cards counts how many passwords in the group rely
on a password alone.

**Adding and editing.** *New password* (or *Edit* on a card) opens a dialog for
title, website, user name, the sign-in method (with the rating and the reason
updating live), the provider for single sign-on, the password with a
*Generate* button and a strength meter, the group, and notes. Every change is
saved to the vault file at once.

**Locking.** *Lock*, five minutes without input to the window, or minimising
it wipes the decrypted vault and the key from memory and returns to the
start page. An open dialog postpones the idle lock.

## Export and import

The vault file **is** the export format. *Export…* offers three choices:

- **Vault file…** — an encrypted copy (`.upwvault`) that opens with the
  current master password. The normal backup, and how a vault moves to
  another computer.
- **Vault file, own password…** — the same format under a separate password,
  for a backup kept apart or a vault handed to someone else.
- **Plain CSV…** — *not encrypted*, for importing into a browser or another
  password manager. Asked for again before anything is written. Columns:
  `name,url,username,password,note,group,signin_method,sso_provider` — the
  first five are what Chrome, Edge, Firefox and Bitwarden read. A cell that
  starts like a spreadsheet formula is prefixed with `'`.

*Import…* reads a `.upwvault` (asking for its password) or a CSV from Chrome,
Edge, Firefox, Bitwarden or UltraPassword itself, into a new group named
*Imported - …*, so nothing already in the vault is overwritten.

## Security

| | |
|---|---|
| Key derivation | Argon2id (RFC 9106). Default *Maximum*: 1 GiB, 4 passes — libsodium's "sensitive" cost, far above OWASP's minimum. *Strong*: 256 MiB, 4 passes, for machines short of memory. Fresh 16-byte salt per vault and per password change. |
| Encryption | XChaCha20-Poly1305, 256-bit key, fresh random 192-bit nonce on every save. |
| Integrity | The 60-byte header (magic, version, algorithms, Argon2id cost, salt, nonce) is the AEAD's associated data: lowering the cost or editing anything is detected. The whole vault is one sealed blob, so entries cannot be dropped or swapped one at a time. |
| Failure modes | No plaintext fallback; an empty password is refused; a wrong password and a modified file give the same message. |
| On disk | Atomic writes (temporary file, flush, rename), mode 0600. |
| In memory | The derived key sits in a locked, wiped `UltraCryptSecureBuffer`; strings holding passwords are overwritten before release on lock, delete and close. |

What it does **not** defend against: malware running as the same user while
the vault is unlocked, which can read the process's memory or the clipboard.

### File format

```
offset size field
0      8    magic "UPWVAULT"
8      1    container version (1)
9      1    KDF id   (1 = Argon2id)
10     1    AEAD id  (1 = XChaCha20-Poly1305)
11     1    reserved (0)
12     4    Argon2id passes          (little-endian)
16     4    Argon2id memory, KiB     (little-endian)
20     16   Argon2id salt
36     24   AEAD nonce
60     ...  ciphertext || 16-byte Poly1305 tag
```

The plaintext is a tagged binary payload (`UPWD`, version 1, then records of
vault info, groups and entries, each a list of `tag, length, bytes` fields).
Readers skip tags and record kinds they do not know, so later versions can
add fields without breaking older readers. See
`Apps/UltraPassword/core/PasswordVault.cpp` and `VaultFile.cpp`.

## Where the vault lives

`$XDG_DATA_HOME/UltraPassword/vault.upwvault`, else
`%APPDATA%\UltraPassword\vault.upwvault` on Windows, else
`~/.local/share/UltraPassword/vault.upwvault`. `--vault PATH` uses another
file.

## Building and testing

The core (`UltraPasswordCore`: model, vault file, CSV, generator) needs only
UltraCrypt (libsodium) and builds headless; the GUI is added when the
framework is built. `-DBUILD_TESTS=ON` adds `UltraPasswordTests`, which checks
the model, the payload round trip, the vault file and its exports, wrong
passwords and tampering, the unlock back-off, CSV in and out (Chrome and
Bitwarden layouts included) and the generator.
