// UltraAI/tests/test_adapter_shared.cpp
// Exercises the shared adapter infrastructure in adapters/_shared:
// credential resolution order, HTTP status -> Error mapping, Retry-After
// parsing, retry policy decisions and backoff, the stream-handle
// cancellation contract, and the ScriptedTransport test double.
//
// Uses plain asserts so the test suite has no third-party dependency.

#include "UltraAICredentials.h"
#include "UltraAIHttpError.h"
#include "UltraAIRetryPolicy.h"
#include "UltraAIStreamHandleBase.h"
#include "UltraAITransport.h"

#include <iostream>
#include <string>
#include <vector>

using namespace UltraAI;

namespace {

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { std::cerr << "FAIL: " #cond " at " << __FILE__ << ":" \
                  << __LINE__ << std::endl; std::abort(); } \
} while (0)

#define EXPECT_EQ(a, b) do { \
    if (!((a) == (b))) { std::cerr << "FAIL: " #a " == " #b " at " \
                  << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } \
} while (0)

void TestCredentialResolution() {
    // 1. Literal key wins.
    ProviderConfig cfg;
    cfg.apiKey         = "sk-literal";
    cfg.apiKeyVaultRef = "ai.test.api_key";
    Error err;
    EXPECT_EQ(ResolveApiKey(cfg, &err), std::string("sk-literal"));
    EXPECT_TRUE(err.IsOk());

    // 2. Vault ref without a vault in the build fails with guidance.
    cfg.apiKey = "";
    err = {};
    EXPECT_EQ(ResolveApiKey(cfg, &err), std::string(""));
    EXPECT_EQ(err.code, ErrorCode::AuthenticationFailed);
    EXPECT_TRUE(err.message.find("ai.test.api_key") != std::string::npos);

    // 3. Nothing configured fails the same way.
    ProviderConfig empty;
    err = {};
    EXPECT_EQ(ResolveApiKey(empty, &err), std::string(""));
    EXPECT_EQ(err.code, ErrorCode::AuthenticationFailed);

    // Null outError must be tolerated.
    EXPECT_EQ(ResolveApiKey(empty, nullptr), std::string(""));
}

void TestHttpStatusMapping() {
    EXPECT_EQ(MapHttpStatus(200).code, ErrorCode::None);
    EXPECT_EQ(MapHttpStatus(302).code, ErrorCode::None);
    EXPECT_EQ(MapHttpStatus(400).code, ErrorCode::InvalidRequest);
    EXPECT_EQ(MapHttpStatus(401).code, ErrorCode::AuthenticationFailed);
    EXPECT_EQ(MapHttpStatus(402).code, ErrorCode::QuotaExceeded);
    EXPECT_EQ(MapHttpStatus(403).code, ErrorCode::AuthenticationFailed);
    EXPECT_EQ(MapHttpStatus(404).code, ErrorCode::ModelNotFound);
    EXPECT_EQ(MapHttpStatus(408).code, ErrorCode::Timeout);
    EXPECT_EQ(MapHttpStatus(422).code, ErrorCode::InvalidRequest);
    EXPECT_EQ(MapHttpStatus(429).code, ErrorCode::RateLimited);
    EXPECT_EQ(MapHttpStatus(500).code, ErrorCode::ProviderError);
    EXPECT_EQ(MapHttpStatus(529).code, ErrorCode::ProviderError);
    EXPECT_EQ(MapHttpStatus(418).code, ErrorCode::Unknown);

    Error e = MapHttpStatus(429, "overloaded, slow down");
    EXPECT_EQ(e.providerCode, std::string("429"));
    EXPECT_TRUE(e.message.find("HTTP 429") != std::string::npos);
    EXPECT_TRUE(e.message.find("overloaded") != std::string::npos);
}

void TestRetryAfterParsing() {
    EXPECT_EQ(ParseRetryAfterMs("2"), 2000);
    EXPECT_EQ(ParseRetryAfterMs(" 30 "), 30000);
    EXPECT_EQ(ParseRetryAfterMs("0"), 0);
    EXPECT_EQ(ParseRetryAfterMs(""), -1);
    EXPECT_EQ(ParseRetryAfterMs("-5"), -1);
    EXPECT_EQ(ParseRetryAfterMs("Wed, 21 Oct 2026 07:28:00 GMT"), -1);
    EXPECT_EQ(ParseRetryAfterMs("999999999"), -1);   // > 24h: implausible
}

void TestRetryPolicy() {
    RetryPolicy policy;                 // 4 attempts, 500ms base, x2, 30s cap

    Error rate;    rate.code    = ErrorCode::RateLimited;
    Error net;     net.code     = ErrorCode::NetworkError;
    Error prov;    prov.code    = ErrorCode::ProviderError;
    Error timeout; timeout.code = ErrorCode::Timeout;
    Error auth;    auth.code    = ErrorCode::AuthenticationFailed;
    Error bad;     bad.code     = ErrorCode::InvalidRequest;
    Error cancel;  cancel.code  = ErrorCode::Cancelled;

    EXPECT_TRUE(policy.ShouldRetry(rate, 0));
    EXPECT_TRUE(policy.ShouldRetry(net, 1));
    EXPECT_TRUE(policy.ShouldRetry(prov, 2));
    EXPECT_TRUE(policy.ShouldRetry(timeout, 0));
    EXPECT_TRUE(!policy.ShouldRetry(rate, 3));      // 4th try was the last
    EXPECT_TRUE(!policy.ShouldRetry(auth, 0));
    EXPECT_TRUE(!policy.ShouldRetry(bad, 0));
    EXPECT_TRUE(!policy.ShouldRetry(cancel, 0));

    policy.retryOnTimeout = false;
    EXPECT_TRUE(!policy.ShouldRetry(timeout, 0));

    // Exponential growth, then the cap.
    RetryPolicy backoff;
    EXPECT_EQ(backoff.NextDelayMs(0), 500);
    EXPECT_EQ(backoff.NextDelayMs(1), 1000);
    EXPECT_EQ(backoff.NextDelayMs(2), 2000);
    EXPECT_EQ(backoff.NextDelayMs(10), 30000);      // capped

    // Retry-After takes precedence, still capped.
    EXPECT_EQ(backoff.NextDelayMs(0, 7000), 7000);
    EXPECT_EQ(backoff.NextDelayMs(5, 0), 0);
    EXPECT_EQ(backoff.NextDelayMs(0, 120000), 30000);
}

void TestStreamHandleBase() {
    // Hook installed first, Cancel fires it exactly once.
    {
        auto handle = std::make_shared<StreamHandleBase>();
        int fired = 0;
        handle->SetCancelHook([&] { ++fired; });
        EXPECT_TRUE(!handle->IsCancelled());
        handle->Cancel();
        handle->Cancel();
        EXPECT_EQ(fired, 1);
        EXPECT_TRUE(handle->IsCancelled());
        EXPECT_TRUE(!handle->IsDone());
        handle->MarkDone();
        EXPECT_TRUE(handle->IsDone());
    }
    // Cancel before the hook exists: hook fires on installation.
    {
        auto handle = std::make_shared<StreamHandleBase>();
        handle->Cancel();
        int fired = 0;
        handle->SetCancelHook([&] { ++fired; });
        EXPECT_EQ(fired, 1);
    }
}

void TestScriptedTransportRequests() {
    ScriptedTransport transport;

    TransportResponse ok;
    ok.statusCode = 200;
    ok.headers    = {{"Content-Type", "application/json"},
                     {"Retry-After", "3"}};
    ok.body       = R"({"text":"hi"})";
    transport.ScriptResponse(ok);

    Error netFail;
    netFail.code = ErrorCode::NetworkError;
    transport.ScriptError(netFail);

    TransportRequest req;
    req.url  = "https://api.example.test/v1/messages";
    req.body = R"({"prompt":"hello"})";
    req.headers.push_back({"x-api-key", "sk-test"});

    Error err;
    TransportResponse resp = transport.Request(req, &err);
    EXPECT_TRUE(err.IsOk());
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_EQ(resp.GetHeader("content-type"), std::string("application/json"));
    EXPECT_EQ(ParseRetryAfterMs(resp.GetHeader("RETRY-AFTER")), 3000);

    err = {};
    transport.Request(req, &err);
    EXPECT_EQ(err.code, ErrorCode::NetworkError);

    // Off-script traffic is an error, not a silent success.
    err = {};
    transport.Request(req, &err);
    EXPECT_EQ(err.code, ErrorCode::ProviderError);

    EXPECT_EQ(transport.Requests().size(), static_cast<size_t>(3));
    EXPECT_EQ(transport.Requests()[0].body, req.body);
}

void TestScriptedTransportSse() {
    ScriptedTransport transport;

    std::vector<TransportSseEvent> script(3);
    script[0].event = "message_start";  script[0].data = R"({"role":"assistant"})";
    script[1].event = "text_delta";     script[1].data = R"({"text":"Hel"})";
    script[2].event = "message_stop";   script[2].data = "{}";
    transport.ScriptSse(script);

    TransportRequest req;
    req.url = "https://api.example.test/v1/messages?stream=true";

    std::vector<std::string> seen;
    bool completed = false;
    CancelFn cancel = transport.SseStream(
        req,
        [&](const TransportSseEvent& ev) { seen.push_back(ev.event); },
        [&](const Error& e, int status) {
            EXPECT_TRUE(e.IsOk());
            EXPECT_EQ(status, 200);
            completed = true;
        });

    EXPECT_TRUE(completed);
    EXPECT_EQ(seen.size(), static_cast<size_t>(3));
    EXPECT_EQ(seen[0], std::string("message_start"));
    EXPECT_EQ(seen[2], std::string("message_stop"));

    EXPECT_TRUE(!transport.WasCancelled());
    cancel();
    EXPECT_TRUE(transport.WasCancelled());

    // A scripted terminal failure reaches onComplete.
    Error boom;
    boom.code = ErrorCode::RateLimited;
    transport.ScriptSse({}, boom, 429);
    Error final;
    int finalStatus = 0;
    transport.SseStream(req, nullptr, [&](const Error& e, int status) {
        final = e; finalStatus = status;
    });
    EXPECT_EQ(final.code, ErrorCode::RateLimited);
    EXPECT_EQ(finalStatus, 429);
}

} // namespace

int main() {
    TestCredentialResolution();
    TestHttpStatusMapping();
    TestRetryAfterParsing();
    TestRetryPolicy();
    TestStreamHandleBase();
    TestScriptedTransportRequests();
    TestScriptedTransportSse();
    std::cout << "test_adapter_shared: all checks passed" << std::endl;
    return 0;
}
