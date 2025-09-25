// include/UltraCanvasDrawingSurface.h
// Canvas/Drawing Surface implementation with standard UltraCanvas properties
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include <vector>
#include <string>
#include <memory>
#include <stack>

namespace UltraCanvas {

// ===== DRAWING ENUMS AND STRUCTURES =====

enum class DrawingTool {
    NoneTool = 0,
    Pen = 1,
    Brush = 2,
    Eraser = 3,
    Line = 4,
    Rectangle = 5,
    Circle = 6,
    Polygon = 7,
    Text = 8,
    FloodFill = 9,
    Eyedropper = 10
};

enum class BlendMode {
    Normal = 0,
    Multiply = 1,
    Screen = 2,
    Overlay = 3,
    Add = 4,
    Subtract = 5,
    Alpha = 6
};

enum class LineStyle {
    Solid = 0,
    Dashed = 1,
    Dotted = 2,
    DashDot = 3,
    Custom = 4
};

struct DrawingState {
    Color foregroundColor;
    Color backgroundColor;
    float lineWidth;
    LineStyle lineStyle;
    BlendMode blendMode;
    DrawingTool currentTool;
    float brushSize;
    float brushOpacity;
};

// ===== DRAWING SURFACE CLASS =====
class UltraCanvasDrawingSurface : public UltraCanvasUIElement {
private:
    // ===== STANDARD ULTRACANVAS PROPERTIES =====
    std::string Identifier;          // [text] Element identifier
    long IdentifierID;              // [longint] Unique ID
    long x_pos;                     // [longint] X position
    long y_pos;                     // [longint] Y position  
    long width_size;                // [longint] Width
    long height_size;               // [longint] Height
    bool Active;                    // [boolean] Is element active
    bool Visible;                   // [boolean] Is element visible
    int MousePointer;               // [int] Mouse cursor type when hovering
    int MouseControls;              // [int] Mouse interaction type
    long ParentObject;              // [longint] Reference to parent
    long z_index;                   // [longint] Depth/layer order (higher = front)
    std::string Script;             // [text] Property description text
    std::vector<uint8_t> Cache;     // [image] Compressed bitmap cache
    
    // ===== CANVAS-SPECIFIC PROPERTIES =====
    std::vector<uint32_t> pixelBuffer;  // Main drawing buffer (ARGB format)
    std::vector<uint32_t> layerBuffer;  // Additional layer for compositing
    int bufferWidth, bufferHeight;      // Buffer dimensions
    
    DrawingState currentState;          // Current drawing parameters
    std::stack<DrawingState> stateStack; // State save/restore stack
    
    std::vector<Point2Df> currentPath;  // Current drawing path
    bool isDrawing;                     // Currently in drawing operation
    Point2D lastDrawPoint;              // Last mouse position for drawing
    
    // Undo/Redo system
    std::vector<std::vector<uint32_t>> undoStack;
    std::vector<std::vector<uint32_t>> redoStack;
    int maxUndoLevels;
    
    // Selection area
    Point2D selectionStart, selectionEnd;
    bool hasSelection;
    
public:
    // ===== CONSTRUCTOR =====
    UltraCanvasDrawingSurface(const std::string& identifier, long id, 
                             long x, long y, long w, long h) {
        // Standard properties initialization
        Identifier = identifier;
        IdentifierID = id;
        x_pos = x;
        y_pos = y;
        width_size = w;
        height_size = h;
        Active = true;
        Visible = true;
        MousePointer = 1; // Custom drawing cursor
        MouseControls = 4; // object-2D controls
        ParentObject = -1; // No parent by default
        z_index = 0; // Default layer (0 = background, higher = front)
        Script = "DrawingSurface: " + identifier;
        
        // Canvas-specific initialization
        bufferWidth = w;
        bufferHeight = h;
        pixelBuffer.resize(bufferWidth * bufferHeight, 0xFFFFFFFF); // White background
        layerBuffer.resize(bufferWidth * bufferHeight, 0x00000000); // Transparent layer
        
        // Default drawing state
        currentState.foregroundColor = Color(0, 0, 0, 255);    // Black
        currentState.backgroundColor = Color(255, 255, 255, 255); // White
        currentState.lineWidth = 1.0f;
        currentState.lineStyle = LineStyle::Solid;
        currentState.blendMode = BlendMode::Normal;
        currentState.currentTool = DrawingTool::Pen;
        currentState.brushSize = 5.0f;
        currentState.brushOpacity = 1.0f;
        
        isDrawing = false;
        hasSelection = false;
        maxUndoLevels = 50;
        
        // Initialize base class properties
        this->x = x;
        this->y = y;
        this->width = w;
        this->height = h;
        this->visible = true;
        this->id = identifier;
    }
    
    // ===== STANDARD PROPERTY ACCESSORS =====
    const std::string& GetIdentifier() const { return Identifier; }
    void SetIdentifier(const std::string& id) { Identifier = id; this->id = id; }
    
    long GetIdentifierID() const { return IdentifierID; }
    void SetIdentifierID(long id) { IdentifierID = id; }
    
    long GetXPos() const { return x_pos; }
    void SetXPos(long x) { x_pos = x; this->x = x; }
    
    long GetYPos() const { return y_pos; }
    void SetYPos(long y) { y_pos = y; this->y = y; }
    
    long GetWidthSize() const { return width_size; }
    void SetWidthSize(long w) { 
        width_size = w; 
        this->width = w;
        ResizeBuffer(w, height_size);
    }
    
    long GetHeightSize() const { return height_size; }
    void SetHeightSize(long h) { 
        height_size = h; 
        this->height = h;
        ResizeBuffer(width_size, h);
    }
    
    bool IsActive() const { return Active; }
    void SetActive(bool active) { Active = active; }
    
    bool IsVisible() const { return Visible; }
    void SetVisible(bool visible) { Visible = visible; this->visible = visible; }
    
    int GetMousePointer() const { return MousePointer; }
    void SetMousePointer(int pointer) { MousePointer = pointer; }
    
    int GetMouseControls() const { return MouseControls; }
    void SetMouseControls(int controls) { MouseControls = controls; }
    
    long GetParentObject() const { return ParentObject; }
    void SetParentObject(long parent) { ParentObject = parent; }
    
    long GetZIndex() const { return z_index; }
    void SetZIndex(long zIndex) { z_index = zIndex; }
    
    const std::string& GetScript() const { return Script; }
    void SetScript(const std::string& script) { Script = script; }
    
    // ===== DRAWING OPERATIONS =====
    
    // Basic pixel operations
    void SetPixel(int x, int y, const Color& color) {
        if (x >= 0 && x < bufferWidth && y >= 0 && y < bufferHeight) {
            int index = y * bufferWidth + x;
            pixelBuffer[index] = color.ToARGB();
        }
    }
    
    Color GetPixel(int x, int y) const {
        if (x >= 0 && x < bufferWidth && y >= 0 && y < bufferHeight) {
            int index = y * bufferWidth + x;
            return Color::FromARGB(pixelBuffer[index]);
        }
        return Color(0, 0, 0, 0);
    }
    
    // Line drawing
    void DrawLine(const Point2D& start, const Point2D& end) {
        ctx->DrawLine(start.x, start.y, end.x, end.y, currentState.foregroundColor);
    }
    
    void DrawLine(float x1, float y1, float x2, float y2, const Color& color) {
        // Bresenham's line algorithm
        int ix1 = (int)x1, iy1 = (int)y1;
        int ix2 = (int)x2, iy2 = (int)y2;
        
        int dx = abs(ix2 - ix1);
        int dy = abs(iy2 - iy1);
        int sx = (ix1 < ix2) ? 1 : -1;
        int sy = (iy1 < iy2) ? 1 : -1;
        int err = dx - dy;
        
        while (true) {
            SetPixel(ix1, iy1, color);
            
            if (ix1 == ix2 && iy1 == iy2) break;
            
            int e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                ix1 += sx;
            }
            if (e2 < dx) {
                err += dx;
                iy1 += sy;
            }
        }
    }
    
    // Shape drawing
    void ctx->DrawRectangle(int x, int y, int w, int h, bool filled = false) {
        if (filled) {
            for (int py = y; py < y + h; py++) {
                for (int px = x; px < x + w; px++) {
                    SetPixel(px, py, currentState.foregroundColor);
                }
            }
        } else {
            // Draw outline
            ctx->DrawLine(x, y, x + w - 1, y, currentState.foregroundColor);         // Top
            ctx->DrawLine(x + w - 1, y, x + w - 1, y + h - 1, currentState.foregroundColor); // Right
            ctx->DrawLine(x + w - 1, y + h - 1, x, y + h - 1, currentState.foregroundColor); // Bottom
            ctx->DrawLine(x, y + h - 1, x, y, currentState.foregroundColor);         // Left
        }
    }
    
    void DrawCircle(int cx, int cy, int radius, bool filled = false) {
        // Midpoint circle algorithm
        int x = 0;
        int y = radius;
        int d = 1 - radius;
        
        auto plotCirclePoints = [&](int px, int py) {
            if (filled) {
                ctx->DrawLine(cx - px, cy + py, cx + px, cy + py, currentState.foregroundColor);
                ctx->DrawLine(cx - px, cy - py, cx + px, cy - py, currentState.foregroundColor);
            } else {
                SetPixel(cx + px, cy + py, currentState.foregroundColor);
                SetPixel(cx - px, cy + py, currentState.foregroundColor);
                SetPixel(cx + px, cy - py, currentState.foregroundColor);
                SetPixel(cx - px, cy - py, currentState.foregroundColor);
                SetPixel(cx + py, cy + px, currentState.foregroundColor);
                SetPixel(cx - py, cy + px, currentState.foregroundColor);
                SetPixel(cx + py, cy - px, currentState.foregroundColor);
                SetPixel(cx - py, cy - px, currentState.foregroundColor);
            }
        };
        
        while (x <= y) {
            plotCirclePoints(x, y);
            
            if (d < 0) {
                d += 2 * x + 3;
            } else {
                d += 2 * (x - y) + 5;
                y--;
            }
            x++;
        }
    }
    
    void DrawPolygon(const std::vector<Point2D>& points, bool filled = false) {
        if (points.size() < 3) return;
        
        if (filled) {
            // Simple scanline fill algorithm
            int minY = bufferHeight, maxY = 0;
            for (const auto& p : points) {
                minY = std::min(minY, (int)p.y);
                maxY = std::max(maxY, (int)p.y);
            }
            
            for (int y = minY; y <= maxY; y++) {
                std::vector<int> intersections;
                
                for (size_t i = 0; i < points.size(); i++) {
                    size_t j = (i + 1) % points.size();
                    const Point2D& p1 = points[i];
                    const Point2D& p2 = points[j];
                    
                    if ((p1.y <= y && p2.y > y) || (p2.y <= y && p1.y > y)) {
                        float x = p1.x + (y - p1.y) * (p2.x - p1.x) / (p2.y - p1.y);
                        intersections.push_back((int)x);
                    }
                }
                
                std::sort(intersections.begin(), intersections.end());
                
                for (size_t i = 0; i < intersections.size(); i += 2) {
                    if (i + 1 < intersections.size()) {
                        ctx->DrawLine(intersections[i], y, intersections[i + 1], y, currentState.foregroundColor);
                    }
                }
            }
        } else {
            // Draw outline
            for (size_t i = 0; i < points.size(); i++) {
                size_t j = (i + 1) % points.size();
                ctx->DrawLine(points[i], points[j]);
            }
        }
    }
    
    // Brush drawing
    void DrawBrush(float x, float y) {
        int brushRadius = (int)(currentState.brushSize / 2);
        
        for (int dy = -brushRadius; dy <= brushRadius; dy++) {
            for (int dx = -brushRadius; dx <= brushRadius; dx++) {
                float distance = sqrt(dx * dx + dy * dy);
                if (distance <= brushRadius) {
                    float alpha = (1.0f - distance / brushRadius) * currentState.brushOpacity;
                    Color brushColor = currentState.foregroundColor;
                    brushColor.a = (uint8_t)(brushColor.a * alpha);
                    
                    int px = (int)x + dx;
                    int py = (int)y + dy;
                    
                    if (px >= 0 && px < bufferWidth && py >= 0 && py < bufferHeight) {
                        // Alpha blend with existing pixel
                        Color existing = GetPixel(px, py);
                        Color blended = BlendColors(existing, brushColor);
                        SetPixel(px, py, blended);
                    }
                }
            }
        }
    }
    
    // Flood fill
    void FloodFill(int x, int y, const Color& fillColor) {
        if (x < 0 || x >= bufferWidth || y < 0 || y >= bufferHeight) return;
        
        Color targetColor = GetPixel(x, y);
        if (targetColor.ToARGB() == fillColor.ToARGB()) return;
        
        std::stack<Point2D> stack;
        stack.push(Point2D(x, y));
        
        while (!stack.empty()) {
            Point2D p = stack.top();
            stack.pop();
            
            int px = (int)p.x, py = (int)p.y;
            if (px < 0 || px >= bufferWidth || py < 0 || py >= bufferHeight) continue;
            if (GetPixel(px, py).ToARGB() != targetColor.ToARGB()) continue;
            
            SetPixel(px, py, fillColor);
            
            stack.push(Point2D(px + 1, py));
            stack.push(Point2D(px - 1, py));
            stack.push(Point2D(px, py + 1));
            stack.push(Point2D(px, py - 1));
        }
    }
    
    // ===== BUFFER MANAGEMENT =====
    void Clear(const Color& color = Color(255, 255, 255, 255)) {
        uint32_t clearColor = color.ToARGB();
        std::fill(pixelBuffer.begin(), pixelBuffer.end(), clearColor);
    }
    
    void ResizeBuffer(int newWidth, int newHeight) {
        std::vector<uint32_t> newBuffer(newWidth * newHeight, 0xFFFFFFFF);
        
        // Copy existing content
        int copyWidth = std::min(bufferWidth, newWidth);
        int copyHeight = std::min(bufferHeight, newHeight);
        
        for (int y = 0; y < copyHeight; y++) {
            for (int x = 0; x < copyWidth; x++) {
                int oldIndex = y * bufferWidth + x;
                int newIndex = y * newWidth + x;
                newBuffer[newIndex] = pixelBuffer[oldIndex];
            }
        }
        
        pixelBuffer = std::move(newBuffer);
        layerBuffer.resize(newWidth * newHeight, 0x00000000);
        bufferWidth = newWidth;
        bufferHeight = newHeight;
    }
    
    // ===== UNDO/REDO SYSTEM =====
    void SaveState() {
        if (undoStack.size() >= maxUndoLevels) {
            undoStack.erase(undoStack.begin());
        }
        undoStack.push_back(pixelBuffer);
        redoStack.clear(); // Clear redo stack when new action is performed
    }
    
    void Undo() {
        if (!undoStack.empty()) {
            redoStack.push_back(pixelBuffer);
            pixelBuffer = undoStack.back();
            undoStack.pop_back();
        }
    }
    
    void Redo() {
        if (!redoStack.empty()) {
            undoStack.push_back(pixelBuffer);
            pixelBuffer = redoStack.back();
            redoStack.pop_back();
        }
    }
    
    // ===== STATE MANAGEMENT =====
    void PushState() {
        stateStack.push(currentState);
    }
    
    void PopState() {
        if (!stateStack.empty()) {
            currentState = stateStack.top();
            stateStack.pop();
        }
    }
    
    // ===== DRAWING STATE SETTERS =====
    void SetForegroundColor(const Color& color) { currentState.foregroundColor = color; }
    void SetBackgroundColor(const Color& color) { currentState.backgroundColor = color; }
    void SetLineWidth(float width) { currentState.lineWidth = width; }
    void SetLineStyle(LineStyle style) { currentState.lineStyle = style; }
    void SetBlendMode(BlendMode mode) { currentState.blendMode = mode; }
    void SetCurrentTool(DrawingTool tool) { currentState.currentTool = tool; }
    void SetBrushSize(float size) { currentState.brushSize = size; }
    void SetBrushOpacity(float opacity) { currentState.brushOpacity = opacity; }
    
    // ===== DRAWING STATE GETTERS =====
    const DrawingState& GetCurrentState() const { return currentState; }
    Color GetForegroundColor() const { return currentState.foregroundColor; }
    Color GetBackgroundColor() const { return currentState.backgroundColor; }
    float GetLineWidth() const { return currentState.lineWidth; }
    LineStyle GetLineStyle() const { return currentState.lineStyle; }
    BlendMode GetBlendMode() const { return currentState.blendMode; }
    DrawingTool GetCurrentTool() const { return currentState.currentTool; }
    float GetBrushSize() const { return currentState.brushSize; }
    float GetBrushOpacity() const { return currentState.brushOpacity; }
    
    // ===== FILE I/O =====
    bool SaveToFile(const std::string& filename) {
        // Implementation would save buffer to image file
        // Would integrate with your existing image plugin system
        return true;
    }
    
    bool LoadFromFile(const std::string& filename) {
        // Implementation would load image file to buffer
        // Would use your existing image loading plugins
        return true;
    }
    
    // ===== EVENT HANDLING =====
    bool OnEvent(const UCEvent& event) override {
        if (!Active || !Visible) return false;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;
            case UCEventType::MouseMove:
                HandleMouseMove(event);
                break;
            case UCEventType::MouseUp:
                HandleMouseUp(event);
                break;
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
            default:
                break;
        }
        return false;
    }
    
    // ===== RENDERING =====
    void Render() override {
        if (!Visible) return;
        
        // This would integrate with your existing render bridge
        // Drawing the pixel buffer to the screen
        UltraCanvas::DrawImageFromFile("", x_pos, y_pos, width_size, height_size);
        
        // Update cache if needed
        UpdateCache();
    }
    
private:
    // ===== HELPER METHODS =====
    Color BlendColors(const Color& base, const Color& overlay) {
        if (overlay.a == 0) return base;
        if (overlay.a == 255) return overlay;
        
        float alpha = overlay.a / 255.0f;
        float invAlpha = 1.0f - alpha;
        
        return Color(
            (uint8_t)(overlay.r * alpha + base.r * invAlpha),
            (uint8_t)(overlay.g * alpha + base.g * invAlpha),
            (uint8_t)(overlay.b * alpha + base.b * invAlpha),
            255
        );
    }
    
    void HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        
        int localX = event.x - x_pos;
        int localY = event.y - y_pos;
        
        SaveState(); // Save for undo
        isDrawing = true;
        lastDrawPoint = Point2D(localX, localY);
        
        switch (currentState.currentTool) {
            case DrawingTool::Pen:
            case DrawingTool::Brush:
                DrawBrush(localX, localY);
                break;
            case DrawingTool::FloodFill:
                FloodFill(localX, localY, currentState.foregroundColor);
                break;
            default:
                break;
        }
    }
    
    void HandleMouseMove(const UCEvent& event) {
        if (!isDrawing || !Contains(event.x, event.y)) return;
        
        int localX = event.x - x_pos;
        int localY = event.y - y_pos;
        Point2D currentPoint(localX, localY);
        
        switch (currentState.currentTool) {
            case DrawingTool::Pen:
                ctx->DrawLine(lastDrawPoint, currentPoint);
                break;
            case DrawingTool::Brush:
                DrawBrush(localX, localY);
                break;
            default:
                break;
        }
        
        lastDrawPoint = currentPoint;
    }
    
    void HandleMouseUp(const UCEvent& event) {
        isDrawing = false;
    }
    
    void HandleKeyDown(const UCEvent& event) {
        if (event.ctrl) {
            switch (event.virtualKey) {
                case 'z': case 'Z':
                    if (event.shift) Redo(); else Undo();
                    break;
                case 'c': case 'C':
                    // Copy selection
                    break;
                case 'v': case 'V':
                    // Paste
                    break;
            }
        }
    }
    
    void UpdateCache() {
        // Convert pixel buffer to compressed cache format
        // This would integrate with your existing image compression
    }
};

// ===== FACTORY FUNCTION =====
std::shared_ptr<UltraCanvasDrawingSurface> CreateDrawingSurface(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return std::make_shared<UltraCanvasDrawingSurface>(identifier, id, x, y, w, h);
}

} // namespace UltraCanvas