// Apps/UltraSocial/engine/connectors/UltraSocialFacebookConnector.cpp
// Version: 0.1.0 (Phase 3)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialFacebookConnector.h"

#include "../UltraSocialComposer.h"
#include "../UltraSocialWebUtil.h"

#include <filesystem>

using UltraCanvas::JSONValue;

namespace UltraSocial {

namespace {

constexpr const char* kGraphBase   = "https://graph.facebook.com";
constexpr const char* kGraphPrefix = "/v21.0/";

SocialCapabilities FacebookCaps() {
    SocialCapabilities caps;
    caps.maxTextChars = 60000;   // effectively unbounded for a composer
    caps.maxImages    = 1;       // one photo + caption; albums are later
    return caps;
}

std::string PageUrl(const std::string& server, const std::string& pageId,
                    const std::string& tail) {
    return JoinUrl(server, kGraphPrefix + pageId + tail);
}

} // namespace

SocialCapabilities FacebookConnector::Capabilities() const {
    return FacebookCaps();
}

UltraNetResult FacebookConnector::Authenticate(const AuthInput& input,
                                               Account& outAccount,
                                               std::string& outCredentialJson) {
    outAccount = {};
    outCredentialJson.clear();
    if (input.identifier.empty() || input.secret.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "Facebook needs the Page id and a Page access token (create a "
            "Meta app, grant it pages_manage_posts for your Page, and "
            "generate a long-lived Page token)");
    }
    const std::string server = input.server.empty() ? kGraphBase : input.server;
    const std::string pageId = input.identifier;
    const std::string token  = input.secret;

    // The token works and really is for this Page?
    JSONValue page;
    auto verify = JsonRequest(
        UltraNetHttpMethod::Get,
        PageUrl(server, pageId, "?fields=id,name&access_token=" + token),
        JSONValue(), std::string(), page);
    if (!verify) return verify;
    const std::string name = page["name"].GetString();
    if (page["id"].GetString() != pageId || name.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "the token does not match this Page");
    }

    outAccount.network     = SocialNetwork::Facebook;
    outAccount.accountId   = MakeAccountId(SocialNetwork::Facebook, pageId, "");
    outAccount.handle      = name + " (Page)";
    outAccount.displayName = name;
    outAccount.server      = server;

    outCredentialJson = UltraCanvas::JSON::Serialize(
        JSONValue::MakeObject()
            .Set("server", server)
            .Set("page_id", pageId)
            .Set("page_token", token)
            .Set("page_name", name));
    return UltraNetResult::Ok();
}

std::vector<std::string> FacebookConnector::ValidateDraft(
    const AdaptedPost& post) const {
    return ValidateAdaptedPost(post, FacebookCaps());
}

UltraNetResult FacebookConnector::PublishPost(const Account& account,
                                              std::string& credentialJson,
                                              const AdaptedPost& post,
                                              PostResult& outResult) {
    outResult = {};
    JSONValue credentials = UltraCanvas::JSON::Parse(credentialJson);
    const std::string server = credentials["server"].GetString(account.server);
    const std::string pageId = credentials["page_id"].GetString();
    const std::string token  = credentials["page_token"].GetString();
    if (pageId.empty() || token.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "account has no stored Page token");
    }

    JSONValue created;
    if (post.media.empty()) {
        auto publish = FormPost(PageUrl(server, pageId, "/feed"),
                                {{"message", post.text},
                                 {"access_token", token}},
                                std::string(), std::string(), std::string(),
                                created);
        if (!publish) return publish;
    } else {
        const auto& media = post.media.front();
        std::vector<uint8_t> bytes;
        auto load = LoadFileBytes(media.filePath, bytes);
        if (!load) return load;

        MultipartFile source;
        source.name        = "source";
        source.fileName    = std::filesystem::path(media.filePath).filename().string();
        source.contentType = media.mimeType.empty() ? GuessMimeType(media.filePath)
                                                    : media.mimeType;
        source.bytes       = std::move(bytes);

        std::vector<MultipartField> fields = {{"access_token", token}};
        if (!post.text.empty()) fields.push_back({"caption", post.text});

        auto publish = MultipartPost(PageUrl(server, pageId, "/photos"),
                                     fields, {source}, std::string(), created);
        if (!publish) return publish;
    }

    // /feed returns "pageid_postid"; /photos returns the photo id plus
    // post_id for the story it created.
    std::string postId = created["post_id"].GetString(created["id"].GetString());
    outResult.accountId = account.accountId;
    outResult.network   = SocialNetwork::Facebook;
    outResult.postId    = postId;
    if (!postId.empty()) {
        outResult.url = "https://www.facebook.com/" + postId;
    }
    return UltraNetResult::Ok();
}

} // namespace UltraSocial
