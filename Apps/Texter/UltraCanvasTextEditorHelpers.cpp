// core/UltraCanvasTextEditorHelpers.cpp
// Helper utilities for text editor components with status bar integration
// Version: 1.1.0
// Last Modified: 2026-02-05
// Author: UltraCanvas Framework

#include "UltraCanvasTextEditorHelpers.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextArea.h"
#include <memory>
#include <string>
#include <sstream>

namespace UltraCanvas {

// ===== INTERNAL COUNTING HELPERS =====

    /**
     * @brief Counts words in a text string
     *
     * A word is defined as a contiguous sequence of non-whitespace characters
     * separated by whitespace (spaces, tabs, newlines).
     *
     * @param text The text to count words in
     * @return Number of words
     */
    static int CountWords(const std::string& text) {
        if (text.empty()) return 0;

        int count = 0;
        bool inWord = false;

        for (char ch : text) {
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                inWord = false;
            } else {
                if (!inWord) {
                    count++;
                    inWord = true;
                }
            }
        }

        return count;
    }

    /**
     * @brief Counts letters (non-whitespace characters) in a text string
     *
     * Counts all characters that are not spaces, tabs, newlines, or
     * carriage returns.
     *
     * @param text The text to count letters in
     * @return Number of non-whitespace characters
     */
    static int CountLetters(const std::string& text) {
        if (text.empty()) return 0;

        int count = 0;
        for (char ch : text) {
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                count++;
            }
        }

        return count;
    }

    /**
     * @brief Updates word count and letter count labels in the status bar
     *
     * Internal helper used by the onTextChanged callback to update
     * both count labels in a single pass.
     *
     * @param statusBar Weak pointer to the status bar toolbar
     * @param text The current editor text content
     */
    static void UpdateTextCounts(const std::weak_ptr<UltraCanvasToolbar>& weakStatusBar,
                                 const std::string& text) {
        auto sb = weakStatusBar.lock();
        if (!sb) return;

        int words = CountWords(text);
        int letters = CountLetters(text);

        // Update word count label
        auto wordItem = sb->GetItem("wordcount");
        if (wordItem) {
            auto widget = wordItem->GetWidget();
            auto wordLabel = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
            if (wordLabel) {
                wordLabel->SetText("Words: " + std::to_string(words));
            }
        }

        // Update letter count label
        auto letterItem = sb->GetItem("lettercount");
        if (letterItem) {
            auto widget = letterItem->GetWidget();
            auto letterLabel = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
            if (letterLabel) {
                letterLabel->SetText("Chars: " + std::to_string(letters));
            }
        }
    }

// ===== STATUS BAR FACTORY =====

    std::shared_ptr<UltraCanvasToolbar> CreateTextEditorStatusBar(
            const std::string& identifier,
            long id,
            std::shared_ptr<UltraCanvasTextArea> editor
    ) {
        // Create status bar using builder pattern with ID
        // Layout: [position | encoding | syntax | <<<stretch>>> | words | chars | selection]
        auto statusBar = UltraCanvasToolbarBuilder(identifier, id)
                .SetOrientation(ToolbarOrientation::Horizontal)
                .SetAppearance(ToolbarAppearance::StatusBar())
                .SetToolbarPosition(ToolbarPosition::Bottom)
                .SetDimensions(0, 0, 800, 24)
                .AddLabel("position", "Ln 1, Col 1")
                .AddSeparator("sep1")
                .AddLabel("encoding", "UTF-8")
                .AddSeparator("sep2")
                .AddLabel("syntax", "Plain Text")
                .AddStretch(1.0f)
                .AddLabel("wordcount", "Words: 0")
                .AddSeparator("sep3")
                .AddLabel("lettercount", "Chars: 0")
                .AddSeparator("sep4")
                .AddLabel("selection", "")
                .Build();

        // Bind editor events to status bar updates
        if (editor) {
            // Capture statusBar as weak_ptr to avoid circular reference
            std::weak_ptr<UltraCanvasToolbar> weakStatusBar = statusBar;

            // Update position on cursor move
            editor->SetOnCursorPositionChanged([weakStatusBar](int line, int col) {
                auto sb = weakStatusBar.lock();
                if (!sb) return;

                auto item = sb->GetItem("position");
                if (item) {
                    auto widget = item->GetWidget();
                    auto posLabel = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
                    if (posLabel) {
                        posLabel->SetText("Ln " + std::to_string(line + 1) +
                                          ", Col " + std::to_string(col + 1));
                    }
                }
            });

            // Update encoding display when encoding changes
//            editor->SetOnEncodingChange([weakStatusBar](const std::string& encoding) {
//                auto sb = weakStatusBar.lock();
//                if (!sb) return;
//
//                auto item = sb->GetItem("encoding");
//                if (item) {
//                    auto widget = item->GetWidget();
//                    auto encLabel = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
//                    if (encLabel) {
//                        encLabel->SetText(encoding);
//                    }
//                }
//            });

            // Update selection count
            std::weak_ptr<UltraCanvasTextArea> weakEditor = editor;
            editor->SetOnSelectionChanged([weakStatusBar, weakEditor](int start, int end) {
                auto sb = weakStatusBar.lock();
                auto ed = weakEditor.lock();
                if (!sb || !ed) return;

                auto item = sb->GetItem("selection");
                if (item) {
                    auto widget = item->GetWidget();
                    auto selLabel = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
                    if (selLabel) {
                        if (start != end && start >= 0 && end >= 0) {
                            int charCount = std::abs(end - start);
                            selLabel->SetText(std::to_string(charCount) + " sel");
                        } else {
                            selLabel->SetText("");
                        }
                    }
                }
            });

            // Set initial position from current cursor
            auto [currentLine, currentCol] = editor->GetLineColumnFromPosition(editor->GetCursorPosition());
            auto posItem = statusBar->GetItem("position");
            if (posItem) {
                auto widget = posItem->GetWidget();
                auto posLabel = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
                if (posLabel) {
                    posLabel->SetText("Ln " + std::to_string(currentLine + 1) +
                                      ", Col " + std::to_string(currentCol + 1));
                }
            }

            // Set initial encoding
//            auto encItem = statusBar->GetItem("encoding");
//            if (encItem) {
//                auto widget = encItem->GetWidget();
//                auto encLabel = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
//                if (encLabel) {
//                    encLabel->SetText(editor->GetCharsetEncoding());
//                }
//            }

            // Set initial word/letter counts from current text
            UpdateTextCounts(weakStatusBar, editor->GetText());
        }

        return statusBar;
    }

// ===== PUBLIC UPDATE HELPERS =====

    void UpdateStatusBarSyntaxMode(
            std::shared_ptr<UltraCanvasToolbar> statusBar,
            const std::string& syntaxMode
    ) {
        if (!statusBar) return;

        auto item = statusBar->GetItem("syntax");
        if (item) {
            auto widget = item->GetWidget();
            auto syntaxLabel = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
            if (syntaxLabel) {
                syntaxLabel->SetText(syntaxMode);
            }
        }
    }

    void UpdateStatusBarLineEnding(
            std::shared_ptr<UltraCanvasToolbar> statusBar,
            const std::string& lineEnding
    ) {
        if (!statusBar) return;

        auto item = statusBar->GetItem("lineending");
        if (item) {
            auto widget = item->GetWidget();
            auto lineEndLabel = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
            if (lineEndLabel) {
                lineEndLabel->SetText(lineEnding);
            }
        }
    }

    void UpdateStatusBarEncoding(
            std::shared_ptr<UltraCanvasToolbar> statusBar,
            const std::string& encoding
    ) {
        if (!statusBar) return;

        auto item = statusBar->GetItem("encoding");
        if (item) {
            auto widget = item->GetWidget();
            auto encLabel = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
            if (encLabel) {
                encLabel->SetText(encoding);
            }
        }
    }

    void UpdateStatusBarWordCount(
            std::shared_ptr<UltraCanvasToolbar> statusBar,
            int wordCount
    ) {
        if (!statusBar) return;

        auto item = statusBar->GetItem("wordcount");
        if (item) {
            auto widget = item->GetWidget();
            auto wordLabel = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
            if (wordLabel) {
                wordLabel->SetText("Words: " + std::to_string(wordCount));
            }
        }
    }

    void UpdateStatusBarLetterCount(
            std::shared_ptr<UltraCanvasToolbar> statusBar,
            int letterCount
    ) {
        if (!statusBar) return;

        auto item = statusBar->GetItem("lettercount");
        if (item) {
            auto widget = item->GetWidget();
            auto letterLabel = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
            if (letterLabel) {
                letterLabel->SetText("Chars: " + std::to_string(letterCount));
            }
        }
    }

} // namespace UltraCanvas