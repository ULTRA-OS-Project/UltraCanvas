# ComfyUI as an UltraAI Provider — Research and Recommendation

**Date:** 2026-08-24
**Status:** Research — no implementation yet
**Scope:** Whether UltraAI should support ComfyUI as a provider backend, what
an adapter would look like against the existing capability interfaces, and
what infrastructure work it needs first.

---

## Verdict up front

**Yes — support it, as an opt-in, out-of-process HTTP adapter (`comfyui`)
implementing `IImageGen` first and `IVideoGen` second. Never vendor, link,
bundle or auto-install ComfyUI.**

Three reasons carry the decision:

1. **UltraAI has no real image provider at all.** `IImageGen`, `IVideoGen`
   and `IMusicGen` ship with mock adapters only — the four adapters that
   exist (`mock`, `anthropic`, `openai`, `llamacpp`) cover text and
   embeddings. `Routing.cpp` advertises a *local-first* policy
   (`KnownLocalProviders`: `llama-cpp` for textllm/embeddings, `whisper-cpp`
   for speechtotext) but has no entry for `imagegen`, because nothing local
   exists to name. ComfyUI is the obvious way to fill that hole.
2. **One adapter covers three capabilities.** The same server generates
   images, video and audio, so `IImageGen` + `IVideoGen` (+ later `IMusicGen`)
   come from one transport, one credential path and one progress mechanism.
3. **The licensing fits only in this shape.** ComfyUI is GPL-3.0; this
   repository is MIT. Talking to a separately installed ComfyUI over HTTP
   keeps the boundary clean. Vendoring it — the `llamacpp` pattern — would
   not (see §7).

The cost is real but bounded, and most of it is infrastructure UltraAI wants
anyway: a WebSocket method on the transport seam, multipart upload, and
binary response bodies (§6).

---

## 1. What ComfyUI is, and why it is the right local backend

ComfyUI is a node-graph GUI, API and backend for diffusion models. Its
relevance here is not the GUI — it is that ComfyUI has become the reference
runtime the diffusion ecosystem targets first: new checkpoints, LoRAs,
ControlNets and video models ship with ComfyUI workflows on day one, and the
server exposes every one of them through a stable HTTP + WebSocket API that
any client can drive.

For UltraAI that means one adapter buys access to whatever the image/video
ecosystem produces next, with no adapter change — a property no cloud image
API has, and no embedded inference library keeps up with.

## 2. Where UltraAI stands today

| Capability | Real providers today |
|---|---|
| `ITextLLM` | anthropic, openai, llama-cpp |
| `IEmbeddings` | openai, llama-cpp |
| `IImageGen` | **none** (mock only) |
| `IVideoGen` | **none** (mock only) |
| `IMusicGen` | **none** (mock only) |
| `ISpeechToText` / `ITextToSpeech` | **none** (mock only; whisper/piper options reserved in CMake) |

`UltraAI/CMakeLists.txt` already reserves `ULTRAAI_ADAPTER_WHISPER` and
`ULTRAAI_ADAPTER_PIPER` for local backends that do not exist yet, and
`Apps/UltraAIApp` already has an `ImageGenDialog` that enumerates
`ListImageGenProviders()` — so a `comfyui` provider shows up in the shipping
UI the moment it registers itself. The seam is built; nothing fills it.

## 3. The ComfyUI API surface

Routes, from the current `server.py` (all served by the ComfyUI process
itself; the adapter should treat the path prefix as configurable, since
deployments front it with proxies):

| Method | Route | Use in the adapter |
|---|---|---|
| POST | `/prompt` | Submit an API-format workflow; returns `prompt_id` (or 400 with `node_errors`) |
| GET | `/prompt` | Queue status |
| GET | `/history/{prompt_id}` | Completed outputs, keyed by node id |
| GET | `/view?filename=&subfolder=&type=output` | Fetch a produced image/video (binary) |
| POST | `/upload/image`, `/upload/mask` | Multipart upload of img2img / inpaint inputs |
| GET | `/object_info` (`/object_info/{class}`) | Node schemas — the source of checkpoint / sampler / scheduler lists |
| GET | `/models`, `/models/{folder}` | Installed model files |
| GET | `/system_stats` | Device / VRAM info |
| POST | `/interrupt`, `/free` | Cancel the running job, release memory |
| POST | `/queue`, `POST /history` | Clear or delete queued / historical items |
| GET/POST | `/api/jobs`, `/api/jobs/{id}`, `/api/jobs/{id}/cancel` | Newer job-oriented view of the same queue |
| GET | `/ws?clientId=<uuid>` | Progress stream for the submitted prompt |
| GET | `/features` | Server feature flags (negotiated on the WebSocket) |

WebSocket text frames carry `status`, `execution_start`, `executing`,
`execution_cached`, `progress` (`value`/`max`), `executed`,
`execution_error`, `execution_interrupted` and `feature_flags`. Binary
frames carry in-progress preview images behind an 8-byte header (4-byte
event type, 4-byte image format). The `clientId` passed to `/ws` is the same
one sent with `/prompt`, which is what ties a job's events to this client.

## 4. The one real mismatch: graphs vs. parameters

ComfyUI has no `prompt + steps + cfg` endpoint. `/prompt` takes a complete
**API-format workflow**: a flat map of
`{ node_id: { class_type, inputs: {...} } }`, where each input is either a
literal or a `[source_node_id, output_index]` reference. Node ids are
arbitrary per workflow, so nothing about the shape is stable across
workflows.

`ImageGenRequest`, meanwhile, is a flat parameter struct. Bridging the two
is the entire design problem, and it has a well-worn answer: **the adapter
owns a small library of API-format workflow templates plus a binding table**
that names which node input each request field patches.

```
ImageGenRequest ──► pick template (by mode + model family)
                └─► apply binding table ──► patched API graph ──► POST /prompt
```

A binding is `field -> (node id or title, input name)`. Bindings are
resolved by node *title* where possible (ComfyUI persists titles), falling
back to `class_type` search, so a template can be re-saved from the GUI
without breaking the adapter.

Concrete mapping for the built-in txt2img/img2img template:

| `ImageGenRequest` | Node / input |
|---|---|
| `prompt` | positive `CLIPTextEncode.text` |
| `negativePrompt` | negative `CLIPTextEncode.text` |
| `width`, `height`, `count` | `EmptyLatentImage.width/height/batch_size` |
| `steps`, `guidanceScale`, `seed` | `KSampler.steps/cfg/seed` |
| `scheduler` | `KSampler.scheduler` (and `sampler_name` via `options`) |
| `model` | `CheckpointLoaderSimple.ckpt_name` |
| `strength` | `KSampler.denoise` (img2img/inpaint templates) |
| `sourceImage`, `maskImage` | `POST /upload/image` → `LoadImage.image` / `LoadImageMask` |
| `upscaleFactor` | upscale template (`ImageScaleBy` / upscale-model loader) |
| `controlImages[]` | ControlNet apply nodes in the ControlNet template |
| `outputFormat` | `SaveImage` (PNG) or a WebP/JPEG save node |
| `returnAsUrl` | return the `/view?...` URL instead of inlining bytes |
| `options` | escape hatch: raw node-input overrides, or a caller-supplied workflow entirely |

Outputs come back from `/history/{prompt_id}` as
`{filename, subfolder, type}` triples per output node; the adapter fetches
each with `/view` and fills `GeneratedImage.image`. (ComfyUI's own websocket
example uses a save-to-websocket node to stream bytes back instead of
writing to the server's disk — worth supporting as an option when the server
is remote, and left out of phase 1.)

The escape hatch matters: users who already have a ComfyUI workflow must be
able to hand it to the adapter whole and only have parameters patched into
it. `RawProvider()` and `OptionsMap` are the places for that, exactly as the
module's escape-hatch rule intends.

## 5. What maps cleanly

Everything else lines up better than any cloud image API would:

| UltraAI | ComfyUI |
|---|---|
| `ImageJobEvent::Queued` | `/prompt` accepted; `status` frame with queue remaining |
| `ImageJobEvent::InProgress` + `progress` | `progress` frame (`value`/`max`), `executing` frame |
| `ImageJobEvent::PreviewImage` | binary WebSocket preview frame |
| `ImageJobEvent::Completed` | `executed` / `execution_success`, then `/history` + `/view` |
| `ImageJobEvent::Error` | `execution_error` frame |
| `StreamHandle` cancel | `POST /interrupt` (and `/queue` delete for a not-yet-running job) |
| `ImageGenProviderCapabilities` | `/object_info` checkpoint / sampler / scheduler lists, `runsLocally = true` |
| `ImageGenModelInfo.id` | checkpoint filename |
| `IVideoGen` modes | the same pipeline with video templates and video output nodes |

Error mapping needs one addition beyond `MapHttpStatus`: `/prompt` answers
400 with a `node_errors` object naming the offending nodes. Missing
checkpoint or missing custom node should surface as
`ErrorCode::ModelNotFound` with the node name in `Error::message`, not a
generic `InvalidRequest` — that is the difference between "install this
model" and "you have a bug". A pre-flight `/object_info` check lets the
adapter say which custom node a template needs before submitting anything.

## 6. Infrastructure the adapter needs first

This is the honest cost, and it is where the work actually is:

1. **WebSocket on the transport seam.** `ITransport` (adapters/_shared)
   exposes `Request` and `SseStream` only. ComfyUI's progress channel is a
   WebSocket, and `UltraNet` already provides one
   (`UltraNet_WebSocketConnect` + `UltraNetWebSocketCallbacks`, ws:// and
   wss://). Add a `WebSocketStream` method to `ITransport`, implement it in
   `UltraNetTransport`, and script it in `ScriptedTransport` so tests stay
   offline. This is the largest single item and it benefits every future
   realtime adapter (realtime voice, live STT).
2. **Multipart request bodies** for `/upload/image` and `/upload/mask`.
   `TransportRequest::body` is a `std::string`, so a multipart body can be
   composed by the adapter; a small shared helper is better than each
   adapter re-implementing boundary generation. `UltraNetMime` already
   exists on the UltraNet side.
3. **Binary responses.** `TransportResponse::body` is a `std::string`, which
   holds arbitrary bytes fine, but `/view` returns image/video payloads —
   the cassette format needs to survive them (base64 in the cassette JSON)
   for recorded tests.
4. **Cassettes for WebSocket frames** so adapter tests replay a full
   generation — submit, progress, preview, complete — with no server.
5. **A polling fallback.** If the WebSocket is unavailable (proxy, older
   build), fall back to polling `/prompt` and `/history/{id}`. Slower and
   preview-less, but it keeps `Generate()` working; worth having regardless
   as the degraded path.

Items 2–5 are small. Item 1 is a genuine module-level addition and should be
designed as such rather than smuggled in under the adapter.

## 7. Licensing and dependency policy

ComfyUI is **GPL-3.0**. This repository is MIT (`LICENSE`, Cloverleaf UG).
The distinction that governs the decision:

- **Talking to a separately installed ComfyUI over HTTP** is arm's-length
  use of a network service. No ComfyUI code is copied, linked, vendored or
  distributed; the adapter is an ordinary HTTP client of a program the user
  installed. UltraCanvas stays MIT and `THIRD_PARTY_LICENSES.md` needs no
  new entry (no code is vendored).
- **Vendoring or linking ComfyUI**, or shipping custom nodes with it, would
  produce a derivative work with GPL-3.0 obligations for whatever it is
  distributed with. Custom nodes under `custom_nodes/` are explicitly
  treated as derivative works by the project.

So the `llamacpp` pattern — `FetchContent` a pinned upstream commit and link
it — is exactly what must **not** be repeated here. llama.cpp is MIT and is
a library; ComfyUI is GPL-3.0 and is an application. The adapter must not
bundle it, must not auto-download it, and must not ship workflow templates
that depend on custom nodes the project cannot redistribute. Built-in
templates should use core nodes only; anything else belongs in a
user-supplied template directory.

Model weights are a separate matter again: checkpoints carry their own
licenses (CreativeML OpenRAIL, Flux non-commercial variants, and so on).
The adapter never ships weights and should surface the model name it used in
`ImageGenResponse::model` so applications can attribute correctly.

## 8. Security and deployment

- **Default is localhost.** ComfyUI ships with no authentication; a default
  `baseUrl` of `http://127.0.0.1:8188` is the only safe default, and the
  adapter should not silently accept a remote plaintext URL without the
  caller setting it explicitly.
- **Do not auto-launch or auto-install.** The adapter connects; the user (or
  ULTRA OS packaging) installs and runs the server. Probe `/system_stats` at
  construction and fail with a clear "no ComfyUI at <url>" rather than
  spawning anything.
- **Treat the server as trusted-but-remote.** `/view` paths come from the
  server's own history response; the adapter must not let a caller-supplied
  filename traverse into arbitrary `/view` reads, and should URL-encode the
  triple it got back rather than string-concatenating user input.
- **Credentials.** No key for local use. For proxied or hosted deployments
  (reverse proxies with auth, ComfyUI's own cloud API nodes) the existing
  `ProviderConfig::apiKey` / `apiKeyVaultRef` path applies unchanged —
  `ai.comfyui.api_key` as the vault reference, sent as a bearer header.
- **TLS.** UltraNet verifies by default and that stays on; a loopback
  `http://` URL is the documented exception, not a global relaxation.

## 9. Alternatives considered

| Option | Verdict |
|---|---|
| **ComfyUI over HTTP** | **Recommended.** Widest model coverage, real progress events, no license entanglement, no build cost. Costs: workflow-template machinery, external process. |
| **AUTOMATIC1111 / Forge WebUI** | Its `/sdapi/v1/txt2img` maps to `ImageGenRequest` almost field-for-field — genuinely less adapter code. But its model coverage now lags badly, and building on the shrinking option to save a few hundred lines is the wrong trade. Reasonable *later* as a second adapter reusing the same transport work. |
| **Embed a diffusion library (stable-diffusion.cpp)** | The true `llamacpp` analogue: in-process, no external server, permissively licensed. But it trails new architectures by months and drags GPU-backend build complexity into UltraAI's build. Not now; revisit if an in-process path becomes a hard requirement. |
| **Cloud image APIs (OpenAI images, Stability, fal/Replicate)** | Complementary, not competing — needed for users with no GPU, and a much smaller adapter each. Should follow ComfyUI, not replace it. |
| **SwarmUI / other ComfyUI front-ends** | They wrap ComfyUI anyway; adding a layer between UltraAI and the engine buys nothing. |

The two that should actually be built are ComfyUI (local, capable) and one
cloud image provider (zero-setup fallback) — in that order, because the
local-first routing policy has nothing to route to today.

## 10. Recommended plan

**Phase 0 — transport seam (prerequisite).**
`ITransport::WebSocketStream`, implemented in `UltraNetTransport`, scripted
in `ScriptedTransport`, cassette support for frames; multipart helper.
Independently useful; land it on its own merits.

**Phase 1 — `comfyui` adapter, `IImageGen`.**
`ULTRAAI_ADAPTER_COMFYUI` (default OFF, like the other local adapters),
`UltraAI/adapters/comfyui/`. Core-node templates for txt2img, img2img,
inpaint and upscale; binding table; `/object_info`-driven
`GetCapabilities()`; job progress with previews; `/interrupt` cancel;
polling fallback; cassette tests. Add `"comfyui"` to
`KnownLocalProviders("imagegen")` in `Routing.cpp` so an empty `providerId`
resolves to it when registered.

**Phase 2 — `IVideoGen` from the same adapter**, plus
`KnownLocalProviders("videogen")`. Same machinery, video templates, longer
timeouts, `/view` for video payloads.

**Phase 3 — UI and docs.** `Apps/UltraAIApp` picks the provider up for free;
what it needs is a way to choose a workflow template and point at a
templates directory. Document the adapter in `Docs/Modules/UltraAI/` and
the module `README.md` adapter table.

**Not planned:** bundling ComfyUI, installing it, shipping custom nodes, or
exposing ComfyUI graph types in any public UltraAI header (the wrapped-engine
rule in `AGENTS.md` — the graph stays inside the adapter, reachable only via
`OptionsMap` and `RawProvider()`).

## 11. Risks and open questions

| Risk | Mitigation |
|---|---|
| Templates rot as core nodes evolve | Core nodes only; validate against `/object_info` at construction and report precisely which node is missing |
| Users' own workflows have unmappable topologies | Binding overrides in `OptionsMap`; document the "your workflow, our parameters" contract |
| Long generations vs. `ProviderConfig::timeoutMs` (60 s default) | Job API is the primary path; the blocking `Generate()` uses a separate, much longer generation timeout |
| Server on another machine → images cross the network twice | Support the save-to-websocket path in phase 2 for remote servers |
| ComfyUI API changes | `/features` flag negotiation, and pin nothing — the adapter degrades to polling |
| No GPU on the user's machine | Exactly why a cloud image adapter should follow this one |

## 12. Sources

- ComfyUI server routes and WebSocket handler — [`comfyanonymous/ComfyUI` `server.py`](https://raw.githubusercontent.com/comfyanonymous/ComfyUI/master/server.py)
- [Workflow API format](https://docs.comfy.org/development/api-development/workflow-api-format) · [Workflow JSON format (DeepWiki)](https://deepwiki.com/Comfy-Org/ComfyUI/7.3-workflow-json-format)
- [ComfyUI server comms routes](https://docs.comfy.org/development/comfyui-server/comms_routes) · [API and programmatic usage (DeepWiki)](https://deepwiki.com/Comfy-Org/ComfyUI/7-api-and-programmatic-usage)
- [Hosting a ComfyUI workflow via API (9elements)](https://9elements.com/blog/hosting-a-comfyui-workflow-via-api/) · [Building a production-ready ComfyUI API (ViewComfy)](https://www.viewcomfy.com/blog/building-a-production-ready-comfyui-api)
- Licensing — [ComfyUI on GitHub (GPL-3.0)](https://github.com/comfyanonymous/ComfyUI) · [nodes and models licences and compliance](https://github.com/Comfy-Org/ComfyUI/discussions/14346) · [Which license for custom nodes?](https://github.com/comfyanonymous/ComfyUI/issues/3362)
- In-repo: `UltraAI/README.md`, `UltraAI/include/UltraAIImageGen.h`, `UltraAI/src/Routing.cpp`, `UltraAI/adapters/_shared/include/UltraAITransport.h`, `UltraCanvas/include/UltraNet/UltraNetWebSocket.h`
