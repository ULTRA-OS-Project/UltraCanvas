# UltraAI Local Runtime — models on this computer, nothing to install

**Version:** 0.4.0
**Last Modified:** 2026-11-15

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
| **Engine gallery** | An index of prebuilt engine bundles: one per (engine, platform, accelerator), with URL, SHA-256 and size. The copy shipped with the framework is llama.cpp's release b11669 for Linux, macOS and Windows on x64 and arm64, CPU and Vulkan (Metal on macOS), hashed on 2026-10-20, and whisper.cpp's server v1.9.0 for Linux, macOS and Windows on x64 and arm64 - upstream's CPU builds for Linux and Windows x64, and the rest (Metal on Apple silicon, CPU on Intel Macs, Windows on ARM, Vulkan) built here (*Engines built here*), hashed on 2026-11-13. There is no default gallery address: `RefreshGallery()` fetches a model server's when the settings name one (`galleryUrl`), and does nothing otherwise. | `LocalRuntimeBuiltinData.cpp`, `<root>/gallery/index.json` |
| **Model catalogue** | The short curated list: models with capability, engine, licence, files, memory needs and features. Shipped with the framework; a model server's replaces it. It is what `Recommend()` and the topics choose from. A model whose engine has no build for this platform (and is not installed or overridden) is left out of `ListModels`. | `LocalRuntimeBuiltinData.cpp`, `<root>/gallery/catalogue.json` |
| **Catalogue sources** | The wider list, downloaded as LocalAI downloads its galleries: each source is an index at an address - a LocalAI model gallery, or a `catalogue.json` of ours - whose models the two engines can run are offered beside the curated ones. LocalAI's own gallery is the source a new installation starts with. See *Catalogue sources*. | `RefreshCatalogueSources`, `ListCatalogueSources`, `<root>/catalogues/<source id>.json` |
| **Hugging Face** | A live search of huggingface.co's repositories and the list of one repository's runnable files (GGUFs, whisper.cpp models) with size, hash, quantisation and fit. See *Hugging Face*. | `SearchHuggingFace`, `ListHuggingFaceFiles` |
| **User models** | A GGUF the user has (`AddModelFile`, used in place) or a Hugging Face file (`AddHuggingFaceModel`, downloaded; with the further parts of a split GGUF and a vision projector when asked); for `"speechtotext"`, a whisper.cpp ggml `.bin`, served by `whisper-server`. | `<root>/user-models.json` |
| **Installs** | Background jobs on the runtime's worker: download to a `.part` file (resumed when one exists), verify the SHA-256 through UltraCrypt, move into place; an engine bundle is unpacked through VirtualFS and gets a `bundle.json`; a model gets its `model.json` last, so a half-installed model never counts as installed. | `InstallModel`, `InstallEngine`, `Cancel`, `ListJobs` |
| **Supervision** | `Ensure(modelId)` starts the model's engine on a free loopback port (`llama-server` with `--api-key-file`), waits for `/health`, and records it in `runtime.json` — shared between processes under a lock file — so a second application finds the running engine instead of starting its own. Idle engines are stopped by `StopIdle()` (run by every `Ensure`). A model whose engine died twice in a row is marked and not retried until it is stopped or reinstalled. | `Ensure`, `Touch`, `StopIdle`, `StopModel`, `StopAll`, `ListRunning` |
| **Hardware fit** | From `UltraCanvasHardwareInfo` (GPUs with dedicated memory, total memory, CPU cores): the accelerator Auto picks (Metal on macOS; Vulkan where a GPU is present on Linux and Windows; CPU otherwise; CUDA and ROCm only when chosen), and each model's fit — *recommended* (fits 70 % of the GPU's memory), *runs on CPU* (fits 70 % of system memory) or *too large*; a model whose engine has CPU builds only for this platform (llama-server and whisper-server on an Intel Mac) is at most *runs on CPU*, unless the engine is an override. | `Hardware()`, `Fit()`, `ChooseBundle()`, `Recommend()` |

### Storage

Under the machine-local data root (`UltraCanvasDataFolder.h`: `%LOCALAPPDATA%`
on Windows, `~/Library/Application Support` on macOS, `$XDG_DATA_HOME` or
`~/.local/share` elsewhere), so the gigabytes never roam with a profile:

```
<root>/UltraAI/
├── engines/<engine>/<version>-<platform>-<accelerator>/   an unpacked bundle, with bundle.json
├── models/<model id>/                                      the files, with model.json
├── downloads/*.part                                        in flight; resumed next time
├── gallery/index.json, catalogue.json, topics.json         the copies refreshed from a model server
├── catalogues/<source id>.json                             the models of each catalogue source, as downloaded
├── file-facts.json                                         sizes and hashes asked of the hosts (ProbeModel, installs)
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
| `preferredAccelerator` | `""` (Auto) | `"cpu"`, `"vulkan"`, `"metal"`, `"cuda12"`, `"rocm"` — the bundle to use when the gallery has one for this platform; for every engine without an entry of its own below |
| `engineAccelerators` | `{}` | the accelerator per engine id (`"llama-server"`, `"whisper-server"`). An entry wins over `preferredAccelerator`, and `""` in it is Auto for that engine: one engine's GPU build may suit a computer where another's does not (whisper-server's Linux Vulkan build needs glibc 2.39). `PreferredAccelerator(engineId)` reads what applies; a settings page sets these |
| `galleryUrl` | `""` (none) | a model server's `index.json`; `catalogue.json` and `topics.json` are read from beside it. Empty: the copies shipped with the framework stay in use |
| `catalogueSources` | LocalAI's model gallery | where the wider list of models is downloaded from: `{id, name, url, enabled}` each (see *Catalogue sources*). A settings file written before the sources existed keeps the default; one that lists them, even none, is taken at its word |
| `trustedKeys` | `[]` | Ed25519 public keys (hex) the gallery's documents must be signed with; pinned from `keys.json` on the first refresh (see *Signed galleries*) |
| `idleMinutes` | 15 | stop an engine nobody used for this long (0 stops at the next sweep) |
| `diskCapBytes` | 20 GB | an install that would exceed it fails with `QuotaExceeded` |
| `startTimeoutSeconds` | 0 | how long a start may take; 0 scales it with the model's size (about 100 MB/s plus 20 s, at least 30 s) |

### The engine's command line

For `llama-server`: `--host 127.0.0.1 --port <free> --api-key-file
<root>/runtime.key --model <file> --alias <model id>`, `--mmproj <file>`
when the model carries a vision projector (a file marked `"projector"`, or
named `mmproj*.gguf`), `--embeddings` for an embedding
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
process of the user's can reach it there. Auto picks Metal on Apple
silicon and Vulkan on Windows with a GPU; on Linux the CPU build, as the
Vulkan builds (glibc 2.39) are optional and run only when chosen.

Engines reach their libraries through their own folder (`$ORIGIN` on
Linux, the bundle's layout on macOS and Windows), so a bundle runs from
wherever it was unpacked.

### Using an engine you built yourself

`LocalRuntimeOptions::engineOverrides["llama-server"]`, or the environment
variable `ULTRAAI_LOCAL_ENGINE_LLAMA_SERVER`, names a folder holding (or
the path of) a `llama-server` executable; the runtime uses it instead of a
bundle and downloads no engine. A developer's own CUDA build, or a test's
fake engine. `ULTRAAI_LOCAL_ENGINE_WHISPER_SERVER` does the same for
`whisper-server`.

---

### Engines built here

Where upstream publishes no build, this repository makes one:
`.github/workflows/whisper-server.yml` builds whisper.cpp's server at a
release tag for macOS on Apple silicon (Metal, the library embedded) and on
Intel (CPU), both for macOS 13.3 and later and signed ad hoc; Windows on ARM
(CPU and Vulkan, with clang-cl, as ggml refuses MSVC on ARM); and Vulkan
builds for Windows x64 and Linux x64 and arm64 (glibc 2.39 and the system's
Vulkan loader). Each is linked statically against whisper.cpp and ggml (and
the C runtime on Windows), and each archive holds the executable,
whisper.cpp's `LICENSE` and a `BUILD.txt` naming the commit, the CMake
arguments and the run.

Every build but Windows Vulkan (a hosted Windows runner has no Vulkan
loader) is started with the runtime's own command line and must transcribe
whisper.cpp's `samples/jfk.wav` sent to `/v1/audio/transcriptions` with the
tiny model; Linux Vulkan runs on Mesa's software device. A pull request that
changes the workflow or `scripts/whisper_gallery_entries.py` runs every build
and test without publishing.

To publish: Actions → *whisper-server* → *Run workflow* with the
whisper.cpp tag and a revision (`r1`, then `r2` for every rebuild of the same
tag - a published release is never replaced, because the gallery pins its
hashes). The release `whisper-server-<tag>-<revision>` of this repository,
not marked *latest*, carries the archives, `SHA256SUMS` and
`whisper-server.gallery.json`: the bundle entries to add to the
`whisper-server` engine in `LocalRuntimeBuiltinData.cpp` and to the model
server's seed (`Server/UltraAIModelServer/ultraai_models/seed/index.json`).
The engine's `version` stays the whisper.cpp tag.

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
    "files": [{"name": "Qwen3-4B-Q4_K_M.gguf", "url": "https://huggingface.co/.../resolve/main/...", "sha256": "", "size": 2497000000},
              {"name": "vision.gguf", "url": "https://...", "projector": true}],
    "homepage": "https://huggingface.co/Qwen/Qwen3-4B-GGUF",
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
`Recommend()` for its capability. A file marked `"projector": true` is the
model's vision projector (`--mmproj`); one named `mmproj*.gguf` is that
without saying so. A file's `name` is a plain file name: an install refuses
one with a separator in it, whatever catalogue it came from.

The built-in catalogue (2026-10-21): Qwen3 4B, 1.7B and 8B for chat,
Gemma 3 4B with vision, Qwen3 Embedding 0.6B and Nomic Embed Text v1.5 for
embeddings. Its file names and sizes were checked against the Hugging Face
repositories on that date; the hashes are still read from the tree API at
install time.

---

## Catalogue sources

The curated catalogue is nine models. Everything else comes from where
LocalAI gets it: a **gallery index** downloaded from an address, and the
files themselves from Hugging Face. `LocalRuntimeSettings::catalogueSources`
lists the indexes; a new installation has one, LocalAI's own
(`github:mudler/LocalAI/gallery/index.yaml@master`, about 2300 entries).

```cpp
LocalRuntime& rt = LocalRuntime::Instance();

std::vector<CatalogueSourceStatus> status;
rt.RefreshCatalogueSources(&status);           // blocking: downloads every enabled source
for (const CatalogueSourceStatus& s : status) {
    // s.source.name, s.error, s.models (offered), s.skipped (need another engine), s.fetched
}
for (const LocalModelInfo& m : rt.ListModels()) {
    if (m.source == LocalModelSource::Gallery) { /* m.gallery == "localai", m.id == "localai:<name>" */ }
}

LocalRuntimeSettings settings = rt.GetSettings();
settings.catalogueSources.push_back({"mine", "My list", "https://example.com/catalogue.json", true});
rt.SetSettings(settings);                      // persisted; Refresh downloads it
```

- **An address** is `https://...`, or LocalAI's short form
  `github:owner/repository/path@branch` (raw.githubusercontent.com; branch
  `main` without one). Anything else - `http://`, `file://`, `oci://` - is
  not fetched, and `SetSettings` drops a source with such an address, with
  no usable id, or with an id another source has.
- **The document says what it is.** A JSON object is one of our
  `catalogue.json` documents; a list is a LocalAI model gallery, in YAML or
  JSON, with each entry's `url` / `files` / `overrides` at the top
  (`gallery/index.yaml`) or under `model` (`gallery/models/index.yaml`).
- **A LocalAI entry becomes a model when one of the two engines can run
  it**: every file a GGUF and the backend `llama-cpp` (or none, with a
  language-model template) makes a `llama-server` model - embeddings when
  the entry says so (`embeddings: true`, the use case, the tag), with its
  `pooling:` option and a context of at most 8192; a `whisper` entry with
  one ggml `.bin` a `whisper-server` model. The model file named by
  `overrides.parameters.model` comes first, with every part of a split
  GGUF; the file named by `overrides.mmproj` is marked as the projector; a
  draft model or another extra is not downloaded. Everything else - piper,
  diffusers, vLLM, the picture and sound models that are GGUF files too, a
  voice-activity model, a reranker - is counted in `skipped`: it needs an
  engine this runtime does not have.
- **A source is somebody else's list**, and may set only what a model
  entry needs. Its files must resolve to `https://` (`huggingface://` and
  `hf://` become Hugging Face resolve URLs) and carry plain names inside
  the model's own folder; the SHA-256 it gives is verified as any other;
  and the engine arguments of a third party's `catalogue.json` are cut down
  to `--ctx-size`, `--pooling`, `--embeddings`, `--jinja`, `--parallel`,
  `--batch-size`, `--ubatch-size` and `--reasoning-format` - an engine also
  takes arguments that name files to read and write.
- **Its models are never picked on their own.** `Recommend()`, the topics
  and `NewModels()` look at the curated catalogue; a source's model is
  used once the user has installed it. An installed one is `Remove`d like a
  catalogue model, not forgotten.
- **The copy is kept**, as our model JSON, in `<root>/catalogues/<id>.json`
  and read at start. A refresh that fails - the address unreachable, the
  document unusable - leaves the copy in use and is reported for that
  source alone. Turning a source off takes its models out of `ListModels`
  at once and keeps the copy; a new address under the same id discards it.
- **An installed model outlives its listing.** `ListModels` and
  `FindModel` read the `model.json` of an installed model no catalogue
  lists any more - its source dropped it, or was turned off or removed -
  so it can still be used and removed.
- **Sizes.** A LocalAI gallery states hashes but no sizes. `ProbeModel(id,
  model)` asks the file's host (Hugging Face's tree API, for the folder the
  file is in) for size and hash and remembers the answer in
  `file-facts.json`, so the size and the fit are known before anything is
  downloaded; an install asks the same way. Until then the model's fit is
  `Unknown`.

The gallery's YAML is read by the runtime itself
(`local_detail::YamlToJson`): the part of the language a generated index
uses - block mappings and sequences, plain, quoted and block scalars, flow
`[ ]` and `{ }`, comments. An anchor, an alias, a tag or a merge key is
refused, so a gallery that starts using them fails its refresh rather than
being misread. It agrees with PyYAML on LocalAI's gallery, model library
and backend indexes as of 2026-11-15 (apart from YAML 1.1's `yes` and
octal numbers, which stay text here).

## Hugging Face

```cpp
std::vector<HuggingFaceRepo> repos;
rt.SearchHuggingFace("qwen3 4b", /*ggufOnly*/ true, 50, repos);    // most downloaded first; "" lists the top
// repos[i].id, .downloads, .likes, .license, .pipelineTag, .gated

std::vector<HuggingFaceFile> files;
HuggingFaceRepo repo;
rt.ListHuggingFaceFiles("Qwen/Qwen3-4B-GGUF", files, &repo);
// files[i].name, .sizeBytes, .sha256, .quantization ("Q4_K_M"), .fit, .capability,
// .projector (an mmproj), .parts (the further parts of a split GGUF)

HuggingFaceModelRequest request;
request.repo       = repo.id;
request.file       = files[0].name;
request.extraFiles = files[0].parts;
request.capability = files[0].capability;
std::string id;
rt.AddHuggingFaceModel(request, id);            // "hf:<repo>:<file>", then InstallModel(id, ...)
```

All three block on the network and are for a worker thread. The search is
`https://huggingface.co/api/models?sort=downloads&direction=-1&full=true&limit=<n>`
with `filter=gguf` and `search=<words>` (`full=true` adds the date of the
last change and whether a repository is gated); the files are the repository's
tree (`/api/models/<repo>/tree/main?recursive=true`) - GGUFs and
`ggml-*.bin` whisper.cpp models, a split GGUF as one entry with its parts,
projectors marked and listed last - with the capability guessed from the
repository's pipeline tag and the name (`.bin`: speech to text; a
`sentence-similarity` or `feature-extraction` repository, or "embed" in the
name: embeddings; else chat). A repository reported as `gated` asks for an
agreement on huggingface.co first, and its files are refused to a download
that is not signed in. A repository that does not exist answers 401 there,
as a private one does; both come back as `ModelNotFound`. Nothing is sent
to Hugging Face but the search words and the repository's name. Checked
against the live service on 2026-11-15.

---

## Signed galleries

A model server ([`Server/UltraAIModelServer`](../../../Server/UltraAIModelServer/README.md))
signs `index.json`, `catalogue.json` and `topics.json` with its Ed25519
key: a detached signature over the exact bytes served, 128 lower-case hex
characters (the convention of UltraStore's `catalog.json.sig`), in the
document's `X-UltraAI-Signature` header and as `<document>.sig` beside it.
`keys.json` beside the index lists the keys a client may trust:

```json
{"formatVersion": 1, "algorithm": "ed25519", "keys": ["3b6a27bc..."]}
```

`RefreshGallery()`:

- with `trustedKeys` empty, reads `keys.json`; when there is one, the index
  must verify against it and its keys are pinned into the settings (trust on
  first use, over TLS). A gallery without `keys.json` - a plain mirror -
  stays unsigned and works as before;
- with `trustedKeys` set, the index, and the catalogue and the topics when
  they are there, must each carry a signature by one of those keys: an
  unsigned or wrongly signed one fails the refresh with `ProviderError` /
  `providerCode` `"bad-signature"`, and nothing is replaced. What `keys.json`
  says that day does not matter - a server that changes its key is refused
  until the user resets the pin (or the server announced the next key
  beforehand and signed with it, `ULTRAAI_MODELS_NEXT_PUBLIC_KEYS`);
- a new `galleryUrl` set through `SetSettings` without new `trustedKeys`
  drops the old pins: they belong to the old server.

An administrator who wants no first-use step hands users the key (UltraAI
ServerAdmin shows it on its Connection page) to put in `trustedKeys`.

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

// The engine of the model the user selected, and its accelerator.
EngineBundleInfo bundle;
if (rt.ChooseBundle(model.engineId, bundle)) { /* bundle.accelerator, bundle.installed, bundle.sizeBytes */ }
LocalRuntimeSettings s = rt.GetSettings();
s.engineAccelerators[model.engineId] = "cpu";   // "" = Auto for this engine
rt.SetSettings(s);
```

The dashboard's page shows the engine of the selected model in its engine
line and its *Acceleration* choice - a speech model's is `whisper-server`,
a chat model's `llama-server` - and disables the choice for an engine with
one build for this computer: there is nothing to choose, and the engine is
downloaded with the first of its models as every engine is.

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
offered); the catalogue sources (the YAML reader, a LocalAI gallery's
entries as models and the ones left out, what a third party's list may
set, a refresh through a transport that answers by address, the copy kept
between runs, a source turned off, an installed model its source dropped,
`ProbeModel`); Hugging Face's search and file lists; and the accelerator
per engine. Nothing
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
- `whisper-server` comes from whisper.cpp's own release archives (CPU
  builds for Linux and Windows x64) and from this repository's release
  `whisper-server-v1.9.0-r1` for the rest (*Engines built here*, above).
  The Linux Vulkan builds need glibc 2.39 and are optional. No CUDA or
  ROCm build yet. The whisper models' hashes are taken from Hugging Face
  at install time; their sizes are whisper.cpp's published figures.
- One `llama-server` process per model; the router mode (one process for
  every GGUF) and the `UltraAIHost` supervisor are phase 2. An engine
  started by an application that exits stays until an idle sweep from any
  application stops it.
- The engine bundles are llama.cpp's own release archives, pinned by hash.
  There is no published gallery of UltraCanvas's own - the repository the
  default address once named was never made - so the engine index changes
  with a release of the framework, or with a model server the settings
  name. On macOS the
  upstream archives are built for 13.3 and later, and a downloaded
  executable runs as it is (UltraNet sets no quarantine attribute).
- The catalogue's hashes are taken from Hugging Face at install time until
  they are pinned; the file names and sizes were checked against the
  repositories on 2026-10-21.
- The built-in gallery (llama.cpp's release archives) is not signed; a
  model server's gallery is (see *Signed galleries*). A catalogue source is
  not signed either: its files are verified against the hashes it gives,
  so it is as trustworthy as its publisher and the address it is fetched
  from over TLS.
- A catalogue source's models have no memory figures: the fit is 1.2 x the
  files' size once that is known (`ProbeModel`). A LocalAI entry's prompt
  template and sampling parameters are not carried over - `llama-server`
  uses the chat template inside the GGUF (`--jinja`).
- A gated Hugging Face repository cannot be downloaded: the runtime holds
  no Hugging Face token.
- The *Get a local model* dialog is the module's (`UltraAI/ui/`,
  `UltraAILocalModelDialog.h`, library `UltraAI_UI`): an application links
  `UltraAI_UI` beside `UltraAI_Core`, tests an error with
  `UltraAILocalModelDialog::NeedsInstall(error)` and calls `Show(window,
  serviceName, capability, preferredModelId, onReady)`; the dashboard's Chat
  and Texter's Translate Selection do. The *Local models* settings page is
  the dashboard's (`Apps/UltraAIApp/UltraAILocalModelsPage`).
