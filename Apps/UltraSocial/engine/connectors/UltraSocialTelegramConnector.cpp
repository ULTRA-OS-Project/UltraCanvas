// Apps/UltraSocial/engine/connectors/UltraSocialTelegramConnector.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialTelegramConnector.h"

#include "../UltraSocialComposer.h"
#include "../UltraSocialWebUtil.h"

#include <filesystem>

using UltraCanvas::JSONValue;

namespace UltraSocial {

namespace {

constexpr const char* kDefaultApi = "https://api.telegram.org";

SocialCapabilities TelegramCaps() {
    SocialCapabilities caps;
    caps.maxTextChars         = 4096;
    caps.maxMediaCaptionChars = 1024;
    caps.maxImages            = 10;    // sendMediaGroup album limit
    return caps;
}

std::string NormalizeServer(std::string server) {
    if (server.empty()) return kDefaultApi;
    while (!server.empty() && server.back() == '/') server.pop_back();
    return server;
}

std::string MethodUrl(const std::string& server, const std::string& botToken,
                      const std::string& method) {
    return JoinUrl(server, "/bot" + botToken + "/" + method);
}

// Bot API responses wrap everything: {"ok": true, "result": …} — and error
// bodies ({"ok": false, "description": …}) surface through JsonRequest.
UltraNetResult UnwrapResult(const UltraNetResult& r, const JSONValue& doc,
                            JSONValue& outResult) {
    if (!r) return r;
    if (!doc["ok"].GetBoolean(false)) {
        return UltraNetResult::Error(
            UltraNetResultCode::HttpError,
            doc["description"].GetString("Bot API reported a failure"));
    }
    outResult = doc["result"];
    return UltraNetResult::Ok();
}

} // namespace

SocialCapabilities TelegramConnector::Capabilities() const {
    return TelegramCaps();
}

UltraNetResult TelegramConnector::Authenticate(const AuthInput& input,
                                               Account& outAccount,
                                               std::string& outCredentialJson) {
    outAccount = {};
    outCredentialJson.clear();
    if (input.secret.empty() || input.identifier.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "Telegram needs a bot token and a channel (@name or chat id)");
    }
    const std::string server   = NormalizeServer(input.server);
    const std::string botToken = input.secret;
    const std::string chatId   = input.identifier;

    // The token works?
    JSONValue meDoc, me;
    auto getMe = JsonRequest(UltraNetHttpMethod::Get,
                             MethodUrl(server, botToken, "getMe"),
                             JSONValue(), std::string(), meDoc);
    auto meOk = UnwrapResult(getMe, meDoc, me);
    if (!meOk) return meOk;

    // The chat exists and the bot can see it?
    JSONValue chatDoc, chat;
    auto getChat = JsonRequest(UltraNetHttpMethod::Post,
                               MethodUrl(server, botToken, "getChat"),
                               JSONValue::MakeObject().Set("chat_id", chatId),
                               std::string(), chatDoc);
    auto chatOk = UnwrapResult(getChat, chatDoc, chat);
    if (!chatOk) return chatOk;

    const std::string chatUsername = chat["username"].GetString();
    outAccount.network     = SocialNetwork::Telegram;
    outAccount.accountId   = MakeAccountId(
        SocialNetwork::Telegram,
        chatUsername.empty() ? chatId : chatUsername,
        me["username"].GetString());
    outAccount.handle      = chatUsername.empty() ? chatId : "@" + chatUsername;
    outAccount.displayName = chat["title"].GetString(outAccount.handle);
    outAccount.server      = server;

    outCredentialJson = UltraCanvas::JSON::Serialize(
        JSONValue::MakeObject()
            .Set("server", server)
            .Set("bot_token", botToken)
            .Set("chat_id", chatId)
            .Set("chat_username", chatUsername));
    return UltraNetResult::Ok();
}

std::vector<std::string> TelegramConnector::ValidateDraft(
    const AdaptedPost& post) const {
    return ValidateAdaptedPost(post, TelegramCaps());
}

UltraNetResult TelegramConnector::PublishPost(const Account& account,
                                              std::string& credentialJson,
                                              const AdaptedPost& post,
                                              PostResult& outResult) {
    outResult = {};
    JSONValue credentials = UltraCanvas::JSON::Parse(credentialJson);
    const std::string server   = credentials["server"].GetString(kDefaultApi);
    const std::string botToken = credentials["bot_token"].GetString();
    const std::string chatId   = credentials["chat_id"].GetString();
    if (botToken.empty() || chatId.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "account has no stored bot token / chat");
    }

    JSONValue doc, message;
    if (post.media.empty()) {
        auto send = JsonRequest(UltraNetHttpMethod::Post,
                                MethodUrl(server, botToken, "sendMessage"),
                                JSONValue::MakeObject()
                                    .Set("chat_id", chatId)
                                    .Set("text", post.text),
                                std::string(), doc);
        auto ok = UnwrapResult(send, doc, message);
        if (!ok) return ok;
    } else if (post.media.size() == 1) {
        const auto& media = post.media.front();
        std::vector<uint8_t> bytes;
        auto load = LoadFileBytes(media.filePath, bytes);
        if (!load) return load;

        MultipartFile photo;
        photo.name        = "photo";
        photo.fileName    = std::filesystem::path(media.filePath).filename().string();
        photo.contentType = media.mimeType.empty() ? GuessMimeType(media.filePath)
                                                   : media.mimeType;
        photo.bytes       = std::move(bytes);

        std::vector<MultipartField> fields = {{"chat_id", chatId}};
        if (!post.text.empty()) fields.push_back({"caption", post.text});

        auto send = MultipartPost(MethodUrl(server, botToken, "sendPhoto"),
                                  fields, {photo}, std::string(), doc);
        auto ok = UnwrapResult(send, doc, message);
        if (!ok) return ok;
    } else {
        // Album: sendMediaGroup with each photo attached as multipart and
        // referenced via attach://<name>; the caption rides on the first.
        JSONValue mediaList = JSONValue::MakeArray();
        std::vector<MultipartFile> files;
        for (std::size_t i = 0; i < post.media.size(); ++i) {
            const auto& media = post.media[i];
            std::vector<uint8_t> bytes;
            auto load = LoadFileBytes(media.filePath, bytes);
            if (!load) return load;

            const std::string attachName = "photo" + std::to_string(i);
            MultipartFile photo;
            photo.name        = attachName;
            photo.fileName    = std::filesystem::path(media.filePath)
                                    .filename().string();
            photo.contentType = media.mimeType.empty()
                                    ? GuessMimeType(media.filePath)
                                    : media.mimeType;
            photo.bytes       = std::move(bytes);
            files.push_back(std::move(photo));

            JSONValue item = JSONValue::MakeObject();
            item.Set("type", "photo");
            item.Set("media", "attach://" + attachName);
            if (i == 0 && !post.text.empty()) item.Set("caption", post.text);
            mediaList.Append(item);
        }

        std::vector<MultipartField> fields = {
            {"chat_id", chatId},
            {"media", UltraCanvas::JSON::Serialize(mediaList)}};
        auto send = MultipartPost(MethodUrl(server, botToken, "sendMediaGroup"),
                                  fields, files, std::string(), doc);
        JSONValue messages;
        auto ok = UnwrapResult(send, doc, messages);
        if (!ok) return ok;
        message = messages[0];   // the album's first message anchors the link
    }

    const int64_t messageId = message["message_id"].GetInteger(0);
    outResult.accountId = account.accountId;
    outResult.network   = SocialNetwork::Telegram;
    outResult.postId    = std::to_string(messageId);
    const std::string chatUsername = credentials["chat_username"].GetString();
    if (!chatUsername.empty() && messageId > 0) {
        outResult.url = "https://t.me/" + chatUsername + "/" +
                        std::to_string(messageId);
    }
    return UltraNetResult::Ok();
}

} // namespace UltraSocial
