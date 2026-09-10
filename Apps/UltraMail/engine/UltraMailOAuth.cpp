// Apps/UltraMail/engine/UltraMailOAuth.cpp
// Version: 0.2.0 - Microsoft (Outlook / Microsoft 365) beside Google; login hint
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailOAuth.h"

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace UltraMail {

namespace {

std::mutex& AppsMutex() { static std::mutex m; return m; }
// Set() registrations win over the INI file, so keep them apart.
std::map<std::string, OAuthApp>& SetApps()  { static std::map<std::string, OAuthApp> a; return a; }
std::map<std::string, OAuthApp>& FileApps() { static std::map<std::string, OAuthApp> a; return a; }

std::string EnvName(const std::string& providerId, const char* suffix) {
    std::string name = "ULTRAMAIL_";
    for (char c : providerId)
        name.push_back(std::isalnum(static_cast<unsigned char>(c))
                           ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : '_');
    return name + suffix;
}

std::string Env(const std::string& name) {
    const char* v = std::getenv(name.c_str());
    return v ? v : "";
}

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

// ===== OAuthApps ==============================================================

void OAuthApps::Set(const std::string& providerId, const OAuthApp& app) {
    std::lock_guard<std::mutex> lock(AppsMutex());
    SetApps()[providerId] = app;
}

std::string OAuthApps::DefaultRedirectUri(const std::string& providerId) {
    if (providerId == "microsoft") return "http://127.0.0.1:0/";
    return "http://127.0.0.1:0/callback";
}

OAuthApp OAuthApps::Get(const std::string& providerId) {
    auto withDefault = [&providerId](OAuthApp app) {
        if (app.redirectUri.empty()) app.redirectUri = DefaultRedirectUri(providerId);
        return app;
    };
    {
        std::lock_guard<std::mutex> lock(AppsMutex());
        auto it = SetApps().find(providerId);
        if (it != SetApps().end() && it->second.IsConfigured()) return withDefault(it->second);
    }
    OAuthApp app;
    app.clientId     = Env(EnvName(providerId, "_CLIENT_ID"));
    app.clientSecret = Env(EnvName(providerId, "_CLIENT_SECRET"));
    app.redirectUri  = Env(EnvName(providerId, "_REDIRECT_URI"));
    if (app.IsConfigured()) return withDefault(app);

    std::lock_guard<std::mutex> lock(AppsMutex());
    auto it = FileApps().find(providerId);
    if (it != FileApps().end() && it->second.IsConfigured()) return withDefault(it->second);
    return OAuthApp{};
}

bool OAuthApps::Has(const std::string& providerId) {
    return Get(providerId).IsConfigured();
}

int OAuthApps::ParseIni(const std::string& text) {
    std::map<std::string, OAuthApp> parsed;
    std::istringstream in(text);
    std::string line, section;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = Lower(Trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || section.empty()) continue;
        const std::string key   = Lower(Trim(line.substr(0, eq)));
        const std::string value = Trim(line.substr(eq + 1));
        OAuthApp& app = parsed[section];
        if      (key == "client_id")     app.clientId     = value;
        else if (key == "client_secret") app.clientSecret = value;
        else if (key == "redirect_uri" && !value.empty()) app.redirectUri = value;
    }
    int count = 0;
    std::lock_guard<std::mutex> lock(AppsMutex());
    for (auto& [id, app] : parsed) {
        if (!app.IsConfigured()) continue;
        FileApps()[id] = app;
        ++count;
    }
    return count;
}

int OAuthApps::LoadFile(const std::string& path) {
    std::ifstream is(path);
    if (!is) return 0;
    std::string text((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    return ParseIni(text);
}

void OAuthApps::Clear() {
    std::lock_guard<std::mutex> lock(AppsMutex());
    SetApps().clear();
    FileApps().clear();
}

// ===== Providers ==============================================================

std::string OAuthProviderFor(const DiscoveryResult& discovery) {
    if (!discovery.found || !discovery.imap.oauth) return "";
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
