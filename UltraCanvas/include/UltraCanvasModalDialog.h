// include/UltraCanvasModalDialog.h
// Cross-platform modal dialog system - Window-based implementation with layout managers
// Supports switching between native OS dialogs and internal UltraCanvas dialogs
// Version: 3.3.0
// Last Modified: 2026-01-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasBoxLayout.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace UltraCanvas {

// Forward declarations
    class UltraCanvasWindowBase;

// ===== DIALOG TYPES =====
    enum class DialogType {
        Information,
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
        URL,
        MultilineText  // Changed from Multiline
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

// ===== DIALOG STYLE =====
    struct ModalDialogStyle {
        // Spacing
        float padding = 16.0f;
        float sectionSpacing = 12.0f;
        float buttonSpacing = 10.0f;
        float iconMessageSpacing = 12.0f;

        // Icon
        float iconSize = 48.0f;
        float iconFontSize = 20.0f;

        // Typography
        float messageFontSize = 12.0f;
        float detailsFontSize = 11.0f;

        // Buttons
        float buttonWidth = 80.0f;
        float buttonHeight = 28.0f;
        float buttonAreaHeight = 50.0f;

        // Colors
        Color messageTextColor = Colors::Black;
        Color detailsTextColor = Colors::DarkGray;

        static ModalDialogStyle Default() { return ModalDialogStyle(); }
    };

// ===== DIALOG CONFIGURATION =====
// DialogConfig extends WindowConfig with dialog-specific properties
    struct DialogConfig : public WindowConfig {
        // Dialog content
        std::string message;
        std::string details;
        DialogType dialogType = DialogType::Information;

        // Buttons
        DialogButtons buttons = DialogButtons::OK;
        DialogButton defaultButton = DialogButton::OK;
        DialogButton cancelButton = DialogButton::Cancel;

        // Dialog-specific positioning
        DialogPosition position = DialogPosition::CenterParent;

        // Dialog behavior
        bool closeOnEscape = true;
//        bool closeOnClickOutside = false;
        float autoCloseTime = 0.0f; // 0 = no auto close

        DialogConfig() {
            // Set window defaults for dialogs
            title = "Dialog";
            width = 500;
            height = 300;
            type = WindowType::Dialog;
            resizable = false;
            minimizable = false;
            maximizable = false;
            closable = true;
            alwaysOnTop = true;
            modal = true;
            deleteOnClose = true;
            backgroundColor = Colors::White;
        }

        // Helper to access size as Point2Di (for compatibility)
        Point2Di GetSize() const { return Point2Di(width, height); }
        void SetSize(const Point2Di& size) { width = size.x; height = size.y; }
    };

// ===== INPUT DIALOG CONFIGURATION =====
    struct InputDialogConfig : public DialogConfig {
        // Input properties
        InputType inputType = InputType::Text;
        std::string inputLabel = "Input:";
        std::string inputPlaceholder;
        std::string defaultValue;
        std::string validationPattern; // Regex pattern
        std::string validationMessage = "Invalid input";

        // Input constraints
        int minLength = 0;
        int maxLength = 1000;
        int minLines = 1;  // For multiline text
        int maxLines = 10; // For multiline text
        bool required = false;

        // Input validation callback
        std::function<bool(const std::string&)> validator;
        std::function<void(const std::string&)> onInputChanged;

        InputDialogConfig() : DialogConfig() {
            buttons = DialogButtons::OKCancel;
            width = 400;
            height = 150;
        }
    };

// ===== FILE DIALOG CONFIGURATION =====
    struct FileDialogConfig : public DialogConfig {
        FileDialogType dialogType = FileDialogType::Open;
        std::string initialDirectory;
        std::string defaultFileName;
        std::string defaultExtension;
        std::vector<FileFilter> filters;
        int selectedFilterIndex = 0;
        bool allowMultipleSelection = false;
        bool showHiddenFiles = false;
        bool validateNames = true;
        bool addToRecent = true;

        FileDialogConfig() : DialogConfig() {
            buttons = DialogButtons::OKCancel;
            width = 600;
            height = 450;
            resizable = true;
            // Default filters
            filters = {
                    FileFilter("All Files", "*"),
                    FileFilter("Text Files", {"txt", "log", "md"}),
                    FileFilter("Image Files", {"png", "jpg", "jpeg", "gif", "bmp"}),
                    FileFilter("Document Files", {"pdf", "doc", "docx", "rtf"})
            };
        }

        // Add a filter
        void AddFilter(const FileFilter& filter) {
            filters.push_back(filter);
        }

        // Add filter with description and single extension
        void AddFilter(const std::string& description, const std::string& extension) {
            filters.emplace_back(description, extension);
        }

        // Add filter with description and multiple extensions
        void AddFilter(const std::string& description, const std::vector<std::string>& extensions) {
            filters.emplace_back(description, extensions);
        }

        // Clear all filters
        void ClearFilters() {
            filters.clear();
            selectedFilterIndex = 0;
        }

        // Set filters from pipe-separated string: "Text files (*.txt)|*.txt|All files (*.*)|*.*"
        void SetFiltersFromString(const std::string& filterString) {
            filters.clear();
            selectedFilterIndex = 0;

            if (filterString.empty()) return;

            std::vector<std::string> parts;
            std::stringstream ss(filterString);
            std::string part;
            while (std::getline(ss, part, '|')) {
                parts.push_back(part);
            }

            for (size_t i = 0; i + 1 < parts.size(); i += 2) {
                std::string desc = parts[i];
                std::string extPattern = parts[i + 1];

                std::vector<std::string> extensions;
                std::stringstream extSs(extPattern);
                std::string ext;
                while (std::getline(extSs, ext, ';')) {
                    // Remove "*." prefix if present
                    if (ext.substr(0, 2) == "*.") {
                        ext = ext.substr(2);
                    }
                    if (!ext.empty()) {
                        extensions.push_back(ext);
                    }
                }

                if (!extensions.empty()) {
                    filters.emplace_back(desc, extensions);
                }
            }
        }
    };

// ===== MODAL DIALOG CLASS =====
// UltraCanvasModalDialog is a top-level window with dialog behavior.
// It inherits from UltraCanvasWindow to get full window functionality
// including proper event handling, coordinate conversion, focus management,
// mouse capture, and platform-native window creation.
//
// Modal behavior is implemented at the application level by blocking
// input events to non-modal windows when a modal dialog is active.
//
// VERSION 3.2.0: Refactored to use layout managers and UI components
// instead of manual coordinate calculations and low-level drawing.
    class UltraCanvasModalDialog : public UltraCanvasWindow {
        friend class UltraCanvasDialogManager;
    protected:
        DialogConfig dialogConfig;
        ModalDialogStyle style;
        DialogResult result = DialogResult::NoResult;

        // ===== SECTION CONTAINERS =====
        std::shared_ptr<UltraCanvasContainer> contentSection;
        std::shared_ptr<UltraCanvasContainer> footerSection;

        // ===== CONTENT COMPONENTS =====
        std::shared_ptr<UltraCanvasContainer> iconContainer;
        std::shared_ptr<UltraCanvasLabel> iconLabel;
        std::shared_ptr<UltraCanvasContainer> messageContainer;
        std::shared_ptr<UltraCanvasLabel> messageLabel;
        std::shared_ptr<UltraCanvasLabel> detailsLabel;

        // ===== FOOTER COMPONENTS =====
        std::vector<std::shared_ptr<UltraCanvasButton>> dialogButtons;

    public:
        // ===== DIALOG OPERATIONS =====
        // ShowModal shows the dialog and returns immediately (non-blocking).
        virtual void ShowModal(UltraCanvasWindowBase* parent = nullptr);
        // Close the dialog with specified result
        void CloseDialog(DialogResult result = DialogResult::Cancel);
        // Create dialog and window with config
        void CreateDialog(const DialogConfig& config = DialogConfig());

        // ===== PROPERTIES =====
        void SetDialogTitle(const std::string& title);
        void SetMessage(const std::string& message);
        void SetDetails(const std::string& details);
        void SetDialogType(DialogType type);
        void SetDialogButtons(DialogButtons buttons);
        void SetDefaultButton(DialogButton button);
        void SetStyle(const ModalDialogStyle& dialogStyle);

        std::string GetDialogTitle() const;
        std::string GetMessage() const;
        std::string GetDetails() const;
        DialogType GetDialogType() const;
        DialogButtons GetDialogButtons() const;
        DialogButton GetDefaultButton() const;
        ModalDialogStyle GetStyle() const;

        // ===== STATE QUERIES =====
        bool IsModalDialog() const;
        DialogResult GetResult() const;

        // ===== BUTTON MANAGEMENT =====
        void AddCustomButton(const std::string& text, DialogResult result, std::function<void()> callback = nullptr);
        void SetButtonDisabled(DialogButton button, bool disabled);
        void SetButtonVisible(DialogButton button, bool visible);

        // ===== CONTENT MANAGEMENT =====
        // Add custom UI elements to the dialog's content area
        void AddDialogElement(std::shared_ptr<UltraCanvasUIElement> element);
        void RemoveDialogElement(std::shared_ptr<UltraCanvasUIElement> element);
        void ClearDialogElements();

        void RequestClose() override;

        void Close() override;

        // ===== RENDERING OVERRIDE =====
        // NOTE: With layout-based architecture, RenderCustomContent delegates
        // to child components which render themselves
        void RenderCustomContent(IRenderContext* ctx) override;

        bool OnEvent(const UCEvent& event) override;

        // Callbacks
        std::function<void(DialogResult)> onResult;
        std::function<bool(DialogResult)> onClosing; // Return false to prevent closing

        protected:
        // ===== LAYOUT BUILDING =====
        void BuildDialogLayout();
        void CreateContentSection();
        void CreateFooterSection();
        void CreateDialogButtons();
        void WireButtonCallbacks();

        // ===== TYPE-SPECIFIC =====
        void UpdateIconAppearance();
        void UpdateMessageContent();
        Color GetTypeColor() const;
        std::string GetTypeIcon() const;
        void ApplyTypeDefaults();

        // ===== EVENT HELPERS =====
        void OnDialogButtonClick(DialogButton button);

        // ===== UTILITY =====
        std::string GetButtonText(DialogButton button) const;
    };

// ===== DIALOG MANAGER =====
    class UltraCanvasInputDialog;
    class UltraCanvasFileDialog;
    class UltraCanvasDialogManager {
        friend class UltraCanvasModalDialog;
    private:
        // Static member declarations only - definitions in .cpp
        static std::vector<std::shared_ptr<UltraCanvasModalDialog>> activeDialogs;
        static std::shared_ptr<UltraCanvasModalDialog> currentModal;
        static bool enabled;
        static bool useNativeDialogs;  // When true, use native OS dialogs instead of internal
        static DialogConfig defaultConfig;
        static InputDialogConfig defaultInputConfig;
        static FileDialogConfig defaultFileConfig;

    public:
        // ===== MODAL EVENT BLOCKING =====
        // Call this from UltraCanvasBaseApplication::DispatchEvent BEFORE routing events.
        // Returns true if the event should be blocked (a modal is active and event
        // targets a different window).
        static bool HandleModalEvents(const UCEvent& event, UltraCanvasWindow* targetWindow);

        // Check if there is an active modal window
        static bool HasActiveModal();

        // Get the current modal window (for event routing)
        static UltraCanvasWindowBase* GetModalWindow();

        // ===== ASYNC CALLBACK-BASED DIALOGS (RECOMMENDED) =====
        // These methods show dialogs non-blocking and deliver results via callbacks.

        static void ShowMessage(const std::string& message, const std::string& title,
                                DialogType type, DialogButtons buttons,
                                std::function<void(DialogResult)> onResult,
                                UltraCanvasWindowBase* parent = nullptr);

        static void ShowInformation(const std::string& message, const std::string& title,
                                    std::function<void(DialogResult)> onResult,
                                    UltraCanvasWindowBase* parent = nullptr);

        static void ShowQuestion(const std::string& message, const std::string& title,
                                 std::function<void(DialogResult)> onResult,
                                 UltraCanvasWindowBase* parent = nullptr);

        static void ShowWarning(const std::string& message, const std::string& title,
                                std::function<void(DialogResult)> onResult,
                                UltraCanvasWindowBase* parent = nullptr);

        static void ShowError(const std::string& message, const std::string& title,
                              std::function<void(DialogResult)> onResult,
                              UltraCanvasWindowBase* parent = nullptr);

        static void ShowConfirmation(const std::string& message, const std::string& title,
                                     std::function<void(bool confirmed)> onResult,
                                     UltraCanvasWindowBase* parent = nullptr);

        static void ShowInputDialog(const std::string& prompt, const std::string& title,
                                    const std::string& defaultValue, InputType type,
                                    std::function<void(DialogResult, const std::string&)> onResult,
                                    UltraCanvasWindowBase* parent = nullptr);

        static void ShowOpenFileDialog(const std::string& title,
                                       const std::vector<FileFilter>& filters,
                                       const std::string& initialDir,
                                       std::function<void(DialogResult, const std::string&)> onResult,
                                       UltraCanvasWindowBase* parent = nullptr);

        static void ShowSaveFileDialog(const std::string& title,
                                       const std::vector<FileFilter>& filters,
                                       const std::string& initialDir,
                                       const std::string& defaultName,
                                       std::function<void(DialogResult, const std::string&)> onResult,
                                       UltraCanvasWindowBase* parent = nullptr);

        static void ShowSelectFolderDialog(const std::string& title,
                                           const std::string& initialDir,
                                           std::function<void(DialogResult, const std::string&)> onResult,
                                           UltraCanvasWindowBase* parent = nullptr);

        // ===== CUSTOM DIALOGS =====
        static std::shared_ptr<UltraCanvasModalDialog> CreateDialog(const DialogConfig& config);
        static void ShowDialog(std::shared_ptr<UltraCanvasModalDialog> dialog,
                               std::function<void(DialogResult)> onResult = nullptr,
                               UltraCanvasWindowBase* parent = nullptr);

        // ===== DIALOG MANAGEMENT =====
        static void CloseAllDialogs();
        static std::shared_ptr<UltraCanvasModalDialog> GetCurrentModalDialog();
        static std::vector<std::shared_ptr<UltraCanvasModalDialog>> GetActiveDialogs();
        static int GetActiveDialogCount();

        // ===== CONFIGURATION =====
        static void SetDefaultConfig(const DialogConfig& config);
        static void SetDefaultInputConfig(const InputDialogConfig& config);
        static void SetDefaultFileConfig(const FileDialogConfig& config);
        static DialogConfig GetDefaultConfig();
        static InputDialogConfig GetDefaultInputConfig();
        static FileDialogConfig GetDefaultFileConfig();

        // ===== ENABLE/DISABLE =====
        static void SetEnabled(bool enable);
        static bool IsEnabled();

        // ===== NATIVE DIALOGS MODE =====
        // When enabled, ShowMessage, ShowQuestion, etc. use native OS dialogs
        // (GTK on Linux, Win32 MessageBox on Windows, NSAlert on macOS)
        // instead of the internal UltraCanvas modal dialog system.
        // Native dialogs are BLOCKING - callbacks are invoked immediately before return.
        // File dialogs ALWAYS use native dialogs regardless of this setting.
        static void SetUseNativeDialogs(bool useNative);
        static bool GetUseNativeDialogs();

        // ===== UPDATE =====
        // Call from application main loop to update dialog state
        static void Update(float deltaTime);

        // ===== UTILITY FUNCTIONS =====
        static std::string DialogResultToString(DialogResult result);
        static DialogResult StringToDialogResult(const std::string& str);
        static std::string DialogButtonToString(DialogButton button);
        static DialogButton StringToDialogButton(const std::string& str);

    private:
        // ===== INTERNAL HELPERS =====
        static void RegisterDialog(std::shared_ptr<UltraCanvasModalDialog> dialog);
        static void UnregisterDialog(std::shared_ptr<UltraCanvasModalDialog> dialog);
        static void SetCurrentModal(std::shared_ptr<UltraCanvasModalDialog> dialog);
        static std::shared_ptr<UltraCanvasModalDialog> CreateMessageDialog(const std::string& message, const std::string& title,
                                                                           DialogType type, DialogButtons buttons);
        static std::shared_ptr<UltraCanvasInputDialog> CreateInputDialog(const InputDialogConfig& config);
        static std::shared_ptr<UltraCanvasFileDialog> CreateFileDialog(const FileDialogConfig& config);
    };

// ===== INPUT DIALOG CLASS =====
    class UltraCanvasInputDialog : public UltraCanvasModalDialog {
    private:
        InputDialogConfig inputConfig;
        std::shared_ptr<UltraCanvasTextInput> textInput;
        std::shared_ptr<UltraCanvasLabel> inputLabel;
        std::string inputValue;
        bool isValid;

    public:
        // Create dialog and window with config
        void CreateInputDialog(const InputDialogConfig& config);

        // Input-specific methods
        std::string GetInputValue() const;
        void SetInputValue(const std::string& value);
        bool IsInputValid() const;
        void ValidateInput();

    protected:
        void SetupInputField();
        void OnInputChanged(const std::string& text);
        void OnInputValidation();
    };

// ===== FILE DIALOG CLASS =====
    class UltraCanvasFileDialog : public UltraCanvasModalDialog {
    private:
        FileDialogConfig fileConfig;
        std::vector<std::string> selectedFiles;
        std::string currentDirectory;

        // File browser state
        std::vector<std::string> directoryList;
        std::vector<std::string> fileList;
        int selectedFileIndex = -1;
        int scrollOffset = 0;
        int maxVisibleItems = 15;
        std::string fileNameText;
        bool showHiddenFiles = false;

        // Layout properties
        int itemHeight = 20;
        int pathBarHeight = 30;
        int buttonHeight = 30;
        int filterHeight = 25;

        // Layout rects
        Rect2Di pathBarRect;
        Rect2Di fileListRect;
        Rect2Di fileNameInputRect;
        Rect2Di filterSelectorRect;

        // Colors
        Color listBackgroundColor = Colors::White;
        Color listBorderColor = Colors::Gray;
        Color selectedItemColor = Color(173, 216, 230, 128);
        Color hoverItemColor = Color(220, 240, 255, 128);
        Color directoryColor = Color(70, 130, 180, 255);
        Color fileColor = Colors::Black;

        // Hover state
        int hoverItemIndex = -1;

    public:
        // Callbacks
        std::function<void(const std::string&)> onFileSelected;
        std::function<void(const std::vector<std::string>&)> onFilesSelected;
        std::function<void(const std::string&)> onDirectoryChanged;


        // Create dialog and window with config
        void CreateFileDialog(const FileDialogConfig& config);

        // File-specific methods
        std::vector<std::string> GetSelectedFiles() const;
        std::string GetSelectedFile() const;
        std::string GetCurrentDirectory() const;
        void SetCurrentDirectory(const std::string& directory);
        void RefreshFileList();

        // Filter methods
        void SetFileFilters(const std::vector<FileFilter>& filters);
        void AddFileFilter(const FileFilter& filter);
        void AddFileFilter(const std::string& description, const std::vector<std::string>& extensions);
        void AddFileFilter(const std::string& description, const std::string& extension);
        int GetSelectedFilterIndex() const;
        void SetSelectedFilterIndex(int index);
        const std::vector<FileFilter>& GetFileFilters() const;

        // Options
        void SetShowHiddenFiles(bool show);
        bool GetShowHiddenFiles() const;
        void SetDefaultFileName(const std::string& fileName);
        std::string GetDefaultFileName() const;

        // Path helpers
        std::string GetSelectedFilePath() const;
        std::vector<std::string> GetSelectedFilePaths() const;

        // Rendering override
        void RenderCustomContent(IRenderContext* ctx) override;

        // Event handling override
        bool OnEvent(const UCEvent& event) override;

    protected:
        void SetupFileInterface();
        void PopulateFileList();
        void OnFileSelected(const std::string& filename);
        void OnDirectoryChanged(const std::string& directory);

        // Layout calculations
        void CalculateFileDialogLayout();
        Rect2Di GetPathBarBounds() const;
        Rect2Di GetFileListBounds() const;
        Rect2Di GetFileNameInputBounds() const;
        Rect2Di GetFilterSelectorBounds() const;

        // Rendering helpers
        void RenderPathBar(IRenderContext* ctx);
        void RenderFileList(IRenderContext* ctx);
        void RenderFileItem(IRenderContext* ctx, const std::string& name, int index, int y, bool isDirectory);
        void RenderScrollbar(IRenderContext* ctx);
        void RenderFileNameInput(IRenderContext* ctx);
        void RenderFilterSelector(IRenderContext* ctx);

        // Event handlers
        void HandleFileListClick(const UCEvent& event);
        void HandleFileListDoubleClick(const UCEvent& event);
        void HandleKeyDown(const UCEvent& event);
        void HandleTextInput(const UCEvent& event);
        void HandleMouseWheel(const UCEvent& event);
        void HandleFilterDropdownClick();
        void HandleOkButton();
        void HandleCancelButton();

        // Navigation helpers
        void NavigateToDirectory(const std::string& dirName);
        void NavigateToParentDirectory();
        void EnsureItemVisible();
        void UpdateSelection();

        // File helpers
        bool IsFileMatchingFilter(const std::string& fileName) const;
        std::string GetFileExtension(const std::string& fileName) const;
        std::string CombinePath(const std::string& dir, const std::string& file) const;
    };
} // namespace UltraCanvas