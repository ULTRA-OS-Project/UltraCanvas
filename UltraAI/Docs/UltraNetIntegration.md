# UltraAI ↔ UltraNet Integration

**Status:** Design proposal, not yet implemented.
**Author:** UltraAI Module
**Last Modified:** 2026-05-08

UltraAI capability adapters (Anthropic, OpenAI, ElevenLabs, …) make
network requests through **UltraNet** — they do not bundle their own
HTTP client. This document specifies how that integration works so the
first real adapter doesn't have to invent the conventions.

---

## 1. Confirmed conventions

| Decision | Value |
|---|---|
| CMake target name | `UltraNet` |
| Header include style | `<UltraNet/UltraNetHttp.h>`, `<UltraNet/UltraNetWebSocket.h>`, etc. |
| Async callback thread | libcurl multi-worker thread (caller must marshal to UI / app thread) |
| SSE support | Adapter-side parser (UltraNet has no SSE helper today) |
| Credential storage | `UltraVault` — see [UltraVault.md](UltraVault.md) |

---

## 2. Module dependency direction

```
App
 ├─► UltraAI::ITextLLM (and other capability interfaces)
 │     │
 │     └─► UltraAI adapter (e.g. AnthropicTextLLM)
 │           │
 │           ├─► UltraNet  (HTTP, WS, FTP, sockets, TLS, DNS)
 │           └─► UltraVault (credential resolution, optional)
 │
 └─► UltraCanvas (UI)
```

* **UltraAI never depends on UltraNet** — only individual adapters do.
* **UltraNet never depends on UltraAI** — strictly one-way.
* Adapter targets are gated by `ULTRAAI_USE_ULTRANET=ON` so the module
  builds cleanly without UltraNet present (mock adapters only).

---

## 3. Capability → UltraNet primitive map

| Capability scenario | UltraNet primitive |
|---|---|
| Provider REST call (sync) | `UltraNet_HttpRequest` |
| Provider REST call (background) | `UltraNet_HttpRequestAsync` + `onComplete` |
| Long-running upload (audio / image / video) | `UltraNet_HttpUploadFile` (streamed; surfaces `onUploadProgress`) |
| Long-running download (generated video / music) | `UltraNet_HttpDownloadFile` (streamed; surfaces `onDownloadProgress`) |
| LLM token streaming (SSE over HTTPS) | `UltraNet_HttpRequestAsync` + `onDataReceive` chunked → adapter-side SSE parser |
| Live STT (push audio chunks, get partials) | `UltraNet_WebSocketConnect` + `UltraNet_WebSocketSendBinary` |
| Reusing connections for chat-style traffic | `UltraNet_CreateSession` + `UltraNet_SessionHttpPost` |
| Cancelling an in-flight generation | `UltraNet_CancelRequest(handle)` invoked from `IStreamHandle::Cancel()` |
| Per-job progress events (image / video / music) | `onUploadProgress` / `onDownloadProgress` → `ImageJobEvent::progress`, etc. |
| Local-vs-cloud routing | UltraNet honors system proxy + `noProxyHosts`; adapters set `baseUrl` |
| TLS pinning / self-hosted endpoints | `UltraNet_TlsAddTrustedCert`, `onCertificateVerify` |

---

## 4. SSE (Server-Sent Events) handling

OpenAI, Anthropic, and many vision / TTS providers stream over
`text/event-stream`. UltraNet does not (yet) have a first-class SSE
helper, so adapters parse SSE on top of `UltraNet_HttpRequestAsync`.

### Minimal SSE state machine

```cpp
// UltraAI/adapters/_shared/SseParser.h  (proposed)
class SseParser {
public:
    struct Event {
        std::string event;     // "" if not provided -> implicit "message"
        std::string data;      // multi-line concatenation
        std::string id;
        int retryMs = -1;
    };

    void Feed(const std::vector<uint8_t>& chunk,
              const std::function<void(const Event&)>& onEvent);
    void Reset();

private:
    std::string buffer_;       // line-accumulator
    Event current_;
};
```

Wire-up inside an adapter:

```cpp
SseParser sse;
UltraNetHttpRequest req = BuildAnthropicChatRequest(/* ... */);
req.options.onDataReceive = [&](const std::vector<uint8_t>& chunk) {
    sse.Feed(chunk, [&](const SseParser::Event& ev) {
        // parse ev.data as JSON, dispatch UltraAI StreamEvent on the
        // adapter's outbound stream callback
    });
};
```

A reusable `SseParser` lives in `UltraAI/adapters/_shared/` so every
network adapter shares the implementation. **TODO**: revisit if/when
UltraNet adds `UltraNet_HttpSseSubscribe`.

---

## 5. Threading model

UltraNet documents `UltraNet_HttpRequestAsync`'s `onComplete` as
running on the **libcurl multi worker thread**. UltraAI's
`StreamCallback` documentation already says "implementation-defined
thread", so there is no contract change — but adapter authors must:

1. **Not block** in the callback (the worker thread also drives every
   other in-flight request).
2. **Be reentrant** — multiple concurrent calls hit the same thread.
3. **Marshal to the app thread when needed.** UI apps typically schedule
   onto the UltraCanvas event loop; CLI apps can use `std::async`
   without further thought.
4. **Handle cancellation** — if `IStreamHandle::Cancel()` is called from
   the app thread while the worker is dispatching, the adapter must
   stop emitting events (`atomic<bool> cancelled_`), invoke
   `UltraNet_CancelRequest`, and surface the next event as `Done`
   (cancelled) or `Error::Cancelled`.

**Recommended adapter skeleton for streaming:**

```cpp
class AnthropicStreamHandle : public IStreamHandle {
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> done_{false};
    UltraNetHandle netHandle_ = 0;
public:
    void Cancel() override {
        cancelled_.store(true);
        if (netHandle_) UltraNet_CancelRequest(netHandle_);
    }
    bool IsDone() const override { return done_.load(); }
    // ...
};
```

---

## 6. End-to-end example — Anthropic chat (sync)

```cpp
#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>
#include "UltraAITextLLM.h"

UltraAI::ChatResponse AnthropicTextLLM::Chat(const ChatRequest& request) {
    UltraNetHttpRequest req;
    req.url    = baseUrl_ + "/v1/messages";
    req.method = UltraNetHttpMethod::Post;
    req.headers.Set("x-api-key", ResolveApiKey());           // see §8
    req.headers.Set("anthropic-version", "2023-06-01");
    req.headers.Set("content-type", "application/json");
    req.body   = SerializeAnthropicMessages(request);

    UltraNetResponse netResp;
    auto result = UltraNet_HttpRequest(req, netResp);

    ChatResponse out;
    if (!result.success) {
        out.error = MapNetError(result);
        return out;
    }
    if (netResp.statusCode >= 400) {
        out.error = MapHttpError(netResp);
        return out;
    }
    return ParseAnthropicResponse(netResp.GetBodyAsString());
}
```

## 7. End-to-end example — streaming with adapter-side SSE

```cpp
StreamHandle AnthropicTextLLM::ChatStream(const ChatRequest& request,
                                          StreamCallback onEvent) {
    auto handle = std::make_shared<AnthropicStreamHandle>();
    auto sse    = std::make_shared<SseParser>();

    UltraNetHttpRequest req = BuildAnthropicStreamingRequest(request);
    req.options.onHeadersReceive = [handle](const UltraNetHttpHeaders& h) {
        // capture status / rate-limit headers if needed
    };
    req.options.onDataReceive = [handle, sse, onEvent](
        const std::vector<uint8_t>& chunk) {
        if (handle->IsCancelled()) return;
        sse->Feed(chunk, [&](const SseParser::Event& ev) {
            DispatchAnthropicStreamEvent(ev, onEvent);
        });
    };

    handle->netHandle_ = UltraNet_HttpRequestAsync(
        req,
        [handle, onEvent](const UltraNetResponse& finalResp) {
            StreamEvent done;
            done.kind = handle->IsCancelled()
                ? StreamEventKind::Error
                : StreamEventKind::Done;
            if (finalResp.statusCode >= 400) {
                done.kind  = StreamEventKind::Error;
                done.error = MapHttpError(finalResp);
            }
            onEvent(done);
            handle->done_.store(true);
        });

    return handle;
}
```

## 8. End-to-end example — live STT over WebSocket

```cpp
LiveTranscriber DeepgramSpeechToText::StartLiveTranscribe(
    const TranscribeRequest& request, TranscriptCallback onEvent) {

    UltraNetWebSocketOptions ws;
    ws.headers.Set("authorization", "Token " + ResolveApiKey());
    UltraNetHandle h = UltraNet_WebSocketConnect(
        BuildLiveSttUrl(request), ws);

    auto live = std::make_shared<DeepgramLiveTranscriber>(h, onEvent);
    // Wire UltraNet WebSocket callbacks to the live object's
    // PushAudio / Finish / Cancel handlers.
    return live;
}
```

---

## 9. Credential resolution

Adapters do **not** read environment variables directly. Resolution
order (implemented by a shared helper in `UltraAI/adapters/_shared/`):

1. `ProviderConfig::apiKey` (literal string) — used verbatim if non-empty.
2. `ProviderConfig::apiKeyVaultRef` — looked up via UltraVault when
   `ULTRAAI_USE_ULTRAVAULT=ON`.
3. Fail with `Error{ ErrorCode::AuthenticationFailed, ... }`.

See [UltraVault.md](UltraVault.md) for the storage architecture.

---

## 10. Adapter checklist

A new network-using adapter should:

- [ ] Live under `UltraAI/adapters/<name>/`.
- [ ] Depend on `UltraAI` (interface) and `UltraNet`; never on
      `UltraAI_AdapterMock` or any other adapter.
- [ ] Be opt-in via a `ULTRAAI_ADAPTER_<NAME>` CMake option.
- [ ] Self-register through `RegisterTextLLMProvider` / etc. when its
      object file is linked.
- [ ] Use `UltraNet_HttpRequestAsync` for any call that may exceed
      ~1 second; only use `UltraNet_HttpRequest` (sync) for short
      auxiliary requests (e.g. `ListVoices`).
- [ ] Implement cancellation via the worker-thread-safe pattern in §5.
- [ ] Surface every UltraNet error via `MapNetError` → `Error{}`.
- [ ] Have at least a smoke test (mock UltraNet transport or a recorded
      session) — never call out to the live provider in CI.

---

## 11. Open items

- Mock UltraNet transport for adapter unit tests (so CI never calls real
  providers).
- Recorded-cassette format for replaying real provider responses
  offline.
- Whether UltraNet should grow `UltraNet_HttpSseSubscribe` (would let
  every adapter drop its `SseParser` dependency).
