// OS/WASM/UltraCanvasWASMWindow.cpp
// WebAssembly platform window implementation
// Version: 1.0.0
// Last Modified: 2025-01-27
// Author: UltraCanvas Framework

#include "UltraCanvasWASMWindow.h"
#include "UltraCanvasWASMApplication.h"
#include <iostream>
#include <emscripten.h>

namespace UltraCanvas {

// ===== CONSTRUCTOR / DESTRUCTOR =====

UltraCanvasWASMWindow::UltraCanvasWASMWindow()
    : canvasId("canvas")
    , canvasCreated(false)
    , mouseInside(false)
    , lastMouseX(0)
    , lastMouseY(0)
{
    std::cout << "[WASM Window] Created" << std::endl;
}

UltraCanvasWASMWindow::~UltraCanvasWASMWindow() {
    std::cout << "[WASM Window] Destroyed" << std::endl;
    DestroyNative();
}

// ===== WINDOW CREATION =====

bool UltraCanvasWASMWindow::CreateNative(const WindowConfig& config) {
    std::cout << "[WASM Window] Creating window: " << config.title 
              << " (" << config.width << "x" << config.height << ")" << std::endl;
    
    // Store configuration
    config_ = config;
    
    // Use provided canvas ID or default
    if (!config.platformData.empty()) {
        canvasId = config.platformData;
    }
    
    // Create or find canvas element
    if (!CreateCanvas()) {
        std::cerr << "[WASM Window] ERROR: Failed to create canvas" << std::endl;
        return false;
    }
    
    // Create render context
    wasmRenderContext = std::make_unique<UltraCanvasWASMRenderContext>(canvasId);
    if (!wasmRenderContext->Initialize()) {
        std::cerr << "[WASM Window] ERROR: Failed to initialize render context" << std::endl;
        return false;
    }
    
    // Set canvas size
    UpdateCanvasSize();
    
    // Set page title
    SetTitle(config.title);
    
    // Register event callbacks
    const char* target = ("#" + canvasId).c_str();
    
    emscripten_set_mousedown_callback(target, this, true, OnMouseEvent);
    emscripten_set_mouseup_callback(target, this, true, OnMouseEvent);
    emscripten_set_mousemove_callback(target, this, true, OnMouseEvent);
    emscripten_set_mouseenter_callback(target, this, true, OnMouseEvent);
    emscripten_set_mouseleave_callback(target, this, true, OnMouseEvent);
    
    emscripten_set_wheel_callback(target, this, true, OnWheelEvent);
    
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, OnKeyEvent);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, OnKeyEvent);
    emscripten_set_keypress_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, OnKeyEvent);
    
    emscripten_set_touchstart_callback(target, this, true, OnTouchEvent);
    emscripten_set_touchend_callback(target, this, true, OnTouchEvent);
    emscripten_set_touchmove_callback(target, this, true, OnTouchEvent);
    emscripten_set_touchcancel_callback(target, this, true, OnTouchEvent);
    
    emscripten_set_focus_callback(target, this, true, OnFocusEvent);
    emscripten_set_blur_callback(target, this, true, OnFocusEvent);
    
    _created = true;
    std::cout << "[WASM Window] Window created successfully" << std::endl;
    
    return true;
}

void UltraCanvasWASMWindow::DestroyNative() {
    if (!_created) {
        return;
    }
    
    std::cout << "[WASM Window] Destroying window" << std::endl;
    
    // Unregister event callbacks
    const char* target = ("#" + canvasId).c_str();
    
    emscripten_set_mousedown_callback(target, nullptr, false, nullptr);
    emscripten_set_mouseup_callback(target, nullptr, false, nullptr);
    emscripten_set_mousemove_callback(target, nullptr, false, nullptr);
    emscripten_set_wheel_callback(target, nullptr, false, nullptr);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr);
    emscripten_set_touchstart_callback(target, nullptr, false, nullptr);
    emscripten_set_touchend_callback(target, nullptr, false, nullptr);
    emscripten_set_touchmove_callback(target, nullptr, false, nullptr);
    emscripten_set_focus_callback(target, nullptr, false, nullptr);
    emscripten_set_blur_callback(target, nullptr, false, nullptr);
    
    // Destroy render context
    wasmRenderContext.reset();
    
    // Destroy canvas
    DestroyCanvas();
    
    _created = false;
}

// ===== CANVAS MANAGEMENT =====

bool UltraCanvasWASMWindow::CreateCanvas() {
    // Check if canvas already exists in HTML
    bool exists = EM_ASM_INT({
        const canvas = document.getElementById(UTF8ToString($0));
        return canvas ? 1 : 0;
    }, canvasId.c_str());
    
    if (exists) {
        std::cout << "[WASM Window] Using existing canvas: " << canvasId << std::endl;
        canvasCreated = false; // We didn't create it
        return true;
    }
    
    // Create new canvas element
    std::cout << "[WASM Window] Creating new canvas: " << canvasId << std::endl;
    
    EM_ASM({
        const canvas = document.createElement('canvas');
        canvas.id = UTF8ToString($0);
        canvas.style.display = 'block';
        canvas.style.margin = '0 auto';
        document.body.appendChild(canvas);
    }, canvasId.c_str());
    
    canvasCreated = true;
    return true;
}

void UltraCanvasWASMWindow::DestroyCanvas() {
    if (canvasCreated) {
        std::cout << "[WASM Window] Destroying canvas: " << canvasId << std::endl;
        
        EM_ASM({
            const canvas = document.getElementById(UTF8ToString($0));
            if (canvas && canvas.parentNode) {
                canvas.parentNode.removeChild(canvas);
            }
        }, canvasId.c_str());
        
        canvasCreated = false;
    }
}

void UltraCanvasWASMWindow::UpdateCanvasSize() {
    int width = config_.width;
    int height = config_.height;
    
    EM_ASM({
        const canvas = document.getElementById(UTF8ToString($0));
        if (canvas) {
            canvas.width = $1;
            canvas.height = $2;
            canvas.style.width = $1 + 'px';
            canvas.style.height = $2 + 'px';
        }
    }, canvasId.c_str(), width, height);
    
    std::cout << "[WASM Window] Canvas size updated: " << width << "x" << height << std::endl;
}

// ===== WINDOW OPERATIONS =====

void UltraCanvasWASMWindow::Show() {
    if (!_created) return;
    
    EM_ASM({
        const canvas = document.getElementById(UTF8ToString($0));
        if (canvas) {
            canvas.style.display = 'block';
        }
    }, canvasId.c_str());
    
    _visible = true;
}

void UltraCanvasWASMWindow::Hide() {
    if (!_created) return;
    
    EM_ASM({
        const canvas = document.getElementById(UTF8ToString($0));
        if (canvas) {
            canvas.style.display = 'none';
        }
    }, canvasId.c_str());
    
    _visible = false;
}

void UltraCanvasWASMWindow::Minimize() {
    // Not applicable in web context
    std::cout << "[WASM Window] Minimize not supported in web context" << std::endl;
}

void UltraCanvasWASMWindow::Maximize() {
    // In web context, we can make the canvas fullscreen
    EM_ASM({
        const canvas = document.getElementById(UTF8ToString($0));
        if (canvas && canvas.requestFullscreen) {
            canvas.requestFullscreen();
        }
    }, canvasId.c_str());
}

void UltraCanvasWASMWindow::Restore() {
    // Exit fullscreen if in fullscreen mode
    EM_ASM({
        if (document.fullscreenElement) {
            document.exitFullscreen();
        }
    });
}

void UltraCanvasWASMWindow::Close() {
    std::cout << "[WASM Window] Closing window" << std::endl;
    
    // Generate close event
    UCEvent closeEvent;
    closeEvent.type = UCEventType::WindowClose;
    OnEvent(closeEvent);
    
    // Hide and destroy
    Hide();
    Destroy();
}

// ===== WINDOW PROPERTIES =====

void UltraCanvasWASMWindow::SetTitle(const std::string& title) {
    config_.title = title;
    
    // Set browser page title
    EM_ASM({
        document.title = UTF8ToString($0);
    }, title.c_str());
}

void UltraCanvasWASMWindow::SetPosition(int x, int y) {
    // Position is not directly controllable in web context
    // But we can set CSS positioning
    config_.x = x;
    config_.y = y;
    
    EM_ASM({
        const canvas = document.getElementById(UTF8ToString($0));
        if (canvas) {
            canvas.style.position = 'absolute';
            canvas.style.left = $1 + 'px';
            canvas.style.top = $2 + 'px';
        }
    }, canvasId.c_str(), x, y);
}

void UltraCanvasWASMWindow::SetSize(int width, int height) {
    config_.width = width;
    config_.height = height;
    
    UpdateCanvasSize();
    
    // Notify render context
    if (wasmRenderContext) {
        wasmRenderContext->SetViewportSize(width, height);
    }
    
    // Generate resize event
    UCEvent resizeEvent;
    resizeEvent.type = UCEventType::WindowResize;
    resizeEvent.width = width;
    resizeEvent.height = height;
    OnEvent(resizeEvent);
}

void UltraCanvasWASMWindow::GetPosition(int& x, int& y) const {
    x = config_.x;
    y = config_.y;
}

void UltraCanvasWASMWindow::GetSize(int& width, int& height) const {
    width = config_.width;
    height = config_.height;
}

// ===== RENDERING =====

void UltraCanvasWASMWindow::Render(IRenderContext* ctx) {
    if (!_created || !_visible || !wasmRenderContext) {
        return;
    }
    
    // Begin frame
    wasmRenderContext->BeginFrame();
    
    // Clear background
    wasmRenderContext->Clear(config_.backgroundColor);
    
    // Call base class render (renders all UI elements)
    UltraCanvasWindowBase::Render(IRenderContext* ctx);
    
    // End frame
    wasmRenderContext->EndFrame();
}

void UltraCanvasWASMWindow::RequestRedraw() {
    _needsRedraw = true;
}

IRenderContext* UltraCanvasWASMWindow::GetRenderContext() {
    return wasmRenderContext.get();
}

// ===== CANVAS ACCESS =====

void UltraCanvasWASMWindow::SetCanvasId(const std::string& id) {
    if (_created) {
        std::cerr << "[WASM Window] Cannot change canvas ID after window creation" << std::endl;
        return;
    }
    canvasId = id;
}

// ===== RESIZE HANDLING =====

void UltraCanvasWASMWindow::HandleResize() {
    // Get current canvas size from DOM
    int width = EM_ASM_INT({
        const canvas = document.getElementById(UTF8ToString($0));
        return canvas ? canvas.width : 0;
    }, canvasId.c_str());
    
    int height = EM_ASM_INT({
        const canvas = document.getElementById(UTF8ToString($0));
        return canvas ? canvas.height : 0;
    }, canvasId.c_str());
    
    if (width > 0 && height > 0 && (width != config_.width || height != config_.height)) {
        SetSize(width, height);
    }
}

// ===== FOCUS =====

void UltraCanvasWASMWindow::SetFocus() {
    EM_ASM({
        const canvas = document.getElementById(UTF8ToString($0));
        if (canvas) {
            canvas.focus();
        }
    }, canvasId.c_str());
}

bool UltraCanvasWASMWindow::HasFocus() const {
    return EM_ASM_INT({
        const canvas = document.getElementById(UTF8ToString($0));
        return (canvas && document.activeElement === canvas) ? 1 : 0;
    }, canvasId.c_str());
}

// ===== EVENT CALLBACKS =====

EM_BOOL UltraCanvasWASMWindow::OnMouseEvent(int eventType, const EmscriptenMouseEvent* event, void* userData) {
    auto* window = static_cast<UltraCanvasWASMWindow*>(userData);
    if (!window) return EM_FALSE;
    
    UCEvent ucEvent = window->ConvertMouseEvent(eventType, event);
    window->OnEvent(ucEvent);
    
    return EM_TRUE; // Prevent default browser behavior
}

EM_BOOL UltraCanvasWASMWindow::OnWheelEvent(int eventType, const EmscriptenWheelEvent* event, void* userData) {
    auto* window = static_cast<UltraCanvasWASMWindow*>(userData);
    if (!window) return EM_FALSE;
    
    UCEvent ucEvent = window->ConvertWheelEvent(event);
    window->OnEvent(ucEvent);
    
    return EM_TRUE; // Prevent default browser scrolling
}

EM_BOOL UltraCanvasWASMWindow::OnKeyEvent(int eventType, const EmscriptenKeyboardEvent* event, void* userData) {
    auto* window = static_cast<UltraCanvasWASMWindow*>(userData);
    if (!window) return EM_FALSE;
    
    UCEvent ucEvent = window->ConvertKeyEvent(eventType, event);
    window->OnEvent(ucEvent);
    
    return EM_TRUE; // Prevent default browser behavior for most keys
}

EM_BOOL UltraCanvasWASMWindow::OnTouchEvent(int eventType, const EmscriptenTouchEvent* event, void* userData) {
    auto* window = static_cast<UltraCanvasWASMWindow*>(userData);
    if (!window) return EM_FALSE;
    
    UCEvent ucEvent = window->ConvertTouchEvent(eventType, event);
    window->OnEvent(ucEvent);
    
    return EM_TRUE; // Prevent default touch behavior
}

EM_BOOL UltraCanvasWASMWindow::OnFocusEvent(int eventType, const EmscriptenFocusEvent* event, void* userData) {
    auto* window = static_cast<UltraCanvasWASMWindow*>(userData);
    if (!window) return EM_FALSE;
    
    UCEvent ucEvent;
    ucEvent.type = (eventType == EMSCRIPTEN_EVENT_FOCUS) ? 
                   UCEventType::WindowFocusGained : UCEventType::WindowFocusLost;
    
    window->OnEvent(ucEvent);
    
    return EM_TRUE;
}

// ===== EVENT CONVERSION =====

UCEvent UltraCanvasWASMWindow::ConvertMouseEvent(int eventType, const EmscriptenMouseEvent* event) {
    UCEvent ucEvent;
    
    // Set event type
    switch (eventType) {
        case EMSCRIPTEN_EVENT_MOUSEDOWN:
            ucEvent.type = UCEventType::MouseButtonDown;
            break;
        case EMSCRIPTEN_EVENT_MOUSEUP:
            ucEvent.type = UCEventType::MouseButtonUp;
            break;
        case EMSCRIPTEN_EVENT_MOUSEMOVE:
            ucEvent.type = UCEventType::MouseMove;
            break;
        case EMSCRIPTEN_EVENT_MOUSEENTER:
            ucEvent.type = UCEventType::MouseEnter;
            mouseInside = true;
            break;
        case EMSCRIPTEN_EVENT_MOUSELEAVE:
            ucEvent.type = UCEventType::MouseLeave;
            mouseInside = false;
            break;
        default:
            ucEvent.type = UCEventType::MouseMove;
            break;
    }
    
    // Set mouse button
    ucEvent.mouseButton = ConvertMouseButton(event->button);
    
    // Set position (canvas-relative)
    ucEvent.x = static_cast<int>(event->targetX);
    ucEvent.y = static_cast<int>(event->targetY);
    
    // Set modifiers
    ucEvent.shiftKey = event->shiftKey;
    ucEvent.ctrlKey = event->ctrlKey;
    ucEvent.altKey = event->altKey;
    ucEvent.metaKey = event->metaKey;
    
    // Track mouse movement
    if (eventType == EMSCRIPTEN_EVENT_MOUSEMOVE) {
        ucEvent.deltaX = ucEvent.x - lastMouseX;
        ucEvent.deltaY = ucEvent.y - lastMouseY;
        lastMouseX = ucEvent.x;
        lastMouseY = ucEvent.y;
    }
    
    return ucEvent;
}

UCEvent UltraCanvasWASMWindow::ConvertKeyEvent(int eventType, const EmscriptenKeyboardEvent* event) {
    UCEvent ucEvent;
    
    // Set event type
    switch (eventType) {
        case EMSCRIPTEN_EVENT_KEYDOWN:
            ucEvent.type = UCEventType::KeyDown;
            break;
        case EMSCRIPTEN_EVENT_KEYUP:
            ucEvent.type = UCEventType::KeyUp;
            break;
        case EMSCRIPTEN_EVENT_KEYPRESS:
            ucEvent.type = UCEventType::KeyPress;
            break;
        default:
            ucEvent.type = UCEventType::KeyDown;
            break;
    }
    
    // Convert key
    ucEvent.virtualKey = ConvertEmscriptenKey(event->key, event->code);
    
    // Set modifiers
    ucEvent.shiftKey = event->shiftKey;
    ucEvent.ctrlKey = event->ctrlKey;
    ucEvent.altKey = event->altKey;
    ucEvent.metaKey = event->metaKey;
    
    // Set repeat flag
    ucEvent.repeat = event->repeat;
    
    return ucEvent;
}

UCEvent UltraCanvasWASMWindow::ConvertWheelEvent(const EmscriptenWheelEvent* event) {
    UCEvent ucEvent;
    ucEvent.type = UCEventType::MouseWheel;
    
    // Set scroll delta
    ucEvent.wheelDelta = -static_cast<int>(event->deltaY);
    
    // Set position
    ucEvent.x = static_cast<int>(event->mouse.targetX);
    ucEvent.y = static_cast<int>(event->mouse.targetY);
    
    // Set modifiers
    ucEvent.shiftKey = event->mouse.shiftKey;
    ucEvent.ctrlKey = event->mouse.ctrlKey;
    ucEvent.altKey = event->mouse.altKey;
    
    return ucEvent;
}

UCEvent UltraCanvasWASMWindow::ConvertTouchEvent(int eventType, const EmscriptenTouchEvent* event) {
    UCEvent ucEvent;
    
    // For now, treat first touch as mouse
    if (event->numTouches > 0) {
        const EmscriptenTouchPoint& touch = event->touches[0];
        
        // Set event type
        switch (eventType) {
            case EMSCRIPTEN_EVENT_TOUCHSTART:
                ucEvent.type = UCEventType::MouseButtonDown;
                ucEvent.mouseButton = UCMouseButton::Left;
                break;
            case EMSCRIPTEN_EVENT_TOUCHEND:
                ucEvent.type = UCEventType::MouseButtonUp;
                ucEvent.mouseButton = UCMouseButton::Left;
                break;
            case EMSCRIPTEN_EVENT_TOUCHMOVE:
                ucEvent.type = UCEventType::MouseMove;
                break;
            case EMSCRIPTEN_EVENT_TOUCHCANCEL:
                ucEvent.type = UCEventType::MouseButtonUp;
                ucEvent.mouseButton = UCMouseButton::Left;
                break;
            default:
                ucEvent.type = UCEventType::MouseMove;
                break;
        }
        
        // Set position
        ucEvent.x = static_cast<int>(touch.targetX);
        ucEvent.y = static_cast<int>(touch.targetY);
    }
    
    return ucEvent;
}

// ===== KEY CONVERSION =====

UCKeys UltraCanvasWASMWindow::ConvertEmscriptenKey(const char* key, const char* code) {
    std::string keyStr(key);
    std::string codeStr(code);
    
    // Function keys
    if (codeStr.substr(0, 1) == "F" && codeStr.length() <= 3) {
        int fNum = std::atoi(codeStr.substr(1).c_str());
        if (fNum >= 1 && fNum <= 12) {
            return static_cast<UCKeys>(static_cast<int>(UCKeys::F1) + (fNum - 1));
        }
    }
    
    // Special keys
    if (keyStr == "Enter" || keyStr == "Return") return UCKeys::Return;
    if (keyStr == "Escape") return UCKeys::Escape;
    if (keyStr == "Backspace") return UCKeys::Backspace;
    if (keyStr == "Tab") return UCKeys::Tab;
    if (keyStr == "Shift") return UCKeys::Shift;
    if (keyStr == "Control") return UCKeys::Control;
    if (keyStr == "Alt") return UCKeys::Alt;
    if (keyStr == "CapsLock") return UCKeys::CapsLock;
    if (keyStr == " ") return UCKeys::Space;
    if (keyStr == "ArrowUp") return UCKeys::Up;
    if (keyStr == "ArrowDown") return UCKeys::Down;
    if (keyStr == "ArrowLeft") return UCKeys::Left;
    if (keyStr == "ArrowRight") return UCKeys::Right;
    if (keyStr == "Home") return UCKeys::Home;
    if (keyStr == "End") return UCKeys::End;
    if (keyStr == "PageUp") return UCKeys::PageUp;
    if (keyStr == "PageDown") return UCKeys::PageDown;
    if (keyStr == "Insert") return UCKeys::Insert;
    if (keyStr == "Delete") return UCKeys::Delete;
    
    // Letter keys
    if (keyStr.length() == 1) {
        char c = keyStr[0];
        if (c >= 'a' && c <= 'z') {
            return static_cast<UCKeys>('A' + (c - 'a'));
        }
        if (c >= 'A' && c <= 'Z') {
            return static_cast<UCKeys>(c);
        }
        if (c >= '0' && c <= '9') {
            return static_cast<UCKeys>(c);
        }
    }
    
    return UCKeys::None;
}

UCMouseButton UltraCanvasWASMWindow::ConvertMouseButton(unsigned short button) {
    switch (button) {
        case 0: return UCMouseButton::Left;
        case 1: return UCMouseButton::Middle;
        case 2: return UCMouseButton::Right;
        default: return UCMouseButton::None;
    }
}

} // namespace UltraCanvas
