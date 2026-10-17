// Tests/UltraNet/test_oauth2_service.cpp
// The client of the OAuth2 sign-in service: the answer parsers, the four
// calls against a fake service on 127.0.0.1 (what they post, how a refusal
// and an outage come back), and the one-call flow end to end. No external
// network.
#include "test_framework.h"
#include "FakeHttpServer.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetOAuth2.h>
#include <UltraNet/UltraNetOAuth2Service.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace {

UltraNetOAuth2ServiceConfig ConfigFor(const FakeHttpServer& service) {
    UltraNetOAuth2ServiceConfig cfg;
    cfg.serviceUrl       = service.Url() + "/";   // a trailing slash is tolerated
    cfg.providerId       = "google";
    cfg.loginHint        = "erika@gmail.com";
    cfg.clientName       = "UltraNetTests";
    cfg.requestTimeoutMs = 5000;
    cfg.pollWaitSeconds  = 1;
    return cfg;
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

// ============================================================================
// URL joining and the parsers
// ============================================================================
TEST(oauth2_service_url_joins_with_one_slash) {
    REQUIRE_EQ(UltraNet_OAuth2ServiceUrl("https://cb.example", "v1/token"),
               std::string("https://cb.example/v1/token"));
    REQUIRE_EQ(UltraNet_OAuth2ServiceUrl("https://cb.example/", "/v1/token"),
               std::string("https://cb.example/v1/token"));
    REQUIRE_EQ(UltraNet_OAuth2ServiceUrl("https://cb.example//", "v1/sessions/abc"),
               std::string("https://cb.example/v1/sessions/abc"));
}

TEST(oauth2_service_parse_session) {
    UltraNetOAuth2ServiceSession s;
    REQUIRE(UltraNet_OAuth2ServiceParseSession(
        R"({"session_id":"sid-1","authorization_url":"https://accounts.google.com/o/oauth2/v2/auth?x=1","expires_in":600})",
        s));
    REQUIRE(s.IsValid());
    REQUIRE_EQ(s.sessionId, std::string("sid-1"));
    REQUIRE_EQ(s.authorizationUrl, std::string("https://accounts.google.com/o/oauth2/v2/auth?x=1"));
    REQUIRE_EQ(s.expiresInSeconds, 600);

    UltraNetResult r = UltraNet_OAuth2ServiceParseSession(
        R"({"error":"unknown_provider","error_description":"no aol here"})", s);
    REQUIRE(!r);
    REQUIRE(!s.IsValid());
    REQUIRE(Contains(r.message, "unknown_provider"));
    REQUIRE(Contains(r.message, "no aol here"));

    REQUIRE(!UltraNet_OAuth2ServiceParseSession(R"({"session_id":"only"})", s));
    REQUIRE(!UltraNet_OAuth2ServiceParseSession("not json", s));
}

TEST(oauth2_service_parse_status) {
    UltraNetOAuth2CallbackResult cb;
    bool pending = false;
    REQUIRE(UltraNet_OAuth2ServiceParseStatus(R"({"status":"pending","expires_in":412})", cb, pending));
    REQUIRE(pending);
    REQUIRE(!cb.HasCode());

    REQUIRE(UltraNet_OAuth2ServiceParseStatus(R"({"status":"ready","code":"4/abc"})", cb, pending));
    REQUIRE(!pending);
    REQUIRE(cb.HasCode());
    REQUIRE_EQ(cb.code, std::string("4/abc"));

    REQUIRE(UltraNet_OAuth2ServiceParseStatus(
        R"({"status":"error","error":"access_denied","error_description":"the user said no"})",
        cb, pending));
    REQUIRE(!pending);
    REQUIRE(!cb.HasCode());
    REQUIRE_EQ(cb.error, std::string("access_denied"));
    REQUIRE_EQ(cb.errorDescription, std::string("the user said no"));

    // The service's own refusal: no status, an error.
    UltraNetResult r = UltraNet_OAuth2ServiceParseStatus(
        R"({"error":"unknown_session","error_description":"no such sign-in"})", cb, pending);
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::NotFound);
    REQUIRE(!UltraNet_OAuth2ServiceParseStatus(R"({"status":"ready"})", cb, pending));   // no code
    REQUIRE(!UltraNet_OAuth2ServiceParseStatus("[]", cb, pending));
}

// ============================================================================
// The calls, against a fake service
// ============================================================================
TEST(oauth2_service_begin_posts_the_challenge_and_returns_the_consent_url) {
    FakeHttpServer service(FakeHttpServer::Json({
        R"({"session_id":"sid-1","authorization_url":"https://accounts.google.com/o/oauth2/v2/auth?state=s","expires_in":600})"}));
    REQUIRE(service.port > 0);
    UltraNetOAuth2ServiceConfig cfg = ConfigFor(service);
    cfg.scopes = {"https://mail.google.com/", "https://www.googleapis.com/auth/drive"};
    UltraNetOAuth2Pkce pkce = UltraNet_OAuth2GeneratePkce();

    UltraNetOAuth2ServiceSession session;
    UltraNetResult r = UltraNet_OAuth2ServiceBegin(cfg, pkce, session);
    service.Join();
    REQUIRE(r);
    REQUIRE_EQ(session.sessionId, std::string("sid-1"));
    REQUIRE(Contains(session.authorizationUrl, "accounts.google.com"));
    REQUIRE_EQ(session.expiresInSeconds, 600);

    REQUIRE_EQ(service.capturedRequestLines.size(), std::size_t(1));
    REQUIRE(Contains(service.capturedRequestLines[0], "POST /v1/sessions "));
    const std::string& body = service.capturedBodies[0];
    REQUIRE(Contains(body, "provider=google"));
    REQUIRE(Contains(body, "code_challenge=" + pkce.codeChallenge));
    REQUIRE(Contains(body, "code_challenge_method=S256"));
    REQUIRE(Contains(body, "login_hint=erika%40gmail.com"));
    REQUIRE(Contains(body, "scope=https%3A%2F%2Fmail.google.com%2F%20https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fdrive"));
    REQUIRE(Contains(body, "client=UltraNetTests"));
    // The verifier never leaves the application.
    REQUIRE(!Contains(body, pkce.codeVerifier));
}

TEST(oauth2_service_begin_reports_an_unknown_provider_as_unsupported) {
    FakeHttpServer service({{404, R"({"error":"unknown_provider","error_description":"no aol"})"}});
    UltraNetOAuth2ServiceConfig cfg = ConfigFor(service);
    cfg.providerId = "aol";
    UltraNetOAuth2ServiceSession session;
    UltraNetResult r = UltraNet_OAuth2ServiceBegin(cfg, UltraNet_OAuth2GeneratePkce(), session);
    service.Join();
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::Unsupported);
    REQUIRE_EQ(r.httpStatus, 404);
    REQUIRE(Contains(r.message, "unknown_provider"));
}

TEST(oauth2_service_begin_needs_a_service_a_provider_and_a_challenge) {
    UltraNetOAuth2ServiceConfig cfg;
    UltraNetOAuth2ServiceSession session;
    REQUIRE(!UltraNet_OAuth2ServiceBegin(cfg, UltraNet_OAuth2GeneratePkce(), session));
    cfg.serviceUrl = "http://127.0.0.1:1";
    REQUIRE(!UltraNet_OAuth2ServiceBegin(cfg, UltraNet_OAuth2GeneratePkce(), session));
    cfg.providerId = "google";
    UltraNetResult r = UltraNet_OAuth2ServiceBegin(cfg, UltraNetOAuth2Pkce{}, session);
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::InvalidState);
}

TEST(oauth2_service_unreachable_is_a_transport_error) {
    UltraNetOAuth2ServiceConfig cfg;
    cfg.serviceUrl = "http://127.0.0.1:1";   // nothing listens on port 1
    cfg.providerId = "google";
    cfg.requestTimeoutMs = 3000;
    UltraNetOAuth2ServiceSession session;
    UltraNetResult r = UltraNet_OAuth2ServiceBegin(cfg, UltraNet_OAuth2GeneratePkce(), session);
    REQUIRE(!r);
    REQUIRE_EQ(r.httpStatus, 0);
    REQUIRE(Contains(r.message, "did not answer"));
}

TEST(oauth2_service_wait_polls_until_the_code_is_there) {
    FakeHttpServer service(FakeHttpServer::Json({
        R"({"status":"pending","expires_in":500})",
        R"({"status":"pending","expires_in":499})",
        R"({"status":"ready","code":"4/the-code"})"}));
    UltraNetOAuth2ServiceConfig cfg = ConfigFor(service);
    UltraNetOAuth2CallbackResult cb;
    UltraNetResult r = UltraNet_OAuth2ServiceWaitForCode(cfg, "sid-1", cb, 10000);
    service.Join();
    REQUIRE(r);
    REQUIRE(cb.HasCode());
    REQUIRE_EQ(cb.code, std::string("4/the-code"));
    REQUIRE_EQ(service.capturedRequestLines.size(), std::size_t(3));
    REQUIRE(Contains(service.capturedRequestLines[0], "GET /v1/sessions/sid-1?wait=1 "));
}

TEST(oauth2_service_wait_reports_a_refusal_and_a_lost_session) {
    FakeHttpServer refused(FakeHttpServer::Json({
        R"({"status":"error","error":"access_denied","error_description":"no"})"}));
    UltraNetOAuth2CallbackResult cb;
    UltraNetResult r = UltraNet_OAuth2ServiceWaitForCode(ConfigFor(refused), "sid-1", cb, 5000);
    refused.Join();
    REQUIRE(r);                       // the wait itself succeeded
    REQUIRE(!cb.HasCode());
    REQUIRE_EQ(cb.error, std::string("access_denied"));

    FakeHttpServer lost({{404, R"({"error":"unknown_session","error_description":"expired"})"}});
    r = UltraNet_OAuth2ServiceWaitForCode(ConfigFor(lost), "sid-gone", cb, 5000);
    lost.Join();
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::NotFound);
    REQUIRE(Contains(r.message, "unknown_session"));
}

TEST(oauth2_service_wait_can_be_cancelled_and_times_out) {
    // Pending forever: the cancel flag ends it between polls.
    FakeHttpServer service(FakeHttpServer::Json({
        R"({"status":"pending"})", R"({"status":"pending"})", R"({"status":"pending"})",
        R"({"status":"pending"})", R"({"status":"pending"})", R"({"status":"pending"})"}));
    UltraNetOAuth2ServiceConfig cfg = ConfigFor(service);
    cfg.pollWaitSeconds = 0;
    std::atomic<bool> cancelled{false};
    std::atomic<int> polls{0};
    UltraNetOAuth2CallbackResult cb;
    UltraNetResult r = UltraNet_OAuth2ServiceWaitForCode(cfg, "sid-1", cb, 60000,
        [&] { return ++polls >= 3 || cancelled.load(); });
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::Cancelled);

    // A deadline in the past is reported as such, before any request.
    FakeHttpServer idle(FakeHttpServer::Json({R"({"status":"pending"})"}));
    r = UltraNet_OAuth2ServiceWaitForCode(ConfigFor(idle), "sid-1", cb, 0);
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::Timeout);
    service.Join();   // the unserved answers end with the listener
}

TEST(oauth2_service_exchange_posts_code_and_verifier_and_parses_the_provider_answer) {
    FakeHttpServer service(FakeHttpServer::Json({
        R"({"access_token":"AT","refresh_token":"RT","expires_in":3599,"token_type":"Bearer"})"}));
    UltraNetOAuth2ServiceConfig cfg = ConfigFor(service);
    UltraNetOAuth2Token token;
    UltraNetResult r = UltraNet_OAuth2ServiceExchangeCode(cfg, "4/abc", "the-verifier", token);
    service.Join();
    REQUIRE(r);
    REQUIRE_EQ(token.accessToken, std::string("AT"));
    REQUIRE_EQ(token.refreshToken, std::string("RT"));
    REQUIRE_EQ(token.expiresInSeconds, int64_t(3599));
    REQUIRE(Contains(service.capturedRequestLines[0], "POST /v1/token "));
    const std::string& body = service.capturedBodies[0];
    REQUIRE(Contains(body, "provider=google"));
    REQUIRE(Contains(body, "grant_type=authorization_code"));
    REQUIRE(Contains(body, "code=4%2Fabc"));
    REQUIRE(Contains(body, "code_verifier=the-verifier"));
    // No secret, no client id: the service adds those.
    REQUIRE(!Contains(body, "client_secret"));
    REQUIRE(!Contains(body, "client_id"));

    REQUIRE(!UltraNet_OAuth2ServiceExchangeCode(cfg, "", "v", token));
    REQUIRE(!UltraNet_OAuth2ServiceExchangeCode(cfg, "c", "", token));
}

TEST(oauth2_service_refresh_keeps_an_unrotated_token_and_passes_provider_errors_through) {
    FakeHttpServer service({
        {200, R"({"access_token":"AT2","expires_in":3600,"token_type":"Bearer"})"},
        {400, R"({"error":"invalid_grant","error_description":"Token has been expired or revoked."})"}});
    UltraNetOAuth2ServiceConfig cfg = ConfigFor(service);
    UltraNetOAuth2Token token;
    UltraNetResult r = UltraNet_OAuth2ServiceRefresh(cfg, "RT-old", token);
    REQUIRE(r);
    REQUIRE_EQ(token.accessToken, std::string("AT2"));
    REQUIRE_EQ(token.refreshToken, std::string("RT-old"));

    r = UltraNet_OAuth2ServiceRefresh(cfg, "RT-dead", token);
    service.Join();
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::AuthenticationFailed);
    REQUIRE_EQ(r.httpStatus, 400);
    REQUIRE(Contains(r.message, "invalid_grant"));
    REQUIRE(Contains(r.message, "expired or revoked"));
    REQUIRE(Contains(service.capturedBodies[1], "grant_type=refresh_token"));
    REQUIRE(Contains(service.capturedBodies[1], "refresh_token=RT-dead"));
}

TEST(oauth2_service_providers_lists_what_the_service_serves) {
    FakeHttpServer service(FakeHttpServer::Json({R"({"providers":"google microsoft yahoo"})"}));
    std::vector<std::string> ids;
    REQUIRE(UltraNet_OAuth2ServiceProviders(service.Url(), ids, 5000));
    service.Join();
    REQUIRE_EQ(ids.size(), std::size_t(3));
    REQUIRE_EQ(ids[0], std::string("google"));
    REQUIRE_EQ(ids[2], std::string("yahoo"));
    REQUIRE(Contains(service.capturedRequestLines[0], "GET /v1/providers "));

    FakeHttpServer none(FakeHttpServer::Json({R"({"providers":""})"}));
    REQUIRE(UltraNet_OAuth2ServiceProviders(none.Url(), ids, 5000));
    none.Join();
    REQUIRE(ids.empty());
}

// ============================================================================
// The whole flow in one call
// ============================================================================
TEST(oauth2_authorize_via_service_end_to_end) {
    FakeHttpServer service(FakeHttpServer::Json({
        R"({"session_id":"sid-9","authorization_url":"https://accounts.google.com/o/oauth2/v2/auth?state=s9","expires_in":600})",
        R"({"status":"pending"})",
        R"({"status":"ready","code":"4/e2e"})",
        R"({"access_token":"AT-e2e","refresh_token":"RT-e2e","expires_in":3599})"}));
    UltraNetOAuth2ServiceConfig cfg = ConfigFor(service);
    std::string opened;
    UltraNetOAuth2Token token;
    UltraNetResult r = UltraNet_OAuth2AuthorizeViaService(
        cfg, [&](const std::string& url) { opened = url; }, token, 10000);
    service.Join();
    REQUIRE(r);
    REQUIRE_EQ(opened, std::string("https://accounts.google.com/o/oauth2/v2/auth?state=s9"));
    REQUIRE_EQ(token.accessToken, std::string("AT-e2e"));
    REQUIRE_EQ(token.refreshToken, std::string("RT-e2e"));
    REQUIRE_EQ(service.capturedRequestLines.size(), std::size_t(4));
    // The exchange carries the verifier whose challenge started the session.
    const std::string& begin = service.capturedBodies[0];
    const std::string& exchange = service.capturedBodies[3];
    const std::size_t at = begin.find("code_challenge=") + 15;
    const std::string challenge = begin.substr(at, begin.find('&', at) - at);
    const std::size_t vat = exchange.find("code_verifier=") + 14;
    const std::string verifier = exchange.substr(vat, exchange.find('&', vat) - vat);
    REQUIRE_EQ(UltraNet_OAuth2ChallengeFromVerifier(verifier), challenge);
}

TEST(oauth2_authorize_via_service_reports_a_refusal) {
    FakeHttpServer service(FakeHttpServer::Json({
        R"({"session_id":"sid-9","authorization_url":"https://example/consent","expires_in":600})",
        R"({"status":"error","error":"access_denied","error_description":"The user denied access."})"}));
    UltraNetOAuth2Token token;
    UltraNetResult r = UltraNet_OAuth2AuthorizeViaService(
        ConfigFor(service), [](const std::string&) {}, token, 10000);
    service.Join();
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::AuthenticationFailed);
    REQUIRE(Contains(r.message, "access_denied"));
    REQUIRE(!token.IsValid());

    UltraNetOAuth2ServiceConfig cfg;
    cfg.serviceUrl = "http://127.0.0.1:1";
    cfg.providerId = "google";
    REQUIRE(!UltraNet_OAuth2AuthorizeViaService(cfg, nullptr, token));
}
