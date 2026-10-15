- **UltraNet signs in through an OAuth2 sign-in service:
  `UltraNetOAuth2Service.h`.** A web service that holds the OAuth client
  registrations - the client id and the secret - and is the public https
  address the providers redirect to, so a desktop application ships no
  secret and a provider that refuses loopback redirect URIs (Yahoo)
  redirects like any other. `UltraNet_OAuth2AuthorizeViaService` is the
  twin of `UltraNet_OAuth2AuthorizeInteractive`: it starts a session at the
  service with the PKCE challenge, hands the consent URL to the browser,
  polls the service for the authorization code and exchanges it through the
  service, which adds the registration; `UltraNet_OAuth2ServiceRefresh`
  refreshes the same way, and the steps and the answer parsers are exposed
  for a caller that runs them itself. The verifier never leaves the
  application. The service itself is `Server/OAuthCallback` (FastAPI, with
  nginx and systemd files and its own tests), deployed for ULTRA OS at
  `https://oauth-callback.ultraos.eu`; UltraMail is its first user. The
  flat-JSON scanner the token parser used moved to the internal
  `UltraNetFlatJson.h`, shared by both; `Tests/UltraNet/FakeHttpServer.h`
  is the fake endpoint the OAuth2 and the service tests serve from.
- **The sign-in service is a setting of UltraNet's OAuth2 app registry,
  and UltraCloud signs in through it.** `UltraNet_OAuth2SignInServiceUrl()`
  resolves the service's address through the registry's tiers - code,
  the environment (`ULTRANET_OAUTH_SERVICE_URL`, and
  `ULTRAMAIL_OAUTH_SERVICE_URL` / `ULTRACLOUD_OAUTH_SERVICE_URL` for the
  modules' prefixes), an INI file's `[service] url`, and the address built
  in by the new `ULTRANET_OAUTH_SERVICE_URL` CMake option (default
  `https://oauth-callback.ultraos.eu`, `cmake/oauth.local.cmake` may set
  it) - so every module in a process reads one setting; `off` turns it
  off. UltraCloud's Google Drive, OneDrive and Dropbox providers go
  through the service when no app of their own is registered (the
  service's `google`, `microsoft` and `dropbox`), where a build without a
  baked-in client could not sign in to them at all before;
  `UsesSignInService` / `CanSignIn` say which way a provider goes, and the
  add-account dialog asks `CanSignIn`. The service serves Dropbox too, and
  OneDrive's `User.Read` scope.
