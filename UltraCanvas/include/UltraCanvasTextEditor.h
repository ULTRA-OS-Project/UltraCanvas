// include/UltraCanvasTextEditor.h
// Complete text editor application with menu bar, toolbar, editor, and status bar
// Version: 1.0.0
// Last Modified: 2025-12-20
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextEditorHelpers.h"
#include <memory>
#include <string>
#include <functional>

namespace UltraCanvas {

// Forward declarations
    class UltraCanvasTextEditor;

/**
 * @brief Configuration options for the text editor application
 */
    struct TextEditorConfig {
        // Window settings
        std::string title = "Ultra Text Editor";
        int width = 1024;
        int height = 768;

        // Feature toggles
        bool showMenuBar = true;
        bool showToolbar = true;
        bool showStatusBar = true;
        bool showLineNumbers = true;

        // Editor settings
        std::string defaultLanguage = "Plain Text";
        bool darkTheme = false;
        std::string defaultEncoding = "UTF-8";

        // File filters for Open/Save dialogs
        std::vector<FileFilter> fileFilters = {
                FileFilter("All Files", "*"),
                FileFilter("Text Files", {"txt", "log", "md", "ini", "cfg"}),
                FileFilter("Source Code", {"cpp", "c", "h", "hpp", "cc", "cxx", "py", "js", "ts", "java", "cs", "go", "rs", "pas", "pp"}),
                FileFilter("Web Files", {"html", "htm", "css", "xml", "json"}),
                FileFilter("Script Files", {"sh", "bash", "bat", "cmd", "ps1"})
        };
    };

/**
 * @brief Complete text editor application component
 *
 * This component provides a full-featured text editor with:
 * - Menu bar (File, Edit, Info)
 * - Optional toolbar
 * - Syntax-highlighted text area
 * - Status bar (position, encoding, syntax, selection)
 *
 * @example
 * auto editor = CreateTextEditor("MyEditor", 1, 0, 0, 1024, 768);
 * editor->LoadFile("/path/to/file.cpp");
 * window->AddChild(editor);
 */
    class UltraCanvasTextEditor : public UltraCanvasContainer {
    public:
        // Constructor
        UltraCanvasTextEditor(const std::string& identifier, long id,
                              int x, int y, int width, int height,
                              const TextEditorConfig& config = TextEditorConfig());

        virtual ~UltraCanvasTextEditor() = default;

        // ===== FILE OPERATIONS =====

        /**
         * @brief Load a file into the editor
         * @param filePath Path to the file to load
         * @return true if file was loaded successfully
         */
        bool LoadFile(const std::string& filePath);

        /**
         * @brief Save the current content to the current file
         * @return true if file was saved successfully
         */
        bool SaveFile();

        /**
         * @brief Save the current content to a new file
         * @param filePath Path to save the file to
         * @return true if file was saved successfully
         */
        bool SaveFileAs(const std::string& filePath);

        /**
         * @brief Create a new empty document
         */
        void NewFile();

        /**
         * @brief Get the current file path
         * @return Current file path, empty if no file is open
         */
        std::string GetCurrentFilePath() const { return currentFilePath; }

        /**
         * @brief Check if the document has unsaved changes
         * @return true if there are unsaved changes
         */
        bool HasUnsavedChanges() const { return isModified; }

        // ===== EDITOR ACCESS =====

        /**
         * @brief Get the text editor component
         * @return Shared pointer to the text area
         */
        std::shared_ptr<UltraCanvasTextArea> GetEditor() { return textArea; }

        /**
         * @brief Get the current text content
         * @return Text content of the editor
         */
        std::string GetText() const;

        /**
         * @brief Set the text content
         * @param text New text content
         */
        void SetText(const std::string& text);

        // ===== SYNTAX HIGHLIGHTING =====

        /**
         * @brief Set the programming language for syntax highlighting
         * @param language Language name (e.g., "C++", "Python", "JavaScript")
         */
        void SetLanguage(const std::string& language);

        /**
         * @brief Get the current language
         * @return Current language name
         */
        std::string GetLanguage() const { return currentLanguage; }

        // ===== THEME =====

        /**
         * @brief Apply dark theme to the editor
         */
        void ApplyDarkTheme();

        /**
         * @brief Apply light theme to the editor
         */
        void ApplyLightTheme();

        // ===== CALLBACKS =====

        /// Called when a file is loaded
        std::function<void(const std::string&)> onFileLoaded;

        /// Called when a file is saved
        std::function<void(const std::string&)> onFileSaved;

        /// Called when the document is modified
        std::function<void(bool)> onModifiedChange;

        /// Called when the user requests to quit
        std::function<void()> onQuitRequest;

        /// Called when Help is requested
        std::function<void()> onHelpRequest;

        /// Called when About is requested
        std::function<void()> onAboutRequest;

    protected:
        // Setup methods
        void SetupMenuBar();
        void SetupToolbar();
        void SetupEditor();
        void SetupStatusBar();
        void SetupLayout();

        // Menu action handlers
        void OnFileNew();
        void OnFileOpen();
        void OnFileSave();
        void OnFileSaveAs();
        void OnFileQuit();

        void OnEditSearch();
        void OnEditReplace();
        void OnEditCopy();
        void OnEditCut();
        void OnEditPasteAll();
        void OnEditPasteText();

        void OnInfoHelp();
        void OnInfoAbout();

        // Helper methods
        void UpdateTitle();
        void SetModified(bool modified);
        std::string DetectLanguageFromExtension(const std::string& filePath);
        bool ConfirmSaveChanges();

    private:
        // Configuration
        TextEditorConfig config;

        // Components
        std::shared_ptr<UltraCanvasMenu> menuBar;
        std::shared_ptr<UltraCanvasToolbar> toolbar;
        std::shared_ptr<UltraCanvasTextArea> textArea;
        std::shared_ptr<UltraCanvasToolbar> statusBar;

        // State
        std::string currentFilePath;
        std::string currentLanguage;
        bool isModified = false;
        bool isDarkTheme = false;
    };

// ===== FACTORY FUNCTIONS =====

/**
 * @brief Create a text editor application with default settings
 */
    std::shared_ptr<UltraCanvasTextEditor> CreateTextEditor(
            const std::string& identifier,
            long id,
            int x, int y,
            int width, int height
    );

/**
 * @brief Create a text editor application with custom configuration
 */
    std::shared_ptr<UltraCanvasTextEditor> CreateTextEditor(
            const std::string& identifier,
            long id,
            int x, int y,
            int width, int height,
            const TextEditorConfig& config
    );

/**
 * @brief Create a dark-themed text editor application
 */
    std::shared_ptr<UltraCanvasTextEditor> CreateDarkTextEditor(
            const std::string& identifier,
            long id,
            int x, int y,
            int width, int height
    );

} // namespace UltraCanvas