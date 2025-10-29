// OS/WASM/UltraCanvasWASMApplication.h
// WebAssembly platform application implementation
// Version: 1.0.0
// Last Modified: 2025-01-27
// Author: UltraCanvas Framework
#pragma once

#include "../../include/UltraCanvasApplication.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <vector>
#include <memory>
#include <functional>

namespace UltraCanvas {

// Forward declaration
class UltraCanvasWASMWindow;

class UltraCanvasWASMApplication : public UltraCanvasApplicationBase {
private:
    // Application state
    bool running;
    bool initialized;
    
    // Main loop callback
    static UltraCanvasWASMApplication* instance;
    std::function<void()> mainLoopCallback;
    
    // Frame timing
    double lastFrameTime;
    double targetFrameTime;
    int targetFPS;
    
    // Event handling
    static EM_BOOL OnBeforeUnload(int eventType, const void* reserved, void* userData);
    static EM_BOOL OnResize(int eventType, const EmscriptenUiEvent* event, void* userData);
    static EM_BOOL OnVisibilityChange(int eventType, const EmscriptenVisibilityChangeEvent* event, void* userData);
    
    // Main loop
    static void EmscriptenMainLoop();
    void MainLoopIteration();
    
    // Performance monitoring
    double accumulatedTime;
    int frameCount;
    double lastFPSUpdate;
    
public:
    UltraCanvasWASMApplication();
    virtual ~UltraCanvasWASMApplication();
    
    // ===== LIFECYCLE =====
    bool InitializeNative() override;
    void RunNative() override;
    void Exit() override;
    
    // ===== FRAME RATE CONTROL =====
    void SetTargetFPS(int fps);
    int GetTargetFPS() const { return targetFPS; }
    double GetActualFPS() const;
    
    // ===== BROWSER INTEGRATION =====
    void RequestAnimationFrame();
    void CancelAnimationFrame();
    
    // ===== PAGE LIFECYCLE =====
    void OnPageVisible();
    void OnPageHidden();
    void OnPageUnload();
    
    // ===== STATIC INSTANCE ACCESS =====
    static UltraCanvasWASMApplication* GetInstance() { return instance; }
};

} // namespace UltraCanvas
