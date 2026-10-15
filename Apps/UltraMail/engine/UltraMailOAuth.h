// Apps/UltraMail/engine/UltraMailOAuth.h
// OAuth2 sign-in for mail providers that reject the account password over
// IMAP/SMTP (Gmail, Outlook / Microsoft 365, Yahoo). The browser shows the
// provider's consent page, the tokens land in the credential vault, and every
// IMAP/SMTP session signs in with a fresh bearer token (XOAUTH2) - refreshed
// through the provider when the previous one has expired.
//
// Two ways to run the flow:
//   * through the ULTRA OS *sign-in service* (the default -
//     https://oauth-callback.ultraos.eu, Server/OAuthCallback in the
//     repository): the service holds the OAuth client registrations and their
//     secrets and is the public https address the providers redirect to. The
//     application carries no client id and no secret, only the service's
//     address, and Yahoo - which refuses loopback redirect URIs - redirects
//     like every other provider. PKCE stays end to end: the verifier never
//     leaves the application (UltraNet/UltraNetOAuth2Service.h).
//   * directly, as an OAuth client of the provider's (UltraNet's OAuth2
//     client, authorization code + PKCE, loopback redirect - or a pasted code
//     for Yahoo): when a client id is configured for the provider at run
//     time, through OAuthApps::Set(), ULTRAMAIL_<PROVIDER>_CLIENT_ID /
//     _CLIENT_SECRET / _REDIRECT_URI in the environment, or an INI file in the
//     data folder (OAuthApps::LoadFile). Such a client bypasses the service
//     for its provider - the escape hatch for a private registration or a
//     build with no service. The lookup is the framework's (UltraNet's OAuth2
//     app registry, shared with UltraCloud); OAuthApps is UltraMail's profile.
//
// Where the service is: OAuthApps::SetServiceUrl() > ULTRAMAIL_OAUTH_SERVICE_URL
// in the environment > "[service] url =" in oauth.ini > the address baked into
// the build (Apps/UltraMail/CMakeLists.txt, UltraMailOAuthDefaults.h.in). The
// value "off" (or "none") at any tier turns the service off.
// Version: 0.6.0 - the sign-in service: no client secret in the application, Yahoo redirects
// Version: 0.5.0 - Yahoo takes an app password again, beside the browser sign-in
// Version: 0.4.0 - ExtractOAuthCode, OAuthUsesPastedCode (https redirects pasted back)
// Version: 0.3.0 - OAuthApps is a profile of UltraNet's shared app registry
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailCredentialVault.h"
#include "UltraMailDiscovery.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetOAuth2.h>
#include <UltraNet/UltraNetOAuth2Apps.h>
#include <UltraNet/UltraNetOAuth2Service.h>

#include <cstdint>
#include <functional>
#include <string>

namespace UltraMail {

// The OAuth app UltraMail signs in as, per provider: the framework's type.
using OAuthApp = UltraNetOAuth2App;

// UltraMail's profile of the process-wide registry in UltraNet
// (<UltraNet/UltraNetOAuth2Apps.h>): the same Set() > environment > INI file
// order, with "ULTRAMAIL_" as the environment prefix (ULTRAMAIL_GOOGLE_CLIENT_ID,
// ULTRAMAIL_YAHOO_CLIENT_ID, ULTRAMAIL_MICROSOFT_CLIENT_ID, ...) beside the
// shared ULTRANET_OAUTH_ one, and an empty redirect URI filled with the
// provider's default. The registry is shared, so a Google client configured
// here also serves UltraCloud's Google Drive sign-in (its "googledrive" falls
// back to "google"). Beside the registry, OAuthApps knows the sign-in service:
// a provider with no client of its own signs in through it.
// True when the sign-in for `redirectUri` cannot be caught by the loopback
// listener and the user pastes the result back instead: "oob" (the consent
// page shows a code) or an https redirect such as Thunderbird's Yahoo
// registration "https://127.0.0.1" (the listener speaks plain HTTP, so the
// browser lands on an error page whose address carries the code).
bool OAuthUsesPastedCode(const std::string& redirectUri);

// The authorization code in what the user pasted: the bare code, or a whole
// redirect address ("https://127.0.0.1/?code=...&state=...") copied from the
// browser's address bar, whose `code` parameter is taken and percent-decoded.
// Surrounding whitespace is trimmed. "" when nothing usable was pasted.
std::string ExtractOAuthCode(const std::string& pasted);

class OAuthApps {
public:
    // The client registered at run time for a provider (Set > environment >
    // INI). Unconfigured when there is none - the provider then signs in
    // through the service, if there is one (UsesService).
    static void     Set(const std::string& providerId, const OAuthApp& app);
    static OAuthApp Get(const std::string& providerId);
    // True when UltraMail can sign in to the provider at all: a client of its
    // own, or the service for one of the providers the service serves.
    static bool     Has(const std::string& providerId);
    // The loopback redirect to register with the provider, port 0:
    // "http://127.0.0.1:0/callback" (Google), "http://127.0.0.1:0/" (Microsoft,
    // which matches loopback URIs on host + path with the port ignored).
    static std::string DefaultRedirectUri(const std::string& providerId);

    // ---- the sign-in service --------------------------------------------
    // Where the service is, resolved as the header says: Set > environment >
    // INI > baked in. "" when there is none (or it was turned off).
    static std::string ServiceUrl();
    // Name a service from code (tests, a settings page): "" drops the
    // override, "off" turns the service off whatever the other tiers say.
    static void SetServiceUrl(const std::string& url);
    // True when `providerId` signs in through the service: no client of its
    // own is configured, the service is on, and it is one of the providers
    // the service serves (IsServiceProvider).
    static bool UsesService(const std::string& providerId);
    // "google", "microsoft", "yahoo": the providers the service signs in to.
    static bool IsServiceProvider(const std::string& providerId);

    // Load "[google]\nclient_id = ...\nclient_secret = ...\nredirect_uri = ..."
    // and "[service]\nurl = ..." from `path` into the shared registry and the
    // service tier. A missing file is not an error (returns 0). Returns the
    // number of providers with a client id.
    static int LoadFile(const std::string& path);
    // The parser behind LoadFile - pure apart from the registry write, for tests.
    static int ParseIni(const std::string& text);

    // Forget everything Set(), SetServiceUrl() or LoadFile() registered, in
    // the shared registry and the service tiers (tests). The environment and
    // the baked-in service stay.
    static void Clear();

    // Make sure the profile is registered with the shared registry (the
    // environment prefix). Every entry point calls it; an app that reads the
    // registry through UltraNet directly may call it once at start-up.
    static void EnsureRegistered();
};

// The provider id UltraMail can sign in to with OAuth2 for a discovered account
// ("google" for Gmail, "microsoft" for Outlook / Microsoft 365, "yahoo" for
// Yahoo Mail), or "" when the account uses a password.
std::string OAuthProviderFor(const DiscoveryResult& discovery);
// "Google" / "Microsoft" / "Yahoo"; the id itself when unknown.
std::string OAuthProviderDisplayName(const std::string& providerId);

// True when the provider rejects the normal account password over IMAP/SMTP
// and a password sign-in needs an *app password* generated in the account's
// security settings: iCloud (which offers no OAuth2 to mail apps), and the
// OAuth2 providers when a password is typed instead of signing in, Yahoo
// among them.
bool ProviderNeedsAppPassword(const DiscoveryResult& discovery);

// False when the provider takes no password of any kind over IMAP/SMTP any
// more and only the browser sign-in works: Microsoft (Outlook.com and
// Microsoft 365 retired basic authentication) and Google. True for everyone
// else, Yahoo included: its browser sign-in needs Mail API access Yahoo grants
// per app, so an app password is the fallback while that is not in place.
bool ProviderAcceptsPassword(const DiscoveryResult& discovery);

// Endpoints, scopes and consent parameters for a provider + app registration
// (the direct flow). `loginHint` (the account's address) preselects the
// account on the consent page; empty = none.
UltraNetOAuth2Config OAuthConfigFor(const std::string& providerId, const OAuthApp& app,
                                    const std::string& loginHint = std::string());

// The service, the provider and the hint for a sign-in through the service
// (OAuthApps::ServiceUrl()). The scopes are the service's defaults for the
// provider - the mail scopes, the twin of OAuthConfigFor's.
UltraNetOAuth2ServiceConfig OAuthServiceConfigFor(const std::string& providerId,
                                                  const std::string& loginHint = std::string());

// Test seams: the interactive authorization and the refresh, directly
// (authorize, refresh) and through the service (serviceAuthorize,
// serviceRefresh). Each defaults to the UltraNet call.
struct OAuthHooks {
    std::function<UltraNetResult(const UltraNetOAuth2Config&,
                                 const std::function<void(const std::string&)>& openUrl,
                                 UltraNetOAuth2Token&)> authorize;
    std::function<UltraNetResult(const UltraNetOAuth2Config&, const std::string& refreshToken,
                                 UltraNetOAuth2Token&)> refresh;
    std::function<UltraNetResult(const UltraNetOAuth2ServiceConfig&,
                                 const std::function<void(const std::string&)>& openUrl,
                                 UltraNetOAuth2Token&)> serviceAuthorize;
    std::function<UltraNetResult(const UltraNetOAuth2ServiceConfig&, const std::string& refreshToken,
                                 UltraNetOAuth2Token&)> serviceRefresh;
};

class MailOAuth {
public:
    explicit MailOAuth(OAuthHooks hooks = {});

    // Browser sign-in: hands the consent URL to `openUrl`, waits for the
    // redirect (at the service, or on the loopback listener when the provider
    // has a client of its own) and exchanges the code. Blocking for as long
    // as the user takes (up to the UltraNet timeout) - run on a worker thread.
    // Fails with InvalidState when neither a client nor the service is
    // configured. `email` preselects the account on the consent page.
    UltraNetResult SignIn(const std::string& providerId, const std::string& email,
                          const std::function<void(const std::string& url)>& openUrl,
                          OAuthTokens& out, int64_t now = 0);

    // Out-of-band sign-in, in two steps, for a provider with a client of its
    // own whose redirect is "oob" (Yahoo, signed in directly): the loopback
    // capture can't be used, so the user copies a code
    // from the consent page. BeginOob builds the consent URL and returns the
    // PKCE verifier to carry to step two; the caller opens the URL and collects
    // the pasted code. CompleteOob exchanges that code for tokens. Splitting the
    // flow lets the UI prompt for the code between the two calls.
    UltraNetResult BeginOob(const std::string& providerId, const std::string& email,
                            std::string& consentUrlOut, std::string& codeVerifierOut);
    UltraNetResult CompleteOob(const std::string& providerId, const std::string& code,
                               const std::string& codeVerifier, OAuthTokens& out,
                               int64_t now = 0);

    // Make sure `tokens` carries a usable access token, refreshing through the
    // provider - via the service, or directly - when it has expired. `refreshed` is set when the set changed and
    // should be written back to the vault. Blocking on a refresh (one HTTPS
    // request) — run off the UI thread.
    UltraNetResult EnsureFresh(const std::string& providerId, OAuthTokens& tokens,
                               bool& refreshed, int64_t now = 0);

    // The credentials an IMAP/SMTP session for an account should use: the
    // stored password, or — for an account that signed in with OAuth2 — a
    // fresh bearer token (XOAUTH2), refreshed and stored again when needed.
    // Blocking on a refresh; run off the UI thread. The vault must be open.
    UltraNetResult CredentialsFor(CredentialVault& vault, const std::string& accountId,
                                  const std::string& email, const std::string& providerId,
                                  UltraNetCredentials& out, int64_t now = 0);

    // OAuthTokens from a token-endpoint answer. Keeps `previous`'s refresh
    // token when the provider issued none (Google only sends it once).
    static OAuthTokens FromToken(const UltraNetOAuth2Token& token, int64_t now,
                                 const OAuthTokens& previous = {});

private:
    OAuthHooks hooks_;
};

} // namespace UltraMail
