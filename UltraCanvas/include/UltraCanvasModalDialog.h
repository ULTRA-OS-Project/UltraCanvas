// include/UltraCanvasModalDialog.h
// Cross-platform modal dialog system with complete type definitions
// Version: 2.3.0
// Last Modified: 2025-01-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTextInput.h"
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

// ===== DIALOG CONFIGURATION =====
    struct DialogConfig {
        // Basic properties
        std::string title = "Dialog";
        std::string message;
        std::string details;
        DialogType type = DialogType::Information;

        // Buttons
        DialogButtons buttons = DialogButtons::OK;
        DialogButton defaultButton = DialogButton::OK;
        DialogButton cancelButton = DialogButton::Cancel;

        // Appearance
        Point2Di size = Point2Di(400, 200);
        DialogPosition position = DialogPosition::Center;
        Point2Di customPosition = Point2Di(0, 0);
        bool resizable = false;
        bool movable = true;

        // Behavior
        bool modal = true;
        bool topMost = true;
        bool showInTaskbar = false;
        bool closeOnEscape = true;
        bool closeOnClickOutside = false;
        float autoCloseTime = 0.0f; // 0 = no auto close

        // Animation
        DialogAnimation showAnimation = DialogAnimation::Fade;
        DialogAnimation hideAnimation = DialogAnimation::Fade;
        float animationDuration = 0.3f;

        // Icon and styling
        std::string iconPath;
        Color backgroundColor = Colors::White;
        Color titleBarColor = Colors::LightGray;
        Color borderColor = Colors::Gray;
        float borderWidth = 1.0f;
        float cornerRadius = 4.0f;

        // Callbacks
        std::function<void(DialogResult)> onResult;
        std::function<void()> onShow;
        std::function<void()> onHide;
        std::function<bool()> onClosing; // Return false to prevent closing

        DialogConfig() = default;
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

        InputDialogConfig() {
            buttons = DialogButtons::OKCancel;
            size = Point2Di(400, 150);
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

        FileDialogConfig() {
            buttons = DialogButtons::OKCancel;
            size = Point2Di(600, 450);
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
    class UltraCanvasModalDialog : public UltraCanvasUIElement {
    protected:
        DialogConfig config;
        DialogResult result;
        bool isVisible;
        bool isAnimating;
        bool isClosing;
        float animationProgress;

        // Child elements
        std::vector<std::shared_ptr<UltraCanvasUIElement>> childElements;
        std::vector<std::shared_ptr<UltraCanvasButton>> buttons;

        // Layout areas
        Rect2Di titleBarRect;
        Rect2Di contentRect;
        Rect2Di buttonAreaRect;
        Rect2Di messageRect;
        Rect2Di detailsRect;
        Rect2Di iconRect;
        Rect2Di overlayRect;
        Rect2Di closeButtonRect;

        // Animation
        std::chrono::steady_clock::time_point animationStartTime;
        Point2Di targetPosition;
        Point2Di startPosition;

        // State
        bool isDragging;
        Point2Di dragOffset;
        Point2Di dragStartPos;
        Point2Di windowStartPos;
        UltraCanvasWindowBase* parentWindow;
        std::shared_ptr<UltraCanvasUIElement> backgroundOverlay;

        // Callbacks
        std::function<void(DialogResult)> onResultCallback;
        std::function<bool(DialogResult)> onClosingCallback;

    public:
        // ===== CONSTRUCTION =====
        UltraCanvasModalDialog(const DialogConfig& dialogConfig = DialogConfig());
        virtual ~UltraCanvasModalDialog();

        // ===== DIALOG OPERATIONS =====
        DialogResult ShowModal(UltraCanvasWindowBase* parent = nullptr);
        void Show(UltraCanvasWindowBase* parent = nullptr);
        void Hide();
        void Close(DialogResult result = DialogResult::Cancel);

        // ===== PROPERTIES =====
        void SetTitle(const std::string& title);
        void SetMessage(const std::string& message);
        void SetDetails(const std::string& details);
        void SetType(DialogType type);
        void SetButtons(DialogButtons buttons);
        void SetDefaultButton(DialogButton button);

        std::string GetTitle() const;
        std::string GetMessage() const;
        std::string GetDetails() const;
        DialogType GetType() const;
        DialogButtons GetButtons() const;
        DialogButton GetDefaultButton() const;

        // ===== STATE QUERIES =====
        bool IsVisible() const;
        bool IsModal() const;
        DialogResult GetResult() const;
        bool IsAnimating() const;

        // ===== POSITIONING =====
        void SetDialogPosition(DialogPosition position);
        void SetCustomPosition(const Point2Di& position);
        void CenterOnParent();
        void CenterOnScreen();

        // ===== ANIMATION =====
        void SetShowAnimation(DialogAnimation animation, float duration = 0.3f);
        void SetHideAnimation(DialogAnimation animation, float duration = 0.3f);
        void Update(float deltaTime);

        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;

        // ===== BUTTON MANAGEMENT =====
        void AddCustomButton(const std::string& text, DialogResult result, std::function<void()> callback = nullptr);
        void RemoveButton(DialogButton button);
        void SetButtonEnabled(DialogButton button, bool enabled);
        void SetButtonVisible(DialogButton button, bool visible);

        // ===== CONTENT MANAGEMENT =====
        void AddElement(std::shared_ptr<UltraCanvasUIElement> element);
        void RemoveElement(std::shared_ptr<UltraCanvasUIElement> element);
        void ClearElements();

    protected:
        // ===== LAYOUT =====
        void CalculateLayout();
        void CalculateTitleBarRect();
        void CalculateContentRect();
        void CalculateButtonAreaRect();
        void CalculateMessageRect();
        void CalculateDetailsRect();
        void CalculateIconRect();
        void PositionButtons();

        // ===== RENDERING HELPERS =====
        void RenderBackground();
        void RenderTitleBar();
        void RenderIcon();
        void RenderMessage();
        void RenderDetails();
        void RenderBorder();
        void RenderOverlay();
        void RenderDialogButtons();
        void RenderChildElements();

        // ===== ANIMATION HELPERS =====
        void StartShowAnimation();
        void StartHideAnimation();
        void UpdateAnimation(float deltaTime);
        float CalculateAnimationAlpha() const;
        Point2Di CalculateAnimationPosition() const;
        float CalculateAnimationScale() const;

        // ===== EVENT HELPERS =====
        void OnButtonClick(DialogButton button);
        void OnTitleBarMouseDown(const Point2Di& position);
        void OnTitleBarMouseMove(const Point2Di& position);
        void OnTitleBarMouseUp();
        void OnOverlayClick();

        // ===== UTILITY =====
        std::string GetButtonText(DialogButton button) const;
        Color GetTypeColor() const;
        std::string GetTypeIcon() const;
        void CreateDefaultButtons();
        void ApplyTypeDefaults();
    };

// ===== DIALOG MANAGER =====
    class UltraCanvasDialogManager {
    private:
        // Static member declarations only - definitions in .cpp
        static std::vector<std::shared_ptr<UltraCanvasModalDialog>> activeDialogs;
        static std::shared_ptr<UltraCanvasModalDialog> currentModal;
        static bool enabled;
        static DialogConfig defaultConfig;
        static InputDialogConfig defaultInputConfig;
        static FileDialogConfig defaultFileConfig;

    public:
        // ===== SIMPLE MESSAGE DIALOGS =====
        static DialogResult ShowMessage(const std::string& message, const std::string& title = "Information",
                                        DialogType type = DialogType::Information, DialogButtons buttons = DialogButtons::OK);

        static DialogResult ShowInformation(const std::string& message, const std::string& title = "Information");
        static DialogResult ShowQuestion(const std::string& message, const std::string& title = "Question");
        static DialogResult ShowWarning(const std::string& message, const std::string& title = "Warning");
        static DialogResult ShowError(const std::string& message, const std::string& title = "Error");

        // ===== CONFIRMATION DIALOGS =====
        static bool ShowConfirmation(const std::string& message, const std::string& title = "Confirm");
        static DialogResult ShowYesNoCancel(const std::string& message, const std::string& title = "Choose");

        // ===== INPUT DIALOGS =====
        static std::string ShowInputDialog(const std::string& prompt, const std::string& title = "Input",
                                           const std::string& defaultValue = "", InputType type = InputType::Text);

        static std::string ShowPasswordDialog(const std::string& prompt, const std::string& title = "Password");
        static std::string ShowMultilineInputDialog(const std::string& prompt, const std::string& title = "Input",
                                                    const std::string& defaultValue = "", int maxLines = 10);

        // ===== FILE DIALOGS =====
        static std::string ShowOpenFileDialog(const std::string& title = "Open File",
                                              const std::vector<FileFilter>& filters = {},
                                              const std::string& initialDir = "");

        static std::string ShowSaveFileDialog(const std::string& title = "Save File",
                                              const std::vector<FileFilter>& filters = {},
                                              const std::string& initialDir = "",
                                              const std::string& defaultName = "");

        static std::vector<std::string> ShowOpenMultipleFilesDialog(const std::string& title = "Open Files",
                                                                    const std::vector<FileFilter>& filters = {},
                                                                    const std::string& initialDir = "");

        static std::string ShowSelectFolderDialog(const std::string& title = "Select Folder",
                                                  const std::string& initialDir = "");

        // ===== CUSTOM DIALOGS =====
        static std::shared_ptr<UltraCanvasModalDialog> CreateCustomDialog(const DialogConfig& config);
        static DialogResult ShowCustomDialog(std::shared_ptr<UltraCanvasModalDialog> dialog, UltraCanvasWindowBase* parent = nullptr);

        // ===== DIALOG MANAGEMENT =====
        static void CloseAllDialogs();
        static void CloseDialog(std::shared_ptr<UltraCanvasModalDialog> dialog);
        static std::shared_ptr<UltraCanvasModalDialog> GetCurrentModal();
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
        static void SetEnabled(bool enabled);
        static bool IsEnabled();

        // ===== UPDATE AND RENDERING =====
        static void Update(float deltaTime);
        static void Render();

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
        static std::shared_ptr<UltraCanvasModalDialog> CreateInputDialog(const InputDialogConfig& config);
        static std::shared_ptr<UltraCanvasModalDialog> CreateFileDialog(const FileDialogConfig& config);
    };

// ===== INPUT DIALOG CLASS =====
    class UltraCanvasInputDialog : public UltraCanvasModalDialog {
    private:
        InputDialogConfig inputConfig;
        std::shared_ptr<UltraCanvasTextInput> textInput;
        std::string inputValue;
        bool isValid;

    public:
        UltraCanvasInputDialog(const InputDialogConfig& config);
        virtual ~UltraCanvasInputDialog();

        // Input-specific methods
        std::string GetInputValue() const;
        void SetInputValue(const std::string& value);
        bool IsInputValid() const;
        void ValidateInput();

        // Override event handling for input validation
        bool OnEvent(const UCEvent& event) override;

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
        std::function<void()> onCancelled;
        std::function<void(const std::string&)> onDirectoryChanged;

        UltraCanvasFileDialog(const FileDialogConfig& config);
        virtual ~UltraCanvasFileDialog();

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
        void Render(IRenderContext* ctx) override;

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

// ===== CONVENIENCE FUNCTIONS =====

// Quick message dialogs
    inline DialogResult ShowInfo(const std::string& message, const std::string& title = "Information") {
        return UltraCanvasDialogManager::ShowInformation(message, title);
    }

    inline DialogResult ShowWarning(const std::string& message, const std::string& title = "Warning") {
        return UltraCanvasDialogManager::ShowWarning(message, title);
    }

    inline DialogResult ShowError(const std::string& message, const std::string& title = "Error") {
        return UltraCanvasDialogManager::ShowError(message, title);
    }

    inline bool ShowConfirm(const std::string& message, const std::string& title = "Confirm") {
        return UltraCanvasDialogManager::ShowConfirmation(message, title);
    }

// Quick input dialogs
    inline std::string ShowInput(const std::string& prompt, const std::string& title = "Input", const std::string& defaultValue = "") {
        return UltraCanvasDialogManager::ShowInputDialog(prompt, title, defaultValue);
    }

    inline std::string ShowPassword(const std::string& prompt, const std::string& title = "Password") {
        return UltraCanvasDialogManager::ShowPasswordDialog(prompt, title);
    }

// Quick file dialogs
    inline std::string OpenFile(const std::vector<FileFilter>& filters = {}, const std::string& title = "Open File") {
        return UltraCanvasDialogManager::ShowOpenFileDialog(title, filters);
    }

    inline std::string SaveFile(const std::vector<FileFilter>& filters = {}, const std::string& title = "Save File", const std::string& defaultName = "") {
        return UltraCanvasDialogManager::ShowSaveFileDialog(title, filters, "", defaultName);
    }

    inline std::string SelectFolder(const std::string& title = "Select Folder") {
        return UltraCanvasDialogManager::ShowSelectFolderDialog(title);
    }

    inline std::vector<std::string> OpenMultipleFiles(const std::vector<FileFilter>& filters = {}, const std::string& title = "Open Files") {
        return UltraCanvasDialogManager::ShowOpenMultipleFilesDialog(title, filters);
    }

// ===== FACTORY FUNCTIONS FOR FILE FILTERS =====
    inline FileFilter CreateAllFilesFilter() {
        return FileFilter("All Files", "*");
    }

    inline FileFilter CreateTextFilesFilter() {
        return FileFilter("Text Files", {"txt", "log", "md", "csv"});
    }

    inline FileFilter CreateImageFilesFilter() {
        return FileFilter("Image Files", {"png", "jpg", "jpeg", "gif", "bmp", "svg", "webp"});
    }

    inline FileFilter CreateDocumentFilesFilter() {
        return FileFilter("Document Files", {"pdf", "doc", "docx", "odt", "rtf"});
    }

    inline FileFilter CreateAudioFilesFilter() {
        return FileFilter("Audio Files", {"mp3", "wav", "flac", "ogg", "aac", "m4a"});
    }

    inline FileFilter CreateVideoFilesFilter() {
        return FileFilter("Video Files", {"mp4", "avi", "mkv", "mov", "webm", "wmv"});
    }

    inline FileFilter CreateCodeFilesFilter() {
        return FileFilter("Code Files", {"cpp", "h", "hpp", "c", "py", "js", "ts", "java", "rs"});
    }

    inline FileFilter CreateArchiveFilesFilter() {
        return FileFilter("Archive Files", {"zip", "tar", "gz", "7z", "rar"});
    }

} // namespace UltraCanvas