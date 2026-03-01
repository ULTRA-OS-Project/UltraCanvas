// Apps/Texter/UltraCanvasTextEditorDialogs.cpp
// Implementation of custom dialogs for Find and Replace
// Version: 1.1.0
// Last Modified: 2026-02-04
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
        config.width = 480;
        config.height = 210;
        config.resizable = false;
        config.buttons = DialogButtons::NoButtons; // We'll add custom buttons

        CreateDialog(config);
        BuildLayout();
        WireCallbacks();
    }

    void UltraCanvasFindDialog::BuildLayout() {
        auto mainLayout = CreateVBoxLayout(this);
        mainLayout->SetSpacing(12);
        SetPadding(16);

        // ===== CONTENT SECTION =====
        contentSection = std::make_shared<UltraCanvasContainer>("FindContent", 3000, 0, 0, 420, 100);
        auto contentLayout = CreateVBoxLayout(contentSection.get());
        contentLayout->SetSpacing(8);

        // Search input row — with history dropdown button
        auto searchRow = std::make_shared<UltraCanvasContainer>("SearchRow", 3001, 0, 0, 420, 30);
        auto searchRowLayout = CreateHBoxLayout(searchRow.get());
        searchRowLayout->SetSpacing(4);

        searchLabel = std::make_shared<UltraCanvasLabel>("SearchLabel", 3002, 0, 0, 80, 25);
        searchLabel->SetText("Find what:");
        searchLabel->SetFontSize(11);

        searchInput = std::make_shared<UltraCanvasTextInput>("SearchInput", 3003, 0, 0, 290, 25);
        searchInput->SetPlaceholder("Enter search text...");

        // *** FIX: Disable the validation checkmark icon ***
        searchInput->SetShowValidationState(false);

        // History dropdown button (small arrow button)
        auto historyButton = std::make_shared<UltraCanvasButton>("SearchHistoryBtn", 3004, 0, 0, 22, 25);
        historyButton->SetText("▼");
        historyButton->SetFontSize(8);

        searchRowLayout->AddUIElement(searchLabel);
        searchRowLayout->AddUIElement(searchInput);
        searchRowLayout->AddUIElement(historyButton);

        contentLayout->AddUIElement(searchRow);

        // Options row
        auto optionsRow = std::make_shared<UltraCanvasContainer>("OptionsRow", 3005, 0, 0, 420, 25);
        auto optionsLayout = CreateHBoxLayout(optionsRow.get());
        optionsLayout->SetSpacing(20);

        caseSensitiveCheck = std::make_shared<UltraCanvasCheckbox>("CaseSensitive", 3006, 0, 0, 130, 20);
        caseSensitiveCheck->SetText("Case sensitive");
        caseSensitiveCheck->SetFontSize(11);

        wholeWordCheck = std::make_shared<UltraCanvasCheckbox>("WholeWord", 3007, 0, 0, 130, 20);
        wholeWordCheck->SetText("Whole word");
        wholeWordCheck->SetFontSize(11);

        optionsLayout->AddUIElement(caseSensitiveCheck);
        optionsLayout->AddUIElement(wholeWordCheck);

        contentLayout->AddUIElement(optionsRow);

        // Status label (from previous change set)
        statusLabel = std::make_shared<UltraCanvasLabel>("FindStatus", 3020, 0, 0, 420, 20);
        statusLabel->SetText("");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(100, 100, 100));
        statusLabel->SetAlignment(TextAlignment::Left);
        contentLayout->AddUIElement(statusLabel);

        mainLayout->AddUIElement(contentSection);

        // ===== BUTTON SECTION =====
        // *** FIX: Use wider minimum button width for i18n ***
        const int buttonMinWidth = 120;  // Was 80-100, now 120 for German/Russian
        const int buttonHeight = 28;

        buttonSection = std::make_shared<UltraCanvasContainer>("ButtonSection", 3010, 0, 0, 420, 35);
        auto buttonLayout = CreateHBoxLayout(buttonSection.get());
        buttonLayout->SetSpacing(8);

        buttonLayout->AddStretch(1);

        findNextButton = std::make_shared<UltraCanvasButton>("FindNext", 3011, 0, 0, buttonMinWidth, buttonHeight);
        findNextButton->SetText("Find Next");
        findNextButton->SetAutoResize(true);        // Auto-expand to fit text
        //findNextButton->SetMinWidth(buttonMinWidth);

        findPreviousButton = std::make_shared<UltraCanvasButton>("FindPrev", 3012, 0, 0, buttonMinWidth, buttonHeight);
        findPreviousButton->SetText("Find Previous");
        findPreviousButton->SetAutoResize(true);
        //findPreviousButton->SetMinWidth(buttonMinWidth);

        closeButton = std::make_shared<UltraCanvasButton>("CloseBtn", 3013, 0, 0, 80, buttonHeight);
        closeButton->SetText("Close");

        buttonLayout->AddUIElement(findPreviousButton);
        buttonLayout->AddUIElement(findNextButton);
        buttonLayout->AddUIElement(closeButton);

        mainLayout->AddUIElement(buttonSection);

        // Create the history dropdown (hidden initially, positioned on demand)
        historyDropdown = std::make_shared<UltraCanvasDropdown>("SearchHistoryDropdown", 3030, 0, 0, 290, 24);
        historyDropdown->SetVisible(false);
    }

    void UltraCanvasFindDialog::AddToSearchHistory(const std::string& text) {
        if (text.empty()) return;

        // Remove duplicate if exists (move to top)
        auto it = std::find(searchHistory.begin(), searchHistory.end(), text);
        if (it != searchHistory.end()) {
            searchHistory.erase(it);
        }

        // Insert at front (most recent first)
        searchHistory.insert(searchHistory.begin(), text);

        // Trim to max size
        if (static_cast<int>(searchHistory.size()) > maxHistoryItems) {
            searchHistory.resize(maxHistoryItems);
        }
    }

    void UltraCanvasFindDialog::SetSearchHistory(const std::vector<std::string>& history) {
        searchHistory = history;
    }

    void UltraCanvasFindDialog::ShowSearchHistory() {
        if (searchHistory.empty() || !historyDropdown) return;

        // Populate dropdown with history items
        historyDropdown->ClearItems();
        for (const auto& item : searchHistory) {
            historyDropdown->AddItem(item);
        }

        // Position below the search input
        if (searchInput) {
            auto inputBounds = searchInput->GetBounds();
            historyDropdown->SetPosition(inputBounds.x, inputBounds.y + inputBounds.height + 2);
            historyDropdown->SetSize(inputBounds.width, 24);
        }

        // Wire selection callback
        historyDropdown->onSelectionChanged = [this](int index, const DropdownItem& item) {
            if (searchInput) {
                searchInput->SetText(item.text);
                searchText = item.text;
            }
            historyDropdown->SetVisible(false);
        };

        historyDropdown->SetVisible(true);
        historyDropdown->OpenDropdown();
    }

    void UltraCanvasFindDialog::WireCallbacks() {
        // Search input text change
        searchInput->onTextChanged = [this](const std::string& text) {
            searchText = text;
            bool hasText = !text.empty();
            findNextButton->SetDisabled(!hasText);
            findPreviousButton->SetDisabled(!hasText);

            // Clear status when search text changes
            if (!hasText && statusLabel) {
                statusLabel->SetText("");
            }
        };

        // Case sensitive checkbox
        caseSensitiveCheck->onStateChanged = [this](CheckboxState oldState, CheckboxState newState) {
            caseSensitive = (newState == CheckboxState::Checked);
        };

        // Whole word checkbox
        wholeWordCheck->onStateChanged = [this](CheckboxState oldState, CheckboxState newState) {
            wholeWord = (newState == CheckboxState::Checked);
        };

        // Find Next — adds to history
        findNextButton->onClick = [this]() {
            if (onFindNext && !searchText.empty()) {
                AddToSearchHistory(searchText);
                onFindNext(searchText, caseSensitive, wholeWord);
            }
        };

        // Find Previous — adds to history
        findPreviousButton->onClick = [this]() {
            if (onFindPrevious && !searchText.empty()) {
                AddToSearchHistory(searchText);
                onFindPrevious(searchText, caseSensitive, wholeWord);
            }
        };

        // Close button
        closeButton->onClick = [this]() {
            CloseDialog(DialogResult::Cancel);
        };

        // History dropdown button (ID 3004)
        // Find the button by iterating contentSection children, or use a member pointer
        // Since we created it as a local in BuildLayout, we need to store it as a member
        // OR wire it up right after creation in BuildLayout.
        // See CHANGE 3 addendum below.

        // Initially disable buttons
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

    void UltraCanvasFindDialog::ShowModal(UltraCanvasWindowBase* parent) {
        // Call base class implementation
        UltraCanvasModalDialog::ShowModal(parent);
        
        // Set focus on search input after dialog is shown
        if (searchInput) {
            searchInput->SetFocus(true);
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
        config.width = 530;
        config.height = 260;
        config.resizable = false;
        config.buttons = DialogButtons::NoButtons;

        CreateDialog(config);
        BuildLayout();
        WireCallbacks();
    }

    void UltraCanvasReplaceDialog::BuildLayout() {
        auto mainLayout = CreateVBoxLayout(this);
        mainLayout->SetSpacing(12);
        SetPadding(16);

        // ===== CONTENT SECTION =====
        contentSection = std::make_shared<UltraCanvasContainer>("ReplaceContent", 4000, 0, 0, 470, 150);
        auto contentLayout = CreateVBoxLayout(contentSection.get());
        contentLayout->SetSpacing(8);

        // ----- Find input row with history button -----
        auto findRow = std::make_shared<UltraCanvasContainer>("FindRow", 4001, 0, 0, 470, 30);
        auto findRowLayout = CreateHBoxLayout(findRow.get());
        findRowLayout->SetSpacing(4);

        findLabel = std::make_shared<UltraCanvasLabel>("FindLabel", 4002, 0, 0, 80, 25);
        findLabel->SetText("Find what:");
        findLabel->SetFontSize(11);

        findInput = std::make_shared<UltraCanvasTextInput>("FindInput", 4003, 0, 0, 330, 25);
        findInput->SetPlaceholder("Enter search text...");

        // *** FIX: Disable the validation checkmark icon ***
        findInput->SetShowValidationState(false);

        // History dropdown button
        auto findHistoryBtn = std::make_shared<UltraCanvasButton>("FindHistoryBtn", 4004, 0, 0, 22, 25);
        findHistoryBtn->SetText("▼");
        findHistoryBtn->SetFontSize(8);
        findHistoryBtn->onClick = [this]() { ShowFindHistory(); };

        findRowLayout->AddUIElement(findLabel);
        findRowLayout->AddUIElement(findInput);
        findRowLayout->AddUIElement(findHistoryBtn);

        contentLayout->AddUIElement(findRow);

        // ----- Replace input row with history button -----
        auto replaceRow = std::make_shared<UltraCanvasContainer>("ReplaceRow", 4005, 0, 0, 470, 30);
        auto replaceRowLayout = CreateHBoxLayout(replaceRow.get());
        replaceRowLayout->SetSpacing(4);

        replaceLabel = std::make_shared<UltraCanvasLabel>("ReplaceLabel", 4006, 0, 0, 80, 25);
        replaceLabel->SetText("Replace:");
        replaceLabel->SetFontSize(11);

        replaceInput = std::make_shared<UltraCanvasTextInput>("ReplaceInput", 4007, 0, 0, 330, 25);
        replaceInput->SetPlaceholder("Enter replacement text...");

        // *** FIX: Disable the validation checkmark icon ***
        replaceInput->SetShowValidationState(false);

        // History dropdown button
        auto replaceHistoryBtn = std::make_shared<UltraCanvasButton>("ReplaceHistoryBtn", 4008, 0, 0, 22, 25);
        replaceHistoryBtn->SetText("▼");
        replaceHistoryBtn->SetFontSize(8);
        replaceHistoryBtn->onClick = [this]() { ShowReplaceHistory(); };

        replaceRowLayout->AddUIElement(replaceLabel);
        replaceRowLayout->AddUIElement(replaceInput);
        replaceRowLayout->AddUIElement(replaceHistoryBtn);

        contentLayout->AddUIElement(replaceRow);

        // ----- Options row -----
        auto optionsRow = std::make_shared<UltraCanvasContainer>("OptionsRow", 4009, 0, 0, 470, 25);
        auto optionsLayout = CreateHBoxLayout(optionsRow.get());
        optionsLayout->SetSpacing(20);

        caseSensitiveCheck = std::make_shared<UltraCanvasCheckbox>("CaseSensitive", 4010, 0, 0, 130, 20);
        caseSensitiveCheck->SetText("Case sensitive");
        caseSensitiveCheck->SetFontSize(11);

        wholeWordCheck = std::make_shared<UltraCanvasCheckbox>("WholeWord", 4011, 0, 0, 130, 20);
        wholeWordCheck->SetText("Whole word");
        wholeWordCheck->SetFontSize(11);

        optionsLayout->AddUIElement(caseSensitiveCheck);
        optionsLayout->AddUIElement(wholeWordCheck);

        contentLayout->AddUIElement(optionsRow);

        // ----- Status label -----
        statusLabel = std::make_shared<UltraCanvasLabel>("ReplaceStatus", 4020, 0, 0, 470, 20);
        statusLabel->SetText("");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(100, 100, 100));
        statusLabel->SetAlignment(TextAlignment::Left);
        contentLayout->AddUIElement(statusLabel);

        mainLayout->AddUIElement(contentSection);

        // ===== BUTTON SECTION =====
        // *** FIX: Use wider buttons for German/Russian translations ***
        const int buttonMinWidth = 120;  // Fits "Nächstes suchen", "Alle ersetzen", "Заменить все"
        const int buttonHeight = 28;

        buttonSection = std::make_shared<UltraCanvasContainer>("ButtonSection", 4030, 0, 0, 490, 35);
        auto buttonLayout = CreateHBoxLayout(buttonSection.get());
        buttonLayout->SetSpacing(8);

        buttonLayout->AddStretch(1);

        findNextButton = std::make_shared<UltraCanvasButton>("FindNext", 4031, 0, 0, buttonMinWidth, buttonHeight);
        findNextButton->SetText("Find Next");
        findNextButton->SetAutoResize(true);
        //findNextButton->SetMinWidth(buttonMinWidth);

        replaceButton = std::make_shared<UltraCanvasButton>("Replace", 4032, 0, 0, buttonMinWidth, buttonHeight);
        replaceButton->SetText("Replace");
        replaceButton->SetAutoResize(true);
        //replaceButton->SetMinWidth(buttonMinWidth);

        replaceAllButton = std::make_shared<UltraCanvasButton>("ReplaceAll", 4033, 0, 0, buttonMinWidth, buttonHeight);
        replaceAllButton->SetText("Replace All");
        replaceAllButton->SetAutoResize(true);
        //replaceAllButton->SetMinWidth(buttonMinWidth);

        closeButton = std::make_shared<UltraCanvasButton>("CloseBtn", 4034, 0, 0, 80, buttonHeight);
        closeButton->SetText("Close");

        buttonLayout->AddUIElement(findNextButton);
        buttonLayout->AddUIElement(replaceButton);
        buttonLayout->AddUIElement(replaceAllButton);
        buttonLayout->AddUIElement(closeButton);

        mainLayout->AddUIElement(buttonSection);

        // Create history dropdowns (hidden, positioned on demand)
        findHistoryDropdown = std::make_shared<UltraCanvasDropdown>("FindHistoryDrop", 4050, 0, 0, 330, 24);
        findHistoryDropdown->SetVisible(false);

        replaceHistoryDropdown = std::make_shared<UltraCanvasDropdown>("ReplaceHistoryDrop", 4051, 0, 0, 330, 24);
        replaceHistoryDropdown->SetVisible(false);
    }

    void UltraCanvasReplaceDialog::WireCallbacks() {
        // Find input text change
        findInput->onTextChanged = [this](const std::string& text) {
            findText = text;
            bool hasText = !text.empty();
            findNextButton->SetDisabled(!hasText);
            replaceButton->SetDisabled(!hasText);
            replaceAllButton->SetDisabled(!hasText);

            if (!hasText && statusLabel) {
                statusLabel->SetText("");
            }
        };

        // Replace input text change
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

        // Find Next — adds to history
        findNextButton->onClick = [this]() {
            if (onFindNext && !findText.empty()) {
                AddToFindHistory(findText);
                onFindNext(findText, caseSensitive, wholeWord);
            }
        };

        // Replace — adds both to history
        replaceButton->onClick = [this]() {
            if (onReplace && !findText.empty()) {
                AddToFindHistory(findText);
                if (!replaceText.empty()) {
                    AddToReplaceHistory(replaceText);
                }
                onReplace(findText, replaceText, caseSensitive, wholeWord);
            }
        };

        // Replace All — adds both to history
        replaceAllButton->onClick = [this]() {
            if (onReplaceAll && !findText.empty()) {
                AddToFindHistory(findText);
                if (!replaceText.empty()) {
                    AddToReplaceHistory(replaceText);
                }
                onReplaceAll(findText, replaceText, caseSensitive, wholeWord);
            }
        };

        // Close
        closeButton->onClick = [this]() {
            CloseDialog(DialogResult::Cancel);
        };

        // Initially disable buttons
        findNextButton->SetDisabled(true);
        replaceButton->SetDisabled(true);
        replaceAllButton->SetDisabled(true);
    }

    void UltraCanvasReplaceDialog::AddToFindHistory(const std::string& text) {
        if (text.empty()) return;
        auto it = std::find(findHistory.begin(), findHistory.end(), text);
        if (it != findHistory.end()) {
            findHistory.erase(it);
        }
        findHistory.insert(findHistory.begin(), text);
        if (static_cast<int>(findHistory.size()) > maxHistoryItems) {
            findHistory.resize(maxHistoryItems);
        }
    }

    void UltraCanvasReplaceDialog::AddToReplaceHistory(const std::string& text) {
        if (text.empty()) return;
        auto it = std::find(replaceHistory.begin(), replaceHistory.end(), text);
        if (it != replaceHistory.end()) {
            replaceHistory.erase(it);
        }
        replaceHistory.insert(replaceHistory.begin(), text);
        if (static_cast<int>(replaceHistory.size()) > maxHistoryItems) {
            replaceHistory.resize(maxHistoryItems);
        }
    }

    void UltraCanvasReplaceDialog::SetFindHistory(const std::vector<std::string>& history) {
        findHistory = history;
    }

    void UltraCanvasReplaceDialog::SetReplaceHistory(const std::vector<std::string>& history) {
        replaceHistory = history;
    }

    void UltraCanvasReplaceDialog::ShowFindHistory() {
        if (findHistory.empty() || !findHistoryDropdown) return;

        findHistoryDropdown->ClearItems();
        for (const auto& item : findHistory) {
            findHistoryDropdown->AddItem(item);
        }

        if (findInput) {
            auto inputBounds = findInput->GetBounds();
            findHistoryDropdown->SetPosition(inputBounds.x, inputBounds.y + inputBounds.height + 2);
            findHistoryDropdown->SetSize(inputBounds.width, 24);
        }

        findHistoryDropdown->onSelectionChanged = [this](int index, const DropdownItem& item) {
            if (findInput) {
                findInput->SetText(item.text);
                findText = item.text;
            }
            findHistoryDropdown->SetVisible(false);
        };

        findHistoryDropdown->SetVisible(true);
        findHistoryDropdown->OpenDropdown();
    }

    void UltraCanvasReplaceDialog::ShowReplaceHistory() {
        if (replaceHistory.empty() || !replaceHistoryDropdown) return;

        replaceHistoryDropdown->ClearItems();
        for (const auto& item : replaceHistory) {
            replaceHistoryDropdown->AddItem(item);
        }

        if (replaceInput) {
            auto inputBounds = replaceInput->GetBounds();
            replaceHistoryDropdown->SetPosition(inputBounds.x, inputBounds.y + inputBounds.height + 2);
            replaceHistoryDropdown->SetSize(inputBounds.width, 24);
        }

        replaceHistoryDropdown->onSelectionChanged = [this](int index, const DropdownItem& item) {
            if (replaceInput) {
                replaceInput->SetText(item.text);
                replaceText = item.text;
            }
            replaceHistoryDropdown->SetVisible(false);
        };

        replaceHistoryDropdown->SetVisible(true);
        replaceHistoryDropdown->OpenDropdown();
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

    void UltraCanvasReplaceDialog::ShowModal(UltraCanvasWindowBase* parent) {
        // Call base class implementation
        UltraCanvasModalDialog::ShowModal(parent);
        
        // Set focus on find input after dialog is shown
        if (findInput) {
            findInput->SetFocus(true);
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

    void UltraCanvasGoToLineDialog::ShowModal(UltraCanvasWindowBase* parent) {
        // Call base class implementation
        UltraCanvasModalDialog::ShowModal(parent);
        
        // Set focus on line input and select all text for easy replacement
        if (lineInput) {
            lineInput->SetFocus(true);
            lineInput->SelectAll();
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