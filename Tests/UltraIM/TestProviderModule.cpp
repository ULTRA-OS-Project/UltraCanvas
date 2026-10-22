// Tests/UltraIM/TestProviderModule.cpp
// A provider module (DSO) for UltraIM_RefreshProviders
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "FakeProvider.h"

#include <memory>

extern "C" ULTRAIM_PLUGIN_EXPORT void UltraIM_PluginInit(const UltraIMPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterProvider) return;
    host->RegisterProvider(std::make_shared<FakeProvider>("moduletest"));
}
