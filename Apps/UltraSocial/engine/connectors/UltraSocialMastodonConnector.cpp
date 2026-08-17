// Apps/UltraSocial/engine/connectors/UltraSocialMastodonConnector.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialMastodonConnector.h"

#include "../UltraSocialComposer.h"
#include "../UltraSocialWebUtil.h"

#include <UltraNet/UltraNetOAuth2.h>
#include <UltraNet/UltraNetSocket.h>
#include <UltraNet/UltraNetUrl.h>

#include <chrono>
#include <filesystem>
#include <thread>

using UltraCanvas::JSONValue;

namespace UltraSocial {

namespace {

// The default 500-char limit is instance-configurable; the composer treats
// this as the safe floor.
SocialCapabilities MastodonCaps() {
    SocialCapabilities caps;
    caps.maxTextChars       = 500;
    caps.maxImages          = 4;
    caps.maxAltTextChars    = 1500;
    caps.supportsVisibility = true;
    return caps;
}

std::string NormalizeServer(std::string server) {
    while (!server.empty() && server.back() == '/') server.pop_back();
    if (server.rfind("http://", 0) != 0 && server.rfind("https://", 0) != 0) {
        server = "https://" + server;
    }
    return server;
}

std::string HostOf(const std::string& server) {
    UltraNetUrlComponents c;
    if (UltraNet_ParseUrl(server, c)) return c.host;
    return server;
}

// A loopback port for the OAuth redirect: Mastodon requires the exact
// redirect URI at app-registration time, before the OAuth flow can listen —
// so pick a free ephemeral port now and register with it. (The tiny window
// in which another process could grab the port just makes the flow fail
// visibly; retrying sign-in picks a new port.)
int FindFreeLoopbackPort() {
    UltraNetSocketOptions options;
    options.bindAddress  = "127.0.0.1";
    options.reuseAddress = true;
    UltraNetHandle listener = UltraNet_TcpListen(0, options);
    if (listener == UltraNetInvalidHandle) return 0;
    UltraNetEndpoint local;
    int port = UltraNet_SocketLocalEndpoint(listener, local) ? local.port : 0;
    UltraNet_SocketClose(listener);
    return port;
}

UltraNetResult VerifyCredentials(const std::string& server,
                                 const std::string& token,
                                 JSONValue& outUser) {
    return JsonRequest(UltraNetHttpMethod::Get,
                       JoinUrl(server, "/api/v1/accounts/verify_credentials"),
                       JSONValue(), token, outUser);
}

} // namespace

SocialCapabilities MastodonConnector::Capabilities() const {
    return MastodonCaps();
}

UltraNetResult MastodonConnector::Authenticate(const AuthInput& input,
                                               Account& outAccount,
                                               std::string& outCredentialJson) {
    outAccount = {};
    outCredentialJson.clear();
    if (input.server.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "a Mastodon server URL is required");
    }
    const std::string server = NormalizeServer(input.server);

    std::string accessToken = input.secret;   // pasted token path
    JSONValue credentials = JSONValue::MakeObject();
    credentials.Set("server", server);

    if (accessToken.empty()) {
        if (!input.onOpenUrl) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                "Mastodon sign-in needs a browser callback (onOpenUrl) or a "
                "pasted access token");
        }
        int port = FindFreeLoopbackPort();
        if (port == 0) {
            return UltraNetResult::Error(UltraNetResultCode::ConnectionRefused,
                                         "no free loopback port for the "
                                         "OAuth redirect");
        }
        const std::string redirectUri =
            "http://127.0.0.1:" + std::to_string(port) + "/callback";

        // Dynamic client registration — the instance mints our client id.
        JSONValue registration;
        auto reg = JsonRequest(UltraNetHttpMethod::Post,
                               JoinUrl(server, "/api/v1/apps"),
                               JSONValue::MakeObject()
                                   .Set("client_name", "UltraSocial")
                                   .Set("redirect_uris", redirectUri)
                                   .Set("scopes", "read write"),
                               std::string(), registration);
        if (!reg) return reg;
        const std::string clientId     = registration["client_id"].GetString();
        const std::string clientSecret = registration["client_secret"].GetString();
        if (clientId.empty()) {
            return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                "app registration returned no client_id");
        }

        UltraNetOAuth2Config oauth;
        oauth.authorizationEndpoint = JoinUrl(server, "/oauth/authorize");
        oauth.tokenEndpoint         = JoinUrl(server, "/oauth/token");
        oauth.clientId              = clientId;
        oauth.clientSecret          = clientSecret;
        oauth.secretInBody          = true;      // Mastodon expects form params
        oauth.scopes                = {"read", "write"};
        oauth.redirectUri           = redirectUri;

        UltraNetOAuth2Token token;
        auto flow = UltraNet_OAuth2AuthorizeInteractive(oauth, input.onOpenUrl,
                                                        token);
        if (!flow) return flow;
        accessToken = token.accessToken;
        credentials.Set("client_id", clientId);
        credentials.Set("client_secret", clientSecret);
    }

    JSONValue user;
    auto verify = VerifyCredentials(server, accessToken, user);
    if (!verify) return verify;
    const std::string username = user["username"].GetString();
    if (username.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
            "verify_credentials returned no username");
    }

    const std::string host = HostOf(server);
    outAccount.network     = SocialNetwork::Mastodon;
    outAccount.accountId   = MakeAccountId(SocialNetwork::Mastodon, host, username);
    outAccount.handle      = "@" + username + "@" + host;
    outAccount.displayName = user["display_name"].GetString(username);
    outAccount.server      = server;

    credentials.Set("access_token", accessToken);
    outCredentialJson = UltraCanvas::JSON::Serialize(credentials);
    return UltraNetResult::Ok();
}

std::vector<std::string> MastodonConnector::ValidateDraft(
    const AdaptedPost& post) const {
    return ValidateAdaptedPost(post, MastodonCaps());
}

UltraNetResult MastodonConnector::PublishPost(const Account& account,
                                              std::string& credentialJson,
                                              const AdaptedPost& post,
                                              PostResult& outResult) {
    outResult = {};
    JSONValue credentials = UltraCanvas::JSON::Parse(credentialJson);
    const std::string server =
        credentials["server"].GetString(account.server);
    const std::string token = credentials["access_token"].GetString();
    if (server.empty() || token.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "account has no stored access token");
    }

    // Media first: upload, then (on 202 Accepted) poll until processed.
    JSONValue mediaIds = JSONValue::MakeArray();
    for (const auto& media : post.media) {
        std::vector<uint8_t> bytes;
        auto load = LoadFileBytes(media.filePath, bytes);
        if (!load) return load;

        MultipartFile file;
        file.name        = "file";
        file.fileName    = std::filesystem::path(media.filePath).filename().string();
        file.contentType = media.mimeType.empty() ? GuessMimeType(media.filePath)
                                                  : media.mimeType;
        file.bytes       = std::move(bytes);

        std::vector<MultipartField> fields;
        if (!media.altText.empty()) fields.push_back({"description", media.altText});

        JSONValue uploaded;
        int status = 0;
        auto up = MultipartPost(JoinUrl(server, "/api/v2/media"),
                                fields, {file}, token, uploaded, &status);
        if (!up) return up;
        const std::string mediaId = uploaded["id"].GetString();
        if (mediaId.empty()) {
            return UltraNetResult::Error(UltraNetResultCode::HttpError,
                                         "media upload returned no id");
        }
        // 202 = still processing; the status endpoint 422s until it's done.
        for (int attempt = 0; status == 202 && attempt < 20; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            JSONValue processed;
            auto poll = JsonRequest(UltraNetHttpMethod::Get,
                                    JoinUrl(server, "/api/v1/media/" + mediaId),
                                    JSONValue(), token, processed, &status);
            if (!poll && status != 206) return poll;
        }
        mediaIds.Append(mediaId);
    }

    JSONValue body = JSONValue::MakeObject();
    body.Set("status", post.text);
    if (!post.visibility.empty()) body.Set("visibility", post.visibility);
    if (mediaIds.GetSize() > 0)   body.Set("media_ids", mediaIds);

    JSONValue posted;
    auto publish = JsonRequest(
        UltraNetHttpMethod::Post, JoinUrl(server, "/api/v1/statuses"),
        body, token, posted, nullptr,
        {{"idempotency-key", UltraNet_OAuth2GenerateState(16)}});
    if (!publish) return publish;

    outResult.accountId = account.accountId;
    outResult.network   = SocialNetwork::Mastodon;
    outResult.postId    = posted["id"].GetString();
    outResult.url       = posted["url"].GetString();
    return UltraNetResult::Ok();
}

} // namespace UltraSocial
