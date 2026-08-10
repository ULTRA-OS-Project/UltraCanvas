// Tests/UltraSocial/test_tier2_connectors.cpp
// The Tier-2 connectors (Reddit, X) against scripted loopback fakes: the
// full browser-simulated OAuth sign-in (Reddit's Basic-auth code flow, X's
// PKCE flow), publishing, server-error surfacing, and token refresh —
// including X's rotating refresh tokens. No external network.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "fake_http_server.h"
#include "test_framework.h"

#include "UltraSocialConnector.h"

#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetUrl.h>

#include <chrono>
#include <string>
#include <thread>

using namespace UltraSocial;
using ultrasocial_test::FakeHttpServer;

namespace {

// Extracts a raw (still URL-encoded) query parameter from a URL.
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

// Simulated browser: GET the redirect URI with a code + the consent URL's
// state, retrying while the loopback listener is still binding.
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

} // namespace

// ============================================================================
// Reddit
// ============================================================================
TEST(reddit_authenticate_full_flow) {
    UltraNet_Initialize();
    FakeHttpServer server({
        {200, R"({"access_token":"RAT","token_type":"bearer",)"
              R"("refresh_token":"RRT","expires_in":3600,)"
              R"("scope":"identity submit"})"},
        {200, R"({"name":"erika_dev","id":"t2_abc"})"}});

    auto connector = CreateConnector(SocialNetwork::Reddit);
    AuthInput input;
    input.server     = server.BaseUrl();
    input.clientId   = "myclientid";
    input.identifier = "r/test";

    std::string consentUrl;
    std::thread browser;
    input.onOpenUrl = [&](const std::string& url) {
        consentUrl = url;
        browser = std::thread([url]() { BrowserRedirect(url, "redditcode"); });
    };

    Account account;
    std::string credentials;
    auto r = connector->Authenticate(input, account, credentials);
    browser.join();
    server.Join();

    REQUIRE(bool(r));
    // Consent URL: no PKCE (Reddit doesn't support it), permanent duration.
    REQUIRE(consentUrl.find("/api/v1/authorize") != std::string::npos);
    REQUIRE_EQ(RawQueryParam(consentUrl, "duration"), std::string{"permanent"});
    REQUIRE_EQ(RawQueryParam(consentUrl, "scope"),
               std::string{"identity%20submit"});
    REQUIRE(RawQueryParam(consentUrl, "code_challenge").empty());

    // Token exchange: HTTP Basic "clientid:" (empty password), form body.
    const auto& tokenRequest = server.Requests()[0];
    REQUIRE_EQ(tokenRequest.Path(), std::string{"/api/v1/access_token"});
    REQUIRE(tokenRequest.HeadHas("Authorization: Basic") ||
            tokenRequest.HeadHas("authorization: Basic"));
    REQUIRE(tokenRequest.BodyHas("grant_type=authorization_code"));
    REQUIRE(tokenRequest.BodyHas("code=redditcode"));

    REQUIRE_EQ(server.Requests()[1].Path(), std::string{"/api/v1/me"});
    REQUIRE(server.Requests()[1].HeadHas("Bearer RAT"));

    REQUIRE_EQ(account.handle, std::string{"u/erika_dev → r/test"});
    REQUIRE(credentials.find("\"subreddit\":\"test\"") != std::string::npos);
    REQUIRE(credentials.find("RRT") != std::string::npos);
}

TEST(reddit_publish_self_post) {
    UltraNet_Initialize();
    FakeHttpServer server({
        {200, R"({"json":{"errors":[],"data":)"
              R"({"url":"https://www.reddit.com/r/test/comments/x1/t/",)"
              R"("name":"t3_x1"}}})"}});

    auto connector = CreateConnector(SocialNetwork::Reddit);
    Account account;
    account.accountId = "reddit-test";
    std::string credentials =
        R"({"auth_base":")" + server.BaseUrl() +
        R"(","api_base":")" + server.BaseUrl() +
        R"(","client_id":"cid","access_token":"RAT",)"
        R"("refresh_token":"RRT","subreddit":"test","username":"erika_dev"})";

    AdaptedPost post;
    post.network = SocialNetwork::Reddit;
    post.text    = "A title line\nAnd the body of the self post.";

    PostResult result;
    auto r = connector->PublishPost(account, credentials, post, result);
    server.Join();

    REQUIRE(bool(r));
    const auto& request = server.Requests()[0];
    REQUIRE_EQ(request.Path(), std::string{"/api/submit"});
    REQUIRE(request.HeadHas("Bearer RAT"));
    REQUIRE(request.BodyHas("sr=test"));
    REQUIRE(request.BodyHas("kind=self"));
    REQUIRE(request.BodyHas("title=A%20title%20line"));
    REQUIRE(request.BodyHas("text=And%20the%20body"));
    REQUIRE_EQ(result.postId, std::string{"t3_x1"});
    REQUIRE_EQ(result.url,
               std::string{"https://www.reddit.com/r/test/comments/x1/t/"});
}

TEST(reddit_publish_surfaces_submit_errors) {
    UltraNet_Initialize();
    // /api/submit reports soft errors inside a 200 body.
    FakeHttpServer server({
        {200, R"({"json":{"errors":[["SUBREDDIT_NOEXIST",)"
              R"("that subreddit doesn't exist","sr"]]}})"}});

    auto connector = CreateConnector(SocialNetwork::Reddit);
    Account account;
    account.accountId = "reddit-test";
    std::string credentials =
        R"({"auth_base":")" + server.BaseUrl() +
        R"(","api_base":")" + server.BaseUrl() +
        R"(","client_id":"cid","access_token":"RAT",)"
        R"("refresh_token":"","subreddit":"gone","username":"erika_dev"})";

    AdaptedPost post;
    post.network = SocialNetwork::Reddit;
    post.text    = "hi";
    PostResult result;
    auto r = connector->PublishPost(account, credentials, post, result);
    server.Join();

    REQUIRE(!r);
    REQUIRE(r.message.find("SUBREDDIT_NOEXIST") != std::string::npos);
}

TEST(reddit_publish_refreshes_on_401) {
    UltraNet_Initialize();
    FakeHttpServer server({
        {401, R"({"message":"Unauthorized","error":401})"},
        {200, R"({"access_token":"RAT2","token_type":"bearer",)"
              R"("expires_in":3600})"},
        {200, R"({"json":{"errors":[],"data":{"url":"u","name":"t3_y"}}})"}});

    auto connector = CreateConnector(SocialNetwork::Reddit);
    Account account;
    account.accountId = "reddit-test";
    std::string credentials =
        R"({"auth_base":")" + server.BaseUrl() +
        R"(","api_base":")" + server.BaseUrl() +
        R"(","client_id":"cid","access_token":"OLD",)"
        R"("refresh_token":"RRT","subreddit":"test","username":"erika_dev"})";

    AdaptedPost post;
    post.network = SocialNetwork::Reddit;
    post.text    = "retry me";
    PostResult result;
    auto r = connector->PublishPost(account, credentials, post, result);
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(server.Requests().size(), std::size_t{3});
    REQUIRE(server.Requests()[0].HeadHas("Bearer OLD"));
    REQUIRE_EQ(server.Requests()[1].Path(), std::string{"/api/v1/access_token"});
    REQUIRE(server.Requests()[1].BodyHas("grant_type=refresh_token"));
    REQUIRE(server.Requests()[1].BodyHas("refresh_token=RRT"));
    REQUIRE(server.Requests()[2].HeadHas("Bearer RAT2"));
    // Reddit keeps the refresh token; the new access token is persisted.
    REQUIRE(credentials.find("RAT2") != std::string::npos);
    REQUIRE(credentials.find("RRT") != std::string::npos);
}

// ============================================================================
// X
// ============================================================================
TEST(x_authenticate_full_pkce_flow) {
    UltraNet_Initialize();
    FakeHttpServer server({
        {200, R"({"access_token":"XAT","token_type":"bearer",)"
              R"("refresh_token":"XRT","expires_in":7200})"},
        {200, R"({"data":{"id":"1","name":"Erika","username":"erika_x"}})"}});

    auto connector = CreateConnector(SocialNetwork::X);
    AuthInput input;
    input.server   = server.BaseUrl();
    input.clientId = "xclientid";

    std::string consentUrl;
    std::thread browser;
    input.onOpenUrl = [&](const std::string& url) {
        consentUrl = url;
        browser = std::thread([url]() { BrowserRedirect(url, "xcode"); });
    };

    Account account;
    std::string credentials;
    auto r = connector->Authenticate(input, account, credentials);
    browser.join();
    server.Join();

    REQUIRE(bool(r));
    REQUIRE(consentUrl.find("/i/oauth2/authorize") != std::string::npos);
    REQUIRE(!RawQueryParam(consentUrl, "code_challenge").empty());   // PKCE on
    REQUIRE_EQ(RawQueryParam(consentUrl, "code_challenge_method"),
               std::string{"S256"});

    const auto& tokenRequest = server.Requests()[0];
    REQUIRE_EQ(tokenRequest.Path(), std::string{"/2/oauth2/token"});
    REQUIRE(tokenRequest.BodyHas("code_verifier="));
    REQUIRE(tokenRequest.BodyHas("client_id=xclientid"));

    REQUIRE_EQ(server.Requests()[1].Path(), std::string{"/2/users/me"});
    REQUIRE_EQ(account.handle, std::string{"@erika_x"});
    REQUIRE_EQ(account.displayName, std::string{"Erika"});
    REQUIRE(credentials.find("XAT") != std::string::npos);
    REQUIRE(credentials.find("XRT") != std::string::npos);
}

TEST(x_publish_tweet) {
    UltraNet_Initialize();
    FakeHttpServer server({
        {201, R"({"data":{"id":"1234567890","text":"hi x"}})"}});

    auto connector = CreateConnector(SocialNetwork::X);
    Account account;
    account.accountId = "x-test";
    std::string credentials =
        R"({"api_base":")" + server.BaseUrl() +
        R"(","client_id":"cid","access_token":"XAT",)"
        R"("refresh_token":"XRT","username":"erika_x"})";

    AdaptedPost post;
    post.network = SocialNetwork::X;
    post.text    = "hi x";
    PostResult result;
    auto r = connector->PublishPost(account, credentials, post, result);
    server.Join();

    REQUIRE(bool(r));
    const auto& request = server.Requests()[0];
    REQUIRE_EQ(request.Path(), std::string{"/2/tweets"});
    REQUIRE(request.HeadHas("Bearer XAT"));
    REQUIRE(request.BodyHas(R"("text":"hi x")"));
    REQUIRE_EQ(result.postId, std::string{"1234567890"});
    REQUIRE_EQ(result.url,
               std::string{"https://x.com/erika_x/status/1234567890"});
}

TEST(x_publish_refresh_rotates_tokens) {
    UltraNet_Initialize();
    FakeHttpServer server({
        {401, R"({"title":"Unauthorized","detail":"token expired",)"
              R"("status":401})"},
        {200, R"({"access_token":"XAT2","token_type":"bearer",)"
              R"("refresh_token":"XRT2","expires_in":7200})"},
        {201, R"({"data":{"id":"42","text":"retry"}})"}});

    auto connector = CreateConnector(SocialNetwork::X);
    Account account;
    account.accountId = "x-test";
    std::string credentials =
        R"({"api_base":")" + server.BaseUrl() +
        R"(","client_id":"cid","access_token":"OLD",)"
        R"("refresh_token":"XRT","username":"erika_x"})";

    AdaptedPost post;
    post.network = SocialNetwork::X;
    post.text    = "retry";
    PostResult result;
    auto r = connector->PublishPost(account, credentials, post, result);
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(server.Requests().size(), std::size_t{3});
    REQUIRE_EQ(server.Requests()[1].Path(), std::string{"/2/oauth2/token"});
    REQUIRE(server.Requests()[1].BodyHas("grant_type=refresh_token"));
    REQUIRE(server.Requests()[1].BodyHas("refresh_token=XRT"));
    REQUIRE(server.Requests()[2].HeadHas("Bearer XAT2"));
    // X rotates refresh tokens — both new tokens must be in the blob.
    REQUIRE(credentials.find("XAT2") != std::string::npos);
    REQUIRE(credentials.find("XRT2") != std::string::npos);
    REQUIRE(credentials.find("\"refresh_token\":\"XRT\"") == std::string::npos);
}
