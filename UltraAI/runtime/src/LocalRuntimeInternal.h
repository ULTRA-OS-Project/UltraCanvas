// UltraAI/runtime/src/LocalRuntimeInternal.h
// The parts of the local runtime that are pure or platform-bound and are
// tested on their own: the gallery and catalogue formats, the hardware fit,
// the storage layout, files, hashing, archives and processes. The facade in
// LocalRuntime.cpp composes them.
// Version: 0.1.0
// Last Modified: 2026-10-20
// Author: UltraAI Module
#pragma once

#include "UltraAILocalRuntime.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace UltraAI {
namespace local_detail {

// ===== Gallery and catalogue =====

struct Gallery {
    int formatVersion = 0;
    std::string published;              // "2026-10-20"
    std::vector<EngineInfo> engines;
    const EngineInfo* Find(const std::string& engineId) const;
};

struct Catalogue {
    int formatVersion = 0;
    std::string published;
    std::vector<LocalModelInfo> models;
    const LocalModelInfo* Find(const std::string& modelId) const;
};

// Parse the JSON formats of Docs/Modules/UltraAI/LocalRuntime.md. False
// with a message naming the field when the document is not one.
bool ParseGallery(const std::string& json, Gallery& out, std::string& outError);
bool ParseCatalogue(const std::string& json, Catalogue& out, std::string& outError);
// Serialize user-added models (file: and hf: entries) and read them back.
std::string SerializeModels(const std::vector<LocalModelInfo>& models);
bool ParseModels(const std::string& json, std::vector<LocalModelInfo>& out, std::string& outError);
// The copies shipped with the framework.
const char* BuiltinGalleryJson();
const char* BuiltinCatalogueJson();
// The default gallery URL (its index.json).
const char* DefaultGalleryUrl();
// A Hugging Face "resolve" URL for a file.
std::string HuggingFaceFileUrl(const std::string& repo, const std::string& revision,
                               const std::string& file);
// The tree API URL that reports sizes and LFS hashes.
std::string HuggingFaceTreeUrl(const std::string& repo, const std::string& revision);
// Parse the tree API's reply for one file: its size and sha256 (the LFS
// oid). False when the file is not listed.
bool ParseHuggingFaceTree(const std::string& json, const std::string& file,
                          uint64_t& outSize, std::string& outSha256);
// Settings to and from JSON.
std::string SerializeSettings(const LocalRuntimeSettings& settings);
bool ParseSettings(const std::string& json, LocalRuntimeSettings& out);

// ===== Hardware fit =====

// How a model suits a machine, from its memory needs: Recommended when
// gpuMemoryBytes (or 1.2 x the files) fits 70 % of the largest GPU's
// dedicated memory, RunsOnCpu when memoryBytes (or 1.2 x the files) fits
// 70 % of system memory, TooLarge otherwise, Unknown without any size.
ModelFit ComputeFit(const LocalModelInfo& model, const HardwareProfile& hardware);
// The bundle Auto picks: the first of the machine's accelerators (or the
// preferred one, when it names one the gallery has) that the engine has a
// non-optional bundle for on this platform, honouring minOs on macOS.
// False when none.
bool ChooseBundleFor(const EngineInfo& engine, const HardwareProfile& hardware,
                     const std::string& preferredAccelerator, EngineBundleInfo& out);
// The GPU-offload and thread arguments for llama.cpp-family engines.
std::vector<std::string> OffloadArgs(const LocalModelInfo& model, const HardwareProfile& hardware,
                                     const std::string& accelerator);
// The whole command line for an engine start, without the executable.
std::vector<std::string> BuildEngineArgs(const EngineInfo& engine, const LocalModelInfo& model,
                                         const std::string& accelerator, int port,
                                         const std::string& apiKeyFileUtf8,
                                         const HardwareProfile& hardware);
// Bundle folder name: "<version>-<platform>-<accelerator>".
std::string BundleFolderName(const EngineBundleInfo& bundle);
// "linux-x64" and the rest from the macros of this build.
std::string CurrentPlatformId();
// The macOS version of this machine as "major.minor" ("" elsewhere).
std::string CurrentMacOsVersion();
// "14.0" <= "15.2" by numeric parts.
bool VersionAtLeast(const std::string& have, const std::string& need);

// ===== Storage =====

struct Paths {
    std::filesystem::path root;
    std::filesystem::path engines;
    std::filesystem::path models;
    std::filesystem::path downloads;
    std::filesystem::path logs;
    std::filesystem::path runtimeJson;
    std::filesystem::path runtimeLock;
    std::filesystem::path runtimeKey;
    std::filesystem::path settingsJson;
    std::filesystem::path galleryJson;
    std::filesystem::path catalogueJson;
    std::filesystem::path userModelsJson;

    static Paths For(const std::filesystem::path& root);
    // The default root: <LocalDataFolderRoot>/UltraAI.
    static std::filesystem::path DefaultRoot();
    bool EnsureFolders(std::string& outError) const;
};

// A folder name a model id maps to: "file:/x/y.gguf" and "hf:org/repo:f"
// become safe names; catalogue ids are kept.
std::string ModelFolderName(const std::string& modelId);

// Whole-file read and atomic write (temp file + rename) on UTF-8 paths.
bool ReadWholeFile(const std::filesystem::path& path, std::string& out);
bool WriteFileAtomic(const std::filesystem::path& path, const std::string& content,
                     std::string& outError);
// Bytes under a folder, recursively.
uint64_t FolderSizeBytes(const std::filesystem::path& folder);
// Seconds since the epoch.
int64_t NowUnix();

// ===== Hashing and archives =====

// SHA-256 of a file as lowercase hex, through UltraCrypt. False with a
// message when the backend is unavailable or the file unreadable.
bool Sha256File(const std::filesystem::path& path, std::string& outHex, std::string& outError);
// Unpack an archive (tar.gz, zip, ...) into a folder, through VirtualFS.
Error UnpackArchive(const std::filesystem::path& archive, const std::filesystem::path& dest);
// Find a file named `executable` (with ".exe" on Windows) under a folder,
// at any depth; "" when absent. Also accepts `folder` being the file.
std::string FindExecutableIn(const std::filesystem::path& folder, const std::string& executable);
// Mark the files of an unpacked bundle executable on POSIX (a zip made on
// Windows records no mode bits).
void MarkExecutables(const std::filesystem::path& folder);

// ===== Processes =====

struct ProcessStart {
    std::vector<std::string> argv;      // argv[0] is the program
    std::string workingDirUtf8;
    std::string logPathUtf8;            // stdout and stderr, appended
};
// Start a program that outlives this process (its own session / process
// group) and return its id. False with a message when it could not start.
bool StartEngineProcess(const ProcessStart& start, uint64_t& outPid, std::string& outError);
// Whether the process exists. Reaps a child of ours that has exited.
bool IsEngineProcessAlive(uint64_t pid);
// Ask the process to end (SIGTERM / a console control), wait up to
// `graceMs`, then end it (SIGKILL / TerminateProcess).
void StopEngineProcess(uint64_t pid, int graceMs);
// A free TCP port on 127.0.0.1; 0 when none could be found.
int FindFreeLoopbackPort();
// The id of this process.
uint64_t ThisProcessId();
// The last `maxLines` lines of a text file, for an error message.
std::string TailOfFile(const std::filesystem::path& path, int maxLines);

// A lock file shared between the processes that edit runtime.json. Created
// exclusively with this process's id inside; a lock whose holder is dead
// is taken over. Held for the object's lifetime.
class ScopedFileLock {
public:
    ScopedFileLock(const std::filesystem::path& path, int timeoutMs);
    ~ScopedFileLock();
    bool Held() const { return held_; }
private:
    std::filesystem::path path_;
    bool held_ = false;
};

// 32 random bytes as hex, through UltraCrypt (std::random_device when its
// backend is unavailable).
std::string RandomKeyHex();

} // namespace local_detail
} // namespace UltraAI
