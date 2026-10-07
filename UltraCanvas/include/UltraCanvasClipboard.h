// include/UltraCanvasClipboard.h
// Platform-independent clipboard core functionality
// Version: 1.1.0
// Last Modified: 2026-10-06
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

// ===== WHAT THE SOURCE SAYS ABOUT A COPY =====
// Secret: a password or a key. The copy carries each platform's marker
// (x-kde-passwordManagerHint on X11; ExcludeClipboardContentFromMonitorProcessing,
// CanIncludeInClipboardHistory = 0 and CanUploadToCloudClipboard = 0 on
// Windows; org.nspasteboard.ConcealedType on macOS), so clipboard histories
// - this framework's and every other that honours the markers - leave it out.
// It still pastes like any other text.
enum class ClipboardHint {
    Normal,
    Secret
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

    // Rich text: HTML next to its plain-text form, so another application
    // pastes it formatted (text/html on X11, "HTML Format" on Windows). A
    // backend without it puts the plain text only, and reads none.
    virtual bool SetClipboardHtml(const std::string& html, const std::string& plainText) {
        (void)html;
        return SetClipboardText(plainText);
    }
    virtual bool GetClipboardHtml(std::string& html) {
        (void)html;
        return false;
    }

    // Secret text (see ClipboardHint): the text plus the platform's
    // "leave this out of the history" marker. A backend without markers puts
    // the plain text.
    virtual bool SetClipboardSecretText(const std::string& text) {
        return SetClipboardText(text);
    }
    // True when whoever owns the clipboard now - this process or another -
    // marked its content secret or transient. A clipboard history skips it.
    virtual bool IsClipboardMarkedSecret() {
        return false;
    }
    // False when no program holds the clipboard any more: on X11 the content
    // leaves with the program that copied it. Systems that keep the content
    // themselves always answer true.
    virtual bool HasClipboardOwner() {
        return true;
    }

    // Cut/copy-aware file clipboard operations. File managers mark a "cut"
    // (move-on-paste) on the clipboard next to the file list
    // (x-special/gnome-copied-files on Linux, "Preferred DropEffect" on
    // Windows). Backends without that concept fall back to the plain
    // file-list operations above and report cutOperation = false.
    virtual bool SetClipboardFiles(const std::vector<std::string>& filePaths, bool cutOperation) {
        (void)cutOperation;
        return SetClipboardFiles(filePaths);
    }
    virtual bool GetClipboardFiles(std::vector<std::string>& filePaths, bool& cutOperation) {
        cutOperation = false;
        return GetClipboardFiles(filePaths);
    }
    
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
    // Initialize() picks the platform's backend; InitializeWithBackend takes
    // one from the caller (a platform the framework has none for, or a test).
    bool Initialize();
    bool InitializeWithBackend(std::unique_ptr<UltraCanvasClipboardBackend> clipboardBackend);
    void Shutdown();
    UltraCanvasClipboardBackend* GetBackend() { return backend.get(); }

    // ===== CLIPBOARD OPERATIONS =====
    bool GetText(std::string& text);
    bool SetText(const std::string& text);
    // ClipboardHint::Secret for passwords and keys: never recorded in a
    // clipboard history, this process's or another's.
    bool SetText(const std::string& text, ClipboardHint hint);
    // HTML with its plain text (see UltraCanvasClipboardBackend).
    bool GetHtml(std::string& html);
    bool SetHtml(const std::string& html, const std::string& plainText);
    bool GetImage(std::vector<uint8_t>& imageData, std::string& format);
    bool SetImage(const std::vector<uint8_t>& imageData, const std::string& format);
    bool GetFiles(std::vector<std::string>& filePaths);
    bool SetFiles(const std::vector<std::string>& filePaths);
    // Cut/copy-aware variants (file-manager semantics: cut = move on paste).
    bool GetFiles(std::vector<std::string>& filePaths, bool& cutOperation);
    bool SetFiles(const std::vector<std::string>& filePaths, bool cutOperation);
    
    // ===== HISTORY MANAGEMENT =====
    // Newest first: AddEntry puts an entry at index 0, and copying something
    // already in the history moves it back there. Content the source marked
    // secret (ClipboardHint::Secret, or another program's marker) is never
    // recorded.
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
bool SetClipboardText(const std::string& text, ClipboardHint hint);
bool GetClipboardHtml(std::string& html);
bool SetClipboardHtml(const std::string& html, const std::string& plainText);
void AddClipboardEntry(const ClipboardData& entry);

} // namespace UltraCanvas
