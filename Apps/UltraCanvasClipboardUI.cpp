// UltraCanvasClipboardUI.cpp
// UI manager implementation for multi-entry clipboard
// Version: 1.0.0
// Last Modified: 2025-08-13
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
        Color bgColor = isSelected ? selectedColor : (IsHovered() ? hoverColor : normalColor);
        UltraCanvas::DrawFilledRect(bounds, bgColor);

        // Border
        SetStrokeColor(borderColor);
        UltraCanvas::DrawRectangle(bounds);

        // Render content
        RenderContent();

        // Draw type icon if thumbnail area exists
        if (thumbnailRect.width > 0) {
            DrawTypeIcon();
        }

        // Render action buttons
        RenderActionButtons();
    }

    void UltraCanvasClipboardItem::RenderContent() {
        ULTRACANVAS_RENDER_SCOPE();

        // Entry type and timestamp
        SetTextColor(Colors::DarkGray);
        SetFont("Arial", 10.0f);
        DrawText(entry.GetTypeString(), Point2D(contentRect.x, contentRect.y + 12));
        DrawText(entry.GetFormattedTime(), Point2D(contentRect.x, contentRect.y + 25));

        // Content preview
        SetTextColor(Colors::Black);
        SetFont("Arial", 11.0f);
        std::string preview = entry.preview;
        if (preview.length() > 60) {
            preview = preview.substr(0, 60) + "...";
        }
        DrawText(preview, Point2D(contentRect.x, contentRect.y + 40));

        // Data size
        SetTextColor(Colors::Gray);
        SetFont("Arial", 9.0f);
        std::string sizeText = FormatBytes(entry.dataSize);
        DrawText(sizeText, Point2D(contentRect.x, contentRect.y + 55));
    }

    void UltraCanvasClipboardItem::DrawTypeIcon() {
        ULTRACANVAS_RENDER_SCOPE();

        if (thumbnailRect.width == 0) return;

        Rect2D iconRect = Rect2D(thumbnailRect.x + 10, thumbnailRect.y + 10, 40, 40);

        switch (entry.type) {
            case ClipboardDataType::Text:
            case ClipboardDataType::RichText:
                UltraCanvas::DrawFilledRect(iconRect, Color(100, 150, 255, 255));
                SetTextColor(Colors::White);
                SetFont("Arial", 12.0f);
                DrawText("T", Point2D(iconRect.x + 15, iconRect.y + 25));
                break;

            case ClipboardDataType::Image:
                UltraCanvas::DrawFilledRect(iconRect, Color(255, 150, 100, 255));
                SetTextColor(Colors::White);
                SetFont("Arial", 12.0f);
                DrawText("I", Point2D(iconRect.x + 15, iconRect.y + 25));
                break;

            case ClipboardDataType::Vector:
                UltraCanvas::DrawFilledRect(iconRect, Color(150, 255, 100, 255));
                SetTextColor(Colors::White);
                SetFont("Arial", 12.0f);
                DrawText("V", Point2D(iconRect.x + 15, iconRect.y + 25));
                break;

            case ClipboardDataType::Animation:
                UltraCanvas::DrawFilledRect(iconRect, Color(255, 100, 150, 255));
                SetTextColor(Colors::White);
                SetFont("Arial", 12.0f);
                DrawText("A", Point2D(iconRect.x + 15, iconRect.y + 25));
                break;

            case ClipboardDataType::Video:
                UltraCanvas::DrawFilledRect(iconRect, Color(150, 100, 200, 255));
                SetTextColor(Colors::White);
                SetFont("Arial", 11.0f);
                DrawText("▶", Point2D(iconRect.x + 15, iconRect.y + 25));
                break;

            case ClipboardDataType::ThreeD:
                UltraCanvas::DrawFilledRect(iconRect, Color(100, 200, 200, 255));
                SetTextColor(Colors::White);
                SetFont("Arial", 11.0f);
                DrawText("3D", Point2D(iconRect.x + 12, iconRect.y + 25));
                break;

            case ClipboardDataType::Document:
                UltraCanvas::DrawFilledRect(iconRect, Color(200, 200, 100, 255));
                SetTextColor(Colors::White);
                SetFont("Arial", 12.0f);
                DrawText("D", Point2D(iconRect.x + 15, iconRect.y + 25));
                break;

            case ClipboardDataType::FilePath:
                UltraCanvas::DrawFilledRect(iconRect, Color(150, 150, 150, 255));
                SetTextColor(Colors::White);
                SetFont("Arial", 12.0f);
                DrawText("F", Point2D(iconRect.x + 15, iconRect.y + 25));
                break;

            default:
                UltraCanvas::DrawFilledRect(iconRect, Colors::LightGray);
                break;
        }
    }

    void UltraCanvasClipboardItem::RenderActionButtons() {
        ULTRACANVAS_RENDER_SCOPE();

        // Copy button
        Color copyColor = copyButtonRect.Contains(lastMousePos) ? Color(100, 250, 100, 255) : Color(50, 200, 50, 255);
        UltraCanvas::DrawFilledRect(copyButtonRect, copyColor);
        SetTextColor(Colors::White);
        SetFont("Arial", 10.0f);
        DrawText("C", Point2D(copyButtonRect.x + 6, copyButtonRect.y + 2));

        // Save button
        Color saveColor = saveButtonRect.Contains(lastMousePos) ? Color(100, 150, 250, 255) : Color(150, 150, 250, 255);
        UltraCanvas::DrawFilledRect(saveButtonRect, saveColor);
        SetTextColor(Colors::White);
        SetFont("Arial", 10.0f);
        DrawText("S", Point2D(saveButtonRect.x + 6, saveButtonRect.y + 2));

        // Delete button
        Color deleteColor = deleteButtonRect.Contains(lastMousePos) ? Color(255, 10, 10, 255) : Color(250, 100, 100, 255);
        UltraCanvas::DrawFilledRect(deleteButtonRect, deleteColor);
        SetTextColor(Colors::White);
        SetFont("Arial", 10.0f);
        DrawText("X", Point2D(deleteButtonRect.x + 6, deleteButtonRect.y + 2));
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
        std::cout << "UltraCanvas: Initializing clipboard UI..." << std::endl;

        // Get the clipboard instance
        clipboard = GetClipboard();
        if (!clipboard) {
            std::cerr << "UltraCanvas: No clipboard instance available" << std::endl;
            return false;
        }

        // Create the clipboard window
        CreateClipboardWindow();

        // Register global hotkey
        RegisterGlobalHotkey();

        // Set up clipboard change callback
        clipboard->SetChangeCallback([this](const ClipboardData& newEntry) {
            OnClipboardChanged(newEntry);
        });

        std::cout << "UltraCanvas: Clipboard UI initialized successfully" << std::endl;
        return true;
    }

    void UltraCanvasClipboardUI::Shutdown() {
        if (clipboardWindow) {
            clipboardWindow.reset();
        }

        itemComponents.clear();
        clipboard = nullptr;

        std::cout << "UltraCanvas: Clipboard UI shut down" << std::endl;
    }

    void UltraCanvasClipboardUI::CreateClipboardWindow() {
        // Create window using proper API
        clipboardWindow = std::make_shared<UltraCanvasWindow>();

        // Set up window event handlers
//        clipboardWindow->onWindowBlurred = [this]() {
//            HideClipboardWindow();
//        };
    }

    void UltraCanvasClipboardUI::RegisterGlobalHotkey() {
        // Register ALT+P shortcut for toggling clipboard window
        UltraCanvasKeyboardManager::RegisterShortcut(
                static_cast<int>(UCKeys::P), // P key
                static_cast<int>(ModifierKeys::Alt), // ALT modifier
                [this]() {
                    std::cout << "ALT+P pressed - toggling clipboard window" << std::endl;
                    ToggleClipboardWindow();
                },
                "Toggle Multi-Entry Clipboard"
        );

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

        if (!clipboardWindow->IsCreated()) {
            WindowConfig config;
            config.title = "Multi-Entry Clipboard";
            config.width = WINDOW_WIDTH;
            config.height = WINDOW_HEIGHT;
            config.resizable = true;
            config.alwaysOnTop = true;
            config.type = WindowType::Tool;

            clipboardWindow->Create(config);
        }
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
        if (!clipboardWindow || !clipboard) return;

        // Clear existing components
        for (auto& item : itemComponents) {
            clipboardWindow->RemoveElement(item);
        }
        itemComponents.clear();

        // Create new components for each entry
        const auto& entries = clipboard->GetEntries();
        int yPos = 10;

        for (size_t i = 0; i < entries.size(); ++i) {
            auto item = std::make_shared<UltraCanvasClipboardItem>(
                    "item_" + std::to_string(i), 1000 + i,
                    10, yPos - scrollOffset, WINDOW_WIDTH - 40, ITEM_HEIGHT,
                    entries[i]
            );

            // Set up event handlers
            item->onCopyRequested = [this](const ClipboardData& entry) {
                OnCopyRequested(entry);
            };

            item->onSaveRequested = [this](const ClipboardData& entry) {
                OnSaveRequested(entry);
            };

            item->onDeleteRequested = [this, i](const ClipboardData& entry) {
                OnDeleteRequested(entry);
            };

            itemComponents.push_back(item);
            clipboardWindow->AddElement(item);

            yPos += ITEM_HEIGHT + 5;
        }
    }

    void UltraCanvasClipboardUI::Update() {
        // The clipboard handles its own monitoring, UI just needs to refresh when notified
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
            if (entries[i] == entry) {
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
            if (entries[i] == entry) {
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
        std::string downloadsDir = homeDir + "/Downloads";

        // Create Downloads directory if it doesn't exist
        mkdir(downloadsDir.c_str(), 0755);

        return downloadsDir + "/" + suggestedName;
#else
        return suggestedName; // Fallback
#endif
    }

    bool UltraCanvasClipboardUI::SaveEntryToFile(const ClipboardData& entry, const std::string& filePath) {
        try {
            switch (entry.type) {
                case ClipboardDataType::Text:
                case ClipboardDataType::RichText: {
                    std::ofstream file(filePath);
                    if (file.is_open()) {
                        file << entry.content;
                        return true;
                    }
                    break;
                }

                case ClipboardDataType::Image:
                case ClipboardDataType::Vector:
                case ClipboardDataType::Animation:
                case ClipboardDataType::Video:
                case ClipboardDataType::ThreeD:
                case ClipboardDataType::Document: {
                    if (!entry.rawData.empty()) {
                        std::ofstream file(filePath, std::ios::binary);
                        if (file.is_open()) {
                            file.write(reinterpret_cast<const char*>(entry.rawData.data()), entry.rawData.size());
                            return true;
                        }
                    } else if (!entry.content.empty()) {
                        return CopyFile(entry.content, filePath);
                    }
                    break;
                }

                case ClipboardDataType::FilePath:
                    return CopyFile(entry.content, filePath);

                default:
                    return false;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error saving file: " << e.what() << std::endl;
        }

        return false;
    }

    bool UltraCanvasClipboardUI::CopyFile(const std::string& sourcePath, const std::string& destPath) {
        try {
            std::ifstream source(sourcePath, std::ios::binary);
            std::ofstream dest(destPath, std::ios::binary);

            if (source.is_open() && dest.is_open()) {
                dest << source.rdbuf();
                return true;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error copying file: " << e.what() << std::endl;
        }

        return false;
    }

    void UltraCanvasClipboardUI::ShowSaveSuccessNotification(const std::string& filePath) {
        std::cout << "✅ File saved successfully: " << filePath << std::endl;
    }

    void UltraCanvasClipboardUI::ShowSaveErrorNotification() {
        std::cout << "❌ Error saving file" << std::endl;
    }

// ===== GLOBAL UI FUNCTIONS =====
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

    void UpdateClipboardUI() {
        if (g_clipboardUI) {
            g_clipboardUI->Update();
        }
    }

    UltraCanvasClipboardUI* GetClipboardUI() {
        return g_clipboardUI.get();
    }

    void ShowClipboard() {
        if (g_clipboardUI) {
            g_clipboardUI->ShowClipboardWindow();
        }
    }

    void HideClipboard() {
        if (g_clipboardUI) {
            g_clipboardUI->HideClipboardWindow();
        }
    }

    void ToggleClipboard() {
        if (g_clipboardUI) {
            g_clipboardUI->ToggleClipboardWindow();
        }
    }

} // namespace UltraCanvas