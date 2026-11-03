// Tests/UltraIM/StaleProviderModules.cpp
// Two provider modules the host must refuse (built twice, once per define):
//   ULTRAIM_STALE_V1     - as modules were built for ABI 1: no
//                          UltraIM_PluginAbiVersion, and it accepts any host
//   ULTRAIM_STALE_OTHER  - says it was built for another version
// Either one registering a provider makes UltraIMTests' count of the modules
// loaded wrong. Its own vtable would not match the host's interface.
// Version: 0.1.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework

#include "FakeProvider.h"

#include <memory>

#if defined(ULTRAIM_STALE_OTHER)
extern "C" ULTRAIM_PLUGIN_EXPORT int UltraIM_PluginAbiVersion() { return ULTRAIM_PLUGIN_HOST_ABI_VERSION + 1; }
#endif

extern "C" ULTRAIM_PLUGIN_EXPORT void UltraIM_PluginInit(const UltraIMPluginHost* host) {
    if (!host || !host->RegisterProvider) return;       // any version: what a version 1 module did
    host->RegisterProvider(std::make_shared<FakeProvider>("stale"));
}
