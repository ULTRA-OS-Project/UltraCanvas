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
