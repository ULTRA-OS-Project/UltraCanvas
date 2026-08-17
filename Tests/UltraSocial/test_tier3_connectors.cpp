// Tests/UltraSocial/test_tier3_connectors.cpp
// Phase-3 coverage: the LinkedIn and Facebook Pages connectors against
// scripted loopback fakes (LinkedIn's full browser-simulated OAuth with
// secret-in-body, the x-restli-id response header, token refresh; Facebook's
// pasted-Page-token sign-in, feed and photo posts, Meta's error-object
// shape), plus the Tier-2 media upgrades: X image uploads and Telegram
// albums. No external network.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "fake_http_server.h"
#include "test_framework.h"

#include "UltraSocialConnector.h"

#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetUrl.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

using namespace UltraSocial;
using ultrasocial_test::FakeHttpServer;

namespace {

std::string RawQueryParam(const std::string& url, const std::string& key) {
    std::string needle = key + "=";
    std::size_t qpos = url.find('?');
    if (qpos == std::string::npos) return {};
    std::size_t i = qpos;
    while (i != std::string::npos) {
        std::size_t start = i + 1;
        if (url.compare(start, needle.size(), needle) == 0) {
            std::size_t vstart = start + needle.size();
            std::size_t amp = url.find('&', vstart);
            return url.substr(vstart, amp == std::string::npos
                                          ? std::string::npos : amp - vstart);
        }
        i = url.find('&', start);
    }
    return {};
}

void BrowserRedirect(const std::string& consentUrl, const std::string& code) {
    std::string redirectUri =
        UltraNet_UrlDecode(RawQueryParam(consentUrl, "redirect_uri"));
    std::string state = RawQueryParam(consentUrl, "state");
    std::string url = redirectUri + "?code=" + code + "&state=" + state;
    UltraNetHttpOptions options;
    options.timeoutMs = 3000;
    for (int attempt = 0; attempt < 20; ++attempt) {
        UltraNetResponse response;
        auto r = UltraNet_HttpGet(url, response, options);
        if (r || r.httpStatus != 0) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

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
// LinkedIn
// ============================================================================
TEST(linkedin_authenticate_full_flow) {
    UltraNet_Initialize();
    FakeHttpServer server({
        {200, R"({"access_token":"LAT","expires_in":5184000,)"
              R"("refresh_token":"LRT"})"},
        {200, R"({"sub":"AbC123","name":"Erika Example",)"
              R"("email":"e@example.com"})"}});

    auto connector = CreateConnector(SocialNetwork::LinkedIn);
    AuthInput input;
    input.server   = server.BaseUrl();
    input.clientId = "liclientid";
    input.secret   = "lisecret";

    std::string consentUrl;
    std::thread browser;
    input.onOpenUrl = [&](const std::string& url) {
        consentUrl = url;
        browser = std::thread([url]() { BrowserRedirect(url, "licode"); });
    };

    Account account;
    std::string credentials;
    auto r = connector->Authenticate(input, account, credentials);
    browser.join();
    server.Join();

    REQUIRE(bool(r));
    REQUIRE(consentUrl.find("/oauth/v2/authorization") != std::string::npos);
    REQUIRE(RawQueryParam(consentUrl, "code_challenge").empty());   // no PKCE
    REQUIRE_EQ(RawQueryParam(consentUrl, "scope"),
               std::string{"openid%20profile%20w_member_social"});

    // Token exchange: secret in the form body (no Basic header).
    const auto& tokenRequest = server.Requests()[0];
    REQUIRE_EQ(tokenRequest.Path(), std::string{"/oauth/v2/accessToken"});
    REQUIRE(tokenRequest.BodyHas("client_secret=lisecret"));
    REQUIRE(!tokenRequest.HeadHas("Authorization: Basic"));

    REQUIRE_EQ(server.Requests()[1].Path(), std::string{"/v2/userinfo"});
    REQUIRE_EQ(account.displayName, std::string{"Erika Example"});
    REQUIRE(credentials.find("\"sub\":\"AbC123\"") != std::string::npos);
}

TEST(linkedin_publish_reads_restli_id_header) {
    UltraNet_Initialize();
    FakeHttpServer server({
        {201, "", "application/json",
         {{"x-restli-id", "urn:li:share:7100"}}}});

    auto connector = CreateConnector(SocialNetwork::LinkedIn);
    Account account;
    account.accountId = "linkedin-test";
    std::string credentials =
        R"({"auth_base":")" + server.BaseUrl() +
        R"(","api_base":")" + server.BaseUrl() +
        R"(","client_id":"cid","client_secret":"cs",)"
        R"("access_token":"LAT","refresh_token":"LRT","sub":"AbC123"})";

    PostResult result;
    auto r = connector->PublishPost(account, credentials,
                                    TextPost(SocialNetwork::LinkedIn,
                                             "hello network"),
                                    result);
    server.Join();

    REQUIRE(bool(r));
    const auto& request = server.Requests()[0];
    REQUIRE_EQ(request.Path(), std::string{"/rest/posts"});
    REQUIRE(request.HeadHas("Bearer LAT"));
    REQUIRE(request.HeadHas("linkedin-version:") ||
            request.HeadHas("Linkedin-Version:") ||
            request.HeadHas("LinkedIn-Version:"));
    REQUIRE(request.HeadHas("2.0.0"));   // x-restli-protocol-version
    REQUIRE(request.BodyHas(R"("author":"urn:li:person:AbC123")"));
    REQUIRE(request.BodyHas(R"("commentary":"hello network")"));
    REQUIRE(request.BodyHas(R"("lifecycleState":"PUBLISHED")"));
    REQUIRE_EQ(result.postId, std::string{"urn:li:share:7100"});
    REQUIRE_EQ(result.url,
               std::string{"https://www.linkedin.com/feed/update/urn:li:share:7100"});
}

TEST(linkedin_publish_refreshes_on_401) {
    UltraNet_Initialize();
    FakeHttpServer server({
        {401, R"({"serviceErrorCode":65600,"message":"expired",)"
              R"("status":401})"},
        {200, R"({"access_token":"LAT2","expires_in":5184000})"},
        {201, "", "application/json", {{"x-restli-id", "urn:li:share:8"}}}});

    auto connector = CreateConnector(SocialNetwork::LinkedIn);
    Account account;
    account.accountId = "linkedin-test";
    std::string credentials =
        R"({"auth_base":")" + server.BaseUrl() +
        R"(","api_base":")" + server.BaseUrl() +
        R"(","client_id":"cid","client_secret":"cs",)"
        R"("access_token":"OLD","refresh_token":"LRT","sub":"AbC123"})";

    PostResult result;
    auto r = connector->PublishPost(account, credentials,
                                    TextPost(SocialNetwork::LinkedIn, "retry"),
                                    result);
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(server.Requests().size(), std::size_t{3});
    REQUIRE_EQ(server.Requests()[1].Path(), std::string{"/oauth/v2/accessToken"});
    REQUIRE(server.Requests()[1].BodyHas("grant_type=refresh_token"));
    REQUIRE(server.Requests()[1].BodyHas("client_secret=cs"));
    REQUIRE(server.Requests()[2].HeadHas("Bearer LAT2"));
    REQUIRE(credentials.find("LAT2") != std::string::npos);
}

// ============================================================================
// Facebook Pages
// ============================================================================
TEST(facebook_authenticate_verifies_page) {
    UltraNet_Initialize();
    FakeHttpServer server({
        {200, R"({"id":"103245","name":"Cloverleaf News"})"}});

    auto connector = CreateConnector(SocialNetwork::Facebook);
    AuthInput input;
    input.server     = server.BaseUrl();
    input.identifier = "103245";
    input.secret     = "EAABPAGETOKEN";

    Account account;
    std::string credentials;
    auto r = connector->Authenticate(input, account, credentials);
    server.Join();

    REQUIRE(bool(r));
    const auto& request = server.Requests()[0];
    REQUIRE(request.Path().find("/v21.0/103245") != std::string::npos);
    REQUIRE(request.Path().find("access_token=EAABPAGETOKEN")
            != std::string::npos);
    REQUIRE_EQ(account.displayName, std::string{"Cloverleaf News"});
    REQUIRE(credentials.find("EAABPAGETOKEN") != std::string::npos);
}

TEST(facebook_authenticate_rejects_wrong_page) {
    UltraNet_Initialize();
    FakeHttpServer server({
        {200, R"({"id":"999","name":"Somebody Else"})"}});

    auto connector = CreateConnector(SocialNetwork::Facebook);
    AuthInput input;
    input.server     = server.BaseUrl();
    input.identifier = "103245";
    input.secret     = "TOK";
    Account account;
    std::string credentials;
    auto r = connector->Authenticate(input, account, credentials);
    server.Join();
    REQUIRE(!r);
}

TEST(facebook_publish_text_to_feed) {
    UltraNet_Initialize();
    FakeHttpServer server({
        {200, R"({"id":"103245_777"})"}});

    auto connector = CreateConnector(SocialNetwork::Facebook);
    Account account;
    account.accountId = "facebook-test";
    std::string credentials =
        R"({"server":")" + server.BaseUrl() +
        R"(","page_id":"103245","page_token":"TOK","page_name":"News"})";

    PostResult result;
    auto r = connector->PublishPost(account, credentials,
                                    TextPost(SocialNetwork::Facebook,
                                             "page update"),
                                    result);
    server.Join();

    REQUIRE(bool(r));
    const auto& request = server.Requests()[0];
    REQUIRE(request.Path().find("/v21.0/103245/feed") != std::string::npos);
    REQUIRE(request.BodyHas("message=page%20update"));
    REQUIRE(request.BodyHas("access_token=TOK"));
    REQUIRE_EQ(result.postId, std::string{"103245_777"});
    REQUIRE_EQ(result.url, std::string{"https://www.facebook.com/103245_777"});
}

TEST(facebook_publish_photo_with_caption) {
    UltraNet_Initialize();
    TempMedia media("facebook-test-media.jpg", "JPGDATA");
    FakeHttpServer server({
        {200, R"({"id":"4444","post_id":"103245_888"})"}});

    auto connector = CreateConnector(SocialNetwork::Facebook);
    Account account;
    account.accountId = "facebook-test";
    std::string credentials =
        R"({"server":")" + server.BaseUrl() +
        R"(","page_id":"103245","page_token":"TOK","page_name":"News"})";

    AdaptedPost post = TextPost(SocialNetwork::Facebook, "look at this");
    post.media = {{media.path, "", ""}};
    PostResult result;
    auto r = connector->PublishPost(account, credentials, post, result);
    server.Join();

    REQUIRE(bool(r));
    const auto& request = server.Requests()[0];
    REQUIRE(request.Path().find("/v21.0/103245/photos") != std::string::npos);
    REQUIRE(request.HeadHas("multipart/form-data"));
    REQUIRE(request.BodyHas("name=\"source\""));
    REQUIRE(request.BodyHas("JPGDATA"));
    REQUIRE(request.BodyHas("look at this"));
    REQUIRE_EQ(result.postId, std::string{"103245_888"});   // post over photo id
}

TEST(facebook_surfaces_meta_error_shape) {
    UltraNet_Initialize();
    FakeHttpServer server({
        {400, R"({"error":{"message":"Invalid OAuth access token.",)"
              R"("type":"OAuthException","code":190}})"}});

    auto connector = CreateConnector(SocialNetwork::Facebook);
    Account account;
    account.accountId = "facebook-test";
    std::string credentials =
        R"({"server":")" + server.BaseUrl() +
        R"(","page_id":"103245","page_token":"BAD","page_name":"News"})";
    PostResult result;
    auto r = connector->PublishPost(account, credentials,
                                    TextPost(SocialNetwork::Facebook, "x"),
                                    result);
    server.Join();

    REQUIRE(!r);
    REQUIRE(r.message.find("OAuthException") != std::string::npos);
    REQUIRE(r.message.find("Invalid OAuth access token") != std::string::npos);
}

// ============================================================================
// Tier-2 media upgrades
// ============================================================================
TEST(x_publish_uploads_media) {
    UltraNet_Initialize();
    TempMedia one("x-test-media1.png", "IMGONE");
    TempMedia two("x-test-media2.png", "IMGTWO");
    FakeHttpServer server({
        {200, R"({"data":{"id":"7001","media_key":"3_7001"}})"},
        {200, R"({"data":{"id":"7002","media_key":"3_7002"}})"},
        {201, R"({"data":{"id":"555","text":"with pics"}})"}});

    auto connector = CreateConnector(SocialNetwork::X);
    Account account;
    account.accountId = "x-test";
    std::string credentials =
        R"({"api_base":")" + server.BaseUrl() +
        R"(","client_id":"cid","access_token":"XAT",)"
        R"("refresh_token":"XRT","username":"erika_x"})";

    AdaptedPost post = TextPost(SocialNetwork::X, "with pics");
    post.media = {{one.path, "", ""}, {two.path, "", ""}};
    PostResult result;
    auto r = connector->PublishPost(account, credentials, post, result);
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(server.Requests().size(), std::size_t{3});
    const auto& firstUpload = server.Requests()[0];
    REQUIRE_EQ(firstUpload.Path(), std::string{"/2/media/upload"});
    REQUIRE(firstUpload.HeadHas("multipart/form-data"));
    REQUIRE(firstUpload.BodyHas("media_category"));
    REQUIRE(firstUpload.BodyHas("IMGONE"));
    REQUIRE(server.Requests()[1].BodyHas("IMGTWO"));
    const auto& tweet = server.Requests()[2];
    REQUIRE(tweet.BodyHas(R"("media_ids":["7001","7002"])"));
    REQUIRE_EQ(result.postId, std::string{"555"});
}

TEST(telegram_publish_album) {
    UltraNet_Initialize();
    TempMedia one("tg-album1.jpg", "ALBUMONE");
    TempMedia two("tg-album2.jpg", "ALBUMTWO");
    TempMedia three("tg-album3.jpg", "ALBUMTHREE");
    FakeHttpServer server({
        {200, R"({"ok":true,"result":[{"message_id":70},{"message_id":71},)"
              R"({"message_id":72}]})"}});

    auto connector = CreateConnector(SocialNetwork::Telegram);
    Account account;
    account.accountId = "telegram-test";
    std::string credentials =
        R"({"server":")" + server.BaseUrl() +
        R"(","bot_token":"123:T","chat_id":"@mychannel",)"
        R"("chat_username":"mychannel"})";

    AdaptedPost post = TextPost(SocialNetwork::Telegram, "album caption");
    post.media = {{one.path, "", ""}, {two.path, "", ""}, {three.path, "", ""}};
    PostResult result;
    auto r = connector->PublishPost(account, credentials, post, result);
    server.Join();

    REQUIRE(bool(r));
    const auto& request = server.Requests()[0];
    REQUIRE_EQ(request.Path(), std::string{"/bot123:T/sendMediaGroup"});
    REQUIRE(request.BodyHas("attach://photo0"));
    REQUIRE(request.BodyHas("attach://photo2"));
    REQUIRE(request.BodyHas(R"(\"caption\":\"album caption\")") ||
            request.BodyHas(R"("caption":"album caption")"));
    REQUIRE(request.BodyHas("ALBUMONE"));
    REQUIRE(request.BodyHas("ALBUMTHREE"));
    // The album anchors on its first message.
    REQUIRE_EQ(result.postId, std::string{"70"});
    REQUIRE_EQ(result.url, std::string{"https://t.me/mychannel/70"});
}
