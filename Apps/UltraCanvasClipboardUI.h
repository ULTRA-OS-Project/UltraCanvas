// UltraCanvasClipboardUI.h
// UI manager for multi-entry clipboard display
// Version: 1.0.0
// Last Modified: 2025-08-13
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasClipboard.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasKeyboardManager.h"
#include <memory>
#include <vector>
#include <functional>

namespace UltraCanvas {

// ===== CLIPBOARD ITEM UI COMPONENT =====
class UltraCanvasClipboardItem : public UltraCanvasElement {
private:
    StandardProperties properties;
    ClipboardData entry;
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
    UltraCanvasClipboardItem(const std::string& id, long uid, long x, long y, long w, long h, const ClipboardData& clipEntry)
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
    const ClipboardData& GetEntry() const { return entry; }

    // Event callbacks
    std::function<void(const ClipboardData&)> onCopyRequested;
    std::function<void(const ClipboardData&)> onSaveRequested;
    std::function<void(const ClipboardData&)> onDeleteRequested;
    std::function<void(bool)> onSelectionChanged;

private:
    std::string FormatBytes(size_t bytes);
};

// ===== MAIN CLIPBOARD UI MANAGER =====
class UltraCanvasClipboardUI {
private:
    static constexpr int ITEM_HEIGHT = 80;
    static constexpr int WINDOW_WIDTH = 600;
    static constexpr int WINDOW_HEIGHT = 500;

    std::shared_ptr<UltraCanvasWindow> clipboardWindow;
    std::vector<std::shared_ptr<UltraCanvasClipboardItem>> itemComponents;
    UltraCanvasClipboard* clipboard;
    bool isWindowVisible = false;
    int scrollOffset = 0;

public:
    UltraCanvasClipboardUI();
    ~UltraCanvasClipboardUI();

    // ===== INITIALIZATION =====
    bool Initialize();
    void Shutdown();

    // ===== WINDOW MANAGEMENT =====
    void CreateClipboardWindow();
    void RegisterGlobalHotkey();
    void ToggleClipboardWindow();
    void ShowClipboardWindow();
    void HideClipboardWindow();

    // ===== UI OPERATIONS =====
    void RefreshUI();
    void Update();

    // ===== EVENT HANDLERS =====
    void OnClipboardChanged(const ClipboardData& newEntry);
    void OnCopyRequested(const ClipboardData& entry);
    void OnSaveRequested(const ClipboardData& entry);
    void OnDeleteRequested(const ClipboardData& entry);

    // ===== FILE OPERATIONS =====
    std::string ShowSaveFileDialog(const std::string& suggestedName, ClipboardDataType type);
    bool SaveEntryToFile(const ClipboardData& entry, const std::string& filePath);
    bool CopyFile(const std::string& sourcePath, const std::string& destPath);
    void ShowSaveSuccessNotification(const std::string& filePath);
    void ShowSaveErrorNotification();

    // ===== ACCESSORS =====
    bool IsWindowVisible() const { return isWindowVisible; }
    std::shared_ptr<UltraCanvasWindow> GetWindow() const { return clipboardWindow; }
};

// ===== GLOBAL UI FUNCTIONS =====
void InitializeClipboardUI();
void ShutdownClipboardUI();
void UpdateClipboardUI();
UltraCanvasClipboardUI* GetClipboardUI();
void ShowClipboard();
void HideClipboard();
void ToggleClipboard();

} // namespace UltraCanvas

