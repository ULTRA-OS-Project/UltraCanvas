// core/UltraCanvasModalDialog.cpp
// Implementation of cross-platform modal dialog system
// Version: 2.3.0
// Last Modified: 2025-01-17
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

    UltraCanvasModalDialog::UltraCanvasModalDialog(const DialogConfig& dialogConfig)
            : UltraCanvasUIElement("modal_dialog", 0, dialogConfig.customPosition.x,
                                   dialogConfig.customPosition.y,
                                   dialogConfig.size.x, dialogConfig.size.y),
              config(dialogConfig),
              result(DialogResult::NoResult),
              isVisible(false),
              isAnimating(false),
              isClosing(false),
              animationProgress(0.0f),
              isDragging(false),
              parentWindow(nullptr) {

        ApplyTypeDefaults();
        CreateDefaultButtons();
        CalculateLayout();
    }

    UltraCanvasModalDialog::~UltraCanvasModalDialog() {
        if (isVisible) {
            Close(DialogResult::Cancel);
        }
    }

    void UltraCanvasModalDialog::SetMessage(const std::string& message) {
        config.message = message;
        CalculateLayout();
    }

    void UltraCanvasModalDialog::SetTitle(const std::string& title) {
        config.title = title;
    }

    void UltraCanvasModalDialog::SetDetails(const std::string& details) {
        config.details = details;
        CalculateLayout();
    }

    void UltraCanvasModalDialog::SetType(DialogType type) {
        config.type = type;
        ApplyTypeDefaults();
    }

    void UltraCanvasModalDialog::SetButtons(DialogButtons buttonsConfig) {
        config.buttons = buttonsConfig;
        CreateDefaultButtons();
        CalculateLayout();
    }

    void UltraCanvasModalDialog::SetDefaultButton(DialogButton button) {
        config.defaultButton = button;
    }

    std::string UltraCanvasModalDialog::GetTitle() const {
        return config.title;
    }

    std::string UltraCanvasModalDialog::GetMessage() const {
        return config.message;
    }

    std::string UltraCanvasModalDialog::GetDetails() const {
        return config.details;
    }

    DialogType UltraCanvasModalDialog::GetType() const {
        return config.type;
    }

    DialogButtons UltraCanvasModalDialog::GetButtons() const {
        return config.buttons;
    }

    DialogButton UltraCanvasModalDialog::GetDefaultButton() const {
        return config.defaultButton;
    }

    void UltraCanvasModalDialog::AddElement(std::shared_ptr<UltraCanvasUIElement> element) {
        if (element) {
            childElements.push_back(element);
            CalculateLayout();
        }
    }

    void UltraCanvasModalDialog::RemoveElement(std::shared_ptr<UltraCanvasUIElement> element) {
        auto it = std::find(childElements.begin(), childElements.end(), element);
        if (it != childElements.end()) {
            childElements.erase(it);
            CalculateLayout();
        }
    }

    void UltraCanvasModalDialog::ClearElements() {
        childElements.clear();
        CalculateLayout();
    }

    DialogResult UltraCanvasModalDialog::ShowModal(UltraCanvasWindowBase* parent) {
        if (isVisible) return result;

        parentWindow = parent;

        // Position dialog centered on parent if available
        if (parent && config.position == DialogPosition::CenterParent) {
            Point2Di parentPos = parent->GetPosition();
            Size2Di parentSize = parent->GetSize();

            int dialogX = parentPos.x + (parentSize.width - config.size.x) / 2;
            int dialogY = parentPos.y + (parentSize.height - config.size.y) / 2;
            SetPosition(dialogX, dialogY);
        }

        // Show dialog
        Show(parent);

        if (config.modal) {
            // Modal dialog - block until closed
            while (isVisible && !isClosing) {
                std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
            }
        }

        return result;
    }

    void UltraCanvasModalDialog::Show(UltraCanvasWindowBase* parent) {
        if (isVisible) return;

        parentWindow = parent;
        isVisible = true;
        isClosing = false;
        result = DialogResult::NoResult;

        CalculateLayout();
        UltraCanvasUIElement::SetVisible(true);

        if (config.onShow) {
            config.onShow();
        }
    }

    void UltraCanvasModalDialog::Hide() {
        if (!isVisible) return;

        UltraCanvasUIElement::SetVisible(false);
        isVisible = false;

        if (config.onHide) {
            config.onHide();
        }
    }

    void UltraCanvasModalDialog::Close(DialogResult dialogResult) {
        if (!isVisible || isClosing) return;

        if (onClosingCallback && !onClosingCallback(dialogResult)) {
            return;
        }

        isClosing = true;
        result = dialogResult;

        if (onResultCallback) {
            onResultCallback(result);
        }

        if (config.onResult) {
            config.onResult(result);
        }

        Hide();
    }

    bool UltraCanvasModalDialog::IsModal() const {
        return config.modal;
    }

    bool UltraCanvasModalDialog::IsVisible() const {
        return isVisible && UltraCanvasUIElement::IsVisible();
    }

    bool UltraCanvasModalDialog::IsAnimating() const {
        return isAnimating;
    }

    DialogResult UltraCanvasModalDialog::GetResult() const {
        return result;
    }

    void UltraCanvasModalDialog::SetDialogPosition(DialogPosition position) {
        config.position = position;
        CalculateLayout();
    }

    void UltraCanvasModalDialog::SetCustomPosition(const Point2Di& position) {
        config.customPosition = position;
        config.position = DialogPosition::Custom;
        SetPosition(position.x, position.y);
        CalculateLayout();
    }

    void UltraCanvasModalDialog::CenterOnParent() {
        if (parentWindow) {
            Point2Di parentPos = parentWindow->GetPosition();
            Size2Di parentSize = parentWindow->GetSize();

            int dialogX = parentPos.x + (parentSize.width - GetWidth()) / 2;
            int dialogY = parentPos.y + (parentSize.height - GetHeight()) / 2;
            SetPosition(dialogX, dialogY);
            CalculateLayout();
        }
    }

    void UltraCanvasModalDialog::CenterOnScreen() {
        SetPosition(100, 100);
        CalculateLayout();
    }

    void UltraCanvasModalDialog::SetShowAnimation(DialogAnimation animation, float duration) {
        config.showAnimation = animation;
        config.animationDuration = duration;
    }

    void UltraCanvasModalDialog::SetHideAnimation(DialogAnimation animation, float duration) {
        config.hideAnimation = animation;
        config.animationDuration = duration;
    }

    void UltraCanvasModalDialog::Update(float deltaTime) {
        if (isAnimating) {
            UpdateAnimation(deltaTime);
        }
    }

    bool UltraCanvasModalDialog::OnEvent(const UCEvent& event) {
        if (!isVisible) return false;

        Point2Di eventPos(event.x, event.y);

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left) {
                    if (titleBarRect.Contains(eventPos)) {
                        if (closeButtonRect.Contains(eventPos)) {
                            Close(DialogResult::Cancel);
                            return true;
                        }
                        OnTitleBarMouseDown(eventPos);
                        return true;
                    }
                    else if (!GetBounds().Contains(eventPos)) {
                        OnOverlayClick();
                        return true;
                    }
                    else {
                        for (size_t i = 0; i < buttons.size(); ++i) {
                            if (buttons[i] && buttons[i]->Contains(event.x, event.y)) {
                                buttons[i]->OnEvent(event);
                                return true;
                            }
                        }
                    }
                }
                break;

            case UCEventType::MouseMove:
                if (isDragging) {
                    OnTitleBarMouseMove(eventPos);
                    return true;
                }
                break;

            case UCEventType::MouseUp:
                if (event.button == UCMouseButton::Left && isDragging) {
                    OnTitleBarMouseUp();
                    return true;
                }
                break;

            case UCEventType::KeyDown:
                if (event.virtualKey == UCKeys::Escape && config.closeOnEscape) {
                    Close(DialogResult::Cancel);
                    return true;
                }
                else if (event.virtualKey == UCKeys::Return) {
                    OnButtonClick(config.defaultButton);
                    return true;
                }
                break;

            default:
                break;
        }

        for (auto& element : childElements) {
            if (element && element->IsVisible()) {
                if (element->OnEvent(event)) {
                    return true;
                }
            }
        }

        return false;
    }

    void UltraCanvasModalDialog::Render(IRenderContext* ctx) {
        if (!isVisible || !ctx) return;

        ctx->PushState();

        if (config.modal) {
            RenderOverlay();
        }

        RenderBackground();
        RenderTitleBar();
        RenderIcon();
        RenderMessage();
        RenderDetails();
        RenderDialogButtons();
        RenderChildElements();
        RenderBorder();

        ctx->PopState();
    }

    void UltraCanvasModalDialog::CalculateLayout() {
        Rect2Di bounds = GetBounds();

        titleBarRect = Rect2Di(bounds.x, bounds.y, bounds.width, 30);
        closeButtonRect = Rect2Di(bounds.x + bounds.width - 25, bounds.y + 5, 20, 20);
        iconRect = Rect2Di(bounds.x + 10, titleBarRect.y + titleBarRect.height + 10, 32, 32);

        int messageX = bounds.x + 10;
        if (config.type != DialogType::Custom) {
            messageX += 42;
        }

        messageRect = Rect2Di(messageX, titleBarRect.y + titleBarRect.height + 10,
                              bounds.width - (messageX - bounds.x) - 10, 60);

        detailsRect = Rect2Di(messageX, messageRect.y + messageRect.height + 5,
                              messageRect.width, 40);

        buttonAreaRect = Rect2Di(bounds.x + 10, bounds.y + bounds.height - 50,
                                 bounds.width - 20, 40);

        contentRect = Rect2Di(bounds.x + 10, detailsRect.y + detailsRect.height + 10,
                              bounds.width - 20,
                              buttonAreaRect.y - (detailsRect.y + detailsRect.height) - 20);

        overlayRect = Rect2Di(0, 0, 1920, 1080);

        PositionButtons();
    }

    void UltraCanvasModalDialog::PositionButtons() {
        if (buttons.empty()) return;

        int buttonWidth = 80;
        int buttonHeight = 30;
        int buttonSpacing = 10;
        int totalWidth = static_cast<int>(buttons.size()) * buttonWidth +
                         (static_cast<int>(buttons.size()) - 1) * buttonSpacing;

        int startX = buttonAreaRect.x + buttonAreaRect.width - totalWidth;
        int buttonY = buttonAreaRect.y + (buttonAreaRect.height - buttonHeight) / 2;

        for (size_t i = 0; i < buttons.size(); ++i) {
            int buttonX = startX + static_cast<int>(i) * (buttonWidth + buttonSpacing);
            buttons[i]->SetPosition(buttonX, buttonY);
            buttons[i]->SetSize(buttonWidth, buttonHeight);
        }
    }

    void UltraCanvasModalDialog::RenderOverlay() {
        IRenderContext* ctx = GetRenderContext();
        if (!ctx) return;

        Color overlayColor(0, 0, 0, 128);
        ctx->DrawFilledRectangle(overlayRect, overlayColor);
    }

    void UltraCanvasModalDialog::RenderBackground() {
        IRenderContext* ctx = GetRenderContext();
        if (!ctx) return;

        Rect2Di bounds = GetBounds();
        ctx->DrawFilledRectangle(bounds, config.backgroundColor);
    }

    void UltraCanvasModalDialog::RenderBorder() {
        IRenderContext* ctx = GetRenderContext();
        if (!ctx) return;

        Rect2Di bounds = GetBounds();
        ctx->SetStrokePaint(config.borderColor);
        ctx->SetStrokeWidth(config.borderWidth);
        ctx->DrawRectangle(bounds);
    }

    void UltraCanvasModalDialog::RenderTitleBar() {
        IRenderContext* ctx = GetRenderContext();
        if (!ctx) return;

        Color titleBg(240, 240, 240, 255);
        ctx->DrawFilledRectangle(titleBarRect, titleBg);

        ctx->SetStrokePaint(Color(200, 200, 200, 255));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(titleBarRect);

        ctx->SetTextPaint(Colors::Black);
        ctx->SetFontSize(12.0f);
        Point2Di titlePos(titleBarRect.x + 10, titleBarRect.y + 20);
        ctx->DrawText(config.title, titlePos);

        ctx->SetFillPaint(Color(220, 220, 220, 255));
        ctx->FillRectangle(closeButtonRect);
        ctx->SetTextPaint(Colors::Black);
        ctx->SetFontSize(10.0f);
        ctx->DrawText("×", Point2Di(closeButtonRect.x + 6, closeButtonRect.y + 14));
    }

    void UltraCanvasModalDialog::RenderIcon() {
        IRenderContext* ctx = GetRenderContext();
        if (!ctx) return;

        if (config.type == DialogType::Custom) return;

        Color iconColor = GetTypeColor();
        ctx->SetFillPaint(iconColor);
        ctx->FillRectangle(iconRect);

        ctx->SetTextPaint(Colors::White);
        ctx->SetFontSize(20.0f);
        std::string iconText = GetTypeIcon();
        Point2Di iconTextPos(iconRect.x + 10, iconRect.y + 22);
        ctx->DrawText(iconText, iconTextPos);
    }

    void UltraCanvasModalDialog::RenderMessage() {
        IRenderContext* ctx = GetRenderContext();
        if (!ctx) return;

        if (config.message.empty()) return;

        ctx->SetTextPaint(Colors::Black);
        ctx->SetFontSize(11.0f);

        std::vector<std::string> lines;
        std::string currentLine;
        std::stringstream ss(config.message);
        std::string word;

        while (ss >> word) {
            if (currentLine.empty()) {
                currentLine = word;
            } else {
                std::string testLine = currentLine + " " + word;
                if (static_cast<int>(testLine.length()) * 7 < messageRect.width) {
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

        float lineHeight = 16.0f;
        for (size_t i = 0; i < lines.size() && i < 3; ++i) {
            Point2Di linePos(messageRect.x, messageRect.y + static_cast<int>((i + 1) * lineHeight));
            ctx->DrawText(lines[i], linePos);
        }
    }

    void UltraCanvasModalDialog::RenderDetails() {
        IRenderContext* ctx = GetRenderContext();
        if (!ctx) return;

        if (config.details.empty()) return;

        ctx->SetTextPaint(Color(100, 100, 100, 255));
        ctx->SetFontSize(9.0f);
        Point2Di detailsPos(detailsRect.x, detailsRect.y + 12);
        ctx->DrawText(config.details, detailsPos);
    }

    void UltraCanvasModalDialog::RenderDialogButtons() {
        IRenderContext* ctx = GetRenderContext();
        if (!ctx) return;

        for (auto& button : buttons) {
            if (button && button->IsVisible()) {
                button->Render(ctx);
            }
        }
    }

    void UltraCanvasModalDialog::RenderChildElements() {
        IRenderContext* ctx = GetRenderContext();
        if (!ctx) return;

        for (auto& element : childElements) {
            if (element && element->IsVisible()) {
                element->Render(ctx);
            }
        }
    }

    void UltraCanvasModalDialog::OnButtonClick(DialogButton button) {
        DialogResult buttonResult = DialogResult::NoResult;

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

    void UltraCanvasModalDialog::OnTitleBarMouseDown(const Point2Di& position) {
        if (!config.movable) return;

        isDragging = true;
        dragStartPos = position;
        windowStartPos = Point2Di(GetX(), GetY());
    }

    void UltraCanvasModalDialog::OnTitleBarMouseMove(const Point2Di& position) {
        if (!isDragging) return;

        Point2Di delta(position.x - dragStartPos.x, position.y - dragStartPos.y);
        Point2Di newPos(windowStartPos.x + delta.x, windowStartPos.y + delta.y);
        SetPosition(newPos.x, newPos.y);
        CalculateLayout();
    }

    void UltraCanvasModalDialog::OnTitleBarMouseUp() {
        isDragging = false;
    }

    void UltraCanvasModalDialog::OnOverlayClick() {
        if (config.modal && config.closeOnClickOutside) {
            Close(DialogResult::Cancel);
        }
    }

    void UltraCanvasModalDialog::StartShowAnimation() {
        isAnimating = true;
        animationProgress = 0.0f;
        animationStartTime = std::chrono::steady_clock::now();
    }

    void UltraCanvasModalDialog::StartHideAnimation() {
        isAnimating = true;
        animationProgress = 1.0f;
        animationStartTime = std::chrono::steady_clock::now();
    }

    void UltraCanvasModalDialog::UpdateAnimation(float deltaTime) {
        if (!isAnimating) return;

        animationProgress += deltaTime / config.animationDuration;

        if (animationProgress >= 1.0f) {
            animationProgress = 1.0f;
            isAnimating = false;
        }
    }

    float UltraCanvasModalDialog::CalculateAnimationAlpha() const {
        return animationProgress;
    }

    Point2Di UltraCanvasModalDialog::CalculateAnimationPosition() const {
        return Point2Di(GetX(), GetY());
    }

    float UltraCanvasModalDialog::CalculateAnimationScale() const {
        return animationProgress;
    }

    void UltraCanvasModalDialog::AddCustomButton(const std::string& text, DialogResult buttonResult,
                                                 std::function<void()> callback) {
        auto button = std::make_shared<UltraCanvasButton>("custom_btn_" + text,
                                                          static_cast<long>(buttons.size() + 2000),
                                                          0, 0, 80, 30);
        button->SetText(text);
        button->onClick = [this, buttonResult, callback]() {
            if (callback) callback();
            Close(buttonResult);
        };
        buttons.push_back(button);
        PositionButtons();
    }

    void UltraCanvasModalDialog::RemoveButton(DialogButton button) {
        // Implementation placeholder
    }

    void UltraCanvasModalDialog::SetButtonEnabled(DialogButton button, bool enabled) {
        // Implementation placeholder
    }

    void UltraCanvasModalDialog::SetButtonVisible(DialogButton button, bool visible) {
        // Implementation placeholder
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
            case DialogType::Information: return Color(52, 144, 220, 255);
            case DialogType::Question:    return Color(92, 184, 92, 255);
            case DialogType::Warning:     return Color(240, 173, 78, 255);
            case DialogType::Error:       return Color(217, 83, 79, 255);
            case DialogType::Custom:
            default:                      return Color(128, 128, 128, 255);
        }
    }

    std::string UltraCanvasModalDialog::GetTypeIcon() const {
        switch (config.type) {
            case DialogType::Information: return "i";
            case DialogType::Question:    return "?";
            case DialogType::Warning:     return "!";
            case DialogType::Error:       return "X";
            case DialogType::Custom:
            default:                      return "*";
        }
    }

    void UltraCanvasModalDialog::CreateDefaultButtons() {
        buttons.clear();

        int buttonFlags = static_cast<int>(config.buttons);

        auto createButton = [this](const std::string& id, long uid, const std::string& text,
                                   DialogButton btnType) {
            auto button = std::make_shared<UltraCanvasButton>(id, uid, 0, 0, 80, 30);
            button->SetText(text);
            button->onClick = [this, btnType]() {
                OnButtonClick(btnType);
            };
            return button;
        };

        if (buttonFlags & static_cast<int>(DialogButton::OK)) {
            buttons.push_back(createButton("ok_btn", 1001, "OK", DialogButton::OK));
        }
        if (buttonFlags & static_cast<int>(DialogButton::Cancel)) {
            buttons.push_back(createButton("cancel_btn", 1002, "Cancel", DialogButton::Cancel));
        }
        if (buttonFlags & static_cast<int>(DialogButton::Yes)) {
            buttons.push_back(createButton("yes_btn", 1003, "Yes", DialogButton::Yes));
        }
        if (buttonFlags & static_cast<int>(DialogButton::No)) {
            buttons.push_back(createButton("no_btn", 1004, "No", DialogButton::No));
        }
        if (buttonFlags & static_cast<int>(DialogButton::Apply)) {
            buttons.push_back(createButton("apply_btn", 1005, "Apply", DialogButton::Apply));
        }
        if (buttonFlags & static_cast<int>(DialogButton::Close)) {
            buttons.push_back(createButton("close_btn", 1006, "Close", DialogButton::Close));
        }
        if (buttonFlags & static_cast<int>(DialogButton::Help)) {
            buttons.push_back(createButton("help_btn", 1007, "Help", DialogButton::Help));
        }
        if (buttonFlags & static_cast<int>(DialogButton::Retry)) {
            buttons.push_back(createButton("retry_btn", 1008, "Retry", DialogButton::Retry));
        }
        if (buttonFlags & static_cast<int>(DialogButton::Ignore)) {
            buttons.push_back(createButton("ignore_btn", 1009, "Ignore", DialogButton::Ignore));
        }
        if (buttonFlags & static_cast<int>(DialogButton::Abort)) {
            buttons.push_back(createButton("abort_btn", 1010, "Abort", DialogButton::Abort));
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

    UltraCanvasInputDialog::UltraCanvasInputDialog(const InputDialogConfig& inputCfg)
            : UltraCanvasModalDialog(inputCfg),
              inputConfig(inputCfg),
              isValid(true) {

        SetTitle(inputCfg.title);
        SetMessage(inputCfg.inputLabel);
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

        if (static_cast<int>(inputValue.length()) < inputConfig.minLength ||
            static_cast<int>(inputValue.length()) > inputConfig.maxLength) {
            isValid = false;
        }

        if (isValid && inputConfig.validator) {
            isValid = inputConfig.validator(inputValue);
        }
    }

    bool UltraCanvasInputDialog::OnEvent(const UCEvent& event) {
        if (textInput) {
            if (textInput->OnEvent(event)) {
                inputValue = textInput->GetText();
                ValidateInput();
                return true;
            }
        }

        return UltraCanvasModalDialog::OnEvent(event);
    }

    void UltraCanvasInputDialog::SetupInputField() {
        textInput = std::make_shared<UltraCanvasTextInput>("input_field", 2001, 0, 0, 300, 25);
        textInput->SetText(inputConfig.defaultValue);
        textInput->SetPlaceholder(inputConfig.inputPlaceholder);
        inputValue = inputConfig.defaultValue;

        switch (inputConfig.inputType) {
            case InputType::Password:
                textInput->SetInputType(TextInputType::Password);
                break;
            case InputType::Number:
                textInput->SetInputType(TextInputType::Number);
                break;
            case InputType::Email:
                textInput->SetInputType(TextInputType::Email);
                break;
            case InputType::MultilineText:
                textInput->SetInputType(TextInputType::Multiline);
                textInput->SetSize(300, 80);
                break;
            default:
                textInput->SetInputType(TextInputType::Text);
                break;
        }

        AddElement(textInput);
        ValidateInput();
    }

    void UltraCanvasInputDialog::OnInputChanged(const std::string& text) {
        inputValue = text;
        ValidateInput();

        if (inputConfig.onInputChanged) {
            inputConfig.onInputChanged(text);
        }
    }

    void UltraCanvasInputDialog::OnInputValidation() {
    }

// ===== FILE DIALOG IMPLEMENTATION =====

    UltraCanvasFileDialog::UltraCanvasFileDialog(const FileDialogConfig& fileCfg)
            : UltraCanvasModalDialog(fileCfg),
              fileConfig(fileCfg),
              currentDirectory(fileCfg.initialDirectory),
              showHiddenFiles(fileCfg.showHiddenFiles) {

        SetTitle(fileCfg.title);
        SetButtons(DialogButtons::OKCancel);

        if (currentDirectory.empty()) {
            try {
                currentDirectory = std::filesystem::current_path().string();
            } catch (const std::exception& e) {
                currentDirectory = ".";
            }
        }

        fileNameText = fileCfg.defaultFileName;

        SetupFileInterface();
        CalculateFileDialogLayout();
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
        try {
            if (std::filesystem::exists(directory) && std::filesystem::is_directory(directory)) {
                currentDirectory = std::filesystem::canonical(directory).string();
                RefreshFileList();
                if (onDirectoryChanged) onDirectoryChanged(currentDirectory);
            }
        } catch (const std::exception& e) {
            std::cerr << "Invalid path: " << directory << " - " << e.what() << std::endl;
        }
    }

    std::string UltraCanvasFileDialog::GetCurrentDirectory() const {
        return currentDirectory;
    }

    void UltraCanvasFileDialog::SetupFileInterface() {
        RefreshFileList();
    }

    void UltraCanvasFileDialog::RefreshFileList() {
        fileList.clear();
        directoryList.clear();

        try {
            for (const auto& entry : std::filesystem::directory_iterator(currentDirectory)) {
                std::string fileName = entry.path().filename().string();

                if (!showHiddenFiles && !fileName.empty() && fileName[0] == '.') {
                    continue;
                }

                if (entry.is_directory()) {
                    directoryList.push_back(fileName);
                } else if (entry.is_regular_file()) {
                    if (fileConfig.dialogType == FileDialogType::SelectFolder) continue;

                    if (IsFileMatchingFilter(fileName)) {
                        fileList.push_back(fileName);
                    }
                }
            }

            std::sort(directoryList.begin(), directoryList.end());
            std::sort(fileList.begin(), fileList.end());

        } catch (const std::exception& e) {
            std::cerr << "Error reading directory: " << e.what() << std::endl;
        }

        selectedFileIndex = -1;
        selectedFiles.clear();
        scrollOffset = 0;
    }

    void UltraCanvasFileDialog::PopulateFileList() {
        RefreshFileList();
    }

    void UltraCanvasFileDialog::OnFileSelected(const std::string& filename) {
        if (fileConfig.allowMultipleSelection) {
            selectedFiles.push_back(filename);
        } else {
            selectedFiles.clear();
            selectedFiles.push_back(filename);
        }
    }

    void UltraCanvasFileDialog::OnDirectoryChanged(const std::string& directory) {
        SetCurrentDirectory(directory);
    }

    void UltraCanvasFileDialog::SetFileFilters(const std::vector<FileFilter>& filters) {
        fileConfig.filters = filters;
        if (!fileConfig.filters.empty()) {
            fileConfig.selectedFilterIndex = 0;
        }
        RefreshFileList();
    }

    void UltraCanvasFileDialog::AddFileFilter(const FileFilter& filter) {
        fileConfig.filters.push_back(filter);
    }

    void UltraCanvasFileDialog::AddFileFilter(const std::string& description, const std::vector<std::string>& extensions) {
        fileConfig.filters.emplace_back(description, extensions);
    }

    void UltraCanvasFileDialog::AddFileFilter(const std::string& description, const std::string& extension) {
        fileConfig.filters.emplace_back(description, extension);
    }

    int UltraCanvasFileDialog::GetSelectedFilterIndex() const {
        return fileConfig.selectedFilterIndex;
    }

    void UltraCanvasFileDialog::SetSelectedFilterIndex(int index) {
        if (index >= 0 && index < static_cast<int>(fileConfig.filters.size())) {
            fileConfig.selectedFilterIndex = index;
            RefreshFileList();
        }
    }

    const std::vector<FileFilter>& UltraCanvasFileDialog::GetFileFilters() const {
        return fileConfig.filters;
    }

    void UltraCanvasFileDialog::SetShowHiddenFiles(bool show) {
        showHiddenFiles = show;
        fileConfig.showHiddenFiles = show;
        RefreshFileList();
    }

    bool UltraCanvasFileDialog::GetShowHiddenFiles() const {
        return showHiddenFiles;
    }

    void UltraCanvasFileDialog::SetDefaultFileName(const std::string& fileName) {
        fileConfig.defaultFileName = fileName;
        fileNameText = fileName;
    }

    std::string UltraCanvasFileDialog::GetDefaultFileName() const {
        return fileConfig.defaultFileName;
    }

    std::string UltraCanvasFileDialog::GetSelectedFilePath() const {
        if (selectedFiles.empty()) return "";
        return CombinePath(currentDirectory, selectedFiles[0]);
    }

    std::vector<std::string> UltraCanvasFileDialog::GetSelectedFilePaths() const {
        std::vector<std::string> paths;
        for (const auto& file : selectedFiles) {
            paths.push_back(CombinePath(currentDirectory, file));
        }
        return paths;
    }

    void UltraCanvasFileDialog::CalculateFileDialogLayout() {
        Rect2Di bounds = GetBounds();
        int titleBarHeight = 30;

        pathBarRect = Rect2Di(bounds.x + 10, bounds.y + titleBarHeight + 10, bounds.width - 20, pathBarHeight);

        int topOffset = titleBarHeight + pathBarHeight + 20;
        int bottomOffset = buttonHeight + filterHeight + 70;
        fileListRect = Rect2Di(bounds.x + 10, bounds.y + topOffset,
                               bounds.width - 20, bounds.height - topOffset - bottomOffset);

        maxVisibleItems = fileListRect.height / itemHeight;

        int fileNameY = bounds.y + bounds.height - buttonHeight - filterHeight - 55;
        fileNameInputRect = Rect2Di(bounds.x + 90, fileNameY, bounds.width - 110, 22);

        int filterY = bounds.y + bounds.height - buttonHeight - filterHeight - 25;
        filterSelectorRect = Rect2Di(bounds.x + 90, filterY, bounds.width - 110, filterHeight);
    }

    Rect2Di UltraCanvasFileDialog::GetPathBarBounds() const {
        return pathBarRect;
    }

    Rect2Di UltraCanvasFileDialog::GetFileListBounds() const {
        return fileListRect;
    }

    Rect2Di UltraCanvasFileDialog::GetFileNameInputBounds() const {
        return fileNameInputRect;
    }

    Rect2Di UltraCanvasFileDialog::GetFilterSelectorBounds() const {
        return filterSelectorRect;
    }

    void UltraCanvasFileDialog::Render(IRenderContext* ctx) {
        if (!isVisible || !ctx) return;

        ctx->PushState();

        if (config.modal) {
            RenderOverlay();
        }

        RenderBackground();
        RenderTitleBar();
        RenderBorder();

        RenderPathBar(ctx);
        RenderFileList(ctx);

        if (fileConfig.dialogType != FileDialogType::SelectFolder) {
            RenderFileNameInput(ctx);
        }

        RenderFilterSelector(ctx);
        RenderDialogButtons();

        ctx->PopState();
    }

    void UltraCanvasFileDialog::RenderPathBar(IRenderContext* ctx) {
        ctx->SetFillPaint(Colors::White);
        ctx->FillRectangle(pathBarRect);
        ctx->SetStrokePaint(listBorderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(pathBarRect);

        ctx->SetTextPaint(Colors::Black);
        ctx->SetFontSize(12.0f);
        ctx->DrawText(currentDirectory, Point2Di(pathBarRect.x + 5, pathBarRect.y + 20));
    }

    void UltraCanvasFileDialog::RenderFileList(IRenderContext* ctx) {
        ctx->SetFillPaint(listBackgroundColor);
        ctx->FillRectangle(fileListRect);
        ctx->SetStrokePaint(listBorderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(fileListRect);

        ctx->ClipRect(Rect2D(fileListRect.x, fileListRect.y, fileListRect.width, fileListRect.height));

        ctx->SetFontSize(12.0f);
        int currentY = fileListRect.y + 2;
        int itemIndex = 0;

        for (const std::string& dir : directoryList) {
            if (itemIndex < scrollOffset) {
                itemIndex++;
                continue;
            }

            if (currentY + itemHeight > fileListRect.y + fileListRect.height) break;

            RenderFileItem(ctx, dir, itemIndex, currentY, true);
            currentY += itemHeight;
            itemIndex++;
        }

        for (const std::string& file : fileList) {
            if (itemIndex < scrollOffset) {
                itemIndex++;
                continue;
            }

            if (currentY + itemHeight > fileListRect.y + fileListRect.height) break;

            RenderFileItem(ctx, file, itemIndex, currentY, false);
            currentY += itemHeight;
            itemIndex++;
        }

        ctx->ClearClipRect();
        RenderScrollbar(ctx);
    }

    void UltraCanvasFileDialog::RenderFileItem(IRenderContext* ctx, const std::string& name, int index, int y, bool isDirectory) {
        bool isSelected = (index == selectedFileIndex);
        bool isHovered = (index == hoverItemIndex);

        if (isSelected) {
            ctx->SetFillPaint(selectedItemColor);
            ctx->FillRectangle(Rect2Di(fileListRect.x + 1, y, fileListRect.width - 17, itemHeight));
        } else if (isHovered) {
            ctx->SetFillPaint(hoverItemColor);
            ctx->FillRectangle(Rect2Di(fileListRect.x + 1, y, fileListRect.width - 17, itemHeight));
        }

        ctx->SetTextPaint(isDirectory ? directoryColor : fileColor);
        std::string icon = isDirectory ? "[D] " : "    ";
        ctx->DrawText(icon + name, Point2Di(fileListRect.x + 5, y + 14));
    }

    void UltraCanvasFileDialog::RenderScrollbar(IRenderContext* ctx) {
        int totalItems = static_cast<int>(directoryList.size() + fileList.size());
        if (totalItems <= maxVisibleItems) return;

        Rect2Di scrollBounds(
                fileListRect.x + fileListRect.width - 15,
                fileListRect.y,
                15,
                fileListRect.height
        );

        ctx->SetFillPaint(Color(240, 240, 240, 255));
        ctx->FillRectangle(scrollBounds);

        float thumbHeight = (static_cast<float>(maxVisibleItems) * scrollBounds.height) / totalItems;
        float thumbY = scrollBounds.y + (scrollOffset * (scrollBounds.height - thumbHeight)) /
                                        (totalItems - maxVisibleItems);

        ctx->SetFillPaint(Color(160, 160, 160, 255));
        ctx->FillRectangle(Rect2Di(scrollBounds.x + 2, static_cast<int>(thumbY), 11, static_cast<int>(thumbHeight)));
    }

    void UltraCanvasFileDialog::RenderFileNameInput(IRenderContext* ctx) {
        ctx->SetTextPaint(Colors::Black);
        ctx->SetFontSize(11.0f);
        ctx->DrawText("File name:", Point2Di(fileNameInputRect.x - 75, fileNameInputRect.y + 15));

        ctx->SetFillPaint(Colors::White);
        ctx->FillRectangle(fileNameInputRect);
        ctx->SetStrokePaint(listBorderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(fileNameInputRect);

        ctx->SetTextPaint(Colors::Black);
        ctx->DrawText(fileNameText, Point2Di(fileNameInputRect.x + 5, fileNameInputRect.y + 15));
    }

    void UltraCanvasFileDialog::RenderFilterSelector(IRenderContext* ctx) {
        ctx->SetTextPaint(Colors::Black);
        ctx->SetFontSize(11.0f);
        ctx->DrawText("Files of type:", Point2Di(filterSelectorRect.x - 75, filterSelectorRect.y + 16));

        ctx->SetFillPaint(Color(240, 240, 240, 255));
        ctx->FillRectangle(filterSelectorRect);
        ctx->SetStrokePaint(listBorderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(filterSelectorRect);

        if (fileConfig.selectedFilterIndex >= 0 &&
            fileConfig.selectedFilterIndex < static_cast<int>(fileConfig.filters.size())) {
            const FileFilter& filter = fileConfig.filters[fileConfig.selectedFilterIndex];
            ctx->SetTextPaint(Colors::Black);
            ctx->DrawText(filter.ToDisplayString(), Point2Di(filterSelectorRect.x + 5, filterSelectorRect.y + 16));
        }

        ctx->DrawText("▼", Point2Di(filterSelectorRect.x + filterSelectorRect.width - 20, filterSelectorRect.y + 16));
    }

    bool UltraCanvasFileDialog::OnEvent(const UCEvent& event) {
        if (!isVisible) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left) {
                    Point2Di eventPos(event.x, event.y);

                    if (titleBarRect.Contains(eventPos)) {
                        if (closeButtonRect.Contains(eventPos)) {
                            HandleCancelButton();
                            Close(DialogResult::Cancel);
                            return true;
                        }
                        OnTitleBarMouseDown(eventPos);
                        return true;
                    }

                    if (fileListRect.Contains(eventPos)) {
                        HandleFileListClick(event);
                        return true;
                    }

                    if (filterSelectorRect.Contains(eventPos)) {
                        HandleFilterDropdownClick();
                        return true;
                    }

                    for (auto& button : buttons) {
                        if (button && button->Contains(event.x, event.y)) {
                            button->OnEvent(event);
                            return true;
                        }
                    }
                }
                break;

            case UCEventType::MouseDoubleClick:
                if (fileListRect.Contains(Point2Di(event.x, event.y))) {
                    HandleFileListDoubleClick(event);
                    return true;
                }
                break;

            case UCEventType::MouseMove:
                if (isDragging) {
                    OnTitleBarMouseMove(Point2Di(event.x, event.y));
                    return true;
                }
                if (fileListRect.Contains(Point2Di(event.x, event.y))) {
                    int newHoverIndex = scrollOffset + (event.y - fileListRect.y) / itemHeight;
                    int totalItems = static_cast<int>(directoryList.size() + fileList.size());
                    hoverItemIndex = (newHoverIndex < totalItems) ? newHoverIndex : -1;
                } else {
                    hoverItemIndex = -1;
                }
                break;

            case UCEventType::MouseUp:
                if (isDragging) {
                    OnTitleBarMouseUp();
                    return true;
                }
                break;

            case UCEventType::KeyDown:
                HandleKeyDown(event);
                return true;

            case UCEventType::TextInput:
                HandleTextInput(event);
                return true;

            case UCEventType::MouseWheel:
                HandleMouseWheel(event);
                return true;

            default:
                break;
        }

        return false;
    }

    void UltraCanvasFileDialog::HandleFileListClick(const UCEvent& event) {
        int clickedIndex = scrollOffset + (event.y - fileListRect.y) / itemHeight;
        int totalItems = static_cast<int>(directoryList.size() + fileList.size());

        if (clickedIndex >= totalItems) return;

        selectedFileIndex = clickedIndex;

        if (clickedIndex >= static_cast<int>(directoryList.size())) {
            int fileIndex = clickedIndex - static_cast<int>(directoryList.size());
            if (fileIndex < static_cast<int>(fileList.size())) {
                std::string selectedFile = fileList[fileIndex];
                fileNameText = selectedFile;

                if (fileConfig.allowMultipleSelection) {
                    if (event.ctrl) {
                        auto it = std::find(selectedFiles.begin(), selectedFiles.end(), selectedFile);
                        if (it != selectedFiles.end()) {
                            selectedFiles.erase(it);
                        } else {
                            selectedFiles.push_back(selectedFile);
                        }
                    } else {
                        selectedFiles = {selectedFile};
                    }
                } else {
                    selectedFiles = {selectedFile};
                }
            }
        }
    }

    void UltraCanvasFileDialog::HandleFileListDoubleClick(const UCEvent& event) {
        if (selectedFileIndex < 0) return;

        if (selectedFileIndex < static_cast<int>(directoryList.size())) {
            NavigateToDirectory(directoryList[selectedFileIndex]);
        } else {
            HandleOkButton();
        }
    }

    void UltraCanvasFileDialog::HandleKeyDown(const UCEvent& event) {
        switch (event.virtualKey) {
            case UCKeys::Return:
                HandleOkButton();
                break;

            case UCKeys::Escape:
                HandleCancelButton();
                Close(DialogResult::Cancel);
                break;

            case UCKeys::Up:
                if (selectedFileIndex > 0) {
                    selectedFileIndex--;
                    EnsureItemVisible();
                    UpdateSelection();
                }
                break;

            case UCKeys::Down:
                if (selectedFileIndex < static_cast<int>(directoryList.size() + fileList.size()) - 1) {
                    selectedFileIndex++;
                    EnsureItemVisible();
                    UpdateSelection();
                }
                break;

            case UCKeys::Backspace:
                NavigateToParentDirectory();
                break;

            default:
                break;
        }
    }

    void UltraCanvasFileDialog::HandleTextInput(const UCEvent& event) {
        if (fileConfig.dialogType != FileDialogType::SelectFolder) {
            fileNameText += event.text;
        }
    }

    void UltraCanvasFileDialog::HandleMouseWheel(const UCEvent& event) {
        if (fileListRect.Contains(Point2Di(event.x, event.y))) {
            int totalItems = static_cast<int>(directoryList.size() + fileList.size());
            scrollOffset = std::max(0, std::min(totalItems - maxVisibleItems,
                                                scrollOffset - event.wheelDelta));
        }
    }

    void UltraCanvasFileDialog::HandleFilterDropdownClick() {
        if (!fileConfig.filters.empty()) {
            fileConfig.selectedFilterIndex = (fileConfig.selectedFilterIndex + 1) % static_cast<int>(fileConfig.filters.size());
            RefreshFileList();
        }
    }

    void UltraCanvasFileDialog::HandleOkButton() {
        if (fileConfig.dialogType == FileDialogType::SelectFolder) {
            selectedFiles = {currentDirectory};
            if (onFileSelected) {
                onFileSelected(currentDirectory);
            }
        } else if (fileConfig.dialogType == FileDialogType::Save) {
            if (!fileNameText.empty()) {
                selectedFiles = {fileNameText};
                std::string fullPath = CombinePath(currentDirectory, fileNameText);
                if (onFileSelected) {
                    onFileSelected(fullPath);
                }
            }
        } else {
            if (fileConfig.allowMultipleSelection && !selectedFiles.empty()) {
                if (onFilesSelected) {
                    onFilesSelected(GetSelectedFilePaths());
                }
            } else if (!selectedFiles.empty()) {
                std::string fullPath = CombinePath(currentDirectory, selectedFiles[0]);
                if (onFileSelected) {
                    onFileSelected(fullPath);
                }
            }
        }

        Close(DialogResult::OK);
    }

    void UltraCanvasFileDialog::HandleCancelButton() {
        if (onCancelled) {
            onCancelled();
        }
    }

    void UltraCanvasFileDialog::NavigateToDirectory(const std::string& dirName) {
        if (dirName == "..") {
            NavigateToParentDirectory();
            return;
        }

        std::string newPath = CombinePath(currentDirectory, dirName);
        SetCurrentDirectory(newPath);
    }

    void UltraCanvasFileDialog::NavigateToParentDirectory() {
        try {
            std::filesystem::path parentPath = std::filesystem::path(currentDirectory).parent_path();
            if (!parentPath.empty()) {
                SetCurrentDirectory(parentPath.string());
            }
        } catch (const std::exception& e) {
            std::cerr << "Error navigating to parent directory: " << e.what() << std::endl;
        }
    }

    void UltraCanvasFileDialog::EnsureItemVisible() {
        if (selectedFileIndex < scrollOffset) {
            scrollOffset = selectedFileIndex;
        } else if (selectedFileIndex >= scrollOffset + maxVisibleItems) {
            scrollOffset = selectedFileIndex - maxVisibleItems + 1;
        }
    }

    void UltraCanvasFileDialog::UpdateSelection() {
        if (selectedFileIndex < 0) return;

        int totalDirectories = static_cast<int>(directoryList.size());

        if (selectedFileIndex >= totalDirectories) {
            int fileIndex = selectedFileIndex - totalDirectories;
            if (fileIndex < static_cast<int>(fileList.size())) {
                std::string selectedFile = fileList[fileIndex];
                fileNameText = selectedFile;

                if (!fileConfig.allowMultipleSelection) {
                    selectedFiles = {selectedFile};
                }
            }
        }
    }

    bool UltraCanvasFileDialog::IsFileMatchingFilter(const std::string& fileName) const {
        if (fileConfig.selectedFilterIndex < 0 ||
            fileConfig.selectedFilterIndex >= static_cast<int>(fileConfig.filters.size())) {
            return true;
        }

        const FileFilter& filter = fileConfig.filters[fileConfig.selectedFilterIndex];
        return filter.Matches(fileName);
    }

    std::string UltraCanvasFileDialog::GetFileExtension(const std::string& fileName) const {
        size_t dotPos = fileName.find_last_of('.');
        if (dotPos != std::string::npos && dotPos < fileName.length() - 1) {
            return fileName.substr(dotPos + 1);
        }
        return "";
    }

    std::string UltraCanvasFileDialog::CombinePath(const std::string& dir, const std::string& file) const {
        std::filesystem::path path(dir);
        path /= file;
        return path.string();
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
        return ShowMessage(message, title, DialogType::Warning, DialogButtons::OKCancel);
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
        config.inputLabel = prompt;
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
        if (!enabled) return "";

        InputDialogConfig config;
        config.title = title;
        config.inputLabel = prompt;
        config.defaultValue = defaultValue;
        config.inputType = InputType::MultilineText;
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
                                                             const std::vector<FileFilter>& filters,
                                                             const std::string& initialDir) {
        if (!enabled) return "";

        FileDialogConfig config;
        config.title = title.empty() ? "Open File" : title;
        config.dialogType = FileDialogType::Open;
        config.initialDirectory = initialDir;
        if (!filters.empty()) {
            config.filters = filters;
        }

        auto dialog = CreateFileDialog(config);
        DialogResult result = ShowCustomDialog(dialog);

        if (result == DialogResult::OK) {
            auto fileDialog = std::dynamic_pointer_cast<UltraCanvasFileDialog>(dialog);
            return fileDialog ? fileDialog->GetSelectedFilePath() : "";
        }

        return "";
    }

    std::string UltraCanvasDialogManager::ShowSaveFileDialog(const std::string& title,
                                                             const std::vector<FileFilter>& filters,
                                                             const std::string& initialDir,
                                                             const std::string& defaultName) {
        if (!enabled) return "";

        FileDialogConfig config;
        config.title = title.empty() ? "Save File" : title;
        config.dialogType = FileDialogType::Save;
        config.initialDirectory = initialDir;
        config.defaultFileName = defaultName;
        if (!filters.empty()) {
            config.filters = filters;
        }

        auto dialog = CreateFileDialog(config);
        DialogResult result = ShowCustomDialog(dialog);

        if (result == DialogResult::OK) {
            auto fileDialog = std::dynamic_pointer_cast<UltraCanvasFileDialog>(dialog);
            return fileDialog ? fileDialog->GetSelectedFilePath() : "";
        }

        return "";
    }

    std::vector<std::string> UltraCanvasDialogManager::ShowOpenMultipleFilesDialog(const std::string& title,
                                                                                   const std::vector<FileFilter>& filters,
                                                                                   const std::string& initialDir) {
        if (!enabled) return {};

        FileDialogConfig config;
        config.title = title.empty() ? "Open Files" : title;
        config.dialogType = FileDialogType::OpenMultiple;
        config.allowMultipleSelection = true;
        config.initialDirectory = initialDir;
        if (!filters.empty()) {
            config.filters = filters;
        }

        auto dialog = CreateFileDialog(config);
        DialogResult result = ShowCustomDialog(dialog);

        if (result == DialogResult::OK) {
            auto fileDialog = std::dynamic_pointer_cast<UltraCanvasFileDialog>(dialog);
            return fileDialog ? fileDialog->GetSelectedFilePaths() : std::vector<std::string>();
        }

        return {};
    }

    std::string UltraCanvasDialogManager::ShowSelectFolderDialog(const std::string& title,
                                                                 const std::string& initialDir) {
        if (!enabled) return "";

        FileDialogConfig config;
        config.title = title.empty() ? "Select Folder" : title;
        config.dialogType = FileDialogType::SelectFolder;
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
        return std::make_shared<UltraCanvasModalDialog>(config);
    }

    DialogResult UltraCanvasDialogManager::ShowCustomDialog(std::shared_ptr<UltraCanvasModalDialog> dialog,
                                                            UltraCanvasWindowBase* parent) {
        if (!enabled || !dialog) return DialogResult::Cancel;

        RegisterDialog(dialog);
        DialogResult result = dialog->ShowModal(parent);
        UnregisterDialog(dialog);

        return result;
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

        for (auto& dialog : activeDialogs) {
            if (dialog) {
                dialog->Update(deltaTime);
            }
        }

        activeDialogs.erase(
                std::remove_if(activeDialogs.begin(), activeDialogs.end(),
                               [](const std::shared_ptr<UltraCanvasModalDialog>& dialog) {
                                   return !dialog || !dialog->IsVisible();
                               }),
                activeDialogs.end()
        );
    }

    void UltraCanvasDialogManager::Render() {
        if (!enabled) return;

        for (auto& dialog : activeDialogs) {
            if (dialog && dialog->IsVisible()) {
                IRenderContext* ctx = dialog->GetRenderContext();
                if (ctx) {
                    dialog->Render(ctx);
                }
            }
        }
    }

    std::string UltraCanvasDialogManager::DialogResultToString(DialogResult result) {
        switch (result) {
            case DialogResult::OK:       return "OK";
            case DialogResult::Cancel:   return "Cancel";
            case DialogResult::Yes:      return "Yes";
            case DialogResult::No:       return "No";
            case DialogResult::Apply:    return "Apply";
            case DialogResult::Close:    return "Close";
            case DialogResult::Help:     return "Help";
            case DialogResult::Retry:    return "Retry";
            case DialogResult::Ignore:   return "Ignore";
            case DialogResult::Abort:    return "Abort";
            case DialogResult::NoResult:
            default:                     return "NoResult";
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
        return DialogResult::NoResult;
    }

    std::string UltraCanvasDialogManager::DialogButtonToString(DialogButton button) {
        switch (button) {
            case DialogButton::OK:         return "OK";
            case DialogButton::Cancel:     return "Cancel";
            case DialogButton::Yes:        return "Yes";
            case DialogButton::No:         return "No";
            case DialogButton::Apply:      return "Apply";
            case DialogButton::Close:      return "Close";
            case DialogButton::Help:       return "Help";
            case DialogButton::Retry:      return "Retry";
            case DialogButton::Ignore:     return "Ignore";
            case DialogButton::Abort:      return "Abort";
            case DialogButton::NoneButton:
            default:                       return "None";
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
        return DialogButton::NoneButton;
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

    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::CreateMessageDialog(
            const std::string& message, const std::string& title,
            DialogType type, DialogButtons buttons) {

        DialogConfig config = defaultConfig;
        config.message = message;
        config.title = title;
        config.type = type;
        config.buttons = buttons;

        return CreateCustomDialog(config);
    }

    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::CreateInputDialog(
            const InputDialogConfig& config) {
        return std::make_shared<UltraCanvasInputDialog>(config);
    }

    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::CreateFileDialog(
            const FileDialogConfig& config) {
        return std::make_shared<UltraCanvasFileDialog>(config);
    }

} // namespace UltraCanvas