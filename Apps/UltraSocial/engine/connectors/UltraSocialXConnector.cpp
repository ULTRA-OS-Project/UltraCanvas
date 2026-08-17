// Apps/UltraSocial/engine/connectors/UltraSocialXConnector.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialXConnector.h"

#include "../UltraSocialComposer.h"
#include "../UltraSocialWebUtil.h"

#include <UltraNet/UltraNetOAuth2.h>

#include <filesystem>

using UltraCanvas::JSONValue;

namespace UltraSocial {

namespace {

// The registered redirect URI must match exactly, so the port is fixed —
// the wizard tells the user to register this URI with their X app.
constexpr int kRedirectPort = 17996;
constexpr const char* kAuthBase = "https://x.com";
constexpr const char* kApiBase  = "https://api.x.com";

SocialCapabilities XCaps() {
    SocialCapabilities caps;
    caps.maxTextChars  = 280;
    caps.maxImages     = 4;
    caps.maxImageBytes = 5 * 1024 * 1024;   // tweet_image upload limit
    return caps;
}

UltraNetOAuth2Config TokenConfig(const std::string& apiBase,
                                 const std::string& clientId) {
    UltraNetOAuth2Config config;
    config.tokenEndpoint = JoinUrl(apiBase, "/2/oauth2/token");
    config.clientId      = clientId;
    return config;
}

// One full publish attempt with a given access token: image uploads via the
// v2 media endpoint, then the tweet referencing their media ids.
UltraNetResult TryPublish(const std::string& apiBase,
                          const std::string& accessToken,
                          const AdaptedPost& post,
                          JSONValue& outCreated,
                          int* outStatus) {
    JSONValue mediaIds = JSONValue::MakeArray();
    for (const auto& media : post.media) {
        std::vector<uint8_t> bytes;
        auto load = LoadFileBytes(media.filePath, bytes);
        if (!load) return load;

        MultipartFile file;
        file.name        = "media";
        file.fileName    = std::filesystem::path(media.filePath).filename().string();
        file.contentType = media.mimeType.empty() ? GuessMimeType(media.filePath)
                                                  : media.mimeType;
        file.bytes       = std::move(bytes);

        JSONValue uploaded;
        auto upload = MultipartPost(JoinUrl(apiBase, "/2/media/upload"),
                                    {{"media_category", "tweet_image"}},
                                    {file}, accessToken, uploaded, outStatus);
        if (!upload) return upload;
        // v2 wraps the id in "data"; tolerate the v1.1-style field too.
        std::string mediaId = uploaded["data"]["id"].GetString(
                                  uploaded["media_id_string"].GetString());
        if (mediaId.empty()) {
            return UltraNetResult::Error(UltraNetResultCode::HttpError,
                                         "media upload returned no id");
        }
        mediaIds.Append(mediaId);
    }

    JSONValue body = JSONValue::MakeObject().Set("text", post.text);
    if (mediaIds.GetSize() > 0) {
        body.Set("media", JSONValue::MakeObject().Set("media_ids", mediaIds));
    }
    return JsonRequest(UltraNetHttpMethod::Post, JoinUrl(apiBase, "/2/tweets"),
                       body, accessToken, outCreated, outStatus);
}

} // namespace

SocialCapabilities XConnector::Capabilities() const {
    return XCaps();
}

UltraNetResult XConnector::Authenticate(const AuthInput& input,
                                        Account& outAccount,
                                        std::string& outCredentialJson) {
    outAccount = {};
    outCredentialJson.clear();
    if (input.clientId.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "X sign-in needs your app's OAuth2 client id (register an app "
            "with redirect http://127.0.0.1:17996/callback)");
    }
    if (!input.onOpenUrl) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "X sign-in needs a browser callback (onOpenUrl)");
    }
    const std::string authBase = input.server.empty() ? kAuthBase : input.server;
    const std::string apiBase  = input.server.empty() ? kApiBase  : input.server;

    UltraNetOAuth2Config oauth = TokenConfig(apiBase, input.clientId);
    oauth.authorizationEndpoint = JoinUrl(authBase, "/i/oauth2/authorize");
    oauth.scopes      = {"tweet.read", "tweet.write", "users.read",
                         "offline.access"};
    oauth.redirectUri = "http://127.0.0.1:" + std::to_string(kRedirectPort) +
                        "/callback";

    UltraNetOAuth2Token token;
    auto flow = UltraNet_OAuth2AuthorizeInteractive(oauth, input.onOpenUrl,
                                                    token);
    if (!flow) return flow;

    JSONValue me;
    auto whoami = JsonRequest(UltraNetHttpMethod::Get,
                              JoinUrl(apiBase, "/2/users/me"),
                              JSONValue(), token.accessToken, me);
    if (!whoami) return whoami;
    const std::string username = me["data"]["username"].GetString();
    if (username.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "/2/users/me returned no username");
    }

    outAccount.network     = SocialNetwork::X;
    outAccount.accountId   = MakeAccountId(SocialNetwork::X, username, "");
    outAccount.handle      = "@" + username;
    outAccount.displayName = me["data"]["name"].GetString(username);
    outAccount.server      = apiBase;

    outCredentialJson = UltraCanvas::JSON::Serialize(
        JSONValue::MakeObject()
            .Set("api_base", apiBase)
            .Set("client_id", input.clientId)
            .Set("access_token", token.accessToken)
            .Set("refresh_token", token.refreshToken)
            .Set("username", username));
    return UltraNetResult::Ok();
}

std::vector<std::string> XConnector::ValidateDraft(
    const AdaptedPost& post) const {
    return ValidateAdaptedPost(post, XCaps());
}

UltraNetResult XConnector::PublishPost(const Account& account,
                                       std::string& credentialJson,
                                       const AdaptedPost& post,
                                       PostResult& outResult) {
    outResult = {};
    JSONValue credentials = UltraCanvas::JSON::Parse(credentialJson);
    const std::string apiBase  = credentials["api_base"].GetString(account.server);
    const std::string clientId = credentials["client_id"].GetString();
    std::string accessToken    = credentials["access_token"].GetString();
    std::string refreshToken   = credentials["refresh_token"].GetString();
    if (accessToken.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "account has no stored session");
    }

    JSONValue created;
    int status = 0;
    auto tweet = TryPublish(apiBase, accessToken, post, created, &status);

    if (!tweet && status == 401 && !refreshToken.empty() && !clientId.empty()) {
        // X rotates the refresh token on every refresh — persist both.
        UltraNetOAuth2Token token;
        auto refreshed = UltraNet_OAuth2Refresh(TokenConfig(apiBase, clientId),
                                                refreshToken, token);
        if (!refreshed) return refreshed;
        accessToken  = token.accessToken;
        refreshToken = token.refreshToken;
        credentials.Set("access_token", accessToken);
        credentials.Set("refresh_token", refreshToken);
        credentialJson = UltraCanvas::JSON::Serialize(credentials);

        tweet = TryPublish(apiBase, accessToken, post, created, &status);
    }
    if (!tweet) return tweet;

    const std::string tweetId = created["data"]["id"].GetString();
    outResult.accountId = account.accountId;
    outResult.network   = SocialNetwork::X;
    outResult.postId    = tweetId;
    const std::string username = credentials["username"].GetString();
    if (!tweetId.empty() && !username.empty()) {
        outResult.url = "https://x.com/" + username + "/status/" + tweetId;
    }
    return UltraNetResult::Ok();
}

} // namespace UltraSocial
