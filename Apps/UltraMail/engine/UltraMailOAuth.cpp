// Apps/UltraMail/engine/UltraMailOAuth.cpp
// Version: 0.6.0 - the sign-in service: no client secret in the application, Yahoo redirects
// Version: 0.5.0 - Yahoo takes an app password again, beside the browser sign-in
// Version: 0.4.0 - ExtractOAuthCode, OAuthUsesPastedCode (https redirects pasted back)
// Version: 0.3.0 - OAuthApps is a profile of UltraNet's shared OAuth2 app registry
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailOAuth.h"

#include <cctype>
#include <cstdlib>
#include <ctime>

namespace UltraMail {

// ===== OAuthApps ==============================================================

void OAuthApps::EnsureRegistered() {
    static const bool once = [] {
        UltraNet_OAuth2AddAppEnvPrefix("ULTRAMAIL_");
        return true;
    }();
    (void)once;
}

void OAuthApps::Set(const std::string& providerId, const OAuthApp& app) {
    EnsureRegistered();
    UltraNet_OAuth2SetApp(providerId, app);
}

std::string OAuthApps::DefaultRedirectUri(const std::string& providerId) {
    // Yahoo forces https redirect URIs, which the loopback listener cannot serve
    // (it speaks plain HTTP). Yahoo's documented answer for a desktop client is
    // the out-of-band flow: the consent page shows a code the user pastes back.
    // (Through the service Yahoo redirects to the service's https address and
    // none of this applies.)
    if (providerId == "yahoo")     return "oob";
    if (providerId == "microsoft") return "http://127.0.0.1:0/";
    return "http://127.0.0.1:0/callback";
}

OAuthApp OAuthApps::Get(const std::string& providerId) {
    EnsureRegistered();
    OAuthApp app = UltraNet_OAuth2GetApp(providerId);
    if (!app.IsConfigured()) return OAuthApp{};
    if (app.redirectUri.empty()) app.redirectUri = DefaultRedirectUri(providerId);
    return app;
}

bool OAuthApps::IsServiceProvider(const std::string& providerId) {
    return providerId == "google" || providerId == "microsoft" || providerId == "yahoo";
}

std::string OAuthApps::ServiceUrl() {
    EnsureRegistered();   // the ULTRAMAIL_OAUTH_SERVICE_URL prefix
    return UltraNet_OAuth2SignInServiceUrl();
}

void OAuthApps::SetServiceUrl(const std::string& url) {
    UltraNet_OAuth2SetSignInService(url);
}

bool OAuthApps::UsesService(const std::string& providerId) {
    if (!IsServiceProvider(providerId)) return false;
    if (Get(providerId).IsConfigured()) return false;   // a client of its own wins
    return !ServiceUrl().empty();
}

bool OAuthApps::Has(const std::string& providerId) {
    return Get(providerId).IsConfigured() || UsesService(providerId);
}

int OAuthApps::ParseIni(const std::string& text) {
    EnsureRegistered();
    return UltraNet_OAuth2ParseAppsIni(text);   // [service] url included
}

int OAuthApps::LoadFile(const std::string& path) {
    EnsureRegistered();
    return UltraNet_OAuth2LoadAppsFile(path);
}

void OAuthApps::Clear() {
    UltraNet_OAuth2ClearApps();
}

// ===== Providers ==============================================================

std::string OAuthProviderFor(const DiscoveryResult& discovery) {
    // Detect the provider from its (unambiguous) IMAP host or display name, not
    // from a stored oauth flag: the flag can be lost when an account's settings
    // are edited and re-saved, but imap.gmail.com is always Google. Resolving a
    // password Gmail account to "google" is harmless — CredentialsFor still
    // uses its password when no OAuth token is stored.
    if (!discovery.found) return "";
    if (discovery.imap.host == "imap.gmail.com" || discovery.displayName == "Gmail")
        return "google";
    if (discovery.imap.host == "outlook.office365.com" || discovery.displayName == "Outlook")
        return "microsoft";
    if (discovery.imap.host == "imap.mail.yahoo.com" || discovery.displayName == "Yahoo")
        return "yahoo";
    return "";
}

std::string OAuthProviderDisplayName(const std::string& providerId) {
    if (providerId == "google")    return "Google";
    if (providerId == "microsoft") return "Microsoft";
    if (providerId == "yahoo")     return "Yahoo";
    return providerId;
}

bool ProviderNeedsAppPassword(const DiscoveryResult& discovery) {
    if (!discovery.found) return false;
    if (discovery.imap.oauth) return true;   // Gmail, Outlook: a typed password must be an app password
    return discovery.displayName == "Yahoo" || discovery.displayName == "iCloud";
}

bool ProviderAcceptsPassword(const DiscoveryResult& discovery) {
    auto provider = OAuthProviderFor(discovery);
    // Yahoo takes both: the browser sign-in, or an app password (Account
    // security > Generate app password). The app password is the fallback
    // while the baked-in Yahoo client lacks Mail API access, which Yahoo
    // grants per app, not in the developer console.
    return provider != "microsoft" && provider != "google";
}

UltraNetOAuth2Config OAuthConfigFor(const std::string& providerId, const OAuthApp& app,
                                    const std::string& loginHint) {
    UltraNetOAuth2Config cfg;
    cfg.clientId     = app.clientId;
    cfg.clientSecret = app.clientSecret;
    cfg.redirectUri  = app.redirectUri.empty() ? OAuthApps::DefaultRedirectUri(providerId)
                                               : app.redirectUri;
    cfg.usePkce      = true;
    // Both providers preselect the account named in login_hint, so the user
    // is not asked to pick one they already typed.
    if (!loginHint.empty()) cfg.extraAuthParams["login_hint"] = loginHint;
    if (providerId == "microsoft") {
        // Microsoft identity platform, "common" tenant: personal Microsoft
        // accounts (outlook.com, hotmail.com, ...) and every Microsoft 365
        // organisation. Registered as a public client ("Mobile and desktop
        // applications", "Allow public client flows"), so no secret.
        cfg.authorizationEndpoint = "https://login.microsoftonline.com/common/oauth2/v2.0/authorize";
        cfg.tokenEndpoint         = "https://login.microsoftonline.com/common/oauth2/v2.0/token";
        // The Outlook resource scopes IMAP and SMTP XOAUTH2 accept;
        // offline_access is what yields a refresh token.
        cfg.scopes = {"https://outlook.office.com/IMAP.AccessAsUser.All",
                      "https://outlook.office.com/SMTP.Send",
                      "offline_access"};
        cfg.extraAuthParams["prompt"] = "select_account";
        // A secret, when one was registered anyway, goes in the form body.
        cfg.secretInBody = true;
    }
    if (providerId == "google") {
        cfg.authorizationEndpoint = "https://accounts.google.com/o/oauth2/v2/auth";
        cfg.tokenEndpoint         = "https://oauth2.googleapis.com/token";
        // The full-mailbox scope is what IMAP/SMTP XOAUTH2 requires.
        cfg.scopes = {"https://mail.google.com/"};
        // A refresh token is only issued for offline access and — on a repeat
        // consent — only when consent is asked for again.
        cfg.extraAuthParams["access_type"] = "offline";
        cfg.extraAuthParams["prompt"]      = "consent";
        // Google's "desktop app" clients carry a secret that it expects in the
        // form body, not as HTTP Basic.
        cfg.secretInBody = true;
    }
    if (providerId == "yahoo") {
        cfg.authorizationEndpoint = "https://api.login.yahoo.com/oauth2/request_auth";
        cfg.tokenEndpoint         = "https://api.login.yahoo.com/oauth2/get_token";
        // No scope parameter: Yahoo rejects OAuth2 scope strings on the auth
        // request (a "mail-w"-style value comes back error=invalid_scope) and
        // instead grants whatever API Permissions are enabled on the app, so
        // Mail (read/write) must be ticked in the Yahoo console. scopes stays
        // empty → BuildAuthUrl omits the scope param entirely.
        //
        // Public (installed) client: no secret, so PKCE is the whole security
        // story. redirectUri is "oob" (filled from the app above): Yahoo forces
        // https redirects, which the loopback listener cannot serve, so the
        // consent page shows a code the user pastes back instead.
        cfg.usePkce = true;
    }
    return cfg;
}

UltraNetOAuth2ServiceConfig OAuthServiceConfigFor(const std::string& providerId,
                                                  const std::string& loginHint) {
    UltraNetOAuth2ServiceConfig cfg;
    cfg.serviceUrl = OAuthApps::ServiceUrl();
    cfg.providerId = providerId;
    cfg.loginHint  = loginHint;
    // No scopes: the service's defaults for the provider are the mail scopes
    // OAuthConfigFor asks for (and no scope at all for Yahoo).
    cfg.clientName = "UltraMail";
    return cfg;
}

bool OAuthUsesPastedCode(const std::string& redirectUri) {
    return redirectUri == "oob" || redirectUri.rfind("https://", 0) == 0;
}

std::string ExtractOAuthCode(const std::string& pasted) {
    std::size_t a = 0, b = pasted.size();
    while (a < b && std::isspace(static_cast<unsigned char>(pasted[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(pasted[b - 1]))) --b;
    const std::string s = pasted.substr(a, b - a);

    // A redirect address: take its code parameter (query or fragment).
    std::size_t at = std::string::npos;
    for (std::size_t p = s.find("code="); p != std::string::npos; p = s.find("code=", p + 1)) {
        if (p > 0 && (s[p - 1] == '?' || s[p - 1] == '&' || s[p - 1] == '#')) { at = p + 5; break; }
    }
    if (at == std::string::npos)
        return s.find("://") == std::string::npos ? s : std::string();   // a URL without a code
    const std::size_t end = s.find_first_of("&#", at);
    const std::string raw = s.substr(at, end == std::string::npos ? std::string::npos : end - at);

    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '%' && i + 2 < raw.size()
            && std::isxdigit(static_cast<unsigned char>(raw[i + 1]))
            && std::isxdigit(static_cast<unsigned char>(raw[i + 2]))) {
            out += static_cast<char>(std::stoi(raw.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else if (raw[i] == '+') {
            out += ' ';
        } else {
            out += raw[i];
        }
    }
    return out;
}

// ===== MailOAuth ==============================================================

MailOAuth::MailOAuth(OAuthHooks hooks) : hooks_(std::move(hooks)) {
    if (!hooks_.authorize)
        hooks_.authorize = [](const UltraNetOAuth2Config& cfg,
                              const std::function<void(const std::string&)>& openUrl,
                              UltraNetOAuth2Token& out) {
            return UltraNet_OAuth2AuthorizeInteractive(cfg, openUrl, out);
        };
    if (!hooks_.refresh)
        hooks_.refresh = [](const UltraNetOAuth2Config& cfg, const std::string& refreshToken,
                            UltraNetOAuth2Token& out) {
            return UltraNet_OAuth2Refresh(cfg, refreshToken, out);
        };
    if (!hooks_.serviceAuthorize)
        hooks_.serviceAuthorize = [](const UltraNetOAuth2ServiceConfig& cfg,
                                     const std::function<void(const std::string&)>& openUrl,
                                     UltraNetOAuth2Token& out) {
            return UltraNet_OAuth2AuthorizeViaService(cfg, openUrl, out);
        };
    if (!hooks_.serviceRefresh)
        hooks_.serviceRefresh = [](const UltraNetOAuth2ServiceConfig& cfg,
                                   const std::string& refreshToken, UltraNetOAuth2Token& out) {
            return UltraNet_OAuth2ServiceRefresh(cfg, refreshToken, out);
        };
}

namespace {

UltraNetResult NoClientError(const std::string& providerId) {
    return UltraNetResult::Error(UltraNetResultCode::InvalidState,
        "no OAuth client id is configured for " + OAuthProviderDisplayName(providerId)
        + " and the sign-in service is off");
}

} // namespace

OAuthTokens MailOAuth::FromToken(const UltraNetOAuth2Token& token, int64_t now,
                                 const OAuthTokens& previous) {
    OAuthTokens t;
    t.accessToken  = token.accessToken;
    t.refreshToken = token.refreshToken.empty() ? previous.refreshToken : token.refreshToken;
    // Renew a minute early so a session never starts with a token that dies
    // mid-way through the fetch.
    t.expiresAt = token.expiresInSeconds > 0 ? now + token.expiresInSeconds - 60 : 0;
    return t;
}

UltraNetResult MailOAuth::SignIn(const std::string& providerId, const std::string& email,
                                 const std::function<void(const std::string& url)>& openUrl,
                                 OAuthTokens& out, int64_t now) {
    if (now <= 0) now = static_cast<int64_t>(std::time(nullptr));
    UltraNetOAuth2Token token;
    UltraNetResult r;
    if (OAuthApps::UsesService(providerId)) {
        r = hooks_.serviceAuthorize(OAuthServiceConfigFor(providerId, email), openUrl, token);
    } else {
        const OAuthApp app = OAuthApps::Get(providerId);
        if (!app.IsConfigured()) return NoClientError(providerId);
        r = hooks_.authorize(OAuthConfigFor(providerId, app, email), openUrl, token);
    }
    if (!r) return r;
    if (!token.IsValid())
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "the sign-in returned no access token");
    out = FromToken(token, now);
    return UltraNetResult::Ok();
}

UltraNetResult MailOAuth::BeginOob(const std::string& providerId, const std::string& email,
                                   std::string& consentUrlOut, std::string& codeVerifierOut) {
    consentUrlOut.clear();
    codeVerifierOut.clear();
    const OAuthApp app = OAuthApps::Get(providerId);
    if (!app.IsConfigured())
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "no OAuth client id is configured for " + OAuthProviderDisplayName(providerId));
    const UltraNetOAuth2Config cfg = OAuthConfigFor(providerId, app, email);
    UltraNetOAuth2Pkce pkce;
    if (cfg.usePkce) pkce = UltraNet_OAuth2GeneratePkce();
    // No state to verify: an oob code is pasted by the user, not returned on a
    // redirect we listen for, so there is no redirect to protect against CSRF.
    consentUrlOut   = UltraNet_OAuth2BuildAuthUrl(cfg, pkce, std::string());
    codeVerifierOut = pkce.codeVerifier;
    return UltraNetResult::Ok();
}

UltraNetResult MailOAuth::CompleteOob(const std::string& providerId, const std::string& code,
                                      const std::string& codeVerifier, OAuthTokens& out,
                                      int64_t now) {
    if (now <= 0) now = static_cast<int64_t>(std::time(nullptr));
    const std::string authCode = ExtractOAuthCode(code);
    if (authCode.empty())
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "no authorization code was entered");
    const OAuthApp app = OAuthApps::Get(providerId);
    if (!app.IsConfigured())
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "no OAuth client id is configured for " + OAuthProviderDisplayName(providerId));
    UltraNetOAuth2Token token;
    UltraNetResult r = UltraNet_OAuth2ExchangeCode(OAuthConfigFor(providerId, app), authCode,
                                                   codeVerifier, token);
    if (!r) return r;
    if (!token.IsValid())
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "the sign-in returned no access token");
    out = FromToken(token, now);
    return UltraNetResult::Ok();
}

UltraNetResult MailOAuth::EnsureFresh(const std::string& providerId, OAuthTokens& tokens,
                                      bool& refreshed, int64_t now) {
    refreshed = false;
    if (now <= 0) now = static_cast<int64_t>(std::time(nullptr));
    if (!tokens.NeedsRefresh(now)) return UltraNetResult::Ok();
    if (tokens.refreshToken.empty())
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationRequired,
                                     "the " + OAuthProviderDisplayName(providerId)
                                     + " sign-in has expired; sign in again");
    UltraNetOAuth2Token token;
    UltraNetResult r;
    if (OAuthApps::UsesService(providerId)) {
        r = hooks_.serviceRefresh(OAuthServiceConfigFor(providerId), tokens.refreshToken, token);
    } else {
        const OAuthApp app = OAuthApps::Get(providerId);
        if (!app.IsConfigured()) return NoClientError(providerId);
        r = hooks_.refresh(OAuthConfigFor(providerId, app), tokens.refreshToken, token);
    }
    if (!r) return r;
    if (!token.IsValid())
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "the token refresh returned no access token");
    tokens = FromToken(token, now, tokens);
    refreshed = true;
    return UltraNetResult::Ok();
}

UltraNetResult MailOAuth::CredentialsFor(CredentialVault& vault, const std::string& accountId,
                                         const std::string& email, const std::string& providerId,
                                         UltraNetCredentials& out, int64_t now) {
    out = UltraNetCredentials{};
    out.username = email;
    if (!vault.IsUnlocked())
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationRequired,
                                     "the credential vault is locked");

    OAuthTokens tokens;
    if (vault.RetrieveOAuthTokens(accountId, tokens)) {
        if (providerId.empty())
            return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                "the account signed in with OAuth2, but its provider is not known");
        bool refreshed = false;
        UltraNetResult r = EnsureFresh(providerId, tokens, refreshed, now);
        if (!r) return r;
        // A failed write is not fatal for this session — the next one simply
        // refreshes again — so it is not reported here.
        if (refreshed) vault.StoreOAuthTokens(accountId, tokens);
        out.type  = UltraNetAuthType::OAuth2;
        out.token = tokens.accessToken;
        return UltraNetResult::Ok();
    }

    std::string password;
    if (!vault.Retrieve(accountId, password) || password.empty())
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationRequired,
                                     "no password or sign-in is stored for this account");
    out.type     = UltraNetAuthType::Basic;
    out.password = password;
    return UltraNetResult::Ok();
}

} // namespace UltraMail
