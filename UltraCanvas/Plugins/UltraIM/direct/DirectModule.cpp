// Plugins/UltraIM/direct/DirectModule.cpp
// ChatCube Direct - the provider module's entry point (ultraim_direct.so / .dll / .dylib)
// Version: 0.1.1 - UltraIM_PluginAbiVersion; only a host of the version it was built for
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "DirectProvider.h"

#include <memory>

ULTRAIM_DEFINE_PLUGIN_ABI_VERSION()

extern "C" ULTRAIM_PLUGIN_EXPORT void UltraIM_PluginInit(const UltraIMPluginHost* host) {
    // Only the host it was built for: the interface and its types are part of the version.
    if (!host || host->abiVersion != ULTRAIM_PLUGIN_HOST_ABI_VERSION || !host->RegisterProvider) return;
    host->RegisterProvider(std::make_shared<UltraIMDirect::DirectProvider>());
}
