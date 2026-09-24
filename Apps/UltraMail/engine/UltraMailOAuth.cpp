// Apps/UltraMail/engine/UltraMailOAuth.cpp
// Version: 0.3.0 - OAuthApps is a profile of UltraNet's shared OAuth2 app registry
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailOAuth.h"

#include "UltraMailOAuthDefaults.h"   // generated at configure time (build tree)

#include <cstdlib>
#include <ctime>

namespace UltraMail {

namespace {

// Reverse the build-time obfuscation of a baked-in credential (see
// UltraMailOAuthDefaults.h.in): verbatim when the build stored the values in the
// clear, else XOR each byte back with the key. The high-bit key means an
// obfuscated ASCII credential never contains a NUL, so the length is intact.
std::string Deobf(const char* s) {
    std::string out(s);
    if (defaults::kObfuscated)
        for (char& c : out)
            c = static_cast<char>(static_cast<unsigned char>(c) ^ defaults::kXorKey);
    return out;
}

// The OAuth client baked into this build for `providerId`, if any. Unconfigured
// (clientId empty) when the build carried no credentials for it — the common
// case for a dev/CI/test build.
OAuthApp BuiltInApp(const std::string& providerId) {
    OAuthApp app;
    if (providerId == "google") {
        app.clientId     = Deobf(defaults::kGoogleClientId);
        app.clientSecret = Deobf(defaults::kGoogleClientSecret);
    } else if (providerId == "microsoft") {
        app.clientId     = Deobf(defaults::kMicrosoftClientId);  // public client, no secret
    }
    return app;
}

} // namespace

// ===== OAuthApps ==============================================================

void OAuthApps::EnsureRegistered() {
    static const bool once = [] {
        UltraNet_OAuth2AddAppEnvPrefix("ULTRAMAIL_");
        // The floor: what this build bakes in. Unconfigured apps are ignored by
        // the registry, so a dev/CI/test build registers nothing here.
        for (const char* id : {"google", "microsoft"})
            UltraNet_OAuth2SetBuiltInApp(id, BuiltInApp(id));
        return true;
    }();
    (void)once;
}

void OAuthApps::Set(const std::string& providerId, const OAuthApp& app) {
    EnsureRegistered();
    UltraNet_OAuth2SetApp(providerId, app);
}

std::string OAuthApps::DefaultRedirectUri(const std::string& providerId) {
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

bool OAuthApps::Has(const std::string& providerId) {
    return Get(providerId).IsConfigured();
}

int OAuthApps::ParseIni(const std::string& text) {
    EnsureRegistered();
    return UltraNet_OAuth2ParseAppsIni(text);
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
    return "";
}

std::string OAuthProviderDisplayName(const std::string& providerId) {
    if (providerId == "google")    return "Google";
    if (providerId == "microsoft") return "Microsoft";
    return providerId;
}

bool ProviderNeedsAppPassword(const DiscoveryResult& discovery) {
    if (!discovery.found) return false;
    if (discovery.imap.oauth) return true;   // Gmail, Outlook: a typed password must be an app password
    return discovery.displayName == "Yahoo" || discovery.displayName == "iCloud";
}

bool ProviderAcceptsPassword(const DiscoveryResult& discovery) {
    auto provider = OAuthProviderFor(discovery);
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
    return cfg;
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
}

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
    const OAuthApp app = OAuthApps::Get(providerId);
    if (!app.IsConfigured())
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "no OAuth client id is configured for " + OAuthProviderDisplayName(providerId));
    UltraNetOAuth2Token token;
    UltraNetResult r = hooks_.authorize(OAuthConfigFor(providerId, app, email), openUrl, token);
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
    const OAuthApp app = OAuthApps::Get(providerId);
    if (!app.IsConfigured())
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "no OAuth client id is configured for " + OAuthProviderDisplayName(providerId));
    UltraNetOAuth2Token token;
    UltraNetResult r = hooks_.refresh(OAuthConfigFor(providerId, app), tokens.refreshToken, token);
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
