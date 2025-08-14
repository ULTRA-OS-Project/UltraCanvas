// UltraCanvasLinuxClipboard.h - Fixed X11 Clipboard Implementation
// X11-specific clipboard implementation with proper write support
// Version: 1.1.0
// Last Modified: 2025-08-14
// Author: UltraCanvas Framework

#pragma once

#include "../../include/UltraCanvasClipboard.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <memory>
#include <chrono>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== X11 CLIPBOARD BACKEND =====
    class UltraCanvasLinuxClipboard : public UltraCanvasClipboardBackend {
    private:
        // ===== X11 RESOURCES =====
        Display* display;
        Window window;
        XEvent currentEvent;

        // ===== X11 ATOMS =====
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

        // ===== CLIPBOARD STATE =====
        std::chrono::steady_clock::time_point lastChangeCheck;
        std::string lastClipboardText;
        bool clipboardChanged;

        // ===== SELECTION HANDLING =====
        std::vector<uint8_t> selectionData;
        std::string selectionFormat;
        bool selectionReady;

        // ===== OWNERSHIP TRACKING =====
        bool ownsClipboard;
        bool ownsPrimary;
        std::string clipboardTextData;  // Our clipboard data when we own it

        // ===== CONSTANTS =====
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

        // ===== EVENT PROCESSING =====
        void ProcessEvents(); // Call this from main event loop

    private:
        // ===== INITIALIZATION HELPERS =====
        void InitializeAtoms();
        Window CreateHelperWindow();
        bool GetDisplayFromApplication();

        // ===== CORE SELECTION OPERATIONS =====
        bool TakeSelectionOwnership(Atom selection);
        bool ReadTextFromSelection(Atom selection, std::string& text);
        void ProcessSelectionEvents();

        // ===== EVENT HANDLING =====
        void HandleSelectionEvent(const XEvent& event);
        void HandleSelectionRequest(const XSelectionRequestEvent& request);
        void HandleSelectionClear(const XSelectionClearEvent& clear);
        void HandleSelectionNotify(const XSelectionEvent& notify);

        // ===== UNIFIED EVENT PROCESSING =====
        bool ProcessSelectionEventsWithTimeout(int timeoutMs, bool waitForSelectionReady);

        // ===== LOW-LEVEL SELECTION HANDLING =====
        bool ReadClipboardData(Atom selection, Atom target, std::vector<uint8_t>& data, std::string& format);
        bool WriteClipboardData(Atom selection, Atom target, const std::vector<uint8_t>& data);
        bool WaitForSelectionNotify(std::vector<uint8_t>& data, std::string& format);

        // ===== TEXT OPERATIONS =====
        bool ReadTextFromClipboard(Atom selection, std::string& text);
        bool WriteTextToClipboard(Atom selection, const std::string& text);

        // ===== IMAGE OPERATIONS =====
        bool ReadImageFromClipboard(Atom selection, std::vector<uint8_t>& imageData, std::string& format);
        bool WriteImageToClipboard(Atom selection, const std::vector<uint8_t>& imageData, const std::string& format);

        // ===== FILE OPERATIONS =====
        bool ReadFilesFromClipboard(Atom selection, std::vector<std::string>& filePaths);
        bool WriteFilesToClipboard(Atom selection, const std::vector<std::string>& filePaths);

        // ===== UTILITY FUNCTIONS =====
        std::string AtomToString(Atom atom);
        Atom StringToAtom(const std::string& str, bool createIfMissing = false);
        std::string FormatToMimeType(const std::string& format);
        std::string MimeTypeToFormat(const std::string& mimeType);
        void LogError(const std::string& function, const std::string& message);
        void LogInfo(const std::string& function, const std::string& message);
        bool CheckXError();

        // ===== FORMAT VALIDATION =====
        bool IsTextFormat(Atom target);
        bool IsImageFormat(Atom target);
        bool IsFileFormat(Atom target);

        // ===== DATA CONVERSION =====
        std::vector<uint8_t> StringToBytes(const std::string& str);
        std::string BytesToString(const std::vector<uint8_t>& bytes);
    };

// ===== GLOBAL CLIPBOARD FUNCTIONS =====

// Initialize the Linux clipboard system
    bool InitializeLinuxClipboard();

// Cleanup the Linux clipboard system
    void CleanupLinuxClipboard();

// Get the global clipboard instance
    UltraCanvasLinuxClipboard* GetLinuxClipboard();

// Process clipboard events (call from main event loop)
    void ProcessClipboardEvents();

} // namespace UltraCanvas