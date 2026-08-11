// Tests/UltraSocial/test_connectors.cpp
// The three Phase-1 connectors against scripted loopback HTTP fakes:
// authentication, publishing (JSON + multipart), error surfacing, and the
// Bluesky expired-token refresh. No external network.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "fake_http_server.h"
#include "test_framework.h"

#include "UltraSocialConnector.h"

#include <cstdio>
#include <fstream>
#include <string>

using namespace UltraSocial;
using ultrasocial_test::FakeHttpServer;
using ultrasocial_test::ScriptedResponse;

namespace {

// A tiny "image" file on disk for media tests.
struct TempMedia {
    std::string path;
    explicit TempMedia(const std::string& name, const std::string& bytes = "PNGDATA") {
        path = name;
        std::ofstream os(path, std::ios::binary);
        os << bytes;
    }
    ~TempMedia() { std::remove(path.c_str()); }
};

AdaptedPost TextPost(SocialNetwork network, const std::string& text) {
    AdaptedPost post;
    post.network = network;
    post.text    = text;
    return post;
}

} // namespace

// ============================================================================
// Factory + capabilities
// ============================================================================
TEST(connector_factory_and_capabilities) {
    for (auto network : {SocialNetwork::Mastodon, SocialNetwork::Bluesky,
                         SocialNetwork::Telegram}) {
        auto connector = CreateConnector(network);
        REQUIRE(connector != nullptr);
        REQUIRE(connector->Network() == network);
        REQUIRE(connector->Capabilities().maxTextChars > 0);
    }
    REQUIRE(CreateConnector(SocialNetwork::Bluesky)->Capabilities().maxImageBytes
            == 1000000);
}

// ============================================================================
// Mastodon
// ============================================================================
TEST(mastodon_authenticate_with_pasted_token) {
    FakeHttpServer server({
        {200, R"({"username":"erika","display_name":"Erika","acct":"erika"})"}});

    auto connector = CreateConnector(SocialNetwork::Mastodon);
    AuthInput input;
    input.server = server.BaseUrl();
    input.secret = "PASTED_TOKEN";

    Account account;
    std::string credentials;
    auto r = connector->Authenticate(input, account, credentials);
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(server.Requests().size(), std::size_t{1});
    REQUIRE_EQ(server.Requests()[0].Path(),
               std::string{"/api/v1/accounts/verify_credentials"});
    REQUIRE(server.Requests()[0].HeadHas("Bearer PASTED_TOKEN"));
    REQUIRE_EQ(account.displayName, std::string{"Erika"});
    REQUIRE(account.handle.rfind("@erika@", 0) == 0);
    REQUIRE(credentials.find("PASTED_TOKEN") != std::string::npos);
}

TEST(mastodon_authenticate_requires_server) {
    auto connector = CreateConnector(SocialNetwork::Mastodon);
    Account account;
    std::string credentials;
    REQUIRE(!connector->Authenticate({}, account, credentials));
}

TEST(mastodon_publish_text_only) {
    FakeHttpServer server({
        {200, R"({"id":"42","url":"https://m.social/@e/42"})"}});

    auto connector = CreateConnector(SocialNetwork::Mastodon);
    Account account;
    account.accountId = "mastodon-test";
    std::string credentials = R"({"server":")" + server.BaseUrl() +
                              R"(","access_token":"TOK"})";

    PostResult result;
    auto r = connector->PublishPost(account, credentials,
                                    TextPost(SocialNetwork::Mastodon, "hello"),
                                    result);
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(result.postId, std::string{"42"});
    REQUIRE_EQ(result.url, std::string{"https://m.social/@e/42"});
    const auto& request = server.Requests()[0];
    REQUIRE_EQ(request.Path(), std::string{"/api/v1/statuses"});
    REQUIRE(request.HeadHas("Bearer TOK"));
    REQUIRE(request.HeadHas("idempotency-key:"));
    REQUIRE(request.BodyHas(R"("status":"hello")"));
    REQUIRE(request.BodyHas(R"("visibility":"public")"));
}

TEST(mastodon_publish_with_media) {
    TempMedia media("mastodon-test-media.png");
    FakeHttpServer server({
        {200, R"({"id":"m1","type":"image"})"},                 // media upload
        {200, R"({"id":"77","url":"https://m.social/@e/77"})"}}); // status

    auto connector = CreateConnector(SocialNetwork::Mastodon);
    Account account;
    account.accountId = "mastodon-test";
    std::string credentials = R"({"server":")" + server.BaseUrl() +
                              R"(","access_token":"TOK"})";

    AdaptedPost post = TextPost(SocialNetwork::Mastodon, "with pic");
    post.media = {{media.path, "", "an alt text"}};

    PostResult result;
    auto r = connector->PublishPost(account, credentials, post, result);
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(server.Requests().size(), std::size_t{2});
    const auto& upload = server.Requests()[0];
    REQUIRE_EQ(upload.Path(), std::string{"/api/v2/media"});
    REQUIRE(upload.HeadHas("multipart/form-data; boundary="));
    REQUIRE(upload.BodyHas("name=\"file\""));
    REQUIRE(upload.BodyHas("PNGDATA"));
    REQUIRE(upload.BodyHas("an alt text"));
    const auto& status = server.Requests()[1];
    REQUIRE(status.BodyHas(R"("media_ids":["m1"])"));
    REQUIRE_EQ(result.postId, std::string{"77"});
}

TEST(mastodon_publish_surfaces_server_error) {
    FakeHttpServer server({
        {422, R"({"error":"Validation failed: Text too long"})"}});

    auto connector = CreateConnector(SocialNetwork::Mastodon);
    Account account;
    account.accountId = "mastodon-test";
    std::string credentials = R"({"server":")" + server.BaseUrl() +
                              R"(","access_token":"TOK"})";
    PostResult result;
    auto r = connector->PublishPost(account, credentials,
                                    TextPost(SocialNetwork::Mastodon, "x"),
                                    result);
    server.Join();

    REQUIRE(!r);
    REQUIRE(r.message.find("Text too long") != std::string::npos);
    REQUIRE_EQ(r.httpStatus, 422);
}

// ============================================================================
// Bluesky
// ============================================================================
TEST(bluesky_authenticate_creates_session) {
    FakeHttpServer server({
        {200, R"({"did":"did:plc:abc","handle":"e.bsky.social",)"
              R"("accessJwt":"AJ","refreshJwt":"RJ"})"}});

    auto connector = CreateConnector(SocialNetwork::Bluesky);
    AuthInput input;
    input.server     = server.BaseUrl();
    input.identifier = "e.bsky.social";
    input.secret     = "app-password";

    Account account;
    std::string credentials;
    auto r = connector->Authenticate(input, account, credentials);
    server.Join();

    REQUIRE(bool(r));
    const auto& request = server.Requests()[0];
    REQUIRE_EQ(request.Path(),
               std::string{"/xrpc/com.atproto.server.createSession"});
    REQUIRE(request.BodyHas(R"("identifier":"e.bsky.social")"));
    REQUIRE(request.BodyHas(R"("password":"app-password")"));
    REQUIRE_EQ(account.handle, std::string{"e.bsky.social"});
    REQUIRE(credentials.find("did:plc:abc") != std::string::npos);
    REQUIRE(credentials.find("\"access_jwt\":\"AJ\"") != std::string::npos);
}

TEST(bluesky_publish_text_builds_record) {
    FakeHttpServer server({
        {200, R"({"uri":"at://did:plc:abc/app.bsky.feed.post/rk123","cid":"c"})"}});

    auto connector = CreateConnector(SocialNetwork::Bluesky);
    Account account;
    account.accountId = "bluesky-test";
    std::string credentials =
        R"({"server":")" + server.BaseUrl() +
        R"(","did":"did:plc:abc","handle":"e.bsky.social",)"
        R"("access_jwt":"AJ","refresh_jwt":"RJ"})";

    PostResult result;
    auto r = connector->PublishPost(account, credentials,
                                    TextPost(SocialNetwork::Bluesky, "hi sky"),
                                    result);
    server.Join();

    REQUIRE(bool(r));
    const auto& request = server.Requests()[0];
    REQUIRE_EQ(request.Path(),
               std::string{"/xrpc/com.atproto.repo.createRecord"});
    REQUIRE(request.HeadHas("Bearer AJ"));
    REQUIRE(request.BodyHas(R"("repo":"did:plc:abc")"));
    REQUIRE(request.BodyHas(R"("collection":"app.bsky.feed.post")"));
    REQUIRE(request.BodyHas(R"("$type":"app.bsky.feed.post")"));
    REQUIRE(request.BodyHas(R"("text":"hi sky")"));
    REQUIRE(request.BodyHas("\"createdAt\":\"2"));   // ISO-8601 UTC
    REQUIRE_EQ(result.postId,
               std::string{"at://did:plc:abc/app.bsky.feed.post/rk123"});
    REQUIRE_EQ(result.url,
               std::string{"https://bsky.app/profile/e.bsky.social/post/rk123"});
}

TEST(bluesky_publish_refreshes_expired_token) {
    FakeHttpServer server({
        {400, R"({"error":"ExpiredToken","message":"Token has expired"})"},
        {200, R"({"accessJwt":"AJ2","refreshJwt":"RJ2"})"},
        {200, R"({"uri":"at://did:plc:abc/app.bsky.feed.post/rk9","cid":"c"})"}});

    auto connector = CreateConnector(SocialNetwork::Bluesky);
    Account account;
    account.accountId = "bluesky-test";
    std::string credentials =
        R"({"server":")" + server.BaseUrl() +
        R"(","did":"did:plc:abc","handle":"e.bsky.social",)"
        R"("access_jwt":"OLD","refresh_jwt":"RJ"})";

    PostResult result;
    auto r = connector->PublishPost(account, credentials,
                                    TextPost(SocialNetwork::Bluesky, "retry me"),
                                    result);
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(server.Requests().size(), std::size_t{3});
    REQUIRE(server.Requests()[0].HeadHas("Bearer OLD"));
    REQUIRE_EQ(server.Requests()[1].Path(),
               std::string{"/xrpc/com.atproto.server.refreshSession"});
    REQUIRE(server.Requests()[1].HeadHas("Bearer RJ"));   // refresh token auths
    REQUIRE(server.Requests()[2].HeadHas("Bearer AJ2"));
    // The rewritten credential blob carries the new session for the vault.
    REQUIRE(credentials.find("AJ2") != std::string::npos);
    REQUIRE(credentials.find("RJ2") != std::string::npos);
    REQUIRE_EQ(result.postId,
               std::string{"at://did:plc:abc/app.bsky.feed.post/rk9"});
}

TEST(bluesky_publish_uploads_blobs) {
    TempMedia media("bluesky-test-media.png");
    FakeHttpServer server({
        {200, R"({"blob":{"$type":"blob","ref":{"$link":"bafyx"},)"
              R"("mimeType":"image/png","size":7}})"},
        {200, R"({"uri":"at://did:plc:abc/app.bsky.feed.post/rk1","cid":"c"})"}});

    auto connector = CreateConnector(SocialNetwork::Bluesky);
    Account account;
    account.accountId = "bluesky-test";
    std::string credentials =
        R"({"server":")" + server.BaseUrl() +
        R"(","did":"did:plc:abc","handle":"e.bsky.social",)"
        R"("access_jwt":"AJ","refresh_jwt":"RJ"})";

    AdaptedPost post = TextPost(SocialNetwork::Bluesky, "pic");
    post.media = {{media.path, "", "alt here"}};

    PostResult result;
    auto r = connector->PublishPost(account, credentials, post, result);
    server.Join();

    REQUIRE(bool(r));
    const auto& upload = server.Requests()[0];
    REQUIRE_EQ(upload.Path(), std::string{"/xrpc/com.atproto.repo.uploadBlob"});
    REQUIRE(upload.HeadHas("Content-Type: image/png") ||
            upload.HeadHas("content-type: image/png"));
    REQUIRE_EQ(upload.body, std::string{"PNGDATA"});
    const auto& create = server.Requests()[1];
    REQUIRE(create.BodyHas(R"("$type":"app.bsky.embed.images")"));
    REQUIRE(create.BodyHas(R"("$link":"bafyx")"));   // blob ref passed through
    REQUIRE(create.BodyHas(R"("alt":"alt here")"));
}

// ============================================================================
// Telegram
// ============================================================================
TEST(telegram_authenticate_checks_bot_and_chat) {
    FakeHttpServer server({
        {200, R"({"ok":true,"result":{"id":1,"username":"my_bot"}})"},
        {200, R"({"ok":true,"result":{"id":-100,"title":"My Channel",)"
              R"("username":"mychannel"}})"}});

    auto connector = CreateConnector(SocialNetwork::Telegram);
    AuthInput input;
    input.server     = server.BaseUrl();
    input.identifier = "@mychannel";
    input.secret     = "123:BOTTOKEN";

    Account account;
    std::string credentials;
    auto r = connector->Authenticate(input, account, credentials);
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(server.Requests()[0].Path(),
               std::string{"/bot123:BOTTOKEN/getMe"});
    REQUIRE_EQ(server.Requests()[1].Path(),
               std::string{"/bot123:BOTTOKEN/getChat"});
    REQUIRE(server.Requests()[1].BodyHas(R"("chat_id":"@mychannel")"));
    REQUIRE_EQ(account.displayName, std::string{"My Channel"});
    REQUIRE_EQ(account.handle, std::string{"@mychannel"});
}

TEST(telegram_auth_fails_on_bad_token) {
    FakeHttpServer server({
        {401, R"({"ok":false,"error_code":401,"description":"Unauthorized"})"}});

    auto connector = CreateConnector(SocialNetwork::Telegram);
    AuthInput input;
    input.server     = server.BaseUrl();
    input.identifier = "@c";
    input.secret     = "bad";
    Account account;
    std::string credentials;
    auto r = connector->Authenticate(input, account, credentials);
    server.Join();

    REQUIRE(!r);
    REQUIRE(r.message.find("Unauthorized") != std::string::npos);
}

TEST(telegram_publish_text_message) {
    FakeHttpServer server({
        {200, R"({"ok":true,"result":{"message_id":55}})"}});

    auto connector = CreateConnector(SocialNetwork::Telegram);
    Account account;
    account.accountId = "telegram-test";
    std::string credentials =
        R"({"server":")" + server.BaseUrl() +
        R"(","bot_token":"123:T","chat_id":"@mychannel",)"
        R"("chat_username":"mychannel"})";

    PostResult result;
    auto r = connector->PublishPost(account, credentials,
                                    TextPost(SocialNetwork::Telegram, "news!"),
                                    result);
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(server.Requests()[0].Path(), std::string{"/bot123:T/sendMessage"});
    REQUIRE(server.Requests()[0].BodyHas(R"("text":"news!")"));
    REQUIRE_EQ(result.postId, std::string{"55"});
    REQUIRE_EQ(result.url, std::string{"https://t.me/mychannel/55"});
}

TEST(telegram_publish_photo_with_caption) {
    TempMedia media("telegram-test-media.jpg", "JPGDATA");
    FakeHttpServer server({
        {200, R"({"ok":true,"result":{"message_id":56}})"}});

    auto connector = CreateConnector(SocialNetwork::Telegram);
    Account account;
    account.accountId = "telegram-test";
    std::string credentials =
        R"({"server":")" + server.BaseUrl() +
        R"(","bot_token":"123:T","chat_id":"@mychannel",)"
        R"("chat_username":"mychannel"})";

    AdaptedPost post = TextPost(SocialNetwork::Telegram, "caption here");
    post.media = {{media.path, "", ""}};

    PostResult result;
    auto r = connector->PublishPost(account, credentials, post, result);
    server.Join();

    REQUIRE(bool(r));
    const auto& request = server.Requests()[0];
    REQUIRE_EQ(request.Path(), std::string{"/bot123:T/sendPhoto"});
    REQUIRE(request.HeadHas("multipart/form-data"));
    REQUIRE(request.BodyHas("name=\"photo\""));
    REQUIRE(request.BodyHas("JPGDATA"));
    REQUIRE(request.BodyHas("caption here"));
    REQUIRE(request.BodyHas("@mychannel"));
    REQUIRE_EQ(result.url, std::string{"https://t.me/mychannel/56"});
}

TEST(telegram_publish_reports_api_failure) {
    // HTTP 200 with ok:false must still fail (the Bot API does this for
    // some soft errors).
    FakeHttpServer server({
        {200, R"({"ok":false,"description":"chat not found"})"}});

    auto connector = CreateConnector(SocialNetwork::Telegram);
    Account account;
    account.accountId = "telegram-test";
    std::string credentials =
        R"({"server":")" + server.BaseUrl() +
        R"(","bot_token":"123:T","chat_id":"@x"})";
    PostResult result;
    auto r = connector->PublishPost(account, credentials,
                                    TextPost(SocialNetwork::Telegram, "x"),
                                    result);
    server.Join();

    REQUIRE(!r);
    REQUIRE(r.message.find("chat not found") != std::string::npos);
}
