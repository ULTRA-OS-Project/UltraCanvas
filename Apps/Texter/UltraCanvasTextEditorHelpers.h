// include/UltraCanvasTextEditorHelpers.h
// Helper utilities for text editor components with status bar integration
// Version: 1.0.0
// Last Modified: 2025-12-20
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasToolbar.h"
#include "UltraCanvasTextArea.h"
#include <memory>
#include <string>

namespace UltraCanvas {

/**
 * @brief Creates a comprehensive status bar for text editor components
 *
 * This factory function creates a toolbar configured as a status bar with
 * editor-specific information displays including:
 * - Cursor position (Line, Column)
 * - Character encoding (UTF-8, ASCII, etc.)
 * - Syntax highlighting mode
 * - Selection character count
 *
 * The status bar automatically binds to the editor's events and updates
 * information in real-time.
 *
 * @param identifier Unique identifier for the status bar
 * @param id Numeric ID for the status bar
 * @param editor Shared pointer to the text editor to monitor
 * @return Configured status bar toolbar ready to display
 *
 * @example
 * auto editor = CreateCodeEditor("editor", 1, 0, 0, 800, 570);
 * auto statusBar = CreateTextEditorStatusBar("statusBar", 2, editor);
 * statusBar->SetBounds(Rect2Di(0, 570, 800, 24));
 */
    std::shared_ptr<UltraCanvasToolbar> CreateTextEditorStatusBar(
            const std::string& identifier,
            long id,
            std::shared_ptr<UltraCanvasTextArea> editor
    );

/**
 * @brief Updates the syntax mode label in a text editor status bar
 *
 * Helper function to update the syntax/language display after changing
 * the programming language or file type.
 *
 * @param statusBar The status bar to update
 * @param syntaxMode The new syntax mode (e.g., "C++", "Python", "Markdown")
 */
    void UpdateStatusBarSyntaxMode(
            std::shared_ptr<UltraCanvasToolbar> statusBar,
            const std::string& syntaxMode
    );

/**
 * @brief Updates the line ending style in a text editor status bar
 *
 * Helper function to add/update line ending information (CRLF/LF/CR)
 *
 * @param statusBar The status bar to update
 * @param lineEnding The line ending style ("CRLF", "LF", "CR")
 */
    void UpdateStatusBarLineEnding(
            std::shared_ptr<UltraCanvasToolbar> statusBar,
            const std::string& lineEnding
    );

/**
 * @brief Updates the encoding label in a text editor status bar
 *
 * Helper function to update the encoding display
 *
 * @param statusBar The status bar to update
 * @param encoding The encoding string (e.g., "UTF-8", "ASCII")
 */
    void UpdateStatusBarEncoding(
            std::shared_ptr<UltraCanvasToolbar> statusBar,
            const std::string& encoding
    );

} // namespace UltraCanvas