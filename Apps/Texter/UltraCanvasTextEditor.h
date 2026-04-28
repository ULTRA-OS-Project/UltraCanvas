// Apps/Texter/UltraCanvasTextEditor.h
// Complete text editor application with multi-file tabs, autosave, and enhanced features
// Version: 2.1.0
// Last Modified: 2026-04-24
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasWindow.h"
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
#include "UltraCanvasTextEditorConfig.h"
#include "UltraCanvasEncoding.h"
#include "UltraCanvasBoxLayout.h"
#include "UltraCanvasSearchBar.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <map>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>

namespace UltraCanvas {

// Forward declarations
    class UltraCanvasTextEditor;

/**
 * @brief A New-from-Template entry shown in File > New from Template.
 * Creates a new document pre-filled with boilerplate in the right language.
 */
    struct DocumentTemplate {
        std::string name;       // Label shown in the menu, e.g. "SVG (vector graphics)"
        std::string language;   // Syntax highlighter language name, e.g. "SVG"
        std::string extension;  // Default extension used for the tab name, e.g. "svg"
        std::string content;    // Template body
    };

    std::vector<DocumentTemplate> DefaultDocumentTemplates();

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
        bool showMarkdownToolbar = true;
        bool enableAutosave = true;

        bool showRecentFiles = true;       // Show Recent Files submenu in File menu
        int maxRecentFiles = 30;           // Max files shown in Recent Files submenu (configurable)


        // Editor settings
        std::string defaultLanguage = "Plain Text";
        bool darkTheme = false;
        bool wordWrap = true;
        std::string defaultEncoding = "UTF-8";

        const std::vector<int> fontZoomPercents = {50,65,80,90,100,110,125,150,175,200};
        int fontZoomPercent = 100;
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
                FileFilter("Data / Config", {"json", "jsonc", "json5", "geojson", "webmanifest", "xml", "xsd", "xsl", "xslt", "yaml", "yml", "toml", "ini", "cfg", "pom", "rss", "atom", "kml"}),
                FileFilter("Vector Graphics (Text)", std::vector<std::string>{"svg", "svgz"}),
                FileFilter("Script Files", {"sh", "bash", "bat", "cmd", "ps1"})
        };

        // Templates available from File > New from Template
        std::vector<DocumentTemplate> documentTemplates = DefaultDocumentTemplates();
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
        bool isSaved;                      // Has unsaved changes
        bool isModified;                   // Has unsaved changes
        bool isNewFile;                    // Never been saved
        std::string autosaveBackupPath;    // Path to autosave backup
        std::chrono::steady_clock::time_point lastSaveTime;  // Last save timestamp
        std::chrono::steady_clock::time_point lastModifiedTime;  // Last edit timestamp

        std::string encoding;                  // iconv encoding name (e.g. "UTF-8", "CP1251")
        std::vector<uint8_t> originalRawBytes; // Raw file bytes for re-encoding on manual change
        bool hasBOM;                           // Whether the file had a BOM
        LineEndingType eolType = UltraCanvasTextArea::GetSystemDefaultLineEnding(); // Line ending type

        DocumentTab()
                : documentId(-1)
                , isModified(false)
                , isSaved(false)
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
        void ResetTimer() { lastAutosaveTime = std::chrono::steady_clock::now(); }
        std::string CreateBackupPath(const std::string& originalPath, int tabIndex);
        bool SaveBackup(const std::string& backupPath, const std::string& content,
                        const std::string& originalPath = "",
                        const std::string& encoding = "",
                        const std::string& language = "");
        bool LoadBackup(const std::string& backupPath, std::string& content,
                        std::string& originalPath, std::string& encoding,
                        std::string& language);
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
    class UltraCanvasTextEditor : public UltraCanvasWindow {
    private:
        // ===== CONFIGURATION =====
        TextEditorConfig config;
        bool isDarkTheme;

        // ===== UI COMPONENTS =====
        std::shared_ptr<UltraCanvasMenu> menuBar;
        std::shared_ptr<UltraCanvasContainer> toolbarContainer;  // HBox wrapper for both toolbars
        std::shared_ptr<UltraCanvasToolbar> toolbar;
        std::shared_ptr<UltraCanvasToolbar> markdownToolbar;
        std::shared_ptr<UltraCanvasTabbedContainer> tabContainer;
        std::shared_ptr<UltraCanvasLabel> statusLabel;
        std::shared_ptr<UltraCanvasDropdown> languageDropdown;
        std::shared_ptr<UltraCanvasDropdown> encodingDropdown;
        std::shared_ptr<UltraCanvasDropdown> eolDropdown;
        std::shared_ptr<UltraCanvasDropdown> zoomDropdown;

        // ===== TAB CONTEXT MENU =====
        std::shared_ptr<UltraCanvasMenu> tabContextMenu;

        // ===== RECENT FILES TOOLBAR POPUP =====
        std::shared_ptr<UltraCanvasMenu> recentFilesPopupMenu;

        // ===== HEADING SUB-TOOLBAR =====
        std::shared_ptr<UltraCanvasToolbar> headingSubToolbar;

        // ===== DIALOGS =====
        std::shared_ptr<UltraCanvasFindDialog> findDialog;
        // ===== INLINE SEARCH/REPLACE BAR =====
        std::shared_ptr<UltraCanvasSearchBar> searchBar;
        static constexpr int searchBarHeight = 32; // single-row height; doubles for replace mode

        void SetupSearchBar();
        void ShowSearchBar(SearchBarMode mode);
        void HideSearchBar();

        std::shared_ptr<UltraCanvasReplaceDialog> replaceDialog;
        std::shared_ptr<UltraCanvasGoToLineDialog> goToLineDialog;
        std::shared_ptr<UltraCanvasModalDialog> aboutDialog;
        std::shared_ptr<UltraCanvasModalDialog> fileStatsDialog;

        // ===== DOCUMENT MANAGEMENT =====
        std::vector<std::shared_ptr<DocumentTab>> documents;
        int activeDocumentIndex;
        int nextDocumentId;             // Auto-incremented stable ID counter
        bool isDocumentClosing;

        // ===== AUTOSAVE SYSTEM =====
        AutosaveManager autosaveManager;
        bool hasCheckedForBackups;

        // ===== RECENT FILES =====
        std::vector<std::string> recentFiles;      // Ordered list (newest first)
        TextEditorConfigFile configFile;           // Persistent config file manager

        // Recent files methods
        void AddToRecentFiles(const std::string& filePath);
        void RemoveFromRecentFiles(const std::string& filePath);
        void ShowRecentFilesPopup();
        void LoadRecentFiles();
        void SaveRecentFiles();
        bool IsFileCurrentlyOpen(const std::string& filePath) const;

        // Config file methods
        void LoadConfig();
        void SaveConfig();

        // Shared search history across Find/Replace dialog instances
        std::vector<std::string> searchHistory;
        std::vector<std::string> replaceHistory;

//        RecentFilesManager recentFilesManager;
        std::string lastOpenedDirectory;

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
        void SetupTabContextMenu();
        void SetupStatusBar();
        void SetupLayout();

        // ===== MARKDOWN TOOLBAR =====
        void UpdateMarkdownToolbarVisibility();
        void InsertMarkdownSnippet(const std::string& prefix, const std::string& suffix,
                                   const std::string& sampleText);
        bool IsMarkdownMode() const;

        // ===== DOCUMENT MANAGEMENT =====
        int CreateNewDocument(const std::string& fileName = "");
        int CreateDocumentFromTemplate(const DocumentTemplate& tpl);
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
        bool IsBinaryFile(const std::vector<uint8_t>& rawBytes, const std::string& extension) const;

        std::string FormatFullTabTooltip(int docIndex);

        // ===== AUTOSAVE =====
        void AutosaveDocument(int docIndex);
        void RecoverBackupIntoDocument(int docIndex, const std::string& backupPath,
                                       const std::string& content, const std::string& encoding,
                                       const std::string& language);
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
        void OnFilePrint();

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
        void OnViewToggleLineNumbers(bool checked);
        void OnViewToggleWordWrap(bool checked);
        void OnViewToggleToolbar(bool checked);
        void OnViewToggleMarkdownToolbar(bool checked);
        void UpdateToolbarsSubmenu();

        void OnInfoAbout();
        void OnInfoFileStatistics();


        // ===== UI UPDATES =====
        void UpdateStatusBar();
        void UpdateZoomDropdownSelection();
        void UpdateLanguageDropdown();
        void OnLanguageChanged(int index, const DropdownItem& item);
        void UpdateEncodingDropdown();
        void OnEncodingChanged(int index, const DropdownItem& item);
        void UpdateEOLDropdown();
        void OnEOLChanged(int index, const DropdownItem& item);
        void UpdateMenuStates();
        void UpdateTitle();

        // ===== THEME =====
        void ApplyThemeToDocument(int docIndex);
        void ApplyThemeToAllDocuments();
        void ApplyHeadingButtonStyles(bool dark);

        // ===== CALLBACKS =====
        void SetupDocumentCallbacks(int docIndex);
        void ConfirmSaveChanges(int docIndex, std::function<void(bool)> onComplete);
        void ConfirmCloseWithUnsavedChanges(std::function<void(bool)> onComplete);

        // Layout
        void UpdateChildLayout();

        // ===== ASYNC MATCH COUNTING =====
        std::thread              matchCountThread;
        std::atomic<bool>        matchCountCancel{false};
        std::atomic<bool>        matchCountReady{false};
        std::mutex               matchCountMutex;
        int                      pendingMatchTotal   = 0;
        int                      pendingMatchCurrent = 0;

        void StartAsyncMatchCount(const std::string& searchText, bool caseSensitive,
                                  int selectionPos);
        void CancelAsyncMatchCount();

        // ===== DRAG-AND-DROP STATE =====
        bool isDragOverActive = false;        // true while external drag is hovering
        int dragOverX = 0;                    // Current drag position X
        int dragOverY = 0;                    // Current drag position Y

        // ===== DRAG-AND-DROP METHODS =====
        void HandleDragEnter(const UCEvent& event);
        void HandleDragOver(const UCEvent& event);
        void HandleDragLeave(const UCEvent& event);
        void HandleFileDrop(const UCEvent& event);
        void RenderDropOverlay(IRenderContext* ctx);

        static std::string FormatFileSize(uintmax_t bytes);

    public:
        static std::string version;

        // ===== CONSTRUCTOR =====
        UltraCanvasTextEditor(const TextEditorConfig& config = TextEditorConfig());

        virtual ~UltraCanvasTextEditor();

        // Post-construction initialization (called after window is shown)
        void RestoreSessionAndRecoverBackups();

        // ===== RENDERING =====
        void Render(IRenderContext* ctx, const Rect2Di& dirtyRect) override;

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
         * @brief Save open file paths to session file for restore on next launch
         */
        void SaveSession();

        /**
         * @brief Generate a display name that does not collide with any existing document's fileName.
         * Returns base + N, where N is the smallest positive integer for which the name is unused.
         */
        std::string GenerateUniqueDocumentName(const std::string& base) const;

        /**
         * @brief Build a sanitized filename stem (no extension) from a single line of text.
         * Strips common comment markers, replaces filesystem-unsafe characters, collapses
         * whitespace, and truncates to a reasonable length. Returns an empty string if the
         * line has no usable content; callers treat that as "no suggestion".
         */
        std::string SuggestFileNameFromFirstLine(const std::string& firstLine) const;


        void PerformAutosave(bool force = false);

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
        void SetFontZoomPercent(int percent);

        /**
         * @brief Get current font size
         * @return Font size in points
         */
        float GetFontSize() const { return config.defaultFontSize * config.fontZoomPercent / 100.0; }
        int GetFontZoomPercent() const { return config.fontZoomPercent; }

        /**
         * @brief Increase font size
         */
        void IncreaseFontZoom();

        /**
         * @brief Decrease font size
         */
        void DecreaseFontZoom();

        /**
         * @brief Reset font size to default
         */
        void ResetFontZoom();

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
         * @brief Callback when file is saved
         * @param filePath Path where file was saved
         * @param tabIndex Tab index of saved file
         */
        std::function<void(const std::string& filePath, int tabIndex)> onFileSaved;

        /**
         * @brief Callback when tab is closed
         * @param tabIndex Tab index that was closed
         */
        std::function<void(int tabIndex)> onTabClosed;

        /// Callback when files are dropped onto the editor
        /// @param filePaths Vector of dropped file paths
        std::function<void(const std::vector<std::string>& filePaths)> onFileDrop;

        // ===== MULTI-WINDOW SUPPORT =====

        /// Callback when "New Window" is selected from File menu
        std::function<void()> onNewWindowRequest;

        /// Callback when a tab is dragged out of the tab bar
        /// @param doc The DocumentTab being transferred
        /// @param screenX Screen X where the drag ended
        /// @param screenY Screen Y where the drag ended
        std::function<void(std::shared_ptr<DocumentTab> doc, int screenX, int screenY)> onTabDraggedOut;

        /// Callback when theme is toggled (for syncing across windows)
        std::function<void(bool isDark)> onThemeChanged;

        /// Callback to check if this window can close when empty (i.e. other windows exist)
        /// Return true to allow auto-close, false to keep window open with a new document
        std::function<bool()> canCloseEmptyWindow;

        /// Accept a DocumentTab from another window
        /// @param doc The document to insert
        /// @return Tab index, or -1 on failure
        int AcceptDocument(std::shared_ptr<DocumentTab> doc);

        /// Extract a DocumentTab without save prompts or auto-creating empty tab
        /// @param index Document index to extract
        /// @return The extracted document, or nullptr if invalid
        std::shared_ptr<DocumentTab> ExtractDocument(int index);

        /// Get number of open documents
        int GetDocumentCount() const { return static_cast<int>(documents.size()); }
    };

// ===== FACTORY FUNCTIONS =====

    std::shared_ptr<UltraCanvasTextEditor> CreateTextEditor(
            const TextEditorConfig& config = TextEditorConfig());

    std::shared_ptr<UltraCanvasTextEditor> CreateDarkTextEditor();

} // namespace UltraCanvas