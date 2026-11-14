// Apps/UltraSocial/engine/connectors/UltraSocialDiscordConnector.cpp
// Version: 0.1.0 - Discord via a channel webhook
// Last Modified: 2026-11-14
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialDiscordConnector.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include "../UltraSocialComposer.h"
#include "../UltraSocialWebUtil.h"

#include <filesystem>

using UltraCanvas::JSONValue;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace UltraSocial {

namespace {

SocialCapabilities DiscordCaps() {
    SocialCapabilities caps;
    caps.maxTextChars  = 2000;              // message content limit
    caps.maxImages     = 10;                // attachments per message
    caps.maxImageBytes = 10 * 1000 * 1000;  // upload limit without boosts
    return caps;
}

std::string Trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// The webhook URL without a trailing slash or query (a pasted one sometimes
// carries ?wait=true or ?thread_id=...; the connector adds what it needs).
std::string NormalizeWebhookUrl(std::string url) {
    url = Trim(std::move(url));
    const auto query = url.find('?');
    if (query != std::string::npos) url.erase(query);
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

bool LooksLikeWebhookUrl(const std::string& url) {
    const bool scheme = url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
    return scheme && url.find("/api/webhooks/") != std::string::npos;
}

std::string MessageLink(const std::string& guildId, const std::string& channelId,
                        const std::string& messageId) {
    if (guildId.empty() || channelId.empty() || messageId.empty()) return {};
    return "https://discord.com/channels/" + guildId + "/" + channelId + "/" + messageId;
}

} // namespace

SocialCapabilities DiscordConnector::Capabilities() const {
    return DiscordCaps();
}

UltraNetResult DiscordConnector::Authenticate(const AuthInput& input,
                                              Account& outAccount,
                                              std::string& outCredentialJson) {
    outAccount = {};
    outCredentialJson.clear();
    const std::string url = NormalizeWebhookUrl(input.secret);
    if (url.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "Discord needs a channel webhook URL");
    }
    if (!LooksLikeWebhookUrl(url)) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "That is not a Discord webhook URL - copy it from the channel's "
            "Settings > Integrations > Webhooks (it contains /api/webhooks/)");
    }

    // GET on a webhook URL with its token returns the webhook itself - no
    // authorisation header, and a deleted webhook answers 404 Unknown Webhook.
    JSONValue webhook;
    auto get = JsonRequest(UltraNetHttpMethod::Get, url, JSONValue(), std::string(),
                           webhook);
    if (!get) return get;

    const std::string webhookId = webhook["id"].GetString();
    const std::string name      = webhook["name"].GetString("Discord webhook");
    const std::string channelId = webhook["channel_id"].GetString();
    const std::string guildId   = webhook["guild_id"].GetString();
    if (webhookId.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::HttpError,
                                     "Discord returned no webhook for that URL");
    }

    outAccount.network     = SocialNetwork::Discord;
    outAccount.accountId   = MakeAccountId(SocialNetwork::Discord, webhookId, "");
    outAccount.handle      = name;
    outAccount.displayName = name;
    outAccount.server      = "https://discord.com";

    // The URL carries the webhook's token, so it is the credential.
    outCredentialJson = UltraCanvas::JSON::Serialize(
        JSONValue::MakeObject()
            .Set("webhook_url", url)
            .Set("channel_id", channelId)
            .Set("guild_id", guildId));
    return UltraNetResult::Ok();
}

std::vector<std::string> DiscordConnector::ValidateDraft(const AdaptedPost& post) const {
    return ValidateAdaptedPost(post, DiscordCaps());
}

UltraNetResult DiscordConnector::PublishPost(const Account& account,
                                             std::string& credentialJson,
                                             const AdaptedPost& post,
                                             PostResult& outResult) {
    outResult = {};
    JSONValue credentials = UltraCanvas::JSON::Parse(credentialJson);
    const std::string url = credentials["webhook_url"].GetString();
    if (url.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "account has no stored webhook URL");
    }
    // wait=true makes Discord answer with the message it created (its id),
    // instead of an empty 204.
    const std::string postUrl = url + "?wait=true";

    JSONValue payload = JSONValue::MakeObject();
    if (!post.text.empty()) payload.Set("content", post.text);

    JSONValue message;
    if (post.media.empty()) {
        auto send = JsonRequest(UltraNetHttpMethod::Post, postUrl, payload,
                                std::string(), message);
        if (!send) return send;
    } else {
        // Multipart: the message as payload_json, each image as files[n],
        // listed in payload_json's attachments so a description (alt text)
        // goes with it.
        JSONValue attachments = JSONValue::MakeArray();
        std::vector<MultipartFile> files;
        for (std::size_t i = 0; i < post.media.size(); ++i) {
            const auto& media = post.media[i];
            std::vector<uint8_t> bytes;
            auto load = LoadFileBytes(media.filePath, bytes);
            if (!load) return load;

            MultipartFile file;
            file.name        = "files[" + std::to_string(i) + "]";
            file.fileName    = PathToUtf8(PathFromUtf8(media.filePath).filename());
            file.contentType = media.mimeType.empty() ? GuessMimeType(media.filePath)
                                                      : media.mimeType;
            file.bytes       = std::move(bytes);

            JSONValue attachment = JSONValue::MakeObject();
            attachment.Set("id", static_cast<int64_t>(i));
            attachment.Set("filename", file.fileName);
            if (!media.altText.empty()) attachment.Set("description", media.altText);
            attachments.Append(attachment);
            files.push_back(std::move(file));
        }
        payload.Set("attachments", attachments);

        auto send = MultipartPost(postUrl,
                                  {{"payload_json", UltraCanvas::JSON::Serialize(payload)}},
                                  files, std::string(), message);
        if (!send) return send;
    }

    const std::string messageId = message["id"].GetString();
    std::string channelId = message["channel_id"].GetString();
    if (channelId.empty()) channelId = credentials["channel_id"].GetString();

    outResult.accountId = account.accountId;
    outResult.network   = SocialNetwork::Discord;
    outResult.postId    = messageId;
    outResult.url       = MessageLink(credentials["guild_id"].GetString(), channelId,
                                      messageId);
    return UltraNetResult::Ok();
}

} // namespace UltraSocial
