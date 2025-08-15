// UltraCanvasApplication.cpp
// Main UltraCanvas App
// Version: 1.0.0
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework

#include "../include/UltraCanvasApplication.h"
#include "../include/UltraCanvasClipboard.h"

namespace UltraCanvas {
    std::unique_ptr<UltraCanvasApplication> UltraCanvasApplication::instance;

    void UltraCanvasApplication::SetGlobalEventHandler(std::function<bool(const UCEvent&)> handler) {
        std::cout << "UltraCanvas: Setting cross-platform global event handler" << std::endl;

        // Delegate to platform-specific implementation
        // This works the same way regardless of platform
        auto* nativeApp = static_cast<UltraCanvasNativeApplication*>(this);
        nativeApp->SetGlobalEventHandler(handler);
    }

    void UltraCanvasApplication::RunInEventLoop() {
        auto clipbrd = GetClipboard();
        if (clipbrd) {
            clipbrd->Update();
        }
    }
}