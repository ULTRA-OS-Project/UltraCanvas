// core/IODeviceManager/UltraCanvasIODeviceBackends.h
// Internal: the single place where compiled-in device backends are attached
// to the manager. Not part of the public IODeviceManager surface.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "../../include/IODeviceManager/UltraCanvasIODeviceManager.h"

namespace UltraCanvas {
namespace Internal {

// Called once from IODeviceManager::Initialize(). Each backend exposes a
// RegisterXxxBackend(IODeviceManager&) entry point, declared below and
// defined in exactly one translation unit; this function calls the ones the
// build enabled.
//
// Backends register explicitly rather than from a static initialiser because
// UltraCanvas also builds as a static library, where the linker drops the
// static initialisers of object files nothing else references — which would
// silently leave a platform with no devices at all.
//
// Adding a backend is: declare its entry point here, define it in its own
// file under core/IODeviceManager/ or OS/<Platform>/, and add a guarded call
// in RegisterCompiledBackends().
void RegisterCompiledBackends(IODeviceManager& manager);

} // namespace Internal
} // namespace UltraCanvas
