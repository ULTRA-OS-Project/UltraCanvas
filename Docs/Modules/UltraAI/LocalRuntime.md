# UltraAI Local Runtime — models on this computer, nothing to install

**Version:** 0.3.0
**Last Modified:** 2026-11-10

## Overview

The local runtime runs language, embedding and speech-to-text models on the
user's own machine the way LocalAI's galleries do, without the user
installing anything: the inference engine (llama.cpp's `llama-server`, or
whisper.cpp's `whisper-server` for speech) and the model files are
downloaded on demand, verified, started as a loopback process, shared
between every UltraCanvas application that wants the same model, and
stopped when nothing has used them for a while. The `local` provider serves
`ITextLLM`, `IEmbeddings` and `ISpeechToText` through it and ranks first in
the module's local-first routing, so an application that leaves
`providerId` empty gets a model on this computer whenever one is installed
or can be.

Header: `UltraAILocalRuntime.h` (the runtime) and `UltraAILocal.h` (the
provider). Design: [`../../Research/UltraAILocalRuntimeDesignProposal.md`](../../Research/UltraAILocalRuntimeDesignProposal.md);
this document is phase 1 of its plan (chat and embeddings over
`llama-server`) and the speech-to-text part of phase 3 (`whisper-server`),
with text to speech, images and the supervisor process still to come. Built in-tree only
(`ULTRAAI_LOCAL_RUNTIME`, default ON): it needs UltraNet, UltraCrypt and
VirtualFS, which live in the shared core.

```cpp
#include <UltraAI.h>
#include <UltraAILocalRuntime.h>
using namespace UltraAI;

// An application that just wants a chat model on this computer.
TextLLMConfig cfg;
cfg.providerId = "local";                 // or empty: routing prefers "local"
cfg.providerOptions["allow_download"] = true;   // the user agreed in a dialog
auto llm = CreateTextLLM(cfg);

ChatRequest req;
Message m; m.role = Role::User; m.text = "Fasse diesen Absatz zusammen: ...";
req.messages.push_back(std::move(m));
ChatResponse resp = llm->Chat(req);       // installs, starts and answers
```

The first call downloads the engine bundle for this machine (about 30 MB)
and the recommended model (a few GB), starts the engine and answers; later
calls find it running. Without `allow_download` under the default *Ask*
policy the call fails with `ErrorCode::ModelNotFound` and `providerCode`
`"needs-install"`: the cue for an application to show what the runtime
recommends and ask.

---

## What the runtime does

| Part | What it is | Where |
|---|---|---|
| **Engine gallery** | An index of prebuilt engine bundles: one per (engine, platform, accelerator), with URL, SHA-256 and size. The copy shipped with the framework is llama.cpp's release b11669 for Linux, macOS and Windows on x64 and arm64, CPU and Vulkan (Metal on macOS), hashed on 2026-10-20, and whisper.cpp's release v1.9.0 for Linux x64 and arm64 and Windows x64, CPU, hashed on 2026-11-10; `RefreshGallery()` fetches the published one. | `LocalRuntimeBuiltinData.cpp`, `<root>/gallery/index.json` |
| **Model catalogue** | Curated models with capability, engine, licence, files, memory needs and features. Shipped with the framework; refreshed beside the gallery. A model whose engine has no build for this platform (and is not installed or overridden) is left out of `ListModels`: whisper-server has none for macOS or Windows on ARM. | `LocalRuntimeBuiltinData.cpp`, `<root>/gallery/catalogue.json` |
| **User models** | A GGUF the user has (`AddModelFile`, used in place) or a Hugging Face file (`AddHuggingFaceModel`, downloaded); for `"speechtotext"`, a whisper.cpp ggml `.bin`, served by `whisper-server`. | `<root>/user-models.json` |
| **Installs** | Background jobs on the runtime's worker: download to a `.part` file (resumed when one exists), verify the SHA-256 through UltraCrypt, move into place; an engine bundle is unpacked through VirtualFS and gets a `bundle.json`; a model gets its `model.json` last, so a half-installed model never counts as installed. | `InstallModel`, `InstallEngine`, `Cancel`, `ListJobs` |
| **Supervision** | `Ensure(modelId)` starts the model's engine on a free loopback port (`llama-server` with `--api-key-file`), waits for `/health`, and records it in `runtime.json` — shared between processes under a lock file — so a second application finds the running engine instead of starting its own. Idle engines are stopped by `StopIdle()` (run by every `Ensure`). A model whose engine died twice in a row is marked and not retried until it is stopped or reinstalled. | `Ensure`, `Touch`, `StopIdle`, `StopModel`, `StopAll`, `ListRunning` |
| **Hardware fit** | From `UltraCanvasHardwareInfo` (GPUs with dedicated memory, total memory, CPU cores): the accelerator Auto picks (Metal on macOS; Vulkan where a GPU is present on Linux and Windows; CPU otherwise; CUDA and ROCm only when chosen), and each model's fit — *recommended* (fits 70 % of the GPU's memory), *runs on CPU* (fits 70 % of system memory) or *too large*; a model whose engine has CPU builds only for this platform (whisper-server; llama-server on an Intel Mac) is at most *runs on CPU*, unless the engine is an override. | `Hardware()`, `Fit()`, `ChooseBundle()`, `Recommend()` |

### Storage

Under the machine-local data root (`UltraCanvasDataFolder.h`: `%LOCALAPPDATA%`
on Windows, `~/Library/Application Support` on macOS, `$XDG_DATA_HOME` or
`~/.local/share` elsewhere), so the gigabytes never roam with a profile:

```
<root>/UltraAI/
├── engines/<engine>/<version>-<platform>-<accelerator>/   an unpacked bundle, with bundle.json
├── models/<model id>/                                      the files, with model.json
├── downloads/*.part                                        in flight; resumed next time
├── gallery/index.json, catalogue.json, topics.json         the refreshed copies
├── logs/<engine>-<model>.log                               the engine's output (rotated at 4 MB)
├── runtime.json                                            running engines: pid, port, model, started, last used
├── runtime.key                                             the loopback API key (owner-only on POSIX)
├── runtime-settings.json                                   the settings below
├── topic-preferences.json                                  the user's model per topic, new models (Topics)
└── user-models.json                                        models the user added
```

`RootFolder()` returns it for a settings page's *Open folder*;
`DiskUsedBytes()` what engines and models take.

### Settings

`GetSettings()` / `SetSettings()`, persisted:

| Setting | Default | Meaning |
|---|---|---|
| `policy` | `Ask` | `Never`: installs only from a settings page; `Ask`: `Ensure` installs when the caller passes `allowDownload` (the dialog's answer); `Automatic`: `Ensure` installs silently |
| `preferredAccelerator` | `""` (Auto) | `"cpu"`, `"vulkan"`, `"metal"`, `"cuda12"`, `"rocm"` — the bundle to use when the gallery has one for this platform |
| `galleryUrl` | the built-in default | a mirror's `index.json`; `catalogue.json` is read from beside it |
| `idleMinutes` | 15 | stop an engine nobody used for this long (0 stops at the next sweep) |
| `diskCapBytes` | 20 GB | an install that would exceed it fails with `QuotaExceeded` |
| `startTimeoutSeconds` | 0 | how long a start may take; 0 scales it with the model's size (about 100 MB/s plus 20 s, at least 30 s) |

### The engine's command line

For `llama-server`: `--host 127.0.0.1 --port <free> --api-key-file
<root>/runtime.key --model <file> --alias <model id>`, `--mmproj <file>`
when the model carries an `mmproj*.gguf`, `--embeddings` for an embedding
model, `--n-gpu-layers 999` when the model fits the GPU (0 on a CPU bundle;
otherwise the engine's own fitting decides), `--threads` from the physical
cores (two left for the desktop), then the gallery's `defaultArgs`
(`--jinja`, for tool calls) and the model's `engineArgs` (`--ctx-size`,
`--pooling`). `local_detail::BuildEngineArgs` is the one place this is
decided.

For `whisper-server`: `--host 127.0.0.1 --port <free> --model <file>
--inference-path /v1/audio/transcriptions --language auto --threads <n>`,
then the gallery's and the model's arguments. The inference path is the
OpenAI audio API's, so the OpenAI adapter speaks to it unchanged; `auto`
because the server's own default language is English. whisper.cpp's server
takes no key: like every engine it listens on loopback only, but any
process of the user's can reach it there. The builds are CPU builds, so a
speech model's fit is *runs on CPU* whatever the GPU.

Engines reach their libraries through their own folder (`$ORIGIN` on
Linux, the bundle's layout on macOS and Windows), so a bundle runs from
wherever it was unpacked.

### Using an engine you built yourself

`LocalRuntimeOptions::engineOverrides["llama-server"]`, or the environment
variable `ULTRAAI_LOCAL_ENGINE_LLAMA_SERVER`, names a folder holding (or
the path of) a `llama-server` executable; the runtime uses it instead of a
bundle and downloads no engine. A developer's own CUDA build, or a test's
fake engine. `ULTRAAI_LOCAL_ENGINE_WHISPER_SERVER` does the same for
`whisper-server` - on macOS, a whisper.cpp built with Metal is how the
speech models are offered there.

---

## The gallery index

`index.json`, format version 1:

```json
{
  "formatVersion": 1,
  "published": "2026-10-20",
  "engines": [{
    "id": "llama-server",
    "version": "b11669",
    "license": "MIT",
    "executable": "llama-server",
    "serves": ["textllm", "embeddings"],
    "defaultArgs": ["--jinja"],
    "bundles": [
      {"platform": "linux-x64",   "accel": "cpu",    "url": "https://...", "sha256": "...", "size": 17851611},
      {"platform": "linux-x64",   "accel": "vulkan", "url": "https://...", "sha256": "...", "size": 30497889},
      {"platform": "macos-arm64", "accel": "metal",  "url": "https://...", "sha256": "...", "size": 12191582, "minOs": "13.3"},
      {"platform": "win-x64",     "accel": "cuda12", "url": "https://...", "sha256": "...", "size": 254000000, "optional": true}
    ]
  }]
}
```

Platforms are `linux-x64`, `linux-arm64`, `macos-x64`, `macos-arm64`,
`win-x64`, `win-arm64`. A bundle marked `optional` is offered, never
chosen by Auto. `minOs` is honoured on macOS. A bundle is an archive
(`.tar.gz` or `.zip`) holding the executable at any depth; the runtime
finds it by name, breadth first.

## The model catalogue

`catalogue.json`, format version 1:

```json
{
  "formatVersion": 1,
  "published": "2026-10-20",
  "models": [{
    "id": "qwen3-4b-q4",
    "displayName": "Qwen3 4B (Q4_K_M)",
    "description": "...",
    "capability": "textllm",
    "engine": "llama-server",
    "license": "Apache-2.0",
    "files": [{"name": "Qwen3-4B-Q4_K_M.gguf", "url": "https://huggingface.co/.../resolve/main/...", "sha256": "", "size": 2497000000}],
    "requires": {"memoryBytes": 4000000000, "gpuMemoryBytes": 3500000000},
    "features": {"contextWindow": 32768, "tools": true, "vision": false, "embeddingDimensions": 0},
    "engineArgs": ["--ctx-size", "8192"],
    "tags": ["chat", "tools", "recommended"]
  }]
}
```

A file whose `sha256` is empty is verified against the hash its host
reports: for a Hugging Face `resolve` URL the runtime asks the repository's
tree API for the file's LFS hash and size before downloading, and records
them in the model's `model.json`. The built-in catalogue ships that way
until its hashes are pinned; a catalogue entry with a hash is verified
against that hash only. `requires` drives the fit; without it, 1.2 x the
files' size stands in. `tags` holding `recommended` puts a model first in
`Recommend()` for its capability.

The built-in catalogue (2026-10-21): Qwen3 4B, 1.7B and 8B for chat,
Gemma 3 4B with vision, Qwen3 Embedding 0.6B and Nomic Embed Text v1.5 for
embeddings. Its file names and sizes were checked against the Hugging Face
repositories on that date; the hashes are still read from the tree API at
install time.

---

## Topics

A topic is what a model is used for: general chat, programming,
translation, summaries, pictures and documents, search, or one the model
server adds. The server ([`Server/UltraAIModelServer`](../../../Server/UltraAIModelServer/README.md),
managed with [UltraAI ServerAdmin](../../UltraAIServerAdmin/README.md))
publishes `topics.json` beside the index; `RefreshGallery()` fetches it
with the catalogue, and the copy built into the framework stands in until
then (and for a mirror that publishes none).

```json
{
  "formatVersion": 1,
  "published": "2026-11-06",
  "topics": [
    {"id": "translation", "name": "Translation", "description": "...", "capability": "textllm",
     "models": [{"id": "qwen3-4b-q4", "default": true}, {"id": "qwen3-8b-q4"}, {"id": "qwen3-1.7b-q8"}]}
  ]
}
```

`models` is the server's order, best first; the one marked `default` is
the server's choice (the first one when none is marked). The built-in
topics are `chat`, `code`, `translation`, `summary`, `vision` and
`search`.

The user's side is `TopicPreferences`, kept in `topic-preferences.json`:

| Field | Meaning |
|---|---|
| `choices[topic].modelId` | `""` (the default): follow the server's choice, and move with it; otherwise the model the user picked |
| `choices[topic].autoInstall` | while following: `ApplyTopicUpdates` downloads the server's choice when it is not installed (default on) |
| `newModels` | `Notify` (default): `NewModels()` lists catalogue models published since the user last looked; `Automatic`: also downloads one that becomes a followed topic's choice; `Ignore`: nothing is reported |
| `seenModels` | the catalogue ids already reported; the whole catalogue the first time, so only later ones count as new |

`ModelForTopic(topic)` decides which model a topic uses on this computer:
the user's pick while it exists and fits; else the server's choice when it
fits (installed, or installable because the user lets it download); else
an installed model the topic offers; else the first offered model that
fits; else `Recommend(capability)`. A model that is *too large* is never
chosen.

```cpp
LocalRuntime& rt = LocalRuntime::Instance();
for (const ModelTopic& t : rt.ListTopics()) {
    // t.name, t.capability, t.ServerDefault(), rt.ModelForTopic(t.id)
}
rt.SetTopicModel("code", "qwen3-8b-q4");          // the user's pick; "" follows the server again
TopicPreferences p = rt.GetTopicPreferences();
p.newModels = NewModelPolicy::Automatic;
rt.SetTopicPreferences(p);
rt.RefreshGallery();                               // the server's index, catalogue and topics
rt.ApplyTopicUpdates(onProgress);                  // download what the topic settings call for
for (const LocalModelInfo& m : rt.NewModels()) { /* tell the user */ }
rt.MarkModelsSeen({});                             // all of them seen
```

`ApplyTopicUpdates` does nothing under `DownloadPolicy::Never`. An
application asks for a topic's model through the `local` provider with
`providerOptions["topic"] = "translation"` (and no `defaultModel`). The
dashboard's *Model topics* settings page (`Apps/UltraAIApp/UltraAITopicsPage`)
is the user's view of all of this.

---

## The `local` provider

`CreateTextLLM` / `CreateEmbeddings` / `CreateSpeechToText` with
`providerId = "local"`, or an empty `providerId` when nothing names another
provider: `local` is first in `KnownLocalProviders` for `textllm`,
`embeddings`, `translator` and `speechtotext`.

| Config field | Meaning |
|---|---|
| `defaultModel` | a catalogue id, `file:<path>`, `hf:<repo>:<file>`, or empty for the topic's model or `Recommend(capability)` |
| `providerOptions["topic"]` | string: with no `defaultModel`, the model the user set for this topic (`ModelForTopic`) |
| `providerOptions["allow_download"]` | bool: the first call may install under the *Ask* policy |
| `timeoutMs`, other `providerOptions` | passed to the OpenAI-wire delegate |
| `apiKey`, `apiKeyVaultRef`, `baseUrl` | ignored: the runtime's endpoint and key are used |

A request's `model` names another local model; each gets its own engine.
`GetCapabilities()` lists the installed models first and then the
catalogue's for this computer with ` (not installed)` in the name, each
`runsLocally`, so a picker shows both. `RawProvider()` is the OpenAI
delegate of the default model once it has been used.

Speech to text (`CreateLocalSpeechToText`): the topic is `transcription`
unless one is set (Whisper Small by default; Whisper Base and Whisper
Large v3 Turbo are offered). Files only - live transcription is an error
event. WAV, MP3, FLAC and Ogg Vorbis go as they are (whisper.cpp decodes
them), with no size limit; `translateToEnglish` is whisper's own
`translate` field on the same path; segments come with `verbose_json`, and
their word timings only when `wordTimestamps` is asked for. The default
60 s timeout is raised to 30 minutes, as an hour of sound takes minutes on
the CPU; a timeout set by the caller is kept. UltraAI's speech transcriber
for UltraCanvas applications (`UltraAISpeechTranscription.h`) uses it
first; a `local` with nothing downloaded under the *Ask* policy steps aside
for the next provider.

Errors: `ModelNotFound` / `"needs-install"` (a download is needed and not
allowed), `ModelNotFound` / `"too-large"` (the model does not fit this
computer's memory), `ProviderError` with the log's last lines (the engine
ended while starting), `ProviderError` / `"start-failed"` (two failed
starts; stop or reinstall the model to try again), `Timeout` (the engine
did not answer in time), `NetworkError` (a download failed; the `.part`
is kept for a resume), `QuotaExceeded` (the disk cap).

---

## For a settings page

```cpp
LocalRuntime& rt = LocalRuntime::Instance();

for (const LocalModelInfo& m : rt.ListModels()) {
    // m.displayName, m.capability, m.license, m.sizeBytes,
    // m.fit (Recommended / RunsOnCpu / TooLarge / Unknown),
    // m.installed, m.running, m.installedPath
}
for (const EngineInfo& e : rt.ListEngines()) {
    for (const EngineBundleInfo& b : e.bundles) {
        // b.accelerator, b.sizeBytes, b.installed, b.recommended, b.optional
    }
}
const std::string job = rt.InstallModel("qwen3-4b-q4", [](const InstallProgress& p) {
    // p.state: Queued, Downloading (p.bytesDone / p.bytesTotal), Verifying,
    // Unpacking, Done, Failed (p.error), Cancelled — on the runtime's worker
});
rt.Cancel(job);
rt.RemoveModel("qwen3-4b-q4");
rt.RemoveEngine("llama-server", "vulkan");
for (const RunningEngineInfo& r : rt.ListRunning()) { /* r.modelId, r.baseUrl, r.processId */ }
rt.StopAll();
```

Progress callbacks fire on the runtime's worker thread: hand them to the
UI thread before touching an element.

---

## Testing

`UltraAI/tests/test_local_runtime.cpp` runs against `fake_engine.cpp`, a
stand-in for `llama-server` that takes the same command line and serves
`/health`, `/v1/models`, `/v1/chat/completions` and `/v1/embeddings` over
loopback: the built-in data parses; the fit and the bundle choice; an
install through an injected copy fetcher (progress, a wrong hash refused,
a failed transfer resumed, `model.json` last); `Ensure` under each policy,
the shared record found by a second runtime, the idle stop, the restart
marker; and the `local` provider's chat, embeddings and speech to text end
to end (the fake engine started as `whisper-server` answers on its
inference path; a speech model whose engine has no build here is not
offered). Nothing
in it touches the network. `ULTRACANVAS_BUILD_ULTRAAI_TESTS=ON` builds the
module's suite in-tree and CI runs it with the rest.

To try the real engine on a machine with no model installed:

```cpp
LocalRuntimeSettings s = LocalRuntime::Instance().GetSettings();
s.policy = DownloadPolicy::Automatic;
LocalRuntime::Instance().SetSettings(s);
// then any CreateTextLLM(cfg) with cfg.providerId = "local"
```

## Limits (0.1.0)

- Chat, embeddings and speech to text: image generation and text-to-speech
  engines are still to come.
- `whisper-server` comes from whisper.cpp's own release archives, which
  hold CPU builds for Linux x64 and arm64 and Windows x64 only: no macOS,
  no Windows on ARM, no GPU build. The gallery the proposal describes
  (whisper.cpp built by our CI, Metal and Vulkan) fills those in without a
  format change. The whisper models' hashes are taken from Hugging Face at
  install time; their sizes are whisper.cpp's published figures.
- One `llama-server` process per model; the router mode (one process for
  every GGUF) and the `UltraAIHost` supervisor are phase 2. An engine
  started by an application that exits stays until an idle sweep from any
  application stops it.
- The engine bundles are llama.cpp's own release archives, pinned by hash;
  the UltraCanvas-built, signed gallery is phase 0 of the proposal and
  replaces the built-in index without a format change. On macOS the
  upstream archives are built for 13.3 and later, and a downloaded
  executable runs as it is (UltraNet sets no quarantine attribute).
- The catalogue's hashes are taken from Hugging Face at install time until
  they are pinned; the file names and sizes were checked against the
  repositories on 2026-10-21.
- The gallery index is not yet signed; it is fetched over TLS from a URL
  the settings control.
- The *Get a local model* dialog is the module's (`UltraAI/ui/`,
  `UltraAILocalModelDialog.h`, library `UltraAI_UI`): an application links
  `UltraAI_UI` beside `UltraAI_Core`, tests an error with
  `UltraAILocalModelDialog::NeedsInstall(error)` and calls `Show(window,
  serviceName, capability, preferredModelId, onReady)`; the dashboard's Chat
  and Texter's Translate Selection do. The *Local models* settings page is
  the dashboard's (`Apps/UltraAIApp/UltraAILocalModelsPage`).
