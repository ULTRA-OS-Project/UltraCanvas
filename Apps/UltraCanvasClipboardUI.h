// UltraCanvasClipboardUI.h
// UI manager for multi-entry clipboard display with scrollable container
// Version: 1.1.0
// Last Modified: 2025-08-15
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasClipboard.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasKeyboardManager.h"
#include <memory>
#include <vector>
#include <functional>

namespace UltraCanvas {

// ===== CLIPBOARD ITEM UI COMPONENT =====
    class UltraCanvasClipboardItem : public UltraCanvasUIElement {
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
                : UltraCanvasUIElement(id, uid, x, y, w, h), properties(id, uid, x, y, w, h), entry(clipEntry) {
            CalculateLayout();
        }

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
        static constexpr int ITEM_SPACING = 5;
        static constexpr int WINDOW_WIDTH = 600;
        static constexpr int WINDOW_HEIGHT = 500;
        static constexpr int CONTAINER_PADDING = 10;

        std::shared_ptr<UltraCanvasWindow> clipboardWindow;
        std::shared_ptr<UltraCanvasContainer> scrollableContainer;
        std::vector<std::shared_ptr<UltraCanvasClipboardItem>> itemComponents;
        UltraCanvasClipboard* clipboard;
        bool isWindowVisible = false;

    public:
        UltraCanvasClipboardUI();
        ~UltraCanvasClipboardUI();

        // ===== INITIALIZATION =====
        bool Initialize();
        void Shutdown();

        // ===== WINDOW MANAGEMENT =====
        void CreateClipboardWindow();
        void SetupScrollableContainer();
        void RegisterGlobalHotkey();
        void ShowClipboardWindow();
        void HideClipboardWindow();
        void ToggleClipboardWindow();
        bool IsVisible() const { return isWindowVisible; }

        // ===== UI MANAGEMENT =====
        void RefreshUI();
        void UpdateContainerLayout();
        void ClearClipboardItems();
        void AddClipboardItem(const ClipboardData& entry, size_t index);

        // ===== EVENT HANDLERS =====
        void OnClipboardChanged(const ClipboardData& newEntry);
        void OnCopyRequested(const ClipboardData& entry);
        void OnSaveRequested(const ClipboardData& entry);
        void OnDeleteRequested(const ClipboardData& entry);

        // ===== FILE OPERATIONS =====
        std::string ShowSaveFileDialog(const std::string& suggestedName, ClipboardDataType type);
        bool SaveEntryToFile(const ClipboardData& entry, const std::string& filePath);
        void ShowSaveSuccessNotification(const std::string& filePath);
        void ShowSaveErrorNotification();

        // ===== GLOBAL ACCESS =====
        static UltraCanvasClipboardUI* GetInstance();
        static void SetInstance(std::unique_ptr<UltraCanvasClipboardUI> instance);

        // ===== UTILITIES =====
        void Update();
        void HandleKeyboardInput(const UCEvent& event);
    };

// ===== GLOBAL FUNCTIONS =====
    UltraCanvasClipboardUI* GetClipboardUI();
    void InitializeClipboardUI();
    void ShutdownClipboardUI();

} // namespace UltraCanvas