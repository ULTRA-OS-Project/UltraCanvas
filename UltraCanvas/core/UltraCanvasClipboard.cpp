// UltraCanvasClipboard.cpp
// Platform-independent clipboard core implementation
// Version: 1.0.0
// Last Modified: 2025-08-13
// Author: UltraCanvas Framework

#include "UltraCanvasClipboard.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <ctime>

#ifdef __linux__
#include "../OS/Linux/UltraCanvasLinuxClipboard.h"
#elif _WIN32
#include "../OS/Win32/UltraCanvasWindowsClipboard.h"
#elif __APPLE__
#include "../OS/Apple/UltraCanvasMacClipboard.h"
#endif

namespace UltraCanvas {

// ===== GLOBAL CLIPBOARD INSTANCE =====
static std::unique_ptr<UltraCanvasClipboard> g_clipboard = nullptr;

// ===== CLIPBOARD DATA METHODS =====
void ClipboardData::GeneratePreview() {
    if (type == ClipboardDataType::Text || type == ClipboardDataType::RichText) {
        preview = content.length() > 50 ? content.substr(0, 50) + "..." : content;
        // Replace newlines with spaces for preview
        for (char& c : preview) {
            if (c == '\n' || c == '\r') c = ' ';
        }
    } else if (type == ClipboardDataType::Image) {
        preview = "Image";
    } else if (type == ClipboardDataType::Vector) {
        preview = "Vector Graphics";
    } else if (type == ClipboardDataType::Animation) {
        preview = "Animated Image";
    } else if (type == ClipboardDataType::Video) {
        preview = "Video";
    } else if (type == ClipboardDataType::ThreeD) {
        preview = "3D Model";
    } else if (type == ClipboardDataType::Document) {
        preview = "Document";
    } else if (type == ClipboardDataType::FilePath) {
        size_t pos = content.find_last_of("/\\");
        preview = (pos != std::string::npos) ? content.substr(pos + 1) : content;
    }
}

std::string ClipboardData::GetTypeString() const {
    switch (type) {
        case ClipboardDataType::Text: return "Text";
        case ClipboardDataType::Image: return "Image";
        case ClipboardDataType::RichText: return "Rich Text";
        case ClipboardDataType::FilePath: return "File";
        case ClipboardDataType::Vector: return "Vector";
        case ClipboardDataType::Animation: return "Animation";
        case ClipboardDataType::Video: return "Video";
        case ClipboardDataType::ThreeD: return "3D Model";
        case ClipboardDataType::Document: return "Document";
        default: return "Unknown";
    }
}

std::string ClipboardData::GetFormattedTime() const {
    auto time = std::chrono::system_clock::to_time_t(timestamp);
    std::string timeStr = std::ctime(&time);
    timeStr.pop_back(); // Remove newline
    return timeStr;
}

// ===== MAIN CLIPBOARD IMPLEMENTATION =====
UltraCanvasClipboard::UltraCanvasClipboard() {
    lastCheckTime = std::chrono::steady_clock::now();
}

UltraCanvasClipboard::~UltraCanvasClipboard() {
    if (backend) {
        backend->Shutdown();
    }
}

bool UltraCanvasClipboard::Initialize() {
    // Create platform-specific backend
#ifdef __linux__
    backend = std::make_unique<UltraCanvasLinuxClipboard>();
#elif _WIN32
    backend = std::make_unique<UltraCanvasWindowsClipboard>();
#elif __APPLE__
    backend = std::make_unique<UltraCanvasMacClipboard>();
#else
    std::cerr << "UltraCanvas: Clipboard not supported on this platform" << std::endl;
    return false;
#endif

    if (!backend || !backend->Initialize()) {
        std::cerr << "UltraCanvas: Failed to initialize clipboard backend" << std::endl;
        return false;
    }

    // Get initial clipboard content
    std::string initialText;
    if (GetText(initialText)) {
        lastClipboardContent = initialText;
    }

    std::cout << "UltraCanvas: Clipboard initialized successfully" << std::endl;
    return true;
}

void UltraCanvasClipboard::Shutdown() {
    StopMonitoring();
    
    if (backend) {
        backend->Shutdown();
        backend.reset();
    }
    
    entries.clear();
    std::cout << "UltraCanvas: Clipboard shut down" << std::endl;
}

// ===== CLIPBOARD OPERATIONS =====
bool UltraCanvasClipboard::GetText(std::string& text) {
    if (!backend) return false;
    return backend->GetClipboardText(text);
}

bool UltraCanvasClipboard::SetText(const std::string& text) {
    if (!backend) return false;
    
    bool success = backend->SetClipboardText(text);
    if (success) {
        lastClipboardContent = text;
        backend->ResetChangeState();
    }
    return success;
}

bool UltraCanvasClipboard::GetImage(std::vector<uint8_t>& imageData, std::string& format) {
    if (!backend) return false;
    return backend->GetClipboardImage(imageData, format);
}

bool UltraCanvasClipboard::SetImage(const std::vector<uint8_t>& imageData, const std::string& format) {
    if (!backend) return false;
    
    bool success = backend->SetClipboardImage(imageData, format);
    if (success) {
        backend->ResetChangeState();
    }
    return success;
}

bool UltraCanvasClipboard::GetFiles(std::vector<std::string>& filePaths) {
    if (!backend) return false;
    return backend->GetClipboardFiles(filePaths);
}

bool UltraCanvasClipboard::SetFiles(const std::vector<std::string>& filePaths) {
    if (!backend) return false;
    
    bool success = backend->SetClipboardFiles(filePaths);
    if (success) {
        backend->ResetChangeState();
    }
    return success;
}

// ===== HISTORY MANAGEMENT =====
void UltraCanvasClipboard::AddEntry(const ClipboardData& entry) {
    // Remove duplicates
    if (RemoveDuplicateEntries(entry)) {
        return; // Entry already exists, don't add duplicate
    }

    // Add to front
    entries.insert(entries.begin(), entry);

    // Limit to max entries
    LimitEntriesToMax();
}

void UltraCanvasClipboard::RemoveEntry(size_t index) {
    if (index < entries.size()) {
        entries.erase(entries.begin() + index);
    }
}

void UltraCanvasClipboard::ClearHistory() {
    entries.clear();
}

// ===== MONITORING =====
void UltraCanvasClipboard::StartMonitoring() {
    monitoringEnabled = true;
    lastCheckTime = std::chrono::steady_clock::now();
    
    std::string currentText;
    if (GetText(currentText)) {
        lastClipboardContent = currentText;
    }
    
    std::cout << "UltraCanvas: Clipboard monitoring started" << std::endl;
}

void UltraCanvasClipboard::StopMonitoring() {
    monitoringEnabled = false;
    std::cout << "UltraCanvas: Clipboard monitoring stopped" << std::endl;
}

void UltraCanvasClipboard::Update() {
    if (!monitoringEnabled || !backend) {
        return;
    }

    CheckForChanges();
}

void UltraCanvasClipboard::CheckForChanges() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCheckTime);

    if (elapsed.count() > 500) { // Check every 500ms
        if (backend->HasClipboardChanged()) {
            ProcessNewClipboardContent();
            backend->ResetChangeState();
        }
        lastCheckTime = now;
    }
}

void UltraCanvasClipboard::ProcessNewClipboardContent() {
    std::string currentText;
    if (GetText(currentText) && currentText != lastClipboardContent && !currentText.empty()) {
        std::cout << "UltraCanvas: Clipboard changed: " << currentText.substr(0, 50) << "..." << std::endl;

        ClipboardData newEntry = CreateEntryFromCurrentClipboard();
        AddEntry(newEntry);
        lastClipboardContent = currentText;

        // Notify callback
        if (changeCallback) {
            changeCallback(newEntry);
        }
    }
}

ClipboardData UltraCanvasClipboard::CreateEntryFromCurrentClipboard() {
    // Try to get text first
    std::string text;
    if (GetText(text) && !text.empty()) {
        ClipboardData entry(ClipboardDataType::Text, text);
        return entry;
    }

    // Try to get image
    std::vector<uint8_t> imageData;
    std::string imageFormat;
    if (GetImage(imageData, imageFormat) && !imageData.empty()) {
        ClipboardData entry(ClipboardDataType::Image, "");
        entry.rawData = imageData;
        entry.mimeType = imageFormat;
        entry.dataSize = imageData.size();
        entry.GeneratePreview();
        return entry;
    }

    // Try to get files
    std::vector<std::string> filePaths;
    if (GetFiles(filePaths) && !filePaths.empty()) {
        ClipboardData entry(ClipboardDataType::FilePath, filePaths[0]); // Take first file
        entry.GeneratePreview();
        return entry;
    }

    // Default empty entry
    return ClipboardData();
}

// ===== FORMAT DETECTION =====
std::vector<std::string> UltraCanvasClipboard::GetAvailableFormats() {
    if (!backend) return {};
    return backend->GetAvailableFormats();
}

bool UltraCanvasClipboard::IsFormatAvailable(const std::string& format) {
    if (!backend) return false;
    return backend->IsFormatAvailable(format);
}

ClipboardDataType UltraCanvasClipboard::DetectDataType(const std::string& mimeType) {
    if (mimeType.find("text/") == 0) {
        if (mimeType == "text/html" || mimeType == "text/rtf") {
            return ClipboardDataType::RichText;
        }
        return ClipboardDataType::Text;
    }
    
    if (mimeType.find("image/") == 0) {
        if (mimeType == "image/gif") {
            return ClipboardDataType::Animation;
        }
        if (mimeType == "image/svg+xml") {
            return ClipboardDataType::Vector;
        }
        return ClipboardDataType::Image;
    }
    
    if (mimeType.find("video/") == 0) {
        return ClipboardDataType::Video;
    }
    
    if (mimeType.find("application/pdf") == 0 || 
        mimeType.find("application/msword") == 0 ||
        mimeType.find("application/vnd.openxmlformats") == 0) {
        return ClipboardDataType::Document;
    }
    
    return ClipboardDataType::Unknown;
}

// ===== UTILITY METHODS =====
bool UltraCanvasClipboard::CopyEntryToClipboard(size_t index) {
    if (index >= entries.size()) return false;
    
    const ClipboardData& entry = entries[index];
    
    switch (entry.type) {
        case ClipboardDataType::Text:
        case ClipboardDataType::RichText:
            return SetText(entry.content);
            
        case ClipboardDataType::Image:
        case ClipboardDataType::Vector:
        case ClipboardDataType::Animation:
            if (!entry.rawData.empty()) {
                return SetImage(entry.rawData, entry.mimeType);
            }
            break;
            
        case ClipboardDataType::FilePath:
            return SetFiles({entry.content});
            
        default:
            break;
    }
    
    return false;
}

std::string UltraCanvasClipboard::GenerateSuggestedFilename(const ClipboardData& entry) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::string timestamp = std::to_string(time_t);
    std::string extension = GetDefaultExtension(entry.type);

    switch (entry.type) {
        case ClipboardDataType::Text:
        case ClipboardDataType::RichText:
            return "clipboard_text_" + timestamp + ".txt";
        case ClipboardDataType::Image:
            return "clipboard_image_" + timestamp + extension;
        case ClipboardDataType::Vector:
            return "clipboard_vector_" + timestamp + extension;
        case ClipboardDataType::Animation:
            return "clipboard_animation_" + timestamp + extension;
        case ClipboardDataType::Video:
            return "clipboard_video_" + timestamp + extension;
        case ClipboardDataType::ThreeD:
            return "clipboard_3d_" + timestamp + extension;
        case ClipboardDataType::Document:
            return "clipboard_document_" + timestamp + extension;
        case ClipboardDataType::FilePath: {
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

std::string UltraCanvasClipboard::GetDefaultExtension(ClipboardDataType type) {
    switch (type) {
        case ClipboardDataType::Text:
        case ClipboardDataType::RichText:
            return ".txt";
        case ClipboardDataType::Image:
            return ".png";
        case ClipboardDataType::Vector:
            return ".svg";
        case ClipboardDataType::Animation:
            return ".gif";
        case ClipboardDataType::Video:
            return ".mp4";
        case ClipboardDataType::ThreeD:
            return ".obj";
        case ClipboardDataType::Document:
            return ".pdf";
        default:
            return ".dat";
    }
}

// ===== PRIVATE HELPER METHODS =====
bool UltraCanvasClipboard::RemoveDuplicateEntries(const ClipboardData& newEntry) {
    auto it = std::find_if(entries.begin(), entries.end(),
        [&newEntry](const ClipboardData& existing) {
            return existing.content == newEntry.content && 
                   existing.type == newEntry.type;
        });
    
    if (it != entries.end()) {
        entries.erase(it);
        return false; // Entry was duplicate but removed, so add the new one
    }
    
    return false; // No duplicate found
}

void UltraCanvasClipboard::LimitEntriesToMax() {
    if (entries.size() > MAX_ENTRIES) {
        entries.resize(MAX_ENTRIES);
    }
}

// ===== GLOBAL FUNCTIONS =====
bool InitializeClipboard() {
    if (!g_clipboard) {
        g_clipboard = std::make_unique<UltraCanvasClipboard>();
        return g_clipboard->Initialize();
    }
    return true;
}

void ShutdownClipboard() {
    if (g_clipboard) {
        g_clipboard->Shutdown();
        g_clipboard.reset();
    }
}

UltraCanvasClipboard* GetClipboard() {
    return g_clipboard.get();
}

bool GetClipboardText(std::string& text) {
    if (g_clipboard) {
        return g_clipboard->GetText(text);
    }
    return false;
}

bool SetClipboardText(const std::string& text) {
    if (g_clipboard) {
        return g_clipboard->SetText(text);
    }
    return false;
}

void AddClipboardEntry(const ClipboardData& entry) {
    if (g_clipboard) {
        g_clipboard->AddEntry(entry);
    }
}

} // namespace UltraCanvas
