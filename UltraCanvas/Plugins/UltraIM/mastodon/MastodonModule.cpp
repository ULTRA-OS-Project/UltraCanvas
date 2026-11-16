// Plugins/UltraIM/mastodon/MastodonModule.cpp
// The module's entry point: ultraim_mastodon.so registers the Mastodon provider.
// Version: 0.1.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework
#include "MastodonProvider.h"

#include <memory>

ULTRAIM_DEFINE_PLUGIN_ABI_VERSION()

extern "C" ULTRAIM_PLUGIN_EXPORT void UltraIM_PluginInit(const UltraIMPluginHost* host) {
    if (!host || host->abiVersion != ULTRAIM_PLUGIN_HOST_ABI_VERSION || !host->RegisterProvider) return;
    host->RegisterProvider(std::make_shared<UltraIMMastodon::MastodonProvider>());
}
