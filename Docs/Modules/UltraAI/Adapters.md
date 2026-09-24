# UltraAI Adapters

**Version:** 0.1.0
**Last Modified:** 2026-08-24

## Overview

Each UltraAI capability interface is served by one or more provider
adapters. This document covers configuration, option keys and known limits
for the adapters that ship with the module; the interfaces themselves are
described in [`README.md`](README.md).

Every adapter is created the same way — through the capability factory with
a provider id, or through its own `Create…` function when you want to inject
a transport (tests) or hold the concrete type:

```cpp
#include <UltraAI.h>
using namespace UltraAI;

ImageGenConfig cfg;
cfg.providerId = "comfyui";          // empty -> routing policy decides
auto images = CreateImageGen(cfg);
```

Adapters are independently opt-in at build time (`ULTRAAI_ADAPTER_<NAME>`)
and self-register the moment they are linked, so `ListImageGenProviders()`
and friends report what this build actually has.

| Adapter | Capabilities | Where the model runs |
|---|---|---|
| `mock` | all ten | in-process |
| `anthropic` | `ITextLLM` | cloud |
| `openai` | `ITextLLM`, `IEmbeddings` | cloud or OpenAI-compatible server |
| `minimax` | `IVideoGen`, `IImageGen`, `ITextToSpeech` | cloud |
| `elevenlabs` | `ITextToSpeech` | cloud, direct or through a hosted relay |
| `qwen` | `ITextLLM`, `IEmbeddings` | local server |
| `comfyui` | `IImageGen`, `IVideoGen` | local server |
| `llama-cpp` | `ITextLLM`, `IEmbeddings` | local, in-process |

---

## `minimax` — MiniMax / Hailuo video, image and speech

Header: `UltraAIMiniMax.h`. Cloud provider; a key is required
(`ai.minimax.api_key` is the canonical vault reference). Default base URL
`https://api.minimax.io`; mainland-China accounts use
`https://api.minimaxi.com`.

Video generation is asynchronous — the adapter submits the task, polls it,
then resolves the finished file:

```
POST /v1/video_generation        -> task_id
GET  /v1/query/video_generation  -> Preparing | Queueing | Processing | Success | Fail
GET  /v1/files/retrieve          -> download_url
```

```cpp
VideoGenConfig cfg;
cfg.providerId     = "minimax";
cfg.apiKeyVaultRef = "ai.minimax.api_key";
auto video = CreateVideoGen(cfg);

VideoGenRequest req;
req.prompt      = "a kettle boiling on a stove, slow push in";
req.durationSec = 6.0;
req.width       = 1920;
req.height      = 1080;          // -> resolution "1080P"

StreamHandle job = video->GenerateJob(req, [](const VideoJobEvent& ev) {
    if (ev.kind == VideoJobEventKind::Completed && ev.result) {
        const GeneratedVideo& out = ev.result->videos.front();
        // out.video.bytes holds the mp4 (or out.video.url with
        // "return_url_only")
    }
});
```

**Option keys** (provider options or per request; never sent to MiniMax):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `job_timeout_ms` | int | 900000 | how long to poll before giving up |
| `poll_interval_ms` | int | 5000 | delay between status polls |
| `return_url_only` | bool | false | report the download URL instead of fetching the bytes |

Any other option is passed through as a top-level API field
(`prompt_optimizer`, and whatever MiniMax adds later).

**Limits.** `negativePrompt`, `steps`, `guidanceScale`, `seed`, `fps` and
`count` have no MiniMax equivalent and are ignored — one video per request.
`VideoToVideo`, `FrameInterpolation` and `Upscale` are rejected with
`ErrorCode::UnsupportedFormat`. MiniMax has no task-cancel endpoint, so
cancelling a job stops the polling while the job keeps running (and
billing) server-side. Download URLs are short-lived; fetch promptly when
using `return_url_only`.

Image generation (`image-01`) is synchronous and supports `TextToImage`
plus `Variation` (sent as MiniMax's `subject_reference`);
`ImageGenRequest::returnAsUrl` selects between a URL and decoded bytes.

**Errors.** MiniMax reports application failures in a `base_resp` envelope
*inside* HTTP 200, so the adapter checks every response body: `1002` →
`RateLimited`, `1004` / `2049` → `AuthenticationFailed`, `1008` →
`QuotaExceeded`, `1026` / `1027` → `ContentFiltered`, `2013` →
`InvalidRequest`. The numeric code is kept in `Error::providerCode`.

### Speech (`ITextToSpeech`)

`POST /v1/t2a_v2`, one-shot or SSE-streamed. The audio comes back hex
encoded in both modes.

```cpp
TextToSpeechConfig cfg;
cfg.providerId     = "minimax";
cfg.apiKeyVaultRef = "ai.minimax.api_key";
auto tts = CreateTextToSpeech(cfg);

// MiniMax has no default voice; ask which ones this account can use.
for (const VoiceInfo& voice : tts->ListVoices()) {
    std::cout << voice.id << "  " << voice.displayName << '\n';
}

SpeakRequest req;
req.text    = "Der Kessel kocht.";
req.voiceId = "male-qn-qingse";
req.style   = "happy";        // -> the API's `emotion`
req.format  = TtsAudioFormat::Mp3;

// Streaming lets playback start before synthesis finishes.
StreamHandle handle = tts->SpeakStream(req, [](const TtsStreamEvent& ev) {
    if (ev.kind == TtsStreamEventKind::AudioChunk) Play(ev.audioChunk);
});
```

| `SpeakRequest` | API field |
|---|---|
| `voiceId`, `speed`, `pitch`, `volume`, `style` | `voice_setting.voice_id` / `speed` / `pitch` / `vol` / `emotion` |
| `format`, `sampleRateHz` | `audio_setting.format` / `sample_rate` |
| `language` | `language_boost` (`"auto"` when empty) |

**Limits.** `voiceId` is required. Formats are Mp3, Wav, Flac and
PcmS16Le; Opus and Ogg are rejected. SSML is rejected — the API takes
plain text. `CloneVoice()` returns `ErrorCode::UnsupportedFormat`: MiniMax
clones through a file upload plus a separate endpoint, and the voice only
becomes listable after its first synthesis, so the console does it better.
`ListVoices()` ignores its `language` argument because MiniMax's voice
records carry no language tag.

**Text needs no adapter.** MiniMax's M-series speaks the OpenAI Chat
Completions format, so the existing `openai` adapter reaches it with a
`baseUrl` — there is deliberately no `minimax` `ITextLLM`:

```cpp
TextLLMConfig cfg;
cfg.providerId     = "openai";
cfg.baseUrl        = "https://api.minimax.io";   // -> /v1/chat/completions
cfg.apiKeyVaultRef = "ai.minimax.api_key";
cfg.defaultModel   = "MiniMax-M2";
auto llm = CreateTextLLM(cfg);
```

Compatibility is not identity — verify the specific fields you rely on.

**Not covered.** Music generation: MiniMax closed that API to new accounts
in August 2026.

---

## `elevenlabs` — ElevenLabs speech

Header: `UltraAIElevenLabs.h`. Cloud provider; a credential is required.
Default base URL `https://api.elevenlabs.io`.

The adapter serves two deployments with the same code:

| Deployment | `baseUrl` | Credential | `auth_scheme` |
|---|---|---|---|
| **Hosted** — ULTRA runs a relay that holds the ElevenLabs key, checks the user's plan and meters usage | the relay's URL | the user's session token | `"bearer"` |
| **Bring your own key** | empty (ElevenLabs) or a data-residency host | the user's key, vault reference `ai.elevenlabs.api_key` | `"xi-api-key"` (default) |

The relay forwards ElevenLabs' own wire format unchanged, so nothing in the
adapter differs between the two except where it connects and which header
carries the credential. The relay itself is not part of this repository.

```cpp
// Bring your own key.
TextToSpeechConfig cfg;
cfg.providerId     = "elevenlabs";
cfg.apiKeyVaultRef = "ai.elevenlabs.api_key";

// Hosted service instead:
//   cfg.baseUrl = "<relay URL>";
//   cfg.apiKey  = sessionToken;
//   cfg.providerOptions["auth_scheme"] = std::string("bearer");

auto tts = CreateTextToSpeech(cfg);

for (const VoiceInfo& voice : tts->ListVoices("de")) {
    std::cout << voice.id << "  " << voice.displayName << '\n';
}

SpeakRequest req;
req.text     = "Der Kessel kocht.";
req.voiceId  = "JBFqnCBsd6RMkjVDRZzb";
req.language = "de-DE";                   // -> language_code "de"
req.options["stability"] = 0.5;           // -> voice_settings.stability

StreamHandle handle = tts->SpeakStream(req, [](const TtsStreamEvent& ev) {
    if (ev.kind == TtsStreamEventKind::AudioChunk) Play(ev.audioChunk);
});
```

```
POST /v1/text-to-speech/{voice_id}?output_format=...                         one-shot, raw audio
POST /v1/text-to-speech/{voice_id}/stream/with-timestamps?output_format=...  streamed, NDJSON
GET  /v1/voices                                                              ListVoices
POST /v1/voices/add                                                          CloneVoice (multipart)
```

Streaming uses the `with-timestamps` variant on purpose: each line is a
JSON object carrying base64 audio, so an error body (`{"detail": ...}`)
cannot be mistaken for audio even though the HTTP status is only known when
the stream ends. It runs over the transport seam's `ByteStream`.

| `SpeakRequest` | API field |
|---|---|
| `voiceId` (or option `default_voice_id`) | path segment |
| `model` (or `defaultModel`, else `eleven_multilingual_v2`) | `model_id` |
| `language` | `language_code` (primary subtag) |
| `speed` (when not 1.0) | `voice_settings.speed` |
| `format`, `sampleRateHz` | `output_format` query: `mp3_44100_128`, `mp3_22050_32`, `pcm_<rate>` (8000–48000, default 24000) |

**Option keys** (provider options or per request; never sent as body
fields):

| Key | Type | Meaning |
|---|---|---|
| `auth_scheme` | string | `"xi-api-key"` (default) or `"bearer"` for a relay; provider options only |
| `default_voice_id` | string | voice used when `SpeakRequest::voiceId` is empty |
| `output_format` | string | ElevenLabs format verbatim (`ulaw_8000`, `mp3_44100_192`, ...), overriding the mapping above |
| `enable_logging` | bool | `false` requests zero-retention mode (enterprise accounts) |
| `stability`, `similarity_boost`, `style` | double | `voice_settings` fields |
| `use_speaker_boost` | bool | `voice_settings.use_speaker_boost` |

Any other option is passed through as a top-level body field (`seed`,
`previous_text`, `next_text`, `apply_text_normalization`, ...).

**Usage.** `SpeakResponse::usage.units` is the character cost from
ElevenLabs' `character-cost` response header, falling back to the
character count of the text. `durationSec` is filled for PCM output only.

**Errors.** ElevenLabs' `detail.status` refines the HTTP mapping and is kept
in `Error::providerCode`: `quota_exceeded` → `QuotaExceeded` (ElevenLabs
sends it with HTTP 401), `invalid_api_key` / `missing_permissions` →
`AuthenticationFailed`, `too_many_concurrent_requests` / `system_busy` →
`RateLimited`, `voice_not_found` → `InvalidRequest`. 422 validation errors
report their first message.

**Limits.** SSML is rejected (inline `<break time="1s" />` tags are plain
text to ElevenLabs and pass through). Wav, Flac, Opus and Ogg are rejected
unless `output_format` names an ElevenLabs format. `style`, `pitch` and
`volume` have no equivalent and are ignored. `CloneVoice()` takes inline
sample bytes only; URL samples are rejected rather than downloaded.
Speech-to-text and the WebSocket input-streaming endpoint are not
implemented yet.

---

## `qwen` — Qwen on a local OpenAI-compatible server

Header: `UltraAIQwen.h`. Runs against Ollama, vLLM, llama.cpp's server or
LM Studio — anything exposing `/v1/chat/completions`. The wire protocol is
the OpenAI adapter's; what this adapter adds is finding the server, listing
what it serves, and picking a Qwen model so nothing has to be configured:

```cpp
TextLLMConfig cfg;
cfg.providerId = "qwen";         // baseUrl and defaultModel optional
auto llm = CreateTextLLM(cfg);

ChatRequest req;
Message m; m.role = Role::User; m.text = "summarise this changelog";
req.messages.push_back(std::move(m));
ChatResponse resp = llm->Chat(req);
```

**Discovery.** With an empty `baseUrl` the adapter probes, in order:

| Endpoint | Server |
|---|---|
| `http://localhost:11434` | Ollama |
| `http://localhost:8000` | vLLM |
| `http://localhost:8080` | llama.cpp server |
| `http://localhost:1234` | LM Studio |

The first that answers `GET /v1/models` wins, and the result is cached for
the lifetime of the adapter instance. `QwenDefaultEndpoints()` returns the
list so an application can tell the user what was tried. Construction never
performs network I/O — discovery happens on first use, and when nothing
answers, calls fail with `ErrorCode::NetworkError` naming the endpoints.

**Model selection.** An empty `defaultModel` picks the first served model
whose id contains `qwen` (preferring a non-embedding model for `ITextLLM`
and an embedding one for `IEmbeddings`), falling back to the server's first
model. `ChatRequest::model` always wins.

**Credentials.** Optional: local servers run keyless. Set `apiKey` for LM
Studio or a locked-down vLLM and it is sent as a bearer token.

**Routing.** The adapter reports `runsLocally = true` and sits in
`KnownLocalProviders("textllm")` after `llama-cpp`, so an empty
`providerId` prefers in-process llama.cpp, then a local Qwen server, then
cloud providers.

An explicitly configured `baseUrl` that refuses `/v1/models` is still used:
requests that name a model work, and the model list is simply empty.

---

## `comfyui` — local image generation

Header: `UltraAIComfyUI.h`. ComfyUI is a separate program the user
installs and runs (GPL-3.0); this adapter is an HTTP + WebSocket client of
it and links none of its code. Default base URL `http://127.0.0.1:8188` —
ComfyUI ships without authentication, so the default is loopback on
purpose.

```cpp
ImageGenConfig cfg;
cfg.providerId   = "comfyui";
cfg.defaultModel = "sd_xl_base_1.0.safetensors";   // a checkpoint file name
auto images = CreateImageGen(cfg);

ImageGenRequest req;
req.prompt         = "a kettle on a stove, morning light";
req.negativePrompt = "blurry, watermark";
req.width  = 1024;
req.height = 1024;
req.steps  = 30;

StreamHandle job = images->GenerateJob(req, [](const ImageJobEvent& ev) {
    switch (ev.kind) {
        case ImageJobEventKind::InProgress:   /* ev.progress is 0..1 */ break;
        case ImageJobEventKind::PreviewImage: /* ev.preview holds a frame */ break;
        case ImageJobEventKind::Completed:    /* ev.result->images */ break;
        default: break;
    }
});
```

**How a request becomes a graph.** `/prompt` takes a whole API-format
workflow, so the adapter carries one template per mode and writes the
request into named node inputs. Nodes are found by their `_meta.title`,
falling back to a search by `class_type`:

| Request field | Node title | Node input |
|---|---|---|
| `prompt` | `UltraAI Positive` | `text` |
| `negativePrompt` | `UltraAI Negative` | `text` |
| `model` / `defaultModel` | `UltraAI Checkpoint` | `ckpt_name` |
| `width`, `height`, `count` | `UltraAI Latent` | `width`, `height`, `batch_size` |
| `steps`, `guidanceScale`, `seed`, `scheduler`, `strength` | `UltraAI Sampler` | `steps`, `cfg`, `seed`, `scheduler`, `denoise` |
| `sourceImage` | `UltraAI Source` | `image` (uploaded first) |
| `maskImage` | `UltraAI Mask` | `image` (uploaded first) |
| `upscaleFactor` | `UltraAI Upscale` | `scale_by` |

Give your own exported workflow those titles and it binds the same way.

An unset `seed` becomes a fresh random one: ComfyUI caches results per
(graph, seed), so a constant seed would return the previous image.

**Built-in templates** cover `TextToImage`, `ImageToImage`, `Inpaint` and
`Upscale`, using core nodes only — a stock ComfyUI install runs them, and
no GPL-3.0 custom nodes are redistributed. `Outpaint`,
`BackgroundRemoval`, `Variation` and ControlNet guidance need nodes a
stock install does not have, so they are rejected with
`ErrorCode::UnsupportedFormat` unless you supply a workflow.

**Option keys:**

| Key | Type | Default | Meaning |
|---|---|---|---|
| `workflow` | string | — | API-format workflow JSON to use instead of the built-in template |
| `node_overrides` | string | — | JSON `{"<node id>": {"<input>": value}}` applied after binding |
| `sampler_name` | string | `euler` | `KSampler.sampler_name` |
| `job_timeout_ms` | int | 600000 | generation deadline |
| `poll_interval_ms` | int | 1000 | `/history` poll delay |
| `use_websocket` | bool | true for `GenerateJob`, false for `Generate` | follow the job over `/ws` |

```cpp
// Your own workflow, with UltraAI filling in the parameters.
req.options["workflow"] = LoadFileAsString("flux-txt2img.api.json");
req.options["node_overrides"] = R"({"14": {"lora_strength": 0.7}})";
```

**Progress.** `GenerateJob` opens `/ws?clientId=…` *before* submitting, so
nothing is missed, and maps ComfyUI's frames onto the job events: `progress`
→ `InProgress` with a 0..1 fraction, binary frames → `PreviewImage`,
`executing` with a null node → completion, `execution_error` →
`ErrorCode::ProviderError`. If the socket is unavailable the adapter falls
back to polling `/history`, which is also what blocking `Generate` uses.
Cancelling a job posts `/interrupt`.

**Errors.** A rejected graph comes back as HTTP 400 with `node_errors`
naming the offending node; a `value_not_in_list` entry (a checkpoint or
sampler the server does not have) maps to `ErrorCode::ModelNotFound` with
the node id, class and detail in the message — the difference between
"install this model" and "fix your code".

**Capabilities.** `GetCapabilities()` reads
`GET /object_info/CheckpointLoaderSimple` once and reports the installed
checkpoints as models with `runsLocally = true`; with no server it returns
an empty model list rather than an error.

**Limits.** Source images must carry bytes — ComfyUI has no core
"load from URL" node, so a `MediaBlob` holding only a `url` is rejected.
Uploads land in ComfyUI's shared input directory, so the adapter prefixes
the file name (`ultraai-<n>-<yours>`) and does not pass `overwrite`: an
uploaded source can never replace a file the user put there.
`count` batches only in the modes that start from an empty latent.
`SaveImage` writes PNG, so `outputFormat` is advisory. Video generation
through ComfyUI is not implemented yet.

### Video (`IVideoGen`)

The same machinery with a different graph. The one built-in template is
image-to-video on **Stable Video Diffusion**, whose nodes
(`ImageOnlyCheckpointLoader`, `SVD_img2vid_Conditioning`,
`VideoLinearCFGGuidance`, `SaveAnimatedWEBP`) are part of core ComfyUI.

```cpp
VideoGenConfig cfg;
cfg.providerId   = "comfyui";
cfg.defaultModel = "svd_xt.safetensors";
auto video = CreateVideoGen(cfg);

VideoGenRequest req;
req.mode        = VideoGenMode::ImageToVideo;
req.sourceImage = firstFrame;      // must carry bytes
req.durationSec = 2.0;
req.fps         = 12;              // -> 24 frames
```

| Request field | Node title | Node input |
|---|---|---|
| `model` / `defaultModel` | `UltraAI Checkpoint` | `ckpt_name` |
| `sourceImage` | `UltraAI Source` | `image` (uploaded first) |
| `width`, `height` | `UltraAI Video` | `width`, `height` |
| `durationSec` × `fps` | `UltraAI Video` | `video_frames` |
| `fps` | `UltraAI Video` and the save node | `fps` |
| `seed`, `steps`, `guidanceScale` | `UltraAI Sampler` | `seed`, `steps`, `cfg` |

SVD is image-conditioned and has **no text encoder**, so `prompt` has
nothing to bind to in the built-in template; it is applied when a
caller-supplied workflow has an `UltraAI Positive` node. Text-to-video,
restyling, interpolation and upscaling each need model-specific loaders and
are rejected with `ErrorCode::UnsupportedFormat` unless a `workflow` option
supplies one.

**Why there is no built-in text-to-video template.** Every text-to-video
model brings its own loaders and its own field names — the frame count is
`video_frames` on SVD's conditioning node and `length` on the Hunyuan and
Wan latent nodes; the model is `ckpt_name` on a checkpoint loader and
`unet_name` on a `UNETLoader`; a `SamplerCustomAdvanced` graph has no
`steps` or `cfg` at all. A template pinned to one of those would break on
the next model generation and fail outright on an install without those
exact files. Bring your own workflow instead — the adapter binds into it:

- Nodes are found **by `_meta.title` first**, so the classes may be
  anything. Title them `UltraAI Positive`, `UltraAI Negative`,
  `UltraAI Checkpoint`, `UltraAI Video`, `UltraAI Sampler`,
  `UltraAI Source`, `UltraAI Output`.
- Each request field is written to **the first input the node already
  declares** — model to `ckpt_name` / `unet_name` / `model_name`, frame
  count to `video_frames` / `length` / `num_frames`, seed to `seed` /
  `noise_seed`. A field the node does not declare is dropped, never added:
  an unknown input would make ComfyUI reject the whole graph.
- Anything the interface cannot express goes through `node_overrides`.

The template writes an animated WebP, which `VideoFormat` has no name for:
`GetCapabilities()` reports `VideoFormat::Auto` and the produced blob
carries the real mime type. A workflow ending in `SaveWEBM` produces webm
instead. `IVideoGen` has no `returnAsUrl` field, so the adapter takes
`return_url_only` (bool) as an option — worth setting when the server is
remote and the video is large.

---

## Related documents

* [`README.md`](README.md) — the module, its interfaces and architecture.
* [`../../Research/ComfyUIIntegrationResearch.md`](../../Research/ComfyUIIntegrationResearch.md)
  — why these adapters are shaped the way they are, and the licensing
  boundary against ComfyUI.
* `UltraAI/Docs/UltraNetIntegration.md` — how network adapters use
  UltraNet (HTTP, SSE, WebSocket), threading and cancellation rules.
* `UltraAI/Docs/UltraVault.md` — credential storage for `apiKeyVaultRef`.
