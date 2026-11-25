// OS/iOS/UltraCanvasSpellCheckSupport.cpp
// No native spell-check backend on iOS yet: UITextChecker is the natural one
// (Docs/UltraCanvas/iOSPortInvestigation.md 3.1). Until then the service
// falls back to Hunspell where it is compiled in, and reports none otherwise.
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "ISpellCheckBackend.h"

namespace UltraCanvas {

std::unique_ptr<ISpellCheckBackend> CreateNativeSpellCheckBackend() {
    return nullptr;
}

} // namespace UltraCanvas
