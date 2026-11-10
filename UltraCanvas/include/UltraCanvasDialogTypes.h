// include/UltraCanvasDialogTypes.h
// The plain types a dialog is described with - its kind (DialogType), its
// buttons and their roles, its result, animation and position, the input and
// file dialog kinds, and a file type filter (FileFilter) - apart from the
// dialog classes in UltraCanvasModalDialog.h. A header that only names these
// (UltraCanvasFilerWidget.h, whose operation dialogs carry a DialogType and
// button roles) includes this one, so it does not pull in the whole dialog
// system - and UltraCanvasModalDialog.h, which includes the Filer for the
// file dialog's listing, does not include itself back through it.
// Version: 1.0.0
// Last Modified: 2026-11-09
// Author: UltraCanvas Framework
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== DIALOG TYPES =====
    enum class DialogType {
        Information,
        Successful,   // "Success" is an X11 macro (#define Success 0), so we spell it out
        Question,
        Warning,
        Error,
        Custom
    };

// ===== DIALOG BUTTONS =====
    enum class DialogButton {
        NoneButton = 0,
        OK = 1,
        Cancel = 2,
        Yes = 4,
        No = 8,
        Apply = 16,
        Close = 32,
        Help = 64,
        Retry = 128,
        Ignore = 256,
        Abort = 512
    };

// Button combinations
    enum class DialogButtons {
        NoButtons = 0,
        OK = static_cast<int>(DialogButton::OK),
        OKCancel = static_cast<int>(DialogButton::OK) | static_cast<int>(DialogButton::Cancel),
        YesNo = static_cast<int>(DialogButton::Yes) | static_cast<int>(DialogButton::No),
        YesNoCancel = static_cast<int>(DialogButton::Yes) | static_cast<int>(DialogButton::No) | static_cast<int>(DialogButton::Cancel),
        RetryCancel = static_cast<int>(DialogButton::Retry) | static_cast<int>(DialogButton::Cancel),
        AbortRetryIgnore = static_cast<int>(DialogButton::Abort) | static_cast<int>(DialogButton::Retry) | static_cast<int>(DialogButton::Ignore)
    };

// ===== CUSTOM BUTTON ROLES =====
    // What a custom button (AddCustomButton) is to the dialog beyond its
    // label. Default is the button Return activates and the one drawn in the
    // accent colour; Destructive is drawn red, so an answer that cannot be
    // undone (Delete permanently, Replace a folder) looks different from the
    // one beside it; Cancel is the button Escape and the close box map to.
    // A button can be Default and Destructive at once: DestructiveDefault.
    enum class DialogButtonRole {
        Normal,
        Default,
        Destructive,
        DestructiveDefault,
        Cancel
    };

// ===== DIALOG RESULT =====
    enum class DialogResult {
        NoResult,  // Changed from None to avoid X11 macro conflict
        OK,
        Cancel,
        Yes,
        No,
        Apply,
        Close,
        Help,
        Retry,
        Ignore,
        Abort
    };

// ===== DIALOG ANIMATION =====
    enum class DialogAnimation {
        NoAnimation,
        Fade,
        Scale,
        Slide,
        Bounce
    };

// ===== DIALOG POSITION =====
    enum class DialogPosition {
        Center,
        CenterParent,
        TopLeft,
        TopCenter,
        TopRight,
        MiddleLeft,
        MiddleRight,
        BottomLeft,
        BottomCenter,
        BottomRight,
        Custom
    };

// ===== INPUT DIALOG TYPES =====
    enum class InputType {
        Text,
        Password,
        Number,
        Email,
        URL
    };

// ===== FILE DIALOG TYPE =====
    enum class FileDialogType {
        Open,
        Save,
        OpenMultiple,
        SelectFolder
    };

// ===== FILE FILTER STRUCTURE =====
    struct FileFilter {
        std::string description;
        std::vector<std::string> extensions;

        FileFilter() = default;

        FileFilter(const std::string& desc, const std::vector<std::string>& exts)
                : description(desc), extensions(exts) {}

        FileFilter(const std::string& desc, const std::string& ext)
                : description(desc), extensions({ext}) {}

        // Convert to display string: "Text Files (*.txt, *.log)"
        std::string ToDisplayString() const {
            std::string result = description + " (";
            for (size_t i = 0; i < extensions.size(); i++) {
                if (i > 0) result += ", ";
                result += "*." + extensions[i];
            }
            result += ")";
            return result;
        }

        // Check if a filename matches this filter
        bool Matches(const std::string& filename) const {
            for (const std::string& ext : extensions) {
                if (ext == "*") return true;

                size_t dotPos = filename.find_last_of('.');
                if (dotPos != std::string::npos && dotPos < filename.length() - 1) {
                    std::string fileExt = filename.substr(dotPos + 1);
                    // Case-insensitive comparison
                    std::string lowerFileExt = fileExt;
                    std::string lowerExt = ext;
                    std::transform(lowerFileExt.begin(), lowerFileExt.end(), lowerFileExt.begin(), ::tolower);
                    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
                    if (lowerFileExt == lowerExt) return true;
                }
            }
            return false;
        }
    };

} // namespace UltraCanvas
