// UltraCloud/include/UltraCloud/UltraCloudOAuth.h
// OAuth2 for the hosted providers (Dropbox, OneDrive, Google Drive): the
// per-provider app registration (client id / secret / redirect URI) and the
// provider base that signs in through the system browser and refreshes
// tokens, both over UltraNet's OAuth2 client (PKCE, loopback redirect).
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudHttp.h"

#include <UltraNet/UltraNetOAuth2.h>

#include <functional>
#include <string>

namespace UltraCloud {

// The OAuth app a provider signs in as. Each hosted provider needs one
// registered by the ULTRA OS project (or the app vendor); it is
// configuration, never a literal in the code. Sources, in order: SetOAuthApp,
// then the environment (ULTRACLOUD_<PROVIDER>_CLIENT_ID, _CLIENT_SECRET,
// _REDIRECT_URI, provider id upper-cased).
struct OAuthApp {
    std::string clientId;
    std::string clientSecret;    // empty = public client (PKCE only)
    std::string redirectUri = "http://127.0.0.1:53682/callback";
    bool IsConfigured() const { return !clientId.empty(); }
};
void     SetOAuthApp(const std::string& providerId, const OAuthApp& app);
OAuthApp GetOAuthApp(const std::string& providerId);
bool     HasOAuthApp(const std::string& providerId);

// The OAuth seam for tests: the interactive authorization and the refresh.
struct OAuthHooks {
    std::function<UltraNetResult(const UltraNetOAuth2Config&,
                                 const std::function<void(const std::string&)>& openUrl,
                                 UltraNetOAuth2Token&)> authorize;
    std::function<UltraNetResult(const UltraNetOAuth2Config&, const std::string& refreshToken,
                                 UltraNetOAuth2Token&)> refresh;
};

class OAuthProviderBase : public HttpProviderBase {
public:
    explicit OAuthProviderBase(HttpFn http = nullptr, OAuthHooks hooks = {});

    // The provider's endpoints and scopes for a given app registration.
    virtual UltraNetOAuth2Config OAuthConfig(const OAuthApp& app) const = 0;

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
