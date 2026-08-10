// Tests/UltraNet/ApiStatus/ProbeSse.cpp
// Probes for UltraNet/UltraNetSse.h — the chunk parser and the two HTTP
// front-ends. The loopback origin serves a spec-shaped event stream on /sse.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ApiStatus.h"
#include "ProbeServer.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetSse.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

using namespace ultranet_apistatus;

namespace {

constexpr const char* kArea = "SSE";

std::vector<uint8_t> Bytes(const char* s) {
    return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(s),
                                reinterpret_cast<const uint8_t*>(s) + std::strlen(s));
}

// Verifies the three events the loopback /sse route emits.
Outcome CheckStreamEvents(const std::vector<UltraNetSseEvent>& events) {
    if (events.size() != 3) {
        return Broken("expected 3 events from /sse, got " +
                      std::to_string(events.size()));
    }
    if (events[0].event != "greeting" || events[0].data != "hello" ||
        events[0].id != "1") {
        return Broken("first event mismatched: event=\"" + events[0].event +
                      "\" data=\"" + events[0].data + "\" id=\"" + events[0].id + "\"");
    }
    if (events[1].data != "line one\nline two") {
        return Broken("multi-line data joined wrongly: \"" + events[1].data + "\"");
    }
    if (events[2].data != "bye" || events[2].retry != 2500) {
        return Broken("retry/data mismatched on the last event: retry=" +
                      std::to_string(events[2].retry));
    }
    return Working("3 events parsed from the live stream: event type, id, "
                   "multi-line data join, retry, and comment lines ignored");
}

} // namespace

ULTRANET_PROBE(kArea, UltraNet_SseStream) {
    // Missing callback is rejected without touching the network.
    const UltraNetResult noCallback = UltraNet_SseStream("http://127.0.0.1/", nullptr);
    PROBE_EXPECT(!noCallback && noCallback.code == UltraNetResultCode::InvalidState);

    LoopbackServer& server = Loopback();
    if (!server.Running()) {
        return Implemented("missing-callback guard verified, but no loopback "
                           "origin for a live stream: " + server.Error());
    }

    std::vector<UltraNetSseEvent> events;
    const UltraNetResult r = UltraNet_SseStream(
        server.Url("/sse"), [&](const UltraNetSseEvent& e) { events.push_back(e); });
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    return CheckStreamEvents(events);
}

ULTRANET_PROBE(kArea, UltraNet_SseStreamAsync) {
    {
        UltraNetHttpRequest req;
        req.url = "http://127.0.0.1/";
        bool completed = false;
        const UltraNetHandle h = UltraNet_SseStreamAsync(
            req, nullptr, [&](const UltraNetResult&) { completed = true; });
        PROBE_EXPECT(h == UltraNetInvalidHandle);
        PROBE_EXPECT(completed);
    }

    LoopbackServer& server = Loopback();
    if (!server.Running()) {
        return Implemented("missing-callback guard verified, but no loopback "
                           "origin for a live async stream: " + server.Error());
    }

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    std::vector<UltraNetSseEvent> events;
    UltraNetResult finalResult;

    UltraNetHttpRequest req;
    req.url = server.Url("/sse");
    const UltraNetHandle h = UltraNet_SseStreamAsync(
        req,
        [&](const UltraNetSseEvent& e) {
            std::lock_guard<std::mutex> lk(m);
            events.push_back(e);
        },
        [&](const UltraNetResult& r) {
            std::lock_guard<std::mutex> lk(m);
            finalResult = r;
            done = true;
            cv.notify_all();
        });
    PROBE_EXPECT(h != UltraNetInvalidHandle);

    std::unique_lock<std::mutex> lk(m);
    const bool fired = cv.wait_for(lk, std::chrono::seconds(10), [&] { return done; });
    PROBE_EXPECT_MSG(fired, "onComplete never fired within 10s");
    PROBE_EXPECT_MSG(static_cast<bool>(finalResult), finalResult.message);

    const Outcome parsed = CheckStreamEvents(events);
    if (parsed.status != Status::Working) return parsed;
    return Working("3 events parsed on the worker thread; onComplete reported "
                   "success with httpStatus " + std::to_string(finalResult.httpStatus));
}

ULTRANET_PROBE_NAMED(kArea, "UltraNetSseParser::Feed", SseParserFeed) {
    UltraNetSseParser p;
    std::vector<UltraNetSseEvent> events;
    const auto sink = [&](const UltraNetSseEvent& e) { events.push_back(e); };

    // One complete event, split mid-field across two feeds.
    p.Feed(Bytes("event: token\nda"), sink);
    PROBE_EXPECT(events.empty());
    p.Feed(Bytes("ta: chunk-0\nid: 7\n\n"), sink);
    PROBE_EXPECT(events.size() == 1);
    PROBE_EXPECT(events[0].event == "token");
    PROBE_EXPECT(events[0].data == "chunk-0");
    PROBE_EXPECT(events[0].id == "7");

    // Comments, unknown fields, CRLF line endings, multiple events per chunk.
    events.clear();
    p.Feed(Bytes(": keep-alive\r\nfoo: ignored\r\ndata: a\r\n\r\ndata: b\r\n\r\n"), sink);
    PROBE_EXPECT(events.size() == 2);
    PROBE_EXPECT(events[0].data == "a");
    PROBE_EXPECT(events[1].data == "b");

    // Consecutive data: lines join with '\n'.
    events.clear();
    p.Feed(Bytes("data: one\ndata: two\n\n"), sink);
    PROBE_EXPECT(events.size() == 1);
    PROBE_EXPECT(events[0].data == "one\ntwo");
    return Working("stateful across chunk boundaries; comments and unknown "
                   "fields ignored; CRLF handled; multi-line data joined");
}

ULTRANET_PROBE_NAMED(kArea, "UltraNetSseParser::Flush", SseParserFlush) {
    UltraNetSseParser p;
    std::vector<UltraNetSseEvent> events;
    const auto sink = [&](const UltraNetSseEvent& e) { events.push_back(e); };

    // No terminating blank line — Flush must still dispatch.
    p.Feed(Bytes("event: last\ndata: trailing\n"), sink);
    PROBE_EXPECT(events.empty());
    p.Flush(sink);
    PROBE_EXPECT(events.size() == 1);
    PROBE_EXPECT(events[0].event == "last");
    PROBE_EXPECT(events[0].data == "trailing");

    // Flushing an empty parser must not invent an event.
    events.clear();
    p.Flush(sink);
    PROBE_EXPECT(events.empty());
    return Working("dispatches an event that never got its terminating blank "
                   "line; no-op when nothing is pending");
}
