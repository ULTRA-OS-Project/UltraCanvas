// Tests/UltraCloud/test_oauth.cpp
// OAuth app registration (explicit + environment), the per-provider OAuth
// configs, sign-in and refresh through injected hooks, token expiry handling,
// and the service refreshing an expired token before a call.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraCloud/UltraCloud.h>
#include <UltraCloud/UltraCloudDropbox.h>
#include <UltraCloud/UltraCloudGoogleDrive.h>
#include <UltraCloud/UltraCloudOneDrive.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>

using namespace UltraCloud;
namespace fs = std::filesystem;

namespace {
std::string TempDir(const std::string& tag) {
    fs::path p = fs::temp_directory_path() / ("ultracloud-test-" + tag);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
}
UltraNetResult Ok() { UltraNetResult r; r.success = true; return r; }
} // namespace

TEST(oauth_app_from_registry_and_environment) {
    OAuthApp none = GetOAuthApp("no-such-provider");
    REQUIRE(!none.IsConfigured());

    OAuthApp app; app.clientId = "abc"; app.redirectUri = "http://127.0.0.1:9999/cb";
    SetOAuthApp("dropbox", app);
    REQUIRE(HasOAuthApp("dropbox"));
    REQUIRE_EQ(GetOAuthApp("dropbox").redirectUri, std::string("http://127.0.0.1:9999/cb"));

#if !defined(_WIN32)
    setenv("ULTRACLOUD_GOOGLEDRIVE_CLIENT_ID", "env-client", 1);
    REQUIRE(HasOAuthApp("googledrive"));
    REQUIRE_EQ(GetOAuthApp("googledrive").clientId, std::string("env-client"));
    unsetenv("ULTRACLOUD_GOOGLEDRIVE_CLIENT_ID");
#endif
}

TEST(provider_oauth_configs) {
    OAuthApp app; app.clientId = "id"; app.clientSecret = "sec"; app.redirectUri = "http://127.0.0.1:1/cb";
    UltraNetOAuth2Config d = DropboxProvider().OAuthConfig(app);
    REQUIRE(d.authorizationEndpoint.find("dropbox.com/oauth2/authorize") != std::string::npos);
    REQUIRE(d.extraAuthParams["token_access_type"] == "offline");
    REQUIRE(d.usePkce);
    UltraNetOAuth2Config o = OneDriveProvider().OAuthConfig(app);
    REQUIRE(o.tokenEndpoint.find("login.microsoftonline.com") != std::string::npos);
    bool offline = false;
    for (auto& s : o.scopes) if (s == "offline_access") offline = true;
    REQUIRE(offline);
    UltraNetOAuth2Config g = GoogleDriveProvider().OAuthConfig(app);
    REQUIRE(g.extraAuthParams["access_type"] == "offline");
    REQUIRE_EQ(g.clientId, std::string("id"));
}

TEST(sign_in_and_refresh_through_hooks) {
    OAuthApp app; app.clientId = "id";
    SetOAuthApp("dropbox", app);
    std::string openedUrl;
    OAuthHooks hooks;
    hooks.authorize = [](const UltraNetOAuth2Config& cfg,
                         const std::function<void(const std::string&)>& openUrl,
                         UltraNetOAuth2Token& out) {
        openUrl(cfg.authorizationEndpoint + "?client_id=" + cfg.clientId);
        out.accessToken = "access-1"; out.refreshToken = "refresh-1"; out.expiresInSeconds = 3600;
        return Ok();
    };
    hooks.refresh = [](const UltraNetOAuth2Config&, const std::string& refreshToken,
                       UltraNetOAuth2Token& out) {
        REQUIRE_EQ(refreshToken, std::string("refresh-1"));
        out.accessToken = "access-2"; out.expiresInSeconds = 1800;   // no new refresh token
        return Ok();
    };
    DropboxProvider dropbox(nullptr, hooks);
    Account a; a.providerId = "dropbox";
    Credentials c;
    REQUIRE(dropbox.SignIn(a, [&](const std::string& url) { openedUrl = url; }, c));
    REQUIRE(openedUrl.find("client_id=id") != std::string::npos);
    REQUIRE_EQ(c.token, std::string("access-1"));
    REQUIRE_EQ(c.refreshToken, std::string("refresh-1"));
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    REQUIRE(c.tokenExpiresAt > now + 3000 && c.tokenExpiresAt <= now + 3600);
    REQUIRE(!c.TokenExpired(now));
    REQUIRE(c.TokenExpired(now + 4000));

    REQUIRE(dropbox.RefreshCredentials(a, c));
    REQUIRE_EQ(c.token, std::string("access-2"));
    REQUIRE_EQ(c.refreshToken, std::string("refresh-1"));   // kept

    // No app configured → Unsupported with a helpful message.
    SetOAuthApp("dropbox", OAuthApp{});
    Credentials d;
    Result r = dropbox.SignIn(a, [](const std::string&) {}, d);
    REQUIRE(r.code == ResultCode::Unsupported);
    REQUIRE(r.message.find("ULTRACLOUD_dropbox_CLIENT_ID") != std::string::npos);
}

TEST(service_refreshes_expired_token_and_stores_it) {
    OAuthApp app; app.clientId = "id";
    SetOAuthApp("onedrive", app);
    int refreshes = 0;
    OAuthHooks hooks;
    hooks.refresh = [&refreshes](const UltraNetOAuth2Config&, const std::string&,
                                 UltraNetOAuth2Token& out) {
        ++refreshes;
        out.accessToken = "fresh"; out.expiresInSeconds = 3600;
        return Ok();
    };
    std::string seenAuth;
    HttpFn fake = [&seenAuth](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        seenAuth = req.headers.Get("Authorization");
        resp.statusCode = 200;
        const std::string body = R"({"value":[]})";
        resp.body.assign(body.begin(), body.end());
        return Ok();
    };
    RegisterProvider(std::make_shared<OneDriveProvider>(fake, hooks));

    AccountStore accounts;
    REQUIRE(accounts.Open("uctest-oauth-service", ":memory:"));
    FileSecretStore secrets(TempDir("oauth-secrets"));
    CloudService service(accounts, secrets);

    Account a; a.providerId = "onedrive"; a.username = "erika@outlook.com";
    Credentials stale; stale.token = "stale"; stale.refreshToken = "r"; stale.tokenExpiresAt = 1;   // 1970
    REQUIRE(service.AddAccount(a, stale, /*verify=*/false));

    std::vector<Entry> entries;
    REQUIRE(service.List(a.accountId, "/", entries));
    REQUIRE_EQ(refreshes, 1);
    REQUIRE_EQ(seenAuth, std::string("Bearer fresh"));
    Credentials stored;
    REQUIRE(secrets.Retrieve(a.accountId, stored));
    REQUIRE_EQ(stored.token, std::string("fresh"));
    REQUIRE(stored.tokenExpiresAt > 1);

    // Second call: still fresh, no refresh.
    REQUIRE(service.List(a.accountId, "/", entries));
    REQUIRE_EQ(refreshes, 1);
}

TEST(service_sign_in_fills_account_from_provider) {
    OAuthApp app; app.clientId = "id";
    SetOAuthApp("googledrive", app);
    OAuthHooks hooks;
    hooks.authorize = [](const UltraNetOAuth2Config&, const std::function<void(const std::string&)>&,
                         UltraNetOAuth2Token& out) {
        out.accessToken = "g-token"; out.refreshToken = "g-refresh"; out.expiresInSeconds = 3600;
        return Ok();
    };
    HttpFn fake = [](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        resp.statusCode = 200;
        std::string body = req.url.find("/about") != std::string::npos
            ? R"({"user":{"emailAddress":"erika@gmail.com","displayName":"Erika Example"}})"
            : R"({"files":[]})";
        resp.body.assign(body.begin(), body.end());
        return Ok();
    };
    RegisterProvider(std::make_shared<GoogleDriveProvider>(fake, hooks));

    AccountStore accounts;
    REQUIRE(accounts.Open("uctest-oauth-signin", ":memory:"));
    FileSecretStore secrets(TempDir("oauth-signin-secrets"));
    CloudService service(accounts, secrets);

    Account a; a.providerId = "googledrive";
    REQUIRE(service.SignInAccount(a, [](const std::string&) {}));
    REQUIRE_EQ(a.username, std::string("erika@gmail.com"));
    REQUIRE_EQ(a.displayName, std::string("Google Drive (Erika Example)"));
    REQUIRE_EQ(a.accountId, std::string("googledrive-erika-gmail-com"));
    Credentials stored;
    REQUIRE(secrets.Retrieve(a.accountId, stored));
    REQUIRE_EQ(stored.token, std::string("g-token"));
    REQUIRE_EQ(stored.refreshToken, std::string("g-refresh"));
}
