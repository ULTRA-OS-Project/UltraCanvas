// Apps/Texter/UltraCanvasTextEditor.h
// Complete text editor application with multi-file tabs, autosave, and enhanced features
// Version: 2.0.2
// Last Modified: 2026-02-02
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasTextEditorHelpers.h"
#include "UltraCanvasTextEditorDialogs.h"
#include "UltraCanvasEncoding.h"
#include "UltraCanvasBoxLayout.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <map>
#include <cstdint>

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
        bool enableAutosave = true;

        // Editor settings
        std::string defaultLanguage = "Plain Text";
        bool darkTheme = false;
        bool wordWrap = false;
        std::string defaultEncoding = "UTF-8";
        float defaultFontSize = 10.0;

        // Autosave settings
        int autosaveIntervalSeconds = 60;  // Autosave every 60 seconds
        std::string autosaveDirectory = "";  // Empty = use system temp directory

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
 * @brief Data structure for each open file/document
 */
    struct DocumentTab {
        int documentId;                    // Stable unique ID (survives index shifts)
        std::string filePath;              // Full file path (empty for new/unsaved files)
        std::string fileName;              // Display name
        std::shared_ptr<UltraCanvasTextArea> textArea;  // Text editor component
        std::string language;              // Syntax highlighting language
        bool isModified;                   // Has unsaved changes
        bool isNewFile;                    // Never been saved
        std::string autosaveBackupPath;    // Path to autosave backup
        std::chrono::steady_clock::time_point lastSaveTime;  // Last save timestamp
        std::chrono::steady_clock::time_point lastModifiedTime;  // Last edit timestamp

        std::string encoding;                  // iconv encoding name (e.g. "UTF-8", "CP1251")
        std::vector<uint8_t> originalRawBytes; // Raw file bytes for re-encoding on manual change
        bool hasBOM;                           // Whether the file had a BOM

        DocumentTab()
                : documentId(-1)
                , isModified(false)
                , isNewFile(true)
                , encoding("UTF-8")
                , hasBOM(false)
                , lastSaveTime(std::chrono::steady_clock::now())
                , lastModifiedTime(std::chrono::steady_clock::now())
        {}
    };

/**
 * @brief Autosave manager for crash recovery
 */
    class AutosaveManager {
    private:
        std::string autosaveDirectory;
        bool enabled;
        int intervalSeconds;
        std::chrono::steady_clock::time_point lastAutosaveTime;

    public:
        AutosaveManager()
                : enabled(true)
                , intervalSeconds(60)
                , lastAutosaveTime(std::chrono::steady_clock::now())
        {}

        void SetEnabled(bool enable) { enabled = enable; }
        bool IsEnabled() const { return enabled; }

        void SetInterval(int seconds) { intervalSeconds = seconds; }
        int GetInterval() const { return intervalSeconds; }

        void SetDirectory(const std::string& dir) { autosaveDirectory = dir; }
        std::string GetDirectory() const;

        bool ShouldAutosave() const;
        std::string CreateBackupPath(const std::string& originalPath, int tabIndex);
        bool SaveBackup(const std::string& backupPath, const std::string& content);
        bool LoadBackup(const std::string& backupPath, std::string& content);
        void DeleteBackup(const std::string& backupPath);
        std::vector<std::string> FindExistingBackups();
        void CleanupOldBackups(int maxAgeHours = 24);
    };

/**
 * @brief Complete multi-file text editor application component
 *
 * This component provides a full-featured text editor with:
 * - Multi-file tabs with "+" button for new files
 * - Tab badges showing modified state
 * - Menu bar (File, Edit, View, Info)
 * - Optional toolbar
 * - Syntax-highlighted text area
 * - Status bar (line:col, encoding, syntax, selection)
 * - Full undo/redo support
 * - Autosave and crash recovery
 * - Theme switching (dark/light)
 * - Font size adjustment
 *
 * @example
 * auto editor = CreateTextEditor("MyEditor", 1, 0, 0, 1024, 768);
 * editor->OpenFile("/path/to/file.cpp");
 * window->AddChild(editor);
 */
    class UltraCanvasTextEditor : public UltraCanvasContainer {
    private:
        std::string version = "1.0.2";
        // ===== CONFIGURATION =====
        TextEditorConfig config;
        bool isDarkTheme;
        const std::vector<int> fontZoomLevels;
        int fontZoomLevelIdx;

        // ===== UI COMPONENTS =====
        std::shared_ptr<UltraCanvasMenu> menuBar;
        std::shared_ptr<UltraCanvasContainer> toolbarContainer;  // HBox wrapper for both toolbars
        std::shared_ptr<UltraCanvasToolbar> toolbar;
        std::shared_ptr<UltraCanvasToolbar> markdownToolbar;
        std::shared_ptr<UltraCanvasTabbedContainer> tabContainer;
        std::shared_ptr<UltraCanvasLabel> statusLabel;
        std::shared_ptr<UltraCanvasDropdown> languageDropdown;
        std::shared_ptr<UltraCanvasDropdown> encodingDropdown;
        std::shared_ptr<UltraCanvasDropdown> zoomDropdown;

        // ===== DIALOGS =====
        std::shared_ptr<UltraCanvasFindDialog> findDialog;
        std::shared_ptr<UltraCanvasReplaceDialog> replaceDialog;
        std::shared_ptr<UltraCanvasGoToLineDialog> goToLineDialog;
        std::shared_ptr<UltraCanvasModalDialog> aboutDialog;

        // ===== DOCUMENT MANAGEMENT =====
        std::vector<std::shared_ptr<DocumentTab>> documents;
        int activeDocumentIndex;
        int nextDocumentId;             // Auto-incremented stable ID counter
        bool isDocumentClosing;

        // ===== AUTOSAVE SYSTEM =====
        AutosaveManager autosaveManager;
        bool hasCheckedForBackups;

        // ===== LAYOUT =====
        int menuBarHeight;
        int toolbarHeight;
        int markdownToolbarWidth;
        int statusBarHeight;
        int tabBarHeight;

        // ===== SETUP METHODS =====
        void SetupMenuBar();
        void SetupToolbar();
        void SetupMarkdownToolbar();
        void SetupTabContainer();
        void SetupStatusBar();
        void SetupLayout();

        // ===== MARKDOWN TOOLBAR =====
        void UpdateMarkdownToolbarVisibility();
        void InsertMarkdownSnippet(const std::string& prefix, const std::string& suffix,
                                   const std::string& sampleText);
        bool IsMarkdownMode() const;

        // ===== DOCUMENT MANAGEMENT =====
        int CreateNewDocument(const std::string& fileName = "");
        int OpenDocumentFromPath(const std::string& filePath);
        void CloseDocument(int index);
        void SwitchToDocument(int index);
        DocumentTab* GetActiveDocument();
        const DocumentTab* GetActiveDocument() const;
        int FindDocumentIndexById(int documentId) const;
        void SetDocumentModified(int index, bool modified);
        void UpdateTabTitle(int index);
        void UpdateTabBadge(int index);

        // ===== FILE OPERATIONS =====
        bool LoadFileIntoDocument(int docIndex, const std::string& filePath);
        bool SaveDocument(int docIndex);
        bool SaveDocumentAs(int docIndex, const std::string& filePath);

        // ===== AUTOSAVE =====
        void PerformAutosave();
        void AutosaveDocument(int docIndex);
        void CheckForCrashRecovery();
        void OfferRecoveryForBackup(const std::string& backupPath);

        // ===== MENU HANDLERS =====
        void OnFileNew();
        void OnFileOpen();
        void OnFileSave();
        void OnFileSaveAs();
        void OnFileSaveAll();
        void OnFileClose();
        void OnFileCloseAll();
        void OnFileQuit();

        void OnEditUndo();
        void OnEditRedo();
        void OnEditCut();
        void OnEditCopy();
        void OnEditPaste();
        void OnEditSelectAll();
        void OnEditSearch();
        void OnEditReplace();
        void OnEditGoToLine();

        void OnViewIncreaseFontSize();
        void OnViewDecreaseFontSize();
        void OnViewResetFontSize();
        void OnViewToggleTheme();
        void OnViewToggleLineNumbers();
        void OnViewToggleWordWrap();

        void OnInfoAbout();

        // ===== UI UPDATES =====
        void UpdateStatusBar();
        void UpdateZoomDropdownSelection();
        void UpdateLanguageDropdown();
        void OnLanguageChanged(int index, const DropdownItem& item);
        void UpdateEncodingDropdown();
        void OnEncodingChanged(int index, const DropdownItem& item);
        void UpdateMenuStates();
        void UpdateTitle();

        // ===== THEME =====
        void ApplyThemeToDocument(int docIndex);
        void ApplyThemeToAllDocuments();

        // ===== CALLBACKS =====
        void SetupDocumentCallbacks(int docIndex);
        void ConfirmSaveChanges(int docIndex, std::function<void(bool)> onComplete);
        void ConfirmCloseWithUnsavedChanges(std::function<void(bool)> onComplete);

        // Layout
        void UpdateChildLayout();

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasTextEditor(const std::string& identifier, long id,
                              int x, int y, int width, int height,
                              const TextEditorConfig& config = TextEditorConfig());

        virtual ~UltraCanvasTextEditor() = default;

        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;
        void SetBounds(const Rect2Di& b) override;
        // ===== FILE OPERATIONS (PUBLIC API) =====

        /**
         * @brief Open a file in a new tab
         * @param filePath Path to the file to open
         * @return Tab index, or -1 on failure
         */
        int OpenFile(const std::string& filePath);

        /**
         * @brief Create a new empty document
         * @return Tab index of the new document
         */
        int NewFile();

        /**
         * @brief Save the active document
         * @return true if save was successful
         */
        bool SaveActiveFile();

        /**
         * @brief Save the active document with a new path
         * @param filePath Path to save to
         * @return true if save was successful
         */
        bool SaveActiveFileAs(const std::string& filePath);

        /**
         * @brief Save all open documents
         * @return true if all saves were successful
         */
        bool SaveAllFiles();

        /**
         * @brief Close the active tab
         */
        void CloseActiveTab();

        /**
         * @brief Close all tabs
         */
        void CloseAllTabs();

        /**
         * @brief Get the current file path of active document
         * @return Current file path, empty if new/unsaved
         */
        std::string GetActiveFilePath() const;

        /**
         * @brief Check if active document has unsaved changes
         * @return true if there are unsaved changes
         */
        bool HasUnsavedChanges() const;

        /**
         * @brief Check if any document has unsaved changes
         * @return true if any document is modified
         */
        bool HasAnyUnsavedChanges() const;

        // ===== TEXT OPERATIONS =====

        /**
         * @brief Get the text content of active document
         * @return Text content
         */
        std::string GetText() const;

        /**
         * @brief Set the text content of active document
         * @param text New text content
         */
        void SetText(const std::string& text);

        // ===== UNDO/REDO =====

        /**
         * @brief Undo last action in active document
         */
        void Undo();

        /**
         * @brief Redo last undone action in active document
         */
        void Redo();

        /**
         * @brief Check if undo is available
         * @return true if can undo
         */
        bool CanUndo() const;

        /**
         * @brief Check if redo is available
         * @return true if can redo
         */
        bool CanRedo() const;

        // ===== SYNTAX HIGHLIGHTING =====

        /**
         * @brief Set the programming language for syntax highlighting
         * @param language Language name (e.g., "C++", "Python", "JavaScript")
         */
        void SetLanguage(const std::string& language);

        /**
         * @brief Get the current language of active document
         * @return Current language name
         */
        std::string GetLanguage() const;

        // ===== THEME =====

        /**
         * @brief Apply dark theme
         */
        void ApplyDarkTheme();

        /**
         * @brief Apply light theme
         */
        void ApplyLightTheme();

        /**
         * @brief Toggle between dark and light theme
         */
        void ToggleTheme();

        /**
         * @brief Check if dark theme is active
         * @return true if dark theme
         */
        bool IsDarkTheme() const { return isDarkTheme; }

        // ===== FONT SIZE =====

        /**
         * @brief Set font size for all documents
         * @param size Font size in points
         */
        void SetDefaultFontSize(float fontSize);
        void SetFontZoomLevel(int level);

        /**
         * @brief Get current font size
         * @return Font size in points
         */
        float GetFontSize() const { return config.defaultFontSize * fontZoomLevels[fontZoomLevelIdx] / 100.0; }
        float GetFontZoomLevel() const { return fontZoomLevelIdx; }

        /**
         * @brief Increase font size
         */
        void IncreaseFontZoomLevel();

        /**
         * @brief Decrease font size
         */
        void DecreaseFontZoomLevel();

        /**
         * @brief Reset font size to default
         */
        void ResetFontZoomLevel();

        // ===== AUTOSAVE =====

        /**
         * @brief Enable or disable autosave
         * @param enable true to enable autosave
         */
        void SetAutosaveEnabled(bool enable);

        /**
         * @brief Check if autosave is enabled
         * @return true if autosave is enabled
         */
        bool IsAutosaveEnabled() const;

        /**
         * @brief Set autosave interval
         * @param seconds Interval in seconds
         */
        void SetAutosaveInterval(int seconds);

        /**
         * @brief Perform autosave now (if needed)
         */
        void AutosaveNow();

        // ===== CALLBACKS =====

        /**
         * @brief Callback when quit is requested
         */
        std::function<void()> onQuitRequest;

        /**
         * @brief Callback when file is loaded
         * @param filePath Path of loaded file
         * @param tabIndex Tab index where file was loaded
         */
        std::function<void(const std::string& filePath, int tabIndex)> onFileLoaded;

        /**
         * @brief Callback when file is saved
         * @param filePath Path where file was saved
         * @param tabIndex Tab index of saved file
         */
        std::function<void(const std::string& filePath, int tabIndex)> onFileSaved;

        /**
         * @brief Callback when document modified state changes
         * @param modified true if document has unsaved changes
         * @param tabIndex Tab index of modified document
         */
        std::function<void(bool modified, int tabIndex)> onModifiedChange;

        /**
         * @brief Callback when active tab changes
         * @param tabIndex New active tab index
         */
        std::function<void(int tabIndex)> onTabChanged;

        /**
         * @brief Callback when tab is closed
         * @param tabIndex Tab index that was closed
         */
        std::function<void(int tabIndex)> onTabClosed;
    };

// ===== FACTORY FUNCTIONS =====

    std::shared_ptr<UltraCanvasTextEditor> CreateTextEditor(
            const std::string& identifier,
            long id,
            int x, int y,
            int width, int height);

    std::shared_ptr<UltraCanvasTextEditor> CreateTextEditor(
            const std::string& identifier,
            long id,
            int x, int y,
            int width, int height,
            const TextEditorConfig& config);

    std::shared_ptr<UltraCanvasTextEditor> CreateDarkTextEditor(
            const std::string& identifier,
            long id,
            int x, int y,
            int width, int height);

} // namespace UltraCanvas