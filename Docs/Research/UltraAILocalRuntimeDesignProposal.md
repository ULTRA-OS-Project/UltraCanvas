# UltraAI local runtime — models on this computer, with nothing to install (Investigation and Proposal)

**Date:** 2026-10-20
**Status:** Phase 1 of §9 implemented on 2026-10-20 (`UltraAI/runtime/`, the `local` provider, the routing change; `Docs/Modules/UltraAI/LocalRuntime.md` documents what was built and where it departs from this text — the upstream macOS archives turned out to be built for macOS 13.3, not 26); the dashboard's Local models page and the Get-a-local-model dialog of phase 2 followed on 2026-10-21 in `Apps/UltraAIApp/` (the dialog is the module's, `UltraAI/ui/`, and Texter's Translate Selection uses it too); phase 0, the rest of phase 2 and phases 3–4 are still a proposal
**Scope:** how UltraAI runs language, embedding, speech, image and
text-to-speech models *on the user's own machine*, next to the endpoints it
already has for Claude, ChatGPT and other hosted providers — with the
inference engines and the model files fetched on demand, the way LocalAI's
backend and model galleries do it, and without the user installing or
configuring Ollama, LM Studio, ComfyUI, Python, CUDA or anything else.
**Sibling in shape:**
[`ComfyUIIntegrationResearch.md`](ComfyUIIntegrationResearch.md) (the last
local-provider decision) and
[`UltraWinDesignProposal.md`](UltraWinDesignProposal.md) (a runtime the
framework spawns rather than links).

Everything said about the repository was checked against the tree on this
date. What is said about llama.cpp, whisper.cpp, stable-diffusion.cpp, Piper
and LocalAI comes from their published repositories and release pages on
this date (§11). A detail that could not be checked from this environment is
marked **[unverified]** rather than guessed.

---

## 1. Recommendation

**Run the models in separate engine processes that UltraAI downloads,
starts, shares and stops itself.** A new headless part of the module,
`UltraAI/runtime/` (public header `UltraAILocalRuntime.h`), owns three
things: an *engine gallery* (prebuilt engine bundles per platform and
accelerator, verified by hash), a *model catalogue* (curated models with
sizes, hashes, licences and hardware requirements, plus any GGUF the user
brings) and a *supervisor* (start an engine on loopback for a model, keep it
healthy, share it between the applications that want it, unload it when
idle). A new provider id, `local`, serves `ITextLLM`, `IEmbeddings`,
`ISpeechToText`, `ITextToSpeech` and `IImageGen` through it; on the wire it
is the OpenAI-compatible protocol the module's `openai` and `qwen` adapters
speak today, so the work is downloading and supervising, not a new client.
The UltraAI dashboard gets a *Local models* page, and every application gets
one framework dialog, *Get a local model*, that appears the first time a
service is asked for and nothing is installed.

The decisions, one line each; the rest of the document is the evidence.

| Decision | Reason |
|---|---|
| Separate processes, not in-process inference (§4) | A model is 2–8 GB and every UltraCanvas app that wants it would map its own copy; a GPU driver fault or a malformed GGUF takes down the engine, not UltraMail; GPU acceleration (CUDA, ROCm, Vulkan, Metal) is chosen when the engine is *built*, so only a downloaded engine matched to the machine can use the GPU without bloating every app with every backend. The in-process `llama-cpp` adapter stays for embedded and test use. |
| Engines from an UltraCanvas-owned gallery, built and signed by our CI, not fetched from upstream release pages (§5) | Upstream llama.cpp publishes 36 archives per nightly under names that change, with no hashes in a machine-readable index and macOS builds that need a newer macOS than the suite supports; whisper.cpp publishes no binaries at all; Piper publishes Python wheels. Our gallery index names one bundle per (engine, OS, CPU, accelerator), pinned to the framework version, SHA-256 and signed, and the bundles are notarized on macOS like the apps. |
| Vulkan first for the GPU, CPU as the floor, CUDA/ROCm as an opt-in upgrade (§6.5) | One 30 MB Vulkan bundle runs on NVIDIA, AMD and Intel with the driver the user already has; a CUDA bundle is 150–570 MB and needs a matching driver. On macOS the plain bundle carries Metal. The first download has to be small enough to happen while the user waits. |
| A curated model catalogue with "recommended for this computer" (§6.3) | The user should see three or four choices that fit their RAM and VRAM, not Hugging Face. `UltraCanvasHardwareInfo` already reports both. Anyone may still point at a GGUF file or a Hugging Face repository. |
| The OpenAI wire protocol on loopback, with a per-installation key (§6.6, §6.8) | `llama-server` serves chat, tools, vision, embeddings and reranking over it; `sd-server` serves images under `/v1/...`; the module's existing client code already speaks it. Loopback plus `--api-key` keeps another user's process off the engine. |
| One engine per model, shared across applications through a manifest file; idle unload (§6.7) | Texter translating and UltraMail summarising should not each load a 4 GB model. The engine's own `--sleep-idle-seconds` releases memory, and the supervisor stops it when nothing has used it for a while. |
| A small supervisor process, `UltraAIHost`, in phase 2 — the library spawns directly in phase 1 (§6.6, §9) | Spawning from the library gets chat working in one phase; the host adds "survives the app that started it" and one place for cross-app lifecycle on ULTRA OS. Both sit behind the same `LocalRuntime` API. |
| Speech-to-text through `whisper-server`, images through `sd-server`, text-to-speech through Piper as a spawned GPL program (§5) | Each is the established engine for its kind; the first two are MIT. Piper is GPL-3.0 since its move to OHF-Voice, so it is spawned and never linked, under the rule that already covers Wine, winetricks and QEMU. |

What this does *not* propose: bundling LocalAI itself (§3), building
engines at the user's machine, or replacing the `qwen` and `comfyui`
adapters — a user who already runs Ollama or ComfyUI keeps using them, and
the routing prefers `local` only because it needs nothing started by hand.

---

## 2. What the tree has today, and the gap

UltraAI (`UltraAI/`, `Docs/Modules/UltraAI/`) has ten capability
interfaces, a registry with self-registering adapters, and a routing policy
that is local-first by design (`UltraAI/include/UltraAIRouting.h`): an empty
`providerId` resolves to `llama-cpp`, then `qwen`, then — only when the
user allows it — a cloud provider, then the mock. Three adapters run models
locally:

| Adapter | How | What the user must do first |
|---|---|---|
| `llama-cpp` (`UltraAI/adapters/llamacpp/`) | in-process, llama.cpp fetched and built from a pinned commit at configure time | nothing — but `ULTRAAI_ADAPTER_LLAMACPP` is OFF by default and no CI row turns it on, so **no shipped build has it**; and the model is a GGUF *path* the user must have |
| `qwen` (`UltraAI/adapters/qwen/`) | HTTP client; probes Ollama, vLLM, llama.cpp server and LM Studio on their usual ports | install and run one of those programs, and pull a model in it |
| `comfyui` (`UltraAI/adapters/comfyui/`) | HTTP + WebSocket client of a ComfyUI server | install ComfyUI (Python, PyTorch, checkpoints) and run it |

So in a packaged UltraCanvas suite today the "local-first" route resolves,
on a machine with nothing installed, to **the mock**. The dashboard's
*Local & cloud* page (`Apps/UltraAIApp/UltraAISettingsWindow.cpp:434`)
says "Off (the default): every service runs locally", which is true of the
policy and not of any machine. The dashboard's endpoint model
(`Apps/UltraAIApp/UltraAIEndpoints.h`) is a provider id, a base URL, a
default model and a vault reference for a key — exactly right for hosted
services, and for a local engine it has nowhere to say "download this".

What the tree already has that the runtime builds on, so that none of it is
written twice:

- **Downloading to a file, with progress**: `UltraNet_HttpDownloadFile`
  with `onDownloadProgress` (`UltraCanvas/include/UltraNet/UltraNetHttp.h`),
  TLS verification on by default, the system proxy honoured.
- **A download-on-demand precedent**: the OCR plugin's language packs
  (`UltraCanvas/Plugins/Documents/OCR/include/UltraCanvasOCRPlugin.h`:
  catalogue, `IsLanguageInstalled`, `DownloadLanguage`, `EnsureLanguages`),
  which stream to a `.part` file and rename it into place so a partial
  download is never mistaken for a pack.
- **Hashing**: `UltraCrypt_Hash(SHA256, …)` (`UltraCrypt/UltraCryptCore.h`).
- **Starting programs**: `RunProcessCaptured` (argv, never a shell) and
  `LaunchDetachedProcess` (`UltraCanvasUtils.h`), and
  `LaunchWatchedProgram` with per-run folders and owner markers that a
  later start sweeps (`UltraCanvasArchiveRun.h`).
- **Hardware**: `UltraCanvasHardwareInfo.h` reports GPUs with vendor and
  dedicated VRAM, NPUs, total and available memory, and the CPU's
  instruction sets and x86-64 psABI level.
- **Where to put gigabytes**: the machine-local data root
  (`UltraCanvasLocalDataFolderRoot`, `UltraCanvasDataFolder.h`), made for
  "a cache of downloads" that must not roam with a Windows profile.
- **One running copy**: `UltraCanvasSingleInstance` and the archive-run
  markers show the lock-and-manifest pattern for "who owns this process".
- **The wire protocol**: the `openai` adapter (chat, streaming, tools,
  structured output, embeddings) and the `qwen` adapter's endpoint
  discovery; the shared transport seam with cassette record/replay for
  tests (`UltraAI/adapters/_shared/`).

---

## 3. What LocalAI does, and what to borrow

LocalAI (MIT, Go) is the reference the request names. Its shape, from its
documentation and `backend/index.yaml` (§11):

- **Backends are OCI container images pulled on demand.** The gallery is a
  YAML list; each entry has a name, a description, tags, a licence and a
  `capabilities` map from the machine's class to a concrete image —
  `default: cpu-llama-cpp`, `nvidia: cuda12-llama-cpp`, `amd: rocm-llama-cpp`,
  `vulkan: vulkan-llama-cpp`, `metal: metal-llama-cpp`, Intel SYCL and
  Jetson variants. LocalAI detects the GPU and pulls the matching image.
  Each backend is a gRPC server process that LocalAI starts and talks to.
- **Models are a second gallery**: a YAML per model naming the backend, the
  files with their SHA-256 and sizes, the prompt template and default
  parameters.
- **The UI** is one list with *Installed* and *Catalog* views; each row has
  the state (Current, Update, Not installed, Queued, a progress bar with
  Cancel, a failed install with its reason and Retry); installs run in the
  background; removing a backend warns which models stop working.

Worth borrowing: the two-gallery split, the per-machine-class capability
map, the hash-per-file model entries, the row states and the background
installs. Not worth borrowing: the OCI images (they need a registry client
and an image store, and they package a whole root filesystem where a 30 MB
archive of binaries does the same), the gRPC layer between LocalAI and its
backends (the engines already speak HTTP), and LocalAI itself as a
dependency — it is a server in front of our server, several hundred
megabytes, with its own configuration surface the user would have to see.

---

## 4. In-process or a separate process

The question the request asks ("inside UltraAI or in a separate process")
decides most of the rest.

**In-process** (the current `llama-cpp` adapter, made the default):

- *Memory*: every application that wants the model maps its own copy. The
  framework's one-shared-core rule exists so that running applications
  share one copy of code and state; a 4 GB model per process is the
  opposite.
- *Isolation*: a GPU driver fault, an out-of-memory in the backend or a
  malformed GGUF kills the application. A GGUF is a file from the internet
  parsed by C++; the engine process is the blast radius we want.
- *Acceleration*: ggml selects CUDA, ROCm, Vulkan, Metal or SYCL when it is
  built. ggml can also load backends as shared libraries at run time
  (`GGML_BACKEND_DL`, which is how the upstream binaries carry several CPU
  variants in one archive), so an in-process engine *could* download its
  backends — but the engine itself still has to be in the framework
  library on every platform, built from a pinned commit of a project that
  changes daily and that no CI row builds today.
- *Updating*: the engine moves with the framework release. llama.cpp gains
  model architectures weekly; a user whose new model needs last week's
  engine waits for the next suite.

**A separate process** that the module downloads and supervises:

- One engine per model, shared by every application on the machine; the
  model loads once.
- A crash is a reconnect with an error, and the supervisor restarts it.
- The engine bundle is chosen for the machine (OS, CPU, accelerator); the
  framework carries no inference code.
- An engine update is a gallery entry, not a release.
- The cost: process management (spawn, health, ports, keys, idle, orphan
  sweep), a loopback hop per request (microseconds against seconds of
  generation), and a download the user has to see and consent to.

The separate process wins on every axis but the first-run download, which
the in-process adapter also has (the model). **Keep the in-process
`llama-cpp` adapter** for what it is good at — an embedded ULTRA OS device
with one application, tests that need exact token counts without a
process, structured output through GBNF — and make `local` the default
local route.

---

## 5. The engines: what exists, what we must build, licences

What each upstream publishes today, and what the gallery would carry.

| Engine | Capabilities | Licence | Upstream prebuilt binaries | What our gallery does |
|---|---|---|---|---|
| **llama.cpp** `llama-server` (ggml-org) | chat and completion, tool calls (`--jinja`), vision (`--mmproj`), embeddings (`/v1/embeddings`), reranking, an Anthropic-Messages endpoint, router mode | MIT | Yes, per nightly `bNNNN`: ubuntu x64 / arm64 (cpu, vulkan, cuda 12.8 and 13.4, rocm, sycl, openvino), macos arm64 / x64 (Metal in the plain build), win x64 / arm64 (cpu, vulkan, cuda, rocm, sycl, openvino, opencl-adreno), android; 11–30 MB for cpu / vulkan / macos, 140–250 MB for cuda, plus 370–570 MB `cudart` archives | Repackage the pinned nightly's cpu, vulkan and macos archives under our names with hashes; sign and notarize the macOS one; offer cuda / rocm as optional upgrades |
| **whisper.cpp** `whisper-server` (ggml-org) | speech-to-text: `/inference` (multipart audio, JSON with segments), `/load` to switch models | MIT | **No** — v1.9.5 ships source only | Build it in our CI (same ggml backends as llama.cpp: cpu, vulkan, Metal) |
| **stable-diffusion.cpp** `sd-server` (leejet) | text-to-image, image-to-image, upscaling; compatibility APIs under `/v1/...` and `/sdapi/v1/...` | MIT | Yes: Linux x64 (cpu 25 MB, vulkan 35 MB, rocm 282 MB), macOS arm64 (35 MB), Windows x64 (cpu 17 MB, vulkan 28 MB, cuda12 328 MB, rocm 252 MB) | Repackage cpu / vulkan / macOS; cuda / rocm optional |
| **Piper** (OHF-Voice `piper1-gpl`) | text-to-speech, ~hundred voices, embeds espeak-ng | **GPL-3.0** | Python wheels per platform (32 MB) and a source tarball; `libpiper` is a C/C++ library in the repository | Build the `piper` command-line program in our CI and spawn it per request (never link `libpiper`); label the licence in the catalogue; `Docs/Dependencies.md` gets a *runtime, not linked* row as for Wine |
| **ComfyUI** | images, video | GPL-3.0 | n/a (Python) | unchanged — a program the user installs; not part of the gallery |

Two things decide the shape of the gallery:

1. **We cannot point at upstream release pages.** The names carry the
   build number, the set of archives changes, there is no index with
   hashes, whisper.cpp has no binaries, and the upstream macOS archive
   is built on a macOS newer than the suite's deployment target
   (`MACOSX_DEPLOYMENT_TARGET` 14.0 in CI) **[unverified for the current
   nightly]**. A gallery index of our own, versioned with the framework,
   is the only thing the framework can pin to.
2. **Signing.** A binary downloaded by a notarized app and executed with
   `posix_spawn` is checked by Gatekeeper only if it carries the quarantine
   attribute, which a download through UltraNet does not set; but on Apple
   silicon an unsigned Mach-O is refused outright, and a hardened-runtime
   parent may not load unsigned libraries. The safe rule is the one the apps
   follow: our CI signs and notarizes every macOS engine bundle
   (`package-macos.sh` has the machinery), and signs the Windows ones with
   the same certificate as the `.exe`s. Linux needs nothing.

The engine bundle is a plain archive: `bin/` (the server and its shared
libraries, including ggml's loadable backends), `LICENSE`, and a
`bundle.json` naming the engine, version, platform, accelerator and the
command line template the supervisor uses. Unpacking goes through VirtualFS
like `UltraCanvasArchiveRun`.

---

## 6. The runtime

A UI-free part of the module: `UltraAI/runtime/{include,src}/`, built as
`UltraAI_Runtime` and linked by `UltraAI_Core` under
`ULTRAAI_LOCAL_RUNTIME` (ON when UltraNet is available, which is every
in-tree build). It needs UltraNet (downloads, HTTP to the engine),
UltraCrypt (hashes, signature check) and VirtualFS (unpacking) — all
inside the shared core. Nothing in it draws.

### 6.1 Storage

Under the machine-local data root, so it never roams and a profile backup
does not carry gigabytes:

```
<LocalDataFolderRoot>/UltraAI/
├── engines/<engine>/<version>-<platform>-<accel>/   one unpacked bundle
├── models/<catalogue id>/                            the model's files
│     ├── model.json                                  the catalogue entry it was installed from
│     └── *.gguf, mmproj-*.gguf, *.safetensors, *.onnx
├── downloads/*.part                                  in flight; swept on start
├── gallery/index.json                                the last fetched gallery, with its signature
├── runtime.json                                      running engines: pid, port, model, started, last used
└── runtime.key                                       the loopback API key (0600)
```

Settings (the download policy, the disk cap, the idle timeout, the
preferred accelerator, a gallery mirror URL) go in the settings root, read
through `UltraCanvasSettingsFolder.h`; the dashboard already keeps its
`config.ini` there.

### 6.2 The engine gallery

`index.json`, published from our release pipeline, fetched at most once a
day and verified with an Ed25519 signature over its bytes (public key
compiled into the module) so that a mirror or a proxy cannot substitute an
engine. One entry per engine, LocalAI's capability map in JSON:

```json
{
  "formatVersion": 1,
  "published": "2026-10-20",
  "engines": [{
    "id": "llama-server",
    "version": "b11669",
    "license": "MIT",
    "serves": ["textllm", "embeddings"],
    "bundles": [
      {"platform": "linux-x64",   "accel": "cpu",    "url": "...", "sha256": "...", "size": 17825792},
      {"platform": "linux-x64",   "accel": "vulkan", "url": "...", "sha256": "...", "size": 30510080, "requires": {"vulkan": "1.2"}},
      {"platform": "linux-x64",   "accel": "cuda12", "url": "...", "sha256": "...", "size": 173015040, "requires": {"nvidiaDriver": "550"}, "optional": true},
      {"platform": "macos-arm64", "accel": "metal",  "url": "...", "sha256": "...", "size": 12163072, "minOs": "14.0"},
      {"platform": "win-x64",     "accel": "vulkan", "url": "...", "sha256": "...", "size": 32190464}
    ]
  }]
}
```

The framework ships the index it was released with inside its resources, so
a machine that has never been online still knows what exists, and a gallery
mirror (an enterprise that forbids GitHub) is one setting.

### 6.3 The model catalogue

`catalogue.json`, shipped with the framework and refreshed from the same
channel. Curated and short: for each capability, three or four models at
two or three sizes, chosen for licence (Apache-2.0 / MIT / Llama-style with
the terms named), quality per gigabyte and engine support. An entry:

```json
{
  "id": "qwen3-4b-instruct-q4",
  "displayName": "Qwen3 4B Instruct (Q4_K_M)",
  "capability": "textllm",
  "engine": "llama-server",
  "license": "Apache-2.0",
  "features": {"tools": true, "vision": false, "contextWindow": 32768},
  "files": [{"name": "Qwen3-4B-Instruct-Q4_K_M.gguf", "url": "https://huggingface.co/.../resolve/<revision>/...", "sha256": "...", "size": 2497077248}],
  "requires": {"memoryBytes": 4000000000, "preferGpuMemoryBytes": 3200000000},
  "engineArgs": ["--ctx-size", "8192", "--jinja"]
}
```

Hugging Face is the file host (its `resolve/<revision>/<file>` URLs are
stable and its tree API reports the LFS SHA-256 the catalogue pins); the
catalogue is ours. Beyond the catalogue the user may **add a GGUF file**
(copied or referenced in place; the file picker goes through FileLoader's
dialogs as every file the user opens does) or **a Hugging Face repository
id**, for which the runtime lists the GGUF quantisations through the HF
API and lets them pick one — with "no hash on record" said plainly.

### 6.4 Downloads

One `Downloader` for engines and models: `UltraNet_HttpDownloadFile` to
`downloads/<sha256 prefix>.part`, resumed with a `Range` header when a
`.part` exists and the server answers 206 **[UltraNet's resume support
unverified — it has the header, the status handling needs checking]**,
progress reported per file and per job, cancellable, verified with
`UltraCrypt_Hash` before the rename into place (the OCR rule), the model
directory written last so an interrupted install leaves no half model that
`IsInstalled` could believe. Concurrency: one job at a time by default; a
job queue the dashboard shows. A disk cap with "Remove" suggestions when the
next install would exceed it. A download never runs on a UI thread; the
runtime owns one worker and posts progress through callbacks.

### 6.5 Hardware fit

Read once per process through `UltraCanvasHardwareInfo` (CPU, GPU, NPU,
Memory queries only — the fast ones):

- **Engine bundle choice**: macOS → the platform bundle (Metal); Linux and
  Windows → `vulkan` when a GPU with a Vulkan driver is present, else
  `cpu`; the x86-64 psABI level decides whether the AVX2 / AVX-512 CPU
  backends ggml loads at run time will work, and a CPU below level 2 is
  told so. `cuda` / `rocm` appear as "Faster on your NVIDIA/AMD GPU
  (150 MB more)" and are chosen only by the user. The chosen accelerator
  is a setting the user can override (Auto / CPU / Vulkan / CUDA / ROCm).
- **Model fit**: a model is *recommended* when its files fit in 70 % of the
  GPU's dedicated memory (full offload), *runs on the CPU* when they fit in
  70 % of total RAM, and *too large* otherwise. The number of GPU layers is
  derived the same way (`-ngl` all, partial, or 0); llama.cpp also accepts
  an automatic setting.
- **Thread count**: physical cores from the CPU topology, leaving two for
  the desktop.

### 6.6 Process lifecycle

`LocalRuntime::Ensure(modelId)` is the one call every adapter makes. It:

1. Finds the catalogue entry, its engine, and the bundle for this machine.
2. Installs what is missing — or, when the download policy is *ask*, fails
   with `ErrorCode::ModelNotFound` and a `NeedsInstall` detail the
   framework dialog (§8) turns into a prompt; the adapter never shows UI.
3. Finds a running engine for the model in `runtime.json` whose pid is
   alive and whose `/health` answers, and returns its base URL and key.
4. Otherwise starts one: `llama-server --host 127.0.0.1 --port <free port>
   --api-key-file runtime.key --model <path> [--mmproj …] -ngl <n>
   --threads <n> --sleep-idle-seconds 600 --jinja`, argv through
   `LaunchWatchedProgram`, stdout and stderr to
   `logs/<engine>-<model>.log` with rotation, and waits on `/health` with a
   timeout that scales with model size (loading 8 GB from a cold disk takes
   a minute). Records pid, port, model, start time in `runtime.json` under
   a lock.
5. Returns `{baseUrl, apiKey, modelAlias}`.

Idle handling is two-stage: the engine sleeps on its own (`is_sleeping` in
`/props`), and the supervisor stops an engine nobody has used for the
configured time (default 15 minutes), the last thing in `runtime.json`
deciding. An engine that exits is restarted on the next `Ensure`; one that
crashes twice in a row for the same model is marked and the error says so
rather than looping.

**Router mode** (`llama-server` started without a model, `--models-dir`,
`--models-max`, automatic loading per request) is the simpler topology:
one process for all GGUF models, switching on the `model` field. It is the
intended phase-2 shape **[the router API — `/models/load`, `/models/unload`,
the architecture object on `/models` — is documented but was not exercised
here]**; phase 1 runs one process per model, which the same `Ensure`
contract covers.

`whisper-server` and `sd-server` follow the same lifecycle with their own
command lines; Piper runs per request (`piper --model voice.onnx
--output_file -`, text on stdin, WAV on stdout) through `RunProcessCaptured`
because a synthesis is a second and the program is a few hundred
milliseconds to start.

### 6.7 Sharing between applications

Phase 1: the first application to need a model starts its engine; others
find it in `runtime.json`; the engine is detached from its starter so the
starter's exit does not end it; each application's `LocalRuntime` touches
the record's *last used* on every request; the idle sweep runs in whichever
application calls `Ensure` next, and on every application start a sweep
removes records whose pid is gone. The failure mode is an orphaned engine
that outlives every application until a sweep — bounded by
`--sleep-idle-seconds` releasing the memory in the meantime.

Phase 2: `UltraAIHost`, a command-line program built from the module (as
`UltraMessageCli` is built from UltraMessage), started detached on first
need, owning the engines, serving `GET /ultraai/status`, `POST
/ultraai/ensure` and the download queue on its own loopback port, and
exiting when nothing has used it for the idle time. `LocalRuntime` becomes
its client; `runtime.json` becomes the host's. On ULTRA OS the desktop can
start it at login, the way it records the clipboard for everyone.

### 6.8 Security

- Engines listen on `127.0.0.1` only, with a per-installation key that
  every request carries; `/health` is the one open endpoint.
- Every download is TLS-verified, size-bounded by the index, hash-checked
  before use; the gallery index is signed; a catalogue entry without a hash
  (a user-supplied repository) is shown as such.
- An engine runs with the user's privileges and reads only what it is
  given; the model file is the attack surface, and a separate process is
  the mitigation this proposal offers. Running the engine under a
  restricted token (Windows) or a seccomp profile (Linux) is listed as a
  later hardening, not a phase-1 item.
- Model and engine licences are shown before install and recorded in
  `model.json`; the catalogue carries no model whose licence forbids
  redistribution of the weights.

---

## 7. The API

### 7.1 `UltraAILocalRuntime.h`

A sketch of the public surface; names follow the module's conventions.

```cpp
namespace UltraAI {

struct EngineBundleInfo {
    std::string engineId;          // "llama-server", "whisper-server", "sd-server", "piper"
    std::string version;
    std::string platform;          // "linux-x64", "macos-arm64", "win-x64", ...
    std::string accelerator;       // "cpu", "vulkan", "metal", "cuda12", "rocm"
    uint64_t    sizeBytes = 0;
    bool        installed = false;
    bool        recommended = false;   // the bundle Auto would pick
};

enum class ModelFit { Recommended, RunsOnCpu, TooLarge, Unknown };

struct LocalModelInfo {
    std::string id;                // catalogue id, or "file:<path>" / "hf:<repo>:<file>"
    std::string displayName;
    std::string capability;        // "textllm", "embeddings", "speechtotext", ...
    std::string engineId;
    std::string license;
    uint64_t    sizeBytes = 0;
    ModelFit    fit = ModelFit::Unknown;
    bool        installed = false;
    bool        running = false;
};

struct InstallProgress {
    std::string jobId;
    std::string what;              // "Qwen3 4B Instruct", "llama-server (Vulkan)"
    uint64_t    bytesDone = 0, bytesTotal = 0;
    enum class State { Queued, Downloading, Verifying, Unpacking, Done, Failed, Cancelled } state;
    std::string error;
};

struct EngineEndpoint {
    std::string baseUrl;           // "http://127.0.0.1:41235"
    std::string apiKey;
    std::string modelAlias;        // what to send as "model"
};

enum class DownloadPolicy { Never, Ask, Automatic };

class LocalRuntime {
public:
    static LocalRuntime& Instance();

    // Inventory (no network, no I/O beyond the local folders).
    std::vector<LocalModelInfo>   ListModels(const std::string& capability = "");
    std::vector<EngineBundleInfo> ListEngines();
    LocalModelInfo                Recommend(const std::string& capability);

    // Installs run on the runtime's worker; progress on that thread.
    std::string InstallModel(const std::string& modelId,
                             std::function<void(const InstallProgress&)> onProgress);
    std::string InstallEngine(const std::string& engineId, const std::string& accelerator,
                              std::function<void(const InstallProgress&)> onProgress);
    void        Cancel(const std::string& jobId);
    bool        Remove(const std::string& modelId, std::string& outError);

    // The call the adapters make. Blocking: may download (policy permitting)
    // and start a process. Never shows UI.
    Error       Ensure(const std::string& modelId, EngineEndpoint& outEndpoint);

    // Lifecycle and settings.
    void        StopIdle();                     // the sweep; also run on a timer
    void        StopAll();
    void        SetDownloadPolicy(DownloadPolicy policy);
    void        SetPreferredAccelerator(const std::string& accelerator);   // "" = Auto
    void        SetGalleryUrl(const std::string& url);                     // "" = default
    std::string RootFolder() const;             // UTF-8, for the settings page
};

} // namespace UltraAI
```

### 7.2 The `local` provider

`UltraAI/adapters/local/`: five thin adapters registered under provider id
`local`. Each takes `defaultModel` as a catalogue id (or a `file:` / `hf:`
reference), calls `LocalRuntime::Ensure` on first use (not at construction,
like `qwen`), and hands the request to the existing OpenAI-wire
implementation with the endpoint's base URL and key — `ITextLLM` and
`IEmbeddings` reuse the `openai` adapter's request and stream code (lifted
into `_shared` where it is not already); `ISpeechToText` posts multipart
audio to `whisper-server`'s `/inference`; `IImageGen` posts to `sd-server`'s
`/v1/images/generations`; `ITextToSpeech` spawns Piper. `GetCapabilities`
lists the installed models for the capability with `runsLocally = true`,
and the uninstalled catalogue entries too, so a dialog can offer "Qwen3 4B
(not installed, 2.5 GB)" without a second API. An empty `defaultModel`
means `Recommend(capability)`.

Routing: `local` goes first in `KnownLocalProviders` for the five
capabilities, ahead of `llama-cpp`, and `IsLocalProvider("local")` is true.
A `local` adapter whose runtime cannot serve (policy *Never* and nothing
installed) fails to construct, and the factories' constructibility fallback
walks on to `llama-cpp`, `qwen` and the rest exactly as today.

In the dashboard, an endpoint with provider `local` gets a model picker fed
from `ListModels` instead of a free-text model field, and no key row.

---

## 8. The user experience

**The first time.** A user opens Texter, selects a paragraph, picks
*Translate*. Nothing local is installed. Instead of the mock's placeholder,
the framework's *Get a local model* dialog opens (a `UltraCanvasModalDialog`
in `UltraAI/ui/`, built from catalogue elements — the gauge element in
`LinearBar` mode for progress, as the catalogue rule requires; applications
never build their own):

> **Translate needs a language model.**
> ○ **Qwen3 4B Instruct** — recommended for this computer, 2.5 GB, Apache-2.0. Runs on your GPU.
> ○ Qwen3 1.7B Instruct — 1.1 GB, smaller and faster.
> ○ Use a cloud service instead… (only when one is set up)
> *Also downloads the inference engine (30 MB).* ▢ Download future models without asking
> [Download and continue] [Not now]

Download and continue runs the install in the background with the progress
in the dialog; the application's request proceeds when `Ensure` returns.
*Not now* fails the request with `ModelNotFound` and the application says
so once. The policy checkbox sets `DownloadPolicy::Automatic`, after which
`Ensure` installs silently and the application shows only the progress its
own status line chooses to show (the `onProgress` callback is public).

**The dashboard's Local models page** (`Apps/UltraAIApp`, a new settings
page beside *Local & cloud*), LocalAI's two views in one list:

| Column | Content |
|---|---|
| Model | name, capability chip, licence |
| Size | on disk, or to download |
| Fits | *GPU* / *CPU* / *Too large for this computer* (from `ModelFit`) |
| State | Installed · Running (with memory in use) · Not installed · a progress bar with Cancel · Failed, with the reason and Retry · Update available |
| Action | Install / Remove / Stop |

Above it: the engine in use ("llama-server b11669, Vulkan on AMD Radeon
780M — change"), disk used of the cap, the storage folder with *Open*, the
download policy and the idle time. Below it: *Add a model file…* (FileLoader
dialog) and *Add from Hugging Face…*. The *Local & cloud* page's note is
corrected to say what is installed rather than that everything runs
locally.

**Every state the user can be in has a sentence**: no network ("The
catalogue is from 2026-10-01; downloads need a connection"), no space, a
hash mismatch ("The downloaded file is not the one the catalogue expects;
it was discarded"), an engine that will not start (the log's last lines in
the dialog, with *Copy*), a GPU driver too old for Vulkan ("Running on the
CPU; update the graphics driver for GPU speed").

---

## 9. Plan

| Phase | Delivers | Rough size |
|---|---|---|
| **0 — Gallery and catalogue formats; the engine builds** | `index.json` / `catalogue.json` schemas and the signing key; a `.github/workflows/ai-engines.yml` that repackages llama.cpp's and stable-diffusion.cpp's cpu / vulkan / macOS archives under our names, builds whisper.cpp and Piper, hashes, signs and notarizes, and publishes a release of a new `ultracanvas-ai-engines` repository; `Docs/Dependencies.md`, `master_dependencies.yaml` and `THIRD_PARTY_LICENSES.md` rows | CI and scripts, ~600 lines; no framework code |
| **1 — The runtime and chat** | `UltraAI/runtime/`: storage, downloader (resume, hash, cancel), inventory, hardware fit, `Ensure` for `llama-server` with `runtime.json` sharing and the idle sweep; the `local` provider for `ITextLLM` and `IEmbeddings` over the shared OpenAI-wire code; routing changes; tests with a scripted transport and a fake engine executable built by the test target (answers `/health` and `/v1/chat/completions`), so no test downloads anything | ~3 000 lines |
| **2 — The user interface** | *Get a local model* dialog in `UltraAI/ui/`; the dashboard's *Local models* page; endpoint editing for `local`; the first-run flow in Texter's translate and the dashboard's chat as the two reference integrations; `Docs/Modules/UltraAI/LocalRuntime.md` | ~2 000 lines |
| **3 — Speech, images, voices** | `whisper-server` STT, `sd-server` image generation, Piper TTS, vision through `--mmproj`; catalogue entries for each | ~1 500 lines |
| **4 — The host** | `UltraAIHost`, router mode for GGUF models, the desktop starting it on ULTRA OS, restricted-token / seccomp hardening | ~1 500 lines |

Phase 1 is usable on its own: a developer build with `defaultModel =
"qwen3-4b-instruct-q4"` and `DownloadPolicy::Automatic` gets a chat answer
from a cold machine with no UI at all. Phase 2 is what the user sees.

---

## 10. Risks and open questions

- **Engine cadence.** Pinning one llama.cpp build per framework release is
  the stable choice and leaves a new model architecture unsupported until
  the next release; an engine channel updated between releases is possible
  (the index is signed and versioned) and is a policy to decide, not a
  code change.
- **macOS signing of downloaded binaries** (§5) is asserted from Apple's
  documented rules, not tested here. The engine bundle workflow needs a
  signing identity in CI — the same one `package-macos.sh` uses.
- **MSIX**: a packaged app's folder is read-only and it starts in
  System32; `LocalDataFolderRoot` is writable and a full-trust package may
  spawn processes, and `Docs/UltraCanvas/UltraCanvasWindowsMSIX.md` must
  be re-read before the supervisor assumes a working directory.
- **UltraNet resume** (§6.4) must be verified or added; without it a
  2.5 GB download restarts from zero after a dropped connection.
- **Disk**: the gallery must say sizes before the user agrees, and the
  default cap (20 GB?) is a product decision.
- **Memory pressure**: two applications wanting two different 4 GB models
  on an 8 GB machine; the runtime should refuse the second with a clear
  error rather than swap the machine to a halt — the `requires` fields
  make that decision possible.
- **Catalogue curation** is ongoing work: who adds models, under which
  licence review, how often; the file should name an owner.
- **Piper's GPL** means our gallery redistributes a GPL program; the
  obligation is to offer its source, which the gallery entry's `source`
  URL and `THIRD_PARTY_LICENSES.md` satisfy. Kokoro over ONNX Runtime
  (Apache-2.0) is the alternative if that is unwelcome — it needs ONNX
  Runtime as another engine.
- **Router mode** (§6.6) and whisper-server's `/load` are documented
  upstream and not exercised here.

---

## 11. Sources

- LocalAI backends and galleries:
  [localai.io/docs/backends](https://localai.io/docs/backends/) (mirrored in
  `docs/content/features/backends.md` of
  [mudler/LocalAI](https://github.com/mudler/LocalAI)) and its
  [`backend/index.yaml`](https://github.com/mudler/LocalAI/blob/master/backend/index.yaml),
  read on this date.
- llama.cpp server: [`tools/server/README.md`](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md)
  (router mode, `--api-key-file`, `--sleep-idle-seconds`, `--mmproj`,
  `--jinja`, `/v1/embeddings`, reranking); release assets of nightly
  [b11669](https://github.com/ggml-org/llama.cpp/releases/tag/b11669);
  release v0.7.0 of 2026-10-15 carries source only.
- whisper.cpp: release [v1.9.5](https://github.com/ggml-org/whisper.cpp/releases/tag/v1.9.5)
  (source only) and
  [`examples/server/README.md`](https://github.com/ggml-org/whisper.cpp/blob/master/examples/server/README.md).
- stable-diffusion.cpp: release
  [master-975-95b3090](https://github.com/leejet/stable-diffusion.cpp/releases/tag/master-975-95b3090)
  and [`examples/server/README.md`](https://github.com/leejet/stable-diffusion.cpp/blob/master/examples/server/README.md).
- Piper: [OHF-Voice/piper1-gpl](https://github.com/OHF-Voice/piper1-gpl)
  (GPL-3.0, release v1.8.0: wheels and source).
- In this repository: `UltraAI/include/UltraAIRouting.h`,
  `UltraAI/adapters/{llamacpp,qwen,openai,_shared}/`,
  `Apps/UltraAIApp/UltraAIEndpoints.h`,
  `Apps/UltraAIApp/UltraAISettingsWindow.cpp`,
  `UltraCanvas/Plugins/Documents/OCR/include/UltraCanvasOCRPlugin.h`,
  `UltraCanvas/include/{UltraCanvasHardwareInfo,UltraCanvasDataFolder,UltraCanvasArchiveRun,UltraCanvasUtils}.h`,
  `UltraCanvas/include/UltraNet/UltraNetHttp.h`, `Docs/Dependencies.md`.
