// OS/WASM/UltraCanvasWASMApplication.cpp
// WebAssembly platform application implementation
// Version: 1.0.0
// Last Modified: 2025-01-27
// Author: UltraCanvas Framework

#include "UltraCanvasWASMApplication.h"
#include "UltraCanvasWASMWindow.h"
#include <iostream>
#include <emscripten/html5.h>

namespace UltraCanvas {

// Static instance pointer
UltraCanvasWASMApplication* UltraCanvasWASMApplication::instance = nullptr;

// ===== CONSTRUCTOR / DESTRUCTOR =====

UltraCanvasWASMApplication::UltraCanvasWASMApplication()
    : running(false)
    , initialized(false)
    , lastFrameTime(0.0)
    , targetFrameTime(1.0 / 60.0)
    , targetFPS(60)
    , accumulatedTime(0.0)
    , frameCount(0)
    , lastFPSUpdate(0.0)
{
    instance = this;
    std::cout << "[WASM] UltraCanvasWASMApplication created" << std::endl;
}

UltraCanvasWASMApplication::~UltraCanvasWASMApplication() {
    std::cout << "[WASM] UltraCanvasWASMApplication destroyed" << std::endl;
    
    if (running) {
        Exit();
    }
    
    instance = nullptr;
}

// ===== INITIALIZATION =====

bool UltraCanvasWASMApplication::InitializeNative() {
    if (initialized) {
        std::cout << "[WASM] Already initialized" << std::endl;
        return true;
    }
    
    std::cout << "[WASM] Initializing WebAssembly application..." << std::endl;
    
    // Register page lifecycle callbacks
    emscripten_set_beforeunload_callback(this, OnBeforeUnload);
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, OnResize);
    emscripten_set_visibilitychange_callback(this, true, OnVisibilityChange);
    
    // Initialize timing
    lastFrameTime = emscripten_get_now() / 1000.0;
    lastFPSUpdate = lastFrameTime;
    
    // Print browser info
    EM_ASM({
        console.log('UltraCanvas WASM Application initialized');
        console.log('User Agent:', navigator.userAgent);
        console.log('Platform:', navigator.platform);
        console.log('Screen:', screen.width + 'x' + screen.height);
    });
    
    initialized = true;
    std::cout << "[WASM] Initialization complete" << std::endl;
    
    return true;
}

// ===== MAIN LOOP =====

void UltraCanvasWASMApplication::RunNative() {
    if (!initialized) {
        std::cerr << "[WASM] ERROR: Application not initialized!" << std::endl;
        return;
    }
    
    if (running) {
        std::cout << "[WASM] Already running" << std::endl;
        return;
    }
    
    std::cout << "[WASM] Starting main loop (target FPS: " << targetFPS << ")" << std::endl;
    
    running = true;
    
    // Set up Emscripten main loop
    // Note: emscripten_set_main_loop takes control and never returns
    emscripten_set_main_loop(EmscriptenMainLoop, targetFPS, 1);
    
    // This line is never reached due to Emscripten's event loop model
}

void UltraCanvasWASMApplication::EmscriptenMainLoop() {
    if (instance) {
        instance->MainLoopIteration();
    }
}

void UltraCanvasWASMApplication::MainLoopIteration() {
    if (!running) {
        return;
    }
    
    // Calculate delta time
    double currentTime = emscripten_get_now() / 1000.0;
    double deltaTime = currentTime - lastFrameTime;
    lastFrameTime = currentTime;
    
    // FPS calculation
    accumulatedTime += deltaTime;
    frameCount++;
    
    if (currentTime - lastFPSUpdate >= 1.0) {
        double fps = frameCount / accumulatedTime;
        lastFPSUpdate = currentTime;
        accumulatedTime = 0.0;
        frameCount = 0;
        
        // Optionally log FPS (can be disabled in production)
        // std::cout << "[WASM] FPS: " << fps << std::endl;
    }
    
    // Process events (from browser event queue)
    RunInEventLoop();
    
    // Render all windows
    for (auto* window : windows) {
        if (window && window->IsVisible()) {
            window->Render();
        }
    }
}

void UltraCanvasWASMApplication::Exit() {
    if (!running) {
        return;
    }
    
    std::cout << "[WASM] Exiting application..." << std::endl;
    
    running = false;
    
    // Cancel the main loop
    emscripten_cancel_main_loop();
    
    // Close all windows
    for (auto* window : windows) {
        if (window) {
            window->Close();
        }
    }
    
    std::cout << "[WASM] Application exited" << std::endl;
}

// ===== FRAME RATE CONTROL =====

void UltraCanvasWASMApplication::SetTargetFPS(int fps) {
    if (fps <= 0 || fps > 240) {
        std::cerr << "[WASM] Invalid FPS: " << fps << " (valid range: 1-240)" << std::endl;
        return;
    }
    
    targetFPS = fps;
    targetFrameTime = 1.0 / fps;
    
    std::cout << "[WASM] Target FPS set to: " << fps << std::endl;
    
    // Update main loop if running
    if (running) {
        emscripten_cancel_main_loop();
        emscripten_set_main_loop(EmscriptenMainLoop, targetFPS, 1);
    }
}

double UltraCanvasWASMApplication::GetActualFPS() const {
    if (accumulatedTime > 0.0) {
        return frameCount / accumulatedTime;
    }
    return 0.0;
}

// ===== ANIMATION FRAME =====

void UltraCanvasWASMApplication::RequestAnimationFrame() {
    // Emscripten's main loop already uses requestAnimationFrame internally
    // when simulate_infinite_loop is set to 1
    // This function is provided for compatibility
}

void UltraCanvasWASMApplication::CancelAnimationFrame() {
    emscripten_cancel_main_loop();
}

// ===== PAGE LIFECYCLE CALLBACKS =====

EM_BOOL UltraCanvasWASMApplication::OnBeforeUnload(int eventType, const void* reserved, void* userData) {
    auto* app = static_cast<UltraCanvasWASMApplication*>(userData);
    if (app) {
        std::cout << "[WASM] Page unloading..." << std::endl;
        app->OnPageUnload();
    }
    return EM_FALSE; // Allow default behavior
}

EM_BOOL UltraCanvasWASMApplication::OnResize(int eventType, const EmscriptenUiEvent* event, void* userData) {
    auto* app = static_cast<UltraCanvasWASMApplication*>(userData);
    if (app) {
        std::cout << "[WASM] Window resized to: " << event->windowInnerWidth 
                  << "x" << event->windowInnerHeight << std::endl;
        
        // Notify all windows about resize
        for (auto* window : app->windows) {
            if (window) {
                // Window will handle its own resize
                window->HandleResize();
            }
        }
    }
    return EM_TRUE;
}

EM_BOOL UltraCanvasWASMApplication::OnVisibilityChange(int eventType, 
                                                       const EmscriptenVisibilityChangeEvent* event, 
                                                       void* userData) {
    auto* app = static_cast<UltraCanvasWASMApplication*>(userData);
    if (app) {
        if (event->hidden) {
            std::cout << "[WASM] Page hidden" << std::endl;
            app->OnPageHidden();
        } else {
            std::cout << "[WASM] Page visible" << std::endl;
            app->OnPageVisible();
        }
    }
    return EM_TRUE;
}

void UltraCanvasWASMApplication::OnPageVisible() {
    std::cout << "[WASM] Page became visible - resuming rendering" << std::endl;
    // Resume rendering if paused
    running = true;
}

void UltraCanvasWASMApplication::OnPageHidden() {
    std::cout << "[WASM] Page hidden - pausing rendering" << std::endl;
    // Pause rendering to save resources
    // Note: We don't fully stop, just reduce activity
}

void UltraCanvasWASMApplication::OnPageUnload() {
    std::cout << "[WASM] Page unloading - cleaning up" << std::endl;
    Exit();
}

} // namespace UltraCanvas
