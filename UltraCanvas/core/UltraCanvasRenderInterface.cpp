// UltraCanvasRenderInterface.cpp - Enhanced Implementation
// Cross-platform rendering interface with improved context management
// Version: 2.2.0
// Last Modified: 2025-07-11
// Author: UltraCanvas Framework

#include "../include/UltraCanvasRenderInterface.h"
#include <iostream>
#include <cassert>
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ===== ENHANCED RENDER CONTEXT MANAGER STATIC MEMBERS =====

// Thread-local storage for render contexts
thread_local IRenderContext* RenderContextManager::currentContext = nullptr;
thread_local std::stack<IRenderContext*> RenderContextManager::contextStack;
thread_local UltraCanvasBaseWindow* RenderContextManager::currentWindow = nullptr;

// Global mapping of windows to their render contexts (thread-safe)
std::unordered_map<void*, IRenderContext*> RenderContextManager::windowContextMap;
std::mutex RenderContextManager::windowContextMutex;

} // namespace UltraCanvas

/*
=== USAGE EXAMPLES ===

// Example 1: Multi-window rendering with automatic context management
void RenderMultipleWindows() {
    auto app = std::make_unique<UltraCanvasLinuxApplication>();
    app->Initialize();
    
    // Create two windows
    WindowConfig config1;
    config1.title = "Window 1";
    config1.width = 400;
    config1.height = 300;
    auto window1 = app->CreateWindow(config1);
    
    WindowConfig config2;
    config2.title = "Window 2"; 
    config2.width = 600;
    config2.height = 400;
    auto window2 = app->CreateWindow(config2);
    
    // Add elements to both windows
    auto button1 = CreateButton("Button in Window 1", 10, 10, 100, 30);
    auto button2 = CreateButton("Button in Window 2", 20, 20, 120, 35);
    
    window1->AddElement(button1.get());
    window2->AddElement(button2.get());
    
    window1->Show();
    window2->Show();
    
    // When app->Run() is called:
    // 1. window1->Render() sets currentContext to window1's LinuxRenderContext
    // 2. button1->Render() calls DrawRect(), which automatically uses window1's context
    // 3. window2->Render() sets currentContext to window2's LinuxRenderContext  
    // 4. button2->Render() calls DrawRect(), which automatically uses window2's context
    // Each window gets its own correct context automatically!
    
    app->Run();
}

// Example 2: UI Element that works correctly with multiple windows
class CustomMultiWindowElement : public UltraCanvasElement {
public:
    void Render() override {
        ULTRACANVAS_RENDER_SCOPE(); // Automatic state management
        
        // These calls automatically use the correct context for whichever window
        // is currently being rendered - no need to worry about which window!
        SetFillColor(Colors::Blue);
        FillRect(x, y, width, height);
        
        SetStrokeColor(Colors::White);
        SetStrokeWidth(2.0f);
        DrawRect(x, y, width, height);
        
        SetTextColor(Colors::White);
        SetFont("Arial", 12.0f);
        DrawText("Multi-Window Element", x + 10, y + 20);
        
        // This element will render correctly in any window it's added to!
    }
};

// Example 3: Manual context management for special cases
void ManualContextExample() {
    auto window1 = GetWindow1();
    auto window2 = GetWindow2();
    
    // Render something specifically to window1
    {
        ULTRACANVAS_WINDOW_RENDER_SCOPE(window1);
        SetFillColor(Colors::Red);
        FillRect(0, 0, 100, 100);
    } // Automatically restores previous context
    
    // Render something specifically to window2
    {
        ULTRACANVAS_WINDOW_RENDER_SCOPE(window2);
        SetFillColor(Colors::Green);
        FillRect(0, 0, 100, 100);
    } // Automatically restores previous context
    
    // Or use context directly
    IRenderContext* ctx1 = RenderContextManager::GetWindowContext(window1);
    IRenderContext* ctx2 = RenderContextManager::GetWindowContext(window2);
    
    {
        ULTRACANVAS_CONTEXT_SCOPE(ctx1);
        DrawCircle(50, 50, 25); // Draws in window1
    }
    
    {
        ULTRACANVAS_CONTEXT_SCOPE(ctx2);
        DrawCircle(50, 50, 25); // Draws in window2
    }
}

// Example 4: Cross-platform UI element
class CrossPlatformButton : public UltraCanvasElement {
public:
    void Render() override {
        ULTRACANVAS_RENDER_SCOPE();
        
        // This same code works on:
        // - Linux (uses LinuxRenderContext with Cairo)
        // - Windows (would use WindowsRenderContext with Direct2D)
        // - macOS (would use macOSRenderContext with Core Graphics)
        // - Web (would use WebRenderContext with Canvas API)
        
        SetFillColor(pressed ? Colors::DarkGray : Colors::LightGray);
        FillRoundedRect(Rect2D(x, y, width, height), 5.0f);
        
        SetStrokeColor(focused ? Colors::Blue : Colors::Gray);
        SetStrokeWidth(focused ? 2.0f : 1.0f);
        DrawRoundedRect(Rect2D(x, y, width, height), 5.0f);
        
        SetTextColor(Colors::Black);
        SetFont("system", 12.0f);
        SetTextAlign(TextAlign::Center);
        DrawText(buttonText, Point2D(x + width/2, y + height/2));
    }
    
private:
    std::string buttonText = "Button";
    bool pressed = false;
    bool focused = false;
};

=== KEY BENEFITS ===

1. **Thread-Local Contexts**: Each thread has its own current context, supporting
   multi-threaded rendering if needed.

2. **Automatic Context Resolution**: UI elements just call DrawRect() and it 
   automatically uses the correct context for the current window.

3. **Multiple Window Support**: Each window can have its own render context,
   and the system automatically switches between them.

4. **Scope-Based Management**: RAII-style scope guards ensure contexts are
   properly managed and restored.

5. **Backward Compatibility**: Existing code using RenderContextManager::GetCurrent()
   continues to work but now gets enhanced functionality.

6. **Cross-Platform**: The same UI element code works on all platforms, with
   each platform providing its own IRenderContext implementation.

7. **Error Resilience**: GetRenderContext() tries multiple ways to find a valid
   context, including checking the current window.

=== MIGRATION FROM SINGLE CONTEXT ===

OLD CODE:
```cpp
void MyElement::Render() {
    RenderContextManager::GetCurrent()->DrawRect(...);  // Might be null!
}
```

NEW CODE (same functionality, but more robust):
```cpp
void MyElement::Render() {
    ULTRACANVAS_RENDER_SCOPE();
    DrawRect(...);  // Automatically finds correct context
}
```

The enhanced system solves the multi-window context problem while maintaining
backward compatibility and adding useful new features.
*/