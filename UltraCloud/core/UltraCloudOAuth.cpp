// UltraCloud/core/UltraCloudOAuth.cpp
// Version: 0.5.0 - the browser sign-in says its steps in the thread's log,
//                  and can be called off (SetThreadSignInCancel)
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudOAuth.h>
#include <UltraCloud/UltraCloudLog.h>

#include <ctime>

namespace UltraCloud {

namespace {
thread_local std::function<bool()> t_signInCancelled;
} // namespace

std::function<bool()> SetThreadSignInCancel(std::function<bool()> cancelled) {
    std::function<bool()> previous = std::move(t_signInCancelled);
    t_signInCancelled = std::move(cancelled);
    return previous;
}

bool SignInCancelled() { return t_signInCancelled && t_signInCancelled(); }

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
            // The calling thread's cancel check, asked while the browser
            // answer is awaited.
            return UltraNet_OAuth2AuthorizeInteractive(cfg, openUrl, out, 300000,
                                                       [] { return SignInCancelled(); });
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
    // The sign-in page's address goes to the log without its query, which
    // carries the request's one-time state and code challenge.
    auto logOpen = [&openUrl](const std::string& url) {
        LogToThread(LogKind::Step, "Opening the sign-in page in the browser: " +
                                   url.substr(0, url.find('?')));
        if (openUrl) openUrl(url);
    };
    LogToThread(LogKind::Step, "Signing in to " + DisplayName() +
                               " through the browser - waiting for the answer on " +
                               app.redirectUri);
    UltraNetOAuth2Token token;
    UltraNetResult r = hooks_.authorize(OAuthConfig(app), logOpen, token);
    if (SignInCancelled()) {
        LogToThread(LogKind::Step, "The sign-in was cancelled");
        return Result::Error(ResultCode::Cancelled, "sign-in cancelled");
    }
    if (!r || !token.IsValid()) {
        const std::string why = r.message.empty() ? "no token" : r.message;
        LogToThread(LogKind::Step, "The browser sign-in did not complete: " + why);
        return Result::Error(ResultCode::AuthFailed, "sign-in failed: " + why);
    }
    LogToThread(LogKind::Step, "Signed in - " + DisplayName() + " gave an access token" +
                               (token.refreshToken.empty() ? std::string(" (no refresh token)")
                                                           : std::string()));
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
