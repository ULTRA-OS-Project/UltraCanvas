- **Signed and encrypted mail: S/MIME and OpenPGP.** Mail on the computer is
  now exactly as readable as it was on its way to the server: encrypted mail
  stays encrypted on disk and is decrypted only for the reading pane.
  - Reading: S/MIME (signed, opaque-signed, encrypted) and OpenPGP (PGP/MIME
    signed and encrypted, inline and clear-signed PGP) are decrypted where a
    key here opens them and their signatures checked; pills say what holds -
    *Encrypted*, *S/MIME* / *OpenPGP* green (vouched for), amber (holds,
    nobody vouches), red (changed after signing). An encrypted message no key
    opens says why instead of showing ciphertext.
  - Sending: the compose window's *Security* row (Off, S/MIME, OpenPGP; Sign,
    Encrypt). Encrypted to every recipient and to the sender; Drafts, the send
    and Sent use the same secured bytes; a missing recipient certificate or
    key is named before anything is queued, and a message that cannot be
    secured is never sent in plain text. Replies to encrypted mail start
    encrypted.
  - Keys: an S/MIME certificate and key imported from a `.p12` / `.pfx` in
    *Account Settings* (kept in the vault; only for the account's address);
    correspondents' certificates learnt from their vouched-for signatures
    (address book schema 4); OpenPGP through the user's GnuPG keyring, with
    `application/pgp-keys` attachments offered for import.
  - Built with OpenSSL 3 (S/MIME) and GPGME (OpenPGP) where found; the
    outbox gains schema 5 (the draft's security choice).
  - `test_securemime` checks the MIME pieces, S/MIME signing (untrusted,
    tampered, LF line ends), encrypting to recipient and sender, PKCS #12
    import, the outbox filing only ciphertext, and OpenPGP signing,
    encrypting and inline messages against a throw-away keyring.
- The *S/MIME* / *OpenPGP* pill no longer says UltraMail does not check such
  signatures.
- **OpenPGP on macOS.** The macOS suite bundles GPGME (built by vcpkg for
  macOS 14, with libassuan and libgpg-error); GnuPG is the user's, from
  Homebrew (`brew install gnupg pinentry-mac`) or GPG Suite, and UltraMail
  finds it in their folders although a Finder-started app's `PATH` lacks them.
