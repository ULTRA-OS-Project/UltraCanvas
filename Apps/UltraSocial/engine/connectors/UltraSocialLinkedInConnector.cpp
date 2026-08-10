// Apps/UltraSocial/engine/connectors/UltraSocialLinkedInConnector.cpp
// Version: 0.1.0 (Phase 3)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialLinkedInConnector.h"

#include "../UltraSocialComposer.h"
#include "../UltraSocialWebUtil.h"

#include <UltraNet/UltraNetOAuth2.h>

using UltraCanvas::JSONValue;

namespace UltraSocial {

namespace {

constexpr int kRedirectPort = 17997;
constexpr const char* kAuthBase = "https://www.linkedin.com";
constexpr const char* kApiBase  = "https://api.linkedin.com";
// Monthly API version header required by the /rest/ surface.
constexpr const char* kLinkedInVersion = "202501";

SocialCapabilities LinkedInCaps() {
    SocialCapabilities caps;
    caps.maxTextChars = 3000;
    caps.maxImages    = 0;   // images need the initializeUpload flow — later
    return caps;
}

std::vector<std::pair<std::string, std::string>> RestHeaders() {
    return {{"linkedin-version", kLinkedInVersion},
            {"x-restli-protocol-version", "2.0.0"}};
}

UltraNetResult CreatePost(const std::string& apiBase,
                          const std::string& accessToken,
                          const std::string& personUrn,
                          const std::string& text,
                          std::string& outPostUrn,
                          int* outStatus) {
    JSONValue body = JSONValue::MakeObject();
    body.Set("author", personUrn);
    body.Set("commentary", text);
    body.Set("visibility", "PUBLIC");
    body.Set("distribution",
             JSONValue::MakeObject()
                 .Set("feedDistribution", "MAIN_FEED")
                 .Set("targetEntities", JSONValue::MakeArray())
                 .Set("thirdPartyDistributionChannels", JSONValue::MakeArray()));
    body.Set("lifecycleState", "PUBLISHED");
    body.Set("isReshareDisabledByAuthor", false);

    JSONValue response;
    UltraNetHttpHeaders responseHeaders;
    auto post = JsonRequest(UltraNetHttpMethod::Post,
                            JoinUrl(apiBase, "/rest/posts"),
                            body, accessToken, response, outStatus,
                            RestHeaders(), &responseHeaders);
    // The created post's URN travels in a response header, not the body.
    outPostUrn = responseHeaders.Get("x-restli-id");
    return post;
}

} // namespace

SocialCapabilities LinkedInConnector::Capabilities() const {
    return LinkedInCaps();
}

UltraNetResult LinkedInConnector::Authenticate(const AuthInput& input,
                                               Account& outAccount,
                                               std::string& outCredentialJson) {
    outAccount = {};
    outCredentialJson.clear();
    if (input.clientId.empty() || input.secret.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "LinkedIn sign-in needs your app's client id and client secret "
            "(register an app with the 'Share on LinkedIn' and 'Sign In with "
            "LinkedIn' products, redirect http://127.0.0.1:17997/callback)");
    }
    if (!input.onOpenUrl) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "LinkedIn sign-in needs a browser callback (onOpenUrl)");
    }
    const std::string authBase = input.server.empty() ? kAuthBase : input.server;
    const std::string apiBase  = input.server.empty() ? kApiBase  : input.server;

    UltraNetOAuth2Config oauth;
    oauth.authorizationEndpoint = JoinUrl(authBase, "/oauth/v2/authorization");
    oauth.tokenEndpoint         = JoinUrl(authBase, "/oauth/v2/accessToken");
    oauth.clientId     = input.clientId;
    oauth.clientSecret = input.secret;
    oauth.secretInBody = true;    // LinkedIn wants the secret as a form field
    oauth.usePkce      = false;   // not supported
    oauth.scopes       = {"openid", "profile", "w_member_social"};
    oauth.redirectUri  = "http://127.0.0.1:" + std::to_string(kRedirectPort) +
                         "/callback";

    UltraNetOAuth2Token token;
    auto flow = UltraNet_OAuth2AuthorizeInteractive(oauth, input.onOpenUrl,
                                                    token);
    if (!flow) return flow;

    // OpenID userinfo supplies the member id ("sub") the post author URN needs.
    JSONValue user;
    auto userinfo = JsonRequest(UltraNetHttpMethod::Get,
                                JoinUrl(apiBase, "/v2/userinfo"),
                                JSONValue(), token.accessToken, user);
    if (!userinfo) return userinfo;
    const std::string sub = user["sub"].GetString();
    if (sub.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "userinfo returned no member id");
    }
    const std::string name = user["name"].GetString(sub);

    outAccount.network     = SocialNetwork::LinkedIn;
    outAccount.accountId   = MakeAccountId(SocialNetwork::LinkedIn, sub, "");
    outAccount.handle      = name;
    outAccount.displayName = name;
    outAccount.server      = apiBase;

    outCredentialJson = UltraCanvas::JSON::Serialize(
        JSONValue::MakeObject()
            .Set("auth_base", authBase)
            .Set("api_base", apiBase)
            .Set("client_id", input.clientId)
            .Set("client_secret", input.secret)
            .Set("access_token", token.accessToken)
            .Set("refresh_token", token.refreshToken)
            .Set("sub", sub)
            .Set("name", name));
    return UltraNetResult::Ok();
}

std::vector<std::string> LinkedInConnector::ValidateDraft(
    const AdaptedPost& post) const {
    return ValidateAdaptedPost(post, LinkedInCaps());
}

UltraNetResult LinkedInConnector::PublishPost(const Account& account,
                                              std::string& credentialJson,
                                              const AdaptedPost& post,
                                              PostResult& outResult) {
    outResult = {};
    JSONValue credentials = UltraCanvas::JSON::Parse(credentialJson);
    const std::string authBase = credentials["auth_base"].GetString(kAuthBase);
    const std::string apiBase  = credentials["api_base"].GetString(account.server);
    std::string accessToken    = credentials["access_token"].GetString();
    const std::string refresh  = credentials["refresh_token"].GetString();
    const std::string sub      = credentials["sub"].GetString();
    if (accessToken.empty() || sub.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "account has no stored session");
    }
    const std::string personUrn = "urn:li:person:" + sub;

    std::string postUrn;
    int status = 0;
    auto create = CreatePost(apiBase, accessToken, personUrn, post.text,
                             postUrn, &status);

    if (!create && status == 401 && !refresh.empty()) {
        JSONValue refreshed;
        auto refreshResult = FormPost(
            JoinUrl(authBase, "/oauth/v2/accessToken"),
            {{"grant_type", "refresh_token"},
             {"refresh_token", refresh},
             {"client_id", credentials["client_id"].GetString()},
             {"client_secret", credentials["client_secret"].GetString()}},
            std::string(), std::string(), std::string(), refreshed);
        if (!refreshResult) return refreshResult;
        accessToken = refreshed["access_token"].GetString();
        if (accessToken.empty()) {
            return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                         "token refresh returned no token");
        }
        credentials.Set("access_token", accessToken);
        if (!refreshed["refresh_token"].GetString().empty()) {
            credentials.Set("refresh_token", refreshed["refresh_token"]);
        }
        credentialJson = UltraCanvas::JSON::Serialize(credentials);

        create = CreatePost(apiBase, accessToken, personUrn, post.text,
                            postUrn, &status);
    }
    if (!create) return create;

    outResult.accountId = account.accountId;
    outResult.network   = SocialNetwork::LinkedIn;
    outResult.postId    = postUrn;
    if (!postUrn.empty()) {
        outResult.url = "https://www.linkedin.com/feed/update/" + postUrn;
    }
    return UltraNetResult::Ok();
}

} // namespace UltraSocial
