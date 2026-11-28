#### 2026-11-26 *0.1.1*
- **Every text in 29 languages.** The application had no translation, so
  it showed English whatever the language; its whole catalogue is now
  translated into all 29 (machine drafts, reviewed by -; Brazilian
  Portuguese uses the Portuguese).

#### 2026-11-22 *0.1.0*
- **UltraAccount: the one ULTRA OS account, in its own window.** Create an
  account (the address verified with a mailed code), sign in (with the
  authenticator code when two-factor sign-in is on), reset a forgotten
  password, and sign out. Signing in here signs in every application on the
  machine: the session is kept in UltraAccount's data folder, readable by the
  owner only, and the applications that rest on the account read it from
  there.
  - **Phone number**, verified by a texted code. A text message costs money
    to send, so a number needs **UltraAccount Plus**, the paid plan.
  - **Subscription**: the plan the account is on and until when; a licence
    for UltraAccount Plus from the store or the payment page is pasted and
    activated here.
  - **Recovery keys**: the applications that can be recovered with a code to
    the account's email or phone - UltraAuthenticator's forgotten master
    password, first - listed, and removed one by one.
  - `core/UltraAccountClient.h` is the client every application shares
    (`UltraAccountCore`).
