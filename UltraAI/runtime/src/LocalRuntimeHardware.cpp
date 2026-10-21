// UltraAI/runtime/src/LocalRuntimeHardware.cpp
// This machine as the runtime sees it, and the pure decisions made from it:
// which bundle, how a model fits, what to put on the engine's command line.
// Version: 0.1.0
// Last Modified: 2026-10-20
// Author: UltraAI Module

#include "LocalRuntimeInternal.h"

#ifdef ULTRAAI_RUNTIME_HAS_HARDWARE_INFO
#include "UltraCanvasHardwareInfo.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <thread>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#endif

namespace UltraAI {

namespace local_detail {

std::string CurrentPlatformId() {
    std::string os;
#if defined(_WIN32)
    os = "win";
#elif defined(__APPLE__)
    os = "macos";
#elif defined(__linux__)
    os = "linux";
#else
    os = "other";
#endif
    std::string cpu;
#if defined(__x86_64__) || defined(_M_X64)
    cpu = "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    cpu = "arm64";
#else
    cpu = "other";
#endif
    return os + "-" + cpu;
}

std::string CurrentMacOsVersion() {
#if defined(__APPLE__)
    char buffer[64] = {};
    size_t size = sizeof(buffer);
    if (sysctlbyname("kern.osproductversion", buffer, &size, nullptr, 0) == 0) {
        return std::string(buffer);
    }
#endif
    return {};
}

bool VersionAtLeast(const std::string& have, const std::string& need) {
    if (need.empty()) return true;
    if (have.empty()) return false;
    auto parts = [](const std::string& v) {
        std::vector<int> out;
        std::stringstream in(v);
        std::string part;
        while (std::getline(in, part, '.')) {
            out.push_back(std::atoi(part.c_str()));
        }
        return out;
    };
    const std::vector<int> a = parts(have), b = parts(need);
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        const int x = i < a.size() ? a[i] : 0;
        const int y = i < b.size() ? b[i] : 0;
        if (x != y) return x > y;
    }
    return true;
}

namespace {

// 1.2 x the files, when no requirement is on record: weights plus the KV
// cache and the compute buffers at a modest context.
uint64_t NeedOr(uint64_t stated, uint64_t files) {
    if (stated) return stated;
    return files + files / 5;
}

uint64_t Budget(uint64_t total) { return total - total * 3 / 10; }   // 70 %

uint64_t TotalMemoryFallback() {
#if defined(__linux__)
    struct sysinfo info {};
    if (sysinfo(&info) == 0) return static_cast<uint64_t>(info.totalram) * info.mem_unit;
#elif defined(__APPLE__)
    uint64_t bytes = 0;
    size_t size = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0) return bytes;
#elif defined(_WIN32)
    MEMORYSTATUSEX status {};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) return status.ullTotalPhys;
#endif
    return 0;
}

} // namespace

ModelFit ComputeFit(const LocalModelInfo& model, const HardwareProfile& hardware) {
    if (!model.sizeBytes && !model.memoryBytes && !model.gpuMemoryBytes) return ModelFit::Unknown;
    // What the GPU would need: the stated figure, else 1.2 x the files,
    // else the system-memory figure (a model with no files on record).
    uint64_t gpuNeed = model.gpuMemoryBytes;
    if (!gpuNeed) gpuNeed = model.sizeBytes ? NeedOr(0, model.sizeBytes) : model.memoryBytes;
    uint64_t memoryNeed = model.memoryBytes;
    if (!memoryNeed) memoryNeed = model.sizeBytes ? NeedOr(0, model.sizeBytes) : model.gpuMemoryBytes;
    const uint64_t gpu = hardware.LargestGpuMemoryBytes();
    if (gpu && gpuNeed && gpuNeed <= Budget(gpu)) return ModelFit::Recommended;
    if (hardware.memoryBytes && memoryNeed <= Budget(hardware.memoryBytes)) return ModelFit::RunsOnCpu;
    if (!hardware.memoryBytes) return ModelFit::Unknown;
    return ModelFit::TooLarge;
}

bool ChooseBundleFor(const EngineInfo& engine, const HardwareProfile& hardware,
                     const std::string& preferredAccelerator, EngineBundleInfo& out) {
    const std::string macOs = CurrentMacOsVersion();
    auto usable = [&](const EngineBundleInfo& b) {
        if (b.platform != hardware.platform) return false;
        if (hardware.platform.rfind("macos", 0) == 0 && !b.minOs.empty() &&
            !macOs.empty() && !VersionAtLeast(macOs, b.minOs)) {
            return false;
        }
        return true;
    };
    std::vector<std::string> order;
    if (!preferredAccelerator.empty()) order.push_back(preferredAccelerator);
    for (const std::string& a : hardware.accelerators) {
        if (std::find(order.begin(), order.end(), a) == order.end()) order.push_back(a);
    }
    if (std::find(order.begin(), order.end(), "cpu") == order.end()) order.push_back("cpu");
    for (const std::string& accel : order) {
        for (const EngineBundleInfo& b : engine.bundles) {
            if (!usable(b) || b.accelerator != accel) continue;
            // An optional bundle (cuda, rocm) is only chosen when asked for.
            if (b.optional && accel != preferredAccelerator) continue;
            out = b;
            return true;
        }
    }
    return false;
}

std::vector<std::string> OffloadArgs(const LocalModelInfo& model, const HardwareProfile& hardware,
                                     const std::string& accelerator) {
    std::vector<std::string> args;
    if (accelerator != "cpu" && ComputeFit(model, hardware) == ModelFit::Recommended) {
        // Everything on the GPU. Otherwise the engine's own fitting decides
        // how many layers to offload.
        args.push_back("--n-gpu-layers");
        args.push_back("999");
    } else if (accelerator == "cpu") {
        args.push_back("--n-gpu-layers");
        args.push_back("0");
    }
    int threads = hardware.physicalCores > 0 ? hardware.physicalCores
                                             : hardware.logicalCores / 2;
    if (threads > 4) threads -= 2;      // leave the desktop two cores
    if (threads > 0) {
        args.push_back("--threads");
        args.push_back(std::to_string(threads));
    }
    return args;
}

std::vector<std::string> BuildEngineArgs(const EngineInfo& engine, const LocalModelInfo& model,
                                         const std::string& accelerator, int port,
                                         const std::string& apiKeyFileUtf8,
                                         const HardwareProfile& hardware) {
    std::vector<std::string> args = {
        "--host", "127.0.0.1",
        "--port", std::to_string(port),
        "--api-key-file", apiKeyFileUtf8,
    };
    if (!model.primaryFile.empty()) {
        args.push_back("--model");
        args.push_back(model.primaryFile);
    }
    if (engine.id == "llama-server") {
        args.push_back("--alias");
        args.push_back(model.id);
        // A multimodal projector beside the weights: "mmproj*.gguf".
        for (const auto& f : model.files) {
            if (f.name.rfind("mmproj", 0) == 0 && !model.installedPath.empty()) {
                args.push_back("--mmproj");
                args.push_back(model.installedPath + "/" + f.name);
                break;
            }
        }
        if (model.capability == "embeddings") {
            args.push_back("--embeddings");
        }
        for (const std::string& a : OffloadArgs(model, hardware, accelerator)) args.push_back(a);
    }
    for (const std::string& a : engine.defaultArgs) args.push_back(a);
    for (const std::string& a : model.engineArgs) args.push_back(a);
    return args;
}

std::string BundleFolderName(const EngineBundleInfo& bundle) {
    return bundle.version + "-" + bundle.platform + "-" + bundle.accelerator;
}

} // namespace local_detail

// ===== HardwareProfile =====

uint64_t HardwareProfile::LargestGpuMemoryBytes() const {
    uint64_t largest = 0;
    for (const Gpu& g : gpus) largest = std::max(largest, g.videoMemoryBytes);
    return largest;
}

std::string HardwareProfile::CurrentPlatform() {
    return local_detail::CurrentPlatformId();
}

void HardwareProfile::DeriveAccelerators() {
    accelerators.clear();
    const bool mac = platform.rfind("macos", 0) == 0;
    if (mac) {
        accelerators.push_back("metal");
    } else if (!gpus.empty()) {
        // Any GPU with a driver has Vulkan on a current Linux or Windows;
        // vendor toolkits are offered, never chosen.
        accelerators.push_back("vulkan");
        for (const Gpu& g : gpus) {
            std::string vendor = g.vendor;
            std::transform(vendor.begin(), vendor.end(), vendor.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (vendor.find("nvidia") != std::string::npos &&
                std::find(accelerators.begin(), accelerators.end(), "cuda12") == accelerators.end()) {
                accelerators.push_back("cuda12");
            }
            if ((vendor.find("amd") != std::string::npos || vendor.find("advanced micro") != std::string::npos) &&
                std::find(accelerators.begin(), accelerators.end(), "rocm") == accelerators.end()) {
                accelerators.push_back("rocm");
            }
        }
    }
    accelerators.push_back("cpu");
}

HardwareProfile HardwareProfile::Detect() {
    HardwareProfile hw;
    hw.platform = local_detail::CurrentPlatformId();
#ifdef ULTRAAI_RUNTIME_HAS_HARDWARE_INFO
    const UltraCanvas::CPUInfo cpu = UltraCanvas::UltraCanvasHardwareInfo::GetCPU();
    hw.physicalCores = cpu.physicalCores;
    hw.logicalCores  = cpu.logicalCores;
    hw.x86MicroarchitectureLevel = cpu.x86MicroarchitectureLevel;
    const UltraCanvas::MemoryInfo memory = UltraCanvas::UltraCanvasHardwareInfo::GetMemory();
    hw.memoryBytes          = memory.totalBytes;
    hw.availableMemoryBytes = memory.availableBytes;
    for (const UltraCanvas::GPUInfo& g : UltraCanvas::UltraCanvasHardwareInfo::ListGPUs()) {
        if (g.kind == UltraCanvas::GPUKind::Software || g.kind == UltraCanvas::GPUKind::Virtual) continue;
        Gpu gpu;
        gpu.vendor           = g.vendor;
        gpu.model            = g.model;
        gpu.videoMemoryBytes = g.videoMemoryBytes;
        gpu.discrete         = g.kind == UltraCanvas::GPUKind::Discrete;
        hw.gpus.push_back(std::move(gpu));
    }
#endif
    if (!hw.logicalCores) {
        hw.logicalCores = static_cast<int>(std::thread::hardware_concurrency());
    }
    if (!hw.memoryBytes) hw.memoryBytes = local_detail::TotalMemoryFallback();
    if (!hw.availableMemoryBytes) hw.availableMemoryBytes = hw.memoryBytes;
#if defined(__APPLE__) && (defined(__aarch64__) || defined(_M_ARM64))
    // Apple silicon: unified memory; the GPU's share is what Metal may use.
    if (hw.gpus.empty() && hw.memoryBytes) {
        Gpu gpu;
        gpu.vendor = "Apple";
        gpu.model  = "Apple silicon";
        gpu.videoMemoryBytes = hw.memoryBytes * 3 / 4;
        hw.gpus.push_back(std::move(gpu));
    }
#endif
    hw.DeriveAccelerators();
    return hw;
}

} // namespace UltraAI
