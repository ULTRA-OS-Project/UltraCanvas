// include/UltraNet/UltraNetSse.h
// Server-Sent Events (text/event-stream) client. Layers an event-parsing
// state machine on top of UltraNet_HttpRequest / UltraNet_HttpRequestAsync
// so callers receive parsed UltraNetSseEvent objects instead of raw bytes.
//
// Use this for streaming LLM tokens (Anthropic / OpenAI / etc.),
// progress updates from long-running APIs, or any text/event-stream feed.
//
// Conforms to the WHATWG SSE spec (sections "Parsing" and "Field
// definitions"): supports data, event, id, retry, and comment lines;
// concatenates consecutive data: fields with newline separators; treats
// a blank line as event terminator.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"
#include "UltraNetHttp.h"

#include <cstdint>
#include <functional>
#include <string>

struct UltraNetSseEvent {
    std::string id;          // last "id:" line seen for this event
    std::string event;       // event type from "event:" (empty == "message")
    std::string data;        // concatenated "data:" lines (joined by '\n')
    int         retry = 0;   // reconnection time in ms from "retry:" (0 = unset)
};

// Synchronous SSE stream. Blocks the calling thread, invoking `onEvent` for
// each event as it arrives. Returns when the server closes the stream or
// when an error occurs. The Accept and Cache-Control headers are set
// automatically; caller-supplied headers / auth take precedence.
UltraNetResult UltraNet_SseStream(
    const std::string& url,
    std::function<void(const UltraNetSseEvent&)> onEvent,
    const UltraNetHttpOptions& options = UltraNetHttpOptions::Default());

// Async SSE stream. Returns a request handle usable with
// UltraNet_CancelRequest / UltraNet_IsRequestActive. `onEvent` fires on the
// curl_multi worker thread as each event is parsed; `onComplete` fires once
// when the stream ends, carrying the final UltraNetResult.
UltraNetHandle UltraNet_SseStreamAsync(
    const UltraNetHttpRequest& request,
    std::function<void(const UltraNetSseEvent&)> onEvent,
    std::function<void(const UltraNetResult&)>    onComplete);

// ----------------------------------------------------------------------------
// Public-but-internal: an SSE chunk parser. Apps that want to feed bytes
// from a non-HTTP source (a TCP socket, a UDP datagram stream, a test
// fixture) can use this directly. Stateful — feed chunks in arrival order.
// ----------------------------------------------------------------------------
class UltraNetSseParser {
public:
    // Feed raw bytes received from the wire. Fires `onEvent` once per
    // complete event in the chunk. Partial events are buffered and
    // dispatched on the next Feed() that completes them.
    void Feed(const std::vector<uint8_t>& chunk,
              const std::function<void(const UltraNetSseEvent&)>& onEvent);

    // For shutdown: flush any pending event-in-progress without waiting
    // for a terminating blank line.
    void Flush(const std::function<void(const UltraNetSseEvent&)>& onEvent);

private:
    void DispatchLine(const std::string& line,
                      const std::function<void(const UltraNetSseEvent&)>& onEvent);

    std::string      buffer_;
    UltraNetSseEvent current_;
};
