// core/UltraCanvasModalDialog.cpp
// Implementation of cross-platform modal dialog system - Window-based
// Version: 3.1.0
// Last Modified: 2025-01-19
// Author: UltraCanvas Framework

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasApplication.h"
#include <fmt/os.h>
#include <iostream>
#include <algorithm>

namespace UltraCanvas {

// ===== STATIC MEMBER DEFINITIONS =====
    std::vector<std::shared_ptr<UltraCanvasModalDialog>> UltraCanvasDialogManager::activeDialogs;
    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::currentModal = nullptr;
    bool UltraCanvasDialogManager::enabled = true;
    DialogConfig UltraCanvasDialogManager::defaultConfig;
    InputDialogConfig UltraCanvasDialogManager::defaultInputConfig;
    FileDialogConfig UltraCanvasDialogManager::defaultFileConfig;

// ===== MODAL DIALOG IMPLEMENTATION =====

    void UltraCanvasModalDialog::CreateDialog(const DialogConfig& config) {
        dialogConfig = config;
        UltraCanvasWindow::Create(dialogConfig);
        ApplyTypeDefaults();
        CreateDialogButtons();
        CalculateDialogLayout();
    }

    void UltraCanvasModalDialog::SetDialogTitle(const std::string& title) {
        dialogConfig.title = title;
        SetWindowTitle(title);
    }

    void UltraCanvasModalDialog::SetMessage(const std::string& message) {
        dialogConfig.message = message;
        if (messageLabel) {
            messageLabel->SetText(message);
        }
        CalculateDialogLayout();
    }

    void UltraCanvasModalDialog::SetDetails(const std::string& details) {
        dialogConfig.details = details;
        if (detailsLabel) {
            detailsLabel->SetText(details);
        }
        CalculateDialogLayout();
    }

    void UltraCanvasModalDialog::SetDialogType(DialogType type) {
        dialogConfig.dialogType = type;
        ApplyTypeDefaults();
    }

    void UltraCanvasModalDialog::SetDialogButtons(DialogButtons buttons) {
        dialogConfig.buttons = buttons;
        CreateDialogButtons();
        CalculateDialogLayout();
    }

    void UltraCanvasModalDialog::SetDefaultButton(DialogButton button) {
        dialogConfig.defaultButton = button;
    }

    std::string UltraCanvasModalDialog::GetDialogTitle() const {
        return dialogConfig.title;
    }

    std::string UltraCanvasModalDialog::GetMessage() const {
        return dialogConfig.message;
    }

    std::string UltraCanvasModalDialog::GetDetails() const {
        return dialogConfig.details;
    }

    DialogType UltraCanvasModalDialog::GetDialogType() const {
        return dialogConfig.dialogType;
    }

    DialogButtons UltraCanvasModalDialog::GetDialogButtons() const {
        return dialogConfig.buttons;
    }

    DialogButton UltraCanvasModalDialog::GetDefaultButton() const {
        return dialogConfig.defaultButton;
    }

    void UltraCanvasModalDialog::ShowModal(UltraCanvasWindowBase* parent) {
        // Center on parent if specified
        if (parent && dialogConfig.position == DialogPosition::CenterParent) {
            int parentX, parentY, parentW, parentH;
            parent->GetWindowPosition(parentX, parentY);
            parent->GetWindowSize(parentW, parentH);

            int dialogX = parentX + (parentW - dialogConfig.width) / 2;
            int dialogY = parentY + (parentH - dialogConfig.height) / 2;
            SetWindowPosition(dialogX, dialogY);
        }

        // Register with dialog manager
        UltraCanvasDialogManager::RegisterDialog(
                std::dynamic_pointer_cast<UltraCanvasModalDialog>(shared_from_this()));

        // Show the window
        Show();
        //Focus();
    }

    void UltraCanvasModalDialog::RequestClose() {
        if (!_created || _state == WindowState::Closing) {
            return;
        }

        if (!dialogConfig.onClosing || dialogConfig.onClosing(result)) {
            Close();
        }
    }

    void UltraCanvasModalDialog::Close() {
        UltraCanvasWindow::Close();

        if (dialogConfig.onResult) {
            dialogConfig.onResult(result);
        }
        // Unregister from dialog manager
        UltraCanvasDialogManager::UnregisterDialog(
                std::dynamic_pointer_cast<UltraCanvasModalDialog>(shared_from_this()));
    }

    void UltraCanvasModalDialog::CloseDialog(DialogResult dialogResult) {
        result = dialogResult;
        // Hide and close the window
        RequestClose();
    }

    bool UltraCanvasModalDialog::OnEvent(const UCEvent& event) {
        if (dialogConfig.closeOnEscape && event.type == UCEventType::KeyDown && event.virtualKey == UCKeys::Escape) {
            CloseDialog(DialogResult::Cancel);
            return true;
        }
        return UltraCanvasWindow::OnEvent(event);
    }

    bool UltraCanvasModalDialog::IsModalDialog() const {
        return dialogConfig.modal;
    }

    DialogResult UltraCanvasModalDialog::GetResult() const {
        return result;
    }

    void UltraCanvasModalDialog::AddDialogElement(std::shared_ptr<UltraCanvasUIElement> element) {
        if (element) {
            AddChild(element);
            CalculateDialogLayout();
        }
    }

    void UltraCanvasModalDialog::RemoveDialogElement(std::shared_ptr<UltraCanvasUIElement> element) {
        if (element) {
            RemoveChild(element);
            CalculateDialogLayout();
        }
    }

    void UltraCanvasModalDialog::ClearDialogElements() {
        ClearChildren();
        // Re-add dialog-specific elements
        CreateDialogButtons();
        CalculateDialogLayout();
    }

    void UltraCanvasModalDialog::RenderCustomContent(IRenderContext* ctx) {
        if (!ctx) return;

        // Render dialog-specific content
        RenderDialogIcon(ctx);
        RenderDialogMessage(ctx);
        RenderDialogDetails(ctx);
    }

    void UltraCanvasModalDialog::CalculateDialogLayout() {
        int width = config_.width;
        int height = config_.height;
        int padding = 16;
        int titleBarHeight = 32;
        int buttonAreaHeight = 50;

        // Title bar
        // Content area
        contentRect = Rect2Di(padding, padding,
                              width - 2 * padding,
                              height - titleBarHeight - buttonAreaHeight - 2 * padding);

        // Button area
        buttonAreaRect = Rect2Di(padding, height - buttonAreaHeight,
                                 width - 2 * padding, buttonAreaHeight - padding);

        // Icon (if dialog type has one)
        int iconSize = 48;
        iconRect = Rect2Di(padding, padding, iconSize, iconSize);

        // Message area
        int messageX = padding + (dialogConfig.dialogType != DialogType::Custom ? iconSize + padding : 0);
        messageRect = Rect2Di(messageX, padding,
                              width - messageX - padding, 60);

        // Details area
        detailsRect = Rect2Di(messageX, messageRect.y + messageRect.height + 8,
                              width - messageX - padding,
                              contentRect.height - messageRect.height - 16);

        PositionDialogButtons();
    }

    void UltraCanvasModalDialog::CreateDialogButtons() {
        // Clear existing buttons
        for (auto& btn : dialogButtons) {
            if (btn) {
                RemoveChild(btn);
            }
        }
        dialogButtons.clear();

        int buttonMask = static_cast<int>(dialogConfig.buttons);

        auto addButton = [this](DialogButton btn, const std::string& text) {
            auto button = std::make_shared<UltraCanvasButton>(
                    fmt::format("DialogBtn_{}", (int)btn), 0, 0, 80, 28);
            button->SetText(text);
            button->onClick = [this, btn]() {
                OnDialogButtonClick(btn);
            };
            dialogButtons.push_back(button);
            AddChild(button);
        };

        if (buttonMask & static_cast<int>(DialogButton::OK)) {
            addButton(DialogButton::OK, "OK");
        }
        if (buttonMask & static_cast<int>(DialogButton::Cancel)) {
            addButton(DialogButton::Cancel, "Cancel");
        }
        if (buttonMask & static_cast<int>(DialogButton::Yes)) {
            addButton(DialogButton::Yes, "Yes");
        }
        if (buttonMask & static_cast<int>(DialogButton::No)) {
            addButton(DialogButton::No, "No");
        }
        if (buttonMask & static_cast<int>(DialogButton::Retry)) {
            addButton(DialogButton::Retry, "Retry");
        }
        if (buttonMask & static_cast<int>(DialogButton::Abort)) {
            addButton(DialogButton::Abort, "Abort");
        }
        if (buttonMask & static_cast<int>(DialogButton::Ignore)) {
            addButton(DialogButton::Ignore, "Ignore");
        }
    }

    void UltraCanvasModalDialog::PositionDialogButtons() {
        if (dialogButtons.empty()) return;

        int buttonWidth = 80;
        int buttonHeight = 28;
        int buttonSpacing = 10;
        int totalWidth = static_cast<int>(dialogButtons.size()) * buttonWidth +
                         (static_cast<int>(dialogButtons.size()) - 1) * buttonSpacing;

        int startX = buttonAreaRect.x + (buttonAreaRect.width - totalWidth) / 2;
        int y = buttonAreaRect.y + (buttonAreaRect.height - buttonHeight) / 2;

        for (size_t i = 0; i < dialogButtons.size(); ++i) {
            int x = startX + static_cast<int>(i) * (buttonWidth + buttonSpacing);
            dialogButtons[i]->SetPosition(x, y);
            dialogButtons[i]->SetSize(buttonWidth, buttonHeight);
        }
    }

    void UltraCanvasModalDialog::OnDialogButtonClick(DialogButton button) {
        DialogResult dialogResult = DialogResult::NoResult;

        switch (button) {
            case DialogButton::OK:     dialogResult = DialogResult::OK; break;
            case DialogButton::Cancel: dialogResult = DialogResult::Cancel; break;
            case DialogButton::Yes:    dialogResult = DialogResult::Yes; break;
            case DialogButton::No:     dialogResult = DialogResult::No; break;
            case DialogButton::Retry:  dialogResult = DialogResult::Retry; break;
            case DialogButton::Abort:  dialogResult = DialogResult::Abort; break;
            case DialogButton::Ignore: dialogResult = DialogResult::Ignore; break;
            case DialogButton::Apply:  dialogResult = DialogResult::Apply; break;
            case DialogButton::Close:  dialogResult = DialogResult::Close; break;
            case DialogButton::Help:   dialogResult = DialogResult::Help; break;
            default: break;
        }

        CloseDialog(dialogResult);
    }

    void UltraCanvasModalDialog::RenderDialogIcon(IRenderContext* ctx) {
        if (dialogConfig.dialogType == DialogType::Custom) return;

        Color iconColor = GetTypeColor();
        ctx->SetFillPaint(iconColor);

        int centerX = iconRect.x + iconRect.width / 2;
        int centerY = iconRect.y + iconRect.height / 2;
        int radius = iconRect.width / 2 - 4;

        ctx->FillCircle(centerX, centerY, radius);

        ctx->SetTextPaint(Colors::White);
        ctx->SetFontSize(20.0f);
        std::string iconText = GetTypeIcon();
        Point2Di iconTextPos(iconRect.x + 10, iconRect.y + 22);
        ctx->DrawText(iconText, iconTextPos);
    }

    void UltraCanvasModalDialog::RenderDialogMessage(IRenderContext* ctx) {
        if (dialogConfig.message.empty()) return;

        ctx->SetFillPaint(Colors::Black);
        ctx->DrawText(dialogConfig.message, messageRect.x, messageRect.y);
    }

    void UltraCanvasModalDialog::RenderDialogDetails(IRenderContext* ctx) {
        if (dialogConfig.details.empty()) return;

        ctx->SetFillPaint(Colors::DarkGray);
        ctx->DrawText(dialogConfig.details, detailsRect.x, detailsRect.y);
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
            default:                   return "";
        }
    }

    Color UltraCanvasModalDialog::GetTypeColor() const {
        switch (dialogConfig.dialogType) {
            case DialogType::Information: return Color(70, 130, 180);   // Steel Blue
            case DialogType::Question:    return Color(70, 130, 180);   // Steel Blue
            case DialogType::Warning:     return Color(255, 193, 7);    // Amber
            case DialogType::Error:       return Color(220, 53, 69);    // Red
            default:                      return Colors::Gray;
        }
    }

    void UltraCanvasModalDialog::ApplyTypeDefaults() {
        // Set default icon/title based on type
        switch (dialogConfig.dialogType) {
            case DialogType::Information:
                if (dialogConfig.title == "Dialog") dialogConfig.title = "Information";
                break;
            case DialogType::Question:
                if (dialogConfig.title == "Dialog") dialogConfig.title = "Question";
                break;
            case DialogType::Warning:
                if (dialogConfig.title == "Dialog") dialogConfig.title = "Warning";
                break;
            case DialogType::Error:
                if (dialogConfig.title == "Dialog") dialogConfig.title = "Error";
                break;
            default:
                break;
        }
    }

    void UltraCanvasModalDialog::AddCustomButton(const std::string& text, DialogResult buttonResult,
                                                 std::function<void()> callback) {
        auto button = std::make_shared<UltraCanvasButton>(
                "DialogBtn_Custom_" + text, 1000 + static_cast<long>(dialogButtons.size()), 0, 0, 80, 28);
        button->SetText(text);
        button->onClick = [this, buttonResult, callback]() {
            if (callback) callback();
            CloseDialog(buttonResult);
        };
        dialogButtons.push_back(button);
        AddChild(button);
        PositionDialogButtons();
    }

    void UltraCanvasModalDialog::SetButtonDisabled(DialogButton button, bool disabled) {
        auto btnId = fmt::format("DialogBtn_{}", static_cast<int>(button));
        for (auto& btn : dialogButtons) {
            if (btn && btn->GetIdentifier() == btnId) {
                btn->SetDisabled(disabled);
                break;
            }
        }
    }

    void UltraCanvasModalDialog::SetButtonVisible(DialogButton button, bool buttonVisible) {
        auto btnId = fmt::format("DialogBtn_{}", static_cast<int>(button));
        for (auto& btn : dialogButtons) {
            if (btn && btn->GetIdentifier() == btnId) {
                btn->SetVisible(buttonVisible);
                break;
            }
        }
    }

    std::string UltraCanvasModalDialog::GetTypeIcon() const {
        switch (dialogConfig.dialogType) {
            case DialogType::Information: return "i";
            case DialogType::Question:    return "?";
            case DialogType::Warning:     return "!";
            case DialogType::Error:       return "X";
            case DialogType::Custom:
            default:                      return "*";
        }
    }

// ===== INPUT DIALOG IMPLEMENTATION =====
    void UltraCanvasInputDialog::CreateInputDialog(const InputDialogConfig &config) {
        inputConfig = config;
        CreateDialog(config);

        SetMessage(inputConfig.inputLabel);
        SetDialogButtons(DialogButtons::OKCancel);

        SetupInputField();
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

        AddChild(textInput);
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
    void UltraCanvasFileDialog::CreateFileDialog(const FileDialogConfig &config) {
        fileConfig = config;
        UltraCanvasModalDialog::CreateDialog(config);

        currentDirectory = fileConfig.initialDirectory;
        showHiddenFiles = fileConfig.showHiddenFiles;

        SetDialogButtons(DialogButtons::OKCancel);

        if (currentDirectory.empty()) {
            try {
                currentDirectory = std::filesystem::current_path().string();
            } catch (const std::exception& e) {
                currentDirectory = ".";
            }
        }

        fileNameText = fileConfig.defaultFileName;

        SetupFileInterface();
        CalculateFileDialogLayout();

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

        pathBarRect = Rect2Di(bounds.x + 10, bounds.y + 10, bounds.width - 20, pathBarHeight);

        int topOffset = pathBarHeight + 20;
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

    void UltraCanvasFileDialog::RenderCustomContent(UltraCanvas::IRenderContext *ctx) {
        if (!IsVisible() || !ctx) return;

        ctx->PushState();

//        if (config_.modal) {
//            RenderOverlay();
//        }

//        RenderBackground();
//        RenderTitleBar();
//        RenderBorder();

        RenderPathBar(ctx);
        RenderFileList(ctx);

        if (fileConfig.dialogType != FileDialogType::SelectFolder) {
            RenderFileNameInput(ctx);
        }

        RenderFilterSelector(ctx);
//        RenderDialogButtons();

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
        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left) {
                    Point2Di eventPos(event.x, event.y);

                    if (fileListRect.Contains(eventPos)) {
                        HandleFileListClick(event);
                        return true;
                    }

                    if (filterSelectorRect.Contains(eventPos)) {
                        HandleFilterDropdownClick();
                        return true;
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
                if (fileListRect.Contains(Point2Di(event.x, event.y))) {
                    int newHoverIndex = scrollOffset + (event.y - fileListRect.y) / itemHeight;
                    int totalItems = static_cast<int>(directoryList.size() + fileList.size());
                    hoverItemIndex = (newHoverIndex < totalItems) ? newHoverIndex : -1;
                } else {
                    hoverItemIndex = -1;
                }
                break;

            case UCEventType::MouseUp:
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
        return UltraCanvasModalDialog::OnEvent(event);
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

        CloseDialog(DialogResult::OK);
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

// ===== MODAL EVENT BLOCKING =====
    bool UltraCanvasDialogManager::HandleModalEvents(const UCEvent& event, UltraCanvasWindow* targetWindow) {
        if (!enabled || !currentModal) return false;

        // Get the modal window
        UltraCanvasWindowBase* modalWindow = currentModal.get();
        if (!modalWindow || !modalWindow->IsVisible()) return false;


        // Block input events going to other windows when modal is active
        switch (event.type) {
            case UCEventType::MouseDown:
            case UCEventType::MouseUp:
            case UCEventType::MouseMove:
            case UCEventType::MouseWheel:
            case UCEventType::MouseDoubleClick:
            case UCEventType::MouseEnter:
            case UCEventType::MouseLeave:
            case UCEventType::KeyDown:
            case UCEventType::KeyUp:
            case UCEventType::TextInput:
            case UCEventType::Shortcut:
                // Block these events from reaching non-modal windows
                if (targetWindow != modalWindow) return true;
                break;

            case UCEventType::WindowFocus:
                if (targetWindow && targetWindow != modalWindow) {
                    modalWindow->RaiseAndFocus();
                    return true;
                }
                break;
//            case UCEventType::WindowBlur:
            default:
                return false;
        }
        return false;
    }

    bool UltraCanvasDialogManager::HasActiveModal() {
        return enabled && currentModal && currentModal->IsVisible();
    }

    UltraCanvasWindowBase* UltraCanvasDialogManager::GetModalWindow() {
        if (HasActiveModal()) {
            return currentModal.get();
        }
        return nullptr;
    }

// ===== ASYNC CALLBACK-BASED DIALOGS =====
    void UltraCanvasDialogManager::ShowMessage(const std::string& message, const std::string& title,
                                                    DialogType type, DialogButtons buttons,
                                                    std::function<void(DialogResult)> onResult,
                                                    UltraCanvasWindowBase* parent) {
        if (!enabled) {
            if (onResult) onResult(DialogResult::Cancel);
            return;
        }

        auto dialog = CreateMessageDialog(message, title, type, buttons);
        ShowDialog(dialog, onResult, parent);
    }

    void UltraCanvasDialogManager::ShowInformation(const std::string& message, const std::string& title,
                                                        std::function<void(DialogResult)> onResult,
                                                        UltraCanvasWindowBase* parent) {
        ShowMessage(message, title, DialogType::Information, DialogButtons::OK, onResult, parent);
    }

    void UltraCanvasDialogManager::ShowQuestion(const std::string& message, const std::string& title,
                                                     std::function<void(DialogResult)> onResult,
                                                     UltraCanvasWindowBase* parent) {
        ShowMessage(message, title, DialogType::Question, DialogButtons::YesNo, onResult, parent);
    }

    void UltraCanvasDialogManager::ShowWarning(const std::string& message, const std::string& title,
                                                    std::function<void(DialogResult)> onResult,
                                                    UltraCanvasWindowBase* parent) {
        ShowMessage(message, title, DialogType::Warning, DialogButtons::OKCancel, onResult, parent);
    }

    void UltraCanvasDialogManager::ShowError(const std::string& message, const std::string& title,
                                                  std::function<void(DialogResult)> onResult,
                                                  UltraCanvasWindowBase* parent) {
        ShowMessage(message, title, DialogType::Error, DialogButtons::OK, onResult, parent);
    }

    void UltraCanvasDialogManager::ShowConfirmation(const std::string& message, const std::string& title,
                                                         std::function<void(bool confirmed)> onResult,
                                                         UltraCanvasWindowBase* parent) {
        ShowMessage(message, title, DialogType::Question, DialogButtons::YesNo,
                         [onResult](DialogResult r) {
                             if (onResult) onResult(r == DialogResult::Yes);
                         }, parent);
    }

// ===== LEGACY METHODS (now async with optional callbacks) =====
//    void UltraCanvasDialogManager::ShowMessage(const std::string& message, const std::string& title,
//                                               DialogType type, DialogButtons buttons,
//                                               std::function<void(DialogResult)> onResult) {
//        ShowMessage(message, title, type, buttons, onResult, nullptr);
//    }
//
//    void UltraCanvasDialogManager::ShowInformation(const std::string& message, const std::string& title,
//                                                   std::function<void(DialogResult)> onResult) {
//        ShowMessage(message, title, DialogType::Information, DialogButtons::OK, onResult, nullptr);
//    }
//
//    void UltraCanvasDialogManager::ShowQuestion(const std::string& message, const std::string& title,
//                                                std::function<void(DialogResult)> onResult) {
//        ShowMessage(message, title, DialogType::Question, DialogButtons::YesNo, onResult, nullptr);
//    }
//
//    void UltraCanvasDialogManager::ShowWarning(const std::string& message, const std::string& title,
//                                               std::function<void(DialogResult)> onResult) {
//        ShowMessage(message, title, DialogType::Warning, DialogButtons::OKCancel, onResult, nullptr);
//    }
//
//    void UltraCanvasDialogManager::ShowError(const std::string& message, const std::string& title,
//                                             std::function<void(DialogResult)> onResult) {
//        ShowMessage(message, title, DialogType::Error, DialogButtons::OK, onResult, nullptr);
//    }
//
//    void UltraCanvasDialogManager::ShowConfirmation(const std::string& message, const std::string& title,
//                                                    std::function<void(bool)> onResult) {
//        ShowConfirmation(message, title, onResult, nullptr);
//    }
// ===== CUSTOM DIALOGS =====
    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::CreateDialog(const DialogConfig& config) {
        auto dialog = std::make_shared<UltraCanvasModalDialog>();
        dialog->CreateDialog(config);
        return dialog;
    }

    void UltraCanvasDialogManager::ShowDialog(std::shared_ptr<UltraCanvasModalDialog> dialog,
                                              std::function<void(DialogResult)> onResult,
                                              UltraCanvasWindowBase* parent) {
        if (!enabled || !dialog) {
            if (onResult) onResult(DialogResult::Cancel);
            return;
        }

        if (onResult) {
            dialog->SetResultCallback(onResult);
        }
        dialog->ShowModal(parent);
    }

    void UltraCanvasDialogManager::ShowInputDialog(const std::string& prompt, const std::string& title,
                                                          const std::string& defaultValue, InputType type,
                                                          std::function<void(DialogResult, const std::string&)> onResult, UltraCanvasWindowBase* parent) {
        if (!enabled) return;

        InputDialogConfig config;
        config.title = title;
        config.inputLabel = prompt;
        config.defaultValue = defaultValue;
        config.inputType = type;

        auto dialog = CreateInputDialog(config);
        ShowDialog(dialog, [onResult, dialog](DialogResult result) {
            if (onResult) {
                onResult(result, dialog->GetInputValue());
            }
        }, parent);
    }

    void UltraCanvasDialogManager::ShowOpenFileDialog(const std::string& title,
                                                             const std::vector<FileFilter>& filters,
                                                             const std::string& initialDir,
                                                             std::function<void(DialogResult, const std::string&)> onResult,
                                                             UltraCanvasWindowBase* parent) {
        if (!enabled) return;

        FileDialogConfig config;
        config.title = title.empty() ? "Open File" : title;
        config.dialogType = FileDialogType::Open;
        config.initialDirectory = initialDir;
        if (!filters.empty()) {
            config.filters = filters;
        }

        auto dialog = CreateFileDialog(config);
        ShowDialog(dialog, [onResult, dialog](DialogResult result) {
            if (onResult) {
                onResult(result, dialog->GetSelectedFilePath());
            }
        }, parent);
    }

    void UltraCanvasDialogManager::ShowSaveFileDialog(const std::string& title,
                                                             const std::vector<FileFilter>& filters,
                                                             const std::string& initialDir,
                                                             const std::string& defaultName,
                                                             std::function<void(DialogResult, const std::string&)> onResult,
                                                             UltraCanvasWindowBase* parent) {
        if (!enabled) return;

        FileDialogConfig config;
        config.title = title.empty() ? "Save File" : title;
        config.dialogType = FileDialogType::Save;
        config.initialDirectory = initialDir;
        config.defaultFileName = defaultName;
        if (!filters.empty()) {
            config.filters = filters;
        }

        auto dialog = CreateFileDialog(config);
        ShowDialog(dialog, [onResult, dialog](DialogResult result) {
            if (onResult) {
                onResult(result, dialog->GetSelectedFilePath());
            }
        }, parent);
    }

//    std::vector<std::string> UltraCanvasDialogManager::ShowOpenMultipleFilesDialog(const std::string& title,
//                                                                                   const std::vector<FileFilter>& filters,
//                                                                                   const std::string& initialDir) {
//        if (!enabled) return {};
//
//        FileDialogConfig config;
//        config.title = title.empty() ? "Open Files" : title;
//        config.dialogType = FileDialogType::OpenMultiple;
//        config.allowMultipleSelection = true;
//        config.initialDirectory = initialDir;
//        if (!filters.empty()) {
//            config.filters = filters;
//        }
//
//        auto dialog = CreateFileDialog(config);
//        DialogResult result = ShowCustomDialog(dialog);
//
//        if (result == DialogResult::OK) {
//            auto fileDialog = std::dynamic_pointer_cast<UltraCanvasFileDialog>(dialog);
//            return fileDialog ? fileDialog->GetSelectedFilePaths() : std::vector<std::string>();
//        }
//
//        return {};
//    }

    void UltraCanvasDialogManager::ShowSelectFolderDialog(const std::string& title,
                                                                 const std::string& initialDir,
                                                                 std::function<void(DialogResult, const std::string&)> onResult,
                                                                 UltraCanvasWindowBase* parent) {
        if (!enabled) return;

        FileDialogConfig config;
        config.title = title.empty() ? "Select Folder" : title;
        config.dialogType = FileDialogType::SelectFolder;
        config.initialDirectory = initialDir;

        auto dialog = CreateFileDialog(config);
        ShowDialog(dialog, [onResult, dialog](DialogResult result) {
            if (onResult) {
                onResult(result, dialog->GetCurrentDirectory());
            }
        }, parent);
    }

    void UltraCanvasDialogManager::CloseAllDialogs() {
        for (auto& dialog : activeDialogs) {
            if (dialog) {
                dialog->CloseDialog(DialogResult::Cancel);
            }
        }
        activeDialogs.clear();
        currentModal.reset();
    }

    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::GetCurrentModalDialog() {
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

        // Clean up closed dialogs
        activeDialogs.erase(
                std::remove_if(activeDialogs.begin(), activeDialogs.end(),
                               [](const std::shared_ptr<UltraCanvasModalDialog>& dialog) {
                                   return !dialog || !dialog->IsVisible();
                               }),
                activeDialogs.end()
        );

        // Update current modal reference
        if (currentModal && !currentModal->IsVisible()) {
            currentModal.reset();
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
            if (dialog->IsModalDialog()) {
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
        config.dialogType = type;
        config.buttons = buttons;

        return CreateDialog(config);
    }

    std::shared_ptr<UltraCanvasInputDialog> UltraCanvasDialogManager::CreateInputDialog(
            const InputDialogConfig& config) {
        auto dialog = std::make_shared<UltraCanvasInputDialog>();
        dialog->CreateInputDialog(config);
        return dialog;
    }

    std::shared_ptr<UltraCanvasFileDialog> UltraCanvasDialogManager::CreateFileDialog(
            const FileDialogConfig& config) {
        auto dialog =  std::make_shared<UltraCanvasFileDialog>();
        dialog->CreateFileDialog(config);
        return dialog;
    }

} // namespace UltraCanvas