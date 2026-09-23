// UltraCloud/core/UltraCloudOAuth.cpp
// Version: 0.3.0 - the app registration is UltraNet's shared registry
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudOAuth.h>

#include <ctime>

namespace UltraCloud {

void EnsureOAuthAppsRegistered() {
    static const bool once = [] {
        UltraNet_OAuth2AddAppEnvPrefix("ULTRACLOUD_");
        UltraNet_OAuth2SetAppAlias("googledrive", "google");
        UltraNet_OAuth2SetAppAlias("onedrive", "microsoft");
        return true;
    }();
    (void)once;
}

std::string DefaultRedirectUri() { return "http://127.0.0.1:53682/callback"; }

void SetOAuthApp(const std::string& providerId, const OAuthApp& app) {
    EnsureOAuthAppsRegistered();
    UltraNet_OAuth2SetApp(providerId, app);
}

OAuthApp GetOAuthApp(const std::string& providerId) {
    EnsureOAuthAppsRegistered();
    OAuthApp app = UltraNet_OAuth2GetApp(providerId);
    if (!app.IsConfigured()) return OAuthApp{};
    if (app.redirectUri.empty()) app.redirectUri = DefaultRedirectUri();
    return app;
}

bool HasOAuthApp(const std::string& providerId) {
    return GetOAuthApp(providerId).IsConfigured();
}

OAuthProviderBase::OAuthProviderBase(HttpFn http, OAuthHooks hooks)
    : HttpProviderBase(std::move(http)), hooks_(std::move(hooks)) {
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

void OAuthProviderBase::ApplyToken(const UltraNetOAuth2Token& token, Credentials& credentials,
                                   int64_t now) {
    if (now <= 0) now = static_cast<int64_t>(std::time(nullptr));
    credentials.token = token.accessToken;
    if (!token.refreshToken.empty()) credentials.refreshToken = token.refreshToken;
    // Renew a minute early so a request never carries a token that dies mid-flight.
    credentials.tokenExpiresAt = token.expiresInSeconds > 0
        ? now + token.expiresInSeconds - 60 : 0;
    credentials.password.clear();
}

Result OAuthProviderBase::SignIn(const Account& account,
                                 const std::function<void(const std::string& url)>& openUrl,
                                 Credentials& out) {
    (void)account;
    const OAuthApp app = GetOAuthApp(Id());
    if (!app.IsConfigured())
        return Result::Error(ResultCode::Unsupported,
                             "no OAuth client id configured for " + DisplayName()
                             + " (set ULTRACLOUD_" + Id() + "_CLIENT_ID or call SetOAuthApp)");
    UltraNetOAuth2Token token;
    UltraNetResult r = hooks_.authorize(OAuthConfig(app), openUrl, token);
    if (!r || !token.IsValid())
        return Result::Error(ResultCode::AuthFailed,
                             "sign-in failed: " + (r.message.empty() ? "no token" : r.message));
    ApplyToken(token, out);
    return Result::Ok();
}

Result OAuthProviderBase::RefreshCredentials(const Account& account, Credentials& credentials) {
    (void)account;
    if (credentials.refreshToken.empty())
        return Result::Error(ResultCode::AuthFailed, "the sign-in has expired; sign in again");
    const OAuthApp app = GetOAuthApp(Id());
    if (!app.IsConfigured())
        return Result::Error(ResultCode::Unsupported, "no OAuth client id configured for " + DisplayName());
    UltraNetOAuth2Token token;
    UltraNetResult r = hooks_.refresh(OAuthConfig(app), credentials.refreshToken, token);
    if (!r || !token.IsValid())
        return Result::Error(ResultCode::AuthFailed,
                             "token refresh failed: " + (r.message.empty() ? "no token" : r.message));
    ApplyToken(token, credentials);
    return Result::Ok();
}

} // namespace UltraCloud
