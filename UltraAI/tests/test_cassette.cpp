// UltraAI/tests/test_cassette.cpp
// Round-trips the on-disk cassette format: traffic recorded through
// RecordingTransport (backed by a ScriptedTransport standing in for a live
// server) is saved, loaded into a fresh ScriptedTransport, and replayed
// with identical results. Also covers load failures (missing file, not a
// cassette).
//
// Uses plain asserts so the test suite has no third-party dependency.

#include "UltraAICassette.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
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

std::string TempPath(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

void TestRoundTrip() {
    // "Live server": a scripted transport with one of each exchange kind.
    auto server = std::make_shared<ScriptedTransport>();
    TransportResponse ok;
    ok.statusCode = 200;
    ok.headers    = {{"content-type", "application/json"},
                     {"retry-after", "7"}};
    ok.body       = R"({"greeting":"hello"})";
    server->ScriptResponse(ok);

    Error netFail;
    netFail.code    = ErrorCode::Timeout;
    netFail.message = "connect timed out";
    server->ScriptError(netFail);

    TransportSseEvent ev1; ev1.event = "delta"; ev1.data = R"({"n":1})";
    TransportSseEvent ev2; ev2.data = "[DONE]"; ev2.id = "42";
    server->ScriptSse({ev1, ev2}, {}, 200);

    // Record the three exchanges.
    RecordingTransport recorder(server);
    TransportRequest req;
    req.url = "https://example.test/v1/things";

    Error err;
    TransportResponse r1 = recorder.Request(req, &err);
    EXPECT_TRUE(err.IsOk());
    EXPECT_EQ(r1.statusCode, 200);

    recorder.Request(req, &err);
    EXPECT_EQ(err.code, ErrorCode::Timeout);

    int sseEvents = 0;
    bool sseDone = false;
    recorder.SseStream(req,
        [&](const TransportSseEvent&) { ++sseEvents; },
        [&](const Error& e, int status) {
            sseDone = true;
            EXPECT_TRUE(e.IsOk());
            EXPECT_EQ(status, 200);
        });
    EXPECT_EQ(sseEvents, 2);
    EXPECT_TRUE(sseDone);

    const std::string path = TempPath("ultraai_test_cassette.json");
    std::string saveError;
    EXPECT_TRUE(recorder.Save(path, &saveError));

    // Replay from disk into a fresh transport.
    ScriptedTransport replay;
    std::string loadError;
    EXPECT_TRUE(LoadCassette(path, replay, &loadError));

    TransportResponse p1 = replay.Request(req, &err);
    EXPECT_TRUE(err.IsOk());
    EXPECT_EQ(p1.statusCode, 200);
    EXPECT_EQ(p1.body, ok.body);
    EXPECT_EQ(p1.GetHeader("Retry-After"), std::string("7"));

    replay.Request(req, &err);
    EXPECT_EQ(err.code, ErrorCode::Timeout);
    EXPECT_EQ(err.message, std::string("connect timed out"));

    std::vector<TransportSseEvent> replayed;
    bool replayDone = false;
    replay.SseStream(req,
        [&](const TransportSseEvent& e) { replayed.push_back(e); },
        [&](const Error& e, int status) {
            replayDone = true;
            EXPECT_TRUE(e.IsOk());
            EXPECT_EQ(status, 200);
        });
    EXPECT_TRUE(replayDone);
    EXPECT_EQ(replayed.size(), static_cast<size_t>(2));
    EXPECT_EQ(replayed[0].event, std::string("delta"));
    EXPECT_EQ(replayed[0].data, std::string(R"({"n":1})"));
    EXPECT_EQ(replayed[1].id, std::string("42"));

    std::remove(path.c_str());
}

void TestSseErrorRoundTrip() {
    auto server = std::make_shared<ScriptedTransport>();
    Error rate;
    rate.code         = ErrorCode::RateLimited;
    rate.message      = "slow down";
    rate.providerCode = "rate_limit_error";
    server->ScriptSse({}, rate, 429);

    RecordingTransport recorder(server);
    TransportRequest req;
    recorder.SseStream(req, [](const TransportSseEvent&) {},
                       [](const Error&, int) {});

    const std::string path = TempPath("ultraai_test_cassette_sse.json");
    EXPECT_TRUE(recorder.Save(path));

    ScriptedTransport replay;
    EXPECT_TRUE(LoadCassette(path, replay));
    bool done = false;
    replay.SseStream(req, [](const TransportSseEvent&) {},
        [&](const Error& e, int status) {
            done = true;
            EXPECT_EQ(e.code, ErrorCode::RateLimited);
            EXPECT_EQ(e.providerCode, std::string("rate_limit_error"));
            EXPECT_EQ(status, 429);
        });
    EXPECT_TRUE(done);
    std::remove(path.c_str());
}

void TestLoadFailures() {
    ScriptedTransport transport;
    std::string error;

    EXPECT_TRUE(!LoadCassette(TempPath("ultraai_no_such_cassette.json"),
                              transport, &error));
    EXPECT_TRUE(!error.empty());

    const std::string bogus = TempPath("ultraai_bogus_cassette.json");
    {
        std::ofstream out(bogus, std::ios::trunc);
        out << R"({"something":"else"})" << std::endl;
    }
    EXPECT_TRUE(!LoadCassette(bogus, transport, &error));
    EXPECT_TRUE(error.find("cassette") != std::string::npos);
    std::remove(bogus.c_str());
}

} // namespace

int main() {
    TestRoundTrip();
    TestSseErrorRoundTrip();
    TestLoadFailures();
    std::cout << "test_cassette: all checks passed" << std::endl;
    return 0;
}
