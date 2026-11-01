- **An e-mail address and a phone number for every ChatCube ID.** A new ID
  asks for both: they are how the account is recovered, kept in the
  identity's encrypted key vault on this computer and on the relay, for the
  owner alone - contacts see the name, never these.
- **A password, an UltraAuthenticator code, or both to sign in.** The
  sign-in card asks how signing in on this computer is protected: no
  password (as before), a password - the key to the ID is then encrypted
  with it (Argon2id, XChaCha20-Poly1305) and cannot be read without it - a
  6-digit code from UltraAuthenticator or any other authenticator app, or
  the password and the code. A protected ID asks for them at every start,
  the password first, then the code; each code works once, and after five
  wrong ones ChatCube waits before it takes another.
- **Edit profile.** The person button beside your ID opens a window with
  the name, the e-mail address and the phone number, and how signing in is
  protected. Changing the protection asks for what protects it now; a new
  authenticator is a QR code (and its key, for typing) that
  UltraAuthenticator scans from the screen, proven with the first code it
  shows.
