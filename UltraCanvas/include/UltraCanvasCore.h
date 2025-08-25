// UltraCanvasCore.h - Pure graphics/UI framework for ULTRA OS
#pragma once
#include "UltraCanvasEvent.h"
#include <vector>
#include <string>
#include <functional>

// Forward declarations
class CanvasContext;
class UltraCanvasElement;

// ===== RENDERING CONTEXT =====
class CanvasContext {
public:
    // Drawing primitives
    virtual void DrawRectangle(int x, int y, int w, int h, uint32_t color) = 0;
    virtual void DrawText(const std::string& text, int x, int y, const std::string& font, int size, uint32_t color) = 0;
    virtual void DrawLine(int x1, int y1, int x2, int y2, uint32_t color) = 0;
    virtual void DrawCircle(int cx, int cy, int radius, uint32_t color) = 0;
    
    // Clipping and state
    virtual void PushClipRect(int x, int y, int w, int h) = 0;
    virtual void PopClipRect() = 0;
    virtual void SetAlpha(float alpha) = 0;
    
    // Text measurement
    virtual int GetTextWidth(const std::string& text, const std::string& font, int size) = 0;
    virtual int GetTextHeight(const std::string& font, int size) = 0;
    
    // Advanced drawing
    virtual void DrawGradient(int x, int y, int w, int h, uint32_t color1, uint32_t color2, bool vertical = true) = 0;
    virtual void DrawBorder(int x, int y, int w, int h, int thickness, uint32_t color) = 0;
};

// ===== BASE ELEMENT CLASS =====
class UltraCanvasElement {
public:
    int x, y, width, height;
    bool visible = true;
    bool focused = false;
    bool hovered = false;
    bool enabled = true;
    std::string id;
    
    // Hierarchy
    UltraCanvasElement* parent = nullptr;
    std::vector<UltraCanvasElement*> children;

    virtual ~UltraCanvasElement();
    
    // Core virtual methods
    virtual void Render(CanvasContext* ctx);
    virtual bool HandleEvent(const UCEvent& event);
    virtual void SetBounds(int x, int y, int w, int h);
    virtual void PerformLayout() {}
    
    // Hierarchy management
    void AddChild(UltraCanvasElement* child);
    void RemoveChild(UltraCanvasElement* child);
    UltraCanvasElement* FindChildById(const std::string& id);
    
    // Hit testing
    bool Contains(int px, int py) const {
        return px >= x && px <= (x + width) && py >= y && py <= (y + height);
    }
    
    // Focus management
    void SetFocus(bool focus = true);
    void SetHovered(bool hover = true);
    
    // Event callbacks (optional for simpler usage)
    std::function<void(const UCEvent&)> onMouseClick;
    std::function<void(const UCEvent&)> onKeyPress;
    std::function<void()> onFocusGained;
    std::function<void()> onFocusLost;

protected:
    // Helper method for child rendering
    void RenderChildren(CanvasContext* ctx);
    
    // Helper method for child event handling
    bool DispatchEventToChildren(const UCEvent& event);
};

// ===== WINDOW CLASS =====
class UltraCanvasWindow : public UltraCanvasElement {
public:
    UltraCanvasWindow(const std::string& title, int w, int h);
    ~UltraCanvasWindow();
    
    // Window operations
    void Show();
    void Hide();
    void Close();
    void SetTitle(const std::string& title);
    void SetResizable(bool resizable);
    void Minimize();
    void Maximize();
    void Restore();
    
    // Window properties
    bool IsVisible() const { return shown; }
    const std::string& GetWindowTitle() const { return title; }
    
    // Rendering
    void Render(CanvasContext* ctx) override;
    bool HandleEvent(const UCEvent& event) override;
    
    // Platform-specific window handle
    void* nativeHandle = nullptr;
    
    // Window events
    std::function<void()> onClose;
    std::function<void(int w, int h)> onResize;
    
private:
    std::string title;
    bool shown = false;
    bool resizable = true;
    bool decorated = true; // Title bar, borders
};

// ===== BASIC UI COMPONENTS =====

class UltraCanvasListView : public UltraCanvasElement {
public:
    UltraCanvasListView(int x, int y, int w, int h);
    
    void AddItem(const std::string& text);
    void InsertItem(int index, const std::string& text);
    void RemoveItem(int index);
    void Clear();
    void SetSelectedIndex(int index);
    int GetSelectedIndex() const { return selectedIndex; }
    const std::string& GetSelectedItem() const;
    const std::vector<std::string>& GetItems() const { return items; }
    
    void Render(CanvasContext* ctx) override;
    bool HandleEvent(const UCEvent& event) override;
    
    // Styling
    void SetItemHeight(int height) { itemHeight = height; }
    void SetBackgroundColor(uint32_t color) { backgroundColor = color; }
    void SetSelectionColor(uint32_t color) { selectionColor = color; }
    
    // Callbacks
    std::function<void(int index, const std::string& item)> onItemSelected;
    std::function<void(int index, const std::string& item)> onItemDoubleClick;
    
private:
    std::vector<std::string> items;
    int selectedIndex = -1;
    int scrollOffset = 0;
    int itemHeight = 20;
    uint32_t backgroundColor = 0xFFFFFFFF;
    uint32_t selectionColor = 0xFF0078D4;
    
    void UpdateScroll();
    int GetVisibleItemCount() const;
    void EnsureVisible(int index);
};

class UltraCanvasButton : public UltraCanvasElement {
public:
    UltraCanvasButton(int x, int y, int w, int h, const std::string& text);
    
    void SetText(const std::string& text);
    const std::string& GetText() const { return text; }
    
    void Render(CanvasContext* ctx) override;
    bool HandleEvent(const UCEvent& event) override;
    
    std::function<void()> onClicked;
    
private:
    std::string text;
    bool pressed = false;
    uint32_t normalColor = 0xFFE1E1E1;
    uint32_t hoverColor = 0xFFE5F1FB;
    uint32_t pressedColor = 0xFFCCE4F7;
};

class UltraCanvasLabel : public UltraCanvasElement {
public:
    UltraCanvasLabel(int x, int y, int w, int h, const std::string& text);
    
    void SetText(const std::string& text);
    const std::string& GetText() const { return text; }
    void SetTextColor(uint32_t color) { textColor = color; }
    void SetAlignment(int align) { alignment = align; } // 0=left, 1=center, 2=right
    
    void Render(CanvasContext* ctx) override;
    bool HandleEvent(const UCEvent& event) override;
    
private:
    std::string text;
    uint32_t textColor = 0xFF000000;
    int alignment = 0;
};

class UltraCanvasTextInput : public UltraCanvasElement {
public:
    UltraCanvasTextInput(int x, int y, int w, int h);
    
    void SetText(const std::string& text);
    const std::string& GetText() const { return text; }
    void SetPlaceholder(const std::string& placeholder);
    
    void Render(CanvasContext* ctx) override;
    bool HandleEvent(const UCEvent& event) override;
    
    std::function<void(const std::string&)> onTextChanged;
    std::function<void()> onEnterPressed;
    
private:
    std::string text;
    std::string placeholder;
    int cursorPos = 0;
    int selectionStart = -1;
    int selectionEnd = -1;
    bool showCursor = true;
    float cursorTimer = 0;
};

// ===== APPLICATION FRAMEWORK =====
class UltraCanvasApplication {
public:
    static UltraCanvasApplication& Instance();
    
    // Lifecycle
    bool Initialize();
    void Shutdown();
    void Run();
    void Quit() { running = false; }
    bool IsRunning() const { return running; }
    
    // Window management
    UltraCanvasWindow* CreateWindow(const std::string& title, int w, int h);
    void DestroyWindow(UltraCanvasWindow* window);
    
    // Main loop
    void ProcessEvents();
    void Update(float deltaTime);
    void Render();
    
    // Focus management
    void SetFocusedElement(UltraCanvasElement* element);
    UltraCanvasElement* GetFocusedElement() const { return focusedElement; }
    
    // Event system
    void RegisterGlobalEventHandler(std::function<bool(const UCEvent&)> handler);
    
private:
    UltraCanvasApplication() = default;
    
    std::vector<UltraCanvasWindow*> windows;
    std::vector<std::function<bool(const UCEvent&)>> globalEventHandlers;
    UltraCanvasElement* focusedElement = nullptr;
    bool running = false;
    CanvasContext* renderContext = nullptr;
};

// ===== UTILITY FUNCTIONS =====
namespace UltraCanvas {
    // System initialization
    bool Initialize();
    void Shutdown();
    
    // Color utilities
    inline uint32_t RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return (a << 24) | (r << 16) | (g << 8) | b;
    }
    
    inline uint32_t RGB(uint8_t r, uint8_t g, uint8_t b) {
        return RGBA(r, g, b, 255);
    }
    
    // Common colors
    const uint32_t COLOR_WHITE = 0xFFFFFFFF;
    const uint32_t COLOR_BLACK = 0xFF000000;
    const uint32_t COLOR_GRAY = 0xFF808080;
    const uint32_t COLOR_LIGHT_GRAY = 0xFFD3D3D3;
    const uint32_t COLOR_BLUE = 0xFF0078D4;
    const uint32_t COLOR_RED = 0xFFFF0000;
    const uint32_t COLOR_GREEN = 0xFF00FF00;
    
    // Font management
    bool LoadFont(const std::string& name, const std::string& path);
    bool IsFontLoaded(const std::string& name);
    
    // Resource management
    void* LoadImage(const std::string& path);
    void UnloadImage(void* handle);
}