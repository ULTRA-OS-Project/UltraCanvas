// Plugins/UltraIM/telegram/TelegramRequests.cpp
// Telegram provider - the TDLib requests it sends
// Version: 0.1.3 - DownloadFile; SendPhoto, SendVideo, SendDocument
// Version: 0.1.2 - RequestQrCode
// Version: 0.1.1 - ResendCode
// Version: 0.1.0
// Last Modified: 2026-10-29
// Author: UltraCanvas Framework

#include "TelegramRequests.h"

#include "UltraCanvasTextUtils.h"

namespace UltraIMTelegram {
namespace Request {

namespace {

TdObject Make(const char* type) {
    TdObject o = TdObject::MakeObject();
    o.Set("@type", TdObject(type));
    return o;
}

TdObject Ids(const std::vector<int64_t>& ids) {
    TdObject list = TdObject::MakeArray();
    for (int64_t id : ids) list.Append(TdObject(id));
    return list;
}

TdObject Formatted(const std::string& text) {
    TdObject formatted = Make("formattedText");
    formatted.Set("text", TdObject(text));
    formatted.Set("entities", TdObject::MakeArray());
    return formatted;
}

TdObject Text(const std::string& text) {
    TdObject content = Make("inputMessageText");
    content.Set("text", Formatted(text));
    return content;
}

TdObject LocalFile(const std::string& localPath) {
    TdObject file = Make("inputFileLocal");
    file.Set("path", TdObject(localPath));
    return file;
}

TdObject SendContent(int64_t chatId, TdObject content, int64_t replyToMessageId) {
    TdObject o = Make("sendMessage");
    o.Set("chat_id", TdObject(chatId));
    if (replyToMessageId) {
        TdObject reply = Make("inputMessageReplyToMessage");
        reply.Set("message_id", TdObject(replyToMessageId));
        o.Set("reply_to", reply);
    }
    o.Set("input_message_content", content);
    return o;
}

} // namespace

TdObject GetVersion() {
    TdObject o = Make("getOption");
    o.Set("name", TdObject("version"));
    return o;
}

TdObject SetTdlibParameters(const Parameters& p) {
    TdObject o = Make("setTdlibParameters");
    o.Set("use_test_dc", TdObject(p.useTestDc));
    o.Set("database_directory", TdObject(p.databaseDirectory));
    o.Set("files_directory", TdObject(p.filesDirectory));
    o.Set("database_encryption_key",
          TdObject(UltraCanvas::Base64Encode(std::vector<uint8_t>(p.databaseKey.begin(), p.databaseKey.end()), false)));
    o.Set("use_file_database", TdObject(true));
    o.Set("use_chat_info_database", TdObject(true));
    o.Set("use_message_database", TdObject(true));
    o.Set("use_secret_chats", TdObject(false));
    o.Set("api_id", TdObject(static_cast<int64_t>(p.apiId)));
    o.Set("api_hash", TdObject(p.apiHash));
    o.Set("system_language_code", TdObject(p.languageCode));
    o.Set("device_model", TdObject(p.deviceModel));
    o.Set("system_version", TdObject(p.systemVersion));
    o.Set("application_version", TdObject(p.applicationVersion));
    return o;
}

TdObject SetPhoneNumber(const std::string& phone) {
    TdObject o = Make("setAuthenticationPhoneNumber");
    o.Set("phone_number", TdObject(phone));
    return o;
}

TdObject RequestQrCode() {
    TdObject o = Make("requestQrCodeAuthentication");
    o.Set("other_user_ids", TdObject::MakeArray());
    return o;
}

TdObject CheckCode(const std::string& code) {
    TdObject o = Make("checkAuthenticationCode");
    o.Set("code", TdObject(code));
    return o;
}

TdObject ResendCode() {
    return Make("resendAuthenticationCode");
}

TdObject CheckPassword(const std::string& password) {
    TdObject o = Make("checkAuthenticationPassword");
    o.Set("password", TdObject(password));
    return o;
}

TdObject RegisterUser(const std::string& firstName, const std::string& lastName) {
    TdObject o = Make("registerUser");
    o.Set("first_name", TdObject(firstName));
    o.Set("last_name", TdObject(lastName));
    return o;
}

TdObject GetMe() {
    return Make("getMe");
}

TdObject LoadChats(int32_t limit) {
    TdObject o = Make("loadChats");
    o.Set("chat_list", Make("chatListMain"));
    o.Set("limit", TdObject(static_cast<int64_t>(limit)));
    return o;
}

TdObject GetChat(int64_t chatId) {
    TdObject o = Make("getChat");
    o.Set("chat_id", TdObject(chatId));
    return o;
}

TdObject GetChatHistory(int64_t chatId, int64_t fromMessageId, int32_t limit) {
    TdObject o = Make("getChatHistory");
    o.Set("chat_id", TdObject(chatId));
    o.Set("from_message_id", TdObject(fromMessageId));
    o.Set("offset", TdObject(0));
    o.Set("limit", TdObject(static_cast<int64_t>(limit)));
    o.Set("only_local", TdObject(false));
    return o;
}

TdObject SendText(int64_t chatId, const std::string& text, int64_t replyToMessageId) {
    return SendContent(chatId, Text(text), replyToMessageId);
}

TdObject SendPhoto(int64_t chatId, const std::string& localPath, const std::string& caption, int64_t replyToMessageId) {
    // The size is left to TDLib, which reads the file; the server makes the
    // other sizes.
    TdObject content = Make("inputMessagePhoto");
    content.Set("photo", LocalFile(localPath));
    content.Set("added_sticker_file_ids", TdObject::MakeArray());
    content.Set("width", TdObject(0));
    content.Set("height", TdObject(0));
    content.Set("caption", Formatted(caption));
    return SendContent(chatId, content, replyToMessageId);
}

TdObject SendVideo(int64_t chatId, const std::string& localPath, const std::string& caption, int64_t replyToMessageId) {
    TdObject content = Make("inputMessageVideo");
    content.Set("video", LocalFile(localPath));
    content.Set("added_sticker_file_ids", TdObject::MakeArray());
    content.Set("duration", TdObject(0));
    content.Set("width", TdObject(0));
    content.Set("height", TdObject(0));
    content.Set("supports_streaming", TdObject(true));
    content.Set("caption", Formatted(caption));
    return SendContent(chatId, content, replyToMessageId);
}

TdObject SendDocument(int64_t chatId, const std::string& localPath, const std::string& caption, int64_t replyToMessageId) {
    TdObject content = Make("inputMessageDocument");
    content.Set("document", LocalFile(localPath));
    content.Set("disable_content_type_detection", TdObject(false));
    content.Set("caption", Formatted(caption));
    return SendContent(chatId, content, replyToMessageId);
}

TdObject DownloadFile(int32_t fileId, int32_t priority) {
    TdObject o = Make("downloadFile");
    o.Set("file_id", TdObject(static_cast<int64_t>(fileId)));
    o.Set("priority", TdObject(static_cast<int64_t>(priority)));
    o.Set("offset", TdObject(0));
    o.Set("limit", TdObject(0));
    o.Set("synchronous", TdObject(false));
    return o;
}

TdObject EditText(int64_t chatId, int64_t messageId, const std::string& text) {
    TdObject o = Make("editMessageText");
    o.Set("chat_id", TdObject(chatId));
    o.Set("message_id", TdObject(messageId));
    o.Set("input_message_content", Text(text));
    return o;
}

TdObject DeleteMessages(int64_t chatId, const std::vector<int64_t>& ids, bool forEveryone) {
    TdObject o = Make("deleteMessages");
    o.Set("chat_id", TdObject(chatId));
    o.Set("message_ids", Ids(ids));
    o.Set("revoke", TdObject(forEveryone));
    return o;
}

TdObject ViewMessages(int64_t chatId, const std::vector<int64_t>& ids) {
    TdObject o = Make("viewMessages");
    o.Set("chat_id", TdObject(chatId));
    o.Set("message_ids", Ids(ids));
    o.Set("force_read", TdObject(true));
    return o;
}

TdObject SendTyping(int64_t chatId, bool typing) {
    TdObject o = Make("sendChatAction");
    o.Set("chat_id", TdObject(chatId));
    o.Set("action", Make(typing ? "chatActionTyping" : "chatActionCancel"));
    return o;
}

TdObject SearchPublicChat(const std::string& username) {
    TdObject o = Make("searchPublicChat");
    o.Set("username", TdObject(username));
    return o;
}

TdObject SearchUserByPhone(const std::string& phone) {
    TdObject o = Make("searchUserByPhoneNumber");
    o.Set("phone_number", TdObject(phone));
    o.Set("only_local", TdObject(false));
    return o;
}

TdObject CreatePrivateChat(int64_t userId) {
    TdObject o = Make("createPrivateChat");
    o.Set("user_id", TdObject(userId));
    o.Set("force", TdObject(false));
    return o;
}

TdObject CreateBasicGroup(const std::vector<int64_t>& userIds, const std::string& title) {
    TdObject o = Make("createNewBasicGroupChat");
    o.Set("user_ids", Ids(userIds));
    o.Set("title", TdObject(title));
    o.Set("message_auto_delete_time", TdObject(0));
    return o;
}

TdObject Close() {
    return Make("close");
}

} // namespace Request
} // namespace UltraIMTelegram
