// include/UltraCanvasNativeDialogs.h
// Cross-platform native OS dialog system for file operations and messages
// Uses unified DialogType, DialogButtons, DialogResult from UltraCanvasModalDialog.h
// Version: 2.1.0
// Last Modified: 2026-01-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasModalDialog.h"  // For DialogType, DialogButtons, DialogResult, FileFilter, InputType
#include <string>
#include <vector>
#include <functional>

namespace UltraCanvas {

// ===== NATIVE FILE DIALOG OPTIONS =====
// Uses FileFilter from UltraCanvasModalDialog.h
    struct NativeFileDialogOptions {
        std::string title;
        std::string initialDirectory;
        std::string defaultFileName;
        std::vector<FileFilter> filters;
        bool allowMultiSelect = false;
        bool showHiddenFiles = false;
        UltraCanvasWindowBase* parentWindow = nullptr;  // Parent window for modal behavior

        NativeFileDialogOptions() = default;

        NativeFileDialogOptions& SetTitle(const std::string& t) { title = t; return *this; }
        NativeFileDialogOptions& SetInitialDirectory(const std::string& dir) { initialDirectory = dir; return *this; }
        NativeFileDialogOptions& SetDefaultFileName(const std::string& name) { defaultFileName = name; return *this; }
        NativeFileDialogOptions& AddFilter(const std::string& desc, const std::string& ext) {
            filters.emplace_back(desc, ext);
            return *this;
        }
        NativeFileDialogOptions& AddFilter(const std::string& desc, const std::vector<std::string>& exts) {
            filters.emplace_back(desc, exts);
            return *this;
        }
        NativeFileDialogOptions& SetMultiSelect(bool multi) { allowMultiSelect = multi; return *this; }
        NativeFileDialogOptions& SetShowHidden(bool show) { showHiddenFiles = show; return *this; }
        NativeFileDialogOptions& SetParentWindow(UltraCanvasWindowBase* parent) { parentWindow = parent; return *this; }
    };

// ===== NATIVE INPUT DIALOG OPTIONS =====
    struct NativeInputDialogOptions {
        std::string title = "Input";
        std::string prompt = "Enter value:";
        std::string defaultValue;
        bool password = false;
        UltraCanvasWindowBase* parentWindow = nullptr;  // Parent window for modal behavior

        NativeInputDialogOptions() = default;

        NativeInputDialogOptions& SetTitle(const std::string& t) { title = t; return *this; }
        NativeInputDialogOptions& SetPrompt(const std::string& p) { prompt = p; return *this; }
        NativeInputDialogOptions& SetDefaultValue(const std::string& val) { defaultValue = val; return *this; }
        NativeInputDialogOptions& SetPassword(bool pwd) { password = pwd; return *this; }
        NativeInputDialogOptions& SetParentWindow(UltraCanvasWindowBase* parent) { parentWindow = parent; return *this; }
    };

// ===== NATIVE INPUT RESULT =====
    struct NativeInputResult {
        DialogResult result = DialogResult::Cancel;
        std::string value;

        bool IsOK() const { return result == DialogResult::OK; }
        bool IsCancelled() const { return result == DialogResult::Cancel; }
        operator bool() const { return IsOK(); }
    };

// ===== NATIVE DIALOGS CLASS =====
// Platform-independent interface - implementations in OS-specific files
// Uses unified enums: DialogType, DialogButtons, DialogResult
    class UltraCanvasNativeDialogs {
    public:
        // ===== MESSAGE DIALOGS =====

        // Show information message
        static DialogResult ShowInfo(
                const std::string& message,
                const std::string& title = "Information",
                UltraCanvasWindowBase*  parent = nullptr);

        // Show warning message
        static DialogResult ShowWarning(
                const std::string& message,
                const std::string& title = "Warning",
                UltraCanvasWindowBase*  parent = nullptr);

        // Show error message
        static DialogResult ShowError(
                const std::string& message,
                const std::string& title = "Error",
                UltraCanvasWindowBase*  parent = nullptr);

        // Show question dialog
        static DialogResult ShowQuestion(
                const std::string& message,
                const std::string& title = "Question",
                DialogButtons buttons = DialogButtons::YesNo,
                UltraCanvasWindowBase*  parent = nullptr);

        // Show generic message box
        static DialogResult ShowMessage(
                const std::string& message,
                const std::string& title,
                DialogType type,
                DialogButtons buttons,
                UltraCanvasWindowBase*  parent = nullptr);

        // ===== CONFIRMATION DIALOGS =====

        // Show OK/Cancel confirmation (returns true if OK)
        static bool Confirm(
                const std::string& message,
                const std::string& title = "Confirm",
                UltraCanvasWindowBase*  parent = nullptr);

        // Show Yes/No confirmation (returns true if Yes)
        static bool ConfirmYesNo(
                const std::string& message,
                const std::string& title = "Confirm",
                UltraCanvasWindowBase*  parent = nullptr);

        // ===== FILE DIALOGS =====

        // Show open file dialog (single file)
        static std::string OpenFile(
                const std::string& title = "Open File",
                const std::vector<FileFilter>& filters = {},
                const std::string& initialDir = "",
                UltraCanvasWindowBase*  parent = nullptr);

        // Show open file dialog with options
        static std::string OpenFile(const NativeFileDialogOptions& options);

        // Show open multiple files dialog
        static std::vector<std::string> OpenMultipleFiles(
                const std::string& title = "Open Files",
                const std::vector<FileFilter>& filters = {},
                const std::string& initialDir = "",
                UltraCanvasWindowBase*  parent = nullptr);

        // Show open multiple files dialog with options
        static std::vector<std::string> OpenMultipleFiles(const NativeFileDialogOptions& options);

        // Show save file dialog
        static std::string SaveFile(
                const std::string& title = "Save File",
                const std::vector<FileFilter>& filters = {},
                const std::string& initialDir = "",
                const std::string& defaultFileName = "",
                UltraCanvasWindowBase*  parent = nullptr);

        // Show save file dialog with options
        static std::string SaveFile(const NativeFileDialogOptions& options);

        // Show folder selection dialog
        static std::string SelectFolder(
                const std::string& title = "Select Folder",
                const std::string& initialDir = "",
                UltraCanvasWindowBase*  parent = nullptr);

        // ===== INPUT DIALOGS =====

        // Show text input dialog
        static NativeInputResult InputText(
                const std::string& prompt,
                const std::string& title = "Input",
                const std::string& defaultValue = "",
                UltraCanvasWindowBase*  parent = nullptr);

        // Show text input dialog with options
        static NativeInputResult InputText(const NativeInputDialogOptions& options);

        // Show password input dialog
        static NativeInputResult InputPassword(
                const std::string& prompt,
                const std::string& title = "Password",
                UltraCanvasWindowBase*  parent = nullptr);

        // ===== CONVENIENCE FUNCTIONS =====

        // Simple input that returns string directly (empty if cancelled)
        static std::string GetInput(
                const std::string& prompt,
                const std::string& title = "Input",
                const std::string& defaultValue = "",
                UltraCanvasWindowBase*  parent = nullptr);

        // Simple password input that returns string directly (empty if cancelled)
        static std::string GetPassword(
                const std::string& prompt,
                const std::string& title = "Password",
                UltraCanvasWindowBase*  parent = nullptr);

        // Show the OS native print dialog for the given text content.
        // documentName  — displayed in the printer queue (e.g. the filename)
        // textContent   — UTF-8 plain text to be sent to the printer on OK
        // parent        — native parent window handle for modal behaviour
        // Returns true if the user confirmed printing, false if cancelled.
        static bool ShowPrintDialog(
                const std::string& documentName,
                const std::string& textContent,
                UltraCanvasWindowBase*  parent = nullptr);
    };

// ===== GLOBAL CONVENIENCE FUNCTIONS =====
// These provide even simpler access without class prefix

    namespace NativeDialog {

// Message boxes
        inline DialogResult Info(const std::string& message, const std::string& title = "Information",
                                 UltraCanvasWindowBase*  parent = nullptr) {
            return UltraCanvasNativeDialogs::ShowInfo(message, title, parent);
        }

        inline DialogResult Warning(const std::string& message, const std::string& title = "Warning",
                                    UltraCanvasWindowBase*  parent = nullptr) {
            return UltraCanvasNativeDialogs::ShowWarning(message, title, parent);
        }

        inline DialogResult Error(const std::string& message, const std::string& title = "Error",
                                  UltraCanvasWindowBase*  parent = nullptr) {
            return UltraCanvasNativeDialogs::ShowError(message, title, parent);
        }

        inline DialogResult Question(const std::string& message, const std::string& title = "Question",
                                     DialogButtons buttons = DialogButtons::YesNo,
                                     UltraCanvasWindowBase*  parent = nullptr) {
            return UltraCanvasNativeDialogs::ShowQuestion(message, title, buttons, parent);
        }

// Confirmations
        inline bool Confirm(const std::string& message, const std::string& title = "Confirm",
                            UltraCanvasWindowBase*  parent = nullptr) {
            return UltraCanvasNativeDialogs::Confirm(message, title, parent);
        }

        inline bool ConfirmYesNo(const std::string& message, const std::string& title = "Confirm",
                                 UltraCanvasWindowBase*  parent = nullptr) {
            return UltraCanvasNativeDialogs::ConfirmYesNo(message, title, parent);
        }

// File dialogs
        inline std::string OpenFile(const std::string& title = "Open File",
                                    const std::vector<FileFilter>& filters = {},
                                    const std::string& initialDir = "",
                                    UltraCanvasWindowBase*  parent = nullptr) {
            return UltraCanvasNativeDialogs::OpenFile(title, filters, initialDir, parent);
        }

        inline std::vector<std::string> OpenMultipleFiles(const std::string& title = "Open Files",
                                                          const std::vector<FileFilter>& filters = {},
                                                          const std::string& initialDir = "",
                                                          UltraCanvasWindowBase*  parent = nullptr) {
            return UltraCanvasNativeDialogs::OpenMultipleFiles(title, filters, initialDir, parent);
        }

        inline std::string SaveFile(const std::string& title = "Save File",
                                    const std::vector<FileFilter>& filters = {},
                                    const std::string& initialDir = "",
                                    const std::string& defaultFileName = "",
                                    UltraCanvasWindowBase*  parent = nullptr) {
            return UltraCanvasNativeDialogs::SaveFile(title, filters, initialDir, defaultFileName, parent);
        }

        inline std::string SelectFolder(const std::string& title = "Select Folder",
                                        const std::string& initialDir = "",
                                        UltraCanvasWindowBase*  parent = nullptr) {
            return UltraCanvasNativeDialogs::SelectFolder(title, initialDir, parent);
        }

// Input dialogs
        inline std::string Input(const std::string& prompt, const std::string& title = "Input",
                                 const std::string& defaultValue = "",
                                 UltraCanvasWindowBase*  parent = nullptr) {
            return UltraCanvasNativeDialogs::GetInput(prompt, title, defaultValue, parent);
        }

        inline std::string Password(const std::string& prompt, const std::string& title = "Password",
                                    UltraCanvasWindowBase*  parent = nullptr) {
            return UltraCanvasNativeDialogs::GetPassword(prompt, title, parent);
        }

        inline bool Print(const std::string& documentName,
                          const std::string& textContent,
                          UltraCanvasWindowBase*  parent = nullptr) {
            return UltraCanvasNativeDialogs::ShowPrintDialog(documentName, textContent, parent);
        }
    } // namespace NativeDialog

} // namespace UltraCanvas

/*
=== USAGE EXAMPLES ===

// ===== MESSAGE DIALOGS (using unified DialogResult) =====

// Simple info message
UltraCanvas::NativeDialog::Info("Operation completed successfully!");

// Warning with custom title
UltraCanvas::NativeDialog::Warning("File may be corrupted", "Warning");

// Error message
UltraCanvas::NativeDialog::Error("Failed to save file", "Error");

// Question dialog - now uses DialogResult from UltraCanvasModalDialog.h
auto result = UltraCanvas::NativeDialog::Question("Do you want to save changes?", "Save");
if (result == UltraCanvas::DialogResult::Yes) {
    // Save changes
}

// ===== FILE DIALOGS (using FileFilter from UltraCanvasModalDialog.h) =====

// Open single file
std::string filePath = UltraCanvas::NativeDialog::OpenFile("Select Image", {
    UltraCanvas::FileFilter("Image Files", {"png", "jpg", "jpeg", "gif", "bmp"}),
    UltraCanvas::FileFilter("All Files", "*")
});
if (!filePath.empty()) {
    // Load file
}

// Save file
std::string savePath = UltraCanvas::NativeDialog::SaveFile("Save Document", {
    UltraCanvas::FileFilter("Text Files", "txt"),
    UltraCanvas::FileFilter("All Files", "*")
}, "", "document.txt");
if (!savePath.empty()) {
    // Save to path
}

// ===== SWITCHING BETWEEN NATIVE AND INTERNAL DIALOGS =====

// Use native OS dialogs (GTK/Win32/Cocoa)
UltraCanvas::UltraCanvasDialogManager::SetUseNativeDialogs(true);

// Use UltraCanvas internal dialogs (consistent appearance)
UltraCanvas::UltraCanvasDialogManager::SetUseNativeDialogs(false);

// All DialogManager methods work the same regardless of setting:
UltraCanvas::UltraCanvasDialogManager::ShowMessage("Hello", "Title",
    UltraCanvas::DialogType::Information, UltraCanvas::DialogButtons::OK,
    [](UltraCanvas::DialogResult r) { // handle result // });

=== PLATFORM NOTES ===

Windows:
- Uses GetOpenFileName/GetSaveFileName for file dialogs
- Uses MessageBox for message dialogs
- Input dialogs use custom dialog resource or TaskDialog

        Linux:
- Uses GTK+ file chooser dialogs (gtk_file_chooser_dialog)
- Uses GTK+ message dialogs (gtk_message_dialog)
- Uses GTK+ entry dialogs for input

        macOS:
- Uses NSOpenPanel/NSSavePanel for file dialogs
- Uses NSAlert for message dialogs
- Uses NSAlert with text field for input dialogs

=== INTEGRATION NOTES ===

This module provides NATIVE OS dialogs that integrate seamlessly with
UltraCanvas's modal dialog system. Use SetUseNativeDialogs() to switch:

- Native dialogs (true): OS-native appearance, optimized performance
- Internal dialogs (false): Consistent cross-platform appearance

The same DialogType, DialogButtons, DialogResult enums work for both!

*/