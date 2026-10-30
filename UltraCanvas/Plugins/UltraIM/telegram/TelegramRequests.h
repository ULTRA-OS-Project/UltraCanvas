// Plugins/UltraIM/telegram/TelegramRequests.h
// Telegram provider - the TDLib requests it sends, built in one place (td_api
// names and fields), so the tests can hand every one of them to a real TDLib
// and have it say whether it parses.
// Version: 0.1.4 - SendVideo and SendDocument take a preview picture (inputThumbnail)
// Version: 0.1.3 - DownloadFile; SendPhoto, SendVideo, SendDocument (a local file with a caption)
// Version: 0.1.2 - RequestQrCode
// Version: 0.1.1 - ResendCode
// Version: 0.1.0
// Last Modified: 2026-10-29
// Author: UltraCanvas Framework
#pragma once

#include "TdJson.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraIMTelegram {
namespace Request {

struct Parameters {
    bool        useTestDc = false;
    std::string databaseDirectory;
    std::string filesDirectory;
    std::string databaseKey;        // raw bytes; base64 in the JSON
    int32_t     apiId = 0;
    std::string apiHash;
    std::string languageCode = "en";
    std::string deviceModel = "ChatCube";
    std::string systemVersion;
    std::string applicationVersion;
};

TdObject GetVersion();          // getOption "version": what starts a client
TdObject SetTdlibParameters(const Parameters& p);
TdObject SetPhoneNumber(const std::string& phone);
TdObject RequestQrCode();       // requestQrCodeAuthentication: a link to scan from a signed-in app, instead of the number
TdObject CheckCode(const std::string& code);
TdObject ResendCode();          // the code by the next way (code_info.next_type)
TdObject CheckPassword(const std::string& password);
TdObject RegisterUser(const std::string& firstName, const std::string& lastName);
TdObject GetMe();
TdObject LoadChats(int32_t limit);              // the main list
TdObject GetChat(int64_t chatId);
TdObject GetChatHistory(int64_t chatId, int64_t fromMessageId, int32_t limit);
TdObject SendText(int64_t chatId, const std::string& text, int64_t replyToMessageId);
// A file of ours (inputFileLocal) as a photo, a video or a document, with a
// caption ("" for none). TDLib uploads it and reports the message as it
// goes out and once it is sent, as for a text.
TdObject SendPhoto(int64_t chatId, const std::string& localPath, const std::string& caption, int64_t replyToMessageId);
// A preview picture to send with a video or a document: a JPEG under 320
// pixels a side; "" for none.
struct Thumbnail {
    std::string localPath;
    int         width = 0;
    int         height = 0;
};
TdObject SendVideo(int64_t chatId, const std::string& localPath, const std::string& caption, int64_t replyToMessageId,
                   const Thumbnail& thumbnail = Thumbnail());
TdObject SendDocument(int64_t chatId, const std::string& localPath, const std::string& caption, int64_t replyToMessageId,
                      const Thumbnail& thumbnail = Thumbnail());
// Fetches a file TDLib knows (a photo's size, a video, a profile picture)
// to its files directory; priority 1 (lowest) to 32. The answer is the file
// as it stands, and updateFile follows as it comes in.
TdObject DownloadFile(int32_t fileId, int32_t priority);
TdObject EditText(int64_t chatId, int64_t messageId, const std::string& text);
TdObject DeleteMessages(int64_t chatId, const std::vector<int64_t>& ids, bool forEveryone);
TdObject ViewMessages(int64_t chatId, const std::vector<int64_t>& ids);
TdObject SendTyping(int64_t chatId, bool typing);
TdObject SearchPublicChat(const std::string& username);
TdObject SearchUserByPhone(const std::string& phone);
TdObject CreatePrivateChat(int64_t userId);
TdObject CreateBasicGroup(const std::vector<int64_t>& userIds, const std::string& title);
TdObject Close();

} // namespace Request
} // namespace UltraIMTelegram
