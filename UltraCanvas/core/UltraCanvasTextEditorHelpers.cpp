// core/UltraCanvasTextEditorHelpers.cpp
// Helper utilities for text editor components with status bar integration
// Version: 1.0.0
// Last Modified: 2025-12-20
// Author: UltraCanvas Framework

#include "UltraCanvasTextEditorHelpers.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextArea.h"
#include <memory>
#include <string>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasToolbar> CreateTextEditorStatusBar(
            const std::string& identifier,
            long id,
            std::shared_ptr<UltraCanvasTextArea> editor
    ) {
        // Create status bar using builder pattern with ID
        auto statusBar = UltraCanvasToolbarBuilder(identifier, id)
                .SetOrientation(ToolbarOrientation::Horizontal)
                .SetStyle(ToolbarStyle::StatusBar)
                .SetToolbarPosition(ToolbarPosition::Bottom)
                .SetDimensions(0, 0, 800, 24)
                .AddLabel("position", "Ln 1, Col 1")
                .AddSeparator("sep1")
                .AddLabel("encoding", "UTF-8")
                .AddSeparator("sep2")
                .AddLabel("syntax", "Plain Text")
                .AddStretch(1.0f)
                .AddLabel("selection", "")
                .Build();

        // Bind editor events to status bar updates
        if (editor) {
            // Capture statusBar as weak_ptr to avoid circular reference
            std::weak_ptr<UltraCanvasToolbar> weakStatusBar = statusBar;

            // Update position on cursor move
            // Note: SelectionChangedCallback signature is (int start, int end)
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
            // Note: SelectionChangedCallback signature is (int start, int end)
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
                            selLabel->SetText(std::to_string(charCount) + " chars");
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
        }

        return statusBar;
    }

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

} // namespace UltraCanvas