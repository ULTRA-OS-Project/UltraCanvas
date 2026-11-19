// Plugins/UltraIM/telegram/TelegramRequests.h
// Telegram provider - the TDLib requests it sends, built in one place (td_api
// names and fields), so the tests can hand every one of them to a real TDLib
// and have it say whether it parses.
// Version: 0.1.8 - the proxy TDLib connects through: AddProxy, DisableProxy, GetProxies, RemoveProxy;
//                  SetNetworkAvailable
// Version: 0.1.7 - GetSupergroupMembers (a page of them, the recent ones first)
// Version: 0.1.6 - a group after it is made: SetChatTitle, AddChatMembers, RemoveChatMember, LeaveChat,
//                  DeleteChatHistory, GetBasicGroupFullInfo
// Version: 0.1.5 - SearchChatMessages
// Version: 0.1.5 - SendVoiceNote (inputMessageVoiceNote: an Ogg Opus recording and its length)
// Version: 0.1.4 - SendVideo and SendDocument take a preview picture (inputThumbnail)
// Version: 0.1.3 - DownloadFile; SendPhoto, SendVideo, SendDocument (a local file with a caption)
// Version: 0.1.2 - RequestQrCode
// Version: 0.1.1 - ResendCode
// Version: 0.1.0
// Last Modified: 2026-11-18
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
// The proxy TDLib connects through - its networking is its own, so a proxy
// is named to it: SOCKS 5, or an HTTP proxy that tunnels (CONNECT), with the
// sign-in the proxy asks for ("" for none). TDLib takes no other kind.
struct Proxy {
    enum class Kind { Socks5, Http };
    Kind        kind = Kind::Socks5;
    std::string server;         // a name or an address
    int32_t     port = 0;
    std::string username;
    std::string password;

    bool operator==(const Proxy&) const = default;
};
// Adds the proxy to the ones TDLib's database keeps (the same one again is
// the one already there) and connects through it from now on; the answer's
// `id` names it. TDLib takes it before setTdlibParameters and applies it
// with them, so no connection is made without it. Written in both forms the
// request has had - `proxy` as an object (what TDLib 1.8.69, the one the
// build fetches, takes) and its fields beside `enable` (earlier ones): TDLib
// reads the fields it knows and ignores the rest.
TdObject AddProxy(const Proxy& proxy);
TdObject DisableProxy();        // every connection direct; the proxies stay in the database
TdObject GetProxies();          // the proxies in the database, each with its `id`
TdObject RemoveProxy(int32_t proxyId);
// TDLib on the network or off it (setNetworkType: networkTypeOther /
// networkTypeNone): off, it makes no connection at all until it is told
// otherwise.
TdObject SetNetworkAvailable(bool available);
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
// A voice message: an Ogg Opus recording (Telegram plays nothing else as
// one) and its length in whole seconds.
TdObject SendVoiceNote(int64_t chatId, const std::string& localPath, int32_t durationSeconds, int64_t replyToMessageId);
// Fetches a file TDLib knows (a photo's size, a video, a profile picture)
// to its files directory; priority 1 (lowest) to 32. The answer is the file
// as it stands, and updateFile follows as it comes in.
TdObject DownloadFile(int32_t fileId, int32_t priority);
TdObject EditText(int64_t chatId, int64_t messageId, const std::string& text);
TdObject DeleteMessages(int64_t chatId, const std::vector<int64_t>& ids, bool forEveryone);
TdObject ViewMessages(int64_t chatId, const std::vector<int64_t>& ids);
TdObject SendTyping(int64_t chatId, bool typing);
// Telegram's own search of one chat, newest first: `query` ("" = everything
// the filter lets through), older than fromMessageId (0 = from the newest),
// `filter` a SearchMessagesFilter's name ("searchMessagesFilterEmpty",
// "...Photo", "...Video", "...PhotoAndVideo", "...Document", "...Url").
// The answer (foundChatMessages) names where the next page starts.
TdObject SearchChatMessages(int64_t chatId, const std::string& query, int64_t fromMessageId, int32_t limit,
                            const std::string& filter);
TdObject SearchPublicChat(const std::string& username);
TdObject SearchUserByPhone(const std::string& phone);
TdObject CreatePrivateChat(int64_t userId);
TdObject CreateBasicGroup(const std::vector<int64_t>& userIds, const std::string& title);
// A group after it is made: its name, members in (addChatMembers) and out
// (setChatMemberStatus to left - a kick, in a supergroup too), leaving it;
// a private chat's history deleted for us alone and the chat off the list;
// a basic group's members and creator (basicGroupFullInfo).
TdObject SetChatTitle(int64_t chatId, const std::string& title);
TdObject AddChatMembers(int64_t chatId, const std::vector<int64_t>& userIds);
TdObject RemoveChatMember(int64_t chatId, int64_t userId);
TdObject LeaveChat(int64_t chatId);
TdObject DeleteChatHistory(int64_t chatId);
TdObject GetBasicGroupFullInfo(int64_t basicGroupId);
// A supergroup's members, `limit` (up to 200) from `offset`, and how many there are (chatMembers).
TdObject GetSupergroupMembers(int64_t supergroupId, int32_t offset, int32_t limit);
TdObject Close();

} // namespace Request
} // namespace UltraIMTelegram
