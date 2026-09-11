// Apps/UltraMail/engine/UltraMailOAuth.h
// OAuth2 sign-in for mail providers that reject the account password over
// IMAP/SMTP (Gmail, Outlook / Microsoft 365). Built on UltraNet's OAuth2 client (authorization code +
// PKCE, loopback redirect): the browser shows the provider's consent page,
// the tokens land in the credential vault, and every IMAP/SMTP session signs
// in with a fresh bearer token (XOAUTH2) — refreshed through the provider
// when the previous one has expired.
//
// Two pieces of configuration:
//   * the provider table (endpoints + scopes) — built in, keyed by a short
//     provider id ("google", "microsoft");
//   * the OAuth *app* UltraMail signs in as (client id, optional secret,
//     loopback redirect URI). That registration belongs to whoever ships the
//     app, so it is configuration, never a literal here: SetOAuthApp(), or
//     ULTRAMAIL_<PROVIDER>_CLIENT_ID / _CLIENT_SECRET / _REDIRECT_URI in the
//     environment, or an INI file in the data folder (OAuthApps::LoadFile).
// Version: 0.2.0 - Microsoft (Outlook / Microsoft 365) beside Google; login hint
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailCredentialVault.h"
#include "UltraMailDiscovery.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetOAuth2.h>

#include <cstdint>
#include <functional>
#include <string>

namespace UltraMail {

// The OAuth app UltraMail signs in as, per provider.
struct OAuthApp {
    std::string clientId;
    std::string clientSecret;    // empty = public client (PKCE only)
    // Loopback redirect. Both providers ignore the port of a loopback URI, so
    // port 0 lets UltraNet bind an ephemeral one per attempt — a cancelled
    // sign-in whose listener is still waiting never blocks a retry. Empty =
    // the provider's default (DefaultRedirectUri).
    std::string redirectUri;

    bool IsConfigured() const { return !clientId.empty(); }
};

// Registry of app registrations. Lookup order: Set() > environment
// (ULTRAMAIL_GOOGLE_CLIENT_ID, ULTRAMAIL_MICROSOFT_CLIENT_ID, ...) > entries
// loaded from the INI file. Get() fills an empty redirectUri with the
// provider's default.
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
    // from `path`. A missing file is not an error (returns 0). Returns the
    // number of providers with a client id.
    static int LoadFile(const std::string& path);
    // The parser behind LoadFile — pure, for tests.
    static int ParseIni(const std::string& text);

    // Forget everything Set() or LoadFile() registered (tests).
    static void Clear();
};

// The provider id UltraMail can sign in to with OAuth2 for a discovered
// account ("google" for Gmail, "microsoft" for Outlook / Microsoft 365), or ""
// when the account uses a password.
std::string OAuthProviderFor(const DiscoveryResult& discovery);
// "Google" / "Microsoft"; the id itself when unknown.
std::string OAuthProviderDisplayName(const std::string& providerId);

// True when the provider rejects the normal account password over IMAP/SMTP
// and a password sign-in needs an *app password* generated in the account's
// security settings: Yahoo and iCloud (which offer no OAuth2 to mail apps),
// and the OAuth2 providers when a password is typed instead of signing in.
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
