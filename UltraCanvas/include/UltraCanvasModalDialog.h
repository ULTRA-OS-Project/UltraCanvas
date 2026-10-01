// include/UltraCanvasModalDialog.h
// Cross-platform modal dialog system - Window-based implementation with layout managers
// Supports switching between native OS dialogs and internal UltraCanvas dialogs
// Version: 3.6.0
// Last Modified: 2026-08-23
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <set>

namespace UltraCanvas {

// Forward declarations
    class UltraCanvasWindowBase;

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
        // The footer buttons' label size; 0 keeps UltraCanvasButton's own
        // default. An application that runs its UI at one size (the Filer at
        // 9) sets all three so a dialog reads like the window under it.
        float buttonFontSize = 0.0f;

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

        // ===== SEVERITY ICON =====
        // The coloured square with the type letter (i / ! / X / ?) on the left
        // of the message. Turn it off for a dialog whose content carries its own
        // graphic — a progress ring, a chart, a preview — so that content is
        // centred in the full width of the window instead of being pushed to the
        // right of a badge that adds nothing.
        bool showIcon = true;

        // Dialog behavior
        bool closeOnEscape = true;
//        bool closeOnClickOutside = false;
        float autoCloseTime = 0.0f; // 0 = no auto close

        // ===== KEYBOARD BEHAVIOUR =====
        // Return/Enter activates defaultButton when the focused element does not
        // consume the key itself (a focused button, a multiline text field).
        bool activateDefaultOnEnter = true;
        // Give every button a mnemonic letter, drawn underlined in its label.
        // Alt+letter always activates it; a bare letter does too, but only while
        // the dialog holds no editable text field that would swallow it.
        bool useButtonMnemonics = true;
        // Discard keys that were already held down when the dialog opened, and
        // forget the keyboard state again when it closes. Without this a key
        // still down from the action that opened the dialog dismisses it
        // immediately, and the key that dismissed it leaks to the parent window.
        bool clearPendingInput = true;

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

        FileDialogConfig();

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
        void SetFiltersFromString(const std::string& filterString);
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
        // The message (and optional details) are rendered by a read-only,
        // word-wrapping Markdown text area. It supplies its own vertical
        // scrollbar when the text is taller than the (capped) dialog.
        std::shared_ptr<UltraCanvasTextArea> messageArea;

        // When true, ShowModal() grows/shrinks the dialog height to fit the
        // message text (clamped to the monitor); beyond the cap the message
        // area scrolls. Input/file dialogs disable this and keep a fixed size.
        bool autoSizeHeight = true;

        // ===== FOOTER COMPONENTS =====
        // A footer button plus everything the keyboard path needs to activate
        // it: which result it produces and which letter selects it.
        struct DialogButtonEntry {
            std::shared_ptr<UltraCanvasButton> button;
            DialogButton type = DialogButton::NoneButton;
            DialogResult result = DialogResult::NoResult;
            char mnemonic = 0;   // uppercase ASCII, 0 = no mnemonic
            DialogButtonRole role = DialogButtonRole::Normal;   // custom buttons
        };
        std::vector<DialogButtonEntry> dialogButtons;
        // Elements AddFooterElement() put at the left of the button bar (an
        // "Apply to all" checkbox), and the spacer that pushes the buttons to
        // the right once there is one.
        std::vector<std::shared_ptr<UltraCanvasUIElement>> footerElements;
        std::shared_ptr<UltraCanvasContainer> footerSpacer;

        // ===== KEYBOARD STATE =====
        // Keys that were already held when the dialog was shown. Their KeyDown /
        // KeyUp / TextInput events are dropped until the key is released, so a
        // leftover keystroke cannot act on a dialog that was not on screen when
        // it was pressed. Empty once the keyboard is fully "armed".
        std::vector<UCKeys> pendingHeldKeys;
        // Identifier of the window event filter that implements the keyboard
        // handling. Set on the first ShowModal() and kept for the window's
        // lifetime; empty means no filter has been installed yet.
        std::string keyboardFilterId;

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
        // Show or hide the severity icon. Hiding it gives the message column
        // (and anything AddDialogElement() put in it) the whole content width.
        void SetIconVisible(bool visible);
        bool IsIconVisible() const;
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
        // The same with a role: which button Return activates and wears the
        // accent colour (Default), which is drawn red (Destructive), which
        // Escape maps to (Cancel). Buttons are laid out in the order they are
        // added and each is as wide as its label needs. `callback` runs before
        // the dialog closes with `result`.
        void AddCustomButton(const std::string& text, DialogResult result,
                             DialogButtonRole role,
                             std::function<void()> callback = nullptr);
        // Puts `element` at the left end of the button bar; the buttons move
        // to the right end. For the scope choice of a question that is asked
        // once per entry - "Apply to all 7 remaining conflicts" - which
        // belongs beside the answers, not among the facts above them. Give
        // the element an explicit size: the bar is laid out before the
        // dialog has a render context to measure text with.
        void AddFooterElement(std::shared_ptr<UltraCanvasUIElement> element);
        void SetButtonDisabled(DialogButton button, bool disabled);
        void SetButtonVisible(DialogButton button, bool visible);

        // ===== KEYBOARD =====
        // Override the letter that activates a button. The letter must occur in
        // the button's label; returns false (leaving the assignment untouched)
        // when it does not, or when no such button exists.
        bool SetButtonMnemonic(DialogButton button, char letter);
        // Uppercase mnemonic of a button, or 0 when it has none.
        char GetButtonMnemonic(DialogButton button) const;
        // Activate a button from code exactly as the keyboard would, by running
        // its own click handler. Returns false without doing anything for a
        // button that is absent, hidden or disabled.
        bool ActivateButton(DialogButton button);

        // ===== CONTENT MANAGEMENT =====
        // Add custom UI elements to the dialog's content area
        void AddDialogElement(std::shared_ptr<UltraCanvasUIElement> element);
        void RemoveDialogElement(std::shared_ptr<UltraCanvasUIElement> element);
        void ClearDialogElements();

        void PerformClose() override;

        // ===== RENDERING OVERRIDE =====
        // NOTE: With layout-based architecture, RenderCustomContent delegates
        // to child components which render themselves
        void RenderCustomContent(IRenderContext* ctx, const Rect2Di& dirtyRect) override;

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
        // Width follows the label (never below style.buttonWidth), so a longer
        // caption is not ellipsized away.
        void SizeButtonToLabel(const std::shared_ptr<UltraCanvasButton>& button);
        // style.buttonFontSize on one button, when it is set; after the role
        // style, which carries a font size of its own.
        void ApplyButtonFont(const std::shared_ptr<UltraCanvasButton>& button);
        void WireButtonCallbacks();

        // ===== TYPE-SPECIFIC =====
        void UpdateIconAppearance();
        void UpdateMessageContent();
        Color GetTypeColor() const;
        std::string GetTypeIcon() const;
        void ApplyTypeDefaults();

        // ===== AUTO-SIZING =====
        // Combined Markdown source fed to the message area (message + details).
        std::string ComposeMessageMarkdown() const;
        // Grow/shrink the window height so the message fits, clamped to the
        // monitor, after widening it when the footer row (the checkbox and
        // the buttons) needs more than the configured width. Called from
        // ShowModal() before the dialog is positioned.
        void AutoSizeToContent();
        // The width the footer's row asks for at the current layout: its
        // padding, every visible element and button, and the gaps between.
        float FooterContentWidth() const;

        // ===== EVENT HELPERS =====
        void OnDialogButtonClick(DialogButton button);

        // ===== KEYBOARD HANDLING =====
        // Runs as a window event filter installed by ShowModal(), i.e. before
        // the focused element sees the key. Only the two cases that need to
        // pre-empt the focused element live here: dropping keystrokes left over
        // from before the dialog opened, and mnemonics. Return and Escape are
        // handled in OnEvent() instead, so a focused button or a multiline
        // field keeps first claim on them.
        bool HandleDialogKeyEvent(const UCEvent& event);
        void InstallKeyboardHandling();
        // Clears the per-showing keyboard state when the dialog closes. The
        // filter itself stays installed on purpose: a mnemonic closes the dialog
        // from inside the filter callback, and uninstalling there would destroy
        // the std::function that is still running. It belongs to the window and
        // dies with it, and re-showing the dialog reuses it.
        void ResetKeyboardState();
        // Drop events belonging to a key that was already down when the dialog
        // opened; returns true when the event must not be delivered.
        bool ShouldSwallowStaleInput(const UCEvent& event);
        // Give each button a distinct underlined letter, preferring the first
        // character of the label and falling back to later ones on collision.
        void AssignButtonMnemonics();
        // Entry whose mnemonic matches `letter`, or nullptr.
        DialogButtonEntry* FindButtonByMnemonic(char letter);
        DialogButtonEntry* FindButtonEntry(DialogButton button);
        const DialogButtonEntry* FindButtonEntry(DialogButton button) const;
        // The button Return activates: the configured default, else the first
        // affirmative button the dialog carries (OK, Yes, Retry, Apply, Close),
        // else its first usable button.
        DialogButtonEntry* FindDefaultButtonEntry();
        // Run a button's own click handler, so every path — mouse, Return,
        // mnemonic — produces exactly the same behaviour. Falls back to closing
        // with the button's result for a button that has no handler.
        void ActivateButtonEntry(DialogButtonEntry& entry);
        // The button Escape maps to: the configured cancelButton when the dialog
        // has it, else the first of Cancel / No / Close / Abort / Ignore that it
        // does have.
        DialogButtonEntry* FindCancelButtonEntry();
        // True while the dialog holds a focusable, editable text field — bare
        // letters are then typing, not mnemonics. Dialogs that render their own
        // text entry (the file dialog's file-name field) override this.
        virtual bool HasEditableTextField() const;
        // Focus the element the user is most likely to act on first: the input
        // field of an input dialog, otherwise the default button.
        virtual void FocusInitialElement();

        // ===== UTILITY =====
        std::string GetButtonText(DialogButton button) const;
        // Result a button produces when activated.
        static DialogResult ButtonToResult(DialogButton button);
    };

// ===== DIALOG MANAGER =====
    class UltraCanvasInputDialog;
    class UltraCanvasFileDialog;
    class UltraCanvasFilerWidget;   // UltraCanvasFilerWidget.h includes this header
    class UltraCanvasSegmentedControl;
    struct FilerEntry;
    class UltraCanvasDialogManager {
        friend class UltraCanvasModalDialog;
    private:
        // Static member declarations only - definitions in .cpp
        static std::vector<std::shared_ptr<UltraCanvasModalDialog>> activeDialogs;
        static bool enabled;
        static bool useNativeDialogs;  // When true, use native OS dialogs instead of internal
        static DialogConfig defaultConfig;
        static InputDialogConfig defaultInputConfig;
        static FileDialogConfig defaultFileConfig;

    public:
        // ===== ASYNC CALLBACK-BASED DIALOGS (RECOMMENDED) =====
        // These methods show dialogs non-blocking and deliver results via callbacks.

        static void ShowMessage(const std::string& message, const std::string& title,
                                DialogType type, DialogButtons buttons,
                                std::function<void(DialogResult)> onResult = nullptr,
                                UltraCanvasWindowBase* parent = nullptr);

        static void ShowInformation(const std::string& message, const std::string& title,
                                    std::function<void(DialogResult)> onResult = nullptr,
                                    UltraCanvasWindowBase* parent = nullptr);

        static void ShowQuestion(const std::string& message, const std::string& title,
                                 std::function<void(DialogResult)> onResult,
                                 UltraCanvasWindowBase* parent = nullptr);

        static void ShowWarning(const std::string& message, const std::string& title,
                                std::function<void(DialogResult)> onResult = nullptr,
                                UltraCanvasWindowBase* parent = nullptr);

        static void ShowError(const std::string& message, const std::string& title,
                              std::function<void(DialogResult)> onResult = nullptr,
                              UltraCanvasWindowBase* parent = nullptr);

        static void ShowConfirmation(const std::string& message, const std::string& title,
                                     std::function<void(bool confirmed)> onResult,
                                     UltraCanvasWindowBase* parent = nullptr);

        static void ShowInputDialog(const std::string& prompt, const std::string& title,
                                    const std::string& defaultValue, InputType type,
                                    std::function<void(DialogResult, const std::string&)> onResult,
                                    UltraCanvasWindowBase* parent = nullptr);

        // File-selection dialogs (Open/Save/SelectFolder/OpenMultiple) live on
        // UltraCanvasFileLoader — see include/UltraCanvasFileLoader.h.

        // ===== CUSTOM DIALOGS =====
        static std::shared_ptr<UltraCanvasModalDialog> CreateDialog(const DialogConfig& config);
        // The framework's own file browser (open / save / select folder), for
        // a caller that wants it whatever the native-dialogs setting says.
        static std::shared_ptr<UltraCanvasFileDialog> CreateFileDialog(const FileDialogConfig& config);
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
        // UltraCanvasFileLoader's file dialogs (open, open multiple, save,
        // select folder) follow the same setting: native when it is on, the
        // UltraCanvasFileDialog below when it is off.
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
        static std::shared_ptr<UltraCanvasModalDialog> CreateMessageDialog(const std::string& message, const std::string& title,
                                                                           DialogType type, DialogButtons buttons);
        static std::shared_ptr<UltraCanvasInputDialog> CreateInputDialog(const InputDialogConfig& config);
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
        // The field is what the user came to type into, so it takes the focus
        // instead of the default button.
        void FocusInitialElement() override;
    };

// ===== FILE DIALOG CLASS =====
    // The framework's own open / save / select-folder dialog, used whenever
    // native dialogs are off (UltraCanvasFileLoader's file dialogs, Cloud
    // pickers). It is assembled from elements: an editable path field with an
    // "up" button, a folder tree (the user's places and every mounted drive,
    // loaded as it is expanded) beside the listing of the current folder, and
    // the file-name field and file-type dropdown below them.
    class UltraCanvasFileDialog : public UltraCanvasModalDialog {
    private:
        FileDialogConfig fileConfig;
        std::vector<std::string> selectedFiles;
        std::string currentDirectory;

        bool showHiddenFiles = false;

        // ===== ELEMENTS =====
        std::shared_ptr<UltraCanvasTextInput> pathInput;
        std::shared_ptr<UltraCanvasButton> upButton;
        std::shared_ptr<UltraCanvasTreeView> folderTree;
        // The listing of the current folder: the framework's file display.
        std::shared_ptr<UltraCanvasFilerWidget> filerView;
        // Details / list / icon sizes for the listing.
        std::shared_ptr<UltraCanvasSegmentedControl> viewSelector;
        int viewIndex = 0;   // the view button chosen (remembered across dialogs)
        std::shared_ptr<UltraCanvasTextInput> fileNameInput;
        std::shared_ptr<UltraCanvasDropdown> filterDropdown;

        // Folder-tree nodes whose sub-folders have been read.
        std::set<std::string> loadedTreeNodes;
        // The drive roots shown in the tree, used to find the row of a folder.
        std::vector<std::string> treeDriveRoots;
        // Set while the dialog itself selects a tree row, so that selection is
        // not taken for the user navigating there.
        bool syncingTree = false;
        // The name the dialog put into the name field for the listing's
        // selection; while the field still holds it, OK takes the selection.
        std::string autoFileName;
        // A file of the listing was activated (double click, Enter) and is
        // accepted on the next loop pass, outside the widget's own handler;
        // OK in between must not accept a second time.
        bool activationPending = false;
        // Set while the dialog itself points the listing at a folder, so the
        // listing's path notification is not taken for the user's navigation.
        bool settingListingPath = false;
        // The tree row of the current folder is to be scrolled into view at
        // the next layout: before the first one the tree has no height.
        bool revealTreeSelectionPending = false;

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

        void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override;
        // Remembers the view and the window size for the next file dialog.
        void PerformClose() override;

    protected:
        // The name field takes the focus (the listing for a folder picker),
        // so typing a name works at once.
        void FocusInitialElement() override;

        // ===== CONSTRUCTION =====
        void BuildFileInterface();
        void RebuildFilterDropdown();

        // ===== FOLDER TREE =====
        void PopulateFolderTree();
        void AddFolderTreeNode(const std::string& parentId, const std::string& nodeId,
                               const std::string& label, const std::string& iconFile,
                               bool probeSubFolders);
        void LoadFolderTreeChildren(TreeNode* node);
        // Selects the tree row of the current folder, opening its ancestors.
        void SyncFolderTree();

        // ===== LISTING =====
        void OnListingSelectionChanged(const std::vector<FilerEntry>& selected);
        // The listing entered a folder of its own accord (double click, Enter).
        void OnListingPathChanged(const std::string& path);
        // Applies the file-type filter (and folders-only for a folder
        // picker) to the listing.
        void ApplyListingFilter();

        // ===== NAVIGATION =====
        // Makes `directory` the current folder. syncTree: select its row in
        // the folder tree (false when the tree is where the user chose it).
        bool GoToDirectory(const std::string& directory, bool syncTree);
        void NavigateToDirectory(const std::string& dirName);
        void NavigateToParentDirectory();
        void HandleOkButton();
        // Closes the dialog with `files` (names in the current folder, or
        // absolute paths) as the result.
        void Accept(const std::vector<std::string>& files);

        // File helpers
        bool IsFileMatchingFilter(const std::string& fileName) const;
        std::string GetFileExtension(const std::string& fileName) const;
        std::string CombinePath(const std::string& dir, const std::string& file) const;
    };
} // namespace UltraCanvas