// OS/WASM/UltraCanvasWASMSupport.h
// WebAssembly platform support utilities
// Version: 1.0.0
// Last Modified: 2025-01-27
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>
#include <functional>

namespace UltraCanvas {

// ===== FILE SYSTEM SUPPORT =====
class WASMFileSystem {
public:
    // Initialize IndexedDB File System (IDBFS)
    static bool MountFileSystem(const std::string& mountPoint = "/data");
    
    // Synchronize with browser storage
    static bool SyncFromBrowser();  // Load from IndexedDB
    static bool SyncToBrowser();    // Save to IndexedDB
    
    // File operations
    static bool FileExists(const std::string& path);
    static std::vector<uint8_t> ReadFile(const std::string& path);
    static bool WriteFile(const std::string& path, const std::vector<uint8_t>& data);
    static bool DeleteFile(const std::string& path);
    
    // Directory operations
    static bool CreateDirectory(const std::string& path);
    static std::vector<std::string> ListDirectory(const std::string& path);
};

// ===== NETWORK SUPPORT =====
class WASMNetwork {
public:
    // Fetch API wrapper
    static bool FetchURL(const std::string& url, std::vector<uint8_t>& data);
    static bool FetchText(const std::string& url, std::string& text);
    
    // Async fetch with callback
    using FetchCallback = std::function<void(bool success, const std::vector<uint8_t>& data)>;
    static void FetchAsync(const std::string& url, FetchCallback callback);
};

// ===== BROWSER INTEGRATION =====
class WASMBrowser {
public:
    // Download file to user's computer
    static void DownloadFile(const std::string& filename, const std::vector<uint8_t>& data, 
                            const std::string& mimeType = "application/octet-stream");
    
    // Show browser alert
    static void Alert(const std::string& message);
    
    // Show browser confirm dialog
    static bool Confirm(const std::string& message);
    
    // Show browser prompt
    static std::string Prompt(const std::string& message, const std::string& defaultValue = "");
    
    // Console logging
    static void ConsoleLog(const std::string& message);
    static void ConsoleWarn(const std::string& message);
    static void ConsoleError(const std::string& message);
    
    // Browser info
    static std::string GetUserAgent();
    static std::string GetPlatform();
    static void GetScreenSize(int& width, int& height);
    
    // LocalStorage
    static void SetLocalStorage(const std::string& key, const std::string& value);
    static std::string GetLocalStorage(const std::string& key);
    static void RemoveLocalStorage(const std::string& key);
    static void ClearLocalStorage();
};

// ===== RESOURCE LOADING =====
class WASMResourceLoader {
public:
    // Load image from URL
    using ImageLoadCallback = std::function<void(bool success, int width, int height, 
                                                 const std::vector<uint8_t>& pixels)>;
    static void LoadImage(const std::string& url, ImageLoadCallback callback);
    
    // Load font from URL
    using FontLoadCallback = std::function<void(bool success)>;
    static void LoadFont(const std::string& fontFamily, const std::string& url, 
                        FontLoadCallback callback);
    
    // Preload assets
    static void PreloadAsset(const std::string& url);
};

// ===== TIME & PERFORMANCE =====
class WASMTime {
public:
    // High-resolution time in seconds
    static double GetTime();
    
    // Performance marks
    static void PerformanceMark(const std::string& name);
    static double PerformanceMeasure(const std::string& name, 
                                     const std::string& startMark, 
                                     const std::string& endMark);
};

// ===== URL & QUERY PARAMETERS =====
class WASMURL {
public:
    // Get current page URL
    static std::string GetCurrentURL();
    
    // Get query parameter
    static std::string GetQueryParameter(const std::string& name);
    
    // Get all query parameters
    static std::vector<std::pair<std::string, std::string>> GetAllQueryParameters();
    
    // Get hash (fragment identifier)
    static std::string GetHash();
    
    // Navigate
    static void Navigate(const std::string& url);
    static void Reload();
};

} // namespace UltraCanvas
