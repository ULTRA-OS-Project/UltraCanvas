- **Signing in with an account.** The relay can sign its users in with an
  OpenID Connect provider (`[signin]`: Keycloak, Entra ID, Google...): the
  relay is the provider's client - PKCE, the ID token's signature, issuer,
  audience, expiry and nonce checked - finds the user by verified email or
  subject, and hands the page a short-lived ticket. **Sign in** appears
  beside the token in the page and in the desktop app, which signs in
  through the system browser.
- **A statement time-out on the relay** (`statement_timeout`, 300 s unless
  set): a query still running then is stopped on the database and the page
  told why.
- **The relay dialog** is titled "Connect to a DB relay", with the address,
  the token and the buttons first and what a relay is under them; in the
  browser it has a title bar to move it by, as every dialog there now has.
