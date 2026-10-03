#### 2026-10-03 *0.1.0*
- **First release of UltraPassword, the password vault.** Passwords live in
  one encrypted file (`vault.upwvault`): Argon2id key derivation from the
  master password at 1 GiB / 4 passes by default (256 MiB optional), and
  XChaCha20-Poly1305 over the whole vault with the header — cost included —
  authenticated. No plaintext fallback, atomic owner-only writes, the key
  wiped on lock.
- **First run looks like UltraMail's**: a start page with the logo, the name
  and one "Create password vault" button that opens a short wizard (vault
  name, master password twice, a strength meter, the key-protection profile).
  An existing vault opens to the same page with a password field instead.
- **Groups in a tree.** The left pane is a tree of groups; a group holds
  sub-groups and passwords at the same time. The right pane shows a card per
  password in the selected group, or the search results.
- **Every card says how the site signs you in** — password only, password
  plus an authenticator app / SMS / e-mail code / security key, passkey,
  passkey with a password fallback, single sign-on (with the provider) or an
  e-mail link — with a security rating from Weak to Very strong and one
  sentence on why.
- **Export is the vault file itself**: *Export vault…* writes a copy that
  opens with the master password, or under a separate export password.
  *Import…* reads such a file, or a CSV from Chrome, Edge, Firefox or
  Bitwarden, into a new group. A plain CSV export exists for moving to
  another manager and asks for confirmation first.
- Password generator (OS CSPRNG, every character class, no look-alikes),
  copy to the clipboard with automatic clearing after 30 seconds, auto-lock
  after 5 minutes without input and on minimise, and a growing wait after
  repeated wrong master passwords.
