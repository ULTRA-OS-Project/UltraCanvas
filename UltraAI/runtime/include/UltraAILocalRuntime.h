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
// Nothing here draws. The framework's "Get a local model" dialog and the
// dashboard's Local models page are built on top of it.
// Version: 0.1.0
// Last Modified: 2026-10-20
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
    uint64_t    sizeBytes = 0;
};

// Where a model came from.
enum class LocalModelSource { Catalogue, File, HuggingFace };

// One model: a catalogue entry, an installed model, or a file the user
// brought.
struct LocalModelInfo {
    std::string id;                     // catalogue id, "file:<path>" or "hf:<repo>:<file>"
    std::string displayName;
    std::string description;
    std::string capability;             // "textllm", "embeddings", "speechtotext", "texttospeech", "imagegen"
    std::string engineId;
    std::string license;
    LocalModelSource source = LocalModelSource::Catalogue;
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

// The settings. Persisted to <root>/UltraAI/runtime-settings.json by the
// runtime that owns them.
struct LocalRuntimeSettings {
    DownloadPolicy policy = DownloadPolicy::Ask;
    std::string    preferredAccelerator;        // "" = Auto; "cpu", "vulkan", "cuda12", ...
    std::string    galleryUrl;                  // "" = the built-in default
    int            idleMinutes = 15;            // stop an engine nobody used for this long
    uint64_t       diskCapBytes = 20ull * 1024 * 1024 * 1024;
    int            startTimeoutSeconds = 0;     // 0 = scaled with model size
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

    // The catalogue's models and the user-added ones, with their state on
    // this machine; `capability` filters ("" = all).
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
    // The bundle Auto picks for an engine on this machine; false when the
    // gallery has none for this platform.
    bool ChooseBundle(const std::string& engineId, EngineBundleInfo& out) const;
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
    // "embeddings"; `engineId` defaults to the engine serving it.
    Error AddModelFile(const std::string& pathUtf8, const std::string& capability,
                       const std::string& displayName, std::string& outModelId);
    // Register a Hugging Face file for download: repo "org/name", file
    // name inside it, optional revision ("" = main). The id comes back as
    // "hf:<repo>:<file>". Its hash is taken from the host when it reports
    // one.
    Error AddHuggingFaceModel(const std::string& repo, const std::string& file,
                              const std::string& capability,
                              const std::string& displayName,
                              std::string& outModelId);
    // Forget a user-added model (an installed one is removed first).
    Error ForgetModel(const std::string& modelId);

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
    void SetSettings(const LocalRuntimeSettings& settings);   // persisted
    // Fetch the gallery index and the catalogue from the gallery URL and
    // replace the built-in copies when they verify. Blocking.
    Error RefreshGallery();
    // When the in-use gallery was published ("" for the built-in one's
    // date when never refreshed).
    std::string GalleryPublished() const;

    // The name of the capability a model id serves, from the catalogue or
    // the user's registration; "" when unknown.
    std::string CapabilityOf(const std::string& modelId) const;

    class Impl;
private:
    std::unique_ptr<Impl> impl_;
};

// Platform ids and accelerator names as strings, for settings pages.
const char* ToString(DownloadPolicy policy);
bool ParseDownloadPolicy(const std::string& text, DownloadPolicy& out);
const char* ToString(ModelFit fit);
const char* ToString(InstallProgress::State state);

} // namespace UltraAI
