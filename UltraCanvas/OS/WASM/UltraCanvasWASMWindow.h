// OS/WASM/UltraCanvasWASMWindow.h
// WebAssembly platform window implementation
// Version: 1.0.0
// Last Modified: 2025-01-27
// Author: UltraCanvas Framework
#pragma once

#include "../../include/UltraCanvasWindow.h"
#include "UltraCanvasWASMRenderContext.h"
#include <emscripten/html5.h>
#include <string>
#include <memory>

namespace UltraCanvas {

class UltraCanvasWASMWindow : public UltraCanvasWindowBase {
private:
    // Canvas state
    std::string canvasId;
    bool canvasCreated;
    
    // Render context
    std::unique_ptr<UltraCanvasWASMRenderContext> wasmRenderContext;
    
    // Event callbacks
    static EM_BOOL OnMouseEvent(int eventType, const EmscriptenMouseEvent* event, void* userData);
    static EM_BOOL OnWheelEvent(int eventType, const EmscriptenWheelEvent* event, void* userData);
    static EM_BOOL OnKeyEvent(int eventType, const EmscriptenKeyboardEvent* event, void* userData);
    static EM_BOOL OnTouchEvent(int eventType, const EmscriptenTouchEvent* event, void* userData);
    static EM_BOOL OnFocusEvent(int eventType, const EmscriptenFocusEvent* event, void* userData);
    
    // Event conversion helpers
    UCEvent ConvertMouseEvent(int eventType, const EmscriptenMouseEvent* event);
    UCEvent ConvertKeyEvent(int eventType, const EmscriptenKeyboardEvent* event);
    UCEvent ConvertWheelEvent(const EmscriptenWheelEvent* event);
    UCEvent ConvertTouchEvent(int eventType, const EmscriptenTouchEvent* event);
    
    // Key conversion
    UCKeys ConvertEmscriptenKey(const char* key, const char* code);
    UCMouseButton ConvertMouseButton(unsigned short button);
    
    // Canvas operations
    bool CreateCanvas();
    void DestroyCanvas();
    void UpdateCanvasSize();
    
    // Mouse tracking
    bool mouseInside;
    int lastMouseX;
    int lastMouseY;
    
public:
    UltraCanvasWASMWindow();
    virtual ~UltraCanvasWASMWindow();
    
    // ===== WINDOW LIFECYCLE =====
    bool CreateNative(const WindowConfig& config) override;
    void DestroyNative() override;
    
    // ===== WINDOW OPERATIONS =====
    void Show() override;
    void Hide() override;
    void Minimize() override;
    void Maximize() override;
    void Restore() override;
    void Close() override;
    
    // ===== WINDOW PROPERTIES =====
    void SetTitle(const std::string& title) override;
    void SetPosition(int x, int y) override;
    void SetSize(int width, int height) override;
    void GetPosition(int& x, int& y) const override;
    void GetSize(int& width, int& height) const override;
    
    // ===== RENDERING =====
    void Render() override;
    void RequestRedraw() override;
    IRenderContext* GetRenderContext() override;
    
    // ===== CANVAS ACCESS =====
    const std::string& GetCanvasId() const { return canvasId; }
    void SetCanvasId(const std::string& id);
    
    // ===== RESIZE HANDLING =====
    void HandleResize();
    
    // ===== FOCUS =====
    void SetFocus() override;
    bool HasFocus() const override;
};

} // namespace UltraCanvas
