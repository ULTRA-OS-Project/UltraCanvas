// UltraCanvasClipboardUI.cpp
// UI manager implementation for multi-entry clipboard with scrollable container
// Version: 1.1.0
// Last Modified: 2025-08-15
// Author: UltraCanvas Framework

#include "UltraCanvasClipboardUI.h"
#include "UltraCanvasUI.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>

#ifdef __linux__
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace UltraCanvas {

// ===== GLOBAL UI INSTANCE =====
    static std::unique_ptr<UltraCanvasClipboardUI> g_clipboardUI = nullptr;

// ===== CLIPBOARD ITEM IMPLEMENTATION =====
    void UltraCanvasClipboardItem::CalculateLayout() {
        Rect2D bounds = GetBounds();

        // Thumbnail area (left side for visual content)
        if (entry.type == ClipboardDataType::Image ||
            entry.type == ClipboardDataType::Vector ||
            entry.type == ClipboardDataType::Animation ||
            entry.type == ClipboardDataType::Video ||
            entry.type == ClipboardDataType::ThreeD ||
            entry.type == ClipboardDataType::Document) {
            thumbnailRect = Rect2D(bounds.x + 5, bounds.y + 5, 60, bounds.height - 10);
            contentRect = Rect2D(bounds.x + 70, bounds.y + 5, bounds.width - 155, bounds.height - 10);
        } else {
            thumbnailRect = Rect2D(0, 0, 0, 0); // No thumbnail
            contentRect = Rect2D(bounds.x + 10, bounds.y + 5, bounds.width - 105, bounds.height - 10);
        }

        // Button areas (right side) - 3 buttons: Copy, Save, Delete
        copyButtonRect = Rect2D(bounds.x + bounds.width - 85, bounds.y + 5, 20, 20);
        saveButtonRect = Rect2D(bounds.x + bounds.width - 55, bounds.y + 5, 20, 20);
        deleteButtonRect = Rect2D(bounds.x + bounds.width - 25, bounds.y + 5, 20, 20);
    }

    void UltraCanvasClipboardItem::Render() {
        ULTRACANVAS_RENDER_SCOPE();

        Rect2D bounds = GetBounds();

        // Background
        Color bgColor = isSelected ? selectedColor : (IsHovered() ?
                                                      hoverColor : normalColor);
        SetFillColor(bgColor);
        DrawRectangle(bounds);

        // Border
        SetStrokeColor(borderColor);
        SetStrokeWidth(1.0f);
        DrawRectangle(bounds);

        // Render content
        RenderContent();

        // Render type icon if thumbnail area exists
        if (thumbnailRect.width > 0) {
            DrawTypeIcon();
        }

        // Render action buttons
        RenderActionButtons();
    }

    void UltraCanvasClipboardItem::RenderContent() {
        SetTextColor(Colors::Black);
        SetFont("Arial", 11.0f);

        // Display entry information based on type
        std::string displayText;
        std::string details;

        switch (entry.type) {
            case ClipboardDataType::Text:
                displayText = "Text: " + entry.textData.substr(0, 50);
                if (entry.textData.length() > 50) displayText += "...";
                details = FormatBytes(entry.textData.size());
                break;

            case ClipboardDataType::Image:
                displayText = "Image: " + (entry.filename.empty() ? "Clipboard Image" : entry.filename);
                details = FormatBytes(entry.rawData.size());
                break;

            case ClipboardDataType::Vector:
                displayText = "Vector: " + (entry.filename.empty() ? "SVG Image" : entry.filename);
                details = FormatBytes(entry.rawData.size());
                break;

            case ClipboardDataType::Document:
                displayText = "Document: " + (entry.filename.empty() ? "Document" : entry.filename);
                details = FormatBytes(entry.rawData.size());
                break;

            case ClipboardDataType::Animation:
                displayText = "Animation: " + (entry.filename.empty() ? "Animated Image" : entry.filename);
                details = FormatBytes(entry.rawData.size());
                break;

            case ClipboardDataType::Video:
                displayText = "Video: " + (entry.filename.empty() ? "Video Clip" : entry.filename);
                details = FormatBytes(entry.rawData.size());
                break;

            case ClipboardDataType::Audio:
                displayText = "Audio: " + (entry.filename.empty() ? "Audio Clip" : entry.filename);
                details = FormatBytes(entry.rawData.size());
                break;

            case ClipboardDataType::ThreeD:
                displayText = "3D Model: " + (entry.filename.empty() ? "3D Object" : entry.filename);
                details = FormatBytes(entry.rawData.size());
                break;

            default:
                displayText = "Unknown Type";
                details = FormatBytes(entry.rawData.size());
                break;
        }

        // Draw main text
        DrawText(displayText, Point2D(contentRect.x, contentRect.y + 15));

        // Draw details in smaller font
        SetFont("Arial", 9.0f);
        SetTextColor(Color(100, 100, 100, 255));
        DrawText(details, Point2D(contentRect.x, contentRect.y + 35));

        // Draw timestamp
        char timeStr[64];
        std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", std::localtime(&entry.timestamp));
        DrawText(timeStr, Point2D(contentRect.x, contentRect.y + 50));
    }

    void UltraCanvasClipboardItem::DrawTypeIcon() {
        // Simple type indicators in thumbnail area
        SetFillColor(Color(220, 220, 220, 255));
        DrawRectangle(thumbnailRect);

        SetTextColor(Colors::Black);
        SetFont("Arial", 10.0f);

        std::string icon;
        switch (entry.type) {
            case ClipboardDataType::Image: icon = "IMG"; break;
            case ClipboardDataType::Vector: icon = "SVG"; break;
            case ClipboardDataType::Document: icon = "DOC"; break;
            case ClipboardDataType::Animation: icon = "GIF"; break;
            case ClipboardDataType::Video: icon = "VID"; break;
            case ClipboardDataType::Audio: icon = "AUD"; break;
            case ClipboardDataType::ThreeD: icon = "3D"; break;
            default: icon = "?"; break;
        }

        float centerX = thumbnailRect.x + thumbnailRect.width / 2 - 10;
        float centerY = thumbnailRect.y + thumbnailRect.height / 2 + 5;
        DrawText(icon, Point2D(centerX, centerY));
    }

    void UltraCanvasClipboardItem::RenderActionButtons() {
        // Copy button
        Color copyColor = copyButtonRect.Contains(lastMousePos) ? Color(100, 200, 100, 255) : Color(150, 150, 150, 255);
        SetFillColor(copyColor);
        DrawRectangle(copyButtonRect);
        SetTextColor(Colors::White);
        SetFont("Arial", 10.0f);
        DrawText("C", Point2D(copyButtonRect.x + 6, copyButtonRect.y + 14));

        // Save button
        Color saveColor = saveButtonRect.Contains(lastMousePos) ? Color(100, 150, 200, 255) : Color(150, 150, 150, 255);
        SetFillColor(saveColor);
        DrawRectangle(saveButtonRect);
        SetTextColor(Colors::White);
        SetFont("Arial", 10.0f);
        DrawText("S", Point2D(saveButtonRect.x + 6, saveButtonRect.y + 14));

        // Delete button
        Color deleteColor = deleteButtonRect.Contains(lastMousePos) ? Color(200, 100, 100, 255) : Color(150, 150, 150, 255);
        SetFillColor(deleteColor);
        DrawRectangle(deleteButtonRect);
        SetTextColor(Colors::White);
        SetFont("Arial", 10.0f);
        DrawText("X", Point2D(deleteButtonRect.x + 6, deleteButtonRect.y + 14));
    }

    bool UltraCanvasClipboardItem::OnEvent(const UCEvent& event) {
        lastMousePos = Point2D(event.x, event.y);

        if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
            if (copyButtonRect.Contains(Point2D(event.x, event.y))) {
                if (onCopyRequested) onCopyRequested(entry);
            } else if (saveButtonRect.Contains(Point2D(event.x, event.y))) {
                if (onSaveRequested) onSaveRequested(entry);
            } else if (deleteButtonRect.Contains(Point2D(event.x, event.y))) {
                if (onDeleteRequested) onDeleteRequested(entry);
            } else if (GetBounds().Contains(Point2D(event.x, event.y))) {
                isSelected = !isSelected;
                if (onSelectionChanged) onSelectionChanged(isSelected);
            }
            return true;
        }
        return false;
    }

    std::string UltraCanvasClipboardItem::FormatBytes(size_t bytes) {
        if (bytes < 1024) return std::to_string(bytes) + " B";
        if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
        return std::to_string(bytes / (1024 * 1024)) + " MB";
    }

// ===== CLIPBOARD UI MANAGER IMPLEMENTATION =====
    UltraCanvasClipboardUI::UltraCanvasClipboardUI() : clipboard(nullptr) {
    }

    UltraCanvasClipboardUI::~UltraCanvasClipboardUI() {
        Shutdown();
    }

    bool UltraCanvasClipboardUI::Initialize() {
        std::cout << "UltraCanvas: Initializing clipboard UI with scrollable container..." << std::endl;

        // Get the clipboard instance
        clipboard = GetClipboard();
        if (!clipboard) {
            std::cerr << "UltraCanvas: No clipboard instance available" << std::endl;
            return false;
        }

        // Create the clipboard window and scrollable container
        CreateClipboardWindow();
        SetupScrollableContainer();
        RegisterGlobalHotkey();

        // Set up clipboard change callback
        clipboard->SetChangeCallback([this](const ClipboardData& newEntry) {
            OnClipboardChanged(newEntry);
        });

        std::cout << "UltraCanvas: Clipboard UI initialized successfully" << std::endl;
        return true;
    }

    void UltraCanvasClipboardUI::Shutdown() {
        std::cout << "UltraCanvas: Shutting down clipboard UI..." << std::endl;

        if (clipboardWindow) {
            clipboardWindow->Close();
            clipboardWindow.reset();
        }

        scrollableContainer.reset();
        ClearClipboardItems();
        clipboard = nullptr;
    }

    void UltraCanvasClipboardUI::CreateClipboardWindow() {
        // Create window configuration
        WindowConfig config;
        config.title = "Multi-Entry Clipboard";
        config.width = WINDOW_WIDTH;
        config.height = WINDOW_HEIGHT;
        config.resizable = true;
        config.alwaysOnTop = true;
        config.type = WindowType::Tool;

        // Create window using proper API
        clipboardWindow = std::make_shared<UltraCanvasWindow>();
        if (!clipboardWindow->Create(config)) {
            std::cerr << "Failed to create clipboard window" << std::endl;
            return;
        }

        clipboardWindow->SetVisible(false);
    }

    void UltraCanvasClipboardUI::SetupScrollableContainer() {
        if (!clipboardWindow) return;

        // Create scrollable container that fills the window
        scrollableContainer = CreateScrollableContainer(
                "clipboardScrollContainer",
                2000,
                CONTAINER_PADDING,
                CONTAINER_PADDING,
                WINDOW_WIDTH - 2 * CONTAINER_PADDING,
                WINDOW_HEIGHT - 2 * CONTAINER_PADDING,
                true // Auto-hide scrollbars
        );

        // Configure container style for clipboard UI
        ContainerStyle style = scrollableContainer->GetContainerStyle();
        style.backgroundColor = Color(245, 245, 245, 255);
        style.borderColor = Color(180, 180, 180, 255);
        style.borderWidth = 1.0f;
        style.paddingLeft = 5.0f;
        style.paddingTop = 5.0f;
        style.paddingRight = 5.0f;
        style.paddingBottom = 5.0f;
        style.scrollSpeed = 30.0f;
        style.smoothScrolling = true;
        scrollableContainer->SetContainerStyle(style);

        // Add scroll change callback
        scrollableContainer->onScrollChanged = [this](float horizontal, float vertical) {
            // Could add scroll position persistence here if needed
        };

        // Add container to window
        clipboardWindow->AddElement(scrollableContainer);
    }

    void UltraCanvasClipboardUI::RegisterGlobalHotkey() {
        auto keyboardManager = GetKeyboardManager();
        if (keyboardManager) {
            keyboardManager->RegisterGlobalHotkey("ALT+P", [this]() {
                ToggleClipboardWindow();
            });
        }
        std::cout << "Registered ALT+P shortcut for clipboard UI" << std::endl;
    }

    void UltraCanvasClipboardUI::ToggleClipboardWindow() {
        if (isWindowVisible) {
            HideClipboardWindow();
        } else {
            ShowClipboardWindow();
        }
    }

    void UltraCanvasClipboardUI::ShowClipboardWindow() {
        if (!clipboardWindow) return;

        RefreshUI();
        clipboardWindow->Show();
        isWindowVisible = true;
    }

    void UltraCanvasClipboardUI::HideClipboardWindow() {
        if (!clipboardWindow) return;

        clipboardWindow->Hide();
        isWindowVisible = false;
    }

    void UltraCanvasClipboardUI::RefreshUI() {
        if (!scrollableContainer || !clipboard) return;

        // Clear existing items
        ClearClipboardItems();

        // Create new items for each clipboard entry
        const auto& entries = clipboard->GetEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            AddClipboardItem(entries[i], i);
        }

        // Update container layout to accommodate all items
        UpdateContainerLayout();
    }

    void UltraCanvasClipboardUI::UpdateContainerLayout() {
        if (!scrollableContainer) return;

        // Calculate total content height needed
        size_t itemCount = itemComponents.size();
        float totalHeight = static_cast<float>(itemCount * (ITEM_HEIGHT + ITEM_SPACING));

        // Update scrollability based on content size
        // The container will automatically handle scrollbar visibility
        scrollableContainer->UpdateScrollability();
    }

    void UltraCanvasClipboardUI::ClearClipboardItems() {
        if (!scrollableContainer) return;

        // Remove all items from container
        for (auto& item : itemComponents) {
            scrollableContainer->RemoveChild(item);
        }
        itemComponents.clear();
    }

    void UltraCanvasClipboardUI::AddClipboardItem(const ClipboardData& entry, size_t index) {
        if (!scrollableContainer) return;

        // Calculate position for this item
        float yPos = static_cast<float>(index * (ITEM_HEIGHT + ITEM_SPACING));
        float itemWidth = static_cast<float>(WINDOW_WIDTH - 2 * CONTAINER_PADDING - 20); // Account for scrollbar

        // Create new clipboard item
        auto item = std::make_shared<UltraCanvasClipboardItem>(
                "clipboardItem_" + std::to_string(index),
                static_cast<long>(1000 + index),
                0, // X position relative to container
                static_cast<long>(yPos), // Y position in container
                static_cast<long>(itemWidth),
                ITEM_HEIGHT,
                entry
        );

        // Set up event handlers
        item->onCopyRequested = [this](const ClipboardData& entry) {
            OnCopyRequested(entry);
        };

        item->onSaveRequested = [this](const ClipboardData& entry) {
            OnSaveRequested(entry);
        };

        item->onDeleteRequested = [this](const ClipboardData& entry) {
            OnDeleteRequested(entry);
        };

        // Add to container and track in our list
        scrollableContainer->AddChild(item);
        itemComponents.push_back(item);
    }

// ===== EVENT HANDLERS =====
    void UltraCanvasClipboardUI::OnClipboardChanged(const ClipboardData& newEntry) {
        // Refresh UI if window is visible
        if (isWindowVisible) {
            RefreshUI();
        }
    }

    void UltraCanvasClipboardUI::OnCopyRequested(const ClipboardData& entry) {
        if (!clipboard) return;

        // Find the entry index and copy it back to clipboard
        const auto& entries = clipboard->GetEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (&entries[i] == &entry) {
                clipboard->CopyEntryToClipboard(i);
                HideClipboardWindow();
                break;
            }
        }
    }

    void UltraCanvasClipboardUI::OnSaveRequested(const ClipboardData& entry) {
        if (!clipboard) return;

        std::string suggestedFilename = clipboard->GenerateSuggestedFilename(entry);
        std::string savePath = ShowSaveFileDialog(suggestedFilename, entry.type);

        if (!savePath.empty()) {
            bool success = SaveEntryToFile(entry, savePath);
            if (success) {
                ShowSaveSuccessNotification(savePath);
            } else {
                ShowSaveErrorNotification();
            }
        }
    }

    void UltraCanvasClipboardUI::OnDeleteRequested(const ClipboardData& entry) {
        if (!clipboard) return;

        // Find the entry index and remove it
        const auto& entries = clipboard->GetEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (&entries[i] == &entry) {
                clipboard->RemoveEntry(i);
                RefreshUI();
                break;
            }
        }
    }

// ===== FILE OPERATIONS =====
    std::string UltraCanvasClipboardUI::ShowSaveFileDialog(const std::string& suggestedName, ClipboardDataType type) {
        // Simple implementation - save to Downloads folder
        // In a real implementation, this would show a file dialog

#ifdef __linux__
        std::string homeDir = getenv("HOME") ? getenv("HOME") : "/tmp";
        return homeDir + "/Downloads/" + suggestedName;
#else
        return suggestedName;
#endif
    }

    bool UltraCanvasClipboardUI::SaveEntryToFile(const ClipboardData& entry, const std::string& filePath) {
        try {
            std::ofstream file(filePath, std::ios::binary);
            if (!file) return false;

            if (entry.type == ClipboardDataType::Text) {
                file << entry.textData;
            } else {
                file.write(reinterpret_cast<const char*>(entry.rawData.data()), entry.rawData.size());
            }

            return file.good();
        } catch (...) {
            return false;
        }
    }

    void UltraCanvasClipboardUI::ShowSaveSuccessNotification(const std::string& filePath) {
        std::cout << "File saved successfully: " << filePath << std::endl;
        // TODO: Implement toast notification
    }

    void UltraCanvasClipboardUI::ShowSaveErrorNotification() {
        std::cerr << "Error: Failed to save file" << std::endl;
        // TODO: Implement error toast notification
    }

// ===== GLOBAL ACCESS =====
    UltraCanvasClipboardUI* UltraCanvasClipboardUI::GetInstance() {
        return g_clipboardUI.get();
    }

    void UltraCanvasClipboardUI::SetInstance(std::unique_ptr<UltraCanvasClipboardUI> instance) {
        g_clipboardUI = std::move(instance);
    }

    void UltraCanvasClipboardUI::Update() {
        // The clipboard handles its own monitoring, UI just needs to refresh when notified
        // Container handles its own scrolling and rendering
    }

    void UltraCanvasClipboardUI::HandleKeyboardInput(const UCEvent& event) {
        if (event.type == UCEventType::KeyDown) {
            switch (event.key) {
                case UCKey::Escape:
                    HideClipboardWindow();
                    break;
                case UCKey::Delete:
                    // TODO: Delete selected items
                    break;
                default:
                    break;
            }
        }
    }

// ===== GLOBAL FUNCTIONS =====
    UltraCanvasClipboardUI* GetClipboardUI() {
        return UltraCanvasClipboardUI::GetInstance();
    }

    void InitializeClipboardUI() {
        if (!g_clipboardUI) {
            g_clipboardUI = std::make_unique<UltraCanvasClipboardUI>();
            g_clipboardUI->Initialize();
        }
    }

    void ShutdownClipboardUI() {
        if (g_clipboardUI) {
            g_clipboardUI->Shutdown();
            g_clipboardUI.reset();
        }
    }

} // namespace UltraCanvas