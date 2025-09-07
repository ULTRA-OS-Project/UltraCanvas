// UltraCanvasClipboardManager.cpp
// Implementation of multi-entry clipboard manager for UltraCanvas Framework
// Version: 2.0.1
// Last Modified: 2025-08-11
// Author: UltraCanvas Framework

#include "UltraCanvasClipboardManager.h"
#include "UltraCanvasUI.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <ctime>

#ifdef __linux__
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace UltraCanvas {

// ===== CLIPBOARD ENTRY METHODS =====
    void ClipboardEntry::GeneratePreview() {
        if (type == ClipboardEntryType::Text || type == ClipboardEntryType::RichText) {
            preview = content.length() > 50 ? content.substr(0, 50) + "..." : content;
            // Replace newlines with spaces for preview
            for (char& c : preview) {
                if (c == '\n' || c == '\r') c = ' ';
            }
        } else if (type == ClipboardEntryType::Image) {
            preview = "Image";
        } else if (type == ClipboardEntryType::Vector) {
            preview = "Vector Graphics";
        } else if (type == ClipboardEntryType::Animation) {
            preview = "Animated Image";
        } else if (type == ClipboardEntryType::Video) {
            preview = "Video";
        } else if (type == ClipboardEntryType::ThreeD) {
            preview = "3D Model";
        } else if (type == ClipboardEntryType::Document) {
            preview = "Document";
        } else if (type == ClipboardEntryType::FilePath) {
            size_t pos = content.find_last_of("/\\");
            preview = (pos != std::string::npos) ? content.substr(pos + 1) : content;
        }
    }

    std::string ClipboardEntry::GetTypeString() const {
        switch (type) {
            case ClipboardEntryType::Text: return "Text";
            case ClipboardEntryType::Image: return "Image";
            case ClipboardEntryType::RichText: return "Rich Text";
            case ClipboardEntryType::FilePath: return "File";
            case ClipboardEntryType::Vector: return "Vector";
            case ClipboardEntryType::Animation: return "Animation";
            case ClipboardEntryType::Video: return "Video";
            case ClipboardEntryType::ThreeD: return "3D Model";
            case ClipboardEntryType::Document: return "Document";
            default: return "Unknown";
        }
    }

    std::string ClipboardEntry::GetFormattedTime() const {
        auto time = std::chrono::system_clock::to_time_t(timestamp);
        std::string timeStr = std::ctime(&time);
        timeStr.pop_back(); // Remove newline
        return timeStr;
    }

// ===== CLIPBOARD ITEM IMPLEMENTATION =====
    void UltraCanvasClipboardItem::CalculateLayout() {
        Rect2D bounds = GetBounds();

        // Thumbnail area (left side for visual content)
        if (entry.type == ClipboardEntryType::Image ||
            entry.type == ClipboardEntryType::Vector ||
            entry.type == ClipboardEntryType::Animation ||
            entry.type == ClipboardEntryType::Video ||
            entry.type == ClipboardEntryType::ThreeD ||
            entry.type == ClipboardEntryType::Document) {
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
        IRenderContext *ctx = GetRenderContext();
        ctx->PushState();

        Rect2D bounds = GetBounds();

        // Background
        Color bgColor = isSelected ? selectedColor : (IsHovered() ? hoverColor : normalColor);

        // Draw background and border
        UltraCanvas::DrawFilledRect(bounds, bgColor, borderColor, 1.0f);

        // Draw type icon for non-text content
        if (entry.type != ClipboardEntryType::Text && entry.type != ClipboardEntryType::RichText) {
            DrawTypeIcon();
        }

        // Render content text
        RenderContent();

        // Render action buttons
        if (IsHovered() || isSelected) {
            RenderActionButtons();
        }
        ctx->PushState();
    }

    void UltraCanvasClipboardItem::RenderContent() {
        ctx->PushState();

        ctx->SetTextColor(Colors::Black);
        ctx->SetFont("Arial", 11.0f);

        // Main content
        float yPos = contentRect.y + 5;

        // Type and size info
        std::string typeInfo = entry.GetTypeString();
        if (entry.dataSize > 0) {
            typeInfo += " (" + FormatBytes(entry.dataSize) + ")";
        }
        ctx->SetTextColor(Colors::Gray);
        ctx->SetFont("Arial", 9.0f);
        ctx->DrawText(typeInfo, Point2D(contentRect.x, yPos));

        // Preview content
        yPos += 15;
        ctx->SetTextColor(Colors::Black);
        ctx->SetFont("Arial", 11.0f);
        ctx->DrawText(entry.preview, Point2D(contentRect.x, yPos));

        // Timestamp
        yPos += 15;
        ctx->SetTextColor(Colors::Gray);
        ctx->SetFont("Arial", 8.0f);
        ctx->DrawText(entry.GetFormattedTime(), Point2D(contentRect.x, yPos));
    }

    void UltraCanvasClipboardItem::DrawTypeIcon() {
        ctx->PushState();

        // Draw simple type icon based on entry type
        Rect2D iconRect(thumbnailRect.x + 15, thumbnailRect.y + 15, 20, 20);

        switch (entry.type) {
            case ClipboardEntryType::Text:
            case ClipboardEntryType::RichText:
                ctx->DrawFilledRect(iconRect, Color(100, 150, 200, 255));
                ctx->SetTextColor(Colors::White);
                ctx->SetFont("Arial", 12.0f);
                ctx->DrawText("T", Point2D(iconRect.x + 6, iconRect.y + 15));
                break;

            case ClipboardEntryType::FilePath:
                ctx->DrawFilledRect(iconRect, Color(200, 150, 100, 255));
                ctx->SetTextColor(Colors::White);
                ctx->SetFont("Arial", 12.0f);
                ctx->DrawText("F", Point2D(iconRect.x + 6, iconRect.y + 15));
                break;

            case ClipboardEntryType::Vector:
                ctx->DrawFilledRect(iconRect, Color(150, 200, 100, 255));
                ctx->SetTextColor(Colors::White);
                ctx->SetFont("Arial", 12.0f);
                ctx->DrawText("V", Point2D(iconRect.x + 6, iconRect.y + 15));
                break;

            case ClipboardEntryType::Animation:
                ctx->DrawFilledRect(iconRect, Color(200, 100, 150, 255));
                ctx->SetTextColor(Colors::White);
                ctx->SetFont("Arial", 12.0f);
                ctx->DrawText("A", Point2D(iconRect.x + 6, iconRect.y + 15));
                break;

            case ClipboardEntryType::Video:
                ctx->DrawFilledRect(iconRect, Color(150, 100, 200, 255));
                ctx->SetTextColor(Colors::White);
                ctx->SetFont("Arial", 11.0f);
                ctx->DrawText("▶", Point2D(iconRect.x + 6, iconRect.y + 15));
                break;

            case ClipboardEntryType::ThreeD:
                ctx->DrawFilledRect(iconRect, Color(100, 200, 200, 255));
                ctx->SetTextColor(Colors::White);
                ctx->SetFont("Arial", 11.0f);
                ctx->DrawText("3D", Point2D(iconRect.x + 3, iconRect.y + 15));
                break;

            case ClipboardEntryType::Document:
                ctx->DrawFilledRect(iconRect, Color(200, 200, 100, 255));
                ctx->SetTextColor(Colors::White);
                ctx->SetFont("Arial", 12.0f);
                ctx->DrawText("D", Point2D(iconRect.x + 6, iconRect.y + 15));
                break;

            default:
                ctx->DrawFilledRect(iconRect, Colors::LightGray);
                break;
        }
    }

    void UltraCanvasClipboardItem::RenderActionButtons() {
        ctx->PushState();

        // Copy button
        Color copyColor = copyButtonRect.Contains(lastMousePos) ? Color(100, 200, 100, 255) : Color(150, 150, 150, 255);
        ctx->DrawFilledRect(copyButtonRect, copyColor);
        ctx->SetTextColor(Colors::White);
        ctx->SetFont("Arial", 10.0f);
        ctx->DrawText("C", Point2D(copyButtonRect.x + 6, copyButtonRect.y + 14));

        // Save button
        Color saveColor = saveButtonRect.Contains(lastMousePos) ? Color(100, 150, 200, 255) : Color(150, 150, 150, 255);
        ctx->DrawFilledRect(saveButtonRect, saveColor);
        ctx->SetTextColor(Colors::White);
        ctx->SetFont("Arial", 10.0f);
        ctx->DrawText("S", Point2D(saveButtonRect.x + 6, saveButtonRect.y + 14));

        // Delete button
        Color deleteColor = deleteButtonRect.Contains(lastMousePos) ? Color(200, 100, 100, 255) : Color(150, 150, 150, 255);
        ctx->DrawFilledRect(deleteButtonRect, deleteColor);
        ctx->SetTextColor(Colors::White);
        ctx->SetFont("Arial", 10.0f);
        ctx->DrawText("X", Point2D(deleteButtonRect.x + 6, deleteButtonRect.y + 14));
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

// ===== CLIPBOARD MANAGER IMPLEMENTATION =====
    UltraCanvasClipboardManager::UltraCanvasClipboardManager() {
        CreateClipboardWindow();
        RegisterGlobalHotkey();
        StartClipboardMonitoring();
    }

    UltraCanvasClipboardManager::~UltraCanvasClipboardManager() {
        StopClipboardMonitoring();
    }

    void UltraCanvasClipboardManager::CreateClipboardWindow() {
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

        clipboardWindow->Hide(); // Start hidden

        // Set up window event handlers
        clipboardWindow->onWindowBlurred = [this]() {
            HideClipboardWindow();
        };
    }

    void UltraCanvasClipboardManager::RegisterGlobalHotkey() {
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

        std::cout << "Registered ALT+P shortcut for clipboard manager" << std::endl;
    }

    void UltraCanvasClipboardManager::ToggleClipboardWindow() {
        if (isWindowVisible) {
            HideClipboardWindow();
        } else {
            ShowClipboardWindow();
        }
    }

    void UltraCanvasClipboardManager::ShowClipboardWindow() {
        if (!clipboardWindow) return;

        RefreshUI();
        clipboardWindow->Show();
        isWindowVisible = true;
    }

    void UltraCanvasClipboardManager::HideClipboardWindow() {
        if (!clipboardWindow) return;

        clipboardWindow->Hide();
        isWindowVisible = false;
    }

    void UltraCanvasClipboardManager::StartClipboardMonitoring() {
        lastCheckTime = std::chrono::steady_clock::now();
        lastClipboardContent = GetSystemClipboardText();
        std::cout << "Clipboard monitoring started" << std::endl;
    }

    void UltraCanvasClipboardManager::StopClipboardMonitoring() {
        std::cout << "Clipboard monitoring stopped" << std::endl;
    }

    void UltraCanvasClipboardManager::AddClipboardEntry(const ClipboardEntry& entry) {
        // Remove duplicate entries
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&entry](const ClipboardEntry& existing) {
                                         return existing.content == entry.content && existing.type == entry.type;
                                     }), entries.end());

        // Add to front
        entries.insert(entries.begin(), entry);

        // Limit to MAX_ENTRIES
        if (entries.size() > MAX_ENTRIES) {
            entries.resize(MAX_ENTRIES);
        }

        // Refresh UI if window is visible
        if (isWindowVisible) {
            RefreshUI();
        }
    }

    void UltraCanvasClipboardManager::RefreshUI() {
        if (!clipboardWindow) return;

        // Clear existing components
        for (auto& item : itemComponents) {
            clipboardWindow->RemoveElement(item);
        }
        itemComponents.clear();

        // Create new components for each entry
        int yPos = 10;
        for (size_t i = 0; i < entries.size(); ++i) {
            auto item = std::make_shared<UltraCanvasClipboardItem>(
                    "item_" + std::to_string(i), 1000 + i,
                    10, yPos - scrollOffset, WINDOW_WIDTH - 40, ITEM_HEIGHT,
                    entries[i]
            );

            // Set up event handlers
            item->onCopyRequested = [this](const ClipboardEntry& entry) {
                CopyEntryToClipboard(entry);
                HideClipboardWindow();
            };

            item->onSaveRequested = [this](const ClipboardEntry& entry) {
                SaveEntryToFile(entry);
            };

            item->onDeleteRequested = [this, i](const ClipboardEntry& entry) {
                DeleteEntry(i);
            };

            itemComponents.push_back(item);
            clipboardWindow->AddElement(item);

            yPos += ITEM_HEIGHT + 5;
        }
    }

    void UltraCanvasClipboardManager::CopyEntryToClipboard(const ClipboardEntry& entry) {
        if (entry.type == ClipboardEntryType::Text || entry.type == ClipboardEntryType::RichText) {
            SetSystemClipboardText(entry.content);
        }
    }

    void UltraCanvasClipboardManager::SaveEntryToFile(const ClipboardEntry& entry) {
        std::string suggestedFilename = GenerateSuggestedFilename(entry);
        std::string savePath = ShowSaveFileDialog(suggestedFilename, entry.type);

        if (!savePath.empty()) {
            bool success = SaveEntryData(entry, savePath);
            if (success) {
                ShowSaveSuccessNotification(savePath);
            } else {
                ShowSaveErrorNotification();
            }
        }
    }

    void UltraCanvasClipboardManager::DeleteEntry(size_t index) {
        if (index < entries.size()) {
            entries.erase(entries.begin() + index);
            RefreshUI();
        }
    }

    void UltraCanvasClipboardManager::CheckClipboardChanges() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCheckTime);

        if (elapsed.count() > 500) { // Check every 500ms
            std::string currentClipboard = GetSystemClipboardText();

            if (currentClipboard != lastClipboardContent && !currentClipboard.empty()) {
                std::cout << "Clipboard changed: " << currentClipboard.substr(0, 50) << "..." << std::endl;

                ClipboardEntry newEntry(ClipboardEntryType::Text, currentClipboard);
                AddClipboardEntry(newEntry);
                lastClipboardContent = currentClipboard;
            }

            lastCheckTime = now;
        }
    }

    void UltraCanvasClipboardManager::Update() {
        CheckClipboardChanges();
    }

// Implementation of other helper methods...
    std::string UltraCanvasClipboardManager::GenerateSuggestedFilename(const ClipboardEntry& entry) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::string timestamp = std::to_string(time_t);
        std::string extension = GetDefaultExtension(entry.type);

        switch (entry.type) {
            case ClipboardEntryType::Text:
            case ClipboardEntryType::RichText:
                return "clipboard_text_" + timestamp + ".txt";
            case ClipboardEntryType::Image:
                return "clipboard_image_" + timestamp + extension;
            case ClipboardEntryType::Vector:
                return "clipboard_vector_" + timestamp + extension;
            case ClipboardEntryType::Animation:
                return "clipboard_animation_" + timestamp + extension;
            case ClipboardEntryType::Video:
                return "clipboard_video_" + timestamp + extension;
            case ClipboardEntryType::ThreeD:
                return "clipboard_3d_" + timestamp + extension;
            case ClipboardEntryType::Document:
                return "clipboard_document_" + timestamp + extension;
            case ClipboardEntryType::FilePath: {
                size_t pos = entry.content.find_last_of("/\\");
                if (pos != std::string::npos) {
                    return entry.content.substr(pos + 1);
                }
                return "clipboard_file_" + timestamp;
            }
            default:
                return "clipboard_entry_" + timestamp;
        }
    }

    std::string UltraCanvasClipboardManager::GetDefaultExtension(ClipboardEntryType type) {
        switch (type) {
            case ClipboardEntryType::Text:
            case ClipboardEntryType::RichText:
                return ".txt";
            case ClipboardEntryType::Image:
                return ".png";
            case ClipboardEntryType::Vector:
                return ".svg";
            case ClipboardEntryType::Animation:
                return ".gif";
            case ClipboardEntryType::Video:
                return ".mp4";
            case ClipboardEntryType::ThreeD:
                return ".obj";
            case ClipboardEntryType::Document:
                return ".pdf";
            default:
                return "";
        }
    }

    std::string UltraCanvasClipboardManager::ShowSaveFileDialog(const std::string& suggestedName, ClipboardEntryType type) {
        const char* homeDir = getenv("HOME");
        if (!homeDir) homeDir = "/tmp";

        std::string downloadsDir = std::string(homeDir) + "/Downloads";

#ifdef __linux__
        mkdir(downloadsDir.c_str(), 0755);
#endif

        return downloadsDir + "/" + suggestedName;
    }

    bool UltraCanvasClipboardManager::SaveEntryData(const ClipboardEntry& entry, const std::string& filePath) {
        try {
            std::ofstream file;

            switch (entry.type) {
                case ClipboardEntryType::Text:
                case ClipboardEntryType::RichText:
                    file.open(filePath, std::ios::out);
                    if (file.is_open()) {
                        file << entry.content;
                        file.close();
                        return true;
                    }
                    break;

                case ClipboardEntryType::Image:
                case ClipboardEntryType::Vector:
                case ClipboardEntryType::Animation:
                case ClipboardEntryType::Video:
                case ClipboardEntryType::ThreeD:
                case ClipboardEntryType::Document:
                    if (!entry.rawData.empty()) {
                        file.open(filePath, std::ios::binary);
                        if (file.is_open()) {
                            file.write(reinterpret_cast<const char*>(entry.rawData.data()), entry.rawData.size());
                            file.close();
                            return true;
                        }
                    } else if (!entry.content.empty()) {
                        return CopyFile(entry.content, filePath);
                    }
                    break;

                case ClipboardEntryType::FilePath:
                    return CopyFile(entry.content, filePath);

                default:
                    return false;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error saving file: " << e.what() << std::endl;
        }

        return false;
    }

    bool UltraCanvasClipboardManager::CopyFile(const std::string& sourcePath, const std::string& destPath) {
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

    void UltraCanvasClipboardManager::ShowSaveSuccessNotification(const std::string& filePath) {
        std::cout << "✅ File saved successfully: " << filePath << std::endl;
    }

    void UltraCanvasClipboardManager::ShowSaveErrorNotification() {
        std::cout << "❌ Error saving file" << std::endl;
    }

    void UltraCanvasClipboardManager::SetSystemClipboardText(const std::string& text) {
        // Platform-specific implementation would go here
        lastClipboardContent = text;
    }

    std::string UltraCanvasClipboardManager::GetSystemClipboardText() {
        // Platform-specific implementation would go here
        return lastClipboardContent;
    }

    void UltraCanvasClipboardManager::AddTextEntry(const std::string& text) {
        ClipboardEntry entry(ClipboardEntryType::Text, text);
        AddClipboardEntry(entry);
    }

    void UltraCanvasClipboardManager::AddImageEntry(const std::string& imagePath, const std::vector<uint8_t>& imageData) {
        ClipboardEntry entry(ClipboardEntryType::Image, imagePath);
        entry.rawData = imageData;
        entry.dataSize = imageData.size();
        entry.GeneratePreview();
        AddClipboardEntry(entry);
    }

    void UltraCanvasClipboardManager::AddFileEntry(const std::string& filePath) {
        ClipboardEntry entry(ClipboardEntryType::FilePath, filePath);

        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            entry.dataSize = file.tellg();
            file.close();
        }

        entry.GeneratePreview();
        AddClipboardEntry(entry);
    }

    void UltraCanvasClipboardManager::ClearAllEntries() {
        entries.clear();
        RefreshUI();
    }

// ===== GLOBAL API IMPLEMENTATION =====
    static std::unique_ptr<UltraCanvasClipboardManager> g_globalClipboardManager;

    void InitializeClipboardManager() {
        if (!g_globalClipboardManager) {
            g_globalClipboardManager = std::make_unique<UltraCanvasClipboardManager>();
            std::cout << "Clipboard manager initialized" << std::endl;
        }
    }

    void ShutdownClipboardManager() {
        if (g_globalClipboardManager) {
            g_globalClipboardManager.reset();
            std::cout << "Clipboard manager shutdown" << std::endl;
        }
    }

    void UpdateClipboardManager() {
        if (g_globalClipboardManager) {
            g_globalClipboardManager->Update();
        }
    }

    UltraCanvasClipboardManager* GetClipboardManager() {
        return g_globalClipboardManager.get();
    }

    void AddClipboardText(const std::string& text) {
        if (g_globalClipboardManager) {
            g_globalClipboardManager->AddTextEntry(text);
        }
    }

    void AddClipboardImage(const std::string& imagePath) {
        if (g_globalClipboardManager) {
            std::vector<uint8_t> imageData;
            g_globalClipboardManager->AddImageEntry(imagePath, imageData);
        }
    }

    void AddClipboardFile(const std::string& filePath) {
        if (g_globalClipboardManager) {
            g_globalClipboardManager->AddFileEntry(filePath);
        }
    }

    void ShowClipboard() {
        if (g_globalClipboardManager) {
            g_globalClipboardManager->ShowClipboardWindow();
        }
    }

    void HideClipboard() {
        if (g_globalClipboardManager) {
            g_globalClipboardManager->HideClipboardWindow();
        }
    }

    void ClearClipboardHistory() {
        if (g_globalClipboardManager) {
            g_globalClipboardManager->ClearAllEntries();
        }
    }

    size_t GetClipboardEntryCount() {
        if (g_globalClipboardManager) {
            return g_globalClipboardManager->GetEntryCount();
        }
        return 0;
    }

} // namespace UltraCanvas