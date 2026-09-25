// Apps/UltraMail/engine/UltraMailOAuth.h
// OAuth2 sign-in for mail providers that reject the account password over
// IMAP/SMTP (Gmail, Outlook / Microsoft 365, Yahoo). Built on UltraNet's OAuth2 client (authorization code +
// PKCE, loopback redirect — or out-of-band for Yahoo): the browser shows the provider's consent page,
// the tokens land in the credential vault, and every IMAP/SMTP session signs
// in with a fresh bearer token (XOAUTH2) — refreshed through the provider
// when the previous one has expired.
//
// Two pieces of configuration:
//   * the provider table (endpoints + scopes) — built in, keyed by a short
//     provider id ("google", "microsoft", "yahoo");
//   * the OAuth *app* UltraMail signs in as (client id, optional secret,
//     loopback redirect URI). That registration belongs to whoever ships the
//     app. A shipped build bakes it in (compiled as a build-time default — see
//     Apps/UltraMail/CMakeLists.txt and UltraMailOAuthDefaults.h.in) so end
//     users need no config. It can still be overridden at runtime, in
//     decreasing priority: OAuthApps::Set(), then ULTRAMAIL_<PROVIDER>_CLIENT_ID
//     / _CLIENT_SECRET / _REDIRECT_URI in the environment, then an INI file in
//     the data folder (OAuthApps::LoadFile) — all of which win over the
//     baked-in default, which is the escape hatch for rotation. The lookup
//     itself is the framework's (UltraNet's OAuth2 app registry, shared with
//     UltraCloud); OAuthApps is UltraMail's profile of it.
// Version: 0.3.0 - OAuthApps is a profile of UltraNet's shared app registry
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailCredentialVault.h"
#include "UltraMailDiscovery.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetOAuth2.h>
#include <UltraNet/UltraNetOAuth2Apps.h>

#include <cstdint>
#include <functional>
#include <string>

namespace UltraMail {

// The OAuth app UltraMail signs in as, per provider: the framework's type.
using OAuthApp = UltraNetOAuth2App;

// UltraMail's profile of the process-wide registry in UltraNet
// (<UltraNet/UltraNetOAuth2Apps.h>): the same Set() > environment > INI file >
// baked-in order, with "ULTRAMAIL_" as the environment prefix
// (ULTRAMAIL_GOOGLE_CLIENT_ID, ULTRAMAIL_YAHOO_CLIENT_ID, ULTRAMAIL_MICROSOFT_CLIENT_ID, ...) beside the
// shared ULTRANET_OAUTH_ one, the build's baked-in client registered as the
// floor, and an empty redirect URI filled with the provider's default. The
// registry is shared, so a Google client configured here also serves
// UltraCloud's Google Drive sign-in (its "googledrive" falls back to "google").
class OAuthApps {
public:
    static void     Set(const std::string& providerId, const OAuthApp& app);
    static OAuthApp Get(const std::string& providerId);
    static bool     Has(const std::string& providerId);
    // The loopback redirect to register with the provider, port 0:
    // "http://127.0.0.1:0/callback" (Google), "http://127.0.0.1:0/" (Microsoft,
    // which matches loopback URIs on host + path with the port ignored).
    static std::string DefaultRedirectUri(const std::string& providerId);

    // Load "[google]\nclient_id = ...\nclient_secret = ...\nredirect_uri = ..."
    // from `path` into the shared registry. A missing file is not an error
    // (returns 0). Returns the number of providers with a client id.
    static int LoadFile(const std::string& path);
    // The parser behind LoadFile — pure apart from the registry write, for tests.
    static int ParseIni(const std::string& text);

    // Forget everything Set() or LoadFile() registered, in the shared registry
    // (tests). The baked-in client stays.
    static void Clear();

    // Make sure the profile is registered with the shared registry (prefix and
    // baked-in client). Every entry point calls it; an app that reads the
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
// OAuth2 providers when a password is typed instead of signing in. (Yahoo also
// rejects the normal password, but is caught by the OAuth2 flag above now that
// it offers OAuth2 — it no longer takes app passwords at all.)
bool ProviderNeedsAppPassword(const DiscoveryResult& discovery);

// False when the provider takes no password of any kind over IMAP/SMTP any
// more and only the browser sign-in works: Microsoft (Outlook.com and
// Microsoft 365 retired basic authentication). True for everyone else.
bool ProviderAcceptsPassword(const DiscoveryResult& discovery);

// Endpoints, scopes and consent parameters for a provider + app registration.
// `loginHint` (the account's address) preselects the account on the consent
// page; empty = none.
UltraNetOAuth2Config OAuthConfigFor(const std::string& providerId, const OAuthApp& app,
                                    const std::string& loginHint = std::string());

// Test seams: the interactive authorization and the refresh.
struct OAuthHooks {
    std::function<UltraNetResult(const UltraNetOAuth2Config&,
                                 const std::function<void(const std::string&)>& openUrl,
                                 UltraNetOAuth2Token&)> authorize;
    std::function<UltraNetResult(const UltraNetOAuth2Config&, const std::string& refreshToken,
                                 UltraNetOAuth2Token&)> refresh;
};

class MailOAuth {
public:
    explicit MailOAuth(OAuthHooks hooks = {});

    // Browser sign-in: hands the consent URL to `openUrl`, waits for the
    // loopback redirect and exchanges the code. Blocking for as long as the
    // user takes (up to the UltraNet timeout) — run on a worker thread.
    // Fails with PluginNotFound-style Unsupported when no app is configured.
    // `email` preselects the account on the consent page.
    UltraNetResult SignIn(const std::string& providerId, const std::string& email,
                          const std::function<void(const std::string& url)>& openUrl,
                          OAuthTokens& out, int64_t now = 0);

    // Out-of-band sign-in, in two steps, for providers whose redirect is "oob"
    // (Yahoo): the loopback capture can't be used, so the user copies a code
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
    // provider when it has expired. `refreshed` is set when the set changed and
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
