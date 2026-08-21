// Apps/UltraSocial/engine/connectors/UltraSocialBlueskyConnector.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialBlueskyConnector.h"

#include "../UltraSocialComposer.h"
#include "../UltraSocialWebUtil.h"

#include <ctime>

using UltraCanvas::JSONValue;

namespace UltraSocial {

namespace {

constexpr const char* kDefaultPds = "https://bsky.social";

SocialCapabilities BlueskyCaps() {
    SocialCapabilities caps;
    caps.maxTextChars    = 300;       // graphemes; code points approximate
    caps.maxImages       = 4;
    caps.maxImageBytes   = 1000000;   // the PDS blob limit for images
    caps.maxAltTextChars = 2000;
    return caps;
}

std::string NormalizeServer(std::string server) {
    if (server.empty()) return kDefaultPds;
    while (!server.empty() && server.back() == '/') server.pop_back();
    if (server.rfind("http://", 0) != 0 && server.rfind("https://", 0) != 0) {
        server = "https://" + server;
    }
    return server;
}

std::string NowIso8601() {
    std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32) || defined(_WIN64)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buf;
}

// at://did:plc:xyz/app.bsky.feed.post/<rkey> -> the record key.
std::string RecordKeyOf(const std::string& atUri) {
    std::size_t slash = atUri.find_last_of('/');
    return slash == std::string::npos ? std::string()
                                      : atUri.substr(slash + 1);
}

bool IsExpiredToken(const UltraNetResult& r, const JSONValue& errorDoc) {
    return !r && (r.httpStatus == 400 || r.httpStatus == 401) &&
           errorDoc["error"].GetString() == "ExpiredToken";
}

// One full publish attempt with a given access token: blob uploads + record.
UltraNetResult TryPublish(const std::string& server,
                          const std::string& accessJwt,
                          const std::string& did,
                          const AdaptedPost& post,
                          JSONValue& outCreated,
                          JSONValue& outErrorDoc) {
    JSONValue images = JSONValue::MakeArray();
    for (const auto& media : post.media) {
        std::vector<uint8_t> bytes;
        auto load = LoadFileBytes(media.filePath, bytes);
        if (!load) return load;

        JSONValue uploaded;
        int status = 0;
        auto up = RawPost(JoinUrl(server, "/xrpc/com.atproto.repo.uploadBlob"),
                          bytes,
                          media.mimeType.empty() ? GuessMimeType(media.filePath)
                                                 : media.mimeType,
                          accessJwt, uploaded, &status);
        if (!up) {
            outErrorDoc = uploaded;
            return up;
        }
        if (!uploaded.Contains("blob")) {
            return UltraNetResult::Error(UltraNetResultCode::HttpError,
                                         "uploadBlob returned no blob ref");
        }
        images.Append(JSONValue::MakeObject()
                          .Set("image", uploaded["blob"])
                          .Set("alt", media.altText));
    }

    JSONValue record = JSONValue::MakeObject();
    record.Set("$type", "app.bsky.feed.post");
    record.Set("text", post.text);
    record.Set("createdAt", NowIso8601());
    if (images.GetSize() > 0) {
        record.Set("embed", JSONValue::MakeObject()
                                .Set("$type", "app.bsky.embed.images")
                                .Set("images", images));
    }

    JSONValue body = JSONValue::MakeObject()
                         .Set("repo", did)
                         .Set("collection", "app.bsky.feed.post")
                         .Set("record", record);
    auto create = JsonRequest(
        UltraNetHttpMethod::Post,
        JoinUrl(server, "/xrpc/com.atproto.repo.createRecord"),
        body, accessJwt, outCreated);
    if (!create) outErrorDoc = outCreated;
    return create;
}

} // namespace

SocialCapabilities BlueskyConnector::Capabilities() const {
    return BlueskyCaps();
}

UltraNetResult BlueskyConnector::Authenticate(const AuthInput& input,
                                              Account& outAccount,
                                              std::string& outCredentialJson) {
    outAccount = {};
    outCredentialJson.clear();
    if (input.identifier.empty() || input.secret.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "Bluesky sign-in needs the handle (or email) and an app password");
    }
    const std::string server = NormalizeServer(input.server);

    JSONValue session;
    auto create = JsonRequest(
        UltraNetHttpMethod::Post,
        JoinUrl(server, "/xrpc/com.atproto.server.createSession"),
        JSONValue::MakeObject()
            .Set("identifier", input.identifier)
            .Set("password", input.secret),
        std::string(), session);
    if (!create) return create;

    const std::string did    = session["did"].GetString();
    const std::string handle = session["handle"].GetString(input.identifier);
    if (did.empty() || session["accessJwt"].GetString().empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "createSession returned no session");
    }

    outAccount.network     = SocialNetwork::Bluesky;
    outAccount.accountId   = MakeAccountId(SocialNetwork::Bluesky, handle, "");
    outAccount.handle      = handle;
    outAccount.displayName = handle;
    outAccount.server      = server;

    outCredentialJson = UltraCanvas::JSON::Serialize(
        JSONValue::MakeObject()
            .Set("server", server)
            .Set("did", did)
            .Set("handle", handle)
            .Set("access_jwt", session["accessJwt"])
            .Set("refresh_jwt", session["refreshJwt"]));
    return UltraNetResult::Ok();
}

std::vector<std::string> BlueskyConnector::ValidateDraft(
    const AdaptedPost& post) const {
    return ValidateAdaptedPost(post, BlueskyCaps());
}

UltraNetResult BlueskyConnector::PublishPost(const Account& account,
                                             std::string& credentialJson,
                                             const AdaptedPost& post,
                                             PostResult& outResult) {
    outResult = {};
    JSONValue credentials = UltraCanvas::JSON::Parse(credentialJson);
    const std::string server = credentials["server"].GetString(account.server);
    const std::string did    = credentials["did"].GetString();
    std::string accessJwt    = credentials["access_jwt"].GetString();
    std::string refreshJwt   = credentials["refresh_jwt"].GetString();
    const std::string handle = credentials["handle"].GetString(account.handle);
    if (did.empty() || accessJwt.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "account has no stored session");
    }

    JSONValue created, errorDoc;
    auto attempt = TryPublish(server, accessJwt, did, post, created, errorDoc);

    if (IsExpiredToken(attempt, errorDoc) && !refreshJwt.empty()) {
        // The refresh endpoint authenticates with the *refresh* token.
        JSONValue refreshed;
        auto refresh = JsonRequest(
            UltraNetHttpMethod::Post,
            JoinUrl(server, "/xrpc/com.atproto.server.refreshSession"),
            JSONValue(), refreshJwt, refreshed);
        if (!refresh) return refresh;
        accessJwt  = refreshed["accessJwt"].GetString();
        refreshJwt = refreshed["refreshJwt"].GetString(refreshJwt);
        if (accessJwt.empty()) {
            return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                         "refreshSession returned no session");
        }
        credentials.Set("access_jwt", accessJwt);
        credentials.Set("refresh_jwt", refreshJwt);
        credentialJson = UltraCanvas::JSON::Serialize(credentials);

        attempt = TryPublish(server, accessJwt, did, post, created, errorDoc);
    }
    if (!attempt) return attempt;

    const std::string atUri = created["uri"].GetString();
    outResult.accountId = account.accountId;
    outResult.network   = SocialNetwork::Bluesky;
    outResult.postId    = atUri;
    const std::string rkey = RecordKeyOf(atUri);
    if (!rkey.empty() && !handle.empty()) {
        outResult.url = "https://bsky.app/profile/" + handle + "/post/" + rkey;
    }
    return UltraNetResult::Ok();
}

} // namespace UltraSocial
