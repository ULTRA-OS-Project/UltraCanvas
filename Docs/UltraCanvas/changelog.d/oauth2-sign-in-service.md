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
