// OS/WASM/UltraCanvasWASMFileLoader.cpp
// WebAssembly implementation of UltraCanvasFileLoader::NotifyRecentFile.
// The browser keeps no cross-application recent-files list a page could feed
// (files never leave the Emscripten virtual filesystem), so this is
// deliberately a no-op. Without this file every application that pulls in the
// file loader fails to link: the core calls the function unconditionally and
// each OS/<Platform>/ directory supplies its own definition.
// Version: 1.0.0
// Last Modified: 2026-09-07
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"

namespace UltraCanvas {

    void UltraCanvasFileLoader::NotifyRecentFile(const std::string&) {}

} // namespace UltraCanvas
