// Tests/UltraIM/TelegramTdlibTests.cpp
// The Telegram provider's requests against the real TDLib (libtdjson), with
// no network: a client started, its parameters taken (to the phone number
// step), and every request the provider builds (TelegramRequests.h) handed
// to it - TDLib answers a request it cannot parse at once ("Failed to parse
// JSON object as TDLib request"), so any other answer, or none yet, means
// the names and fields are TDLib's. The library: ULTRAIM_TDJSON_PATH, else
// the one the configure step fetched into Plugins/UltraIM/tdlib; skips with
// neither unless ULTRAIM_TEST_TDLIB_REQUIRED is set (CI).
// Version: 0.1.2 - requestQrCodeAuthentication
// Version: 0.1.1 - the fetched libtdjson by default
// Version: 0.1.0
// Last Modified: 2026-10-28
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
        {"editMessageText", Request::EditText(777000, 1048576, "edited")},
        {"deleteMessages", Request::DeleteMessages(777000, {1048576, 2097152}, true)},
        {"viewMessages", Request::ViewMessages(777000, {1048576})},
        {"sendChatAction typing", Request::SendTyping(777000, true)},
        {"sendChatAction cancel", Request::SendTyping(777000, false)},
        {"searchPublicChat", Request::SearchPublicChat("telegram")},
        {"searchUserByPhoneNumber", Request::SearchUserByPhone("15550001")},
        {"createPrivateChat", Request::CreatePrivateChat(777000)},
        {"createNewBasicGroupChat", Request::CreateBasicGroup({777000}, "Team")},
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
