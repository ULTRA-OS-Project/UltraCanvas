// Tests/UltraIM/TelegramTdlibTests.cpp
// The Telegram provider's requests against the real TDLib (libtdjson), with
// no network: a client started, its parameters taken (to the phone number
// step), and every request the provider builds (TelegramRequests.h) handed
// to it - TDLib answers a request it cannot parse at once ("Failed to parse
// JSON object as TDLib request"), so any other answer, or none yet, means
// the names and fields are TDLib's. The library: ULTRAIM_TDJSON_PATH, else
// the one the configure step fetched into Plugins/UltraIM/tdlib; skips with
// neither unless ULTRAIM_TEST_TDLIB_REQUIRED is set (CI). The proxy requests
// are held to more than parsing - TDLib ignores a field it does not know, so
// a wrongly shaped addProxy parses and adds nothing: a proxy told before the
// parameters is the one in use after them, and can be listed, replaced,
// turned off and removed.
// Version: 0.1.7 - addProxy before the parameters, getProxies, disableProxy, removeProxy, setNetworkType
// Version: 0.1.6 - getSupergroupMembers
// Version: 0.1.5 - the group requests: setChatTitle, addChatMembers, setChatMemberStatus, leaveChat,
//                  deleteChatHistory, getBasicGroupFullInfo
// Version: 0.1.4 - searchChatMessages, with each filter
// Version: 0.1.3 - the media requests: a photo, a video, a document, downloadFile
// Version: 0.1.2 - requestQrCodeAuthentication
// Version: 0.1.1 - the fetched libtdjson by default
// Version: 0.1.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "TdJson.h"
#include "TelegramRequests.h"

#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace UltraIMTelegram;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

bool Parses(const TdObject& answer) {
    return !(TdType(answer) == "error" && answer["message"].GetString().find("Failed to parse") != std::string::npos);
}

void TestRequests() {
    std::string path = UltraCanvas::GetEnvUtf8("ULTRAIM_TDJSON_PATH");
    if (path.empty()) path = ULTRAIM_TEST_TDJSON_DEFAULT;
    const bool required = !UltraCanvas::GetEnvUtf8("ULTRAIM_TEST_TDLIB_REQUIRED").empty();
    std::string error;
    if (path.empty() || !TdJsonLibrary::Instance().Load({path}, error)) {
        std::printf("TelegramTdlibTests: %s (no libtdjson fetched at configure time; set ULTRAIM_TDJSON_PATH to one)%s%s\n",
                    required ? "FAIL" : "SKIP", error.empty() ? "" : "\n", error.c_str());
        CHECK(!required);
        return;
    }
    std::printf("  TDLib %s from %s\n", TdJsonLibrary::Instance().GetVersion().c_str(),
                TdJsonLibrary::Instance().GetPath().c_str());

    std::mutex mutex;
    std::condition_variable cv;
    std::string state;
    TdClient client;
    CHECK(client.Open([&](const TdObject& update) {
        if (TdType(update) != "updateAuthorizationState") return;
        std::lock_guard<std::mutex> lock(mutex);
        state = TdType(update["authorization_state"]);
        cv.notify_all();
    }, error));
    auto waitState = [&](const std::string& want) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(20), [&]() { return state == want; });
    };
    // A bad request is refused as unparsable - the check below can tell.
    TdObject bad = TdObject::MakeObject();
    bad.Set("@type", TdObject("sendMessage"));
    bad.Set("chat_id", TdObject("not a number"));
    CHECK(!Parses(client.Await(bad, 5000)));

    CHECK(TdType(client.Await(Request::GetVersion(), 10000)) == "optionValueString");
    CHECK(waitState("authorizationStateWaitTdlibParameters"));
    // The proxy before the parameters, as the provider tells it: TDLib keeps
    // the request and answers once they are in.
    Request::Proxy socks;
    socks.kind = Request::Proxy::Kind::Socks5;
    socks.server = "127.0.0.1";
    socks.port = 10808;
    TdObject early;
    bool earlyAnswered = false;
    client.Send(Request::AddProxy(socks), [&](const TdObject& answer) {
        std::lock_guard<std::mutex> lock(mutex);
        early = answer;
        earlyAnswered = true;
        cv.notify_all();
    });
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("ultraim-tdlib-" + std::to_string(::rand()));
    Request::Parameters p;
    p.useTestDc = true;
    p.databaseDirectory = UltraCanvas::PathToUtf8(dir / "td");
    p.filesDirectory = UltraCanvas::PathToUtf8(dir / "files");
    p.databaseKey = std::string(32, '\x5a');
    p.apiId = 12345;
    p.apiHash = "0123456789abcdef0123456789abcdef";
    p.systemVersion = "Linux";
    p.applicationVersion = "test";
    const TdObject taken = client.Await(Request::SetTdlibParameters(p), 10000);
    CHECK(TdType(taken) == "ok");
    CHECK(waitState("authorizationStateWaitPhoneNumber"));

    // The proxy told before the parameters is in use, under an id.
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(cv.wait_for(lock, std::chrono::seconds(20), [&]() { return earlyAnswered; }));
    }
    const int64_t socksId = TdInt(early["id"]);
    if (TdType(early) == "error") std::fprintf(stderr, "  addProxy: %s\n", early["message"].GetString().c_str());
    CHECK(TdType(early) != "error" && socksId != 0);
    // An entry of getProxies' answer: the proxy itself is inside it (TDLib
    // 1.8.69) or the entry (earlier ones).
    auto described = [](const TdObject& entry) { return entry["proxy"].IsObject() ? entry["proxy"] : entry; };
    auto inUse = [&](TdObject& out) {
        const TdObject list = client.Await(Request::GetProxies(), 10000);
        for (const auto& entry : list["proxies"].GetElements()) {
            if (!entry["is_enabled"].GetBoolean(false)) continue;
            out = entry;
            return true;
        }
        return false;
    };
    TdObject entry;
    CHECK(inUse(entry));
    CHECK(TdInt(entry["id"]) == socksId);
    CHECK(described(entry)["server"].GetString() == "127.0.0.1" && TdInt(described(entry)["port"]) == 10808);
    CHECK(TdType(described(entry)["type"]) == "proxyTypeSocks5");
    // The same one again is the one already there.
    CHECK(TdInt(client.Await(Request::AddProxy(socks), 10000)["id"]) == socksId);
    // An HTTP proxy with a sign-in takes its place.
    Request::Proxy http;
    http.kind = Request::Proxy::Kind::Http;
    http.server = "proxy.example";
    http.port = 3128;
    http.username = "user";
    http.password = "secret";
    const TdObject added = client.Await(Request::AddProxy(http), 10000);
    const int64_t httpId = TdInt(added["id"]);
    CHECK(TdType(added) != "error" && httpId != 0 && httpId != socksId);
    CHECK(inUse(entry));
    CHECK(TdInt(entry["id"]) == httpId);
    CHECK(described(entry)["server"].GetString() == "proxy.example" && TdInt(described(entry)["port"]) == 3128);
    CHECK(TdType(described(entry)["type"]) == "proxyTypeHttp");
    CHECK(described(entry)["type"]["username"].GetString() == "user");
    CHECK(described(entry)["type"]["password"].GetString() == "secret");
    CHECK(!described(entry)["type"]["http_only"].GetBoolean(true));
    // Turned off, both stay in the database until they are removed.
    CHECK(TdType(client.Await(Request::DisableProxy(), 10000)) == "ok");
    CHECK(!inUse(entry));
    CHECK(client.Await(Request::GetProxies(), 10000)["proxies"].GetElements().size() == 2);
    CHECK(TdType(client.Await(Request::RemoveProxy(static_cast<int32_t>(socksId)), 10000)) == "ok");
    CHECK(TdType(client.Await(Request::RemoveProxy(static_cast<int32_t>(httpId)), 10000)) == "ok");
    CHECK(client.Await(Request::GetProxies(), 10000)["proxies"].GetElements().empty());
    // Off the network and back on it.
    CHECK(TdType(client.Await(Request::SetNetworkAvailable(false), 10000)) == "ok");
    CHECK(TdType(client.Await(Request::SetNetworkAvailable(true), 10000)) == "ok");

    // Every request the provider sends. Without a network or a session TDLib
    // answers most "Unauthorized", or waits for the network - both after
    // parsing them.
    const std::vector<std::pair<const char*, TdObject>> requests = {
        {"getMe", Request::GetMe()},
        {"loadChats", Request::LoadChats(100)},
        {"getChat", Request::GetChat(777000)},
        {"getChatHistory", Request::GetChatHistory(777000, 1048576, 50)},
        {"sendMessage", Request::SendText(777000, "hello", 0)},
        {"sendMessage (reply)", Request::SendText(777000, "hello", 1048576)},
        {"sendMessage (photo)", Request::SendPhoto(777000, "/tmp/picture.jpg", "a caption", 0)},
        {"sendMessage (video)", Request::SendVideo(777000, "/tmp/clip.mp4", "", 1048576)},
        {"sendMessage (video with a preview)", Request::SendVideo(777000, "/tmp/clip.mp4", "", 0, Request::Thumbnail{"/tmp/clip.jpg", 320, 180})},
        {"sendMessage (document with a preview)", Request::SendDocument(777000, "/tmp/notes.pdf", "", 0, Request::Thumbnail{"/tmp/notes.jpg", 200, 260})},
        {"sendMessage (document)", Request::SendDocument(777000, "/tmp/notes.pdf", "", 0)},
        {"sendMessage (voice note)", Request::SendVoiceNote(777000, "/tmp/voice.ogg", 3, 1048576)},
        {"downloadFile", Request::DownloadFile(1, 32)},
        {"editMessageText", Request::EditText(777000, 1048576, "edited")},
        {"deleteMessages", Request::DeleteMessages(777000, {1048576, 2097152}, true)},
        {"viewMessages", Request::ViewMessages(777000, {1048576})},
        {"sendChatAction typing", Request::SendTyping(777000, true)},
        {"sendChatAction cancel", Request::SendTyping(777000, false)},
        {"searchChatMessages", Request::SearchChatMessages(777000, "hello", 0, 50, "searchMessagesFilterEmpty")},
        {"searchChatMessages (photos, a page on)", Request::SearchChatMessages(777000, "", 1048576, 100, "searchMessagesFilterPhoto")},
        {"searchChatMessages (videos)", Request::SearchChatMessages(777000, "", 0, 50, "searchMessagesFilterVideo")},
        {"searchChatMessages (photos and videos)", Request::SearchChatMessages(777000, "", 0, 50, "searchMessagesFilterPhotoAndVideo")},
        {"searchChatMessages (documents)", Request::SearchChatMessages(777000, "", 0, 50, "searchMessagesFilterDocument")},
        {"searchChatMessages (links)", Request::SearchChatMessages(777000, "", 0, 50, "searchMessagesFilterUrl")},
        {"searchPublicChat", Request::SearchPublicChat("telegram")},
        {"searchUserByPhoneNumber", Request::SearchUserByPhone("15550001")},
        {"createPrivateChat", Request::CreatePrivateChat(777000)},
        {"createNewBasicGroupChat", Request::CreateBasicGroup({777000}, "Team")},
        {"setChatTitle", Request::SetChatTitle(-4000, "Crew")},
        {"addChatMembers", Request::AddChatMembers(-4000, {777000})},
        {"setChatMemberStatus left", Request::RemoveChatMember(-4000, 777000)},
        {"leaveChat", Request::LeaveChat(-4000)},
        {"deleteChatHistory", Request::DeleteChatHistory(777000)},
        {"getBasicGroupFullInfo", Request::GetBasicGroupFullInfo(4000)},
        {"getSupergroupMembers", Request::GetSupergroupMembers(4000, 0, 200)},
        {"checkAuthenticationCode", Request::CheckCode("12345")},
        {"resendAuthenticationCode", Request::ResendCode()},
        {"checkAuthenticationPassword", Request::CheckPassword("x")},
        {"registerUser", Request::RegisterUser("Carol", "Smith")},
        {"setAuthenticationPhoneNumber", Request::SetPhoneNumber("+15550001")},
        {"requestQrCodeAuthentication", Request::RequestQrCode()},
    };
    for (const auto& request : requests) {
        const TdObject answer = client.Await(request.second, 3000);
        const bool ok = Parses(answer);
        if (!ok) std::fprintf(stderr, "  %s: %s\n", request.first, answer["message"].GetString().c_str());
        CHECK(ok);
    }
    client.Close(15000);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

} // namespace

int main() {
    TestRequests();
    std::printf("TelegramTdlibTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
