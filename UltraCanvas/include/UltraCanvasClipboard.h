// include/UltraCanvasClipboard.h
// Platform-independent clipboard core functionality
// Version: 1.0.0
// Last Modified: 2025-08-13
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <memory>

namespace UltraCanvas {

// ===== CLIPBOARD DATA TYPES =====
enum class ClipboardDataType {
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

// ===== CLIPBOARD DATA ENTRY =====
struct ClipboardData {
    ClipboardDataType type;
    std::string content;           // Text content or file path for files
    std::vector<uint8_t> rawData;  // Binary data for images/files
    std::string mimeType;
    std::chrono::system_clock::time_point timestamp;
    std::string thumbnail;         // Path to generated thumbnail for images
    std::string preview;           // Short preview text (first 50 chars for text)
    size_t dataSize;              // Size in bytes

    ClipboardData() : type(ClipboardDataType::Unknown), dataSize(0) {
        timestamp = std::chrono::system_clock::now();
    }

    ClipboardData(ClipboardDataType t, const std::string& data)
            : type(t), content(data), dataSize(data.size()) {
        timestamp = std::chrono::system_clock::now();
        GeneratePreview();
    }

    void GeneratePreview();
    std::string GetTypeString() const;
    std::string GetFormattedTime() const;

    bool operator==(const ClipboardData& other) const {
        // Compare type first (most likely to differ)
        if (type != other.type) {
            return false;
        }

        // Compare data size (quick check before content comparison)
        if (dataSize != other.dataSize) {
            return false;
        }

        // Compare MIME type
        if (mimeType != other.mimeType) {
            return false;
        }

        // Compare content (text content or file path)
        if (content != other.content) {
            return false;
        }

        // Compare raw binary data (for images/files)
        if (rawData != other.rawData) {
            return false;
        }

        // Note: timestamp, thumbnail, and preview are not compared
        // as they are derived/metadata that shouldn't affect equality

        return true;
    }
};

// ===== PLATFORM-INDEPENDENT CLIPBOARD INTERFACE =====
class UltraCanvasClipboardBackend {
public:
    virtual ~UltraCanvasClipboardBackend() = default;
    
    // Core clipboard operations
    virtual bool GetClipboardText(std::string& text) = 0;
    virtual bool SetClipboardText(const std::string& text) = 0;
    virtual bool GetClipboardImage(std::vector<uint8_t>& imageData, std::string& format) = 0;
    virtual bool SetClipboardImage(const std::vector<uint8_t>& imageData, const std::string& format) = 0;
    virtual bool GetClipboardFiles(std::vector<std::string>& filePaths) = 0;
    virtual bool SetClipboardFiles(const std::vector<std::string>& filePaths) = 0;
    
    // Monitoring
    virtual bool HasClipboardChanged() = 0;
    virtual void ResetChangeState() = 0;
    
    // Format detection
    virtual std::vector<std::string> GetAvailableFormats() = 0;
    virtual bool IsFormatAvailable(const std::string& format) = 0;
    
    // Platform-specific initialization
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
};

// ===== MAIN CLIPBOARD CLASS =====
class UltraCanvasClipboard {
public:
    static constexpr size_t MAX_ENTRIES = 100;
    
    // Change notification callback
    using ChangeCallback = std::function<void(const ClipboardData& newEntry)>;
    
private:
    std::unique_ptr<UltraCanvasClipboardBackend> backend;
    std::vector<ClipboardData> entries;
    std::string lastClipboardContent;
    std::chrono::steady_clock::time_point lastCheckTime;
    bool monitoringEnabled = false;
    ChangeCallback changeCallback;
    
public:
    UltraCanvasClipboard();
    ~UltraCanvasClipboard();
    
    // ===== INITIALIZATION =====
    bool Initialize();
    void Shutdown();
    UltraCanvasClipboardBackend* GetBackend() { return backend.get(); }

    // ===== CLIPBOARD OPERATIONS =====
    bool GetText(std::string& text);
    bool SetText(const std::string& text);
    bool GetImage(std::vector<uint8_t>& imageData, std::string& format);
    bool SetImage(const std::vector<uint8_t>& imageData, const std::string& format);
    bool GetFiles(std::vector<std::string>& filePaths);
    bool SetFiles(const std::vector<std::string>& filePaths);
    
    // ===== HISTORY MANAGEMENT =====
    void AddEntry(const ClipboardData& entry);
    void RemoveEntry(size_t index);
    void ClearHistory();
    const std::vector<ClipboardData>& GetEntries() const { return entries; }
    size_t GetEntryCount() const { return entries.size(); }
    
    // ===== MONITORING =====
    void StartMonitoring();
    void StopMonitoring();
    void SetChangeCallback(ChangeCallback callback) { changeCallback = std::move(callback); }
    void Update(); // Call this regularly to check for changes
    
    // ===== FORMAT DETECTION =====
    std::vector<std::string> GetAvailableFormats();
    bool IsFormatAvailable(const std::string& format);
    ClipboardDataType DetectDataType(const std::string& mimeType);
    
    // ===== UTILITY METHODS =====
    bool CopyEntryToClipboard(size_t index);
    std::string GenerateSuggestedFilename(const ClipboardData& entry);
    std::string GetDefaultExtension(ClipboardDataType type);
    
private:
    void CheckForChanges();
    void ProcessNewClipboardContent();
    ClipboardData CreateEntryFromCurrentClipboard();
    bool RemoveDuplicateEntries(const ClipboardData& newEntry);
    void LimitEntriesToMax();
};

// ===== GLOBAL FUNCTIONS =====
// Initialize the global clipboard instance
bool InitializeClipboard();
void ShutdownClipboard();

// Get the global clipboard instance
UltraCanvasClipboard* GetClipboard();

// Convenience functions for quick access
bool GetClipboardText(std::string& text);
bool SetClipboardText(const std::string& text);
void AddClipboardEntry(const ClipboardData& entry);

} // namespace UltraCanvas
