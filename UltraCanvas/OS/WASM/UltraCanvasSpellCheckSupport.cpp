// OS/WASM/UltraCanvasSpellCheckSupport.cpp
// WebAssembly spell check backend - delegates to the portable Hunspell backend
// Version: 1.0.0
// Last Modified: 2026-08-24
// Author: UltraCanvas Framework
//
// WebAssembly exposes no system spell service. Returning nullptr here makes
// UltraCanvasSpellChecker::Initialize() fall through to the Hunspell backend,
// which discovers dictionaries from ULTRACANVAS_DICT_PATH and the application's
// own "dictionaries" folder.
//
// Exactly one translation unit per platform defines this factory, and the build
// picks it up from the platform source directory. Without this file the link
// fails with an undefined CreateNativeSpellCheckBackend().
//
// When a native spell service becomes available, implement ISpellCheckBackend
// here and return it; nothing in core/ or include/ needs to change.

#include "ISpellCheckBackend.h"

namespace UltraCanvas {

std::unique_ptr<ISpellCheckBackend> CreateNativeSpellCheckBackend() {
    return nullptr;
}

} // namespace UltraCanvas
