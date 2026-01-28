// Apps/Texter/UltraCanvasTextEditorDialogs.h
// Custom dialogs for Find and Replace functionality
// Version: 1.0.0
// Last Modified: 2026-01-26
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <string>
#include <functional>

namespace UltraCanvas {

// ===== FIND DIALOG =====

/**
 * @brief Find dialog with search functionality
 *
 * Features:
 * - Search text input
 * - Case sensitive option
 * - Whole word option
 * - Next/Previous buttons
 * - Close button
 */
    class UltraCanvasFindDialog : public UltraCanvasModalDialog {
    private:
        // UI Components
        std::shared_ptr<UltraCanvasContainer> contentSection;
        std::shared_ptr<UltraCanvasLabel> searchLabel;
        std::shared_ptr<UltraCanvasTextInput> searchInput;
        std::shared_ptr<UltraCanvasCheckbox> caseSensitiveCheck;
        std::shared_ptr<UltraCanvasCheckbox> wholeWordCheck;

        std::shared_ptr<UltraCanvasContainer> buttonSection;
        std::shared_ptr<UltraCanvasButton> findNextButton;
        std::shared_ptr<UltraCanvasButton> findPreviousButton;
        std::shared_ptr<UltraCanvasButton> closeButton;

        // State
        std::string searchText;
        bool caseSensitive;
        bool wholeWord;

        // Layout
        void BuildLayout();
        void WireCallbacks();

    public:
        // Constructor
        UltraCanvasFindDialog();

        // Setup
        void Initialize();

        // Getters
        std::string GetSearchText() const { return searchText; }
        bool IsCaseSensitive() const { return caseSensitive; }
        bool IsWholeWord() const { return wholeWord; }

        // Setters
        void SetSearchText(const std::string& text);
        void SetCaseSensitive(bool sensitive);
        void SetWholeWord(bool whole);

        // Callbacks
        std::function<void(const std::string&, bool, bool)> onFindNext;
        std::function<void(const std::string&, bool, bool)> onFindPrevious;
        std::function<void()> onClose;
    };

// ===== REPLACE DIALOG =====

/**
 * @brief Replace dialog with find and replace functionality
 *
 * Features:
 * - Find text input
 * - Replace text input
 * - Case sensitive option
 * - Whole word option
 * - Find Next button
 * - Replace button
 * - Replace All button
 * - Close button
 */
    class UltraCanvasReplaceDialog : public UltraCanvasModalDialog {
    private:
        // UI Components
        std::shared_ptr<UltraCanvasContainer> contentSection;
        std::shared_ptr<UltraCanvasLabel> findLabel;
        std::shared_ptr<UltraCanvasTextInput> findInput;
        std::shared_ptr<UltraCanvasLabel> replaceLabel;
        std::shared_ptr<UltraCanvasTextInput> replaceInput;
        std::shared_ptr<UltraCanvasCheckbox> caseSensitiveCheck;
        std::shared_ptr<UltraCanvasCheckbox> wholeWordCheck;

        std::shared_ptr<UltraCanvasContainer> buttonSection;
        std::shared_ptr<UltraCanvasButton> findNextButton;
        std::shared_ptr<UltraCanvasButton> replaceButton;
        std::shared_ptr<UltraCanvasButton> replaceAllButton;
        std::shared_ptr<UltraCanvasButton> closeButton;

        // State
        std::string findText;
        std::string replaceText;
        bool caseSensitive;
        bool wholeWord;

        // Layout
        void BuildLayout();
        void WireCallbacks();

    public:
        // Constructor
        UltraCanvasReplaceDialog();

        // Setup
        void Initialize();

        // Getters
        std::string GetFindText() const { return findText; }
        std::string GetReplaceText() const { return replaceText; }
        bool IsCaseSensitive() const { return caseSensitive; }
        bool IsWholeWord() const { return wholeWord; }

        // Setters
        void SetFindText(const std::string& text);
        void SetReplaceText(const std::string& text);
        void SetCaseSensitive(bool sensitive);
        void SetWholeWord(bool whole);

        // Callbacks
        std::function<void(const std::string&, bool, bool)> onFindNext;
        std::function<void(const std::string&, const std::string&, bool, bool)> onReplace;
        std::function<void(const std::string&, const std::string&, bool, bool)> onReplaceAll;
        std::function<void()> onClose;
    };

// ===== GO TO LINE DIALOG =====

/**
 * @brief Go to line dialog
 *
 * Features:
 * - Line number input
 * - Go button
 * - Cancel button
 */
    class UltraCanvasGoToLineDialog : public UltraCanvasModalDialog {
    private:
        // UI Components
        std::shared_ptr<UltraCanvasContainer> contentSection;
        std::shared_ptr<UltraCanvasLabel> lineLabel;
        std::shared_ptr<UltraCanvasTextInput> lineInput;

        std::shared_ptr<UltraCanvasContainer> buttonSection;
        std::shared_ptr<UltraCanvasButton> goButton;
        std::shared_ptr<UltraCanvasButton> cancelButton;

        // State
        int lineNumber;
        int maxLine;

        // Layout
        void BuildLayout();
        void WireCallbacks();

    public:
        // Constructor
        UltraCanvasGoToLineDialog();

        // Setup
        void Initialize(int currentLine, int totalLines);

        // Getters
        int GetLineNumber() const { return lineNumber; }

        // Setters
        void SetLineNumber(int line);
        void SetMaxLine(int max) { maxLine = max; }

        // Callbacks
        std::function<void(int)> onGoToLine;
        std::function<void()> onCancel;
    };

// ===== FACTORY FUNCTIONS =====

    std::shared_ptr<UltraCanvasFindDialog> CreateFindDialog();
    std::shared_ptr<UltraCanvasReplaceDialog> CreateReplaceDialog();
    std::shared_ptr<UltraCanvasGoToLineDialog> CreateGoToLineDialog();

} // namespace UltraCanvas