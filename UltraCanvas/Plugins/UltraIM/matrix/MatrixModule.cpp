// Plugins/UltraIM/matrix/MatrixModule.cpp
// Matrix - the provider module's entry point (ultraim_matrix.so / .dll / .dylib)
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "MatrixProvider.h"

#include <memory>

extern "C" ULTRAIM_PLUGIN_EXPORT void UltraIM_PluginInit(const UltraIMPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterProvider) return;
    host->RegisterProvider(std::make_shared<UltraIMMatrix::MatrixProvider>());
}
