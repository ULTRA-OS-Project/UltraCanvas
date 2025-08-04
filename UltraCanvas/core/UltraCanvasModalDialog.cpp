// UltraCanvasModalDialog.cpp
// Implementation of cross-platform modal dialog system
// Version: 2.1.1
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasWindow.h"
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>

namespace UltraCanvas {

// ===== STATIC MEMBER DEFINITIONS =====
    std::vector<std::shared_ptr<UltraCanvasModalDialog>> UltraCanvasDialogManager::activeDialogs;
    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::currentModal = nullptr;
    bool UltraCanvasDialogManager::enabled = true;
    DialogConfig UltraCanvasDialogManager::defaultConfig;
    InputDialogConfig UltraCanvasDialogManager::defaultInputConfig;
    FileDialogConfig UltraCanvasDialogManager::defaultFileConfig;

// ===== MODAL DIALOG IMPLEMENTATION =====

    UltraCanvasModalDialog::UltraCanvasModalDialog(const std::string& identifier, long id, long x, long y, long w, long h)
            : UltraCanvasElement(identifier, id, x, y, w, h), properties(identifier, id, x, y, w, h),
              result(DialogResult::None), isVisible(false), isClosing(false), isDragging(false) {

        // Set default configuration
        config.title = "Dialog";
        config.message = "Dialog message";
        config.type = DialogType::Information;
        config.buttons = DialogButtons::OK;
        config.defaultButton = DialogButton::OK;
        config.cancelButton = DialogButton::Cancel;
        config.width = w;
        config.height = h;

        SyncLegacyProperties();
    }

    UltraCanvasModalDialog::~UltraCanvasModalDialog() {
        if (isVisible) {
            Close(DialogResult::Cancel);
        }
    }

    void UltraCanvasModalDialog::SetConfig(const DialogConfig& dialogConfig) {
        config = dialogConfig;
        ApplyTypeDefaults();
        CreateDefaultButtons();
        CalculateLayout();
    }

    DialogConfig UltraCanvasModalDialog::GetConfig() const {
        return config;
    }

    void UltraCanvasModalDialog::SetMessage(const std::string& message) {
        config.message = message;
        CalculateLayout();
    }

    void UltraCanvasModalDialog::SetTitle(const std::string& title) {
        config.title = title;
    }

    void UltraCanvasModalDialog::SetType(DialogType type) {
        config.type = type;
        ApplyTypeDefaults();
    }

    void UltraCanvasModalDialog::SetButtons(DialogButtons buttons) {
        config.buttons = buttons;
        CreateDefaultButtons();
        CalculateLayout();
    }

    void UltraCanvasModalDialog::AddCustomElement(std::shared_ptr<UltraCanvasElement> element) {
        if (element) {
            config.customElements.push_back(element);
            CalculateLayout();
        }
    }

    DialogResult UltraCanvasModalDialog::ShowModal(UltraCanvasWindow* parent) {
        if (isVisible) return result;

        // Position dialog
        if (parent && config.centerOnParent) {
            int parentX, parentY, parentW, parentH;
            parent->GetPosition(parentX, parentY);
            parent->GetSize(parentW, parentH);

            int dialogX = parentX + (parentW - config.width) / 2;
            int dialogY = parentY + (parentH - config.height) / 2;
            SetPosition(dialogX, dialogY);
        }

        // Show dialog
        Show();

        // Register with dialog manager
        UltraCanvasDialogManager::RegisterDialog(shared_from_this());

        if (config.modal) {
            // Modal dialog - block until closed
            while (isVisible && !isClosing) {
                // Process events
                std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS

                // Check for timeout
                if (config.autoCloseTimeout > 0) {
                    // Auto-close logic would go here
                }
            }
        }

        return result;
    }

    void UltraCanvasModalDialog::Show() {
        if (isVisible) return;

        isVisible = true;
        isClosing = false;
        result = DialogResult::None;

        CalculateLayout();
        SetVisible(true);

        if (onResult) {
            // Callback will be called when dialog closes
        }
    }

    void UltraCanvasModalDialog::Close(DialogResult dialogResult) {
        if (!isVisible || isClosing) return;

        // Check if closing is allowed
        if (onClosing && !onClosing(dialogResult)) {
            return; // Closing was cancelled
        }

        isClosing = true;
        result = dialogResult;

        // Trigger result callback
        if (onResult) {
            onResult(result);
        }

        // Hide dialog
        SetVisible(false);
        isVisible = false;

        // Unregister from dialog manager
        UltraCanvasDialogManager::UnregisterDialog(shared_from_this());
    }

    bool UltraCanvasModalDialog::IsModal() const {
        return config.modal;
    }

    bool UltraCanvasModalDialog::IsShowing() const {
        return isVisible && !isClosing;
    }

    DialogResult UltraCanvasModalDialog::GetResult() const {
        return result;
    }

    void UltraCanvasModalDialog::SetResult(DialogResult dialogResult) {
        result = dialogResult;
    }

    void UltraCanvasModalDialog::SetResultCallback(std::function<void(DialogResult)> callback) {
        onResult = callback;
    }

    void UltraCanvasModalDialog::SetClosingCallback(std::function<bool(DialogResult)> callback) {
        onClosing = callback;
    }

    bool UltraCanvasModalDialog::HandleEvent(const UCEvent& event) {
        if (!isVisible) return false;

        OnEvent(event);
        return true; // Modal dialogs consume all events
    }

    void UltraCanvasModalDialog::OnEvent(const UCEvent& event) {
        if (!isVisible) return;

        Point2D eventPos(event.x, event.y);

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left) {
                    // Check if clicking on title bar for dragging
                    if (titleBarRect.Contains(eventPos)) {
                        OnTitleBarMouseDown(eventPos);
                    }
                        // Check if clicking outside dialog (overlay)
                    else if (!GetBounds().Contains(eventPos)) {
                        OnOverlayClick();
                    }
                        // Check button clicks
                    else {
                        for (auto& button : buttons) {
                            if (button && button->Contains(event.x, event.y)) {
                                // Find which dialog button this is
                                for (int i = 0; i < buttons.size(); ++i) {
                                    if (buttons[i].get() == button.get()) {
                                        // Determine button type based on index and config
                                        DialogButton dialogButton = DialogButton::OK; // Default
                                        // This would be mapped based on button configuration
                                        OnButtonClick(dialogButton);
                                        return;
                                    }
                                }
                            }
                        }
                    }
                }
                break;

            case UCEventType::MouseMove:
                if (isDragging) {
                    OnTitleBarMouseMove(eventPos);
                }
                break;

            case UCEventType::MouseUp:
                if (event.button == UCMouseButton::Left && isDragging) {
                    OnTitleBarMouseUp();
                }
                break;

            case UCEventType::KeyDown:
                // Handle escape key
                if (event.virtualKey == UCKeys::Escape && config.allowEscapeKey) {
                    Close(DialogResult::Cancel);
                }
                    // Handle enter key for default button
                else if (event.virtualKey == UCKeys::Return) {
                    OnButtonClick(config.defaultButton);
                }
                break;

            default:
                break;
        }

        // Forward events to custom elements
        for (auto& element : config.customElements) {
            if (element && element->IsVisible()) {
                element->OnEvent(event);
            }
        }
    }

    void UltraCanvasModalDialog::Render() {
        if (!isVisible) return;

        ULTRACANVAS_RENDER_SCOPE();

        // Render overlay
        if (config.modal) {
            RenderOverlay();
        }

        // Render dialog
        RenderDialog();
    }

    bool UltraCanvasModalDialog::IsVisible() const {
        return isVisible && UltraCanvasElement::IsVisible();
    }

    void UltraCanvasModalDialog::CalculateLayout() {
        auto bounds = GetBounds();

        // Title bar
        titleBarRect = Rect2D(bounds.x, bounds.y, bounds.width, 30);

        // Icon area
        iconRect = Rect2D(bounds.x + 10, titleBarRect.y + titleBarRect.height + 10, 32, 32);

        // Message area
        float messageX = bounds.x + 10;
        if (config.type != DialogType::Custom) {
            messageX += 42; // Space for icon
        }

        messageRect = Rect2D(messageX, titleBarRect.y + titleBarRect.height + 10,
                             bounds.width - (messageX - bounds.x) - 10, 60);

        // Details area
        detailsRect = Rect2D(messageX, messageRect.y + messageRect.height + 5,
                             messageRect.width, 40);

        // Button area
        buttonAreaRect = Rect2D(bounds.x + 10, bounds.y + bounds.height - 50,
                                bounds.width - 20, 40);

        // Content area for custom elements
        contentRect = Rect2D(bounds.x + 10, detailsRect.y + detailsRect.height + 10,
                             bounds.width - 20,
                             buttonAreaRect.y - (detailsRect.y + detailsRect.height) - 20);

        // Overlay rect (full screen)
        overlayRect = Rect2D(0, 0, 1920, 1080); // Would use actual screen size

        PositionButtons();
        UpdateContentArea();
    }

    void UltraCanvasModalDialog::PositionButtons() {
        if (buttons.empty()) return;

        int buttonWidth = 80;
        int buttonHeight = 30;
        int buttonSpacing = 10;
        int totalWidth = static_cast<int>(buttons.size()) * buttonWidth + (static_cast<int>(buttons.size()) - 1) * buttonSpacing;

        int startX = static_cast<int>(buttonAreaRect.x + buttonAreaRect.width - totalWidth);
        int buttonY = static_cast<int>(buttonAreaRect.y + (buttonAreaRect.height - buttonHeight) / 2);

        for (size_t i = 0; i < buttons.size(); ++i) {
            int buttonX = startX + static_cast<int>(i) * (buttonWidth + buttonSpacing);
            buttons[i]->SetPosition(buttonX, buttonY);
            buttons[i]->SetSize(buttonWidth, buttonHeight);
        }
    }

    void UltraCanvasModalDialog::UpdateContentArea() {
        // Position custom elements within content area
        for (auto& element : config.customElements) {
            if (element) {
                // Simple vertical layout for now
                element->SetPosition(static_cast<long>(contentRect.x), static_cast<long>(contentRect.y));
            }
        }
    }

    void UltraCanvasModalDialog::RenderOverlay() {
        // Semi-transparent overlay
        Color overlayColor(0, 0, 0, 128);
        DrawFilledRect(overlayRect, overlayColor);
    }

    void UltraCanvasModalDialog::RenderDialog() {
        auto bounds = GetBounds();

        // Dialog background
        DrawFilledRect(bounds, config.backgroundColor, Color(128, 128, 128, 255), 1);

        // Render components
        RenderTitleBar();
        RenderIcon();
        RenderMessage();
        RenderDetails();
        RenderButtons();
        RenderCustomContent();
    }

    void UltraCanvasModalDialog::RenderTitleBar() {
        // Title bar background
        Color titleBg(240, 240, 240, 255);
        DrawFilledRect(titleBarRect, titleBg, Color(200, 200, 200, 255), 1);

        // Title text
        SetTextColor(Colors::Black);
        SetFont("Arial", 12.0f, FontWeight::Bold);
        Point2D titlePos(titleBarRect.x + 10, titleBarRect.y + 20);
        DrawText(config.title, titlePos);

        // Close button (X)
        Rect2D closeButtonRect(titleBarRect.x + titleBarRect.width - 25, titleBarRect.y + 5, 20, 20);
        DrawFilledRect(closeButtonRect, Color(220, 220, 220, 255));
        SetTextColor(Colors::Black);
        SetFont("Arial", 10.0f);
        DrawText("×", Point2D(closeButtonRect.x + 6, closeButtonRect.y + 14));
    }

    void UltraCanvasModalDialog::RenderIcon() {
        if (config.type == DialogType::Custom) return;

        // Draw icon background
        Color iconColor = GetTypeColor();
        DrawFilledRect(iconRect, iconColor);

        // Draw icon symbol
        SetTextColor(Colors::White);
        SetFont("Arial", 20.0f, FontWeight::Bold);
        std::string iconText = GetTypeIcon();
        Point2D iconTextPos(iconRect.x + 10, iconRect.y + 22);
        DrawText(iconText, iconTextPos);
    }

    void UltraCanvasModalDialog::RenderMessage() {
        if (config.message.empty()) return;

        SetTextColor(Colors::Black);
        SetFont("Arial", 11.0f);

        // Word wrap message if too long
        std::vector<std::string> lines;
        std::string currentLine;
        std::stringstream ss(config.message);
        std::string word;

        while (ss >> word) {
            if (currentLine.empty()) {
                currentLine = word;
            } else {
                std::string testLine = currentLine + " " + word;
                // Simple width estimation
                if (testLine.length() * 7 < messageRect.width) {
                    currentLine = testLine;
                } else {
                    lines.push_back(currentLine);
                    currentLine = word;
                }
            }
        }
        if (!currentLine.empty()) {
            lines.push_back(currentLine);
        }

        // Render lines
        float lineHeight = 16.0f;
        for (size_t i = 0; i < lines.size() && i < 3; ++i) {
            Point2D linePos(messageRect.x, messageRect.y + (i + 1) * lineHeight);
            DrawText(lines[i], linePos);
        }
    }

    void UltraCanvasModalDialog::RenderDetails() {
        if (config.details.empty()) return;

        SetTextColor(Color(100, 100, 100, 255));
        SetFont("Arial", 9.0f);
        Point2D detailsPos(detailsRect.x, detailsRect.y + 12);
        DrawText(config.details, detailsPos);
    }

    void UltraCanvasModalDialog::RenderButtons() {
        for (auto& button : buttons) {
            if (button && button->IsVisible()) {
                button->Render();
            }
        }
    }

    void UltraCanvasModalDialog::RenderCustomContent() {
        for (auto& element : config.customElements) {
            if (element && element->IsVisible()) {
                element->Render();
            }
        }
    }

    void UltraCanvasModalDialog::OnButtonClick(DialogButton button) {
        DialogResult buttonResult = DialogResult::None;

        switch (button) {
            case DialogButton::OK:     buttonResult = DialogResult::OK; break;
            case DialogButton::Cancel: buttonResult = DialogResult::Cancel; break;
            case DialogButton::Yes:    buttonResult = DialogResult::Yes; break;
            case DialogButton::No:     buttonResult = DialogResult::No; break;
            case DialogButton::Apply:  buttonResult = DialogResult::Apply; break;
            case DialogButton::Close:  buttonResult = DialogResult::Close; break;
            case DialogButton::Help:   buttonResult = DialogResult::Help; break;
            case DialogButton::Retry:  buttonResult = DialogResult::Retry; break;
            case DialogButton::Ignore: buttonResult = DialogResult::Ignore; break;
            case DialogButton::Abort:  buttonResult = DialogResult::Abort; break;
            default: buttonResult = DialogResult::Cancel; break;
        }

        Close(buttonResult);
    }

    void UltraCanvasModalDialog::OnTitleBarMouseDown(const Point2D& position) {
        isDragging = true;
        dragStartPos = position;
        windowStartPos = Point2D(GetX(), GetY());
    }

    void UltraCanvasModalDialog::OnTitleBarMouseMove(const Point2D& position) {
        if (!isDragging) return;

        Point2D delta(position.x - dragStartPos.x, position.y - dragStartPos.y);
        Point2D newPos(windowStartPos.x + delta.x, windowStartPos.y + delta.y);
        SetPosition(static_cast<long>(newPos.x), static_cast<long>(newPos.y));
        CalculateLayout();
    }

    void UltraCanvasModalDialog::OnTitleBarMouseUp() {
        isDragging = false;
    }

    void UltraCanvasModalDialog::OnOverlayClick() {
        if (config.modal && config.allowEscapeKey) {
            Close(DialogResult::Cancel);
        }
    }

    std::string UltraCanvasModalDialog::GetButtonText(DialogButton button) const {
        switch (button) {
            case DialogButton::OK:     return "OK";
            case DialogButton::Cancel: return "Cancel";
            case DialogButton::Yes:    return "Yes";
            case DialogButton::No:     return "No";
            case DialogButton::Apply:  return "Apply";
            case DialogButton::Close:  return "Close";
            case DialogButton::Help:   return "Help";
            case DialogButton::Retry:  return "Retry";
            case DialogButton::Ignore: return "Ignore";
            case DialogButton::Abort:  return "Abort";
            default: return "Button";
        }
    }

    Color UltraCanvasModalDialog::GetTypeColor() const {
        switch (config.type) {
            case DialogType::Information: return Color(52, 144, 220, 255);  // Blue
            case DialogType::Question:    return Color(92, 184, 92, 255);   // Green
            case DialogType::Warning:     return Color(240, 173, 78, 255);  // Orange
            case DialogType::Error:       return Color(217, 83, 79, 255);   // Red
            case DialogType::Custom:
            default:                      return Color(128, 128, 128, 255); // Gray
        }
    }

    std::string UltraCanvasModalDialog::GetTypeIcon() const {
        switch (config.type) {
            case DialogType::Information: return "i";
            case DialogType::Question:    return "?";
            case DialogType::Warning:     return "!";
            case DialogType::Error:       return "✗";
            case DialogType::Custom:
            default:                      return "•";
        }
    }

    void UltraCanvasModalDialog::CreateDefaultButtons() {
        buttons.clear();

        int buttonFlags = static_cast<int>(config.buttons);

        if (buttonFlags & static_cast<int>(DialogButton::OK)) {
            auto button = std::make_shared<UltraCanvasButton>("ok_btn", 1001, 0, 0, 80, 30);
            button->SetText("OK");
            buttons.push_back(button);
        }

        if (buttonFlags & static_cast<int>(DialogButton::Cancel)) {
            auto button = std::make_shared<UltraCanvasButton>("cancel_btn", 1002, 0, 0, 80, 30);
            button->SetText("Cancel");
            buttons.push_back(button);
        }

        if (buttonFlags & static_cast<int>(DialogButton::Yes)) {
            auto button = std::make_shared<UltraCanvasButton>("yes_btn", 1003, 0, 0, 80, 30);
            button->SetText("Yes");
            buttons.push_back(button);
        }

        if (buttonFlags & static_cast<int>(DialogButton::No)) {
            auto button = std::make_shared<UltraCanvasButton>("no_btn", 1004, 0, 0, 80, 30);
            button->SetText("No");
            buttons.push_back(button);
        }

        if (buttonFlags & static_cast<int>(DialogButton::Apply)) {
            auto button = std::make_shared<UltraCanvasButton>("apply_btn", 1005, 0, 0, 80, 30);
            button->SetText("Apply");
            buttons.push_back(button);
        }

        if (buttonFlags & static_cast<int>(DialogButton::Close)) {
            auto button = std::make_shared<UltraCanvasButton>("close_btn", 1006, 0, 0, 80, 30);
            button->SetText("Close");
            buttons.push_back(button);
        }

        if (buttonFlags & static_cast<int>(DialogButton::Help)) {
            auto button = std::make_shared<UltraCanvasButton>("help_btn", 1007, 0, 0, 80, 30);
            button->SetText("Help");
            buttons.push_back(button);
        }

        if (buttonFlags & static_cast<int>(DialogButton::Retry)) {
            auto button = std::make_shared<UltraCanvasButton>("retry_btn", 1008, 0, 0, 80, 30);
            button->SetText("Retry");
            buttons.push_back(button);
        }

        if (buttonFlags & static_cast<int>(DialogButton::Ignore)) {
            auto button = std::make_shared<UltraCanvasButton>("ignore_btn", 1009, 0, 0, 80, 30);
            button->SetText("Ignore");
            buttons.push_back(button);
        }

        if (buttonFlags & static_cast<int>(DialogButton::Abort)) {
            auto button = std::make_shared<UltraCanvasButton>("abort_btn", 1010, 0, 0, 80, 30);
            button->SetText("Abort");
            buttons.push_back(button);
        }
    }

    void UltraCanvasModalDialog::ApplyTypeDefaults() {
        switch (config.type) {
            case DialogType::Information:
                if (config.buttons == DialogButtons::OK) {
                    config.defaultButton = DialogButton::OK;
                    config.cancelButton = DialogButton::OK;
                }
                break;

            case DialogType::Question:
                if (config.buttons == DialogButtons::YesNo) {
                    config.defaultButton = DialogButton::Yes;
                    config.cancelButton = DialogButton::No;
                }
                break;

            case DialogType::Warning:
                if (config.buttons == DialogButtons::OKCancel) {
                    config.defaultButton = DialogButton::OK;
                    config.cancelButton = DialogButton::Cancel;
                }
                break;

            case DialogType::Error:
                if (config.buttons == DialogButtons::OK) {
                    config.defaultButton = DialogButton::OK;
                    config.cancelButton = DialogButton::OK;
                }
                break;

            default:
                break;
        }
    }

// ===== INPUT DIALOG IMPLEMENTATION =====

    UltraCanvasInputDialog::UltraCanvasInputDialog(const InputDialogConfig& config)
            : UltraCanvasModalDialog("input_dialog", 2000, 100, 100, 400, 200),
              inputConfig(config), isValid(true) {

        SetTitle(config.title);
        SetMessage(config.prompt);
        SetButtons(DialogButtons::OKCancel);

        SetupInputField();
    }

    UltraCanvasInputDialog::~UltraCanvasInputDialog() {
    }

    std::string UltraCanvasInputDialog::GetInputValue() const {
        return inputValue;
    }

    void UltraCanvasInputDialog::SetInputValue(const std::string& value) {
        inputValue = value;
        if (textInput) {
            textInput->SetText(value);
        }
        ValidateInput();
    }

    bool UltraCanvasInputDialog::IsInputValid() const {
        return isValid;
    }

    void UltraCanvasInputDialog::ValidateInput() {
        isValid = true;

        // Length validation
        if (inputValue.length() < inputConfig.minLength ||
            inputValue.length() > inputConfig.maxLength) {
            isValid = false;
        }

        // Custom validator
        if (isValid && inputConfig.validator) {
            isValid = inputConfig.validator(inputValue);
        }

        // Enable/disable OK button based on validation
        // This would update the button state
    }

    bool UltraCanvasInputDialog::HandleEvent(const UCEvent& event) {
        // Handle input field events
        if (textInput) {
            textInput->OnEvent(event);
            inputValue = textInput->GetText();
            ValidateInput();
        }

        return UltraCanvasModalDialog::HandleEvent(event);
    }

    void UltraCanvasInputDialog::SetupInputField() {
        textInput = std::make_shared<UltraCanvasTextInput>("input_field", 2001, 0, 0, 300, 25);
        textInput->SetText(inputConfig.defaultValue);
        inputValue = inputConfig.defaultValue;

        // Configure based on input type
        switch (inputConfig.inputType) {
            case InputType::Password:
                textInput->SetPasswordMode(true);
                break;
            case InputType::Number:
                // Would set numeric input mode
                break;
            case InputType::Multiline:
                textInput->SetMultiline(true);
                textInput->SetSize(300, 80);
                break;
            default:
                break;
        }

        AddCustomElement(textInput);
        ValidateInput();
    }

    void UltraCanvasInputDialog::OnInputChanged(const std::string& text) {
        inputValue = text;
        ValidateInput();
    }

    void UltraCanvasInputDialog::OnInputValidation() {
        // Additional validation logic would go here
    }

// ===== FILE DIALOG IMPLEMENTATION =====

    UltraCanvasFileDialog::UltraCanvasFileDialog(const FileDialogConfig& config)
            : UltraCanvasModalDialog("file_dialog", 3000, 100, 100, 600, 400),
              fileConfig(config), currentDirectory(config.initialDirectory) {

        SetTitle(config.title);
        SetButtons(DialogButtons::OKCancel);

        if (currentDirectory.empty()) {
            currentDirectory = "."; // Current directory
        }

        SetupFileList();
    }

    UltraCanvasFileDialog::~UltraCanvasFileDialog() {
    }

    std::vector<std::string> UltraCanvasFileDialog::GetSelectedFiles() const {
        return selectedFiles;
    }

    std::string UltraCanvasFileDialog::GetSelectedFile() const {
        return selectedFiles.empty() ? "" : selectedFiles[0];
    }

    void UltraCanvasFileDialog::SetCurrentDirectory(const std::string& directory) {
        currentDirectory = directory;
        RefreshFileList();
    }

    std::string UltraCanvasFileDialog::GetCurrentDirectory() const {
        return currentDirectory;
    }

    void UltraCanvasFileDialog::SetupFileList() {
        // Create file list component (would be a proper file browser)
        // For now, this is a placeholder
        RefreshFileList();
    }

    void UltraCanvasFileDialog::RefreshFileList() {
        // Platform-specific file system browsing would go here
        // For now, this is a placeholder
        selectedFiles.clear();
    }

    void UltraCanvasFileDialog::OnFileSelected(const std::string& filename) {
        if (fileConfig.multiSelect) {
            selectedFiles.push_back(filename);
        } else {
            selectedFiles.clear();
            selectedFiles.push_back(filename);
        }
    }

    void UltraCanvasFileDialog::OnDirectoryChanged(const std::string& directory) {
        SetCurrentDirectory(directory);
    }

// ===== DIALOG MANAGER IMPLEMENTATION =====

    DialogResult UltraCanvasDialogManager::ShowMessage(const std::string& message, const std::string& title,
                                                       DialogType type, DialogButtons buttons) {
        if (!enabled) return DialogResult::Cancel;

        auto dialog = CreateMessageDialog(message, title, type, buttons);
        return ShowCustomDialog(dialog);
    }

    DialogResult UltraCanvasDialogManager::ShowInformation(const std::string& message, const std::string& title) {
        return ShowMessage(message, title, DialogType::Information, DialogButtons::OK);
    }

    DialogResult UltraCanvasDialogManager::ShowQuestion(const std::string& message, const std::string& title) {
        return ShowMessage(message, title, DialogType::Question, DialogButtons::YesNo);
    }

    DialogResult UltraCanvasDialogManager::ShowWarning(const std::string& message, const std::string& title) {
        return ShowMessage(message, title, DialogType::Warning, DialogButtons::OK);
    }

    DialogResult UltraCanvasDialogManager::ShowError(const std::string& message, const std::string& title) {
        return ShowMessage(message, title, DialogType::Error, DialogButtons::OK);
    }

    bool UltraCanvasDialogManager::ShowConfirmation(const std::string& message, const std::string& title) {
        DialogResult result = ShowMessage(message, title, DialogType::Question, DialogButtons::YesNo);
        return result == DialogResult::Yes;
    }

    DialogResult UltraCanvasDialogManager::ShowYesNoCancel(const std::string& message, const std::string& title) {
        return ShowMessage(message, title, DialogType::Question, DialogButtons::YesNoCancel);
    }

    std::string UltraCanvasDialogManager::ShowInputDialog(const std::string& prompt, const std::string& title,
                                                          const std::string& defaultValue, InputType type) {
        if (!enabled) return "";

        InputDialogConfig config;
        config.title = title;
        config.prompt = prompt;
        config.defaultValue = defaultValue;
        config.inputType = type;

        auto dialog = CreateInputDialog(config);
        DialogResult result = ShowCustomDialog(dialog);

        if (result == DialogResult::OK) {
            auto inputDialog = std::dynamic_pointer_cast<UltraCanvasInputDialog>(dialog);
            return inputDialog ? inputDialog->GetInputValue() : "";
        }

        return "";
    }

    std::string UltraCanvasDialogManager::ShowPasswordDialog(const std::string& prompt, const std::string& title) {
        return ShowInputDialog(prompt, title, "", InputType::Password);
    }

    std::string UltraCanvasDialogManager::ShowMultilineInputDialog(const std::string& prompt, const std::string& title,
                                                                   const std::string& defaultValue, int maxLines) {
        InputDialogConfig config;
        config.title = title;
        config.prompt = prompt;
        config.defaultValue = defaultValue;
        config.inputType = InputType::Multiline;
        config.maxLines = maxLines;

        auto dialog = CreateInputDialog(config);
        DialogResult result = ShowCustomDialog(dialog);

        if (result == DialogResult::OK) {
            auto inputDialog = std::dynamic_pointer_cast<UltraCanvasInputDialog>(dialog);
            return inputDialog ? inputDialog->GetInputValue() : "";
        }

        return "";
    }

    std::string UltraCanvasDialogManager::ShowOpenFileDialog(const std::string& title,
                                                             const std::string& filter, const std::string& initialDir) {
        if (!enabled) return "";

        FileDialogConfig config;
        config.title = title.empty() ? "Open File" : title;
        config.type = FileDialogType::Open;
        config.initialDirectory = initialDir;
        if (!filter.empty()) {
            config.filters.push_back(filter);
        }

        auto dialog = CreateFileDialog(config);
        DialogResult result = ShowCustomDialog(dialog);

        if (result == DialogResult::OK) {
            auto fileDialog = std::dynamic_pointer_cast<UltraCanvasFileDialog>(dialog);
            return fileDialog ? fileDialog->GetSelectedFile() : "";
        }

        return "";
    }

    std::string UltraCanvasDialogManager::ShowSaveFileDialog(const std::string& title,
                                                             const std::string& filter, const std::string& initialDir,
                                                             const std::string& defaultName) {
        if (!enabled) return "";

        FileDialogConfig config;
        config.title = title.empty() ? "Save File" : title;
        config.type = FileDialogType::Save;
        config.initialDirectory = initialDir;
        config.defaultFileName = defaultName;
        if (!filter.empty()) {
            config.filters.push_back(filter);
        }

        auto dialog = CreateFileDialog(config);
        DialogResult result = ShowCustomDialog(dialog);

        if (result == DialogResult::OK) {
            auto fileDialog = std::dynamic_pointer_cast<UltraCanvasFileDialog>(dialog);
            return fileDialog ? fileDialog->GetSelectedFile() : "";
        }

        return "";
    }

    std::vector<std::string> UltraCanvasDialogManager::ShowOpenMultipleFilesDialog(const std::string& title,
                                                                                   const std::string& filter, const std::string& initialDir) {
        if (!enabled) return {};

        FileDialogConfig config;
        config.title = title.empty() ? "Open Files" : title;
        config.type = FileDialogType::OpenMultiple;
        config.multiSelect = true;
        config.initialDirectory = initialDir;
        if (!filter.empty()) {
            config.filters.push_back(filter);
        }

        auto dialog = CreateFileDialog(config);
        DialogResult result = ShowCustomDialog(dialog);

        if (result == DialogResult::OK) {
            auto fileDialog = std::dynamic_pointer_cast<UltraCanvasFileDialog>(dialog);
            return fileDialog ? fileDialog->GetSelectedFiles() : std::vector<std::string>();
        }

        return {};
    }

    std::string UltraCanvasDialogManager::ShowSelectFolderDialog(const std::string& title, const std::string& initialDir) {
        if (!enabled) return "";

        FileDialogConfig config;
        config.title = title.empty() ? "Select Folder" : title;
        config.type = FileDialogType::SelectFolder;
        config.initialDirectory = initialDir;

        auto dialog = CreateFileDialog(config);
        DialogResult result = ShowCustomDialog(dialog);

        if (result == DialogResult::OK) {
            auto fileDialog = std::dynamic_pointer_cast<UltraCanvasFileDialog>(dialog);
            return fileDialog ? fileDialog->GetCurrentDirectory() : "";
        }

        return "";
    }

    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::CreateCustomDialog(const DialogConfig& config) {
        auto dialog = std::make_shared<UltraCanvasModalDialog>("custom_dialog", 4000, 100, 100, config.width, config.height);
        dialog->SetConfig(config);
        return dialog;
    }

    DialogResult UltraCanvasDialogManager::ShowCustomDialog(std::shared_ptr<UltraCanvasModalDialog> dialog, UltraCanvasWindow* parent) {
        if (!enabled || !dialog) return DialogResult::Cancel;

        return dialog->ShowModal(parent);
    }

    void UltraCanvasDialogManager::CloseAllDialogs() {
        for (auto& dialog : activeDialogs) {
            if (dialog) {
                dialog->Close(DialogResult::Cancel);
            }
        }
        activeDialogs.clear();
        currentModal.reset();
    }

    void UltraCanvasDialogManager::CloseDialog(std::shared_ptr<UltraCanvasModalDialog> dialog) {
        if (dialog) {
            dialog->Close(DialogResult::Cancel);
            UnregisterDialog(dialog);
        }
    }

    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::GetCurrentModal() {
        return currentModal;
    }

    std::vector<std::shared_ptr<UltraCanvasModalDialog>> UltraCanvasDialogManager::GetActiveDialogs() {
        return activeDialogs;
    }

    int UltraCanvasDialogManager::GetActiveDialogCount() {
        return static_cast<int>(activeDialogs.size());
    }

    void UltraCanvasDialogManager::SetDefaultConfig(const DialogConfig& config) {
        defaultConfig = config;
    }

    void UltraCanvasDialogManager::SetDefaultInputConfig(const InputDialogConfig& config) {
        defaultInputConfig = config;
    }

    void UltraCanvasDialogManager::SetDefaultFileConfig(const FileDialogConfig& config) {
        defaultFileConfig = config;
    }

    DialogConfig UltraCanvasDialogManager::GetDefaultConfig() {
        return defaultConfig;
    }

    InputDialogConfig UltraCanvasDialogManager::GetDefaultInputConfig() {
        return defaultInputConfig;
    }

    FileDialogConfig UltraCanvasDialogManager::GetDefaultFileConfig() {
        return defaultFileConfig;
    }

    void UltraCanvasDialogManager::SetEnabled(bool enable) {
        enabled = enable;
        if (!enabled) {
            CloseAllDialogs();
        }
    }

    bool UltraCanvasDialogManager::IsEnabled() {
        return enabled;
    }

    void UltraCanvasDialogManager::Update(float deltaTime) {
        if (!enabled) return;

        // Update all active dialogs
        for (auto& dialog : activeDialogs) {
            if (dialog) {
                // Dialog update logic would go here
            }
        }

        // Remove closed dialogs
        activeDialogs.erase(
                std::remove_if(activeDialogs.begin(), activeDialogs.end(),
                               [](const std::shared_ptr<UltraCanvasModalDialog>& dialog) {
                                   return !dialog || !dialog->IsShowing();
                               }),
                activeDialogs.end()
        );
    }

    void UltraCanvasDialogManager::Render() {
        if (!enabled) return;

        // Render all active dialogs
        for (auto& dialog : activeDialogs) {
            if (dialog && dialog->IsVisible()) {
                dialog->Render();
            }
        }
    }

    std::string UltraCanvasDialogManager::DialogResultToString(DialogResult result) {
        switch (result) {
            case DialogResult::OK:     return "OK";
            case DialogResult::Cancel: return "Cancel";
            case DialogResult::Yes:    return "Yes";
            case DialogResult::No:     return "No";
            case DialogResult::Apply:  return "Apply";
            case DialogResult::Close:  return "Close";
            case DialogResult::Help:   return "Help";
            case DialogResult::Retry:  return "Retry";
            case DialogResult::Ignore: return "Ignore";
            case DialogResult::Abort:  return "Abort";
            case DialogResult::None:
            default:                   return "NoneFill";
        }
    }

    DialogResult UltraCanvasDialogManager::StringToDialogResult(const std::string& str) {
        if (str == "OK") return DialogResult::OK;
        if (str == "Cancel") return DialogResult::Cancel;
        if (str == "Yes") return DialogResult::Yes;
        if (str == "No") return DialogResult::No;
        if (str == "Apply") return DialogResult::Apply;
        if (str == "Close") return DialogResult::Close;
        if (str == "Help") return DialogResult::Help;
        if (str == "Retry") return DialogResult::Retry;
        if (str == "Ignore") return DialogResult::Ignore;
        if (str == "Abort") return DialogResult::Abort;
        return DialogResult::None;
    }

    std::string UltraCanvasDialogManager::DialogButtonToString(DialogButton button) {
        switch (button) {
            case DialogButton::OK:     return "OK";
            case DialogButton::Cancel: return "Cancel";
            case DialogButton::Yes:    return "Yes";
            case DialogButton::No:     return "No";
            case DialogButton::Apply:  return "Apply";
            case DialogButton::Close:  return "Close";
            case DialogButton::Help:   return "Help";
            case DialogButton::Retry:  return "Retry";
            case DialogButton::Ignore: return "Ignore";
            case DialogButton::Abort:  return "Abort";
            case DialogButton::None:
            default:                   return "NoneFill";
        }
    }

    DialogButton UltraCanvasDialogManager::StringToDialogButton(const std::string& str) {
        if (str == "OK") return DialogButton::OK;
        if (str == "Cancel") return DialogButton::Cancel;
        if (str == "Yes") return DialogButton::Yes;
        if (str == "No") return DialogButton::No;
        if (str == "Apply") return DialogButton::Apply;
        if (str == "Close") return DialogButton::Close;
        if (str == "Help") return DialogButton::Help;
        if (str == "Retry") return DialogButton::Retry;
        if (str == "Ignore") return DialogButton::Ignore;
        if (str == "Abort") return DialogButton::Abort;
        return DialogButton::None;
    }

    void UltraCanvasDialogManager::RegisterDialog(std::shared_ptr<UltraCanvasModalDialog> dialog) {
        if (dialog) {
            activeDialogs.push_back(dialog);
            if (dialog->IsModal()) {
                SetCurrentModal(dialog);
            }
        }
    }

    void UltraCanvasDialogManager::UnregisterDialog(std::shared_ptr<UltraCanvasModalDialog> dialog) {
        auto it = std::find(activeDialogs.begin(), activeDialogs.end(), dialog);
        if (it != activeDialogs.end()) {
            activeDialogs.erase(it);
        }

        if (currentModal == dialog) {
            currentModal.reset();
        }
    }

    void UltraCanvasDialogManager::SetCurrentModal(std::shared_ptr<UltraCanvasModalDialog> dialog) {
        currentModal = dialog;
    }

    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::CreateMessageDialog(const std::string& message, const std::string& title,
                                                                                          DialogType type, DialogButtons buttons) {
        DialogConfig config = defaultConfig;
        config.message = message;
        config.title = title;
        config.type = type;
        config.buttons = buttons;

        return CreateCustomDialog(config);
    }

    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::CreateInputDialog(const InputDialogConfig& config) {
        return std::make_shared<UltraCanvasInputDialog>(config);
    }

    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::CreateFileDialog(const FileDialogConfig& config) {
        return std::make_shared<UltraCanvasFileDialog>(config);
    }

} // namespace UltraCanvas