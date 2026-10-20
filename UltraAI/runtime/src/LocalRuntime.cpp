// UltraAI/runtime/src/LocalRuntime.cpp
// The facade: inventory, install jobs, engine supervision and the shared
// record of running engines (runtime.json), composed from the parts in
// LocalRuntimeInternal.h.
// Version: 0.1.0
// Last Modified: 2026-10-20
// Author: UltraAI Module

#include "UltraAILocalRuntime.h"
#include "LocalRuntimeInternal.h"

#include "UltraAITransport.h"
#ifdef ULTRAAI_HAS_ULTRANET
#include "UltraAIUltraNetTransport.h"
#include <UltraNet/UltraNetHttp.h>
#endif
#include "UltraCanvasPathUtf8.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>

namespace UltraAI {

namespace fs = std::filesystem;
using nlohmann::json;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;
using namespace local_detail;

namespace {

// How long a start may take before it is given up, scaled with the model:
// a cold disk reads about 100 MB/s, so 8 GB is 80 s; never under 30 s.
int StartTimeoutSeconds(const LocalModelInfo& model, const LocalRuntimeSettings& settings) {
    if (settings.startTimeoutSeconds > 0) return settings.startTimeoutSeconds;
    const int fromSize = static_cast<int>(model.sizeBytes / (100ull * 1024 * 1024));
    return std::max(30, fromSize + 20);
}

std::string UpperId(const std::string& id) {
    std::string out;
    for (unsigned char c : id) {
        out += c == '-' ? '_' : static_cast<char>(std::toupper(c));
    }
    return out;
}

Error MakeError(ErrorCode code, const std::string& message, const std::string& providerCode = {}) {
    Error e;
    e.code         = code;
    e.message      = message;
    e.providerCode = providerCode;
    return e;
}

// One entry of runtime.json.
struct RunningRecord {
    std::string engineId;
    std::string modelId;
    std::string baseUrl;
    std::string apiKeyFile;
    uint64_t    pid = 0;
    uint64_t    ownerPid = 0;
    int64_t     startedUnix = 0;
    int64_t     lastUsedUnix = 0;
    int         failures = 0;       // consecutive failed starts of this model
};

std::vector<RunningRecord> ParseRecords(const std::string& text) {
    std::vector<RunningRecord> out;
    json doc = json::parse(text, nullptr, false);
    if (!doc.is_object()) return out;
    auto list = doc.find("engines");
    if (list == doc.end() || !list->is_array()) return out;
    for (const auto& e : *list) {
        if (!e.is_object()) continue;
        RunningRecord r;
        r.engineId     = e.value("engineId", "");
        r.modelId      = e.value("modelId", "");
        r.baseUrl      = e.value("baseUrl", "");
        r.apiKeyFile   = e.value("apiKeyFile", "");
        r.pid          = e.value("pid", 0ull);
        r.ownerPid     = e.value("ownerPid", 0ull);
        r.startedUnix  = e.value("startedUnix", 0ll);
        r.lastUsedUnix = e.value("lastUsedUnix", 0ll);
        r.failures     = e.value("failures", 0);
        out.push_back(std::move(r));
    }
    return out;
}

std::string SerializeRecords(const std::vector<RunningRecord>& records) {
    json list = json::array();
    for (const auto& r : records) {
        json e;
        e["engineId"]     = r.engineId;
        e["modelId"]      = r.modelId;
        e["baseUrl"]      = r.baseUrl;
        e["apiKeyFile"]   = r.apiKeyFile;
        e["pid"]          = r.pid;
        e["ownerPid"]     = r.ownerPid;
        e["startedUnix"]  = r.startedUnix;
        e["lastUsedUnix"] = r.lastUsedUnix;
        e["failures"]     = r.failures;
        list.push_back(std::move(e));
    }
    json doc;
    doc["formatVersion"] = 1;
    doc["engines"]       = std::move(list);
    return doc.dump(2);
}

#ifdef ULTRAAI_HAS_ULTRANET
Error UltraNetFetch(const std::string& url, const std::string& destPathUtf8, bool resume,
                    const std::function<bool(uint64_t, uint64_t)>& onProgress) {
    // A file transfer: streamed to disk, no whole-transfer timeout (a stall
    // timeout instead), resumed from the file's size when asked.
    UltraNetHttpRequest request;
    request.url    = url;
    request.method = UltraNetHttpMethod::Get;
    request.options.outputFilePath     = destPathUtf8;
    request.options.resumeOutputFile   = resume;
    request.options.httpsOnlyRedirects = true;
    std::atomic<bool> cancelled{false};
    request.onDownloadProgress = [&](int64_t now, int64_t total) {
        if (onProgress && !onProgress(static_cast<uint64_t>(std::max<int64_t>(now, 0)),
                                      static_cast<uint64_t>(std::max<int64_t>(total, 0)))) {
            cancelled = true;
        }
    };
    request.onHeaders = [&](const UltraNetResponse&) { return !cancelled.load(); };
    UltraNetResponse response;
    const UltraNetResult r = UltraNet_HttpRequest(request, response);
    if (cancelled) return MakeError(ErrorCode::Cancelled, "download cancelled");
    if (!r.success) {
        return MakeError(ErrorCode::NetworkError,
                         "download of " + url + " failed: " + r.message +
                             (r.httpStatus ? " (HTTP " + std::to_string(r.httpStatus) + ")" : ""));
    }
    if (response.statusCode >= 400) {
        return MakeError(ErrorCode::NetworkError,
                         "download of " + url + " failed with HTTP " + std::to_string(response.statusCode));
    }
    return {};
}
#endif

struct Job {
    InstallProgress progress;
    InstallProgressCallback onProgress;
    std::atomic<bool> cancel{false};
    enum class Kind { Model, Engine } kind = Kind::Model;
    std::string targetId;
    std::string accelerator;
};

} // namespace

// ===== Impl =====

class LocalRuntime::Impl {
public:
    explicit Impl(LocalRuntimeOptions options);
    ~Impl();

    LocalRuntimeOptions options_;
    Paths paths_;
    HardwareProfile hardware_;
    std::shared_ptr<ITransport> transport_;
    FetchFileFn fetch_;
    std::string storageError_;

    mutable std::mutex mutex_;          // guards everything below
    Gallery gallery_;
    Catalogue catalogue_;
    std::vector<LocalModelInfo> userModels_;
    LocalRuntimeSettings settings_;
    std::map<std::string, std::string> engineOverrides_;
    std::map<std::string, int64_t> lastTouchWritten_;   // baseUrl -> when runtime.json last took a touch

    // Jobs.
    std::deque<std::shared_ptr<Job>> queue_;
    std::map<std::string, std::shared_ptr<Job>> jobs_;
    std::condition_variable jobsCv_;
    std::thread worker_;
    bool stopping_ = false;
    int nextJobId_ = 1;
    std::mutex installMutex_;           // one install at a time, whoever runs it

    // ----- inventory -----
    std::vector<LocalModelInfo> AllModelsLocked() const;
    bool FindModelLocked(const std::string& modelId, LocalModelInfo& out) const;
    void DecorateModelLocked(LocalModelInfo& model) const;
    bool FindEngineLocked(const std::string& engineId, EngineInfo& out) const;
    void DecorateEngineLocked(EngineInfo& engine) const;
    // Where an installed engine's executable is: an override, or an
    // installed bundle (the chosen one, else any for this platform).
    bool InstalledEngineExecutableLocked(const EngineInfo& engine, std::string& outExe,
                                         std::string& outAccelerator) const;
    fs::path ModelFolder(const std::string& modelId) const;
    fs::path BundleFolder(const EngineBundleInfo& bundle) const;
    bool IsModelInstalled(const LocalModelInfo& model, std::string& outPrimary) const;

    // ----- installs -----
    Error DownloadVerified(const std::string& url, const fs::path& dest, const std::string& sha256,
                           uint64_t expectedSize, const std::shared_ptr<Job>& job,
                           uint64_t doneBefore, uint64_t totalAll);
    Error DoInstallModel(const std::string& modelId, const std::shared_ptr<Job>& job);
    Error DoInstallEngine(const std::string& engineId, const std::string& accelerator,
                          const std::shared_ptr<Job>& job);
    Error ResolveHostHash(LocalModelFile& file);
    void Report(const std::shared_ptr<Job>& job);
    void WorkerLoop();
    std::string Enqueue(std::shared_ptr<Job> job);

    // ----- supervision -----
    std::vector<RunningRecord> ReadRecords() const;
    bool WriteRecords(const std::vector<RunningRecord>& records) const;
    bool Healthy(const std::string& baseUrl, int timeoutMs) const;
    std::string ApiKey() const;      // read or create runtime.key
    Error StartEngineLocked(const EngineInfo& engine, const std::string& exe,
                            const std::string& accelerator, const LocalModelInfo& model,
                            RunningRecord& outRecord);
    void SweepRecords(std::vector<RunningRecord>& records, bool stopIdle);
    void StopRecord(const RunningRecord& record);

    void LoadState();
    void SaveUserModels();
    void SaveSettings();
};

LocalRuntime::Impl::Impl(LocalRuntimeOptions options) : options_(std::move(options)) {
    paths_ = Paths::For(options_.rootFolder.empty() ? Paths::DefaultRoot()
                                                    : PathFromUtf8(options_.rootFolder));
    paths_.EnsureFolders(storageError_);
    hardware_ = options_.hardware ? *options_.hardware : HardwareProfile::Detect();
    if (hardware_.accelerators.empty()) hardware_.DeriveAccelerators();
    transport_ = options_.transport;
#ifdef ULTRAAI_HAS_ULTRANET
    if (!transport_) transport_ = std::make_shared<UltraNetTransport>();
    fetch_ = options_.fetch ? options_.fetch : FetchFileFn(&UltraNetFetch);
#else
    fetch_ = options_.fetch;
#endif
    engineOverrides_ = options_.engineOverrides;
    LoadState();
    worker_ = std::thread([this] { WorkerLoop(); });
}

LocalRuntime::Impl::~Impl() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        for (auto& job : queue_) job->cancel = true;
        for (auto& kv : jobs_) kv.second->cancel = true;
    }
    jobsCv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void LocalRuntime::Impl::LoadState() {
    std::string error;
    // The gallery: the refreshed copy when it parses, else the built-in.
    std::string text;
    bool haveGallery = false;
    if (!options_.galleryJson.empty()) {
        haveGallery = ParseGallery(options_.galleryJson, gallery_, error);
    } else if (ReadWholeFile(paths_.galleryJson, text)) {
        haveGallery = ParseGallery(text, gallery_, error);
    }
    if (!haveGallery) ParseGallery(BuiltinGalleryJson(), gallery_, error);

    bool haveCatalogue = false;
    if (!options_.catalogueJson.empty()) {
        haveCatalogue = ParseCatalogue(options_.catalogueJson, catalogue_, error);
    } else if (ReadWholeFile(paths_.catalogueJson, text)) {
        haveCatalogue = ParseCatalogue(text, catalogue_, error);
    }
    if (!haveCatalogue) ParseCatalogue(BuiltinCatalogueJson(), catalogue_, error);

    if (ReadWholeFile(paths_.userModelsJson, text)) ParseModels(text, userModels_, error);
    if (ReadWholeFile(paths_.settingsJson, text)) ParseSettings(text, settings_);

    // Engines the environment points at: ULTRAAI_LOCAL_ENGINE_LLAMA_SERVER.
    for (const EngineInfo& e : gallery_.engines) {
        if (engineOverrides_.count(e.id)) continue;
        const std::string value = UltraCanvas::GetEnvUtf8(("ULTRAAI_LOCAL_ENGINE_" + UpperId(e.id)).c_str());
        if (!value.empty()) engineOverrides_[e.id] = value;
    }
}

void LocalRuntime::Impl::SaveUserModels() {
    std::string error;
    WriteFileAtomic(paths_.userModelsJson, SerializeModels(userModels_), error);
}

void LocalRuntime::Impl::SaveSettings() {
    std::string error;
    WriteFileAtomic(paths_.settingsJson, SerializeSettings(settings_), error);
}

// ===== Inventory =====

fs::path LocalRuntime::Impl::ModelFolder(const std::string& modelId) const {
    return paths_.models / PathFromUtf8(ModelFolderName(modelId));
}

fs::path LocalRuntime::Impl::BundleFolder(const EngineBundleInfo& bundle) const {
    return paths_.engines / PathFromUtf8(bundle.engineId) / PathFromUtf8(BundleFolderName(bundle));
}

bool LocalRuntime::Impl::IsModelInstalled(const LocalModelInfo& model, std::string& outPrimary) const {
    std::error_code ec;
    if (model.source == LocalModelSource::File) {
        // In place: the file the user named.
        if (model.files.empty()) return false;
        const fs::path p = PathFromUtf8(model.files.front().name);
        if (!fs::is_regular_file(p, ec)) return false;
        outPrimary = PathToUtf8(p);
        return true;
    }
    const fs::path folder = ModelFolder(model.id);
    if (!fs::is_regular_file(folder / "model.json", ec)) return false;
    outPrimary.clear();
    for (const auto& f : model.files) {
        if (!fs::is_regular_file(folder / PathFromUtf8(f.name), ec)) return false;
        if (outPrimary.empty() && f.name.rfind("mmproj", 0) != 0) {
            outPrimary = PathToUtf8(folder / PathFromUtf8(f.name));
        }
    }
    return !outPrimary.empty();
}

void LocalRuntime::Impl::DecorateModelLocked(LocalModelInfo& model) const {
    model.fit = ComputeFit(model, hardware_);
    std::string primary;
    model.installed = IsModelInstalled(model, primary);
    model.running   = false;
    if (model.installed) {
        model.primaryFile   = primary;
        model.installedPath = model.source == LocalModelSource::File
                                  ? PathToUtf8(PathFromUtf8(primary).parent_path())
                                  : PathToUtf8(ModelFolder(model.id));
    } else {
        model.primaryFile.clear();
        model.installedPath.clear();
    }
}

std::vector<LocalModelInfo> LocalRuntime::Impl::AllModelsLocked() const {
    std::vector<LocalModelInfo> out = catalogue_.models;
    for (const auto& m : userModels_) {
        if (std::none_of(out.begin(), out.end(), [&](const LocalModelInfo& c) { return c.id == m.id; })) {
            out.push_back(m);
        }
    }
    for (auto& m : out) DecorateModelLocked(m);
    return out;
}

bool LocalRuntime::Impl::FindModelLocked(const std::string& modelId, LocalModelInfo& out) const {
    if (const LocalModelInfo* m = catalogue_.Find(modelId)) {
        out = *m;
    } else {
        auto it = std::find_if(userModels_.begin(), userModels_.end(),
                               [&](const LocalModelInfo& u) { return u.id == modelId; });
        if (it == userModels_.end()) return false;
        out = *it;
    }
    DecorateModelLocked(out);
    return true;
}

bool LocalRuntime::Impl::FindEngineLocked(const std::string& engineId, EngineInfo& out) const {
    const EngineInfo* e = gallery_.Find(engineId);
    if (!e) return false;
    out = *e;
    DecorateEngineLocked(out);
    return true;
}

void LocalRuntime::Impl::DecorateEngineLocked(EngineInfo& engine) const {
    EngineBundleInfo chosen;
    const bool haveChoice = ChooseBundleFor(engine, hardware_, settings_.preferredAccelerator, chosen);
    std::error_code ec;
    for (auto& b : engine.bundles) {
        const fs::path folder = BundleFolder(b);
        b.installed = fs::is_regular_file(folder / "bundle.json", ec) &&
                      !FindExecutableIn(folder, engine.executable).empty();
        b.installedPath = b.installed ? PathToUtf8(folder) : std::string();
        b.recommended = haveChoice && b.platform == chosen.platform &&
                        b.accelerator == chosen.accelerator && b.version == chosen.version;
    }
}

bool LocalRuntime::Impl::InstalledEngineExecutableLocked(const EngineInfo& engine, std::string& outExe,
                                                         std::string& outAccelerator) const {
    auto ov = engineOverrides_.find(engine.id);
    if (ov != engineOverrides_.end()) {
        // A file is the executable whatever its name (a developer's own
        // build); a folder is searched for the engine's executable.
        std::error_code ec;
        const std::string exe = fs::is_regular_file(PathFromUtf8(ov->second), ec)
            ? ov->second
            : FindExecutableIn(PathFromUtf8(ov->second), engine.executable);
        if (!exe.empty()) {
            outExe = exe;
            outAccelerator = settings_.preferredAccelerator.empty() ? std::string("cpu")
                                                                    : settings_.preferredAccelerator;
            return true;
        }
    }
    EngineInfo decorated = engine;
    DecorateEngineLocked(decorated);
    // The recommended bundle when installed, else any installed bundle for
    // this platform in the gallery's order.
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& b : decorated.bundles) {
            if (!b.installed || b.platform != hardware_.platform) continue;
            if (pass == 0 && !b.recommended) continue;
            outExe = FindExecutableIn(PathFromUtf8(b.installedPath), engine.executable);
            if (outExe.empty()) continue;
            outAccelerator = b.accelerator;
            return true;
        }
    }
    return false;
}

// ===== Installs =====

void LocalRuntime::Impl::Report(const std::shared_ptr<Job>& job) {
    if (job && job->onProgress) job->onProgress(job->progress);
}

std::string LocalRuntime::Impl::Enqueue(std::shared_ptr<Job> job) {
    std::string id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        id = "job-" + std::to_string(nextJobId_++);
        job->progress.jobId = id;
        jobs_[id] = job;
        queue_.push_back(job);
    }
    Report(job);
    jobsCv_.notify_all();
    return id;
}

void LocalRuntime::Impl::WorkerLoop() {
    while (true) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            jobsCv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            job = queue_.front();
            queue_.pop_front();
        }
        Error error;
        if (job->cancel) {
            job->progress.state = InstallProgress::State::Cancelled;
        } else {
            error = job->kind == Job::Kind::Model
                        ? DoInstallModel(job->targetId, job)
                        : DoInstallEngine(job->targetId, job->accelerator, job);
            if (error.IsOk()) {
                job->progress.state = InstallProgress::State::Done;
            } else if (error.code == ErrorCode::Cancelled) {
                job->progress.state = InstallProgress::State::Cancelled;
            } else {
                job->progress.state = InstallProgress::State::Failed;
                job->progress.error = error.message;
            }
        }
        Report(job);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.erase(job->progress.jobId);
        }
        jobsCv_.notify_all();
    }
}

Error LocalRuntime::Impl::DownloadVerified(const std::string& url, const fs::path& dest,
                                           const std::string& sha256, uint64_t expectedSize,
                                           const std::shared_ptr<Job>& job,
                                           uint64_t doneBefore, uint64_t totalAll) {
    if (!fetch_) {
        return MakeError(ErrorCode::NetworkError,
                         "this build has no network transport for downloads (UltraNet is not in it)");
    }
    std::error_code ec;
    // The in-flight file is named by the hash when known, else by the URL's
    // last part, so a second run resumes what the first left.
    std::string partName = sha256.empty() ? PathToUtf8(dest.filename()) : sha256.substr(0, 32);
    const fs::path part = paths_.downloads / PathFromUtf8(partName + ".part");
    const bool resume = fs::is_regular_file(part, ec) && fs::file_size(part, ec) > 0;

    if (job) {
        job->progress.state = InstallProgress::State::Downloading;
        Report(job);
    }
    uint64_t lastReported = 0;
    Error error = fetch_(url, PathToUtf8(part), resume,
                         [&](uint64_t now, uint64_t total) {
                             if (job) {
                                 if (job->cancel) return false;
                                 const uint64_t fileTotal = total ? total : expectedSize;
                                 job->progress.bytesDone  = doneBefore + now;
                                 job->progress.bytesTotal = totalAll ? totalAll : fileTotal;
                                 if (now - lastReported >= 4u * 1024 * 1024 || now == fileTotal) {
                                     lastReported = now;
                                     Report(job);
                                 }
                             }
                             return true;
                         });
    if (!error.IsOk()) {
        if (error.code != ErrorCode::Cancelled) {
            // A failed transfer keeps its .part for a resume; a refused
            // one (4xx) does not.
            if (error.message.find("HTTP 4") != std::string::npos) fs::remove(part, ec);
        }
        return error;
    }
    if (job && job->cancel) return MakeError(ErrorCode::Cancelled, "cancelled");

    if (job) {
        job->progress.state = InstallProgress::State::Verifying;
        Report(job);
    }
    if (expectedSize && fs::file_size(part, ec) != expectedSize && !sha256.empty()) {
        // Size is advisory (the catalogue's are approximate); the hash decides.
    }
    if (!sha256.empty()) {
        std::string actual, hashError;
        if (!Sha256File(part, actual, hashError)) {
            return MakeError(ErrorCode::ProviderError, "cannot verify the download: " + hashError);
        }
        if (actual != sha256) {
            fs::remove(part, ec);
            return MakeError(ErrorCode::ProviderError,
                             "the downloaded file is not the one the catalogue expects "
                             "(SHA-256 mismatch); it was discarded");
        }
    }
    fs::create_directories(dest.parent_path(), ec);
    fs::remove(dest, ec);
    fs::rename(part, dest, ec);
    if (ec) {
        fs::copy_file(part, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            return MakeError(ErrorCode::ProviderError,
                             "cannot move the download into place: " + ec.message());
        }
        fs::remove(part, ec);
    }
    return {};
}

Error LocalRuntime::Impl::ResolveHostHash(LocalModelFile& file) {
    // Hugging Face reports each LFS file's sha256 through its tree API.
    const std::string prefix = "https://huggingface.co/";
    if (file.url.rfind(prefix, 0) != 0 || !transport_) return {};
    const std::string rest = file.url.substr(prefix.size());
    const size_t resolve = rest.find("/resolve/");
    if (resolve == std::string::npos) return {};
    const std::string repo = rest.substr(0, resolve);
    const std::string after = rest.substr(resolve + 9);
    const size_t slash = after.find('/');
    if (slash == std::string::npos) return {};
    const std::string revision = after.substr(0, slash);
    const std::string path = after.substr(slash + 1);

    TransportRequest req;
    req.method    = "GET";
    req.url       = HuggingFaceTreeUrl(repo, revision);
    req.timeoutMs = 20000;
    Error error;
    const TransportResponse resp = transport_->Request(req, &error);
    if (!error.IsOk()) return {};              // offline hash lookup is best effort
    if (resp.statusCode != 200) return {};
    uint64_t size = 0;
    std::string sha;
    if (ParseHuggingFaceTree(resp.body, path, size, sha)) {
        if (size) file.sizeBytes = size;
        if (!sha.empty()) file.sha256 = sha;
    }
    return {};
}

Error LocalRuntime::Impl::DoInstallEngine(const std::string& engineId, const std::string& accelerator,
                                          const std::shared_ptr<Job>& job) {
    std::lock_guard<std::mutex> one(installMutex_);
    EngineInfo engine;
    EngineBundleInfo bundle;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!FindEngineLocked(engineId, engine)) {
            return MakeError(ErrorCode::ModelNotFound, "unknown engine \"" + engineId + "\"");
        }
        if (!ChooseBundleFor(engine, hardware_, accelerator.empty() ? settings_.preferredAccelerator
                                                                    : accelerator, bundle)) {
            return MakeError(ErrorCode::UnsupportedFormat,
                             "the gallery has no " + engineId + " build for " + hardware_.platform +
                                 (accelerator.empty() ? "" : " with " + accelerator));
        }
        if (!accelerator.empty() && bundle.accelerator != accelerator) {
            return MakeError(ErrorCode::UnsupportedFormat,
                             "the gallery has no " + engineId + " build for " + hardware_.platform +
                                 " with " + accelerator);
        }
    }
    if (job) {
        job->progress.what = engineId + " (" + bundle.accelerator + ")";
        job->progress.bytesTotal = bundle.sizeBytes;
    }
    const fs::path folder = BundleFolder(bundle);
    std::error_code ec;
    if (fs::is_regular_file(folder / "bundle.json", ec)) return {};

    // The archive keeps the URL's extension, which is how VirtualFS picks
    // its reader; ".tar.gz" is two of them.
    std::string extension = ".zip";
    for (const char* known : {".tar.gz", ".tgz", ".tar.xz", ".tar.bz2", ".tar", ".zip", ".7z"}) {
        const std::string suffix = known;
        const size_t query = bundle.url.find('?');
        const std::string path = query == std::string::npos ? bundle.url : bundle.url.substr(0, query);
        if (path.size() >= suffix.size() &&
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
            extension = suffix;
            break;
        }
    }
    const fs::path archivePath = paths_.downloads / PathFromUtf8(
        bundle.engineId + "-" + BundleFolderName(bundle) + extension);
    Error error = DownloadVerified(bundle.url, archivePath, bundle.sha256, bundle.sizeBytes, job, 0,
                                   bundle.sizeBytes);
    if (!error.IsOk()) return error;

    if (job) {
        job->progress.state = InstallProgress::State::Unpacking;
        Report(job);
    }
    const fs::path tmp = folder.parent_path() / PathFromUtf8(BundleFolderName(bundle) + ".tmp");
    fs::remove_all(tmp, ec);
    error = UnpackArchive(archivePath, tmp);
    fs::remove(archivePath, ec);
    if (!error.IsOk()) {
        fs::remove_all(tmp, ec);
        return error;
    }
    MarkExecutables(tmp);
    if (FindExecutableIn(tmp, engine.executable).empty()) {
        fs::remove_all(tmp, ec);
        return MakeError(ErrorCode::ProviderError,
                         "the " + engineId + " bundle holds no \"" + engine.executable + "\"");
    }
    json manifest;
    manifest["engineId"]    = bundle.engineId;
    manifest["version"]     = bundle.version;
    manifest["platform"]    = bundle.platform;
    manifest["accelerator"] = bundle.accelerator;
    manifest["url"]         = bundle.url;
    manifest["sha256"]      = bundle.sha256;
    manifest["installed"]   = NowUnix();
    std::string writeError;
    if (!WriteFileAtomic(tmp / "bundle.json", manifest.dump(2), writeError)) {
        fs::remove_all(tmp, ec);
        return MakeError(ErrorCode::ProviderError, writeError);
    }
    fs::remove_all(folder, ec);
    fs::rename(tmp, folder, ec);
    if (ec) {
        return MakeError(ErrorCode::ProviderError,
                         "cannot move the engine into place: " + ec.message());
    }
    return {};
}

Error LocalRuntime::Impl::DoInstallModel(const std::string& modelId, const std::shared_ptr<Job>& job) {
    LocalModelInfo model;
    EngineInfo engine;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!FindModelLocked(modelId, model)) {
            return MakeError(ErrorCode::ModelNotFound, "unknown model \"" + modelId + "\"");
        }
        if (!FindEngineLocked(model.engineId, engine)) {
            return MakeError(ErrorCode::ModelNotFound,
                             "model \"" + modelId + "\" needs engine \"" + model.engineId +
                                 "\", which the gallery does not have");
        }
    }
    if (job) job->progress.what = model.displayName;
    if (model.source == LocalModelSource::File) {
        std::string primary;
        return IsModelInstalled(model, primary)
                   ? Error{}
                   : MakeError(ErrorCode::ModelNotFound, "the model file is missing: " +
                                                             (model.files.empty() ? std::string() : model.files.front().name));
    }

    // The engine first, so one job brings everything a model needs.
    {
        std::string exe, accel;
        bool haveEngine = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            haveEngine = InstalledEngineExecutableLocked(engine, exe, accel);
        }
        if (!haveEngine) {
            Error error = DoInstallEngine(engine.id, "", job);
            if (!error.IsOk()) return error;
            if (job) job->progress.what = model.displayName;
        }
    }

    std::lock_guard<std::mutex> one(installMutex_);
    if (model.installed) return {};
    const fs::path folder = ModelFolder(model.id);
    std::error_code ec;

    // Hashes and sizes from the host when the catalogue has none.
    for (auto& f : model.files) {
        if (f.sha256.empty()) ResolveHostHash(f);
    }
    uint64_t total = 0;
    for (const auto& f : model.files) total += f.sizeBytes;
    if (job) { job->progress.bytesTotal = total; Report(job); }

    // Room on disk, against the cap.
    uint64_t used = FolderSizeBytes(paths_.engines) + FolderSizeBytes(paths_.models);
    if (settings_.diskCapBytes && used + total > settings_.diskCapBytes) {
        return MakeError(ErrorCode::QuotaExceeded,
                         "installing this model would exceed the local-models disk limit; "
                         "remove a model or raise the limit in the settings");
    }

    uint64_t done = 0;
    for (const auto& f : model.files) {
        if (f.url.empty()) {
            return MakeError(ErrorCode::InvalidRequest, "file \"" + f.name + "\" has no download URL");
        }
        const fs::path dest = folder / PathFromUtf8(f.name);
        if (fs::is_regular_file(dest, ec) && fs::file_size(dest, ec) > 0) {
            done += f.sizeBytes;
            continue;
        }
        Error error = DownloadVerified(f.url, dest, f.sha256, f.sizeBytes, job, done, total);
        if (!error.IsOk()) return error;
        done += f.sizeBytes;
    }
    // model.json last: its presence is what "installed" means.
    LocalModelInfo record = model;
    record.installed = false;
    record.running = false;
    record.installedPath.clear();
    record.primaryFile.clear();
    json doc;
    doc["formatVersion"] = 1;
    doc["installed"]     = NowUnix();
    doc["model"]         = json::parse(SerializeModels({record}))["models"][0];
    std::string writeError;
    if (!WriteFileAtomic(folder / "model.json", doc.dump(2), writeError)) {
        return MakeError(ErrorCode::ProviderError, writeError);
    }
    return {};
}

// ===== Supervision =====

std::vector<RunningRecord> LocalRuntime::Impl::ReadRecords() const {
    std::string text;
    if (!ReadWholeFile(paths_.runtimeJson, text)) return {};
    return ParseRecords(text);
}

bool LocalRuntime::Impl::WriteRecords(const std::vector<RunningRecord>& records) const {
    std::string error;
    return WriteFileAtomic(paths_.runtimeJson, SerializeRecords(records), error);
}

bool LocalRuntime::Impl::Healthy(const std::string& baseUrl, int timeoutMs) const {
    if (!transport_) return false;
    TransportRequest req;
    req.method    = "GET";
    req.url       = baseUrl + "/health";
    req.timeoutMs = timeoutMs;
    Error error;
    const TransportResponse resp = transport_->Request(req, &error);
    return error.IsOk() && resp.statusCode == 200;
}

std::string LocalRuntime::Impl::ApiKey() const {
    std::string key;
    if (ReadWholeFile(paths_.runtimeKey, key)) {
        while (!key.empty() && (key.back() == '\n' || key.back() == '\r' || key.back() == ' ')) key.pop_back();
        if (key.size() >= 32) return key;
    }
    key = RandomKeyHex();
    std::string error;
    WriteFileAtomic(paths_.runtimeKey, key + "\n", error);
#if !defined(_WIN32)
    std::error_code ec;
    fs::permissions(paths_.runtimeKey, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
#endif
    return key;
}

void LocalRuntime::Impl::StopRecord(const RunningRecord& record) {
    StopEngineProcess(record.pid, 3000);
}

void LocalRuntime::Impl::SweepRecords(std::vector<RunningRecord>& records, bool stopIdle) {
    const int64_t now = NowUnix();
    // 0 (or less) stops at the next sweep: what a settings page's "stop
    // when done" and the tests mean by it.
    const int64_t idle = settings_.idleMinutes > 0 ? static_cast<int64_t>(settings_.idleMinutes) * 60 : 0;
    for (auto it = records.begin(); it != records.end();) {
        if (!it->pid) { ++it; continue; }       // a failure marker, kept
        if (!IsEngineProcessAlive(it->pid)) {
            it = records.erase(it);
            continue;
        }
        if (stopIdle && now - std::max(it->lastUsedUnix, it->startedUnix) > idle) {
            StopRecord(*it);
            it = records.erase(it);
            continue;
        }
        ++it;
    }
}

Error LocalRuntime::Impl::StartEngineLocked(const EngineInfo& engine, const std::string& exe,
                                            const std::string& accelerator,
                                            const LocalModelInfo& model, RunningRecord& outRecord) {
    const int port = FindFreeLoopbackPort();
    if (!port) return MakeError(ErrorCode::ProviderError, "no free loopback port");
    const std::string key = ApiKey();
    (void)key;
    ProcessStart start;
    start.argv.push_back(exe);
    for (const std::string& a : BuildEngineArgs(engine, model, accelerator, port,
                                                PathToUtf8(paths_.runtimeKey), hardware_)) {
        start.argv.push_back(a);
    }
    start.workingDirUtf8 = PathToUtf8(PathFromUtf8(exe).parent_path());
    const fs::path log = paths_.logs / PathFromUtf8(engine.id + "-" + ModelFolderName(model.id) + ".log");
    std::error_code ec;
    if (fs::exists(log, ec) && fs::file_size(log, ec) > 4u * 1024 * 1024) fs::remove(log, ec);
    start.logPathUtf8 = PathToUtf8(log);

    uint64_t pid = 0;
    std::string startError;
    if (!StartEngineProcess(start, pid, startError)) {
        return MakeError(ErrorCode::ProviderError, "cannot start " + engine.id + ": " + startError);
    }
    RunningRecord record;
    record.engineId     = engine.id;
    record.modelId      = model.id;
    record.baseUrl      = "http://127.0.0.1:" + std::to_string(port);
    record.apiKeyFile   = PathToUtf8(paths_.runtimeKey);
    record.pid          = pid;
    record.ownerPid     = ThisProcessId();
    record.startedUnix  = NowUnix();
    record.lastUsedUnix = record.startedUnix;

    const int timeout = StartTimeoutSeconds(model, settings_);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!IsEngineProcessAlive(pid)) {
            return MakeError(ErrorCode::ProviderError,
                             engine.id + " ended while starting " + model.displayName + ":\n" +
                                 TailOfFile(log, 12));
        }
        if (Healthy(record.baseUrl, 2000)) {
            outRecord = record;
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    StopEngineProcess(pid, 2000);
    return MakeError(ErrorCode::Timeout,
                     engine.id + " did not answer within " + std::to_string(timeout) +
                         " s while loading " + model.displayName + ":\n" + TailOfFile(log, 12));
}

// ===== LocalRuntime =====

LocalRuntime& LocalRuntime::Instance() {
    static LocalRuntime runtime{LocalRuntimeOptions{}};
    return runtime;
}

LocalRuntime::LocalRuntime(LocalRuntimeOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

LocalRuntime::~LocalRuntime() = default;

std::vector<LocalModelInfo> LocalRuntime::ListModels(const std::string& capability) const {
    std::vector<LocalModelInfo> all;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        all = impl_->AllModelsLocked();
    }
    const std::vector<RunningEngineInfo> running = ListRunning();
    std::vector<LocalModelInfo> out;
    for (auto& m : all) {
        if (!capability.empty() && m.capability != capability) continue;
        m.running = std::any_of(running.begin(), running.end(),
                                [&](const RunningEngineInfo& r) { return r.modelId == m.id; });
        out.push_back(std::move(m));
    }
    return out;
}

bool LocalRuntime::FindModel(const std::string& modelId, LocalModelInfo& out) const {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        if (!impl_->FindModelLocked(modelId, out)) return false;
    }
    for (const RunningEngineInfo& r : ListRunning()) {
        if (r.modelId == modelId) out.running = true;
    }
    return true;
}

std::vector<EngineInfo> LocalRuntime::ListEngines() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::vector<EngineInfo> out = impl_->gallery_.engines;
    for (auto& e : out) impl_->DecorateEngineLocked(e);
    return out;
}

LocalModelInfo LocalRuntime::Recommend(const std::string& capability) const {
    const std::vector<LocalModelInfo> models = ListModels(capability);
    // Installed and usable first.
    for (const auto& m : models) {
        if (m.installed && m.fit != ModelFit::TooLarge) return m;
    }
    // Then the catalogue's recommended ones that fit the GPU, then any
    // that runs at all, in catalogue order.
    for (const ModelFit wanted : {ModelFit::Recommended, ModelFit::RunsOnCpu}) {
        for (const auto& m : models) {
            if (m.source != LocalModelSource::Catalogue || m.fit != wanted) continue;
            if (std::find(m.tags.begin(), m.tags.end(), "recommended") != m.tags.end()) return m;
        }
        for (const auto& m : models) {
            if (m.source == LocalModelSource::Catalogue && m.fit == wanted) return m;
        }
    }
    for (const auto& m : models) {
        if (m.fit == ModelFit::Unknown) return m;
    }
    return LocalModelInfo{};
}

ModelFit LocalRuntime::Fit(const LocalModelInfo& model) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return ComputeFit(model, impl_->hardware_);
}

bool LocalRuntime::ChooseBundle(const std::string& engineId, EngineBundleInfo& out) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    EngineInfo engine;
    if (!impl_->FindEngineLocked(engineId, engine)) return false;
    for (const auto& b : engine.bundles) {
        if (b.recommended) { out = b; return true; }
    }
    return false;
}

const HardwareProfile& LocalRuntime::Hardware() const { return impl_->hardware_; }

std::vector<RunningEngineInfo> LocalRuntime::ListRunning() const {
    std::vector<RunningEngineInfo> out;
    const uint64_t me = ThisProcessId();
    for (const RunningRecord& r : impl_->ReadRecords()) {
        if (!r.pid || !IsEngineProcessAlive(r.pid)) continue;
        RunningEngineInfo info;
        info.engineId           = r.engineId;
        info.modelId            = r.modelId;
        info.baseUrl            = r.baseUrl;
        info.processId          = r.pid;
        info.startedUnix        = r.startedUnix;
        info.lastUsedUnix       = r.lastUsedUnix;
        info.ownedByThisProcess = r.ownerPid == me;
        out.push_back(std::move(info));
    }
    return out;
}

uint64_t LocalRuntime::DiskUsedBytes() const {
    return FolderSizeBytes(impl_->paths_.engines) + FolderSizeBytes(impl_->paths_.models);
}

std::string LocalRuntime::RootFolder() const { return PathToUtf8(impl_->paths_.root); }

Error LocalRuntime::AddModelFile(const std::string& pathUtf8, const std::string& capability,
                                 const std::string& displayName, std::string& outModelId) {
    std::error_code ec;
    if (!fs::is_regular_file(PathFromUtf8(pathUtf8), ec)) {
        return MakeError(ErrorCode::InvalidRequest, "not a file: " + pathUtf8);
    }
    if (capability != "textllm" && capability != "embeddings") {
        return MakeError(ErrorCode::InvalidRequest,
                         "a model file serves \"textllm\" or \"embeddings\", not \"" + capability + "\"");
    }
    LocalModelInfo model;
    model.id          = "file:" + pathUtf8;
    model.displayName = displayName.empty() ? PathToUtf8(PathFromUtf8(pathUtf8).filename()) : displayName;
    model.capability  = capability;
    model.engineId    = "llama-server";
    model.source      = LocalModelSource::File;
    LocalModelFile file;
    file.name      = pathUtf8;
    file.sizeBytes = fs::file_size(PathFromUtf8(pathUtf8), ec);
    model.files.push_back(file);
    model.sizeBytes = file.sizeBytes;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->userModels_.erase(
            std::remove_if(impl_->userModels_.begin(), impl_->userModels_.end(),
                           [&](const LocalModelInfo& m) { return m.id == model.id; }),
            impl_->userModels_.end());
        impl_->userModels_.push_back(model);
        impl_->SaveUserModels();
    }
    outModelId = model.id;
    return {};
}

Error LocalRuntime::AddHuggingFaceModel(const std::string& repo, const std::string& file,
                                        const std::string& capability,
                                        const std::string& displayName, std::string& outModelId) {
    if (repo.find('/') == std::string::npos || file.empty() || file.find("..") != std::string::npos ||
        file.find('/') != std::string::npos) {
        return MakeError(ErrorCode::InvalidRequest,
                         "a Hugging Face model is \"organisation/repository\" and a file name in it");
    }
    if (capability != "textllm" && capability != "embeddings") {
        return MakeError(ErrorCode::InvalidRequest,
                         "a GGUF serves \"textllm\" or \"embeddings\", not \"" + capability + "\"");
    }
    LocalModelInfo model;
    model.id          = "hf:" + repo + ":" + file;
    model.displayName = displayName.empty() ? file : displayName;
    model.capability  = capability;
    model.engineId    = "llama-server";
    model.source      = LocalModelSource::HuggingFace;
    model.license     = "see the repository";
    LocalModelFile f;
    f.name = file;
    f.url  = HuggingFaceFileUrl(repo, "", file);
    impl_->ResolveHostHash(f);
    model.files.push_back(f);
    model.sizeBytes = f.sizeBytes;
    if (capability == "embeddings") model.engineArgs = {"--embeddings"};
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->userModels_.erase(
            std::remove_if(impl_->userModels_.begin(), impl_->userModels_.end(),
                           [&](const LocalModelInfo& m) { return m.id == model.id; }),
            impl_->userModels_.end());
        impl_->userModels_.push_back(model);
        impl_->SaveUserModels();
    }
    outModelId = model.id;
    return {};
}

Error LocalRuntime::ForgetModel(const std::string& modelId) {
    LocalModelInfo model;
    if (!FindModel(modelId, model)) {
        return MakeError(ErrorCode::ModelNotFound, "unknown model \"" + modelId + "\"");
    }
    if (model.source == LocalModelSource::Catalogue) {
        return MakeError(ErrorCode::InvalidRequest, "a catalogue model is removed, not forgotten");
    }
    if (model.installed && model.source != LocalModelSource::File) {
        Error error = RemoveModel(modelId);
        if (!error.IsOk()) return error;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->userModels_.erase(
        std::remove_if(impl_->userModels_.begin(), impl_->userModels_.end(),
                       [&](const LocalModelInfo& m) { return m.id == modelId; }),
        impl_->userModels_.end());
    impl_->SaveUserModels();
    return {};
}

std::string LocalRuntime::InstallModel(const std::string& modelId, InstallProgressCallback onProgress) {
    auto job = std::make_shared<Job>();
    job->kind       = Job::Kind::Model;
    job->targetId   = modelId;
    job->onProgress = std::move(onProgress);
    LocalModelInfo model;
    if (FindModel(modelId, model)) job->progress.what = model.displayName;
    return impl_->Enqueue(job);
}

std::string LocalRuntime::InstallEngine(const std::string& engineId, const std::string& accelerator,
                                        InstallProgressCallback onProgress) {
    auto job = std::make_shared<Job>();
    job->kind        = Job::Kind::Engine;
    job->targetId    = engineId;
    job->accelerator = accelerator;
    job->onProgress  = std::move(onProgress);
    job->progress.what = engineId;
    return impl_->Enqueue(job);
}

void LocalRuntime::Cancel(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->jobs_.find(jobId);
    if (it != impl_->jobs_.end()) it->second->cancel = true;
}

std::vector<InstallProgress> LocalRuntime::ListJobs() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::vector<InstallProgress> out;
    for (const auto& kv : impl_->jobs_) out.push_back(kv.second->progress);
    std::sort(out.begin(), out.end(), [](const InstallProgress& a, const InstallProgress& b) {
        return std::stoi(a.jobId.substr(4)) < std::stoi(b.jobId.substr(4));
    });
    return out;
}

void LocalRuntime::WaitForJobs() {
    std::unique_lock<std::mutex> lock(impl_->mutex_);
    impl_->jobsCv_.wait(lock, [this] { return impl_->jobs_.empty(); });
}

Error LocalRuntime::RemoveModel(const std::string& modelId) {
    LocalModelInfo model;
    if (!FindModel(modelId, model)) {
        return MakeError(ErrorCode::ModelNotFound, "unknown model \"" + modelId + "\"");
    }
    StopModel(modelId);
    if (model.source == LocalModelSource::File) return {};   // the user's file stays
    std::lock_guard<std::mutex> one(impl_->installMutex_);
    std::error_code ec;
    fs::remove_all(impl_->ModelFolder(modelId), ec);
    if (ec) return MakeError(ErrorCode::ProviderError, "cannot remove the model: " + ec.message());
    return {};
}

Error LocalRuntime::RemoveEngine(const std::string& engineId, const std::string& accelerator) {
    EngineInfo engine;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        if (!impl_->FindEngineLocked(engineId, engine)) {
            return MakeError(ErrorCode::ModelNotFound, "unknown engine \"" + engineId + "\"");
        }
    }
    // Every engine of this kind stops: the bundle may be the one running.
    for (const RunningEngineInfo& r : ListRunning()) {
        if (r.engineId == engineId) StopModel(r.modelId);
    }
    std::lock_guard<std::mutex> one(impl_->installMutex_);
    std::error_code ec;
    for (const auto& b : engine.bundles) {
        if (!b.installed) continue;
        if (!accelerator.empty() && b.accelerator != accelerator) continue;
        fs::remove_all(PathFromUtf8(b.installedPath), ec);
    }
    return {};
}

Error LocalRuntime::Ensure(const std::string& modelId, EngineEndpoint& outEndpoint, bool allowDownload) {
    if (!impl_->storageError_.empty()) {
        return MakeError(ErrorCode::ProviderError, impl_->storageError_);
    }
    LocalModelInfo model;
    EngineInfo engine;
    DownloadPolicy policy;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        if (!impl_->FindModelLocked(modelId, model)) {
            return MakeError(ErrorCode::ModelNotFound, "unknown local model \"" + modelId + "\"");
        }
        if (!impl_->FindEngineLocked(model.engineId, engine)) {
            return MakeError(ErrorCode::ModelNotFound,
                             "model \"" + modelId + "\" needs engine \"" + model.engineId +
                                 "\", which the gallery does not have");
        }
        policy = impl_->settings_.policy;
    }
    if (model.fit == ModelFit::TooLarge) {
        return MakeError(ErrorCode::ModelNotFound,
                         model.displayName + " is too large for this computer's memory", "too-large");
    }

    // Install what is missing, policy permitting.
    std::string exe, accelerator;
    bool haveEngine = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        haveEngine = impl_->InstalledEngineExecutableLocked(engine, exe, accelerator);
    }
    if (!haveEngine || !model.installed) {
        const bool may = policy == DownloadPolicy::Automatic ||
                         (policy == DownloadPolicy::Ask && allowDownload);
        if (!may) {
            std::string what = !model.installed ? model.displayName : std::string();
            if (!haveEngine) what += (what.empty() ? "" : " and ") + std::string("the ") + engine.id + " engine";
            return MakeError(ErrorCode::ModelNotFound,
                             what + (policy == DownloadPolicy::Never
                                         ? " must be installed from the Local models settings"
                                         : " must be downloaded first"),
                             "needs-install");
        }
        Error error = impl_->DoInstallModel(modelId, nullptr);
        if (!error.IsOk()) return error;
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->FindModelLocked(modelId, model);
        if (!impl_->InstalledEngineExecutableLocked(engine, exe, accelerator)) {
            return MakeError(ErrorCode::ProviderError, "the " + engine.id + " engine did not install");
        }
    }

    // The shared record: one engine per model across every application.
    ScopedFileLock lock(impl_->paths_.runtimeLock, 15000);
    if (!lock.Held()) {
        return MakeError(ErrorCode::Timeout, "another UltraAI process holds the local runtime lock");
    }
    std::vector<RunningRecord> records = impl_->ReadRecords();
    impl_->SweepRecords(records, true);
    for (auto& r : records) {
        if (r.modelId != modelId || !r.pid) continue;
        if (impl_->Healthy(r.baseUrl, 3000)) {
            r.lastUsedUnix = NowUnix();
            impl_->WriteRecords(records);
            outEndpoint.baseUrl    = r.baseUrl;
            outEndpoint.apiKey     = impl_->ApiKey();
            outEndpoint.modelAlias = modelId;
            outEndpoint.engineId   = r.engineId;
            outEndpoint.processId  = r.pid;
            return {};
        }
        // Recorded, alive, not answering: it is hung or still loading
        // under another owner. Give a loading one its time once.
        if (NowUnix() - r.startedUnix < StartTimeoutSeconds(model, impl_->settings_)) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(
                StartTimeoutSeconds(model, impl_->settings_));
            while (std::chrono::steady_clock::now() < deadline && IsEngineProcessAlive(r.pid)) {
                if (impl_->Healthy(r.baseUrl, 2000)) {
                    r.lastUsedUnix = NowUnix();
                    impl_->WriteRecords(records);
                    outEndpoint.baseUrl    = r.baseUrl;
                    outEndpoint.apiKey     = impl_->ApiKey();
                    outEndpoint.modelAlias = modelId;
                    outEndpoint.engineId   = r.engineId;
                    outEndpoint.processId  = r.pid;
                    return {};
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        }
        impl_->StopRecord(r);
        r.pid = 0;
    }
    // A model that failed to start twice in a row is not tried again until
    // something changes (a reinstall clears the marker).
    int failures = 0;
    for (auto it = records.begin(); it != records.end();) {
        if (it->modelId == modelId && !it->pid) {
            failures = it->failures;
            it = records.erase(it);
        } else {
            ++it;
        }
    }
    if (failures >= 2) {
        RunningRecord marker;
        marker.modelId  = modelId;
        marker.engineId = engine.id;
        marker.failures = failures;
        records.push_back(marker);
        impl_->WriteRecords(records);
        return MakeError(ErrorCode::ProviderError,
                         model.displayName + " failed to start twice; see the log in " +
                             PathToUtf8(impl_->paths_.logs) + " and reinstall it to try again",
                         "start-failed");
    }

    RunningRecord started;
    Error error = impl_->StartEngineLocked(engine, exe, accelerator, model, started);
    if (!error.IsOk()) {
        RunningRecord marker;
        marker.modelId  = modelId;
        marker.engineId = engine.id;
        marker.failures = failures + 1;
        records.push_back(marker);
        impl_->WriteRecords(records);
        return error;
    }
    records.push_back(started);
    impl_->WriteRecords(records);
    outEndpoint.baseUrl    = started.baseUrl;
    outEndpoint.apiKey     = impl_->ApiKey();
    outEndpoint.modelAlias = modelId;
    outEndpoint.engineId   = engine.id;
    outEndpoint.processId  = started.pid;
    return {};
}

void LocalRuntime::Touch(const EngineEndpoint& endpoint) {
    const int64_t now = NowUnix();
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        int64_t& last = impl_->lastTouchWritten_[endpoint.baseUrl];
        if (now - last < 30) return;        // the record takes a touch every 30 s at most
        last = now;
    }
    ScopedFileLock lock(impl_->paths_.runtimeLock, 2000);
    if (!lock.Held()) return;
    std::vector<RunningRecord> records = impl_->ReadRecords();
    for (auto& r : records) {
        if (r.baseUrl == endpoint.baseUrl) r.lastUsedUnix = now;
    }
    impl_->WriteRecords(records);
}

void LocalRuntime::StopIdle() {
    ScopedFileLock lock(impl_->paths_.runtimeLock, 5000);
    if (!lock.Held()) return;
    std::vector<RunningRecord> records = impl_->ReadRecords();
    impl_->SweepRecords(records, true);
    impl_->WriteRecords(records);
}

void LocalRuntime::StopAll() {
    ScopedFileLock lock(impl_->paths_.runtimeLock, 5000);
    std::vector<RunningRecord> records = impl_->ReadRecords();
    for (const auto& r : records) {
        if (r.pid) impl_->StopRecord(r);
    }
    std::vector<RunningRecord> markers;
    for (const auto& r : records) {
        if (!r.pid) markers.push_back(r);
    }
    impl_->WriteRecords(markers);
}

void LocalRuntime::StopModel(const std::string& modelId) {
    ScopedFileLock lock(impl_->paths_.runtimeLock, 5000);
    std::vector<RunningRecord> records = impl_->ReadRecords();
    for (auto it = records.begin(); it != records.end();) {
        if (it->modelId == modelId) {
            if (it->pid) impl_->StopRecord(*it);
            it = records.erase(it);     // a failure marker goes too: a stop is a fresh start
        } else {
            ++it;
        }
    }
    impl_->WriteRecords(records);
}

LocalRuntimeSettings LocalRuntime::GetSettings() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->settings_;
}

void LocalRuntime::SetSettings(const LocalRuntimeSettings& settings) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->settings_ = settings;
    impl_->SaveSettings();
}

Error LocalRuntime::RefreshGallery() {
    std::string url;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        url = impl_->settings_.galleryUrl.empty() ? DefaultGalleryUrl() : impl_->settings_.galleryUrl;
    }
    if (!impl_->transport_) {
        return MakeError(ErrorCode::NetworkError, "this build has no network transport");
    }
    auto fetchText = [&](const std::string& u, std::string& out) -> Error {
        TransportRequest req;
        req.method    = "GET";
        req.url       = u;
        req.timeoutMs = 30000;
        Error error;
        const TransportResponse resp = impl_->transport_->Request(req, &error);
        if (!error.IsOk()) return error;
        if (resp.statusCode != 200) {
            return MakeError(ErrorCode::NetworkError,
                             "the gallery answered HTTP " + std::to_string(resp.statusCode) + " for " + u);
        }
        out = resp.body;
        return {};
    };
    std::string indexText;
    Error error = fetchText(url, indexText);
    if (!error.IsOk()) return error;
    Gallery gallery;
    std::string parseError;
    if (!ParseGallery(indexText, gallery, parseError)) {
        return MakeError(ErrorCode::ProviderError, "the gallery index is not usable: " + parseError);
    }
    // The catalogue sits beside the index.
    std::string catalogueUrl = url;
    const size_t slash = catalogueUrl.rfind('/');
    catalogueUrl = (slash == std::string::npos ? std::string() : catalogueUrl.substr(0, slash + 1)) +
                   "catalogue.json";
    std::string catalogueText;
    Catalogue catalogue;
    const bool haveCatalogue = fetchText(catalogueUrl, catalogueText).IsOk() &&
                               ParseCatalogue(catalogueText, catalogue, parseError);
    std::string writeError;
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->gallery_ = gallery;
    WriteFileAtomic(impl_->paths_.galleryJson, indexText, writeError);
    if (haveCatalogue) {
        impl_->catalogue_ = catalogue;
        WriteFileAtomic(impl_->paths_.catalogueJson, catalogueText, writeError);
    }
    return {};
}

std::string LocalRuntime::GalleryPublished() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->gallery_.published;
}

std::string LocalRuntime::CapabilityOf(const std::string& modelId) const {
    LocalModelInfo model;
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->FindModelLocked(modelId, model) ? model.capability : std::string();
}

} // namespace UltraAI
