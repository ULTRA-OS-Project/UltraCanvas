// UltraAI/tests/test_local_runtime.cpp
// Exercises the local runtime (UltraAILocalRuntime.h) without a network,
// an engine or a model: the built-in gallery and catalogue parse; the
// hardware fit and the bundle choice; a model install through an injected
// copy fetcher (progress, hash verification, the .part file, model.json
// last); the engine supervision with the fake engine of fake_engine.cpp
// (Ensure under each download policy, the shared record, a second Ensure
// finding the running engine, idle stop, the restart marker after two
// failed starts); and the `local` provider talking to the fake engine over
// real loopback HTTP, for chat, embeddings and speech to text (the fake
// engine started as whisper-server: its command line, translation, the
// segments; a speech model whose engine has no build here not offered);
// the topics (the built-in document, the resolution
// of a topic's model, the preferences, new models and the installs the
// topic settings call for); signed galleries (keys pinned on first use, a
// tampered or unsigned document refused, pins dropped with the address).
//
// argv[1] is the fake engine's path (CMake passes it). Uses plain asserts.

#include "UltraAILocalRuntime.h"
#include "LocalRuntimeInternal.h"
#include "UltraAILocal.h"
#include "UltraAI.h"
#include "UltraAITransport.h"
#ifdef ULTRAAI_HAS_ULTRANET
#include "UltraAIUltraNetTransport.h"
#endif
#include "UltraCanvasPathUtf8.h"
#include "UltraCrypt/UltraCryptCore.h"
#include "VirtualFS/VirtualFS.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace UltraAI;
using namespace UltraAI::local_detail;
namespace fs = std::filesystem;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace {

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { std::cerr << "FAIL: " #cond " at " << __FILE__ << ":" \
                  << __LINE__ << std::endl; std::abort(); } \
} while (0)

#define EXPECT_EQ(a, b) do { \
    if (!((a) == (b))) { std::cerr << "FAIL: " #a " == " #b " at " \
                  << __FILE__ << ":" << __LINE__ << " (" << (a) << " vs " << (b) << ")" << std::endl; std::abort(); } \
} while (0)

std::string g_fakeEngine;
fs::path g_scratch;

void WriteFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::string Sha(const fs::path& path) {
    std::string hex, error;
    EXPECT_TRUE(Sha256File(path, hex, error));
    return hex;
}

// A hardware profile for this platform: 16 GB, one 8 GB GPU.
std::shared_ptr<HardwareProfile> TestHardware(uint64_t gpuBytes = 8ull << 30) {
    auto hw = std::make_shared<HardwareProfile>();
    hw->platform      = HardwareProfile::CurrentPlatform();
    hw->memoryBytes   = 16ull << 30;
    hw->availableMemoryBytes = 12ull << 30;
    hw->physicalCores = 8;
    hw->logicalCores  = 16;
    if (gpuBytes) {
        HardwareProfile::Gpu gpu;
        gpu.vendor = "NVIDIA";
        gpu.model  = "Test GPU";
        gpu.videoMemoryBytes = gpuBytes;
        gpu.discrete = true;
        hw->gpus.push_back(gpu);
    }
    hw->DeriveAccelerators();
    return hw;
}

// The fetch of a test: copies from a map of URL -> source file, counting
// the calls and whether a resume was asked for.
struct CopyFetcher {
    std::map<std::string, fs::path> sources;
    std::vector<std::pair<std::string, bool>> calls;
    int failNext = 0;      // fail the next N fetches with NetworkError

    FetchFileFn Fn() {
        return [this](const std::string& url, const std::string& dest, bool resume,
                      const std::function<bool(uint64_t, uint64_t)>& onProgress) -> Error {
            calls.push_back({url, resume});
            Error error;
            if (failNext > 0) {
                --failNext;
                error.code    = ErrorCode::NetworkError;
                error.message = "simulated network failure";
                return error;
            }
            auto it = sources.find(url);
            if (it == sources.end()) {
                error.code    = ErrorCode::NetworkError;
                error.message = "download of " + url + " failed with HTTP 404";
                return error;
            }
            std::error_code ec;
            fs::copy_file(it->second, PathFromUtf8(dest), fs::copy_options::overwrite_existing, ec);
            if (ec) {
                error.code    = ErrorCode::NetworkError;
                error.message = ec.message();
                return error;
            }
            const uint64_t size = fs::file_size(PathFromUtf8(dest), ec);
            if (onProgress) {
                onProgress(size / 2, size);
                onProgress(size, size);
            }
            return error;
        };
    }
};

// A gallery whose one bundle is a zip the test makes, holding the fake
// engine under the name the runtime looks for.
std::string TestGalleryJson(const std::string& bundleUrl, const std::string& sha, uint64_t size) {
    return R"({"formatVersion": 1, "published": "2026-10-20", "engines": [{
        "id": "llama-server", "version": "test", "license": "MIT", "executable": "llama-server",
        "serves": ["textllm", "embeddings"], "defaultArgs": ["--jinja"],
        "bundles": [{"platform": ")" + HardwareProfile::CurrentPlatform() + R"(", "accel": "cpu", "url": ")" +
           bundleUrl + R"(", "sha256": ")" + sha + R"(", "size": )" + std::to_string(size) + R"(}]}]})";
}

std::string TestCatalogueJson(const std::string& modelUrl, const std::string& sha, uint64_t size) {
    return R"({"formatVersion": 1, "published": "2026-10-20", "models": [
      {"id": "tiny", "displayName": "Tiny test model", "capability": "textllm", "engine": "llama-server",
       "license": "MIT", "files": [{"name": "tiny.gguf", "url": ")" + modelUrl + R"(", "sha256": ")" + sha +
           R"(", "size": )" + std::to_string(size) + R"(}],
       "requires": {"memoryBytes": 1000000, "gpuMemoryBytes": 1000000}, "features": {"contextWindow": 2048, "tools": true},
       "engineArgs": ["--ctx-size", "2048"], "tags": ["recommended"]},
      {"id": "tiny-embed", "displayName": "Tiny embeddings", "capability": "embeddings", "engine": "llama-server",
       "license": "MIT", "files": [{"name": "tiny.gguf", "url": ")" + modelUrl + R"(", "sha256": ")" + sha +
           R"(", "size": )" + std::to_string(size) + R"(}],
       "requires": {"memoryBytes": 1000000}, "features": {"embeddingDimensions": 3}},
      {"id": "wrong-hash", "displayName": "Wrong hash", "capability": "textllm", "engine": "llama-server",
       "files": [{"name": "wrong.gguf", "url": ")" + modelUrl + R"(", "sha256": "0000000000000000000000000000000000000000000000000000000000000000", "size": 10}],
       "requires": {"memoryBytes": 1000000}},
      {"id": "huge", "displayName": "Huge", "capability": "textllm", "engine": "llama-server",
       "files": [{"name": "huge.gguf", "url": ")" + modelUrl + R"(", "size": 1000000000000}],
       "requires": {"memoryBytes": 1000000000000}},
      {"id": "crash-me", "displayName": "Crashes", "capability": "textllm", "engine": "llama-server",
       "files": [{"name": "tiny.gguf", "url": ")" + modelUrl + R"(", "sha256": ")" + sha +
           R"(", "size": )" + std::to_string(size) + R"(}],
       "requires": {"memoryBytes": 1000000}}
    ]})";
}

// ----- the pure parts -----

void TestBuiltinData() {
    Gallery gallery;
    std::string error;
    EXPECT_TRUE(ParseGallery(BuiltinGalleryJson(), gallery, error));
    EXPECT_TRUE(!gallery.engines.empty());
    const EngineInfo* llama = gallery.Find("llama-server");
    EXPECT_TRUE(llama != nullptr);
    EXPECT_TRUE(llama->bundles.size() >= 8);
    for (const EngineInfo& engine : gallery.engines) {
        for (const auto& b : engine.bundles) {
            EXPECT_EQ(b.sha256.size(), size_t(64));
            EXPECT_TRUE(b.sizeBytes > 1000000);
            EXPECT_TRUE(b.url.rfind("https://", 0) == 0);
        }
    }
    // whisper.cpp's server on every platform: upstream's CPU builds for
    // Linux and Windows x64, ours for the rest (Metal on Apple silicon,
    // Windows on ARM, Vulkan). The Linux Vulkan builds need glibc 2.39, so
    // Auto never picks them.
    const EngineInfo* whisper = gallery.Find("whisper-server");
    EXPECT_TRUE(whisper != nullptr);
    EXPECT_TRUE(std::find(whisper->serves.begin(), whisper->serves.end(), "speechtotext") != whisper->serves.end());
    auto whisperBundle = [&](const std::string& platform, const std::string& accel) -> const EngineBundleInfo* {
        for (const auto& b : whisper->bundles) {
            if (b.platform == platform && b.accelerator == accel) return &b;
        }
        return nullptr;
    };
    for (const char* platform : {"linux-x64", "linux-arm64", "win-x64", "macos-x64", "win-arm64"}) {
        EXPECT_TRUE(whisperBundle(platform, "cpu") != nullptr);
    }
    EXPECT_TRUE(whisperBundle("macos-arm64", "metal") != nullptr);
    EXPECT_EQ(whisperBundle("macos-arm64", "metal")->minOs, std::string("13.3"));
    for (const char* platform : {"win-x64", "win-arm64"}) {
        EXPECT_TRUE(whisperBundle(platform, "vulkan") != nullptr && !whisperBundle(platform, "vulkan")->optional);
    }
    for (const char* platform : {"linux-x64", "linux-arm64"}) {
        EXPECT_TRUE(whisperBundle(platform, "vulkan") != nullptr && whisperBundle(platform, "vulkan")->optional);
    }
    Catalogue catalogue;
    EXPECT_TRUE(ParseCatalogue(BuiltinCatalogueJson(), catalogue, error));
    EXPECT_TRUE(catalogue.models.size() >= 4);
    bool chat = false, embed = false, speech = false;
    for (const auto& m : catalogue.models) {
        EXPECT_TRUE(!m.files.empty());
        EXPECT_TRUE(!m.license.empty());
        const EngineInfo* engine = gallery.Find(m.engineId);
        EXPECT_TRUE(engine != nullptr);
        EXPECT_TRUE(std::find(engine->serves.begin(), engine->serves.end(), m.capability) != engine->serves.end());
        if (m.capability == "textllm") chat = true;
        if (m.capability == "embeddings") embed = true;
        if (m.capability == "speechtotext") speech = m.engineId == "whisper-server";
    }
    EXPECT_TRUE(chat && embed && speech);

    // A bad document says what is wrong.
    EXPECT_TRUE(!ParseGallery("{\"formatVersion\": 2}", gallery, error));
    EXPECT_TRUE(error.find("formatVersion") != std::string::npos);
    EXPECT_TRUE(!ParseCatalogue("[]", catalogue, error));

    // Hugging Face tree replies.
    uint64_t size = 0;
    std::string sha;
    EXPECT_TRUE(ParseHuggingFaceTree(
        R"([{"type":"file","path":"a.gguf","size":5,"lfs":{"oid":"ABCDEF","size":12345}},{"type":"file","path":"README.md","size":9}])",
        "a.gguf", size, sha));
    EXPECT_EQ(size, uint64_t(12345));
    EXPECT_EQ(sha, std::string("abcdef"));
    EXPECT_TRUE(!ParseHuggingFaceTree("[]", "a.gguf", size, sha));
    EXPECT_EQ(HuggingFaceFileUrl("org/repo", "", "f.gguf"),
              std::string("https://huggingface.co/org/repo/resolve/main/f.gguf"));

    // Settings round-trip.
    LocalRuntimeSettings s;
    s.policy = DownloadPolicy::Automatic;
    s.preferredAccelerator = "vulkan";
    s.idleMinutes = 3;
    LocalRuntimeSettings back;
    EXPECT_TRUE(ParseSettings(SerializeSettings(s), back));
    EXPECT_TRUE(back.policy == DownloadPolicy::Automatic);
    EXPECT_EQ(back.preferredAccelerator, std::string("vulkan"));
    EXPECT_EQ(back.idleMinutes, 3);
}

void TestFitAndBundles() {
    auto hw = TestHardware();
    LocalModelInfo small;
    small.sizeBytes = 2ull << 30;
    EXPECT_TRUE(ComputeFit(small, *hw) == ModelFit::Recommended);
    LocalModelInfo cpuOnly;
    cpuOnly.sizeBytes = 8ull << 30;           // 1.2 x 8 GB > 70 % of 8 GB VRAM, < 70 % of 16 GB
    EXPECT_TRUE(ComputeFit(cpuOnly, *hw) == ModelFit::RunsOnCpu);
    LocalModelInfo huge;
    huge.memoryBytes = 64ull << 30;
    EXPECT_TRUE(ComputeFit(huge, *hw) == ModelFit::TooLarge);
    LocalModelInfo unknown;
    EXPECT_TRUE(ComputeFit(unknown, *hw) == ModelFit::Unknown);

    // Accelerators: a GPU on Linux / Windows means vulkan first, cuda
    // offered for NVIDIA, cpu last; macOS means metal.
    if (hw->platform.rfind("macos", 0) == 0) {
        EXPECT_EQ(hw->accelerators.front(), std::string("metal"));
    } else {
        EXPECT_EQ(hw->accelerators.front(), std::string("vulkan"));
        EXPECT_TRUE(std::find(hw->accelerators.begin(), hw->accelerators.end(), "cuda12") != hw->accelerators.end());
    }
    EXPECT_EQ(hw->accelerators.back(), std::string("cpu"));

    EngineInfo engine;
    engine.id = "llama-server";
    for (const char* accel : {"cpu", "vulkan", "cuda12"}) {
        EngineBundleInfo b;
        b.engineId = engine.id;
        b.platform = "linux-x64";
        b.accelerator = accel;
        b.url = std::string("https://example.invalid/") + accel;
        b.optional = std::string(accel) == "cuda12";
        engine.bundles.push_back(b);
    }
    EngineBundleInfo mac;
    mac.engineId = engine.id; mac.platform = "macos-arm64"; mac.accelerator = "metal";
    mac.url = "https://example.invalid/metal"; mac.minOs = "13.3";
    engine.bundles.push_back(mac);

    HardwareProfile linuxGpu = *hw;
    linuxGpu.platform = "linux-x64";
    linuxGpu.DeriveAccelerators();
    EngineBundleInfo chosen;
    EXPECT_TRUE(ChooseBundleFor(engine, linuxGpu, "", chosen));
    EXPECT_EQ(chosen.accelerator, std::string("vulkan"));
    EXPECT_TRUE(ChooseBundleFor(engine, linuxGpu, "cuda12", chosen));
    EXPECT_EQ(chosen.accelerator, std::string("cuda12"));      // optional, but asked for
    EXPECT_TRUE(ChooseBundleFor(engine, linuxGpu, "cpu", chosen));
    EXPECT_EQ(chosen.accelerator, std::string("cpu"));
    HardwareProfile linuxNoGpu = linuxGpu;
    linuxNoGpu.gpus.clear();
    linuxNoGpu.DeriveAccelerators();
    EXPECT_TRUE(ChooseBundleFor(engine, linuxNoGpu, "", chosen));
    EXPECT_EQ(chosen.accelerator, std::string("cpu"));
    HardwareProfile other = linuxGpu;
    other.platform = "linux-riscv";
    EXPECT_TRUE(!ChooseBundleFor(engine, other, "", chosen));
    HardwareProfile macHw = linuxGpu;
    macHw.platform = "macos-arm64";
    macHw.DeriveAccelerators();
    EXPECT_TRUE(ChooseBundleFor(engine, macHw, "", chosen) == (CurrentMacOsVersion().empty() ||
                                                              VersionAtLeast(CurrentMacOsVersion(), "13.3")));
    EXPECT_TRUE(VersionAtLeast("14.0", "13.3"));
    EXPECT_TRUE(VersionAtLeast("13.3", "13.3"));
    EXPECT_TRUE(!VersionAtLeast("12.7.1", "13.3"));

    // The command line.
    LocalModelInfo model;
    model.id = "tiny";
    model.capability = "textllm";
    model.sizeBytes = 1 << 20;
    model.primaryFile = "/models/tiny/tiny.gguf";
    model.installedPath = "/models/tiny";
    model.engineArgs = {"--ctx-size", "2048"};
    LocalModelFile proj;
    proj.name = "mmproj-f16.gguf";
    model.files.push_back(proj);
    engine.defaultArgs = {"--jinja"};
    const std::vector<std::string> args = BuildEngineArgs(engine, model, "vulkan", 4321, "/root/runtime.key", linuxGpu);
    auto has = [&](const std::string& a, const std::string& b = "") {
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == a && (b.empty() || (i + 1 < args.size() && args[i + 1] == b))) return true;
        }
        return false;
    };
    EXPECT_TRUE(has("--host", "127.0.0.1"));
    EXPECT_TRUE(has("--port", "4321"));
    EXPECT_TRUE(has("--api-key-file", "/root/runtime.key"));
    EXPECT_TRUE(has("--model", "/models/tiny/tiny.gguf"));
    EXPECT_TRUE(has("--alias", "tiny"));
    EXPECT_TRUE(has("--mmproj", "/models/tiny/mmproj-f16.gguf"));
    EXPECT_TRUE(has("--n-gpu-layers", "999"));
    EXPECT_TRUE(has("--threads", "6"));
    EXPECT_TRUE(has("--jinja"));
    EXPECT_TRUE(has("--ctx-size", "2048"));
    EXPECT_TRUE(!has("--embeddings"));
    model.capability = "embeddings";
    const std::vector<std::string> embedArgs = BuildEngineArgs(engine, model, "cpu", 1, "k", linuxNoGpu);
    EXPECT_TRUE(std::find(embedArgs.begin(), embedArgs.end(), "--embeddings") != embedArgs.end());
    EXPECT_TRUE(std::find(embedArgs.begin(), embedArgs.end(), "999") == embedArgs.end());

    // whisper-server: no key, the OpenAI audio API's path, any language;
    // none of llama-server's options.
    EngineInfo whisper;
    whisper.id = "whisper-server";
    LocalModelInfo speech;
    speech.id = "whisper-small";
    speech.capability = "speechtotext";
    speech.primaryFile = "/models/whisper-small/ggml-small.bin";
    const std::vector<std::string> whisperArgs = BuildEngineArgs(whisper, speech, "cpu", 4322, "/root/runtime.key", linuxGpu);
    auto hasIn = [](const std::vector<std::string>& list, const std::string& a, const std::string& b) {
        for (size_t i = 0; i + 1 < list.size(); ++i) {
            if (list[i] == a && list[i + 1] == b) return true;
        }
        return false;
    };
    EXPECT_TRUE(hasIn(whisperArgs, "--host", "127.0.0.1"));
    EXPECT_TRUE(hasIn(whisperArgs, "--port", "4322"));
    EXPECT_TRUE(hasIn(whisperArgs, "--model", "/models/whisper-small/ggml-small.bin"));
    EXPECT_TRUE(hasIn(whisperArgs, "--inference-path", "/v1/audio/transcriptions"));
    EXPECT_TRUE(hasIn(whisperArgs, "--language", "auto"));
    EXPECT_TRUE(hasIn(whisperArgs, "--threads", "6"));
    for (const char* llamaOnly : {"--api-key-file", "--alias", "--n-gpu-layers", "--embeddings"}) {
        EXPECT_TRUE(std::find(whisperArgs.begin(), whisperArgs.end(), llamaOnly) == whisperArgs.end());
    }

    EXPECT_EQ(ModelFolderName("hf:org/repo:file.gguf"), std::string("hf_org_repo_file.gguf"));
    EXPECT_EQ(ModelFolderName("qwen3-4b-q4"), std::string("qwen3-4b-q4"));
}

// ----- a runtime on a scratch root -----

struct Fixture {
    fs::path root;
    fs::path modelSource;
    fs::path bundleZip;
    std::string modelSha, bundleSha;
    uint64_t modelSize = 0, bundleSize = 0;
    // False where VirtualFS has no archive reader (a build without
    // libarchive): the engine then comes from the override, never from a
    // bundle, and the bundle-install checks are skipped.
    bool haveArchives = true;
    CopyFetcher fetcher;

    Fixture(const std::string& name) {
        root = g_scratch / PathFromUtf8(name);
        fs::remove_all(root);
        fs::create_directories(root);
        modelSource = root / "src" / "tiny.gguf";
        WriteFile(modelSource, "GGUF test weights " + name);
        modelSha  = Sha(modelSource);
        modelSize = fs::file_size(modelSource);
        // The bundle: a folder holding the fake engine as "llama-server".
        const fs::path stage = root / "stage" / "llama-test";
        fs::create_directories(stage);
        fs::copy_file(PathFromUtf8(g_fakeEngine), stage / PathFromUtf8(FindExecutableName()), fs::copy_options::overwrite_existing);
        WriteFile(stage / "LICENSE", "MIT");
        bundleZip = root / "src" / "bundle.zip";
        fs::create_directories(bundleZip.parent_path());
        if (!VirtualFS::VirtualFSManager::Instance().IsInitialized()) {
            VirtualFS::VirtualFSManager::Instance().Initialize();
        }
        const VirtualFS::VirtualFSResult r = VirtualFS::VirtualFS_CreateArchive(
            PathToUtf8(bundleZip),
            {PathToUtf8(stage / PathFromUtf8(FindExecutableName())), PathToUtf8(stage / "LICENSE")});
        if (r != VirtualFS::VirtualFSResult::Ok ||
            UltraCanvas::GetEnvUtf8("ULTRAAI_TEST_NO_ARCHIVES") == "1") {
            std::cerr << "bundle archive not available (" << VirtualFS::VirtualFSResultToString(r)
                      << "): engine-bundle checks skipped, the engine comes from the override\n";
            haveArchives = false;
            bundleSha    = "0000000000000000000000000000000000000000000000000000000000000000";
            bundleSize   = 1;
        } else {
            bundleSha  = Sha(bundleZip);
            bundleSize = fs::file_size(bundleZip);
        }
        fetcher.sources["test://model"]  = modelSource;
        fetcher.sources["test://bundle.zip"] = bundleZip;
    }

    static std::string FindExecutableName() {
#if defined(_WIN32)
        return "llama-server.exe";
#else
        return "llama-server";
#endif
    }

    LocalRuntimeOptions Options(bool overrideEngine) {
        LocalRuntimeOptions o;
        o.rootFolder    = PathToUtf8(root / "runtime");
        o.hardware      = TestHardware();
        o.fetch         = fetcher.Fn();
        o.galleryJson   = TestGalleryJson("test://bundle.zip", bundleSha, bundleSize);
        o.catalogueJson = TestCatalogueJson("test://model", modelSha, modelSize);
#ifdef ULTRAAI_HAS_ULTRANET
        o.transport = std::make_shared<UltraNetTransport>();
#endif
        if (overrideEngine || !haveArchives) o.engineOverrides["llama-server"] = g_fakeEngine;
        return o;
    }
};

void TestInstall() {
    Fixture fx("install");
    LocalRuntime rt(fx.Options(/*overrideEngine=*/false));

    // Before: nothing installed, the catalogue visible with its fit.
    std::vector<LocalModelInfo> models = rt.ListModels("textllm");
    EXPECT_TRUE(models.size() >= 3);
    LocalModelInfo tiny;
    EXPECT_TRUE(rt.FindModel("tiny", tiny));
    EXPECT_TRUE(!tiny.installed);
    // It fits the GPU, but the test gallery has a CPU build only: it runs on
    // the CPU - unless the engine is an override, which may use the GPU.
    EXPECT_TRUE(tiny.fit == (fx.haveArchives ? ModelFit::RunsOnCpu : ModelFit::Recommended));
    LocalModelInfo huge;
    EXPECT_TRUE(rt.FindModel("huge", huge));
    EXPECT_TRUE(huge.fit == ModelFit::TooLarge);
    EXPECT_EQ(rt.Recommend("textllm").id, std::string("tiny"));
    std::vector<EngineInfo> engines = rt.ListEngines();
    EXPECT_EQ(engines.size(), size_t(1));
    EXPECT_TRUE(!engines[0].bundles[0].installed);
    EXPECT_TRUE(engines[0].bundles[0].recommended);

    // Install: the engine comes along, progress runs through its states.
    std::vector<InstallProgress::State> states;
    std::string what;
    const std::string job = rt.InstallModel("tiny", [&](const InstallProgress& p) {
        states.push_back(p.state);
        if (p.state == InstallProgress::State::Done) what = p.what;
        if (p.state == InstallProgress::State::Failed) std::cerr << "install failed: " << p.error << "\n";
    });
    EXPECT_TRUE(!job.empty());
    rt.WaitForJobs();
    EXPECT_TRUE(!states.empty());
    EXPECT_TRUE(states.back() == InstallProgress::State::Done);
    EXPECT_TRUE(std::find(states.begin(), states.end(), InstallProgress::State::Downloading) != states.end());
    EXPECT_EQ(what, std::string("Tiny test model"));
    EXPECT_EQ(fx.fetcher.calls.size(), fx.haveArchives ? size_t(2) : size_t(1));
    EXPECT_TRUE(!fx.fetcher.calls[0].second);          // no resume on a first download
    if (fx.haveArchives) {
        EXPECT_TRUE(std::find(states.begin(), states.end(), InstallProgress::State::Unpacking) != states.end());
    }

    EXPECT_TRUE(rt.FindModel("tiny", tiny));
    EXPECT_TRUE(tiny.installed);
    EXPECT_TRUE(fs::is_regular_file(PathFromUtf8(tiny.primaryFile)));
    EXPECT_TRUE(fs::is_regular_file(PathFromUtf8(tiny.installedPath) / "model.json"));
    if (fx.haveArchives) {
        engines = rt.ListEngines();
        EXPECT_TRUE(engines[0].bundles[0].installed);
        EXPECT_TRUE(!FindExecutableIn(PathFromUtf8(engines[0].bundles[0].installedPath), "llama-server").empty());
        EXPECT_TRUE(fs::is_regular_file(PathFromUtf8(engines[0].bundles[0].installedPath) / "bundle.json"));
    }
    EXPECT_TRUE(rt.DiskUsedBytes() > 0);
    // No .part is left behind.
    int parts = 0;
    for (const auto& e : fs::directory_iterator(fx.root / "runtime" / "downloads")) { (void)e; ++parts; }
    EXPECT_EQ(parts, 0);

    // Installing again is a no-op that completes at once.
    fx.fetcher.calls.clear();
    rt.InstallModel("tiny", nullptr);
    rt.WaitForJobs();
    EXPECT_EQ(fx.fetcher.calls.size(), size_t(0));

    // A wrong hash is refused and the download discarded.
    std::string failure;
    rt.InstallModel("wrong-hash", [&](const InstallProgress& p) {
        if (p.state == InstallProgress::State::Failed) failure = p.error;
    });
    rt.WaitForJobs();
    EXPECT_TRUE(failure.find("SHA-256") != std::string::npos);
    LocalModelInfo wrong;
    EXPECT_TRUE(rt.FindModel("wrong-hash", wrong));
    EXPECT_TRUE(!wrong.installed);

    // A failed transfer reports the network error; the next try resumes.
    fx.fetcher.failNext = 1;
    fx.fetcher.calls.clear();
    rt.InstallModel("tiny-embed", [&](const InstallProgress& p) {
        if (p.state == InstallProgress::State::Failed) failure = p.error;
    });
    rt.WaitForJobs();
    EXPECT_TRUE(failure.find("simulated") != std::string::npos);
    rt.InstallModel("tiny-embed", nullptr);
    rt.WaitForJobs();
    LocalModelInfo embed;
    EXPECT_TRUE(rt.FindModel("tiny-embed", embed));
    EXPECT_TRUE(embed.installed);

    // Remove.
    EXPECT_TRUE(rt.RemoveModel("tiny-embed").IsOk());
    EXPECT_TRUE(rt.FindModel("tiny-embed", embed));
    EXPECT_TRUE(!embed.installed);

    // A user's own file, in place.
    std::string fileId;
    EXPECT_TRUE(rt.AddModelFile(PathToUtf8(fx.modelSource), "textllm", "My GGUF", fileId).IsOk());
    LocalModelInfo mine;
    EXPECT_TRUE(rt.FindModel(fileId, mine));
    EXPECT_TRUE(mine.installed);
    EXPECT_TRUE(mine.source == LocalModelSource::File);
    EXPECT_EQ(mine.primaryFile, PathToUtf8(fx.modelSource));
    EXPECT_TRUE(rt.ForgetModel(fileId).IsOk());
    EXPECT_TRUE(!rt.FindModel(fileId, mine));
    EXPECT_TRUE(fs::is_regular_file(fx.modelSource));   // the user's file stays

    // Settings persist.
    LocalRuntimeSettings s = rt.GetSettings();
    s.policy = DownloadPolicy::Automatic;
    rt.SetSettings(s);
    LocalRuntime again(fx.Options(false));
    EXPECT_TRUE(again.GetSettings().policy == DownloadPolicy::Automatic);
}

void TestEnsureAndSupervision() {
    Fixture fx("ensure");
    LocalRuntime rt(fx.Options(/*overrideEngine=*/true));

    // Ask policy, no permission: the dialog's cue.
    EngineEndpoint endpoint;
    Error error = rt.Ensure("tiny", endpoint, false);
    EXPECT_TRUE(error.code == ErrorCode::ModelNotFound);
    EXPECT_EQ(error.providerCode, std::string("needs-install"));
    // Too large is refused before anything is downloaded.
    error = rt.Ensure("huge", endpoint, true);
    EXPECT_EQ(error.providerCode, std::string("too-large"));
    // Never: told where to install from.
    LocalRuntimeSettings s = rt.GetSettings();
    s.policy = DownloadPolicy::Never;
    rt.SetSettings(s);
    error = rt.Ensure("tiny", endpoint, true);
    EXPECT_EQ(error.providerCode, std::string("needs-install"));
    EXPECT_TRUE(error.message.find("settings") != std::string::npos);

    // With permission: the model installs (the engine is the override,
    // so nothing downloads for it) and the fake engine starts.
    s.policy = DownloadPolicy::Ask;
    rt.SetSettings(s);
    error = rt.Ensure("tiny", endpoint, true);
    EXPECT_TRUE(error.IsOk());
    EXPECT_TRUE(endpoint.baseUrl.rfind("http://127.0.0.1:", 0) == 0);
    EXPECT_EQ(endpoint.apiKey.size(), size_t(64));
    EXPECT_EQ(endpoint.modelAlias, std::string("tiny"));
    EXPECT_TRUE(endpoint.processId != 0);
    EXPECT_TRUE(IsEngineProcessAlive(endpoint.processId));
    EXPECT_EQ(fx.fetcher.calls.size(), size_t(1));       // the model only

    std::vector<RunningEngineInfo> running = rt.ListRunning();
    EXPECT_EQ(running.size(), size_t(1));
    EXPECT_EQ(running[0].modelId, std::string("tiny"));
    EXPECT_TRUE(running[0].ownedByThisProcess);
    LocalModelInfo tiny;
    EXPECT_TRUE(rt.FindModel("tiny", tiny));
    EXPECT_TRUE(tiny.running);

    // A second Ensure finds the same engine - also from a second runtime
    // on the same root, as another application would.
    EngineEndpoint second;
    EXPECT_TRUE(rt.Ensure("tiny", second, false).IsOk());
    EXPECT_EQ(second.processId, endpoint.processId);
    {
        LocalRuntime other(fx.Options(true));
        EngineEndpoint third;
        EXPECT_TRUE(other.Ensure("tiny", third, false).IsOk());
        EXPECT_EQ(third.processId, endpoint.processId);
        EXPECT_EQ(third.baseUrl, endpoint.baseUrl);
        EXPECT_EQ(other.ListRunning().size(), size_t(1));
        EXPECT_TRUE(other.ListRunning()[0].ownedByThisProcess);   // same process, two runtimes
    }

    // The engine answers on its endpoint with the key, and refuses without.
#ifdef ULTRAAI_HAS_ULTRANET
    {
        UltraNetTransport transport;
        TransportRequest req;
        req.method = "GET";
        req.url    = endpoint.baseUrl + "/v1/models";
        req.timeoutMs = 5000;
        Error e;
        TransportResponse resp = transport.Request(req, &e);
        EXPECT_TRUE(e.IsOk());
        EXPECT_EQ(resp.statusCode, 401);
        req.headers.push_back({"authorization", "Bearer " + endpoint.apiKey});
        resp = transport.Request(req, &e);
        EXPECT_EQ(resp.statusCode, 200);
        EXPECT_TRUE(resp.body.find("\"tiny\"") != std::string::npos);
    }
#endif

    // Idle: with the idle time at zero, a sweep a moment later stops it.
    s.idleMinutes = 0;
    rt.SetSettings(s);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    rt.StopIdle();
    EXPECT_TRUE(!IsEngineProcessAlive(endpoint.processId));
    EXPECT_EQ(rt.ListRunning().size(), size_t(0));

    // Started again on demand; StopModel ends it.
    EXPECT_TRUE(rt.Ensure("tiny", endpoint, false).IsOk());
    EXPECT_TRUE(IsEngineProcessAlive(endpoint.processId));
    rt.StopModel("tiny");
    EXPECT_TRUE(!IsEngineProcessAlive(endpoint.processId));

    // A model whose engine dies twice is marked; a stop clears the mark.
    s.idleMinutes = 15;
    rt.SetSettings(s);
    error = rt.Ensure("crash-me", endpoint, true);
    EXPECT_TRUE(error.code == ErrorCode::ProviderError);
    EXPECT_TRUE(error.message.find("asked to crash") != std::string::npos);   // the log's tail
    error = rt.Ensure("crash-me", endpoint, true);
    EXPECT_TRUE(error.code == ErrorCode::ProviderError);
    error = rt.Ensure("crash-me", endpoint, true);
    EXPECT_EQ(error.providerCode, std::string("start-failed"));
    rt.StopModel("crash-me");
    error = rt.Ensure("crash-me", endpoint, true);
    EXPECT_TRUE(error.providerCode != "start-failed");

    rt.StopAll();
    EXPECT_EQ(rt.ListRunning().size(), size_t(0));
}

void TestLocalProvider() {
#ifdef ULTRAAI_HAS_ULTRANET
    Fixture fx("provider");
    LocalRuntimeOptions options = fx.Options(true);
    LocalRuntime rt(options);
    LocalRuntimeSettings s = rt.GetSettings();
    s.policy = DownloadPolicy::Automatic;
    rt.SetSettings(s);

    TextLLMConfig cfg;
    cfg.providerId = "local";          // defaultModel empty: the recommendation
    Error error;
    auto llm = CreateLocalTextLLM(cfg, &error, &rt, std::make_shared<UltraNetTransport>());
    EXPECT_TRUE(llm != nullptr);
    ProviderCapabilities caps = llm->GetCapabilities();
    EXPECT_EQ(caps.providerId, std::string("local"));
    EXPECT_TRUE(!caps.models.empty());
    EXPECT_TRUE(caps.models.front().runsLocally);
    bool offered = false;
    for (const auto& m : caps.models) {
        if (m.id == "tiny" && m.displayName.find("not installed") != std::string::npos) offered = true;
    }
    EXPECT_TRUE(offered);

    ChatRequest req;
    Message m;
    m.role = Role::User;
    m.text = "hello engine";
    req.messages.push_back(m);
    ChatResponse resp = llm->Chat(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.text, std::string("echo: hello engine"));
    EXPECT_TRUE(llm->RawProvider() != nullptr);
    caps = llm->GetCapabilities();
    EXPECT_EQ(caps.models.front().id, std::string("tiny"));     // installed now, listed first
    EXPECT_TRUE(caps.models.front().displayName.find("not installed") == std::string::npos);

    EmbeddingsConfig ecfg;
    ecfg.providerId   = "local";
    ecfg.defaultModel = "tiny-embed";
    auto embeddings = CreateLocalEmbeddings(ecfg, &error, &rt, std::make_shared<UltraNetTransport>());
    EmbeddingRequest ereq;
    ereq.input = {"a", "b"};
    EmbeddingResponse eresp = embeddings->Embed(ereq);
    EXPECT_TRUE(eresp.error.IsOk());
    EXPECT_EQ(eresp.embeddings.size(), size_t(2));
    EXPECT_EQ(eresp.embeddings[0].values.size(), size_t(3));
    EXPECT_EQ(rt.ListRunning().size(), size_t(2));

    // Under Ask without permission a fresh model is the dialog's cue.
    s.policy = DownloadPolicy::Ask;
    rt.SetSettings(s);
    rt.RemoveModel("tiny");
    TextLLMConfig cfg2;
    cfg2.providerId   = "local";
    cfg2.defaultModel = "tiny";
    auto llm2 = CreateLocalTextLLM(cfg2, &error, &rt, std::make_shared<UltraNetTransport>());
    resp = llm2->Chat(req);
    EXPECT_TRUE(resp.error.code == ErrorCode::ModelNotFound);
    EXPECT_EQ(resp.error.providerCode, std::string("needs-install"));
    // allow_download is the dialog's answer.
    cfg2.providerOptions["allow_download"] = true;
    auto llm3 = CreateLocalTextLLM(cfg2, &error, &rt, std::make_shared<UltraNetTransport>());
    resp = llm3->Chat(req);
    EXPECT_TRUE(resp.error.IsOk());

    rt.StopAll();
#endif
}

// Speech to text on the local runtime: a whisper-server (the fake engine)
// for this platform and one built only for another; a speech model on each.
std::string SpeechGalleryJson() {
    const std::string zeros(64, '0');
    return R"({"formatVersion": 1, "published": "2026-11-10", "engines": [
      {"id": "whisper-server", "version": "test", "license": "MIT", "executable": "whisper-server",
       "serves": ["speechtotext"], "bundles": [{"platform": ")" + HardwareProfile::CurrentPlatform() +
           R"(", "accel": "cpu", "url": "test://whisper.zip", "sha256": ")" + zeros + R"(", "size": 1}]},
      {"id": "elsewhere-server", "version": "test", "license": "MIT", "executable": "elsewhere-server",
       "serves": ["speechtotext"], "bundles": [{"platform": "linux-riscv", "accel": "cpu",
           "url": "test://elsewhere.zip", "sha256": ")" + zeros + R"(", "size": 1}]}]})";
}

std::string SpeechCatalogueJson(const std::string& modelUrl, const std::string& sha, uint64_t size) {
    auto model = [&](const std::string& id, const std::string& engine) {
        return R"({"id": ")" + id + R"(", "displayName": ")" + id + R"(", "capability": "speechtotext",
                   "engine": ")" + engine + R"(", "license": "MIT",
                   "files": [{"name": "ggml-tiny.bin", "url": ")" + modelUrl + R"(", "sha256": ")" + sha +
               R"(", "size": )" + std::to_string(size) + R"(}],
                   "requires": {"memoryBytes": 1000000}, "tags": ["recommended"]})";
    };
    return R"({"formatVersion": 1, "published": "2026-11-10", "models": [)" +
           model("elsewhere-whisper", "elsewhere-server") + ", " + model("tiny-whisper", "whisper-server") + "]}";
}

void TestLocalSpeech() {
    Fixture fx("speech");
    LocalRuntimeOptions options = fx.Options(true);
    options.galleryJson   = SpeechGalleryJson();
    options.catalogueJson = SpeechCatalogueJson("test://model", fx.modelSha, fx.modelSize);
    options.engineOverrides.clear();
    {
        // The model whose engine has no build here is not offered (it stays
        // known by its id); the other runs on the CPU whatever the GPU, as
        // its engine has CPU builds only.
        LocalRuntime gallery(options);
        const std::vector<LocalModelInfo> offered = gallery.ListModels("speechtotext");
        EXPECT_EQ(offered.size(), size_t(1));
        EXPECT_EQ(offered.front().id, std::string("tiny-whisper"));
        EXPECT_TRUE(offered.front().fit == ModelFit::RunsOnCpu);
        EXPECT_EQ(gallery.Recommend("speechtotext").id, std::string("tiny-whisper"));
        LocalModelInfo elsewhere;
        EXPECT_TRUE(gallery.FindModel("elsewhere-whisper", elsewhere));
    }
    // From here the engine is the fake one.
    options.engineOverrides["whisper-server"] = g_fakeEngine;
    LocalRuntime rt(options);

    // A user's own whisper model is served by whisper-server.
    std::string added;
    EXPECT_TRUE(rt.AddModelFile(PathToUtf8(fx.modelSource), "speechtotext", "Mine", added).IsOk());
    LocalModelInfo mine;
    EXPECT_TRUE(rt.FindModel(added, mine));
    EXPECT_EQ(mine.engineId, std::string("whisper-server"));
    EXPECT_TRUE(rt.ForgetModel(added).IsOk());
    EXPECT_TRUE(!rt.AddModelFile(PathToUtf8(fx.modelSource), "imagegen", "", added).IsOk());

#ifdef ULTRAAI_HAS_ULTRANET
    LocalRuntimeSettings s = rt.GetSettings();
    s.policy = DownloadPolicy::Automatic;
    rt.SetSettings(s);
    SpeechToTextConfig cfg;
    cfg.providerId = "local";
    Error error;
    auto stt = CreateLocalSpeechToText(cfg, &error, &rt, std::make_shared<UltraNetTransport>());
    EXPECT_TRUE(stt != nullptr);
    const STTProviderCapabilities caps = stt->GetCapabilities();
    EXPECT_EQ(caps.providerId, std::string("local"));
    EXPECT_EQ(caps.maxFileSizeBytes, 0);
    EXPECT_TRUE(std::find(caps.supportedEncodings.begin(), caps.supportedEncodings.end(), AudioEncoding::Ogg) !=
                caps.supportedEncodings.end());
    EXPECT_TRUE(std::find(caps.supportedEncodings.begin(), caps.supportedEncodings.end(), AudioEncoding::Opus) ==
                caps.supportedEncodings.end());
    EXPECT_EQ(caps.models.size(), size_t(1));
    EXPECT_TRUE(caps.models.front().runsLocally && caps.models.front().supportsTranslation);
    EXPECT_TRUE(caps.models.front().displayName.find("not installed") != std::string::npos);

    // The first call downloads the model and starts the engine; the server
    // is asked on its inference path, in any language.
    TranscribeRequest req;
    req.audio.bytes = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'};
    req.audio.mimeType = "audio/wav";
    req.audio.filename = "memo.wav";
    req.segmentTimestamps = false;
    TranscribeResponse resp = stt->Transcribe(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.text, std::string("heard memo.wav lang=auto translate=false"));
    const std::vector<RunningEngineInfo> running = rt.ListRunning();
    EXPECT_EQ(running.size(), size_t(1));
    EXPECT_EQ(running.front().engineId, std::string("whisper-server"));
    EXPECT_EQ(running.front().modelId, std::string("tiny-whisper"));

    // Translation is whisper's own flag on the same path; the language goes
    // as its primary subtag.
    req.language = "de-CH";
    req.translateToEnglish = true;
    resp = stt->Transcribe(req);
    EXPECT_TRUE(resp.error.IsOk());
    EXPECT_EQ(resp.text, std::string("heard memo.wav lang=de translate=true"));

    // Segments; the words only when asked for.
    req.translateToEnglish = false;
    req.segmentTimestamps = true;
    resp = stt->Transcribe(req);
    EXPECT_EQ(resp.segments.size(), size_t(1));
    EXPECT_TRUE(resp.segments.front().endSec == 2.0);
    EXPECT_TRUE(resp.segments.front().words.empty());
    req.wordTimestamps = true;
    resp = stt->Transcribe(req);
    EXPECT_EQ(resp.segments.front().words.size(), size_t(1));
    EXPECT_TRUE(resp.segments.front().words.front().confidence == 0.9);

    bool sawError = false;
    EXPECT_TRUE(stt->StartLiveTranscribe(req, [&sawError](const StreamingTranscriptEvent& ev) {
        sawError = ev.kind == StreamingTranscriptKind::Error;
    }) == nullptr);
    EXPECT_TRUE(sawError);
    rt.StopAll();
#endif
}

// With ULTRAAI_TEST_LIVE_GALLERY=1: install the built-in gallery's CPU
// bundle for this platform over the real network (UltraNet, the hash, the
// archive) and run the real llama-server once with --version. Skipped
// otherwise, so CI never downloads.
void TestLiveEngineInstall() {
    const std::string live = UltraCanvas::GetEnvUtf8("ULTRAAI_TEST_LIVE_GALLERY");
    if (live != "1") {
        std::cout << "live gallery install: skipped (ULTRAAI_TEST_LIVE_GALLERY is not 1)\n";
        return;
    }
    const fs::path root = g_scratch / "live";
    LocalRuntimeOptions options;
    options.rootFolder = PathToUtf8(root);
    options.hardware   = TestHardware(0);       // no GPU: the cpu bundle
    LocalRuntime rt(options);
    std::string failure;
    InstallProgress::State last = InstallProgress::State::Queued;
    rt.InstallEngine("llama-server", "cpu", [&](const InstallProgress& p) {
        last = p.state;
        if (p.state == InstallProgress::State::Failed) failure = p.error;
    });
    rt.WaitForJobs();
    EXPECT_TRUE(failure.empty());
    EXPECT_TRUE(last == InstallProgress::State::Done);
    std::string exe;
    for (const EngineInfo& e : rt.ListEngines()) {
        for (const EngineBundleInfo& b : e.bundles) {
            if (b.installed) exe = FindExecutableIn(PathFromUtf8(b.installedPath), e.executable);
        }
    }
    EXPECT_TRUE(!exe.empty());
    ProcessStart start;
    start.argv = {exe, "--version"};
    start.workingDirUtf8 = PathToUtf8(PathFromUtf8(exe).parent_path());
    start.logPathUtf8 = PathToUtf8(root / "version.log");
    uint64_t pid = 0;
    std::string error;
    EXPECT_TRUE(StartEngineProcess(start, pid, error));
    for (int i = 0; i < 100 && IsEngineProcessAlive(pid); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const std::string log = TailOfFile(root / "version.log", 5);
    std::cout << "live llama-server: " << log << "\n";
    EXPECT_TRUE(log.find("version") != std::string::npos);
}

void TestRegistration() {
    const std::vector<std::string> providers = ListTextLLMProviders();
    EXPECT_TRUE(std::find(providers.begin(), providers.end(), "local") != providers.end());
    const std::vector<std::string> embeddings = ListEmbeddingsProviders();
    EXPECT_TRUE(std::find(embeddings.begin(), embeddings.end(), "local") != embeddings.end());
    EXPECT_TRUE(IsLocalProvider("local"));
    ClearDefaultProviders();
    SetCloudFallbackAllowed(false);
    EXPECT_EQ(ResolveProviderId("textllm", {"mock", "qwen", "local", "llama-cpp"}), std::string("local"));
    EXPECT_EQ(ResolveProviderId("embeddings", {"openai", "local"}), std::string("local"));
    EXPECT_EQ(ResolveProviderId("translator", {"mock", "local"}), std::string("local"));
    const std::vector<std::string> speech = ListSpeechToTextProviders();
    EXPECT_TRUE(std::find(speech.begin(), speech.end(), "local") != speech.end());
    EXPECT_EQ(ResolveProviderId("speechtotext", {"openai", "whisper-cpp", "local"}), std::string("local"));
}

// ----- topics -----

void TestTopics() {
    // The built-in topics parse, and every model they name is in the
    // built-in catalogue with the topic's capability.
    Topics builtin;
    Catalogue catalogue;
    std::string error;
    EXPECT_TRUE(ParseTopics(BuiltinTopicsJson(), builtin, error));
    EXPECT_TRUE(ParseCatalogue(BuiltinCatalogueJson(), catalogue, error));
    EXPECT_TRUE(builtin.Find("chat") && builtin.Find("translation") && builtin.Find("search"));
    for (const ModelTopic& t : builtin.topics) {
        EXPECT_TRUE(!t.models.empty());
        for (const TopicModelRef& ref : t.models) {
            const LocalModelInfo* m = catalogue.Find(ref.modelId);
            EXPECT_TRUE(m != nullptr);
            EXPECT_EQ(m->capability, t.capability);
        }
    }

    // The resolution, on a hand-made inventory.
    ModelTopic topic;
    topic.id = "code";
    topic.capability = "textllm";
    topic.models = {{"big", false}, {"mid", true}, {"small", false}};
    auto model = [](const std::string& id, ModelFit fit, bool installed) {
        LocalModelInfo m;
        m.id = id; m.capability = "textllm"; m.fit = fit; m.installed = installed;
        return m;
    };
    std::vector<LocalModelInfo> inv = {model("big", ModelFit::TooLarge, false), model("mid", ModelFit::RunsOnCpu, false),
                                       model("small", ModelFit::Recommended, true), model("other", ModelFit::Recommended, true)};
    EXPECT_EQ(topic.ServerDefault(), std::string("mid"));
    // Following the server with downloads on: its default, to be installed.
    EXPECT_EQ(ResolveTopicModel(topic, nullptr, inv, "fb"), std::string("mid"));
    // Following without downloads: an installed model of the topic first.
    TopicChoice noInstall;
    noInstall.autoInstall = false;
    EXPECT_EQ(ResolveTopicModel(topic, &noInstall, inv, "fb"), std::string("small"));
    // The user's own pick, even outside the topic's list.
    TopicChoice pick;
    pick.modelId = "other";
    EXPECT_EQ(ResolveTopicModel(topic, &pick, inv, "fb"), std::string("other"));
    // A pick that does not fit, or does not exist, falls back.
    pick.modelId = "big";
    EXPECT_EQ(ResolveTopicModel(topic, &pick, inv, "fb"), std::string("small"));
    // Nothing usable: the recommendation.
    ModelTopic empty;
    empty.id = "x"; empty.capability = "textllm";
    EXPECT_EQ(ResolveTopicModel(empty, nullptr, inv, "fb"), std::string("fb"));

    // Preferences round-trip.
    TopicPreferences prefs;
    prefs.newModels = NewModelPolicy::Automatic;
    prefs.choices["code"].modelId = "small";
    prefs.choices["chat"].autoInstall = false;
    prefs.seenModels = {"a", "b"};
    TopicPreferences back;
    bool hadSeen = false;
    EXPECT_TRUE(ParseTopicPreferences(SerializeTopicPreferences(prefs), back, hadSeen));
    EXPECT_TRUE(hadSeen);
    EXPECT_TRUE(back.newModels == NewModelPolicy::Automatic);
    EXPECT_EQ(back.choices["code"].modelId, std::string("small"));
    EXPECT_TRUE(back.choices["code"].autoInstall);
    EXPECT_TRUE(!back.choices["chat"].autoInstall);
    EXPECT_EQ(back.seenModels.size(), size_t(2));

    // Through the runtime: a topics document of the test catalogue.
    Fixture fx("topics");
    LocalRuntimeOptions o = fx.Options(/*overrideEngine=*/true);
    o.topicsJson = R"({"formatVersion": 1, "published": "2026-11-06", "topics": [
        {"id": "chat", "name": "Chat", "capability": "textllm", "models": [{"id": "huge"}, {"id": "tiny", "default": true}]},
        {"id": "search", "name": "Search", "capability": "embeddings", "models": ["tiny-embed"]},
        {"id": "big", "name": "Big", "capability": "textllm", "models": ["huge"]}]})";
    {
        LocalRuntime rt(o);
        EXPECT_EQ(rt.ListTopics().size(), size_t(3));
        ModelTopic chat;
        EXPECT_TRUE(rt.FindTopic("chat", chat));
        EXPECT_EQ(chat.ServerDefault(), std::string("tiny"));
        EXPECT_EQ(rt.ModelForTopic("chat"), std::string("tiny"));
        EXPECT_EQ(rt.ModelForTopic("search"), std::string("tiny-embed"));
        // A topic whose only model is too large: the recommendation.
        EXPECT_EQ(rt.ModelForTopic("big"), rt.Recommend("textllm").id);
        EXPECT_EQ(rt.ModelForTopic("nope"), std::string());
        // The first start counts the whole catalogue as seen.
        EXPECT_TRUE(rt.NewModels().empty());

        // The user follows chat but not search: one install, for chat.
        TopicPreferences p = rt.GetTopicPreferences();
        p.choices["search"].autoInstall = false;
        rt.SetTopicPreferences(p);
        LocalRuntimeSettings s = rt.GetSettings();
        s.policy = DownloadPolicy::Never;
        rt.SetSettings(s);
        EXPECT_TRUE(rt.ApplyTopicUpdates(nullptr).empty());     // Never: nothing
        s.policy = DownloadPolicy::Ask;
        rt.SetSettings(s);
        const std::vector<std::string> jobs = rt.ApplyTopicUpdates(nullptr);
        EXPECT_EQ(jobs.size(), size_t(1));
        rt.WaitForJobs();
        LocalModelInfo tiny;
        EXPECT_TRUE(rt.FindModel("tiny", tiny));
        EXPECT_TRUE(tiny.installed);
        LocalModelInfo embed;
        EXPECT_TRUE(rt.FindModel("tiny-embed", embed));
        EXPECT_TRUE(!embed.installed);
        EXPECT_TRUE(rt.ApplyTopicUpdates(nullptr).empty());     // nothing left to do

        // A fixed pick for chat.
        rt.SetTopicModel("chat", "crash-me");
        EXPECT_EQ(rt.ModelForTopic("chat"), std::string("crash-me"));
        rt.SetTopicModel("chat", "");
        EXPECT_EQ(rt.ModelForTopic("chat"), std::string("tiny"));
    }
    // A model the catalogue gains later is new until it is marked seen.
    {
        TopicPreferences p;
        bool had = false;
        std::string text;
        EXPECT_TRUE(ReadWholeFile(PathFromUtf8(o.rootFolder) / "topic-preferences.json", text));
        EXPECT_TRUE(ParseTopicPreferences(text, p, had));
        EXPECT_TRUE(had);
        p.seenModels.erase(std::remove(p.seenModels.begin(), p.seenModels.end(), "tiny-embed"), p.seenModels.end());
        std::string werr;
        EXPECT_TRUE(WriteFileAtomic(PathFromUtf8(o.rootFolder) / "topic-preferences.json", SerializeTopicPreferences(p), werr));
        LocalRuntime rt(o);
        std::vector<LocalModelInfo> fresh = rt.NewModels();
        EXPECT_EQ(fresh.size(), size_t(1));
        EXPECT_EQ(fresh[0].id, std::string("tiny-embed"));
        // The settings survived the restart.
        EXPECT_TRUE(!rt.GetTopicPreferences().choices["search"].autoInstall);
        rt.MarkModelsSeen({"tiny-embed"});
        EXPECT_TRUE(rt.NewModels().empty());
        // Ignore hides them altogether.
        TopicPreferences q = rt.GetTopicPreferences();
        q.seenModels.clear();
        q.newModels = NewModelPolicy::Ignore;
        rt.SetTopicPreferences(q);
        EXPECT_TRUE(rt.NewModels().empty());
    }
}

// ----- signed galleries -----

// A transport that answers by URL, for RefreshGallery.
class UrlTransport : public ITransport {
public:
    std::map<std::string, TransportResponse> pages;
    std::vector<std::string> asked;
    TransportResponse Request(const TransportRequest& request, Error* outError) override {
        asked.push_back(request.url);
        auto it = pages.find(request.url);
        if (it != pages.end()) return it->second;
        TransportResponse r;
        r.statusCode = 404;
        (void)outError;
        return r;
    }
    CancelFn SseStream(const TransportRequest&, SseEventCallback, SseCompleteCallback onComplete) override {
        if (onComplete) onComplete(Error{ErrorCode::ProviderError, "no streams here", ""}, 0);
        return [] {};
    }
};

std::string Hex(const std::vector<uint8_t>& bytes) {
    std::string out = UltraCrypt_ToHex(bytes);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

struct Signer {
    std::vector<uint8_t> pub;
    UltraCryptSecureBuffer secret;
    Signer() { EXPECT_TRUE(UltraCrypt_GenerateSignatureKeyPair(UltraCryptSignatureAlgorithm::Ed25519, pub, secret).success); }
    std::string Sign(const std::string& bytes) const {
        std::vector<uint8_t> sig;
        EXPECT_TRUE(UltraCrypt_Sign(UltraCryptSignatureAlgorithm::Ed25519, secret, bytes.data(), bytes.size(), sig).success);
        return Hex(sig);
    }
    std::string PublicHex() const { return Hex(pub); }
};

TransportResponse Page(const std::string& body, const std::string& signature = {}) {
    TransportResponse r;
    r.statusCode = 200;
    r.body = body;
    if (!signature.empty()) r.headers.push_back({"X-UltraAI-Signature", signature});
    return r;
}

void TestSignedGallery() {
    if (!UltraCrypt_IsSignatureAvailable(UltraCryptSignatureAlgorithm::Ed25519)) {
        std::cerr << "signed gallery: skipped (no Ed25519 in this build)\n";
        return;
    }
    const std::string base = "https://models.test/gallery/";
    const std::string index = BuiltinGalleryJson();
    const std::string catalogue = BuiltinCatalogueJson();
    const std::string topicsV1 = BuiltinTopicsJson();
    std::string topicsV2 = topicsV1;
    topicsV2.replace(topicsV2.find("General chat"), 12, "Chat, signed");
    Signer key, other;

    // VerifyDocumentSignature itself.
    EXPECT_TRUE(VerifyDocumentSignature("abc", key.Sign("abc"), {other.PublicHex(), key.PublicHex()}));
    EXPECT_TRUE(!VerifyDocumentSignature("abd", key.Sign("abc"), {key.PublicHex()}));
    EXPECT_TRUE(!VerifyDocumentSignature("abc", key.Sign("abc"), {other.PublicHex()}));
    EXPECT_TRUE(!VerifyDocumentSignature("abc", "zz", {key.PublicHex()}));
    std::vector<std::string> parsed;
    EXPECT_TRUE(ParseGalleryKeys(R"({"formatVersion":1,"algorithm":"ed25519","keys":[")" + key.PublicHex() + R"("]})", parsed));
    EXPECT_EQ(parsed.size(), size_t(1));
    EXPECT_TRUE(!ParseGalleryKeys(R"({"formatVersion":1,"keys":["nothex"]})", parsed));

    Fixture fx("signed");
    auto transport = std::make_shared<UrlTransport>();
    transport->pages[base + "index.json"] = Page(index, key.Sign(index));
    transport->pages[base + "catalogue.json"] = Page(catalogue, key.Sign(catalogue));
    transport->pages[base + "topics.json"] = Page(topicsV1);                                   // signature in .sig
    transport->pages[base + "topics.json.sig"] = Page(key.Sign(topicsV1) + "\n");
    transport->pages[base + "keys.json"] = Page(R"({"formatVersion":1,"algorithm":"ed25519","keys":[")" + key.PublicHex() + R"("]})");
    LocalRuntimeOptions o = fx.Options(true);
    o.transport = transport;
    LocalRuntime rt(o);
    LocalRuntimeSettings s = rt.GetSettings();
    s.galleryUrl = base + "index.json";
    rt.SetSettings(s);

    // First refresh: keys.json pinned, everything verified.
    Error e = rt.RefreshGallery();
    if (!e.IsOk()) std::cerr << "refresh: " << e.message << "\n";
    EXPECT_TRUE(e.IsOk());
    EXPECT_EQ(rt.GetSettings().trustedKeys.size(), size_t(1));
    EXPECT_EQ(rt.GetSettings().trustedKeys[0], key.PublicHex());

    // A tampered topics document is refused, and nothing changes.
    transport->pages[base + "topics.json"] = Page(topicsV2);
    e = rt.RefreshGallery();
    EXPECT_TRUE(!e.IsOk());
    EXPECT_EQ(e.providerCode, std::string("bad-signature"));
    ModelTopic chat;
    EXPECT_TRUE(rt.FindTopic("chat", chat));
    EXPECT_EQ(chat.name, std::string("General chat"));

    // Signed with another key - even one keys.json now names - is refused:
    // the pinned key decides, not what the server says today.
    transport->pages[base + "topics.json.sig"] = Page(other.Sign(topicsV2));
    transport->pages[base + "keys.json"] = Page(R"({"formatVersion":1,"algorithm":"ed25519","keys":[")" + other.PublicHex() + R"("]})");
    EXPECT_EQ(rt.RefreshGallery().providerCode, std::string("bad-signature"));

    // Properly signed: taken.
    transport->pages[base + "topics.json.sig"] = Page(key.Sign(topicsV2));
    EXPECT_TRUE(rt.RefreshGallery().IsOk());
    EXPECT_TRUE(rt.FindTopic("chat", chat));
    EXPECT_EQ(chat.name, std::string("Chat, signed"));

    // An unsigned index under pinned keys is refused.
    transport->pages[base + "index.json"] = Page(index);
    EXPECT_EQ(rt.RefreshGallery().providerCode, std::string("bad-signature"));

    // Another server: the pins go with the old address.
    s = rt.GetSettings();
    s.galleryUrl = "https://other.test/gallery/index.json";
    rt.SetSettings(s);
    EXPECT_TRUE(rt.GetSettings().trustedKeys.empty());

    // A gallery that publishes no keys stays unsigned and works as before.
    const std::string plain = "https://mirror.test/";
    transport->pages[plain + "index.json"] = Page(index);
    transport->pages[plain + "catalogue.json"] = Page(catalogue);
    s = rt.GetSettings();
    s.galleryUrl = plain + "index.json";
    rt.SetSettings(s);
    EXPECT_TRUE(rt.RefreshGallery().IsOk());
    EXPECT_TRUE(rt.GetSettings().trustedKeys.empty());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: test_local_runtime <fake engine path>\n";
        return 2;
    }
    g_fakeEngine = argv[1];
    g_scratch = fs::temp_directory_path() / ("ultraai-local-runtime-" + std::to_string(ThisProcessId()));
    fs::create_directories(g_scratch);

    TestBuiltinData();
    TestFitAndBundles();
    TestRegistration();
    TestInstall();
    TestTopics();
    TestSignedGallery();
    TestEnsureAndSupervision();
    TestLocalProvider();
    TestLocalSpeech();
    TestLiveEngineInstall();

    std::error_code ec;
    fs::remove_all(g_scratch, ec);
    std::cout << "test_local_runtime: all passed\n";
    return 0;
}
