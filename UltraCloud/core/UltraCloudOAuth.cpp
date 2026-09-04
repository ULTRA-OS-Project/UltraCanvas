// UltraCloud/core/UltraCloudOAuth.cpp
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudOAuth.h>

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <map>
#include <mutex>

namespace UltraCloud {

namespace {
std::mutex& AppsMutex() { static std::mutex m; return m; }
std::map<std::string, OAuthApp>& Apps() { static std::map<std::string, OAuthApp> a; return a; }

std::string EnvName(const std::string& providerId, const char* suffix) {
    std::string name = "ULTRACLOUD_";
    for (char c : providerId)
        name.push_back(std::isalnum(static_cast<unsigned char>(c))
                           ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : '_');
    return name + suffix;
}
std::string Env(const std::string& name) {
    const char* v = std::getenv(name.c_str());
    return v ? v : "";
}
} // namespace

void SetOAuthApp(const std::string& providerId, const OAuthApp& app) {
    std::lock_guard<std::mutex> lock(AppsMutex());
    Apps()[providerId] = app;
}

OAuthApp GetOAuthApp(const std::string& providerId) {
    {
        std::lock_guard<std::mutex> lock(AppsMutex());
        auto it = Apps().find(providerId);
        if (it != Apps().end() && it->second.IsConfigured()) return it->second;
    }
    OAuthApp app;
    app.clientId     = Env(EnvName(providerId, "_CLIENT_ID"));
    app.clientSecret = Env(EnvName(providerId, "_CLIENT_SECRET"));
    if (std::string r = Env(EnvName(providerId, "_REDIRECT_URI")); !r.empty()) app.redirectUri = r;
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
