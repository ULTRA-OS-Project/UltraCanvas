// UltraCanvasClipboardManager.h
// Multi-entry clipboard manager with proper UltraCanvas integration
// Version: 2.0.0
// Last Modified: 2025-07-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasKeyboardManager.h"
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <functional>

namespace UltraCanvas {

// ===== CLIPBOARD ENTRY TYPES =====
enum class ClipboardEntryType {
    Text,
    Image,
    RichText,
    FilePath,
    Vector,      // SVG, AI, EPS
    Animation,   // GIF with animation
    Video,       // MP4, AVI, MOV, etc.
    ThreeD,      // 3DS, OBJ, etc.
    Document,    // PDF, HTML, etc.
    Unknown
};

// ===== CLIPBOARD ENTRY DATA =====
struct ClipboardEntry {
    ClipboardEntryType type;
    std::string content;           // Text content or file path for images
    std::vector<uint8_t> rawData;  // Binary data for images
    std::string mimeType;
    std::chrono::system_clock::time_point timestamp;
    std::string thumbnail;         // Path to generated thumbnail for images
    std::string preview;           // Short preview text (first 50 chars for text)
    size_t dataSize;              // Size in bytes
    
    ClipboardEntry() : type(ClipboardEntryType::Unknown), dataSize(0) {
        timestamp = std::chrono::system_clock::now();
    }
    
    ClipboardEntry(ClipboardEntryType t, const std::string& data) 
        : type(t), content(data), dataSize(data.size()) {
        timestamp = std::chrono::system_clock::now();
        GeneratePreview();
    }
    
    void GeneratePreview();
    std::string GetTypeString() const;
    std::string GetFormattedTime() const;
};

// ===== CLIPBOARD ITEM UI COMPONENT =====
class UltraCanvasClipboardItem : public UltraCanvasElement {
private:
    StandardProperties properties;
    ClipboardEntry entry;
    bool isSelected = false;
    
    // Button areas
    Rect2D copyButtonRect;
    Rect2D deleteButtonRect;
    Rect2D saveButtonRect;
    Rect2D contentRect;
    Rect2D thumbnailRect;
    
    // Colors and styling
    Color normalColor = Color(250, 250, 250, 255);
    Color hoverColor = Color(229, 241, 251, 255);
    Color selectedColor = Color(204, 228, 247, 255);
    Color borderColor = Color(200, 200, 200, 255);
    
    Point2D lastMousePos;
    
public:
    UltraCanvasClipboardItem(const std::string& id, long uid, long x, long y, long w, long h, const ClipboardEntry& clipEntry)
        : UltraCanvasElement(id, uid, x, y, w, h), properties(id, uid, x, y, w, h), entry(clipEntry) {
        CalculateLayout();
    }
    
    ULTRACANVAS_STANDARD_PROPERTIES_ACCESSORS()
    
    void CalculateLayout();
    void Render() override;
    void RenderContent();
    void DrawTypeIcon();
    void RenderActionButtons();
    bool OnEvent(const UCEvent& event) override;
    
    void SetSelected(bool selected) { isSelected = selected; }
    bool IsSelected() const { return isSelected; }
    const ClipboardEntry& GetEntry() const { return entry; }
    
    // Event callbacks
    std::function<void(const ClipboardEntry&)> onCopyRequested;
    std::function<void(const ClipboardEntry&)> onSaveRequested;
    std::function<void(const ClipboardEntry&)> onDeleteRequested;
    std::function<void(bool)> onSelectionChanged;
    
private:
    std::string FormatBytes(size_t bytes);
};

// ===== MAIN CLIPBOARD MANAGER CLASS =====
class UltraCanvasClipboardManager {
private:
    static constexpr int MAX_ENTRIES = 100;
    static constexpr int ITEM_HEIGHT = 80;
    static constexpr int WINDOW_WIDTH = 600;
    static constexpr int WINDOW_HEIGHT = 500;
    
    std::vector<ClipboardEntry> entries;
    std::shared_ptr<UltraCanvasWindow> clipboardWindow;
    std::vector<std::shared_ptr<UltraCanvasClipboardItem>> itemComponents;
    bool isWindowVisible = false;
    int scrollOffset = 0;
    
    // System clipboard monitoring
    std::string lastClipboardContent;
    std::chrono::steady_clock::time_point lastCheckTime;
    
public:
    UltraCanvasClipboardManager();
    ~UltraCanvasClipboardManager();
    
    void CreateClipboardWindow();
    void RegisterGlobalHotkey();
    void ToggleClipboardWindow();
    void ShowClipboardWindow();
    void HideClipboardWindow();
    void StartClipboardMonitoring();
    void StopClipboardMonitoring();
    void AddClipboardEntry(const ClipboardEntry& entry);
    void RefreshUI();
    void CopyEntryToClipboard(const ClipboardEntry& entry);
    void SaveEntryToFile(const ClipboardEntry& entry);
    void DeleteEntry(size_t index);
    void CheckClipboardChanges();
    void Update();
    
    // Helper methods
    std::string GenerateSuggestedFilename(const ClipboardEntry& entry);
    std::string GetDefaultExtension(ClipboardEntryType type);
    std::string ShowSaveFileDialog(const std::string& suggestedName, ClipboardEntryType type);
    bool SaveEntryData(const ClipboardEntry& entry, const std::string& filePath);
    bool CopyFile(const std::string& sourcePath, const std::string& destPath);
    void ShowSaveSuccessNotification(const std::string& filePath);
    void ShowSaveErrorNotification();
    
    // Platform-specific functions
    void SetSystemClipboardText(const std::string& text);
    std::string GetSystemClipboardText();
    
    // Public interface
    void AddTextEntry(const std::string& text);
    void AddImageEntry(const std::string& imagePath, const std::vector<uint8_t>& imageData);
    void AddFileEntry(const std::string& filePath);
    size_t GetEntryCount() const { return entries.size(); }
    const std::vector<ClipboardEntry>& GetEntries() const { return entries; }
    void ClearAllEntries();
};

// ===== GLOBAL API FUNCTIONS =====
void InitializeClipboardManager();
void ShutdownClipboardManager();
void UpdateClipboardManager();
UltraCanvasClipboardManager* GetClipboardManager();
void AddClipboardText(const std::string& text);
void AddClipboardImage(const std::string& imagePath);
void AddClipboardFile(const std::string& filePath);
void ShowClipboard();
void HideClipboard();
void ClearClipboardHistory();
size_t GetClipboardEntryCount();

} // namespace UltraCanvas