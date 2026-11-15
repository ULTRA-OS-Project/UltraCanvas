// UltraAI/runtime/include/UltraAILocalRuntime.h
// The local runtime: models on the user's own machine with nothing for them
// to install. It owns an engine gallery (prebuilt inference engines per
// platform and accelerator, hash-verified), a model catalogue (curated
// models with sizes, hashes, licences and hardware needs, plus any GGUF
// the user brings) and a supervisor (start an engine on loopback for a
// model, keep it healthy, share it between applications, stop it when
// idle). The `local` provider (UltraAILocal.h) serves the capability
// interfaces through it; applications reach the inventory and the install
// jobs through this header for their own settings pages and dialogs.
//
// Design: Docs/Research/UltraAILocalRuntimeDesignProposal.md.
// Documentation: Docs/Modules/UltraAI/LocalRuntime.md.
//
// Storage, under the machine-local data root (UltraCanvasDataFolder.h):
//   <root>/UltraAI/engines/<engine>/<version>-<platform>-<accelerator>/
//   <root>/UltraAI/models/<catalogue id>/   (model.json + the files)
//   <root>/UltraAI/downloads/*.part
//   <root>/UltraAI/runtime.json, runtime.key, logs/
//
// Threading: every method is safe to call from any thread. Installs run on
// the runtime's own worker; their progress callbacks fire on that worker.
// Ensure() blocks the calling thread (a download, policy permitting, and a
// process start) and is meant for adapters and worker threads, never for a
// UI thread.
//
// Topics: what a model is used for - chat, code, translation, summaries,
// pictures, search. The model server (Server/UltraAIModelServer) publishes
// topics.json beside the gallery index, naming the models it offers for
// each topic and its default; the user keeps that default or picks another
// model per topic, and says whether new models the server publishes are
// downloaded on their own (TopicPreferences). An application asks
// ModelForTopic("translation"), or hands the `local` provider
// providerOptions["topic"].
//
// Catalogue sources: where the list of models comes from, beyond the short
// curated catalogue shipped with the framework. As LocalAI's galleries: each
// source is an index at a URL - a LocalAI model gallery (its
// gallery/index.yaml is the default source) or one of our catalogue.json
// documents - that RefreshCatalogueSources() downloads and keeps. A model
// of a source is offered, installed and run like any other; it is never
// picked on its own (Recommend, the topics): the user chooses it. Hugging
// Face is searched live (SearchHuggingFace, ListHuggingFaceFiles) and a
// file found there is added with AddHuggingFaceModel.
//
// Nothing here draws. The framework's "Get a local model" dialog and the
// dashboard's Local models and Model topics pages are built on top of it.
// Version: 0.5.0 - catalogue sources (LocalAI galleries), Hugging Face
//                  search, the accelerator per engine, no default gallery
//                  address, installed models a catalogue no longer lists
// Version: 0.4.0 - speech models (whisper-server); ListModels leaves out
//                  models whose engine has no build for this platform
// Version: 0.3.0 - signed galleries: trustedKeys, keys.json pinned on first
//                  use, every refreshed document checked (RefreshGallery)
// Version: 0.2.0 - topics: the server's topics.json, the user's model per
//                  topic, new models (ListTopics, ModelForTopic,
//                  TopicPreferences, NewModels, ApplyTopicUpdates)
// Version: 0.1.0
// Last Modified: 2026-11-15
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraAI {

class ITransport;

// Whether Ensure() may download what a model needs. The framework dialog
// turns Ask into a prompt; Never leaves installs to the settings page.
enum class DownloadPolicy { Never, Ask, Automatic };

// How a model suits this computer (LocalRuntime::Fit).
enum class ModelFit {
    Recommended,   // fits the GPU's dedicated memory: full offload
    RunsOnCpu,     // fits system memory; runs on the CPU (or partly offloaded)
    TooLarge,      // does not fit; refused by Ensure
    Unknown        // no requirements on record (a user-supplied file)
};

// What the runtime knows about this machine. Detected once through
// UltraCanvasHardwareInfo when the framework is in the build; a test or an
// application may hand in its own.
struct HardwareProfile {
    std::string platform;               // "linux-x64", "macos-arm64", "win-x64", ...
    uint64_t    memoryBytes = 0;        // total system memory
    uint64_t    availableMemoryBytes = 0;
    int         physicalCores = 0;
    int         logicalCores = 0;
    int         x86MicroarchitectureLevel = 0;   // 0 off x86 or unknown

    struct Gpu {
        std::string vendor;             // "NVIDIA", "AMD", "Intel", "Apple"
        std::string model;
        uint64_t    videoMemoryBytes = 0;   // dedicated
        bool        discrete = false;
    };
    std::vector<Gpu> gpus;

    // The accelerators this machine can use, in preference order, derived
    // from the GPUs: "metal" on macOS, "vulkan" where a GPU is present on
    // Linux and Windows, then "cpu". "cuda12" / "rocm" are listed after
    // "vulkan" for an NVIDIA / AMD GPU so that an optional bundle can be
    // offered; they are never chosen automatically.
    std::vector<std::string> accelerators;

    // The largest dedicated GPU memory among the GPUs; 0 without one.
    uint64_t LargestGpuMemoryBytes() const;

    // This platform's id, from compile-time macros.
    static std::string CurrentPlatform();
    // Detect this machine. Falls back to the C++ standard library's view
    // (thread count, no GPU) where the framework's hardware inventory is
    // not in the build.
    static HardwareProfile Detect();
    // Fill `accelerators` from the GPUs and the platform. Called by Detect;
    // public for profiles built by hand.
    void DeriveAccelerators();
};

// One downloadable engine build.
struct EngineBundleInfo {
    std::string engineId;               // "llama-server", "whisper-server", ...
    std::string version;
    std::string platform;               // "linux-x64", ...
    std::string accelerator;            // "cpu", "vulkan", "metal", "cuda12", "rocm"
    std::string url;
    std::string sha256;                 // lowercase hex; empty = not on record
    uint64_t    sizeBytes = 0;
    bool        optional = false;       // offered, never chosen automatically
    std::string minOs;                  // "14.0" on macOS; empty = any
    std::map<std::string, std::string> requirements;   // "nvidiaDriver": "550", ...
    bool        installed = false;
    bool        recommended = false;    // what Auto picks on this machine
    std::string installedPath;          // the unpacked bundle's folder when installed
};

// One engine of the gallery.
struct EngineInfo {
    std::string id;
    std::string version;
    std::string license;
    std::string executable;             // file name of the server inside a bundle, without extension
    std::vector<std::string> serves;    // capability names: "textllm", "embeddings", ...
    std::vector<EngineBundleInfo> bundles;
    std::vector<std::string> defaultArgs;   // added to every start of this engine
};

// One file of a model.
struct LocalModelFile {
    std::string name;                   // file name inside the model folder
    std::string url;
    std::string sha256;                 // empty = not on record (verified against the host's hash when it reports one)
    uint64_t    sizeBytes = 0;          // 0 = not on record (asked of the host: ProbeModel, an install)
    // A vision projector, handed to the engine beside the model (--mmproj).
    // A file whose name starts with "mmproj" is one without saying so.
    bool        projector = false;
};

// Where a model came from: the curated catalogue, a file the user has, a
// Hugging Face file the user added, or a catalogue source (a gallery).
enum class LocalModelSource { Catalogue, File, HuggingFace, Gallery };

// One model: a catalogue entry, an installed model, or a file the user
// brought.
struct LocalModelInfo {
    // Catalogue id, "file:<path>", "hf:<repo>:<file>" or, for a model of a
    // catalogue source, "<source id>:<its name there>".
    std::string id;
    std::string displayName;
    std::string description;
    std::string capability;             // "textllm", "embeddings", "speechtotext", "texttospeech", "imagegen"
    std::string engineId;
    std::string license;
    LocalModelSource source = LocalModelSource::Catalogue;
    std::string gallery;                // the catalogue source of a Gallery model ("localai"); "" otherwise
    std::string homepage;               // a page about the model (its repository), when the source names one
    std::vector<LocalModelFile> files;
    uint64_t    sizeBytes = 0;          // sum of the files
    uint64_t    memoryBytes = 0;        // what it needs in system memory; 0 = unknown
    uint64_t    gpuMemoryBytes = 0;     // what full GPU offload needs; 0 = unknown
    int32_t     contextWindow = 0;
    int32_t     embeddingDimensions = 0;
    bool        supportsTools = false;
    bool        supportsVision = false;
    std::vector<std::string> engineArgs;    // added to the engine's command line for this model
    std::vector<std::string> tags;

    // State on this machine.
    ModelFit    fit = ModelFit::Unknown;
    bool        installed = false;
    bool        running = false;
    std::string installedPath;          // the model folder when installed
    std::string primaryFile;            // the file handed to the engine (--model), when installed
};

// Progress of an install job.
struct InstallProgress {
    std::string jobId;
    std::string what;                   // "Qwen3 4B Instruct", "llama-server (Vulkan)"
    uint64_t    bytesDone = 0;
    uint64_t    bytesTotal = 0;         // 0 when unknown
    enum class State { Queued, Downloading, Verifying, Unpacking, Done, Failed, Cancelled };
    State       state = State::Queued;
    std::string error;                  // when Failed
};
using InstallProgressCallback = std::function<void(const InstallProgress&)>;

// A running engine, as an adapter needs it.
struct EngineEndpoint {
    std::string baseUrl;                // "http://127.0.0.1:41235" — no trailing slash, no /v1
    std::string apiKey;                 // sent as a bearer token
    std::string modelAlias;             // what to send as "model"
    std::string engineId;
    uint64_t    processId = 0;
};

// One running engine, for a settings page.
struct RunningEngineInfo {
    std::string engineId;
    std::string modelId;
    std::string baseUrl;
    uint64_t    processId = 0;
    int64_t     startedUnix = 0;
    int64_t     lastUsedUnix = 0;
    bool        ownedByThisProcess = false;
};

// A place the list of models is downloaded from (RefreshCatalogueSources).
struct CatalogueSource {
    // Lower-case letters, digits and '-'; it starts the ids of the source's
    // models ("localai:qwen3-4b"), so it is not "file" or "hf".
    std::string id;
    std::string name;                   // "LocalAI model gallery"
    // The index: an https:// address, or LocalAI's short form
    // "github:owner/repository/path@branch". A LocalAI model gallery (YAML)
    // or a catalogue.json of ours; the document says which.
    std::string url;
    bool        enabled = true;         // off: kept in the list, not downloaded, its models not offered
};

// The sources a new installation starts with: LocalAI's model gallery.
std::vector<CatalogueSource> DefaultCatalogueSources();

// A catalogue source and the copy of it in use (ListCatalogueSources), or
// what a refresh did to it (RefreshCatalogueSources).
struct CatalogueSourceStatus {
    CatalogueSource source;
    Error       error;                  // a refresh: IsOk() when the source was read
    int         models = 0;             // entries the local engines can run: offered
    int         skipped = 0;            // entries left out: they need an engine this runtime does not have
    std::string fetched;                // "2026-11-15", when the copy in use was downloaded; "" = never
};

// One repository found on Hugging Face (SearchHuggingFace).
struct HuggingFaceRepo {
    std::string id;                     // "Qwen/Qwen3-4B-GGUF"
    uint64_t    downloads = 0;
    uint64_t    likes = 0;
    std::string pipelineTag;            // "text-generation", "sentence-similarity", "automatic-speech-recognition", ...
    std::string license;                // from its "license:" tag; "" when it names none
    std::string lastModified;           // "2026-05-01"; "" when the answer carries no date
    // The publisher asks for an agreement on huggingface.co first: its files
    // are refused to a download that is not signed in.
    bool        gated = false;
    std::vector<std::string> tags;
};

// One model file of a repository that a local engine can run
// (ListHuggingFaceFiles): a GGUF, or a whisper.cpp ggml .bin.
struct HuggingFaceFile {
    std::string name;                   // its path inside the repository
    uint64_t    sizeBytes = 0;          // with its further parts, for a split model
    std::string sha256;                 // "" when the host reports none
    std::string quantization;           // "Q4_K_M"; "" when the name does not say
    std::string capability;             // what it would be added as: "textllm", "embeddings" or "speechtotext"
    bool        projector = false;      // a vision projector (mmproj): it goes with a model, it is not one
    std::vector<std::string> parts;     // the further parts of a split GGUF ("...-00002-of-00003.gguf")
    ModelFit    fit = ModelFit::Unknown;
};

// What AddHuggingFaceModel registers.
struct HuggingFaceModelRequest {
    std::string repo;                   // "organisation/repository"
    std::string file;                   // the model file inside it (the first part of a split one)
    std::vector<std::string> extraFiles;    // its further parts
    std::string projectorFile;          // a vision projector of the same repository; "" = none
    std::string capability;             // "textllm", "embeddings" or "speechtotext"
    std::string displayName;            // "" = the file's name
    std::string revision;               // "" = main
    std::string license;                // "" = "see the repository"
};

// The settings. Persisted to <root>/UltraAI/runtime-settings.json by the
// runtime that owns them.
struct LocalRuntimeSettings {
    DownloadPolicy policy = DownloadPolicy::Ask;
    // The accelerator of every engine that has no entry of its own below:
    // "" = Auto; "cpu", "vulkan", "cuda12", ...
    std::string    preferredAccelerator;
    // The accelerator per engine id ("llama-server", "whisper-server"); an
    // entry wins over preferredAccelerator, and "" in it means Auto for that
    // engine. One engine's GPU build may suit a computer where another's
    // does not, so a settings page sets these.
    std::map<std::string, std::string> engineAccelerators;
    // The address of an engine gallery (a model server's index.json, with
    // its catalogue.json and topics.json beside it); "" = none: the engine
    // index, the curated catalogue and the topics shipped with the
    // framework stay in use and RefreshGallery has nothing to fetch.
    std::string    galleryUrl;
    // Where the wider list of models comes from (RefreshCatalogueSources).
    std::vector<CatalogueSource> catalogueSources = DefaultCatalogueSources();
    // The Ed25519 public keys (64 hex characters each) the gallery's
    // documents must be signed with. Empty: the first refresh from a server
    // that publishes keys.json pins its keys here (trust on first use, over
    // TLS); a gallery that publishes none stays unsigned. Not empty: a
    // refresh whose index, catalogue or topics carry no signature by one of
    // these keys is refused and changes nothing. Changing galleryUrl
    // through SetSettings without changing these clears them.
    std::vector<std::string> trustedKeys;
    int            idleMinutes = 15;            // stop an engine nobody used for this long
    uint64_t       diskCapBytes = 20ull * 1024 * 1024 * 1024;
    int            startTimeoutSeconds = 0;     // 0 = scaled with model size
};

// One model a topic offers, as the server lists it.
struct TopicModelRef {
    std::string modelId;
    bool        isDefault = false;      // the server's choice for the topic
};

// What a model is used for. From the server's topics.json (beside the
// gallery index), or the copy shipped with the framework.
struct ModelTopic {
    std::string id;                     // "chat", "code", "translation", "summary", "vision", "search", ...
    std::string name;                   // "General chat"
    std::string description;
    std::string capability;             // "textllm", "embeddings", ...
    std::vector<TopicModelRef> models;  // the server's order, best first

    // The model the server names as the default ("" when it offers none).
    std::string ServerDefault() const;
};

// What happens when the server publishes a model the user has not seen.
enum class NewModelPolicy {
    Ignore,      // nothing; the Local models page still lists it
    Notify,      // NewModels() reports it until MarkModelsSeen
    Automatic    // also downloaded, when it is the default of a topic the user follows
};

// The user's choice for one topic.
struct TopicChoice {
    // The model used for the topic; "" follows the server's default (and
    // moves with it when the server changes it).
    std::string modelId;
    // While following the server: download the server's default for this
    // topic when it is not installed (ApplyTopicUpdates). Off, the default
    // is still used once the user installs it.
    bool autoInstall = true;
};

// The user's topic settings. Persisted to <root>/UltraAI/topic-preferences.json.
struct TopicPreferences {
    std::map<std::string, TopicChoice> choices;     // by topic id; absent = follow, autoInstall
    NewModelPolicy newModels = NewModelPolicy::Notify;
    // Catalogue ids the user has been told of. Filled with the whole
    // catalogue the first time, so only models published later count as new.
    std::vector<std::string> seenModels;
};

// Downloads a URL to a local file. The production fetch is
// UltraNet_HttpDownloadFile; a test injects a copy. `resume` asks for the
// bytes after the file's current size. `onProgress` reports (done, total;
// total 0 when unknown) and returns false to cancel.
using FetchFileFn = std::function<Error(
    const std::string& url, const std::string& destPathUtf8, bool resume,
    const std::function<bool(uint64_t, uint64_t)>& onProgress)>;

// How a runtime is built. Instance() uses the defaults; a test builds its
// own with a scratch root, a fixed hardware profile, a scripted transport,
// a copy fetcher and its own gallery and catalogue.
struct LocalRuntimeOptions {
    std::string rootFolder;                      // "" = <LocalDataFolderRoot>/UltraAI
    std::shared_ptr<HardwareProfile> hardware;   // null = Detect()
    std::shared_ptr<ITransport> transport;       // null = UltraNetTransport (health, gallery, host hashes)
    FetchFileFn fetch;                           // null = UltraNet download
    std::string galleryJson;                     // "" = the built-in index
    std::string catalogueJson;                   // "" = the built-in catalogue
    std::string topicsJson;                      // "" = the built-in topics
    // Engines already on this machine, by engine id: the folder holding the
    // executable, or the executable itself. A developer's own llama.cpp
    // build, or a test's fake engine. The environment variable
    // ULTRAAI_LOCAL_ENGINE_<ID> (id upper-cased, '-' as '_') does the same
    // for Instance().
    std::map<std::string, std::string> engineOverrides;
};

class LocalRuntime {
public:
    // The process-wide runtime, on the defaults above.
    static LocalRuntime& Instance();

    explicit LocalRuntime(LocalRuntimeOptions options);
    ~LocalRuntime();
    LocalRuntime(const LocalRuntime&) = delete;
    LocalRuntime& operator=(const LocalRuntime&) = delete;

    // ----- Inventory (local folders only; no network) -----

    // The catalogue's models, those of the catalogue sources and the
    // user-added ones, with their state on this machine; `capability`
    // filters ("" = all). A catalogue or source model whose engine has no
    // build for this platform (whisper-server on macOS) is left out unless
    // it is installed. An installed model that no catalogue lists any more
    // (a source dropped it, or was turned off) is still listed, from the
    // model.json in its folder.
    std::vector<LocalModelInfo> ListModels(const std::string& capability = "") const;
    // One model by id; false when unknown.
    bool FindModel(const std::string& modelId, LocalModelInfo& out) const;
    // The engines of the gallery, each bundle marked installed /
    // recommended for this machine.
    std::vector<EngineInfo> ListEngines() const;
    // The best installed-or-installable model for a capability on this
    // machine: installed ones first, then the catalogue's recommended ones
    // in catalogue order. Empty id when none fits.
    LocalModelInfo Recommend(const std::string& capability) const;
    // How a model suits this machine.
    ModelFit Fit(const LocalModelInfo& model) const;
    // The bundle an engine uses on this machine - its accelerator from the
    // settings (engineAccelerators, else preferredAccelerator), else Auto's
    // pick; false when the gallery has none for this platform.
    bool ChooseBundle(const std::string& engineId, EngineBundleInfo& out) const;
    // The accelerator the settings name for an engine ("" = Auto).
    std::string PreferredAccelerator(const std::string& engineId) const;
    // This machine, as the runtime sees it.
    const HardwareProfile& Hardware() const;
    // The engines running now (from the shared record, pids checked).
    std::vector<RunningEngineInfo> ListRunning() const;
    // Bytes used under the root by engines and models.
    uint64_t DiskUsedBytes() const;
    // The root folder, UTF-8, for a settings page's "Open folder".
    std::string RootFolder() const;

    // ----- User-added models -----

    // Register a GGUF (or other model file) the user has, in place: no copy.
    // The id comes back as "file:<path>". `capability` is "textllm" or
    // "embeddings" (a GGUF, served by llama-server) or "speechtotext" (a
    // whisper.cpp ggml .bin, served by whisper-server).
    Error AddModelFile(const std::string& pathUtf8, const std::string& capability,
                       const std::string& displayName, std::string& outModelId);
    // Register a Hugging Face file for download: repo "org/name", the
    // file's path inside it. The id comes back as "hf:<repo>:<file>". Its
    // hash and size are taken from the host when it reports them.
    Error AddHuggingFaceModel(const std::string& repo, const std::string& file,
                              const std::string& capability,
                              const std::string& displayName,
                              std::string& outModelId);
    // The same with the further parts of a split GGUF, a vision projector
    // and a revision.
    Error AddHuggingFaceModel(const HuggingFaceModelRequest& request, std::string& outModelId);
    // Forget a user-added model (an installed one is removed first).
    Error ForgetModel(const std::string& modelId);

    // ----- Hugging Face (network; blocking, never on a UI thread) -----

    // Search the repositories of huggingface.co, most downloaded first.
    // `ggufOnly` keeps those tagged as holding GGUF files, which is what
    // llama-server runs; an empty query lists the most downloaded. `limit`
    // is 1..100.
    Error SearchHuggingFace(const std::string& query, bool ggufOnly, int limit,
                            std::vector<HuggingFaceRepo>& out);
    // The files of one repository that a local engine can run, each with
    // its size, hash, quantisation, how it fits this computer and what it
    // would be added as; `outRepo` (may be null) takes the repository's own
    // facts (licence, gated). Empty `out` with no error: the repository
    // holds no GGUF and no whisper.cpp model.
    Error ListHuggingFaceFiles(const std::string& repo, std::vector<HuggingFaceFile>& out,
                               HuggingFaceRepo* outRepo = nullptr);

    // ----- Installs (background jobs) -----

    // Queue a model install (its engine too when missing). Returns the job
    // id; progress fires on the worker. An installed model completes at
    // once with State::Done.
    std::string InstallModel(const std::string& modelId, InstallProgressCallback onProgress);
    // Queue an engine bundle install; "" accelerator = Auto's choice.
    std::string InstallEngine(const std::string& engineId, const std::string& accelerator,
                              InstallProgressCallback onProgress);
    // Cancel a queued or running job.
    void Cancel(const std::string& jobId);
    // Jobs not yet finished, oldest first.
    std::vector<InstallProgress> ListJobs() const;
    // Remove an installed model's files (a running engine for it is
    // stopped first).
    Error RemoveModel(const std::string& modelId);
    // Remove an installed engine bundle (its engines are stopped first).
    Error RemoveEngine(const std::string& engineId, const std::string& accelerator);
    // Block until no job is left. For tests and command-line tools.
    void WaitForJobs();

    // ----- The call the adapters make -----

    // Make `modelId` servable and return its engine's endpoint: installs
    // what is missing when the policy allows (Automatic; or Ask with
    // `allowDownload`), starts or finds the engine, waits for /health.
    // Blocking; never shows UI. Errors: ModelNotFound with providerCode
    // "needs-install" when a download is needed and not allowed (the
    // framework dialog's cue), "too-large" when the model does not fit,
    // ProviderError when the engine would not start (the log's last lines
    // in the message), NetworkError when a download failed.
    Error Ensure(const std::string& modelId, EngineEndpoint& outEndpoint,
                 bool allowDownload = false);
    // Record that an endpoint was used, so the idle sweep leaves it alone.
    void Touch(const EngineEndpoint& endpoint);

    // ----- Lifecycle -----

    // Stop engines nobody used for the idle time, and forget records whose
    // process is gone. Ensure() runs it too.
    void StopIdle();
    // Stop every engine in the shared record.
    void StopAll();
    // Stop the engine serving one model, if any.
    void StopModel(const std::string& modelId);

    // ----- Settings and gallery -----

    LocalRuntimeSettings GetSettings() const;
    // Persisted. A catalogue source without a usable id or address is
    // dropped; a source turned off or removed takes its models out of the
    // inventory at once (an installed one stays listed).
    void SetSettings(const LocalRuntimeSettings& settings);
    // Fetch the gallery index, the catalogue and the topics from the gallery
    // URL and replace the copies in use when they verify. Without a gallery
    // URL in the settings there is nothing to fetch and it returns ok. Each
    // document's signature comes in its X-UltraAI-Signature header or as
    // <document>.sig beside it (128 hex characters, Ed25519 over the bytes
    // served). With trustedKeys set, an unsigned or wrongly signed document
    // fails the whole refresh with ProviderError / providerCode
    // "bad-signature" and nothing is replaced; without, keys.json beside the
    // index is pinned when the index verifies against it. Blocking.
    Error RefreshGallery();
    // When the in-use gallery was published ("" for the built-in one's
    // date when never refreshed).
    std::string GalleryPublished() const;

    // ----- Catalogue sources -----

    // Download every enabled catalogue source and replace its models with
    // what it lists now. One source failing leaves its earlier copy in use
    // and does not stop the others; the result is ok when every enabled
    // source was read, else the first failure. `outStatus` (may be null)
    // takes one entry per enabled source. Blocking.
    Error RefreshCatalogueSources(std::vector<CatalogueSourceStatus>* outStatus = nullptr);
    // The sources of the settings, each with the copy of it in use: how
    // many models it offers and when it was downloaded. No network.
    std::vector<CatalogueSourceStatus> ListCatalogueSources() const;
    // Ask a model's host for what its catalogue entry lacks - the files'
    // sizes, and their hashes - and remember it, so the model's size and
    // fit are known before it is installed (a LocalAI gallery lists no
    // sizes). `out` takes the model as ListModels would give it. Does
    // nothing when everything is on record. Blocking.
    Error ProbeModel(const std::string& modelId, LocalModelInfo& out);

    // The name of the capability a model id serves, from the catalogue or
    // the user's registration; "" when unknown.
    std::string CapabilityOf(const std::string& modelId) const;

    // ----- Topics -----

    // The topics, in the server's order. RefreshGallery() fetches
    // topics.json from beside the index; until then the built-in copy.
    std::vector<ModelTopic> ListTopics() const;
    bool FindTopic(const std::string& topicId, ModelTopic& out) const;
    TopicPreferences GetTopicPreferences() const;
    void SetTopicPreferences(const TopicPreferences& preferences);   // persisted
    // Pick the model for one topic ("" = follow the server). Persisted.
    void SetTopicModel(const std::string& topicId, const std::string& modelId);
    // The model a topic uses on this computer: the user's choice when it is
    // known and fits; else the server's default when it fits; else an
    // installed model the topic offers; else the first offered that fits;
    // else Recommend(capability). "" for an unknown topic with no model of
    // its capability.
    std::string ModelForTopic(const std::string& topicId) const;
    // Catalogue models the user has not been told of (NewModelPolicy
    // Notify / Automatic), in catalogue order. Installed ones are left out.
    std::vector<LocalModelInfo> NewModels() const;
    // Remember these ids as seen; MarkModelsSeen({}) marks the whole catalogue.
    void MarkModelsSeen(const std::vector<std::string>& modelIds);
    // Queue the installs the topic settings call for: for every topic the
    // user follows with autoInstall on, the model ModelForTopic picks when
    // it is not installed and fits this computer; with the Automatic policy
    // the new models that are a topic's default as well. Does nothing under
    // DownloadPolicy::Never. Returns the job ids (InstallModel).
    std::vector<std::string> ApplyTopicUpdates(InstallProgressCallback onProgress);

    class Impl;
private:
    std::unique_ptr<Impl> impl_;
};

// Platform ids and accelerator names as strings, for settings pages.
const char* ToString(DownloadPolicy policy);
bool ParseDownloadPolicy(const std::string& text, DownloadPolicy& out);
const char* ToString(ModelFit fit);
const char* ToString(InstallProgress::State state);
const char* ToString(NewModelPolicy policy);
bool ParseNewModelPolicy(const std::string& text, NewModelPolicy& out);

} // namespace UltraAI
