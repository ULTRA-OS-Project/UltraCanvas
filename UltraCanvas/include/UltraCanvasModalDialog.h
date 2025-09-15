// include/UltraCanvasModalDialog.h
// Cross-platform modal dialog system with complete type definitions
// Version: 2.1.0
// Last Modified: 2025-07-08
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

namespace UltraCanvas {

// Forward declarations
    class UltraCanvasWindow;

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
        NoResult,
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
        MultilineText
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
        Point2D size = Point2D(400, 200);
        DialogPosition position = DialogPosition::Center;
        Point2D customPosition = Point2D(0, 0);
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
            size = Point2D(400, 150);
        }
    };

// ===== FILE DIALOG CONFIGURATION =====
    struct FileDialogConfig : public DialogConfig {
        enum class FileDialogType {
            Open,
            Save,
            OpenMultiple,
            SelectFolder
        };

        FileDialogType dialogType = FileDialogType::Open;
        std::string initialDirectory;
        std::string defaultFileName;
        std::string fileFilter; // "Text files (*.txt)|*.txt|All files (*.*)|*.*"
        std::string defaultExtension;
        bool allowMultipleSelection = false;
        bool validateNames = true;
        bool addToRecent = true;

        FileDialogConfig() {
            buttons = DialogButtons::OKCancel;
            size = Point2D(600, 400);
            resizable = true;
        }
    };

// ===== MODAL DIALOG CLASS =====
    class UltraCanvasModalDialog : public UltraCanvasElement {
    private:
        DialogConfig config;
        DialogResult result;
        bool isVisible;
        bool isAnimating;
        float animationProgress;

        // Child elements
        std::vector<std::shared_ptr<UltraCanvasElement>> childElements;
        std::vector<std::shared_ptr<UltraCanvasButton>> buttons;

        // Layout areas
        Rect2D titleBarRect;
        Rect2D contentRect;
        Rect2D buttonAreaRect;
        Rect2D messageRect;
        Rect2D detailsRect;
        Rect2D iconRect;

        // Animation
        std::chrono::steady_clock::time_point animationStartTime;
        Point2D targetPosition;
        Point2D startPosition;

        // State
        bool isDragging;
        Point2D dragOffset;
        UltraCanvasWindow* parentWindow;
        std::shared_ptr<UltraCanvasElement> backgroundOverlay;

    public:
        // ===== CONSTRUCTION =====
        UltraCanvasModalDialog(const DialogConfig& config = DialogConfig());
        virtual ~UltraCanvasModalDialog();

        // ===== DIALOG OPERATIONS =====
        DialogResult ShowModal(UltraCanvasWindow* parent = nullptr);
        void Show(UltraCanvasWindow* parent = nullptr);
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
        void SetPosition(DialogPosition position);
        void SetCustomPosition(const Point2D& position);
        void CenterOnParent();
        void CenterOnScreen();

        // ===== ANIMATION =====
        void SetShowAnimation(DialogAnimation animation, float duration = 0.3f);
        void SetHideAnimation(DialogAnimation animation, float duration = 0.3f);
        void Update(float deltaTime);

        // ===== RENDERING =====
        void Render() override;

        // ===== EVENT HANDLING =====
        bool HandleEvent(const UCEvent& event) override;

        // ===== BUTTON MANAGEMENT =====
        void AddCustomButton(const std::string& text, DialogResult result, std::function<void()> callback = nullptr);
        void RemoveButton(DialogButton button);
        void SetButtonEnabled(DialogButton button, bool enabled);
        void SetButtonVisible(DialogButton button, bool visible);

        // ===== CONTENT MANAGEMENT =====
        void AddElement(std::shared_ptr<UltraCanvasElement> element);
        void RemoveElement(std::shared_ptr<UltraCanvasElement> element);
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

        // ===== ANIMATION HELPERS =====
        void StartShowAnimation();
        void StartHideAnimation();
        void UpdateAnimation(float deltaTime);
        float CalculateAnimationAlpha() const;
        Point2D CalculateAnimationPosition() const;
        float CalculateAnimationScale() const;

        // ===== EVENT HELPERS =====
        void OnButtonClick(DialogButton button);
        void OnTitleBarMouseDown(const Point2D& position);
        void OnTitleBarMouseMove(const Point2D& position);
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
                                              const std::string& filter = "", const std::string& initialDir = "");

        static std::string ShowSaveFileDialog(const std::string& title = "Save File",
                                              const std::string& filter = "", const std::string& initialDir = "",
                                              const std::string& defaultName = "");

        static std::vector<std::string> ShowOpenMultipleFilesDialog(const std::string& title = "Open Files",
                                                                    const std::string& filter = "", const std::string& initialDir = "");

        static std::string ShowSelectFolderDialog(const std::string& title = "Select Folder",
                                                  const std::string& initialDir = "");

        // ===== CUSTOM DIALOGS =====
        static std::shared_ptr<UltraCanvasModalDialog> CreateCustomDialog(const DialogConfig& config);
        static DialogResult ShowCustomDialog(std::shared_ptr<UltraCanvasModalDialog> dialog, UltraCanvasWindow* parent = nullptr);

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
        bool HandleEvent(const UCEvent& event) override;

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

    public:
        UltraCanvasFileDialog(const FileDialogConfig& config);
        virtual ~UltraCanvasFileDialog();

        // File-specific methods
        std::vector<std::string> GetSelectedFiles() const;
        std::string GetSelectedFile() const;
        std::string GetCurrentDirectory() const;
        void SetCurrentDirectory(const std::string& directory);
        void RefreshFileList();

    protected:
        void SetupFileInterface();
        void PopulateFileList();
        void OnFileSelected(const std::string& filename);
        void OnDirectoryChanged(const std::string& directory);
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
    inline std::string OpenFile(const std::string& filter = "", const std::string& title = "Open File") {
        return UltraCanvasDialogManager::ShowOpenFileDialog(title, filter);
    }

    inline std::string SaveFile(const std::string& filter = "", const std::string& title = "Save File", const std::string& defaultName = "") {
        return UltraCanvasDialogManager::ShowSaveFileDialog(title, filter, "", defaultName);
    }

    inline std::string SelectFolder(const std::string& title = "Select Folder") {
        return UltraCanvasDialogManager::ShowSelectFolderDialog(title);
    }

} // namespace UltraCanvas