// UltraCanvasLinuxClipboard.h
// X11-specific clipboard implementation for Linux
// Version: 1.0.0
// Last Modified: 2025-08-13
// Author: UltraCanvas Framework
#pragma once

#include "../../include/UltraCanvasClipboard.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <memory>
#include <chrono>

namespace UltraCanvas {

// ===== X11 CLIPBOARD BACKEND =====
class UltraCanvasLinuxClipboard : public UltraCanvasClipboardBackend {
private:
    // X11 resources (borrowed from UltraCanvasLinuxApplication)
    Display* display;
    Window window;
    
    // X11 Atoms for clipboard handling
    Atom atomClipboard;
    Atom atomPrimary;
    Atom atomTargets;
    Atom atomText;
    Atom atomUtf8String;
    Atom atomString;
    Atom atomTextPlain;
    Atom atomTextPlainUtf8;
    Atom atomImagePng;
    Atom atomImageJpeg;
    Atom atomImageBmp;
    Atom atomTextUriList;
    Atom atomApplicationOctetStream;
    
    // Change detection
    std::chrono::steady_clock::time_point lastChangeCheck;
    std::string lastClipboardText;
    bool clipboardChanged;
    
    // Selection handling
    std::vector<uint8_t> selectionData;
    std::string selectionFormat;
    bool selectionReady;
    
    // Constants
    static constexpr int SELECTION_TIMEOUT_MS = 1000;
    static constexpr size_t MAX_CLIPBOARD_SIZE = 10 * 1024 * 1024; // 10MB
    
public:
    UltraCanvasLinuxClipboard();
    ~UltraCanvasLinuxClipboard() override;
    
    // ===== INITIALIZATION =====
    bool Initialize() override;
    void Shutdown() override;
    
    // ===== CLIPBOARD OPERATIONS =====
    bool GetClipboardText(std::string& text) override;
    bool SetClipboardText(const std::string& text) override;
    bool GetClipboardImage(std::vector<uint8_t>& imageData, std::string& format) override;
    bool SetClipboardImage(const std::vector<uint8_t>& imageData, const std::string& format) override;
    bool GetClipboardFiles(std::vector<std::string>& filePaths) override;
    bool SetClipboardFiles(const std::vector<std::string>& filePaths) override;
    
    // ===== MONITORING =====
    bool HasClipboardChanged() override;
    void ResetChangeState() override;
    
    // ===== FORMAT DETECTION =====
    std::vector<std::string> GetAvailableFormats() override;
    bool IsFormatAvailable(const std::string& format) override;
    
private:
    // ===== X11 HELPER METHODS =====
    void InitializeAtoms();
    Window CreateHelperWindow();
    bool GetDisplayFromApplication();
    
    // ===== SELECTION HANDLING =====
    bool RequestSelection(Atom selection, Atom target, std::vector<uint8_t>& data, std::string& format);
    bool ProcessSelectionNotify(XEvent& event);
    bool ProcessSelectionRequest(XEvent& event);
    bool WaitForSelectionNotify(std::vector<uint8_t>& data, std::string& format);
    
    // ===== CLIPBOARD READING =====
    bool ReadClipboardData(Atom selection, Atom target, std::vector<uint8_t>& data, std::string& format);
    bool ReadTextFromClipboard(Atom selection, std::string& text);
    bool ReadImageFromClipboard(Atom selection, std::vector<uint8_t>& imageData, std::string& format);
    bool ReadFilesFromClipboard(Atom selection, std::vector<std::string>& filePaths);
    
    // ===== CLIPBOARD WRITING =====
    bool WriteClipboardData(Atom selection, Atom target, const std::vector<uint8_t>& data);
    bool WriteTextToClipboard(Atom selection, const std::string& text);
    bool WriteImageToClipboard(Atom selection, const std::vector<uint8_t>& imageData, const std::string& format);
    bool WriteFilesToClipboard(Atom selection, const std::vector<std::string>& filePaths);
    
    // ===== UTILITY METHODS =====
    std::string AtomToString(Atom atom);
    Atom StringToAtom(const std::string& str, bool createIfMissing = false);
    std::string FormatToMimeType(const std::string& format);
    std::string MimeTypeToFormat(const std::string& mimeType);
    bool IsTextFormat(Atom target);
    bool IsImageFormat(Atom target);
    bool IsFileFormat(Atom target);
    
    // ===== ERROR HANDLING =====
    void LogError(const std::string& operation, const std::string& details = "");
    bool CheckXError();
};

} // namespace UltraCanvas