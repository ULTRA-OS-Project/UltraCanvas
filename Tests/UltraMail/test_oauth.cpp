// Tests/UltraMail/test_oauth.cpp
// OAuth2 sign-in for mail providers: the provider table, the app registration
// sources (Set / environment / INI), token bookkeeping, the sign-in + refresh
// service behind test seams, and the token set stored in the credential vault.
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailCredentialVault.h"
#include "UltraMailDiscovery.h"
#include "UltraMailOAuth.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace UltraMail;
namespace fs = std::filesystem;

namespace {

OAuthApp TestApp() {
    OAuthApp app;
    app.clientId     = "client-123.apps.googleusercontent.com";
    app.clientSecret = "shh";
    return app;
}

void SetEnv(const char* name, const char* value) {
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s(name, value);
#else
    if (value && *value) setenv(name, value, 1); else unsetenv(name);
#endif
}

} // namespace

// ---- provider table --------------------------------------------------------

TEST(oauth_provider_is_google_for_gmail_and_microsoft_for_outlook) {
    REQUIRE_EQ(OAuthProviderFor(AutoDiscovery::FromPresets("erika@gmail.com")),
               std::string("google"));
    REQUIRE_EQ(OAuthProviderFor(AutoDiscovery::FromPresets("erika@googlemail.com")),
               std::string("google"));
    REQUIRE_EQ(OAuthProviderFor(AutoDiscovery::FromPresets("erika@outlook.com")),
               std::string("microsoft"));
    REQUIRE_EQ(OAuthProviderFor(AutoDiscovery::FromPresets("erika@hotmail.com")),
               std::string("microsoft"));
    // Yahoo uses app passwords.
    REQUIRE(OAuthProviderFor(AutoDiscovery::FromPresets("erika@yahoo.de")).empty());
    REQUIRE(OAuthProviderFor(DiscoveryResult{}).empty());
    REQUIRE_EQ(OAuthProviderDisplayName("google"), std::string("Google"));
    REQUIRE_EQ(OAuthProviderDisplayName("microsoft"), std::string("Microsoft"));
}

TEST(app_password_needed_at_yahoo_icloud_and_oauth_providers) {
    REQUIRE(ProviderNeedsAppPassword(AutoDiscovery::FromPresets("erika@yahoo.de")));
    REQUIRE(ProviderNeedsAppPassword(AutoDiscovery::FromPresets("erika@icloud.com")));
    REQUIRE(ProviderNeedsAppPassword(AutoDiscovery::FromPresets("erika@me.com")));
    REQUIRE(ProviderNeedsAppPassword(AutoDiscovery::FromPresets("erika@gmail.com")));
    REQUIRE(ProviderNeedsAppPassword(AutoDiscovery::FromPresets("erika@outlook.com")));
    // Ordinary providers take the account password.
    REQUIRE(!ProviderNeedsAppPassword(AutoDiscovery::FromPresets("erika@gmx.de")));
    REQUIRE(!ProviderNeedsAppPassword(AutoDiscovery::FromPresets("erika@example.com")));
    REQUIRE(!ProviderNeedsAppPassword(DiscoveryResult{}));

    // Microsoft retired basic authentication: no password of any kind.
    REQUIRE(!ProviderAcceptsPassword(AutoDiscovery::FromPresets("erika@outlook.com")));
    REQUIRE(!ProviderAcceptsPassword(AutoDiscovery::FromPresets("erika@hotmail.com")));
    REQUIRE(ProviderAcceptsPassword(AutoDiscovery::FromPresets("erika@gmail.com")));
    REQUIRE(ProviderAcceptsPassword(AutoDiscovery::FromPresets("erika@yahoo.com")));
    REQUIRE(ProviderAcceptsPassword(DiscoveryResult{}));
}

TEST(oauth_microsoft_config_requests_imap_smtp_and_offline_access) {
    OAuthApp app; app.clientId = "00000000-1111-2222-3333-444444444444";   // public client
    UltraNetOAuth2Config cfg = OAuthConfigFor("microsoft", app, "erika@outlook.com");
    REQUIRE_EQ(cfg.authorizationEndpoint,
               std::string("https://login.microsoftonline.com/common/oauth2/v2.0/authorize"));
    REQUIRE_EQ(cfg.tokenEndpoint,
               std::string("https://login.microsoftonline.com/common/oauth2/v2.0/token"));
    REQUIRE(cfg.clientSecret.empty());
    REQUIRE(cfg.usePkce);
    REQUIRE_EQ(cfg.scopes.size(), std::size_t(3));
    REQUIRE_EQ(cfg.scopes[0], std::string("https://outlook.office.com/IMAP.AccessAsUser.All"));
    REQUIRE_EQ(cfg.scopes[1], std::string("https://outlook.office.com/SMTP.Send"));
    REQUIRE_EQ(cfg.scopes[2], std::string("offline_access"));
    REQUIRE_EQ(cfg.extraAuthParams.at("prompt"), std::string("select_account"));
    REQUIRE_EQ(cfg.extraAuthParams.at("login_hint"), std::string("erika@outlook.com"));
    // Microsoft matches loopback URIs on host + path: root path, ephemeral port.
    REQUIRE_EQ(cfg.redirectUri, std::string("http://127.0.0.1:0/"));
    REQUIRE_EQ(OAuthApps::DefaultRedirectUri("microsoft"), std::string("http://127.0.0.1:0/"));
    // No hint → no login_hint parameter.
    REQUIRE(OAuthConfigFor("microsoft", app).extraAuthParams.count("login_hint") == 0);
}

TEST(oauth_google_config_requests_offline_mail_scope) {
    UltraNetOAuth2Config cfg = OAuthConfigFor("google", TestApp());
    REQUIRE_EQ(cfg.authorizationEndpoint, std::string("https://accounts.google.com/o/oauth2/v2/auth"));
    REQUIRE_EQ(cfg.tokenEndpoint, std::string("https://oauth2.googleapis.com/token"));
    REQUIRE_EQ(cfg.clientId, std::string("client-123.apps.googleusercontent.com"));
    REQUIRE_EQ(cfg.clientSecret, std::string("shh"));
    REQUIRE(cfg.secretInBody);
    REQUIRE(cfg.usePkce);
    REQUIRE_EQ(cfg.scopes.size(), std::size_t(1));
    REQUIRE_EQ(cfg.scopes[0], std::string("https://mail.google.com/"));
    REQUIRE_EQ(cfg.extraAuthParams.at("access_type"), std::string("offline"));
    REQUIRE_EQ(cfg.extraAuthParams.at("prompt"), std::string("consent"));
    // Ephemeral loopback port: a cancelled attempt never blocks the next one.
    REQUIRE_EQ(cfg.redirectUri, std::string("http://127.0.0.1:0/callback"));
    REQUIRE(cfg.extraAuthParams.count("login_hint") == 0);
    REQUIRE_EQ(OAuthConfigFor("google", TestApp(), "erika@gmail.com").extraAuthParams.at("login_hint"),
               std::string("erika@gmail.com"));
}

// ---- app registration sources ---------------------------------------------

TEST(oauth_apps_come_from_set_env_or_ini_in_that_order) {
    OAuthApps::Clear();
    SetEnv("ULTRAMAIL_GOOGLE_CLIENT_ID", "");
    REQUIRE(!OAuthApps::Has("google"));

    // INI file: sections are provider ids, keys are snake_case.
    REQUIRE_EQ(OAuthApps::ParseIni(
        "# UltraMail OAuth clients\n"
        "[Google]\n"
        "client_id = from-ini\n"
        "client_secret=ini-secret\n"
        "redirect_uri = http://127.0.0.1:4711/cb\n"
        "[empty]\n"
        "client_secret = no-id-so-ignored\n"), 1);
    REQUIRE(OAuthApps::Has("google"));
    REQUIRE(!OAuthApps::Has("empty"));
    REQUIRE_EQ(OAuthApps::Get("google").clientId, std::string("from-ini"));
    REQUIRE_EQ(OAuthApps::Get("google").clientSecret, std::string("ini-secret"));
    REQUIRE_EQ(OAuthApps::Get("google").redirectUri, std::string("http://127.0.0.1:4711/cb"));

    // The environment outranks the file ...
    SetEnv("ULTRAMAIL_GOOGLE_CLIENT_ID", "from-env");
    REQUIRE_EQ(OAuthApps::Get("google").clientId, std::string("from-env"));
    REQUIRE(OAuthApps::Get("google").clientSecret.empty());
    REQUIRE_EQ(OAuthApps::Get("google").redirectUri, std::string("http://127.0.0.1:0/callback"));

    // ... and Set() outranks both.
    OAuthApp app; app.clientId = "from-set";
    OAuthApps::Set("google", app);
    REQUIRE_EQ(OAuthApps::Get("google").clientId, std::string("from-set"));

    SetEnv("ULTRAMAIL_GOOGLE_CLIENT_ID", "");
    OAuthApps::Clear();
    REQUIRE(!OAuthApps::Has("google"));
}

TEST(oauth_apps_load_file_tolerates_a_missing_file) {
    OAuthApps::Clear();
    fs::path dir = fs::temp_directory_path() / "ultramail_oauth_ini";
    fs::remove_all(dir);
    fs::create_directories(dir);
    REQUIRE_EQ(OAuthApps::LoadFile((dir / "oauth.ini").string()), 0);
    {
        std::ofstream os(dir / "oauth.ini");
        os << "[google]\nclient_id = file-id\n";
    }
    REQUIRE_EQ(OAuthApps::LoadFile((dir / "oauth.ini").string()), 1);
    REQUIRE_EQ(OAuthApps::Get("google").clientId, std::string("file-id"));
    OAuthApps::Clear();
    fs::remove_all(dir);
}

// ---- token bookkeeping -----------------------------------------------------

TEST(oauth_tokens_expire_a_minute_early_and_keep_the_refresh_token) {
    UltraNetOAuth2Token first;
    first.accessToken = "a1"; first.refreshToken = "r1"; first.expiresInSeconds = 3600;
    OAuthTokens t = MailOAuth::FromToken(first, /*now=*/1000);
    REQUIRE_EQ(t.accessToken, std::string("a1"));
    REQUIRE_EQ(t.refreshToken, std::string("r1"));
    REQUIRE_EQ(t.expiresAt, int64_t(1000 + 3600 - 60));
    REQUIRE(!t.NeedsRefresh(1000));
    REQUIRE(!t.NeedsRefresh(1000 + 3600 - 61));
    REQUIRE(t.NeedsRefresh(1000 + 3600 - 60));

    // Google only sends the refresh token once: a refresh answer without one
    // must not lose it.
    UltraNetOAuth2Token second;
    second.accessToken = "a2"; second.expiresInSeconds = 3600;
    OAuthTokens t2 = MailOAuth::FromToken(second, 5000, t);
    REQUIRE_EQ(t2.accessToken, std::string("a2"));
    REQUIRE_EQ(t2.refreshToken, std::string("r1"));

    // No lifetime reported: never considered expired on time alone.
    UltraNetOAuth2Token noLifetime; noLifetime.accessToken = "a3";
    REQUIRE(!MailOAuth::FromToken(noLifetime, 1).NeedsRefresh(1 << 30));
    REQUIRE(OAuthTokens{}.NeedsRefresh(0));
}

// ---- sign-in + refresh through the seams -----------------------------------

TEST(oauth_sign_in_needs_an_app_and_hands_the_consent_url_to_the_browser) {
    OAuthApps::Clear();
    SetEnv("ULTRAMAIL_GOOGLE_CLIENT_ID", "");

    int authorizeCalls = 0;
    OAuthHooks hooks;
    hooks.authorize = [&](const UltraNetOAuth2Config& cfg,
                          const std::function<void(const std::string&)>& openUrl,
                          UltraNetOAuth2Token& out) {
        ++authorizeCalls;
        REQUIRE_EQ(cfg.extraAuthParams.at("login_hint"), std::string("erika@gmail.com"));
        openUrl(cfg.authorizationEndpoint + "?client_id=" + cfg.clientId);
        out.accessToken = "access"; out.refreshToken = "refresh"; out.expiresInSeconds = 3599;
        return UltraNetResult::Ok();
    };
    MailOAuth oauth(hooks);

    // Without a registered app nothing is opened and the reason says so.
    OAuthTokens tokens;
    std::string opened;
    UltraNetResult r = oauth.SignIn("google", "erika@gmail.com",
                                    [&](const std::string& u) { opened = u; }, tokens, 100);
    REQUIRE(!r);
    REQUIRE_EQ(authorizeCalls, 0);
    REQUIRE(r.message.find("client id") != std::string::npos);

    OAuthApps::Set("google", TestApp());
    r = oauth.SignIn("google", "erika@gmail.com",
                     [&](const std::string& u) { opened = u; }, tokens, 100);
    REQUIRE(r);
    REQUIRE_EQ(authorizeCalls, 1);
    REQUIRE(opened.find("accounts.google.com") != std::string::npos);
    REQUIRE(opened.find("client-123") != std::string::npos);
    REQUIRE_EQ(tokens.accessToken, std::string("access"));
    REQUIRE_EQ(tokens.refreshToken, std::string("refresh"));
    REQUIRE_EQ(tokens.expiresAt, int64_t(100 + 3599 - 60));
    OAuthApps::Clear();
}

TEST(oauth_ensure_fresh_refreshes_only_expired_tokens) {
    OAuthApps::Clear();
    OAuthApps::Set("google", TestApp());
    int refreshCalls = 0;
    OAuthHooks hooks;
    hooks.refresh = [&](const UltraNetOAuth2Config&, const std::string& refreshToken,
                        UltraNetOAuth2Token& out) {
        ++refreshCalls;
        REQUIRE_EQ(refreshToken, std::string("r1"));
        out.accessToken = "a2"; out.expiresInSeconds = 3600;   // no new refresh token
        return UltraNetResult::Ok();
    };
    MailOAuth oauth(hooks);

    OAuthTokens t; t.accessToken = "a1"; t.refreshToken = "r1"; t.expiresAt = 2000;
    bool refreshed = true;
    REQUIRE(oauth.EnsureFresh("google", t, refreshed, 1000));
    REQUIRE(!refreshed);
    REQUIRE_EQ(refreshCalls, 0);
    REQUIRE_EQ(t.accessToken, std::string("a1"));

    REQUIRE(oauth.EnsureFresh("google", t, refreshed, 2000));
    REQUIRE(refreshed);
    REQUIRE_EQ(refreshCalls, 1);
    REQUIRE_EQ(t.accessToken, std::string("a2"));
    REQUIRE_EQ(t.refreshToken, std::string("r1"));
    REQUIRE_EQ(t.expiresAt, int64_t(2000 + 3600 - 60));

    // An expired token without a refresh token cannot be renewed silently.
    OAuthTokens dead; dead.accessToken = "old"; dead.expiresAt = 1;
    UltraNetResult r = oauth.EnsureFresh("google", dead, refreshed, 5);
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::AuthenticationRequired);
    REQUIRE_EQ(refreshCalls, 1);
    OAuthApps::Clear();
}

// ---- the vault: token sets beside passwords --------------------------------
// Same rules as the vault tests in test_discovery.cpp: unlock first, one vault
// open at a time.

TEST(oauth_tokens_round_trip_through_the_vault_and_replace_the_password) {
    fs::path dir = fs::temp_directory_path() / "ultramail_vault_oauth";
    fs::remove_all(dir);
    CredentialVault vault(dir.string());
    REQUIRE_EQ(static_cast<int>(vault.Unlock("correct horse battery staple")),
               static_cast<int>(VaultStatus::Ok));

    REQUIRE(vault.MethodFor("erika") == SignInMethod::None);
    REQUIRE(!vault.StoreOAuthTokens("erika", OAuthTokens{}));   // nothing to store

    REQUIRE(vault.Store("erika", "app-password"));
    REQUIRE(vault.MethodFor("erika") == SignInMethod::Password);

    OAuthTokens t; t.accessToken = "acc"; t.refreshToken = "ref"; t.expiresAt = 1234567;
    REQUIRE(vault.StoreOAuthTokens("erika", t));
    REQUIRE(vault.MethodFor("erika") == SignInMethod::OAuth2);
    REQUIRE(!vault.Has("erika"));            // the password slot was dropped

    OAuthTokens got;
    REQUIRE(vault.RetrieveOAuthTokens("erika", got));
    REQUIRE_EQ(got.accessToken, std::string("acc"));
    REQUIRE_EQ(got.refreshToken, std::string("ref"));
    REQUIRE_EQ(got.expiresAt, int64_t(1234567));

    // Storing a password again drops the token set.
    REQUIRE(vault.Store("erika", "pw2"));
    REQUIRE(!vault.HasOAuthTokens("erika"));
    REQUIRE(vault.MethodFor("erika") == SignInMethod::Password);

    REQUIRE(vault.StoreOAuthTokens("erika", t));
    REQUIRE(vault.RemoveOAuthTokens("erika"));
    REQUIRE(vault.MethodFor("erika") == SignInMethod::None);

    vault.Lock();
    fs::remove_all(dir);
}

TEST(oauth_credentials_for_account_are_bearer_or_password) {
    OAuthApps::Clear();
    OAuthApps::Set("google", TestApp());
    fs::path dir = fs::temp_directory_path() / "ultramail_vault_creds";
    fs::remove_all(dir);
    CredentialVault vault(dir.string());

    int refreshCalls = 0;
    OAuthHooks hooks;
    hooks.refresh = [&](const UltraNetOAuth2Config&, const std::string&, UltraNetOAuth2Token& out) {
        ++refreshCalls;
        out.accessToken = "fresh"; out.expiresInSeconds = 3600;
        return UltraNetResult::Ok();
    };
    MailOAuth oauth(hooks);
    UltraNetCredentials c;

    // Locked vault: nothing is guessed.
    UltraNetResult r = oauth.CredentialsFor(vault, "erika", "erika@gmail.com", "google", c, 100);
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::AuthenticationRequired);

    REQUIRE_EQ(static_cast<int>(vault.Unlock("correct horse battery staple")),
               static_cast<int>(VaultStatus::Ok));
    r = oauth.CredentialsFor(vault, "erika", "erika@gmail.com", "google", c, 100);
    REQUIRE(!r);                                            // nothing stored yet

    // Password account → Basic.
    REQUIRE(vault.Store("erika", "app-password"));
    REQUIRE(oauth.CredentialsFor(vault, "erika", "erika@gmail.com", "google", c, 100));
    REQUIRE(c.type == UltraNetAuthType::Basic);
    REQUIRE_EQ(c.username, std::string("erika@gmail.com"));
    REQUIRE_EQ(c.password, std::string("app-password"));
    REQUIRE(c.token.empty());

    // OAuth account with a live token → bearer, no refresh.
    OAuthTokens t; t.accessToken = "live"; t.refreshToken = "ref"; t.expiresAt = 1000;
    REQUIRE(vault.StoreOAuthTokens("erika", t));
    REQUIRE(oauth.CredentialsFor(vault, "erika", "erika@gmail.com", "google", c, 100));
    REQUIRE(c.type == UltraNetAuthType::OAuth2);
    REQUIRE_EQ(c.token, std::string("live"));
    REQUIRE(c.password.empty());
    REQUIRE_EQ(refreshCalls, 0);

    // Expired → refreshed through the provider and stored again.
    REQUIRE(oauth.CredentialsFor(vault, "erika", "erika@gmail.com", "google", c, 1000));
    REQUIRE_EQ(c.token, std::string("fresh"));
    REQUIRE_EQ(refreshCalls, 1);
    OAuthTokens stored;
    REQUIRE(vault.RetrieveOAuthTokens("erika", stored));
    REQUIRE_EQ(stored.accessToken, std::string("fresh"));
    REQUIRE_EQ(stored.refreshToken, std::string("ref"));
    REQUIRE_EQ(stored.expiresAt, int64_t(1000 + 3600 - 60));

    vault.Lock();
    fs::remove_all(dir);
    OAuthApps::Clear();
}
