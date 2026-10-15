// UltraCloud/include/UltraCloud/UltraCloudOAuth.h
// OAuth2 for the hosted providers (Dropbox, OneDrive, Google Drive): the
// per-provider app registration (client id / secret / redirect URI) and the
// provider base that signs in through the system browser and refreshes
// tokens. A provider with a client of its own signs in over UltraNet's OAuth2
// client (PKCE, loopback redirect); one without goes through the OAuth2
// sign-in service UltraNet's registry names (UltraNetOAuth2Service.h) - the
// web service that holds the registrations and receives the providers'
// redirects, so the application ships no client secret. Google Drive signs in
// to the service's "google", OneDrive to its "microsoft", Dropbox to its
// "dropbox".
// Version: 0.4.0 - the sign-in service for a provider without a client of its own
// Version: 0.3.0 - the app registration is UltraNet's shared registry
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudHttp.h"

#include <UltraNet/UltraNetOAuth2.h>
#include <UltraNet/UltraNetOAuth2Apps.h>
#include <UltraNet/UltraNetOAuth2Service.h>

#include <functional>
#include <string>

namespace UltraCloud {

// The OAuth app a provider signs in as: the framework's type. Each hosted
// provider needs one registered by the ULTRA OS project (or the app vendor);
// it is configuration, never a literal in the code.
using OAuthApp = UltraNetOAuth2App;

// UltraCloud's profile of the process-wide registry in UltraNet
// (<UltraNet/UltraNetOAuth2Apps.h>), shared with UltraMail: SetOAuthApp, then
// the environment (ULTRACLOUD_<PROVIDER>_CLIENT_ID, _CLIENT_SECRET,
// _REDIRECT_URI, the provider id upper-cased — beside the shared
// ULTRANET_OAUTH_ prefix), then any INI file loaded through UltraNet, then a
// baked-in client another module registered. "googledrive" falls back to a
// "google" registration and "onedrive" to a "microsoft" one, so a client an
// installation configures for Gmail and Outlook serves Drive and OneDrive too
// when its consent screen carries their scopes. An empty redirect URI comes
// back as DefaultRedirectUri().
void     SetOAuthApp(const std::string& providerId, const OAuthApp& app);
OAuthApp GetOAuthApp(const std::string& providerId);
bool     HasOAuthApp(const std::string& providerId);
// True when `providerId` signs in through the sign-in service: no app of its
// own, and the registry names a service (UltraNet_OAuth2SignInServiceUrl;
// ULTRACLOUD_OAUTH_SERVICE_URL in the environment names one for this
// profile, "off" turns it off).
bool     UsesSignInService(const std::string& providerId);
// True when the provider can be signed in to at all: an app of its own, or
// the service. What the add-account dialog asks before offering the browser.
bool     CanSignIn(const std::string& providerId);
// The loopback redirect UltraCloud registers with a provider when no
// registration names one: "http://127.0.0.1:53682/callback".
std::string DefaultRedirectUri();
// Register the profile (prefix and aliases) with the shared registry. Every
// entry point above calls it; a host that reads the registry through UltraNet
// directly may call it once at start-up.
void     EnsureOAuthAppsRegistered();

// The OAuth seam for tests: the interactive authorization and the refresh,
// directly (authorize, refresh) and through the sign-in service
// (serviceAuthorize, serviceRefresh). Each defaults to the UltraNet call.
struct OAuthHooks {
    std::function<UltraNetResult(const UltraNetOAuth2Config&,
                                 const std::function<void(const std::string&)>& openUrl,
                                 UltraNetOAuth2Token&)> authorize;
    std::function<UltraNetResult(const UltraNetOAuth2Config&, const std::string& refreshToken,
                                 UltraNetOAuth2Token&)> refresh;
    std::function<UltraNetResult(const UltraNetOAuth2ServiceConfig&,
                                 const std::function<void(const std::string&)>& openUrl,
                                 UltraNetOAuth2Token&)> serviceAuthorize;
    std::function<UltraNetResult(const UltraNetOAuth2ServiceConfig&, const std::string& refreshToken,
                                 UltraNetOAuth2Token&)> serviceRefresh;
};

class OAuthProviderBase : public HttpProviderBase {
public:
    explicit OAuthProviderBase(HttpFn http = nullptr, OAuthHooks hooks = {});

    // The provider's endpoints and scopes for a given app registration.
    virtual UltraNetOAuth2Config OAuthConfig(const OAuthApp& app) const = 0;
    // The provider's id at the sign-in service: Id() unless the service knows
    // it under the identity provider's name ("google" for Google Drive,
    // "microsoft" for OneDrive).
    virtual std::string SignInServiceProviderId() const { return Id(); }
    // The service, that id, and the scopes OAuthConfig asks for.
    UltraNetOAuth2ServiceConfig OAuthServiceConfig() const;

    Result SignIn(const Account& account,
                  const std::function<void(const std::string& url)>& openUrl,
                  Credentials& out) override;
    Result RefreshCredentials(const Account& account, Credentials& credentials) override;

    // Put a token answer into the credentials (access + refresh + expiry).
    static void ApplyToken(const UltraNetOAuth2Token& token, Credentials& credentials,
                           int64_t now = 0);

protected:
    OAuthHooks hooks_;
};

} // namespace UltraCloud
