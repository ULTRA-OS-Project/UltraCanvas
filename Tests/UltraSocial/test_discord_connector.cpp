// Tests/UltraSocial/test_discord_connector.cpp
// The Discord connector against a scripted loopback fake: sign-in by
// fetching the pasted webhook, a text post and an image post through
// ?wait=true, Discord's {"message": ...} error shape, and a URL that is not
// a webhook refused before any request. No external network.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "fake_http_server.h"
#include "test_framework.h"

#include "UltraSocialConnector.h"

#include <cstdio>
#include <fstream>
#include <string>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

using namespace UltraSocial;
using ultrasocial_test::FakeHttpServer;

namespace {

struct TempMedia {
    std::string path;
    explicit TempMedia(const std::string& name, const std::string& bytes) {
        path = name;
        std::ofstream os(UltraCanvas::PathFromUtf8(path), std::ios::binary);
        os << bytes;
    }
    ~TempMedia() { std::remove(path.c_str()); }
};

std::string Credentials(const FakeHttpServer& server) {
    return R"({"webhook_url":")" + server.BaseUrl() +
           R"(/api/webhooks/42/TOKEN","channel_id":"7","guild_id":"9"})";
}

} // namespace

TEST(discord_authenticate_fetches_the_webhook) {
    FakeHttpServer server({
        {200, R"({"id":"42","name":"UltraSocial","channel_id":"7","guild_id":"9",)"
              R"("token":"TOKEN"})"}});

    auto connector = CreateConnector(SocialNetwork::Discord);
    REQUIRE(connector != nullptr);
    AuthInput input;
    // Pasted with a stray space and a query string: both are dropped.
    input.secret = " " + server.BaseUrl() + "/api/webhooks/42/TOKEN?wait=true ";

    Account account;
    std::string credentials;
    auto r = connector->Authenticate(input, account, credentials);
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(server.Requests()[0].Path(), std::string{"/api/webhooks/42/TOKEN"});
    REQUIRE(server.Requests()[0].startLine.rfind("GET ", 0) == 0);
    REQUIRE(account.network == SocialNetwork::Discord);
    REQUIRE_EQ(account.handle, std::string{"UltraSocial"});
    REQUIRE(credentials.find("/api/webhooks/42/TOKEN") != std::string::npos);
    REQUIRE(credentials.find("wait=true") == std::string::npos);
}

TEST(discord_auth_reports_unknown_webhook) {
    FakeHttpServer server({{404, R"({"message":"Unknown Webhook","code":10015})"}});

    auto connector = CreateConnector(SocialNetwork::Discord);
    AuthInput input;
    input.secret = server.BaseUrl() + "/api/webhooks/1/gone";
    Account account;
    std::string credentials;
    auto r = connector->Authenticate(input, account, credentials);
    server.Join();

    REQUIRE(!r);
    REQUIRE(r.message.find("Unknown Webhook") != std::string::npos);
}

TEST(discord_auth_refuses_a_url_that_is_not_a_webhook) {
    auto connector = CreateConnector(SocialNetwork::Discord);
    AuthInput input;
    input.secret = "https://discord.com/channels/9/7";
    Account account;
    std::string credentials;
    REQUIRE(!connector->Authenticate(input, account, credentials));
}

TEST(discord_publish_text_message) {
    FakeHttpServer server({{200, R"({"id":"1001","channel_id":"7"})"}});

    auto connector = CreateConnector(SocialNetwork::Discord);
    Account account;
    account.accountId = "discord-42";
    std::string credentials = Credentials(server);

    AdaptedPost post;
    post.network = SocialNetwork::Discord;
    post.text    = "hello discord";
    PostResult result;
    auto r = connector->PublishPost(account, credentials, post, result);
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(server.Requests()[0].Path(),
               std::string{"/api/webhooks/42/TOKEN?wait=true"});
    REQUIRE(server.Requests()[0].BodyHas(R"("content":"hello discord")"));
    REQUIRE_EQ(result.postId, std::string{"1001"});
    REQUIRE_EQ(result.url, std::string{"https://discord.com/channels/9/7/1001"});
}

TEST(discord_publish_image_as_multipart) {
    TempMedia media("discord-test-media.png", "PNGBYTES");
    FakeHttpServer server({{200, R"({"id":"1002","channel_id":"7"})"}});

    auto connector = CreateConnector(SocialNetwork::Discord);
    Account account;
    account.accountId = "discord-42";
    std::string credentials = Credentials(server);

    AdaptedPost post;
    post.network = SocialNetwork::Discord;
    post.text    = "with a picture";
    post.media   = {{media.path, "", "a cat"}};
    PostResult result;
    auto r = connector->PublishPost(account, credentials, post, result);
    server.Join();

    REQUIRE(bool(r));
    const auto& request = server.Requests()[0];
    REQUIRE(request.HeadHas("multipart/form-data"));
    REQUIRE(request.BodyHas("name=\"payload_json\""));
    REQUIRE(request.BodyHas("name=\"files[0]\""));
    REQUIRE(request.BodyHas("PNGBYTES"));
    REQUIRE(request.BodyHas("with a picture"));
    REQUIRE(request.BodyHas(R"("description":"a cat")"));
    REQUIRE_EQ(result.url, std::string{"https://discord.com/channels/9/7/1002"});
}

TEST(discord_network_slug_roundtrip) {
    REQUIRE_EQ(ToString(SocialNetwork::Discord), std::string{"discord"});
    REQUIRE(SocialNetworkFromString("discord") == SocialNetwork::Discord);
    REQUIRE_EQ(CreateConnector(SocialNetwork::Discord)->Capabilities().maxTextChars, 2000);
}
