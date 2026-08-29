// include/UltraCanvasHardwareInfoBackend.h
// Internal contract between UltraCanvasHardwareInfo and its per-platform probes.
// Applications include UltraCanvasHardwareInfo.h instead; this header exists so
// OS/<Platform>/UltraCanvas*HardwareInfo.cpp and the shared core agree on one
// set of entry points.
// Version: 0.1.0
// Last Modified: 2026-08-29
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASHARDWAREINFOBACKEND_H
#define ULTRACANVASHARDWAREINFOBACKEND_H

#include "UltraCanvasHardwareInfo.h"

// A platform has a native probe when one of the OS/<Platform> backends is
// compiled into the build. Everything else (WASM, Android, the BSDs) links the
// fallback in core/UltraCanvasHardwareInfo.cpp, which fills what the C++ runtime
// itself can answer and records why the rest is missing.
#if defined(_WIN32)
    #define ULTRACANVAS_HARDWAREINFO_NATIVE 1
#elif defined(__APPLE__)
    #define ULTRACANVAS_HARDWAREINFO_NATIVE 1
#elif defined(__linux__) && !defined(__ANDROID__)
    #define ULTRACANVAS_HARDWAREINFO_NATIVE 1
#endif

namespace UltraCanvas {
namespace HardwareInfoBackend {

// Every Query* function is best-effort: it fills what it can reach, appends a
// human-readable line to `warnings` for what it cannot, and never throws. The
// shared core owns caching, identifier masking and presentation - a backend
// reports raw values only.

std::string BackendName();
bool        IsAvailable();

// ===== x86 FEATURE DETECTION (shared by every backend) =====
// CPUID is a property of the instruction set, not of the operating system, so
// this lives in core/UltraCanvasHardwareInfo.cpp and serves all three backends.
//
// It is read directly rather than through the OS's own feature query because
// Win32's IsProcessorFeaturePresent knows PF_* constants for only a handful of
// extensions - and not for GFNI, VAES or VPCLMULQDQ, which are exactly the ones
// -march=native picks up without needing AVX-512, since they are VEX-encoded.
// A field report on the Ladybird port had an AVX2-capable Ryzen 5 5500U fault
// on VGF2P8AFFINEQB while every feature the reporter knew how to print was
// present. The bit assignments and the level rules below are the same ones
// OS/MSWindows/UltraCanvasWindowsDiagnostics.cpp verified against /proc/cpuinfo;
// keep the two in step.
struct X86CpuFeatures {
    bool sse3 = false, ssse3 = false, sse41 = false, sse42 = false;
    bool popcnt = false, cx16 = false, movbe = false, lahf = false, lzcnt = false;
    bool osxsave = false, avx = false, avx2 = false, fma = false, f16c = false;
    bool bmi1 = false, bmi2 = false;
    bool aes = false, pclmul = false, sha = false;
    bool gfni = false, vaes = false, vpclmulqdq = false;
    bool avx512f = false, avx512bw = false, avx512cd = false;
    bool avx512dq = false, avx512vl = false, avx512vnni = false;
};

// Fills `out` from CPUID. Returns false off x86 (where CPUID does not exist),
// leaving `out` default-constructed. The architecture, never the compiler,
// decides: MSYS2's CLANGARM64 toolchain defines __clang__, so a "GCC or MSVC"
// split sends an ARM64 target down the x86 path.
bool ReadX86CpuFeatures(X86CpuFeatures& out);

// Highest x86-64 psABI microarchitecture level the CPU satisfies (1..4), or 0
// off x86. This is the actionable number: it is exactly what -march=x86-64-v<N>
// means, so a build targeting it is guaranteed to run here. GFNI, VAES,
// VPCLMULQDQ and SHA belong to *no* level - they are opt-in extensions that
// -march=native picks up from the build machine and no -march=x86-64-vN emits.
int X86MicroarchitectureLevel(const X86CpuFeatures& features);

// Appends the human-readable names of the features that are present, in the
// order a reader wants them.
void AppendX86FeatureNames(const X86CpuFeatures& features, std::vector<std::string>& out);

void QuerySystem(SystemInfo& out, std::vector<std::string>& warnings);
void QueryCPU(CPUInfo& out, bool includeSensors, std::vector<std::string>& warnings);
void QueryMemory(MemoryInfo& out, std::vector<std::string>& warnings);
void QueryGPUs(std::vector<GPUInfo>& out, bool includeSensors, std::vector<std::string>& warnings);
void QueryNPUs(std::vector<NPUInfo>& out, bool includeSensors, std::vector<std::string>& warnings);
void QueryStorage(std::vector<StorageDeviceInfo>& out, bool includeSensors, std::vector<std::string>& warnings);
void QueryNetwork(std::vector<NetworkInterfaceInfo>& out, std::vector<std::string>& warnings);
void QueryUSB(std::vector<USBControllerInfo>& controllers, std::vector<USBDeviceInfo>& devices,
              bool includeHubs, std::vector<std::string>& warnings);
void QueryBluetooth(std::vector<BluetoothAdapterInfo>& out, std::vector<std::string>& warnings);

// Re-read the live values of an already captured snapshot: CPU/GPU/NPU/drive
// temperatures and utilisation, free memory, carrier state and Wi-Fi signal.
// Must not add or remove devices - a caller holding indices into the vectors
// keeps them across a refresh.
void RefreshSensors(HardwareSnapshot& snapshot);

} // namespace HardwareInfoBackend
} // namespace UltraCanvas

#endif // ULTRACANVASHARDWAREINFOBACKEND_H
