// Apps/Texter/UltraCanvasTextEditorDialogs.cpp
// Implementation of custom dialogs for Find and Replace
// Version: 1.0.0
// Last Modified: 2026-01-26
// Author: UltraCanvas Framework

#include "UltraCanvasTextEditorDialogs.h"
#include "UltraCanvasBoxLayout.h"
#include <algorithm>

namespace UltraCanvas {

// ===== FIND DIALOG IMPLEMENTATION =====

    UltraCanvasFindDialog::UltraCanvasFindDialog()
            : caseSensitive(false)
            , wholeWord(false)
    {
    }

    void UltraCanvasFindDialog::Initialize() {
        // Configure dialog window
        DialogConfig config;
        config.title = "Find";
        config.width = 450;
        config.height = 180;
        config.resizable = false;
        config.buttons = DialogButtons::NoButtons; // We'll add custom buttons

        CreateDialog(config);
        BuildLayout();
        WireCallbacks();
    }

    void UltraCanvasFindDialog::BuildLayout() {
        // Create main vertical layout
        auto mainLayout = CreateVBoxLayout(this);
        mainLayout->SetSpacing(12);
        SetPadding(16);

        // ===== CONTENT SECTION =====
        contentSection = std::make_shared<UltraCanvasContainer>("FindContent", 3000, 0, 0, 400, 100);
        auto contentLayout = CreateVBoxLayout(contentSection.get());
        contentLayout->SetSpacing(8);

        // Search input row
        auto searchRow = std::make_shared<UltraCanvasContainer>("SearchRow", 3001, 0, 0, 400, 30);
        auto searchRowLayout = CreateHBoxLayout(searchRow.get());
        searchRowLayout->SetSpacing(8);

        searchLabel = std::make_shared<UltraCanvasLabel>("SearchLabel", 3002, 0, 0, 80, 25);
        searchLabel->SetText("Find what:");
        searchLabel->SetFontSize(11);
        // searchLabel->SetTextVerticalAlignment(TextVerticalAlignement::Middle);

        searchInput = std::make_shared<UltraCanvasTextInput>("SearchInput", 3003, 0, 0, 300, 25);
        searchInput->SetPlaceholder("Enter search text...");

        searchRowLayout->AddUIElement(searchLabel)->SetCrossAlignment(LayoutAlignment::Center);
        searchRowLayout->AddUIElement(searchInput)->SetStretch(1);

        contentLayout->AddUIElement(searchRow);

        // Options row
        auto optionsRow = std::make_shared<UltraCanvasContainer>("OptionsRow", 3004, 0, 0, 400, 25);
        auto optionsLayout = CreateHBoxLayout(optionsRow.get());
        optionsLayout->SetSpacing(16);

        caseSensitiveCheck = std::make_shared<UltraCanvasCheckbox>("CaseSensitive", 3005, 0, 0, 150, 20);
        caseSensitiveCheck->SetText("Case sensitive");
        caseSensitiveCheck->SetCheckState(CheckboxState::Unchecked);

        wholeWordCheck = std::make_shared<UltraCanvasCheckbox>("WholeWord", 3006, 0, 0, 150, 20);
        wholeWordCheck->SetText("Whole word");
        wholeWordCheck->SetCheckState(CheckboxState::Unchecked);

        optionsLayout->AddUIElement(caseSensitiveCheck);
        optionsLayout->AddUIElement(wholeWordCheck);

        contentLayout->AddUIElement(optionsRow);

        mainLayout->AddUIElement(contentSection);

        // ===== BUTTON SECTION =====
        buttonSection = std::make_shared<UltraCanvasContainer>("ButtonSection", 3010, 0, 0, 400, 35);
        auto buttonLayout = CreateHBoxLayout(buttonSection.get());
        buttonLayout->SetSpacing(10);

        buttonLayout->AddStretch(1); // Push buttons to the right

        findPreviousButton = std::make_shared<UltraCanvasButton>("FindPrevious", 3011, 0, 0, 100, 28);
        findPreviousButton->SetText("◄ Previous");

        findNextButton = std::make_shared<UltraCanvasButton>("FindNext", 3012, 0, 0, 100, 28);
        findNextButton->SetText("Next ►");

        closeButton = std::make_shared<UltraCanvasButton>("CloseBtn", 3013, 0, 0, 80, 28);
        closeButton->SetText("Close");

        buttonLayout->AddUIElement(findPreviousButton);
        buttonLayout->AddUIElement(findNextButton);
        buttonLayout->AddUIElement(closeButton);

        mainLayout->AddUIElement(buttonSection);
    }

    void UltraCanvasFindDialog::WireCallbacks() {
        // Search input - update on change
        searchInput->onTextChanged = [this](const std::string& text) {
            searchText = text;
            // Enable/disable buttons based on text
            bool hasText = !text.empty();
            findNextButton->SetDisabled(!hasText);
            findPreviousButton->SetDisabled(!hasText);
        };

        // Case sensitive checkbox
        caseSensitiveCheck->onStateChanged = [this](CheckboxState oldState, CheckboxState newState) {
            caseSensitive = (newState == CheckboxState::Checked);
        };

        // Whole word checkbox
        wholeWordCheck->onStateChanged = [this](CheckboxState oldState, CheckboxState newState) {
            wholeWord = (newState == CheckboxState::Checked);
        };

        // Find Next button
        findNextButton->onClick = [this]() {
            if (onFindNext && !searchText.empty()) {
                onFindNext(searchText, caseSensitive, wholeWord);
            }
        };

        // Find Previous button
        findPreviousButton->onClick = [this]() {
            if (onFindPrevious && !searchText.empty()) {
                onFindPrevious(searchText, caseSensitive, wholeWord);
            }
        };

        // Close button
        closeButton->onClick = [this]() {
            if (onClose) {
                onClose();
            }
            CloseDialog(DialogResult::Cancel);
        };

        // Enter key in search input triggers Find Next
        searchInput->onEnterPressed = [this](const std::string& text) {
            if (onFindNext && !text.empty()) {
                onFindNext(text, caseSensitive, wholeWord);
            }
        };

        // Initially disable buttons if no text
        findNextButton->SetDisabled(true);
        findPreviousButton->SetDisabled(true);
    }

    void UltraCanvasFindDialog::SetSearchText(const std::string& text) {
        searchText = text;
        if (searchInput) {
            searchInput->SetText(text);
        }
    }

    void UltraCanvasFindDialog::SetCaseSensitive(bool sensitive) {
        caseSensitive = sensitive;
        if (caseSensitiveCheck) {
            caseSensitiveCheck->SetCheckState(sensitive ? CheckboxState::Checked : CheckboxState::Unchecked);
        }
    }

    void UltraCanvasFindDialog::SetWholeWord(bool whole) {
        wholeWord = whole;
        if (wholeWordCheck) {
            wholeWordCheck->SetCheckState(whole ? CheckboxState::Checked : CheckboxState::Unchecked);
        }
    }

// ===== REPLACE DIALOG IMPLEMENTATION =====

    UltraCanvasReplaceDialog::UltraCanvasReplaceDialog()
            : caseSensitive(false)
            , wholeWord(false)
    {
    }

    void UltraCanvasReplaceDialog::Initialize() {
        // Configure dialog window
        DialogConfig config;
        config.title = "Replace";
        config.width = 500;
        config.height = 220;
        config.resizable = false;
        config.buttons = DialogButtons::NoButtons;

        CreateDialog(config);
        BuildLayout();
        WireCallbacks();
    }

    void UltraCanvasReplaceDialog::BuildLayout() {
        // Create main vertical layout
        auto mainLayout = CreateVBoxLayout(this);
        mainLayout->SetSpacing(12);
        SetPadding(16);

        // ===== CONTENT SECTION =====
        contentSection = std::make_shared<UltraCanvasContainer>("ReplaceContent", 4000, 0, 0, 450, 120);
        auto contentLayout = CreateVBoxLayout(contentSection.get());
        contentLayout->SetSpacing(8);

        // Find input row
        auto findRow = std::make_shared<UltraCanvasContainer>("FindRow", 4001, 0, 0, 450, 30);
        auto findRowLayout = CreateHBoxLayout(findRow.get());
        findRowLayout->SetSpacing(8);

        findLabel = std::make_shared<UltraCanvasLabel>("FindLabel", 4002, 0, 0, 80, 25);
        findLabel->SetText("Find what:");
        findLabel->SetFontSize(11);

        findInput = std::make_shared<UltraCanvasTextInput>("FindInput", 4003, 0, 0, 350, 25);
        findInput->SetPlaceholder("Enter text to find...");

        findRowLayout->AddUIElement(findLabel)->SetCrossAlignment(LayoutAlignment::Center);
        findRowLayout->AddUIElement(findInput)->SetStretch(1);

        contentLayout->AddUIElement(findRow);

        // Replace input row
        auto replaceRow = std::make_shared<UltraCanvasContainer>("ReplaceRow", 4004, 0, 0, 450, 30);
        auto replaceRowLayout = CreateHBoxLayout(replaceRow.get());
        replaceRowLayout->SetSpacing(8);

        replaceLabel = std::make_shared<UltraCanvasLabel>("ReplaceLabel", 4005, 0, 0, 80, 25);
        replaceLabel->SetText("Replace with:");
        replaceLabel->SetFontSize(11);
        //replaceLabel->SetTextVerticalAlignment(TextVerticalAlignement::Middle);

        replaceInput = std::make_shared<UltraCanvasTextInput>("ReplaceInput", 4006, 0, 0, 350, 25);
        replaceInput->SetPlaceholder("Enter replacement text...");

        replaceRowLayout->AddUIElement(replaceLabel)->SetCrossAlignment(LayoutAlignment::Center);
        replaceRowLayout->AddUIElement(replaceInput)->SetStretch(1);

        contentLayout->AddUIElement(replaceRow);

        // Options row
        auto optionsRow = std::make_shared<UltraCanvasContainer>("OptionsRow", 4007, 0, 0, 450, 25);
        auto optionsLayout = CreateHBoxLayout(optionsRow.get());
        optionsLayout->SetSpacing(16);

        caseSensitiveCheck = std::make_shared<UltraCanvasCheckbox>("CaseSensitive", 4008, 0, 0, 150, 20);
        caseSensitiveCheck->SetText("Case sensitive");
        caseSensitiveCheck->SetCheckState(CheckboxState::Unchecked);

        wholeWordCheck = std::make_shared<UltraCanvasCheckbox>("WholeWord", 4009, 0, 0, 150, 20);
        wholeWordCheck->SetText("Whole word");
        wholeWordCheck->SetCheckState(CheckboxState::Unchecked);

        optionsLayout->AddUIElement(caseSensitiveCheck);
        optionsLayout->AddUIElement(wholeWordCheck);

        contentLayout->AddUIElement(optionsRow);

        mainLayout->AddUIElement(contentSection);

        // ===== BUTTON SECTION =====
        buttonSection = std::make_shared<UltraCanvasContainer>("ButtonSection", 4010, 0, 0, 450, 35);
        auto buttonLayout = CreateHBoxLayout(buttonSection.get());
        buttonLayout->SetSpacing(10);

        findNextButton = std::make_shared<UltraCanvasButton>("FindNext", 4011, 0, 0, 100, 28);
        findNextButton->SetText("Find Next");

        replaceButton = std::make_shared<UltraCanvasButton>("Replace", 4012, 0, 0, 100, 28);
        replaceButton->SetText("Replace");

        replaceAllButton = std::make_shared<UltraCanvasButton>("ReplaceAll", 4013, 0, 0, 100, 28);
        replaceAllButton->SetText("Replace All");

        buttonLayout->AddStretch(1);

        closeButton = std::make_shared<UltraCanvasButton>("CloseBtn", 4014, 0, 0, 80, 28);
        closeButton->SetText("Close");

        buttonLayout->AddUIElement(findNextButton);
        buttonLayout->AddUIElement(replaceButton);
        buttonLayout->AddUIElement(replaceAllButton);
        buttonLayout->AddUIElement(closeButton);

        mainLayout->AddUIElement(buttonSection);
    }

    void UltraCanvasReplaceDialog::WireCallbacks() {
        // Find input - update on change
        findInput->onTextChanged = [this](const std::string& text) {
            findText = text;
            // Enable/disable buttons based on text
            bool hasText = !text.empty();
            findNextButton->SetDisabled(!hasText);
            replaceButton->SetDisabled(!hasText);
            replaceAllButton->SetDisabled(!hasText);
        };

        // Replace input - update on change
        replaceInput->onTextChanged = [this](const std::string& text) {
            replaceText = text;
        };

        // Case sensitive checkbox
        caseSensitiveCheck->onStateChanged = [this](CheckboxState oldState, CheckboxState newState) {
            caseSensitive = (newState == CheckboxState::Checked);
        };

        // Whole word checkbox
        wholeWordCheck->onStateChanged = [this](CheckboxState oldState, CheckboxState newState) {
            wholeWord = (newState == CheckboxState::Checked);
        };

        // Find Next button
        findNextButton->onClick = [this]() {
            if (onFindNext && !findText.empty()) {
                onFindNext(findText, caseSensitive, wholeWord);
            }
        };

        // Replace button
        replaceButton->onClick = [this]() {
            if (onReplace && !findText.empty()) {
                onReplace(findText, replaceText, caseSensitive, wholeWord);
            }
        };

        // Replace All button
        replaceAllButton->onClick = [this]() {
            if (onReplaceAll && !findText.empty()) {
                onReplaceAll(findText, replaceText, caseSensitive, wholeWord);
            }
        };

        // Close button
        closeButton->onClick = [this]() {
            if (onClose) {
                onClose();
            }
            CloseDialog(DialogResult::Cancel);
        };

        // Initially disable buttons if no text
        findNextButton->SetDisabled(true);
        replaceButton->SetDisabled(true);
        replaceAllButton->SetDisabled(true);
    }

    void UltraCanvasReplaceDialog::SetFindText(const std::string& text) {
        findText = text;
        if (findInput) {
            findInput->SetText(text);
        }
    }

    void UltraCanvasReplaceDialog::SetReplaceText(const std::string& text) {
        replaceText = text;
        if (replaceInput) {
            replaceInput->SetText(text);
        }
    }

    void UltraCanvasReplaceDialog::SetCaseSensitive(bool sensitive) {
        caseSensitive = sensitive;
        if (caseSensitiveCheck) {
            caseSensitiveCheck->SetCheckState(sensitive ? CheckboxState::Checked : CheckboxState::Unchecked);
        }
    }

    void UltraCanvasReplaceDialog::SetWholeWord(bool whole) {
        wholeWord = whole;
        if (wholeWordCheck) {
            wholeWordCheck->SetCheckState(whole ? CheckboxState::Checked : CheckboxState::Unchecked);
        }
    }

// ===== GO TO LINE DIALOG IMPLEMENTATION =====

    UltraCanvasGoToLineDialog::UltraCanvasGoToLineDialog()
            : lineNumber(1)
            , maxLine(1)
    {
    }

    void UltraCanvasGoToLineDialog::Initialize(int currentLine, int totalLines) {
        lineNumber = currentLine;
        maxLine = totalLines;

        // Configure dialog window
        DialogConfig config;
        config.title = "Go to Line";
        config.width = 350;
        config.height = 140;
        config.resizable = false;
        config.buttons = DialogButtons::NoButtons;

        CreateDialog(config);
        BuildLayout();
        WireCallbacks();
    }

    void UltraCanvasGoToLineDialog::BuildLayout() {
        // Create main vertical layout
        auto mainLayout = CreateVBoxLayout(this);
        mainLayout->SetSpacing(12);
        SetPadding(16);

        // ===== CONTENT SECTION =====
        contentSection = std::make_shared<UltraCanvasContainer>("GoToLineContent", 5000, 0, 0, 300, 50);
        auto contentLayout = CreateVBoxLayout(contentSection.get());
        contentLayout->SetSpacing(8);

        // Line input row
        auto lineRow = std::make_shared<UltraCanvasContainer>("LineRow", 5001, 0, 0, 300, 30);
        auto lineRowLayout = CreateHBoxLayout(lineRow.get());
        lineRowLayout->SetSpacing(8);

        lineLabel = std::make_shared<UltraCanvasLabel>("LineLabel", 5002, 0, 0, 100, 25);
        lineLabel->SetText("Line number:");
        lineLabel->SetFontSize(11);
        // lineLabel->SetTextVerticalAlignment(TextVerticalAlignement::Middle);

        lineInput = std::make_shared<UltraCanvasTextInput>("LineInput", 5003, 0, 0, 180, 25);
        lineInput->SetInputType(TextInputType::Number);
        lineInput->SetPlaceholder("1");
        lineInput->SetText(std::to_string(lineNumber));

        lineRowLayout->AddUIElement(lineLabel)->SetCrossAlignment(LayoutAlignment::Center);
        lineRowLayout->AddUIElement(lineInput)->SetStretch(1);

        contentLayout->AddUIElement(lineRow);

        mainLayout->AddUIElement(contentSection);

        // ===== BUTTON SECTION =====
        buttonSection = std::make_shared<UltraCanvasContainer>("ButtonSection", 5010, 0, 0, 300, 35);
        auto buttonLayout = CreateHBoxLayout(buttonSection.get());
        buttonLayout->SetSpacing(10);

        buttonLayout->AddStretch(1);

        goButton = std::make_shared<UltraCanvasButton>("GoBtn", 5011, 0, 0, 80, 28);
        goButton->SetText("Go");

        cancelButton = std::make_shared<UltraCanvasButton>("CancelBtn", 5012, 0, 0, 80, 28);
        cancelButton->SetText("Cancel");

        buttonLayout->AddUIElement(goButton);
        buttonLayout->AddUIElement(cancelButton);

        mainLayout->AddUIElement(buttonSection);
    }

    void UltraCanvasGoToLineDialog::WireCallbacks() {
        // Line input - update on change
        lineInput->onTextChanged = [this](const std::string& text) {
            try {
                int line = std::stoi(text);
                lineNumber = std::max(1, std::min(line, maxLine));
            } catch (...) {
                lineNumber = 1;
            }
        };

        // Go button
        goButton->onClick = [this]() {
            if (onGoToLine) {
                onGoToLine(lineNumber);
            }
            CloseDialog(DialogResult::OK);
        };

        // Cancel button
        cancelButton->onClick = [this]() {
            if (onCancel) {
                onCancel();
            }
            CloseDialog(DialogResult::Cancel);
        };

        // Enter key in line input triggers Go
        lineInput->onEnterPressed = [this](const std::string& text) {
            if (onGoToLine) {
                onGoToLine(lineNumber);
            }
            CloseDialog(DialogResult::OK);
        };
    }

    void UltraCanvasGoToLineDialog::SetLineNumber(int line) {
        lineNumber = std::max(1, std::min(line, maxLine));
        if (lineInput) {
            lineInput->SetText(std::to_string(lineNumber));
        }
    }

// ===== FACTORY FUNCTIONS =====

    std::shared_ptr<UltraCanvasFindDialog> CreateFindDialog() {
        auto dialog = std::make_shared<UltraCanvasFindDialog>();
        dialog->Initialize();
        return dialog;
    }

    std::shared_ptr<UltraCanvasReplaceDialog> CreateReplaceDialog() {
        auto dialog = std::make_shared<UltraCanvasReplaceDialog>();
        dialog->Initialize();
        return dialog;
    }

    std::shared_ptr<UltraCanvasGoToLineDialog> CreateGoToLineDialog() {
        auto dialog = std::make_shared<UltraCanvasGoToLineDialog>();
        // Initialize() will be called with parameters
        return dialog;
    }

} // namespace UltraCanvas