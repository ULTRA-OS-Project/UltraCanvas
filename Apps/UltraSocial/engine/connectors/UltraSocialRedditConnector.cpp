// Apps/UltraSocial/engine/connectors/UltraSocialRedditConnector.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialRedditConnector.h"

#include "../UltraSocialComposer.h"
#include "../UltraSocialWebUtil.h"

#include <UltraNet/UltraNetOAuth2.h>

#include <chrono>
#include <thread>

using UltraCanvas::JSONValue;

namespace UltraSocial {

namespace {

// The registered redirect URI must match exactly, so the port is fixed —
// the wizard tells the user to register this URI with their Reddit app.
constexpr int kRedirectPort = 17995;
constexpr const char* kAuthBase = "https://www.reddit.com";
constexpr const char* kApiBase  = "https://oauth.reddit.com";

SocialCapabilities RedditCaps() {
    SocialCapabilities caps;
    caps.maxTextChars = 40000;   // selftext limit; the title is derived
    caps.maxImages    = 0;       // media posts are a later extension
    return caps;
}

// Title = first line (Reddit caps titles at 300), body = the rest.
void SplitTitleBody(const std::string& text,
                    std::string& outTitle, std::string& outBody) {
    std::size_t eol = text.find('\n');
    std::string firstLine = eol == std::string::npos ? text
                                                     : text.substr(0, eol);
    outTitle = TruncateToPoints(firstLine, 300);
    outBody  = eol == std::string::npos ? std::string()
                                        : text.substr(eol + 1);
}

// "r/name" / "/r/name/" -> "name".
std::string NormalizeSubreddit(std::string s) {
    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    if (s.rfind("r/", 0) == 0) s.erase(0, 2);
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

// /api/submit wraps outcomes in {"json": {"errors": [[code, msg, field]],
// "data": {...}}} — flatten the first error to a message.
std::string SubmitErrors(const JSONValue& doc) {
    const JSONValue& errors = doc["json"]["errors"];
    if (errors.GetSize() == 0) return {};
    const JSONValue& first = errors[0];
    std::string message = first[0].GetString("submit failed");
    if (!first[1].GetString().empty()) message += ": " + first[1].GetString();
    return message;
}

UltraNetResult ExchangeOrRefresh(
    const std::string& authBase, const std::string& clientId,
    const std::vector<std::pair<std::string, std::string>>& fields,
    UltraNetOAuth2Token& outToken) {
    JSONValue doc;
    int status = 0;
    auto post = FormPost(JoinUrl(authBase, "/api/v1/access_token"), fields,
                         std::string(), clientId, std::string(), doc, &status);
    if (!post && doc.IsNull()) return post;
    auto parsed = UltraNet_OAuth2ParseTokenResponse(
        UltraCanvas::JSON::Serialize(doc), outToken);
    if (!parsed) parsed.httpStatus = status;
    return parsed;
}

} // namespace

SocialCapabilities RedditConnector::Capabilities() const {
    return RedditCaps();
}

UltraNetResult RedditConnector::Authenticate(const AuthInput& input,
                                             Account& outAccount,
                                             std::string& outCredentialJson) {
    outAccount = {};
    outCredentialJson.clear();
    if (input.clientId.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "Reddit sign-in needs your app's client id (register an "
            "'installed app' with redirect http://127.0.0.1:17995/callback)");
    }
    if (!input.onOpenUrl) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "Reddit sign-in needs a browser callback (onOpenUrl)");
    }
    const std::string authBase = input.server.empty() ? kAuthBase : input.server;
    const std::string apiBase  = input.server.empty() ? kApiBase  : input.server;
    const std::string redirectUri =
        "http://127.0.0.1:" + std::to_string(kRedirectPort) + "/callback";

    // Reddit has no PKCE; the code flow runs from the OAuth2 building
    // blocks with a plain state check. The callback listener starts first
    // (on this thread's stack via a helper thread) so the redirect can't
    // race the browser.
    UltraNetOAuth2Config urlConfig;
    urlConfig.authorizationEndpoint = JoinUrl(authBase, "/api/v1/authorize");
    urlConfig.clientId    = input.clientId;
    urlConfig.redirectUri = redirectUri;
    urlConfig.scopes      = {"identity", "submit"};
    urlConfig.extraAuthParams["duration"] = "permanent";
    const std::string state = UltraNet_OAuth2GenerateState();

    UltraNetOAuth2CallbackResult callback;
    UltraNetResult waitResult;
    std::thread waiter([&]() {
        waitResult = UltraNet_OAuth2WaitForCallback(redirectUri, callback,
                                                    300000);
    });
    // Give the listener a moment to bind before the browser can redirect.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    input.onOpenUrl(UltraNet_OAuth2BuildAuthUrl(urlConfig,
                                                UltraNetOAuth2Pkce{},   // no PKCE
                                                state));
    waiter.join();
    if (!waitResult) return waitResult;
    if (!callback.error.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "authorization denied: " + callback.error);
    }
    if (callback.state != state) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
            "state mismatch in authorization redirect (possible CSRF)");
    }

    UltraNetOAuth2Token token;
    auto exchange = ExchangeOrRefresh(
        authBase, input.clientId,
        {{"grant_type", "authorization_code"},
         {"code", callback.code},
         {"redirect_uri", redirectUri}},
        token);
    if (!exchange) return exchange;

    JSONValue me;
    auto whoami = JsonRequest(UltraNetHttpMethod::Get,
                              JoinUrl(apiBase, "/api/v1/me"),
                              JSONValue(), token.accessToken, me);
    if (!whoami) return whoami;
    const std::string username = me["name"].GetString();
    if (username.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "/api/v1/me returned no user name");
    }

    std::string subreddit = NormalizeSubreddit(input.identifier);
    if (subreddit.empty()) subreddit = "u_" + username;   // own profile

    outAccount.network     = SocialNetwork::Reddit;
    outAccount.accountId   = MakeAccountId(SocialNetwork::Reddit, username,
                                           subreddit);
    outAccount.handle      = "u/" + username + " → r/" + subreddit;
    outAccount.displayName = "u/" + username;
    outAccount.server      = apiBase;

    outCredentialJson = UltraCanvas::JSON::Serialize(
        JSONValue::MakeObject()
            .Set("auth_base", authBase)
            .Set("api_base", apiBase)
            .Set("client_id", input.clientId)
            .Set("access_token", token.accessToken)
            .Set("refresh_token", token.refreshToken)
            .Set("subreddit", subreddit)
            .Set("username", username));
    return UltraNetResult::Ok();
}

std::vector<std::string> RedditConnector::ValidateDraft(
    const AdaptedPost& post) const {
    return ValidateAdaptedPost(post, RedditCaps());
}

UltraNetResult RedditConnector::PublishPost(const Account& account,
                                            std::string& credentialJson,
                                            const AdaptedPost& post,
                                            PostResult& outResult) {
    outResult = {};
    JSONValue credentials = UltraCanvas::JSON::Parse(credentialJson);
    const std::string authBase  = credentials["auth_base"].GetString(kAuthBase);
    const std::string apiBase   = credentials["api_base"].GetString(account.server);
    const std::string clientId  = credentials["client_id"].GetString();
    std::string accessToken     = credentials["access_token"].GetString();
    const std::string refresh   = credentials["refresh_token"].GetString();
    const std::string subreddit = credentials["subreddit"].GetString();
    if (accessToken.empty() || subreddit.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "account has no stored session");
    }

    std::string title, body;
    SplitTitleBody(post.text, title, body);
    const std::vector<std::pair<std::string, std::string>> submission = {
        {"api_type", "json"},
        {"sr", subreddit},
        {"kind", "self"},
        {"title", title},
        {"text", body},
    };

    JSONValue doc;
    int status = 0;
    auto submit = FormPost(JoinUrl(apiBase, "/api/submit"), submission,
                           accessToken, std::string(), std::string(),
                           doc, &status);

    // Access tokens live an hour; refresh once on a 401.
    if (!submit && status == 401 && !refresh.empty() && !clientId.empty()) {
        UltraNetOAuth2Token token;
        auto refreshed = ExchangeOrRefresh(
            authBase, clientId,
            {{"grant_type", "refresh_token"}, {"refresh_token", refresh}},
            token);
        if (!refreshed) return refreshed;
        accessToken = token.accessToken;
        credentials.Set("access_token", accessToken);
        if (!token.refreshToken.empty()) {
            credentials.Set("refresh_token", token.refreshToken);
        }
        credentialJson = UltraCanvas::JSON::Serialize(credentials);

        submit = FormPost(JoinUrl(apiBase, "/api/submit"), submission,
                          accessToken, std::string(), std::string(),
                          doc, &status);
    }
    if (!submit) return submit;

    std::string submitError = SubmitErrors(doc);
    if (!submitError.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::HttpError, submitError);
    }

    outResult.accountId = account.accountId;
    outResult.network   = SocialNetwork::Reddit;
    outResult.postId    = doc["json"]["data"]["name"].GetString(
                              doc["json"]["data"]["id"].GetString());
    outResult.url       = doc["json"]["data"]["url"].GetString();
    return UltraNetResult::Ok();
}

} // namespace UltraSocial
