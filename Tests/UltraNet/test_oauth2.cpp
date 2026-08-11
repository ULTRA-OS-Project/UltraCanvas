// Tests/UltraNet/test_oauth2.cpp
// OAuth2 helper tests: PKCE (RFC 7636 vector), state generation, consent-URL
// building, token-response parsing, the loopback redirect listener, and the
// full interactive flow against a fake in-process token endpoint. Everything
// runs on 127.0.0.1 — no external network.
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetOAuth2.h>
#include <UltraNet/UltraNetSocket.h>
#include <UltraNet/UltraNetUrl.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

// GET with a short connect-retry loop: the listener under test is bound on
// another thread, so the first attempt can race it and get ECONNREFUSED.
// Retries only while no HTTP exchange happened at all.
UltraNetResult GetWithRetry(const std::string& url,
                            UltraNetResponse& outResponse) {
    UltraNetHttpOptions options;
    options.timeoutMs = 3000;
    UltraNetResult r;
    for (int attempt = 0; attempt < 20; ++attempt) {
        r = UltraNet_HttpGet(url, outResponse, options);
        if (r || r.httpStatus != 0) return r;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return r;
}

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

// One-shot fake token endpoint on 127.0.0.1:<ephemeral>. Serves `responses`
// HTTP exchanges, capturing each request (head + body) and answering with
// the canned JSON. Join() before reading capturedBodies.
struct FakeTokenServer {
    UltraNetHandle listener = UltraNetInvalidHandle;
    int port = 0;
    std::thread thread;
    std::vector<std::string> responses;
    std::vector<std::string> capturedBodies;

    explicit FakeTokenServer(std::vector<std::string> jsonResponses)
        : responses(std::move(jsonResponses)) {
        UltraNetSocketOptions options;
        options.bindAddress  = "127.0.0.1";
        options.reuseAddress = true;
        listener = UltraNet_TcpListen(0, options);
        if (listener == UltraNetInvalidHandle) return;
        UltraNetEndpoint local;
        if (!UltraNet_SocketLocalEndpoint(listener, local)) return;
        port = local.port;
        thread = std::thread([this] { Serve(); });
    }

    void Serve() {
        for (const auto& json : responses) {
            UltraNetEndpoint remote;
            UltraNetHandle conn = UltraNet_TcpAccept(listener, remote, 5000);
            if (conn == UltraNetInvalidHandle) return;

            std::string request;
            std::size_t bodyStart = std::string::npos;
            std::size_t contentLength = 0;
            while (request.size() < 65536) {
                if (bodyStart == std::string::npos) {
                    bodyStart = request.find("\r\n\r\n");
                    if (bodyStart != std::string::npos) {
                        bodyStart += 4;
                        std::size_t cl = request.find("Content-Length:");
                        if (cl == std::string::npos)
                            cl = request.find("content-length:");
                        if (cl != std::string::npos) {
                            contentLength = static_cast<std::size_t>(
                                std::atoll(request.c_str() + cl + 15));
                        }
                    }
                }
                if (bodyStart != std::string::npos &&
                    request.size() >= bodyStart + contentLength) {
                    break;
                }
                std::vector<uint8_t> chunk;
                auto r = UltraNet_TcpReceive(conn, chunk, 4096, 3000);
                if (!r || chunk.empty()) break;
                request.append(chunk.begin(), chunk.end());
            }
            if (bodyStart != std::string::npos) {
                capturedBodies.push_back(request.substr(bodyStart));
            } else {
                capturedBodies.push_back(std::string());
            }

            std::string resp = "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: " + std::to_string(json.size()) + "\r\n"
                "Connection: close\r\n\r\n" + json;
            int sent = 0;
            UltraNet_TcpSend(conn, std::vector<uint8_t>(resp.begin(), resp.end()),
                             sent);
            UltraNet_SocketClose(conn);
        }
    }

    void Join() {
        if (thread.joinable()) thread.join();
        if (listener != UltraNetInvalidHandle) {
            UltraNet_SocketClose(listener);
            listener = UltraNetInvalidHandle;
        }
    }

    ~FakeTokenServer() { Join(); }
};

} // namespace

// ============================================================================
// PKCE + state
// ============================================================================
TEST(oauth2_pkce_rfc7636_vector) {
    // RFC 7636 Appendix B.
    REQUIRE_EQ(
        UltraNet_OAuth2ChallengeFromVerifier(
            "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
        std::string{"E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"});
}

TEST(oauth2_pkce_generation) {
    auto a = UltraNet_OAuth2GeneratePkce();
    auto b = UltraNet_OAuth2GeneratePkce();
    REQUIRE(a.IsValid());
    REQUIRE(a.codeVerifier.size() >= 43);
    REQUIRE(a.codeVerifier.size() <= 128);
    REQUIRE_EQ(a.method, std::string{"S256"});
    // Unreserved alphabet only (base64url is a subset of it).
    for (char c : a.codeVerifier) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                  c == '.' || c == '~';
        REQUIRE(ok);
    }
    REQUIRE_EQ(a.codeChallenge,
               UltraNet_OAuth2ChallengeFromVerifier(a.codeVerifier));
    REQUIRE(a.codeVerifier != b.codeVerifier);
}

TEST(oauth2_state_generation) {
    auto a = UltraNet_OAuth2GenerateState();
    auto b = UltraNet_OAuth2GenerateState();
    REQUIRE(a.size() >= 43);      // 32 bytes -> 43 base64url chars
    REQUIRE(a != b);
    REQUIRE(UltraNet_OAuth2GenerateState(1).size() >= 22);  // clamped to 16 bytes
}

// ============================================================================
// Consent URL
// ============================================================================
TEST(oauth2_build_auth_url) {
    UltraNetOAuth2Config config;
    config.authorizationEndpoint = "https://provider.example/oauth/authorize";
    config.clientId    = "my client";     // space forces encoding
    config.redirectUri = "http://127.0.0.1:7777/cb";
    config.scopes      = {"read", "write"};
    config.extraAuthParams["access_type"] = "offline";

    UltraNetOAuth2Pkce pkce;
    pkce.codeVerifier  = "v";
    pkce.codeChallenge = "CHALLENGE";

    std::string url = UltraNet_OAuth2BuildAuthUrl(config, pkce, "STATE123");
    REQUIRE(url.rfind("https://provider.example/oauth/authorize?", 0) == 0);
    REQUIRE_EQ(RawQueryParam(url, "response_type"), std::string{"code"});
    REQUIRE_EQ(RawQueryParam(url, "client_id"), std::string{"my%20client"});
    REQUIRE_EQ(RawQueryParam(url, "redirect_uri"),
               std::string{"http%3A%2F%2F127.0.0.1%3A7777%2Fcb"});
    REQUIRE_EQ(RawQueryParam(url, "scope"), std::string{"read%20write"});
    REQUIRE_EQ(RawQueryParam(url, "state"), std::string{"STATE123"});
    REQUIRE_EQ(RawQueryParam(url, "code_challenge"), std::string{"CHALLENGE"});
    REQUIRE_EQ(RawQueryParam(url, "code_challenge_method"), std::string{"S256"});
    REQUIRE_EQ(RawQueryParam(url, "access_type"), std::string{"offline"});
}

TEST(oauth2_build_auth_url_endpoint_with_query) {
    UltraNetOAuth2Config config;
    config.authorizationEndpoint = "https://p.example/auth?tenant=t1";
    config.clientId = "c";
    UltraNetOAuth2Pkce noPkce;
    std::string url = UltraNet_OAuth2BuildAuthUrl(config, noPkce, "");
    REQUIRE(url.rfind("https://p.example/auth?tenant=t1&response_type=code", 0) == 0);
    REQUIRE(url.find("code_challenge") == std::string::npos);
    REQUIRE(url.find("state=") == std::string::npos);
}

// ============================================================================
// Token-response parsing
// ============================================================================
TEST(oauth2_parse_token_success) {
    UltraNetOAuth2Token token;
    auto r = UltraNet_OAuth2ParseTokenResponse(
        R"({"access_token":"AT","token_type":"Bearer","expires_in":3600,)"
        R"("refresh_token":"RT","scope":"read write","id_token":"IDT",)"
        R"("nested":{"ignored":[1,2,{"x":"y"}]},"extra":true})",
        token);
    REQUIRE(bool(r));
    REQUIRE_EQ(token.accessToken, std::string{"AT"});
    REQUIRE_EQ(token.tokenType, std::string{"Bearer"});
    REQUIRE_EQ(token.refreshToken, std::string{"RT"});
    REQUIRE_EQ(token.scope, std::string{"read write"});
    REQUIRE_EQ(token.idToken, std::string{"IDT"});
    REQUIRE_EQ(token.expiresInSeconds, int64_t{3600});
    REQUIRE(token.IsValid());
    REQUIRE(!token.raw.empty());
}

TEST(oauth2_parse_token_expires_in_as_string) {
    UltraNetOAuth2Token token;
    auto r = UltraNet_OAuth2ParseTokenResponse(
        R"({"access_token":"AT","expires_in":"7200"})", token);
    REQUIRE(bool(r));
    REQUIRE_EQ(token.expiresInSeconds, int64_t{7200});
}

TEST(oauth2_parse_token_escapes) {
    UltraNetOAuth2Token token;
    auto r = UltraNet_OAuth2ParseTokenResponse(
        "{\"access_token\":\"A\\u0042C\\n\\\"q\\\"\",\"token_type\":\"bearer\"}",
        token);
    REQUIRE(bool(r));
    REQUIRE_EQ(token.accessToken, std::string{"ABC\n\"q\""});
}

TEST(oauth2_parse_token_error_response) {
    UltraNetOAuth2Token token;
    auto r = UltraNet_OAuth2ParseTokenResponse(
        R"({"error":"invalid_grant","error_description":"code expired"})",
        token);
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::AuthenticationFailed);
    REQUIRE(r.message.find("invalid_grant") != std::string::npos);
    REQUIRE(r.message.find("code expired") != std::string::npos);
}

TEST(oauth2_parse_token_malformed) {
    UltraNetOAuth2Token token;
    REQUIRE(!UltraNet_OAuth2ParseTokenResponse("not json at all", token));
    REQUIRE(!UltraNet_OAuth2ParseTokenResponse("{\"access_token\":", token));
    REQUIRE(!UltraNet_OAuth2ParseTokenResponse("{}", token));
    REQUIRE(!UltraNet_OAuth2ParseTokenResponse(R"({"token_type":"Bearer"})",
                                               token));
}

// ============================================================================
// Loopback listener additions (bindAddress / port 0 / local endpoint)
// ============================================================================
TEST(oauth2_socket_local_endpoint_ephemeral_port) {
    UltraNetSocketOptions options;
    options.bindAddress  = "127.0.0.1";
    options.reuseAddress = true;
    UltraNetHandle listener = UltraNet_TcpListen(0, options);
    REQUIRE(listener != UltraNetInvalidHandle);
    UltraNetEndpoint local;
    REQUIRE(bool(UltraNet_SocketLocalEndpoint(listener, local)));
    REQUIRE(local.port > 0);
    REQUIRE_EQ(local.address, std::string{"127.0.0.1"});
    UltraNet_SocketClose(listener);
}

TEST(oauth2_wait_for_callback_rejects_non_loopback) {
    UltraNetOAuth2CallbackResult cb;
    auto r = UltraNet_OAuth2WaitForCallback("http://example.com:9/cb", cb, 100);
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::InvalidUrl);
    auto r2 = UltraNet_OAuth2WaitForCallback("https://127.0.0.1:9/cb", cb, 100);
    REQUIRE(!r2);
}

TEST(oauth2_wait_for_callback_times_out) {
    UltraNetOAuth2CallbackResult cb;
    auto r = UltraNet_OAuth2WaitForCallback("http://127.0.0.1:18471/cb", cb, 300);
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::Timeout);
}

TEST(oauth2_wait_for_callback_receives_redirect) {
    UltraNet_Initialize();
    constexpr int port = 18472;

    UltraNetOAuth2CallbackResult cb;
    UltraNetResult waitResult;
    std::thread waiter([&] {
        waitResult = UltraNet_OAuth2WaitForCallback(
            "http://127.0.0.1:" + std::to_string(port) + "/callback", cb, 8000);
    });

    // A stray request to the wrong path must not satisfy the wait…
    UltraNetResponse stray;
    GetWithRetry("http://127.0.0.1:" + std::to_string(port) + "/favicon.ico",
                 stray);
    // …the provider redirect does.
    UltraNetResponse redirect;
    auto get = GetWithRetry(
        "http://127.0.0.1:" + std::to_string(port) +
        "/callback?code=ac%2F123&state=st1", redirect);
    waiter.join();

    REQUIRE(bool(get));
    REQUIRE_EQ(redirect.statusCode, 200);
    REQUIRE(bool(waitResult));
    REQUIRE_EQ(cb.code, std::string{"ac/123"});   // percent-decoded
    REQUIRE_EQ(cb.state, std::string{"st1"});
    REQUIRE(cb.HasCode());
}

TEST(oauth2_wait_for_callback_reports_denial) {
    UltraNet_Initialize();
    constexpr int port = 18473;

    UltraNetOAuth2CallbackResult cb;
    UltraNetResult waitResult;
    std::thread waiter([&] {
        waitResult = UltraNet_OAuth2WaitForCallback(
            "http://127.0.0.1:" + std::to_string(port) + "/cb", cb, 8000);
    });
    UltraNetResponse redirect;
    GetWithRetry("http://127.0.0.1:" + std::to_string(port) +
                 "/cb?error=access_denied&error_description=nope", redirect);
    waiter.join();

    REQUIRE(bool(waitResult));
    REQUIRE(!cb.HasCode());
    REQUIRE_EQ(cb.error, std::string{"access_denied"});
    REQUIRE_EQ(cb.errorDescription, std::string{"nope"});
}

// ============================================================================
// Token exchange against the fake endpoint
// ============================================================================
TEST(oauth2_exchange_code_posts_form) {
    UltraNet_Initialize();
    FakeTokenServer server({
        R"({"access_token":"AT1","token_type":"Bearer","expires_in":100})"});
    REQUIRE(server.port > 0);

    UltraNetOAuth2Config config;
    config.tokenEndpoint = "http://127.0.0.1:" + std::to_string(server.port) + "/token";
    config.clientId      = "cid";
    config.redirectUri   = "http://127.0.0.1:7777/cb";
    config.extraTokenParams["audience"] = "api";

    UltraNetOAuth2Token token;
    auto r = UltraNet_OAuth2ExchangeCode(config, "thecode", "theverifier", token);
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(token.accessToken, std::string{"AT1"});
    REQUIRE_EQ(server.capturedBodies.size(), std::size_t{1});
    const std::string& body = server.capturedBodies[0];
    REQUIRE(body.find("grant_type=authorization_code") != std::string::npos);
    REQUIRE(body.find("code=thecode") != std::string::npos);
    REQUIRE(body.find("code_verifier=theverifier") != std::string::npos);
    REQUIRE(body.find("client_id=cid") != std::string::npos);
    REQUIRE(body.find("audience=api") != std::string::npos);
}

TEST(oauth2_refresh_keeps_unrotated_token) {
    UltraNet_Initialize();
    FakeTokenServer server({R"({"access_token":"AT2","token_type":"Bearer"})"});
    REQUIRE(server.port > 0);

    UltraNetOAuth2Config config;
    config.tokenEndpoint = "http://127.0.0.1:" + std::to_string(server.port) + "/token";
    config.clientId      = "cid";

    UltraNetOAuth2Token token;
    auto r = UltraNet_OAuth2Refresh(config, "RTOLD", token);
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(token.accessToken, std::string{"AT2"});
    // Server didn't rotate the refresh token; the old one must survive.
    REQUIRE_EQ(token.refreshToken, std::string{"RTOLD"});
    REQUIRE(server.capturedBodies[0].find("grant_type=refresh_token")
            != std::string::npos);
    REQUIRE(server.capturedBodies[0].find("refresh_token=RTOLD")
            != std::string::npos);
}

// ============================================================================
// Full interactive flow: consent URL -> browser (simulated) -> loopback
// redirect -> token exchange, with the PKCE challenge/verifier round-trip
// checked end to end.
// ============================================================================
TEST(oauth2_authorize_interactive_end_to_end) {
    UltraNet_Initialize();
    FakeTokenServer server({
        R"({"access_token":"ATX","token_type":"Bearer","refresh_token":"RTX"})"});
    REQUIRE(server.port > 0);

    UltraNetOAuth2Config config;
    config.authorizationEndpoint = "https://provider.example/authorize";
    config.tokenEndpoint = "http://127.0.0.1:" + std::to_string(server.port) + "/token";
    config.clientId    = "cid";
    config.redirectUri = "http://127.0.0.1:0/callback";  // ephemeral port

    std::string consentUrl;
    std::thread browser;
    auto onOpenUrl = [&](const std::string& url) {
        consentUrl = url;
        // Simulate the provider: redirect the "browser" to the redirect_uri
        // with a code and the state from the consent URL.
        std::string redirectUri =
            UltraNet_UrlDecode(RawQueryParam(url, "redirect_uri"));
        std::string state = RawQueryParam(url, "state");
        browser = std::thread([redirectUri, state] {
            UltraNetResponse response;
            UltraNetHttpOptions options;
            options.timeoutMs = 3000;
            UltraNet_HttpGet(redirectUri + "?code=xyz&state=" + state,
                             response, options);
        });
    };

    UltraNetOAuth2Token token;
    auto r = UltraNet_OAuth2AuthorizeInteractive(config, onOpenUrl, token, 8000);
    browser.join();
    server.Join();

    REQUIRE(bool(r));
    REQUIRE_EQ(token.accessToken, std::string{"ATX"});
    REQUIRE_EQ(token.refreshToken, std::string{"RTX"});

    // The ephemeral port must have been resolved into the consent URL.
    std::string sentRedirect =
        UltraNet_UrlDecode(RawQueryParam(consentUrl, "redirect_uri"));
    REQUIRE(sentRedirect.find(":0/") == std::string::npos);

    // PKCE round-trip: the verifier sent to the token endpoint must hash to
    // the challenge advertised in the consent URL.
    const std::string& body = server.capturedBodies[0];
    std::size_t vpos = body.find("code_verifier=");
    REQUIRE(vpos != std::string::npos);
    std::size_t vend = body.find('&', vpos);
    std::string verifier = body.substr(
        vpos + 14, vend == std::string::npos ? std::string::npos
                                             : vend - vpos - 14);
    REQUIRE_EQ(RawQueryParam(consentUrl, "code_challenge"),
               UltraNet_OAuth2ChallengeFromVerifier(verifier));
    REQUIRE(body.find("code=xyz") != std::string::npos);
}

TEST(oauth2_authorize_interactive_state_mismatch) {
    UltraNet_Initialize();

    UltraNetOAuth2Config config;
    config.authorizationEndpoint = "https://provider.example/authorize";
    config.tokenEndpoint = "http://127.0.0.1:1/token";  // must never be hit
    config.clientId    = "cid";
    config.redirectUri = "http://127.0.0.1:0/callback";

    std::thread browser;
    auto onOpenUrl = [&](const std::string& url) {
        std::string redirectUri =
            UltraNet_UrlDecode(RawQueryParam(url, "redirect_uri"));
        browser = std::thread([redirectUri] {
            UltraNetResponse response;
            UltraNetHttpOptions options;
            options.timeoutMs = 3000;
            UltraNet_HttpGet(redirectUri + "?code=xyz&state=WRONG",
                             response, options);
        });
    };

    UltraNetOAuth2Token token;
    auto r = UltraNet_OAuth2AuthorizeInteractive(config, onOpenUrl, token, 8000);
    browser.join();

    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::AuthenticationFailed);
    REQUIRE(r.message.find("state") != std::string::npos);
    REQUIRE(!token.IsValid());
}

TEST(oauth2_authorize_interactive_requires_callback) {
    UltraNetOAuth2Config config;
    config.authorizationEndpoint = "https://p.example/auth";
    config.clientId    = "cid";
    config.redirectUri = "http://127.0.0.1:0/cb";
    UltraNetOAuth2Token token;
    auto r = UltraNet_OAuth2AuthorizeInteractive(config, nullptr, token, 100);
    REQUIRE(!r);
}
