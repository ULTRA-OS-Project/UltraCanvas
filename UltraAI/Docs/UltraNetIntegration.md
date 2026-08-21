# UltraAI ↔ UltraNet Integration

**Status:** UltraNet side implemented and probe-verified; shared adapter
infrastructure (`adapters/_shared/`) in place; provider adapters not yet
written.
**Author:** UltraAI Module
**Last Modified:** 2026-08-21

UltraAI capability adapters (Anthropic, OpenAI, ElevenLabs, …) make
network requests through **UltraNet** — they do not bundle their own
HTTP client. This document specifies how that integration works so the
first real adapter doesn't have to invent the conventions.

Every API named here exists and is exercised by the `UltraNetApiStatus`
probe suite (`Tests/UltraNet/ApiStatus/`). Build and run that tool before
assuming an entry point works in a given build — see
`Docs/Modules/UltraNet/ApiStatus.md`.

---

## 1. Confirmed conventions

| Decision | Value |
|---|---|
| CMake target name | `UltraNet` |
| Header include style | `<UltraNet/UltraNetHttp.h>`, `<UltraNet/UltraNetSse.h>`, `<UltraNet/UltraNetWebSocket.h>`, … |
| Async callback thread | libcurl multi-worker thread (caller must marshal to UI / app thread) |
| SSE support | Native — `UltraNet_SseStream[Async]` plus a reusable `UltraNetSseParser` |
| Credential storage | `UltraVault` — see [UltraVault.md](UltraVault.md) |

---

## 2. Module dependency direction

```
App
 ├─► UltraAI::ITextLLM (and other capability interfaces)
 │     │
 │     └─► UltraAI adapter (e.g. AnthropicTextLLM)
 │           │
 │           ├─► UltraNet  (HTTP, SSE, WS, sockets, TLS, DNS)
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
| Long-running upload (audio / image / video) | `UltraNet_HttpUploadFile` (streamed; `UltraNetHttpRequest::onUploadProgress`) |
| Long-running download (generated video / music) | `UltraNet_HttpDownloadFile` (streamed; `UltraNetHttpRequest::onDownloadProgress`) |
| LLM token streaming (SSE over HTTPS) | `UltraNet_SseStreamAsync` → parsed `UltraNetSseEvent`s |
| Raw chunked streaming (non-SSE) | `UltraNet_HttpRequestAsync` + `UltraNetHttpRequest::onDataChunk` |
| Live STT (push audio chunks, get partials) | `UltraNet_WebSocketConnect` + `UltraNet_WebSocketSendBinary` |
| Reusing connections for chat-style traffic | `UltraNet_CreateSession` + `UltraNet_SessionHttpPost` |
| Cancelling an in-flight generation | `UltraNet_CancelRequest(handle)` invoked from `IStreamHandle::Cancel()` |
| Per-job progress events (image / video / music) | `onUploadProgress` / `onDownloadProgress` → `ImageJobEvent::progress`, etc. |
| Local-vs-cloud routing | UltraNet honors system proxy + `noProxyHosts`; adapters set `baseUrl` |
| TLS pinning / self-hosted endpoints | `UltraNet_TlsSetCABundle`, `UltraNet_TlsAddTrustedCert`, per-request `options.verifyTls` |

Two callback-placement details that are easy to get wrong:

* `onDataChunk`, `onDownloadProgress` and `onUploadProgress` live on
  **`UltraNetHttpRequest` itself**, not on `UltraNetHttpOptions`.
* When `onDataChunk` is set, chunks stream to the callback in arrival
  order and `response.body` stays **empty** after completion.

---

## 4. SSE (Server-Sent Events) handling

OpenAI, Anthropic, and many vision / TTS providers stream over
`text/event-stream`. UltraNet ships a first-class SSE client
(`<UltraNet/UltraNetSse.h>`), so adapters never parse SSE bytes
themselves:

```cpp
#include <UltraNet/UltraNetSse.h>

UltraNetHttpRequest req = BuildAnthropicStreamingRequest(request);
UltraNetHandle h = UltraNet_SseStreamAsync(
    req,
    [](const UltraNetSseEvent& ev) {
        // ev.event  — event type ("" == "message")
        // ev.data   — concatenated data: lines (joined by '\n')
        // parse ev.data as JSON, dispatch an UltraAI StreamEvent
    },
    [](const UltraNetResult& final) {
        // stream ended — emit Done or Error
    });
```

`UltraNet_SseStream` is the blocking variant. Both conform to the WHATWG
SSE spec (data / event / id / retry / comments, multi-line `data:`
concatenation, blank-line event termination) and set the `Accept` /
`Cache-Control` headers automatically.

For feeding bytes from a non-HTTP source (a test fixture, a recorded
cassette, a raw socket), the parser is public on its own:

```cpp
UltraNetSseParser sse;
sse.Feed(chunk, [&](const UltraNetSseEvent& ev) { /* ... */ });
sse.Flush([&](const UltraNetSseEvent& ev) { /* ... */ });   // at shutdown
```

Adapter unit tests should drive their event dispatch through
`UltraNetSseParser::Feed` with recorded provider output — no network.

---

## 5. Threading model

UltraNet runs `UltraNet_HttpRequestAsync` / `UltraNet_SseStreamAsync`
callbacks (`onComplete`, `onDataChunk`, per-event SSE callbacks) on the
**curl_multi worker thread** — one worker per process. UltraAI's
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

WebSocket callbacks fire on a per-connection receiver thread instead;
the same four rules apply.

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
    bool IsCancelled() const { return cancelled_.load(); }
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
    req.headers.Set("x-api-key", ResolveApiKey());           // see §9
    req.headers.Set("anthropic-version", "2023-06-01");
    req.headers.Set("content-type", "application/json");
    req.body   = SerializeAnthropicMessages(request);
    req.options.timeoutMs = config_.timeoutMs;

    UltraNetResponse netResp;
    UltraNetResult result = UltraNet_HttpRequest(req, netResp);

    ChatResponse out;
    if (!result) {                       // transport-level failure
        out.error = MapNetError(result);
        return out;
    }
    if (netResp.statusCode >= 400) {     // provider-level failure
        out.error = MapHttpError(netResp);
        return out;
    }
    return ParseAnthropicResponse(netResp.GetBodyAsString());
}
```

## 7. End-to-end example — streaming over SSE

```cpp
#include <UltraNet/UltraNetSse.h>

StreamHandle AnthropicTextLLM::ChatStream(const ChatRequest& request,
                                          StreamCallback onEvent) {
    auto handle = std::make_shared<AnthropicStreamHandle>();

    UltraNetHttpRequest req = BuildAnthropicStreamingRequest(request);

    handle->netHandle_ = UltraNet_SseStreamAsync(
        req,
        [handle, onEvent](const UltraNetSseEvent& ev) {
            if (handle->IsCancelled()) return;
            DispatchAnthropicStreamEvent(ev, onEvent);   // TextDelta / ToolCallDelta / ...
        },
        [handle, onEvent](const UltraNetResult& final) {
            StreamEvent done;
            done.kind = (final && !handle->IsCancelled())
                ? StreamEventKind::Done
                : StreamEventKind::Error;
            if (!final) done.error = MapNetError(final);
            onEvent(done);
            handle->done_.store(true);
        });

    return handle;
}
```

## 8. End-to-end example — live STT over WebSocket

```cpp
#include <UltraNet/UltraNetWebSocket.h>

LiveTranscriber DeepgramSpeechToText::StartLiveTranscribe(
    const TranscribeRequest& request, TranscriptCallback onEvent) {

    UltraNetWebSocketOptions ws;
    ws.headers.Set("authorization", "Token " + ResolveApiKey());
    UltraNetHandle h = UltraNet_WebSocketConnect(BuildLiveSttUrl(request), ws);

    auto live = std::make_shared<DeepgramLiveTranscriber>(h, onEvent);
    // Inbound frames arrive via the global callback bag
    // (UltraNet_WebSocketSetCallbacks); dispatch on the UltraNetHandle
    // argument to route frames to this transcriber's PushAudio /
    // Finish / Cancel handlers.
    return live;
}
```

---

## 9. Shared adapter infrastructure

`UltraAI/adapters/_shared/` (CMake target `UltraAI_AdapterShared`) carries
the pieces every network adapter needs, so none of them re-invents the
plumbing:

| Header | Provides |
|---|---|
| `UltraAICredentials.h` | `ResolveApiKey(config, outError)` — the resolution order below |
| `UltraAIHttpError.h` | `MapHttpStatus(status, detail)` → `Error`; `ParseRetryAfterMs(header)` |
| `UltraAIRetryPolicy.h` | `RetryPolicy` — `ShouldRetry(error, attempt)` / `NextDelayMs(attempt, retryAfterMs)`; Retry-After wins over backoff |
| `UltraAIStreamHandleBase.h` | `StreamHandleBase` — the §5 cancellation pattern with an injectable cancel hook |
| `UltraAITransport.h` | `ITransport` seam + `ScriptedTransport` test double |
| `UltraAIUltraNetTransport.h` | `UltraNetTransport` — the production `ITransport` (built with `ULTRAAI_USE_ULTRANET=ON`) |

Adapters talk to providers through `ITransport` rather than calling
UltraNet directly: production wiring injects `UltraNetTransport`
(a completed HTTP exchange with status ≥ 400 comes back as a response to
map, not a transport error), and unit tests inject `ScriptedTransport`
with scripted responses and SSE event scripts — CI never contacts a live
provider. In-tree framework builds enable `ULTRAAI_USE_ULTRANET`
automatically when the `UltraNet` target exists (cache-first, so an
explicit `-DULTRAAI_USE_ULTRANET=OFF` still wins).

### Credential resolution

Adapters do **not** read environment variables directly. Resolution
order (implemented by `ResolveApiKey`):

1. `ProviderConfig::apiKey` (literal string) — used verbatim if non-empty.
2. `ProviderConfig::apiKeyVaultRef` — looked up via UltraVault when
   `ULTRAAI_USE_ULTRAVAULT=ON`.
3. Fail with `Error{ ErrorCode::AuthenticationFailed, ... }`.

See [UltraVault.md](UltraVault.md) for the storage architecture. The
UltraVault module itself is not implemented yet; the cryptographic
primitives its portable file backend needs (Argon2id, XChaCha20-Poly1305,
zeroizing buffers) are arriving separately as the **UltraCrypt** module.
Until UltraVault lands, adapters run on step 1 alone.

---

## 10. Adapter checklist

A new network-using adapter should:

- [ ] Live under `UltraAI/adapters/<name>/`.
- [ ] Depend on `UltraAI` (interface) and `UltraNet`; never on
      `UltraAI_AdapterMock` or any other adapter.
- [ ] Be opt-in via a `ULTRAAI_ADAPTER_<NAME>` CMake option.
- [ ] Self-register through `RegisterTextLLMProvider` / etc. when its
      object file is linked.
- [ ] Reach the network through the `ITransport` seam (§9) so its unit
      tests run on `ScriptedTransport`; use the streaming entry points for
      any call that may exceed ~1 second and keep blocking `Request` for
      short auxiliary calls (e.g. `ListVoices`).
- [ ] Implement cancellation via `StreamHandleBase` (§5, §9).
- [ ] Map errors with the shared helpers: transport errors arrive as
      `Error{}` from `ITransport`; HTTP statuses go through
      `MapHttpStatus`, refined with the provider's error body.
- [ ] Have at least a smoke test driven by `ScriptedTransport` (or
      `UltraNetSseParser` for raw SSE fixtures); never call out to the
      live provider in CI.

---

## 11. Open items

- Recorded-cassette *file format* for replaying real provider responses
  offline. `ScriptedTransport` already replays scripted exchanges and SSE
  event sequences in-process; what's missing is an on-disk format plus a
  loader so cassettes can be captured once and committed.
- A loopback integration test for `UltraNetTransport` itself (the probe
  suite covers the UltraNet primitives; the transport conversion layer is
  currently validated by compilation and manual smoke testing).
