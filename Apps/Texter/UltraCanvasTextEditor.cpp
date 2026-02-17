// Apps/Texter/UltraCanvasTextEditor.cpp
// Complete text editor implementation with multi-file tabs and autosave
// Version: 2.0.2
// Last Modified: 2026-02-02
// Author: UltraCanvas Framework

#include "UltraCanvasTextEditor.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextEditorHelpers.h"
#include "UltraCanvasTextEditorDialogs.h"
//#include "UltraCanvasDialogManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <ctime>
#include <fmt/os.h>

namespace UltraCanvas {

// ===== AUTOSAVE MANAGER IMPLEMENTATION =====

    std::string AutosaveManager::GetDirectory() const {
        if (!autosaveDirectory.empty()) {
            return autosaveDirectory;
        }

        // Use system temp directory
#ifdef _WIN32
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        return std::string(tempPath) + "UltraTexter\\Autosave\\";
#else
        return "/tmp/UltraTexter/Autosave/";
#endif
    }

    bool AutosaveManager::ShouldAutosave() const {
        if (!enabled) return false;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastAutosaveTime);
        return elapsed.count() >= intervalSeconds;
    }

    std::string AutosaveManager::CreateBackupPath(const std::string& originalPath, int tabIndex) {
        std::string dir = GetDirectory();

        // Create directory if it doesn't exist
        try {
            std::filesystem::create_directories(dir);
        } catch (...) {
            std::cerr << "Failed to create autosave directory: " << dir << std::endl;
            return "";
        }

        // Generate backup filename
        std::string filename;
        if (originalPath.empty()) {
            // New unsaved file
            filename = "Untitled_" + std::to_string(tabIndex) + ".autosave";
        } else {
            // Extract filename from path
            std::filesystem::path p(originalPath);
            filename = p.filename().string() + ".autosave";
        }

        // Add timestamp to make it unique
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::system_clock::to_time_t(now);
        filename += "." + std::to_string(timestamp);

        return dir + filename;
    }

    bool AutosaveManager::SaveBackup(const std::string& backupPath, const std::string& content) {
        try {
            std::ofstream file(backupPath, std::ios::binary);
            if (!file.is_open()) {
                return false;
            }

            // Write metadata header
            file << "ULTRATEXTER_AUTOSAVE_V1\n";
            file << "TIMESTAMP=" << std::time(nullptr) << "\n";
            file << "---CONTENT---\n";
            file << content;
            file.close();

            return true;
        } catch (const std::exception& e) {
            std::cerr << "Autosave error: " << e.what() << std::endl;
            return false;
        }
    }

    bool AutosaveManager::LoadBackup(const std::string& backupPath, std::string& content) {
        try {
            std::ifstream file(backupPath, std::ios::binary);
            if (!file.is_open()) {
                return false;
            }

            std::string line;
            // Read header
            std::getline(file, line);
            if (line != "ULTRATEXTER_AUTOSAVE_V1") {
                return false;
            }

            // Skip metadata
            while (std::getline(file, line)) {
                if (line == "---CONTENT---") {
                    break;
                }
            }

            // Read content
            std::stringstream buffer;
            buffer << file.rdbuf();
            content = buffer.str();

            file.close();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Backup load error: " << e.what() << std::endl;
            return false;
        }
    }

    void AutosaveManager::DeleteBackup(const std::string& backupPath) {
        try {
            std::filesystem::remove(backupPath);
        } catch (...) {
            // Ignore errors
        }
    }

    std::vector<std::string> AutosaveManager::FindExistingBackups() {
        std::vector<std::string> backups;

        try {
            std::string dir = GetDirectory();
            if (!std::filesystem::exists(dir)) {
                return backups;
            }

            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    if (filename.find(".autosave") != std::string::npos) {
                        backups.push_back(entry.path().string());
                    }
                }
            }
        } catch (...) {
            // Ignore errors
        }

        return backups;
    }

    void AutosaveManager::CleanupOldBackups(int maxAgeHours) {
        try {
            std::string dir = GetDirectory();
            if (!std::filesystem::exists(dir)) {
                return;
            }

            auto now = std::time(nullptr);
            int maxAgeSeconds = maxAgeHours * 3600;

            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    if (filename.find(".autosave") != std::string::npos) {
                        auto lastWrite = std::filesystem::last_write_time(entry);
                        auto fileTime = std::chrono::system_clock::to_time_t(
                                std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                        lastWrite - std::filesystem::file_time_type::clock::now() +
                                        std::chrono::system_clock::now()
                                )
                        );

                        if (now - fileTime > maxAgeSeconds) {
                            std::filesystem::remove(entry.path());
                        }
                    }
                }
            }
        } catch (...) {
            // Ignore errors
        }
    }

// ===== CONSTRUCTOR =====
    UltraCanvasTextEditor::UltraCanvasTextEditor(
            const std::string& identifier, long id,
            int x, int y, int width, int height,
            const TextEditorConfig& cfg)
            : UltraCanvasContainer(identifier, id, x, y, width, height)
            , config(cfg)
            , isDarkTheme(cfg.darkTheme)
            , isDocumentClosing(false)
            , nextDocumentId(0)
            , activeDocumentIndex(-1)
            , hasCheckedForBackups(false)
            , menuBarHeight(28)
            , toolbarHeight(cfg.showToolbar ? 44 : 0)
            , statusBarHeight(24)
            , tabBarHeight(32)
            , fontZoomLevels({50,65,80,90,100,110,125,150,175,200})
    {        
        for(int i = 0; i < fontZoomLevels.size(); i++) {
            if (fontZoomLevels[i] == 100) {
                fontZoomLevelIdx = i;
                break;
            }
        }

        SetBackgroundColor(Color(240, 240, 240, 255));

        // Configure autosave
        autosaveManager.SetEnabled(config.enableAutosave);
        autosaveManager.SetInterval(config.autosaveIntervalSeconds);
        if (!config.autosaveDirectory.empty()) {
            autosaveManager.SetDirectory(config.autosaveDirectory);
        }

        // Setup UI components in order
        if (config.showMenuBar) {
            SetupMenuBar();
        }

        if (config.showToolbar) {
            SetupToolbar();
        }

        SetupTabContainer();

        if (config.showStatusBar) {
            SetupStatusBar();
        }

        SetupLayout();

        // Create initial empty document
        CreateNewDocument();

        // Check for crash recovery (do this after first document is created)
        CheckForCrashRecovery();

        UpdateTitle();
    }

// ===== SETUP METHODS =====

    void UltraCanvasTextEditor::SetupMenuBar() {
        int yPos = 0;

        // Create menu bar using MenuBuilder
        menuBar = MenuBuilder("EditorMenuBar", 100, 0, yPos, GetWidth(), menuBarHeight)
                .SetType(MenuType::Menubar)

                        // ===== FILE MENU =====
                .AddSubmenu("File", {
                        MenuItemData::ActionWithShortcut("📄 New", "Ctrl+N", [this]() {
                            OnFileNew();
                        }),
                        MenuItemData::ActionWithShortcut("📂 Open...", "Ctrl+O", [this]() {
                            OnFileOpen();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("💾 Save", "Ctrl+S", [this]() {
                            OnFileSave();
                        }),
                        MenuItemData::ActionWithShortcut("💾 Save As...", "Ctrl+Shift+S", [this]() {
                            OnFileSaveAs();
                        }),
                        MenuItemData::Action("💾 Save All", [this]() {
                            OnFileSaveAll();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Close Tab", "Ctrl+W", "media/icons/new/menu/close_tab.svg", [this]() {
                            OnFileClose();
                        }),
                        MenuItemData::Action("Close All", "media/icons/new/menu/close_tab.svg", [this]() {
                            OnFileCloseAll();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Quit", "Alt+F4", "media/icons/new/menu/exit.svg", [this]() {
                            OnFileQuit();
                        })
                })

                        // ===== EDIT MENU =====
                .AddSubmenu("Edit", {
                        // "↶ Undo"
                        MenuItemData::ActionWithShortcut("Undo", "Ctrl+Z", "media/icons/new/menu/arrow_left.svg", [this]() {
                            OnEditUndo();
                        }),
                        // "↷ Redo"
                        MenuItemData::ActionWithShortcut("Redo", "Ctrl+Y", "media/icons/new/menu/arrow_right.svg", [this]() {
                            OnEditRedo();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("✂ Cut", "Ctrl+X", [this]() {
                            OnEditCut();
                        }),
                        MenuItemData::ActionWithShortcut("Copy", "Ctrl+C", "media/icons/new/menu/copy.svg", [this]() {
                            OnEditCopy();
                        }),
                        MenuItemData::ActionWithShortcut("📋 Paste", "Ctrl+V", [this]() {
                            OnEditPaste();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("🔍 Find...", "Ctrl+F", [this]() {
                            OnEditSearch();
                        }),
                        MenuItemData::ActionWithShortcut("🔄 Replace...", "Ctrl+H", [this]() {
                            OnEditReplace();
                        }),
                        MenuItemData::ActionWithShortcut("🎯 Go to Line...", "Ctrl+G", [this]() {
                            OnEditGoToLine();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("✅ Select All", "Ctrl+A", [this]() {
                            OnEditSelectAll();
                        })
                })

                        // ===== VIEW MENU =====
                .AddSubmenu("View", {
                        MenuItemData::ActionWithShortcut("🔍 Increase Font Size", "Ctrl++", [this]() {
                            OnViewIncreaseFontSize();
                        }),
                        MenuItemData::ActionWithShortcut("🔍 Decrease Font Size", "Ctrl+-", [this]() {
                            OnViewDecreaseFontSize();
                        }),
                        MenuItemData::ActionWithShortcut("🔍 Reset Font Size", "Ctrl+0", [this]() {
                            OnViewResetFontSize();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Toggle Theme", "Ctrl+T", "media/icons/new/menu/theme_mode.svg", [this]() {
                            OnViewToggleTheme();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::Checkbox("Line Numbers", config.showLineNumbers, [this](bool checked) {
                            config.showLineNumbers = checked;
                            OnViewToggleLineNumbers();
                        }),
                        MenuItemData::Checkbox("Word Wrap", config.wordWrap, [this](bool checked) {
                            config.wordWrap = checked;
                            OnViewToggleWordWrap();
                        })
                })

                        // ===== INFO MENU =====
                .AddSubmenu("Info", {
                        MenuItemData::Action("About UltraTexter", [this]() {
                            OnInfoAbout();
                        })
                })

                .Build();

        AddChild(menuBar);
    }
  
    void UltraCanvasTextEditor::SetupToolbar() {
        int toolbarY = config.showMenuBar ? menuBarHeight : 0;

        toolbar = UltraCanvasToolbarBuilder("EditorToolbar", 200)
                .SetOrientation(ToolbarOrientation::Horizontal)
                .SetAppearance(ToolbarAppearance::Flat())
                .SetDimensions(0, toolbarY, GetWidth(), toolbarHeight)
                .AddButton("new", "", "media/icons/new/new-thin.png", [this]() { OnFileNew(); })
                .AddButton("open", "", "media/icons/new/open-thin.png", [this]() { OnFileOpen(); })
                .AddButton("save", "", "media/icons/new/save-thin.png", [this]() { OnFileSave(); })
                .AddSeparator()
                .AddButton("cut", "", "media/icons/new/cut-thin.png", [this]() { OnEditCut(); })
                .AddButton("copy", "", "media/icons/new/copy-thin.png", [this]() { OnEditCopy(); })
                .AddButton("paste", "", "media/icons/new/paste-thin.png", [this]() { OnEditPaste(); })
                .AddSeparator()
                .AddButton("search", "", "media/icons/new/search-thin.png", [this]() { OnEditSearch(); })
                .AddButton("replace", "", "media/icons/new/replace-thin.png", [this]() { OnEditReplace(); })
                .AddSeparator()
                .AddButton("zoom-in", "", "media/icons/new/zoom-in-thin.png", [this]() { OnViewIncreaseFontSize(); })
                .AddButton("zoom-out", "", "media/icons/new/zoom-out-thin.png", [this]() { OnViewDecreaseFontSize(); }) // TODO: Add zoom outIncreaseFontSize(); })

                .Build();

        AddChild(toolbar);
    }


    void UltraCanvasTextEditor::SetupTabContainer() {
        int yPos = 0;
        if (config.showMenuBar) yPos += menuBarHeight;
        if (config.showToolbar) yPos += toolbarHeight;

        int tabAreaHeight = GetHeight() - yPos - (config.showStatusBar ? statusBarHeight : 0);

        // Create tabbed container
        tabContainer = std::make_shared<UltraCanvasTabbedContainer>(
                "EditorTabs", 200,
                0, yPos,
                GetWidth(), tabAreaHeight
        );

        // Configure tab container
        tabContainer->SetTabStyle(TabStyle::Flat);
        tabContainer->SetTabPosition(TabPosition::Top);
        tabContainer->SetCloseMode(TabCloseMode::Closable);
        tabContainer->SetShowNewTabButton(true);
        tabContainer->SetNewTabButtonPosition(NewTabButtonPosition::AfterTabs);
        tabContainer->SetTabHeight(tabBarHeight);
        tabContainer->SetActiveTabBackgroundColor(Colors::White);

        // Setup callbacks
        tabContainer->onTabChange = [this](int oldIndex, int newIndex) {
            SwitchToDocument(newIndex);
        };

        tabContainer->onTabClose = [this](int index) {            
            if (isDocumentClosing) {
                return true;
            }
            CloseDocument(index);
            return false;
        };

        tabContainer->onNewTabRequest = [this]() {
            OnFileNew();
        };

        AddChild(tabContainer);
    }

    void UltraCanvasTextEditor::SetupStatusBar() {
        if (!config.showStatusBar) return;

        int yPos = GetHeight() - statusBarHeight;
        int zoomDropdownWidth = 80;

        statusLabel = std::make_shared<UltraCanvasLabel>(
                "StatusBar", 300,
                4, yPos + 4,
                GetWidth() - zoomDropdownWidth - 12, statusBarHeight - 8
        );
        statusLabel->SetText("Ready");
        statusLabel->SetFontSize(10);
        statusLabel->SetTextColor(Color(80, 80, 80, 255));
        statusLabel->SetBackgroundColor(Color(240, 240, 240, 255));

        AddChild(statusLabel);

        // Create zoom dropdown on the right side of the status bar
        zoomDropdown = std::make_shared<UltraCanvasDropdown>(
                "ZoomDropdown", 301,
                GetWidth() - zoomDropdownWidth - 4, yPos + 2,
                zoomDropdownWidth, statusBarHeight - 4
        );
        fontZoomLevelIdx = 4;
        for(int i = 0; i < fontZoomLevels.size(); i++) {
            auto zoomLabel = fmt::format("{}%", fontZoomLevels[i]);
            auto zoomValue = fmt::format("{}", i);
            zoomDropdown->AddItem(zoomLabel, zoomValue);
            if (fontZoomLevels[i] == 100) {
                fontZoomLevelIdx = i; 
            }
        }

        zoomDropdown->SetSelectedIndex(fontZoomLevelIdx); // 100%

        DropdownStyle zoomStyle = zoomDropdown->GetStyle();
        zoomStyle.fontSize = 10;
        zoomDropdown->SetStyle(zoomStyle);

        zoomDropdown->onSelectionChanged = [this](int index, const DropdownItem& item) {
            int levelIdx = std::stoi(item.value);
            SetFontZoomLevel(levelIdx);
        };

        AddChild(zoomDropdown);
    }

    void UltraCanvasTextEditor::SetupLayout() {
        // Layout is managed by fixed positioning
        // Components are positioned in their setup methods
    }
    void UltraCanvasTextEditor::SetBounds(const Rect2Di& b) {
        UltraCanvasContainer::SetBounds(b);
        UpdateChildLayout();
    }

    void UltraCanvasTextEditor::UpdateChildLayout() {
        int w = GetWidth();
        int h = GetHeight();
        int yPos = 0;

        // ===== Menu bar =====
        if (menuBar && config.showMenuBar) {
            menuBar->SetBounds(Rect2Di(0, yPos, w, menuBarHeight));
            yPos += menuBarHeight;
        }

        // ===== Toolbar =====
        if (toolbar && config.showToolbar) {
            toolbar->SetBounds(Rect2Di(0, yPos, w, toolbarHeight));
            yPos += toolbarHeight;
        }

        // ===== Tab container (fills remaining space minus status bar) =====
        if (tabContainer) {
            int tabAreaHeight = h - yPos - (config.showStatusBar ? statusBarHeight : 0);
            if (tabAreaHeight < 0) tabAreaHeight = 0;
            tabContainer->SetBounds(Rect2Di(0, yPos, w, tabAreaHeight));
        }

        // ===== Status bar =====
        if (config.showStatusBar) {
            int statusY = h - statusBarHeight;
            int zoomDropdownWidth = 80;
            if (statusLabel) {
                statusLabel->SetBounds(Rect2Di(4, statusY + 4, w - zoomDropdownWidth - 12, statusBarHeight - 8));
            }
            if (zoomDropdown) {
                zoomDropdown->SetBounds(Rect2Di(w - zoomDropdownWidth - 4, statusY + 2, zoomDropdownWidth, statusBarHeight - 4));
            }
        }
    }

// ===== DOCUMENT MANAGEMENT =====

    int UltraCanvasTextEditor::CreateNewDocument(const std::string& fileName) {
        // Create new document tab
        auto doc = std::make_shared<DocumentTab>();
        doc->documentId = nextDocumentId++;
        doc->fileName = fileName.empty() ? ("Untitled" + std::to_string(documents.size() + 1)) : fileName;
        doc->filePath = "";
        doc->language = config.defaultLanguage;
        doc->isModified = false;
        doc->isNewFile = true;

        // Calculate text area bounds
        int contentY = 0;
        if (config.showMenuBar) contentY += menuBarHeight;
        if (config.showToolbar) contentY += toolbarHeight;
        contentY += tabBarHeight; // Tab bar height

        int contentHeight = GetHeight() - contentY - (config.showStatusBar ? statusBarHeight : 0);

        // Create text area
        doc->textArea = std::make_shared<UltraCanvasTextArea>(
                "TextArea_" + std::to_string(documents.size()),
                1000 + static_cast<int>(documents.size()),
                0, 0,
                GetWidth(), contentHeight
        );

        // Configure text area
        doc->textArea->SetHighlightSyntax(false); // Plain text by default
        doc->textArea->ApplyPlainTextStyle();

        // Apply current theme
        if (isDarkTheme) {
            doc->textArea->ApplyDarkTheme();
        }

        // Apply current View settings to new document
        doc->textArea->SetFontSize(GetFontSize());
        doc->textArea->SetShowLineNumbers(config.showLineNumbers);
        doc->textArea->SetWordWrap(config.wordWrap);

        // Add document to list
        documents.push_back(doc);
        int docIndex = static_cast<int>(documents.size()) - 1;

        // Setup callbacks for this document
        SetupDocumentCallbacks(docIndex);

        // Add tab
        int tabIndex = tabContainer->AddTab(doc->fileName, doc->textArea);

        // Switch to new document
        activeDocumentIndex = docIndex;
        tabContainer->SetActiveTab(tabIndex);

        UpdateTabTitle(docIndex);
        UpdateStatusBar();

        return docIndex;
    }

    int UltraCanvasTextEditor::OpenDocumentFromPath(const std::string& filePath) {
        // Check if file is already open
        for (size_t i = 0; i < documents.size(); i++) {
            if (documents[i]->filePath == filePath) {
                // Switch to existing tab
                SwitchToDocument(static_cast<int>(i));
                return static_cast<int>(i);
            }
        }

        // Create new document
        std::filesystem::path p(filePath);
        int docIndex = CreateNewDocument(p.filename().string());

        // Load file content
        if (!LoadFileIntoDocument(docIndex, filePath)) {
            // Failed to load - close the document
            CloseDocument(docIndex);
            return -1;
        }

        return docIndex;
    }

    void UltraCanvasTextEditor::CloseDocument(int index) {
        if (isDocumentClosing || index < 0 || index >= static_cast<int>(documents.size())) {
            return;
        }
        isDocumentClosing = true;
        auto doc = documents[index];

        // Check for unsaved changes
        if (doc->isModified) {
            ConfirmSaveChanges(index, [this, doc, index](bool shouldContinue) {
                if (shouldContinue) {
                    // Delete autosave backup
                    if (!doc->autosaveBackupPath.empty()) {
                        autosaveManager.DeleteBackup(doc->autosaveBackupPath);
                    }

                    // Remove from documents list
                    documents.erase(documents.begin() + index);

                    // Remove tab
                    tabContainer->RemoveTab(index);

                    // Update active document index
                    if (documents.empty()) {
                        // No documents left - create a new one
                        CreateNewDocument();
                    } else if (activeDocumentIndex >= static_cast<int>(documents.size())) {
                        activeDocumentIndex = static_cast<int>(documents.size()) - 1;
                    }

                    // Notify callback
                    if (onTabClosed) {
                        onTabClosed(index);
                    }

                    UpdateStatusBar();
                }
                isDocumentClosing = false;
            });
        } else {
            // No unsaved changes - close directly
            if (!doc->autosaveBackupPath.empty()) {
                autosaveManager.DeleteBackup(doc->autosaveBackupPath);
            }

            documents.erase(documents.begin() + index);
            tabContainer->RemoveTab(index);

            if (documents.empty()) {
                CreateNewDocument();
            } else if (activeDocumentIndex >= static_cast<int>(documents.size())) {
                activeDocumentIndex = static_cast<int>(documents.size()) - 1;
            }

            if (onTabClosed) {
                onTabClosed(index);
            }

            UpdateStatusBar();
        }
        isDocumentClosing = false;
    }

void UltraCanvasTextEditor::SwitchToDocument(int index) {
        if (index < 0 || index >= static_cast<int>(documents.size())) {
            return;
        }

        // Avoid recursive callback: if already at this index, skip SetActiveTab
        // (SetActiveTab triggers onTabChange which calls SwitchToDocument again)
        bool needsTabSwitch = (activeDocumentIndex != index);
        
        activeDocumentIndex = index;

        // Update tab selection only if needed (prevents recursive callback)
        if (needsTabSwitch) {
            tabContainer->SetActiveTab(index);
        }

        // Update status bar
        UpdateStatusBar();

        // Notify callback
        if (onTabChanged) {
            onTabChanged(index);
        }
    }

    DocumentTab* UltraCanvasTextEditor::GetActiveDocument() {
        if (activeDocumentIndex >= 0 && activeDocumentIndex < static_cast<int>(documents.size())) {
            return documents[activeDocumentIndex].get();
        }
        return nullptr;
    }

    const DocumentTab* UltraCanvasTextEditor::GetActiveDocument() const {
        if (activeDocumentIndex >= 0 && activeDocumentIndex < static_cast<int>(documents.size())) {
            return documents[activeDocumentIndex].get();
        }
        return nullptr;
    }

    void UltraCanvasTextEditor::SetDocumentModified(int index, bool modified) {
        if (index < 0 || index >= static_cast<int>(documents.size())) {
            return;
        }

        auto doc = documents[index];
        if (doc->isModified != modified) {
            doc->isModified = modified;
            doc->lastModifiedTime = std::chrono::steady_clock::now();

            UpdateTabTitle(index);
            UpdateTabBadge(index);
            UpdateTitle();

            if (onModifiedChange) {
                onModifiedChange(modified, index);
            }
        }
    }

    void UltraCanvasTextEditor::UpdateTabTitle(int index) {
        if (index < 0 || index >= static_cast<int>(documents.size())) {
            return;
        }

        auto doc = documents[index];
        std::string title = doc->fileName;

        tabContainer->SetTabTitle(index, title);
    }

    void UltraCanvasTextEditor::UpdateTabBadge(int index) {
        if (index < 0 || index >= static_cast<int>(documents.size())) {
            return;
        }

        auto doc = documents[index];

        // Show "●" badge for modified documents
        tabContainer->SetTabModified(index, doc->isModified);
    }

// ===== FILE OPERATIONS =====

    bool UltraCanvasTextEditor::LoadFileIntoDocument(int docIndex, const std::string& filePath) {
        if (docIndex < 0 || docIndex >= static_cast<int>(documents.size())) {
            return false;
        }

        try {
            std::ifstream file(filePath);
            if (!file.is_open()) {
                std::cerr << "Failed to open file: " << filePath << std::endl;
                return false;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            file.close();

            auto doc = documents[docIndex];
            doc->textArea->SetText(buffer.str());
            doc->filePath = filePath;
            doc->isNewFile = false;
            doc->isModified = false;
            doc->lastSaveTime = std::chrono::steady_clock::now();

            // Update filename from path
            std::filesystem::path p(filePath);
            doc->fileName = p.filename().string();

            // Detect and set language from file extension
            std::string ext = p.extension().string();
            if (!ext.empty() && ext[0] == '.') {
                ext = ext.substr(1);
            }

            if (ext == "md") {
                doc->textArea->SetMarkdownHybridMode(true);
            } else if (doc->textArea->SetProgrammingLanguageByExtension(ext)) {
                doc->textArea->SetHighlightSyntax(true);
            } else {
                doc->textArea->SetHighlightSyntax(false);
            }
            doc->language = doc->textArea->GetCurrentProgrammingLanguage();

            UpdateTabTitle(docIndex);
            UpdateTabBadge(docIndex);
            UpdateTitle();

            if (onFileLoaded) {
                onFileLoaded(filePath, docIndex);
            }

            return true;

        } catch (const std::exception& e) {
            std::cerr << "Error loading file: " << e.what() << std::endl;
            return false;
        }
    }

    bool UltraCanvasTextEditor::SaveDocument(int docIndex) {
        if (docIndex < 0 || docIndex >= static_cast<int>(documents.size())) {
            return false;
        }

        auto doc = documents[docIndex];

        if (doc->filePath.empty()) {
            return false; // No file path set, use SaveDocumentAs
        }

        return SaveDocumentAs(docIndex, doc->filePath);
    }

    bool UltraCanvasTextEditor::SaveDocumentAs(int docIndex, const std::string& filePath) {
        if (docIndex < 0 || docIndex >= static_cast<int>(documents.size())) {
            return false;
        }

        try {
            std::ofstream file(filePath);
            if (!file.is_open()) {
                std::cerr << "Failed to save file: " << filePath << std::endl;
                return false;
            }

            auto doc = documents[docIndex];
            file << doc->textArea->GetText();
            file.close();

            doc->filePath = filePath;
            doc->isNewFile = false;
            doc->lastSaveTime = std::chrono::steady_clock::now();

            // Update filename
            std::filesystem::path p(filePath);
            doc->fileName = p.filename().string();

            SetDocumentModified(docIndex, false);

            // Delete autosave backup
            if (!doc->autosaveBackupPath.empty()) {
                autosaveManager.DeleteBackup(doc->autosaveBackupPath);
                doc->autosaveBackupPath = "";
            }

            UpdateTabTitle(docIndex);
            UpdateTitle();

            if (onFileSaved) {
                onFileSaved(filePath, docIndex);
            }

            return true;

        } catch (const std::exception& e) {
            std::cerr << "Error saving file: " << e.what() << std::endl;
            return false;
        }
    }

// ===== AUTOSAVE =====

    void UltraCanvasTextEditor::PerformAutosave() {
        if (!autosaveManager.ShouldAutosave()) {
            return;
        }

        for (size_t i = 0; i < documents.size(); i++) {
            if (documents[i]->isModified) {
                AutosaveDocument(static_cast<int>(i));
            }
        }
    }

    void UltraCanvasTextEditor::AutosaveDocument(int docIndex) {
        if (docIndex < 0 || docIndex >= static_cast<int>(documents.size())) {
            return;
        }

        auto doc = documents[docIndex];

        // Create backup path if not exists
        if (doc->autosaveBackupPath.empty()) {
            doc->autosaveBackupPath = autosaveManager.CreateBackupPath(doc->filePath, docIndex);
        }

        if (doc->autosaveBackupPath.empty()) {
            return; // Failed to create backup path
        }

        // Save backup
        std::string content = doc->textArea->GetText();
        if (autosaveManager.SaveBackup(doc->autosaveBackupPath, content)) {
            std::cout << "Autosaved: " << doc->fileName << std::endl;
        }
    }

    void UltraCanvasTextEditor::CheckForCrashRecovery() {
        if (hasCheckedForBackups) {
            return;
        }

        hasCheckedForBackups = true;

        // Find existing backups
        std::vector<std::string> backups = autosaveManager.FindExistingBackups();

        if (backups.empty()) {
            return;
        }

        // Show recovery dialog
        std::string message = "Found " + std::to_string(backups.size()) +
                              " autosaved file(s) from a previous session.\n\n" +
                              "Would you like to recover them?";

        UltraCanvasDialogManager::ShowMessage(
                message,
                "Crash Recovery",
                DialogType::Question,
                DialogButtons::YesNo,
                [this, backups](DialogResult result) {
                    if (result == DialogResult::Yes) {
                        // Recover all backups
                        for (const auto& backupPath : backups) {
                            OfferRecoveryForBackup(backupPath);
                        }
                    } else {
                        // Clean up backups
                        for (const auto& backupPath : backups) {
                            autosaveManager.DeleteBackup(backupPath);
                        }
                    }
                },
                nullptr
        );
    }

    void UltraCanvasTextEditor::OfferRecoveryForBackup(const std::string& backupPath) {
        std::string content;
        if (!autosaveManager.LoadBackup(backupPath, content)) {
            return;
        }

        // Create new document with recovered content
        int docIndex = CreateNewDocument("Recovered");
        auto doc = documents[docIndex];
        doc->textArea->SetText(content);
        doc->isModified = true;
        doc->autosaveBackupPath = backupPath; // Keep backup until saved

        UpdateTabTitle(docIndex);
        UpdateTabBadge(docIndex);
    }

// ===== MENU HANDLERS =====

    void UltraCanvasTextEditor::OnFileNew() {
        CreateNewDocument();
    }

    void UltraCanvasTextEditor::OnFileOpen() {
        UltraCanvasDialogManager::ShowOpenFileDialog(
                "Open File",
                config.fileFilters,
                "",
                [this](DialogResult result, const std::string& filePath) {
                    if (result == DialogResult::OK && !filePath.empty()) {
                        OpenDocumentFromPath(filePath);
                    }
                },
                nullptr
        );
    }

    void UltraCanvasTextEditor::OnFileSave() {
        auto doc = GetActiveDocument();
        if (!doc) return;

        if (doc->filePath.empty()) {
            OnFileSaveAs();
        } else {
            SaveDocument(activeDocumentIndex);
        }
    }

    void UltraCanvasTextEditor::OnFileSaveAs() {
        auto doc = GetActiveDocument();
        if (!doc) return;

        std::string defaultName = doc->fileName;
        if (defaultName.find("Untitled") == 0) {
            defaultName = "untitled.txt";
        }

        UltraCanvasDialogManager::ShowSaveFileDialog(
                "Save File As",
                config.fileFilters,
                "",
                defaultName,
                [this, doc](DialogResult result, const std::string& filePath) {
                    if (result == DialogResult::OK && !filePath.empty()) {
                        SaveDocumentAs(activeDocumentIndex, filePath);
                    }
                },
                nullptr
        );
    }

    void UltraCanvasTextEditor::OnFileSaveAll() {
        for (size_t i = 0; i < documents.size(); i++) {
            if (documents[i]->isModified) {
                if (!documents[i]->filePath.empty()) {
                    SaveDocument(static_cast<int>(i));
                }
            }
        }
    }

    void UltraCanvasTextEditor::OnFileClose() {
        CloseDocument(activeDocumentIndex);
    }

    void UltraCanvasTextEditor::OnFileCloseAll() {
        ConfirmCloseWithUnsavedChanges([this](bool shouldContinue) {
            if (shouldContinue) {
                // Prevent onTabClose callback from intercepting RemoveTab
                isDocumentClosing = true;

                // Close all tabs
                while (!documents.empty()) {
                    // Remove autosave backups
                    if (!documents[0]->autosaveBackupPath.empty()) {
                        autosaveManager.DeleteBackup(documents[0]->autosaveBackupPath);
                    }
                    documents.erase(documents.begin());
                    tabContainer->RemoveTab(0);

                    if (onTabClosed) {
                        onTabClosed(0);
                    }
                }

                activeDocumentIndex = -1;
                isDocumentClosing = false;

                // Create new empty document
                CreateNewDocument();
                UpdateStatusBar();
            }
        });
    }

    void UltraCanvasTextEditor::OnFileQuit() {
        ConfirmCloseWithUnsavedChanges([this](bool shouldContinue) {
            if (shouldContinue && onQuitRequest) {
                onQuitRequest();
            }
        });
    }

    void UltraCanvasTextEditor::OnEditUndo() {
        auto doc = GetActiveDocument();
        if (doc && doc->textArea) {
            doc->textArea->Undo();
        }
        UpdateMenuStates();
    }

    void UltraCanvasTextEditor::OnEditRedo() {
        auto doc = GetActiveDocument();
        if (doc && doc->textArea) {
            doc->textArea->Redo();
        }
        UpdateMenuStates();
    }

    void UltraCanvasTextEditor::OnEditCut() {
        auto doc = GetActiveDocument();
        if (doc && doc->textArea) {
            doc->textArea->CutSelection();
        }
    }

    void UltraCanvasTextEditor::OnEditCopy() {
        auto doc = GetActiveDocument();
        if (doc && doc->textArea) {
            doc->textArea->CopySelection();
        }
    }

    void UltraCanvasTextEditor::OnEditPaste() {
        auto doc = GetActiveDocument();
        if (doc && doc->textArea) {
            doc->textArea->PasteClipboard();
        }
    }

    void UltraCanvasTextEditor::OnEditSelectAll() {
        auto doc = GetActiveDocument();
        if (doc && doc->textArea) {
            doc->textArea->SelectAll();
        }
    }

    void UltraCanvasTextEditor::OnEditSearch() {
        auto doc = GetActiveDocument();
        if (!doc || !doc->textArea) return;

        // Create find dialog if not exists
        if (!findDialog) {
            findDialog = CreateFindDialog();

            // Wire up callbacks
            findDialog->onFindNext = [this](const std::string& searchText, bool caseSensitive, bool wholeWord) {
                auto doc = GetActiveDocument();
                if (doc && doc->textArea) {
                    doc->textArea->SetTextToFind(searchText, caseSensitive);
                    doc->textArea->FindNext();
                }
            };

            findDialog->onFindPrevious = [this](const std::string& searchText, bool caseSensitive, bool wholeWord) {
                auto doc = GetActiveDocument();
                if (doc && doc->textArea) {
                    doc->textArea->SetTextToFind(searchText, caseSensitive);
                    doc->textArea->FindPrevious();
                }
            };

            findDialog->onResult = [this](DialogResult res) {
                findDialog.reset();
            };
        }

        // Show dialog
        findDialog->ShowModal();
    }

    void UltraCanvasTextEditor::OnEditReplace() {
        auto doc = GetActiveDocument();
        if (!doc || !doc->textArea) return;

        // Create replace dialog if not exists
        if (!replaceDialog) {
            replaceDialog = CreateReplaceDialog();

            // Wire up callbacks
            replaceDialog->onFindNext = [this](const std::string& findText, bool caseSensitive, bool wholeWord) {
                auto doc = GetActiveDocument();
                if (doc && doc->textArea) {
                    doc->textArea->SetTextToFind(findText, caseSensitive);
                    doc->textArea->FindNext();
                }
            };

            replaceDialog->onReplace = [this](const std::string& findText, const std::string& replaceText,
                                              bool caseSensitive, bool wholeWord) {
                auto doc = GetActiveDocument();
                if (doc && doc->textArea) {
                    // Find current occurrence
                    doc->textArea->SetTextToFind(findText, caseSensitive);
                    // Replace single occurrence
                    doc->textArea->ReplaceText(findText, replaceText, false);
                }
            };

            replaceDialog->onReplaceAll = [this](const std::string& findText, const std::string& replaceText,
                                                 bool caseSensitive, bool wholeWord) {
                auto doc = GetActiveDocument();
                if (doc && doc->textArea) {
                    // Replace all occurrences
                    doc->textArea->ReplaceText(findText, replaceText, true);
                }
            };

            replaceDialog->onResult = [this](DialogResult res) {
                replaceDialog.reset();
            };
        }

        // Show dialog
        replaceDialog->ShowModal();
    }

    void UltraCanvasTextEditor::OnEditGoToLine() {
        auto doc = GetActiveDocument();
        if (!doc || !doc->textArea) return;

        // Create go to line dialog
        goToLineDialog = CreateGoToLineDialog();

        int currentLine = doc->textArea->GetCurrentLine();
        int totalLines = doc->textArea->GetLineCount();

        goToLineDialog->Initialize(currentLine + 1, totalLines); // +1 for 1-based line numbers

        // Wire up callback
        goToLineDialog->onGoToLine = [this](int lineNumber) {
            auto doc = GetActiveDocument();
            if (doc && doc->textArea) {
                doc->textArea->GoToLine(lineNumber - 1); // -1 for 0-based index
            }
        };

        goToLineDialog->onCancel = [this]() {
            goToLineDialog.reset();
        };

        // Show dialog
        goToLineDialog->ShowModal();
    }

    void UltraCanvasTextEditor::OnViewIncreaseFontSize() {
        IncreaseFontZoomLevel();
    }

    void UltraCanvasTextEditor::OnViewDecreaseFontSize() {
        DecreaseFontZoomLevel();
    }

    void UltraCanvasTextEditor::OnViewResetFontSize() {
        ResetFontZoomLevel();
    }

    void UltraCanvasTextEditor::OnViewToggleTheme() {
        ToggleTheme();
    }

    void UltraCanvasTextEditor::OnViewToggleLineNumbers() {
        // Apply to all open document TextAreas
        for (auto& doc : documents) {
            if (doc->textArea) {
                doc->textArea->SetShowLineNumbers(config.showLineNumbers);
            }
        }
    }

    void UltraCanvasTextEditor::OnViewToggleWordWrap() {
        // Apply to all open document TextAreas
        for (auto& doc : documents) {
            if (doc->textArea) {
                doc->textArea->SetWordWrap(config.wordWrap);
            }
        }
    }

    void UltraCanvasTextEditor::OnInfoAbout() {
        if (aboutDialog) return;

        DialogConfig config;
        config.title = "About UltraTexter";
        config.dialogType = DialogType::Custom;
        config.buttons = DialogButtons::NoButtons;
        config.width = 430;
        config.height = 500;

        aboutDialog = UltraCanvasDialogManager::CreateDialog(config);

        // Replace default layout with custom vertical layout
        auto mainLayout = CreateVBoxLayout(aboutDialog.get());
        mainLayout->SetSpacing(4);
        aboutDialog->SetPadding(20);

        // Logo image
        auto logo = std::make_shared<UltraCanvasImageElement>("AboutLogo", 0, 0, 0, 80, 80);
        logo->LoadFromFile("media/Logo_Texter.svg");
        logo->SetFitMode(ImageFitMode::Contain);
        logo->SetMargin(0, 0, 8, 0);
        mainLayout->AddUIElement(logo)->SetCrossAlignment(LayoutAlignment::Center);

        // Title
        auto titleLabel = std::make_shared<UltraCanvasLabel>("AboutTitle", 300, 25, "UltraTexter");
        titleLabel->SetFontSize(20);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        titleLabel->SetMargin(0, 0, 4, 0);
        mainLayout->AddUIElement(titleLabel)->SetWidthMode(SizeMode::Fill);

        // Version
        auto versionLabel = std::make_shared<UltraCanvasLabel>("AboutVersion", 300, 20, "Version " + version);
        versionLabel->SetFontSize(11);
        versionLabel->SetTextColor(Color(100, 100, 100));
        versionLabel->SetAlignment(TextAlignment::Center);
        versionLabel->SetMargin(0, 0, 10, 0);
        mainLayout->AddUIElement(versionLabel)->SetWidthMode(SizeMode::Fill);

        // Description
        auto descLabel = std::make_shared<UltraCanvasLabel>("AboutDesc", 350, 120,
                "A full-featured text editor built with UltraCanvas\nFramework\n\n"
                "Features:\n"
                "\u2022 Multi-file editing with tabs\n"
                "\u2022 Syntax highlighting\n"
                "\u2022 Autosave and crash recovery\n"
                "\u2022 Dark/Light themes\n"
                "\u2022 Full Markdown text editing");
        descLabel->SetFontSize(11);
        descLabel->SetTextColor(Color(60, 60, 60));
        descLabel->SetAlignment(TextAlignment::Left);
        descLabel->SetWordWrap(true);
        descLabel->SetAutoResize(true);
        descLabel->SetMargin(0, 20, 8, 20);
        mainLayout->AddUIElement(descLabel)->SetWidthMode(SizeMode::Fill);

        // Clickable URL label
        auto urlLabel = std::make_shared<UltraCanvasLabel>("AboutURL", 300, 20);
        urlLabel->SetText("<span color=\"blue\">http://www.ultraos.eu/</span>");
        urlLabel->SetTextIsMarkup(true);
        urlLabel->SetFontSize(11);
        urlLabel->SetAlignment(TextAlignment::Center);
        urlLabel->SetAutoResize(true);
        urlLabel->SetMouseCursor(UCMouseCursor::Hand);
        urlLabel->onClick = []() {
            system("xdg-open http://www.ultraos.eu/");
        };
        urlLabel->SetMargin(0, 0, 10, 20);
        mainLayout->AddUIElement(urlLabel)->SetWidthMode(SizeMode::Fill)->SetCrossAlignment(LayoutAlignment::Center);

        // Copyright
        auto copyrightLabel = std::make_shared<UltraCanvasLabel>("AboutCopyright", 350, 20,
                "\u00A9 2026 UltraCanvas GUI API of ULTRA OS");
        copyrightLabel->SetFontSize(10);
        copyrightLabel->SetTextColor(Color(120, 120, 120));
        copyrightLabel->SetAlignment(TextAlignment::Center);
        mainLayout->AddUIElement(copyrightLabel)->SetWidthMode(SizeMode::Fill)->SetCrossAlignment(LayoutAlignment::Center)->SetMainAlignment(LayoutAlignment::Center);;

        // Push OK button to the bottom
        mainLayout->AddStretch(1);

        // OK button
        auto okButton = std::make_shared<UltraCanvasButton>("AboutOK", 0, 0, 0, 80, 28);
        okButton->SetText("OK");
        okButton->onClick = [this]() {
            aboutDialog->CloseDialog(DialogResult::OK);
        };
        mainLayout->AddUIElement(okButton)->SetCrossAlignment(LayoutAlignment::Center);

        aboutDialog->onResult = [this](DialogResult) {
            aboutDialog.reset();
        };

        UltraCanvasDialogManager::ShowDialog(aboutDialog, nullptr);
    }


// ===== UI UPDATES =====

    void UltraCanvasTextEditor::UpdateStatusBar() {
        if (!statusLabel) return;

        auto doc = GetActiveDocument();
        if (!doc || !doc->textArea) {
            statusLabel->SetText("Ready");
            return;
        }
        // const auto& text = doc->textArea->GetText();
        // int graphemesCount = Grapheme::CountGraphemes(text);
        // int wordCount = Grapheme::CountWords(text);

        // Get cursor position
        int line = doc->textArea->GetCurrentLine();
        int col = doc->textArea->GetCurrentColumn();

        // Build status text
        std::stringstream status;
        status << "Line: " << (line + 1) << ", Col: " << (col + 1);
        status << " | " << config.defaultEncoding;
        status << " | " << doc->language;
        // status << " | Words: " << wordCount << " | Chars: " << graphemesCount;

        // Add selection info if exists
        // if (doc->textArea->HasSelection()) {
        //     std::string selectedText = doc->textArea->GetSelectedText();
        //     status << " | Selected: " << Grapheme::CountGraphemes(selectedText) << " chars";
        // }

        // Add modified indicator
        if (doc->isModified) {
            status << " | Modified";
        }

        statusLabel->SetText(status.str());
    }

    void UltraCanvasTextEditor::UpdateZoomDropdownSelection() {
        if (!zoomDropdown) return;

        zoomDropdown->SetSelectedIndex(fontZoomLevelIdx);
    }

    void UltraCanvasTextEditor::UpdateMenuStates() {
        // Update menu item enabled states based on current state
        // Note: This would require access to individual menu items
        // For now, this is a placeholder
    }

    void UltraCanvasTextEditor::UpdateTitle() {
        // Title is typically managed by the parent window
        // This is a placeholder for future implementation
    }

// ===== THEME MANAGEMENT =====

    void UltraCanvasTextEditor::ApplyThemeToDocument(int docIndex) {
        if (docIndex < 0 || docIndex >= static_cast<int>(documents.size())) {
            return;
        }

        auto doc = documents[docIndex];
        if (!doc->textArea) return;

        if (isDarkTheme) {
            doc->textArea->ApplyDarkTheme();
        } else {
            // Apply light theme
            doc->textArea->ApplyPlainTextStyle();
        }

        // Reapply syntax highlighting if needed
        if (doc->textArea->GetHighlightSyntax()) {
            doc->textArea->SetProgrammingLanguage(doc->language);
        }

        // Reapply current View settings (theme methods may reset these)
        doc->textArea->SetFontSize(GetFontSize());
        doc->textArea->SetShowLineNumbers(config.showLineNumbers);
        doc->textArea->SetWordWrap(config.wordWrap);
    }

    void UltraCanvasTextEditor::ApplyThemeToAllDocuments() {
        for (size_t i = 0; i < documents.size(); i++) {
            ApplyThemeToDocument(static_cast<int>(i));
        }

        // Update UI component colors
        if (isDarkTheme) {
            SetBackgroundColor(Color(30, 30, 30, 255));
            if (statusLabel) {
                statusLabel->SetBackgroundColor(Color(40, 40, 40, 255));
                statusLabel->SetTextColor(Color(200, 200, 200, 255));
            }
            if (zoomDropdown) {
                DropdownStyle zStyle = zoomDropdown->GetStyle();
                zStyle.normalColor = Color(40, 40, 40, 255);
                zStyle.hoverColor = Color(55, 55, 55, 255);
                zStyle.normalTextColor = Color(200, 200, 200, 255);
                zStyle.borderColor = Color(60, 60, 60, 255);
                zStyle.listBackgroundColor = Color(45, 45, 45, 255);
                zStyle.listBorderColor = Color(60, 60, 60, 255);
                zStyle.itemHoverColor = Color(65, 65, 65, 255);
                zStyle.itemSelectedColor = Color(55, 55, 55, 255);
                zoomDropdown->SetStyle(zStyle);
            }
            if (tabContainer) {
                tabContainer->tabBarColor = Color(40, 40, 40, 255);
                tabContainer->activeTabColor = Color(60, 60, 60, 255);
                tabContainer->inactiveTabColor = Color(50, 50, 50, 255);
            }
        } else {
            SetBackgroundColor(Color(240, 240, 240, 255));
            if (statusLabel) {
                statusLabel->SetBackgroundColor(Color(240, 240, 240, 255));
                statusLabel->SetTextColor(Color(80, 80, 80, 255));
            }
            if (zoomDropdown) {
                DropdownStyle zStyle = zoomDropdown->GetStyle();
                zStyle.normalColor = Color(240, 240, 240, 255);
                zStyle.hoverColor = Color(225, 225, 225, 255);
                zStyle.normalTextColor = Color(80, 80, 80, 255);
                zStyle.borderColor = Color(200, 200, 200, 255);
                zStyle.listBackgroundColor = Color(255, 255, 255, 255);
                zStyle.listBorderColor = Color(200, 200, 200, 255);
                zStyle.itemHoverColor = Color(230, 230, 230, 255);
                zStyle.itemSelectedColor = Color(220, 220, 220, 255);
                zoomDropdown->SetStyle(zStyle);
            }
            if (tabContainer) {
                tabContainer->tabBarColor = Color(240, 240, 240, 255);
                tabContainer->activeTabColor = Color(255, 255, 255, 255);
                tabContainer->inactiveTabColor = Color(220, 220, 220, 255);
            }
        }
        RequestRedraw();
    }

// ===== CALLBACKS =====

    int UltraCanvasTextEditor::FindDocumentIndexById(int documentId) const {
        for (int i = 0; i < static_cast<int>(documents.size()); i++) {
            if (documents[i]->documentId == documentId) {
                return i;
            }
        }
        return -1;
    }

    void UltraCanvasTextEditor::SetupDocumentCallbacks(int docIndex) {
        if (docIndex < 0 || docIndex >= static_cast<int>(documents.size())) {
            return;
        }

        auto doc = documents[docIndex];
        if (!doc->textArea) return;

        // Capture stable documentId instead of index, because indices
        // shift when earlier tabs are closed (stale-index bug fix).
        int docId = doc->documentId;

        // Text changed callback
        doc->textArea->onTextChanged = [this, docId](const std::string& text) {
            int currentIndex = FindDocumentIndexById(docId);
            if (currentIndex >= 0) {
                SetDocumentModified(currentIndex, true);
            }
            UpdateStatusBar();
        };

        // Cursor position changed callback
        doc->textArea->onCursorPositionChanged = [this](int line, int col) {
            UpdateStatusBar();
        };

        // Selection changed callback
        doc->textArea->onSelectionChanged = [this](int start, int end) {
            UpdateStatusBar();
        };
    }

    void UltraCanvasTextEditor::ConfirmSaveChanges(int docIndex, std::function<void(bool)> onComplete) {
        if (docIndex < 0 || docIndex >= static_cast<int>(documents.size())) {
            if (onComplete) onComplete(false);
            return;
        }

        auto doc = documents[docIndex];

        if (!doc->isModified) {
            if (onComplete) {
                onComplete(true);
            }
            return;
        }

        std::string message = "Save changes to \"" + doc->fileName + "\"?";

        UltraCanvasDialogManager::ShowMessage(
                message,
                "Save Changes?",
                DialogType::Question,
                DialogButtons::YesNoCancel,
                [this, docIndex, onComplete](DialogResult result) {
                    if (result == DialogResult::Yes) {
                        auto doc = documents[docIndex];
                        if (doc->filePath.empty()) {
                            UltraCanvasDialogManager::ShowSaveFileDialog(
                                    "Save File",
                                    config.fileFilters,
                                    "",
                                    doc->fileName,
                                    [this, docIndex, onComplete](DialogResult saveResult, const std::string& filePath) {
                                        if (saveResult == DialogResult::OK && !filePath.empty()) {
                                            bool saved = SaveDocumentAs(docIndex, filePath);
                                            if (onComplete) {
                                                onComplete(saved);
                                            }
                                        } else {
                                            if (onComplete) {
                                                onComplete(false);
                                            }
                                        }
                                    },
                                    nullptr
                            );
                        } else {
                            bool saved = SaveDocument(docIndex);
                            if (onComplete) {
                                onComplete(saved);
                            }
                        }
                    } else if (result == DialogResult::No) {
                        if (onComplete) {
                            onComplete(true);
                        }
                    } else {
                        if (onComplete) {
                            onComplete(false);
                        }
                    }
                },
                nullptr
        );
    }

    void UltraCanvasTextEditor::ConfirmCloseWithUnsavedChanges(std::function<void(bool)> onComplete) {
        std::vector<int> modifiedDocs;
        for (size_t i = 0; i < documents.size(); i++) {
            if (documents[i]->isModified) {
                modifiedDocs.push_back(static_cast<int>(i));
            }
        }

        if (modifiedDocs.empty()) {
            if (onComplete) {
                onComplete(true);
            }
            return;
        }

        std::string message = std::to_string(modifiedDocs.size()) +
                              " file(s) have unsaved changes.\n\n" +
                              "Save all before closing?";

        UltraCanvasDialogManager::ShowMessage(
                message,
                "Unsaved Changes",
                DialogType::Question,
                DialogButtons::YesNoCancel,
                [this, modifiedDocs, onComplete](DialogResult result) {
                    if (result == DialogResult::Yes) {
                        bool allSaved = true;
                        for (int idx : modifiedDocs) {
                            if (!documents[idx]->filePath.empty()) {
                                if (!SaveDocument(idx)) {
                                    allSaved = false;
                                }
                            }
                        }
                        if (onComplete) {
                            onComplete(allSaved);
                        }
                    } else if (result == DialogResult::No) {
                        if (onComplete) {
                            onComplete(true);
                        }
                    } else {
                        if (onComplete) {
                            onComplete(false);
                        }
                    }
                },
                nullptr
        );
    }

// ===== PUBLIC API =====

    void UltraCanvasTextEditor::Render(IRenderContext* ctx) {
        if (autosaveManager.IsEnabled() && autosaveManager.ShouldAutosave()) {
            PerformAutosave();
        }

        UltraCanvasContainer::Render(ctx);
    }

    bool UltraCanvasTextEditor::OnEvent(const UCEvent& event) {
        if (event.type == UCEventType::KeyDown) {
            if (event.ctrl && event.virtualKey == UCKeys::N) {
                OnFileNew();
                return true;
            }
            if (event.ctrl && event.virtualKey == UCKeys::O) {
                OnFileOpen();
                return true;
            }
            if (event.ctrl && !event.shift && event.virtualKey == UCKeys::S) {
                OnFileSave();
                return true;
            }
            if (event.ctrl && event.shift && event.virtualKey == UCKeys::S) {
                OnFileSaveAs();
                return true;
            }
            if (event.ctrl && event.virtualKey == UCKeys::W) {
                OnFileClose();
                return true;
            }
            if (event.ctrl && event.virtualKey == UCKeys::T) {
                OnViewToggleTheme();
                return true;
            }
            if (event.ctrl && event.virtualKey == UCKeys::F) {
                OnEditSearch();
                return true;
            }
            if (event.ctrl && event.virtualKey == UCKeys::H) {
                OnEditReplace();
                return true;
            }
            if (event.ctrl && event.virtualKey == UCKeys::G) {
                OnEditGoToLine();
                return true;
            }
            if (event.ctrl && (event.virtualKey == UCKeys::Plus || event.virtualKey == UCKeys::NumPadPlus)) {
                OnViewIncreaseFontSize();
                return true;
            }
            if (event.ctrl && (event.virtualKey == UCKeys::Minus || event.virtualKey == UCKeys::NumPadMinus)) {
                OnViewDecreaseFontSize();
                return true;
            }
        }

        return UltraCanvasContainer::OnEvent(event);
    }

    int UltraCanvasTextEditor::OpenFile(const std::string& filePath) {
        return OpenDocumentFromPath(filePath);
    }

    int UltraCanvasTextEditor::NewFile() {
        return CreateNewDocument();
    }

    bool UltraCanvasTextEditor::SaveActiveFile() {
        return SaveDocument(activeDocumentIndex);
    }

    bool UltraCanvasTextEditor::SaveActiveFileAs(const std::string& filePath) {
        return SaveDocumentAs(activeDocumentIndex, filePath);
    }

    bool UltraCanvasTextEditor::SaveAllFiles() {
        bool allSaved = true;
        for (size_t i = 0; i < documents.size(); i++) {
            if (documents[i]->isModified && !documents[i]->filePath.empty()) {
                if (!SaveDocument(static_cast<int>(i))) {
                    allSaved = false;
                }
            }
        }
        return allSaved;
    }

    void UltraCanvasTextEditor::CloseActiveTab() {
        CloseDocument(activeDocumentIndex);
    }

    void UltraCanvasTextEditor::CloseAllTabs() {
        OnFileCloseAll();
    }

    std::string UltraCanvasTextEditor::GetActiveFilePath() const {
        auto doc = GetActiveDocument();
        return doc ? doc->filePath : "";
    }

    bool UltraCanvasTextEditor::HasUnsavedChanges() const {
        auto doc = GetActiveDocument();
        return doc ? doc->isModified : false;
    }

    bool UltraCanvasTextEditor::HasAnyUnsavedChanges() const {
        for (const auto& doc : documents) {
            if (doc->isModified) {
                return true;
            }
        }
        return false;
    }

    std::string UltraCanvasTextEditor::GetText() const {
        auto doc = GetActiveDocument();
        return doc && doc->textArea ? doc->textArea->GetText() : "";
    }

    void UltraCanvasTextEditor::SetText(const std::string& text) {
        auto doc = GetActiveDocument();
        if (doc && doc->textArea) {
            doc->textArea->SetText(text);
        }
    }

    void UltraCanvasTextEditor::Undo() {
        OnEditUndo();
    }

    void UltraCanvasTextEditor::Redo() {
        OnEditRedo();
    }

    bool UltraCanvasTextEditor::CanUndo() const {
        auto doc = GetActiveDocument();
        return doc && doc->textArea ? doc->textArea->CanUndo() : false;
    }

    bool UltraCanvasTextEditor::CanRedo() const {
        auto doc = GetActiveDocument();
        return doc && doc->textArea ? doc->textArea->CanRedo() : false;
    }

    void UltraCanvasTextEditor::SetLanguage(const std::string& language) {
        auto doc = GetActiveDocument();
        if (doc) {
            doc->language = language;
            if (doc->textArea) {
                if (language != "Plain Text") {
                    doc->textArea->SetHighlightSyntax(true);
                    doc->textArea->SetProgrammingLanguage(language);
                } else {
                    doc->textArea->SetHighlightSyntax(false);
                }
            }
        }
    }

    std::string UltraCanvasTextEditor::GetLanguage() const {
        auto doc = GetActiveDocument();
        return doc ? doc->language : "Plain Text";
    }

    void UltraCanvasTextEditor::ApplyDarkTheme() {
        isDarkTheme = true;
        ApplyThemeToAllDocuments();
    }

    void UltraCanvasTextEditor::ApplyLightTheme() {
        isDarkTheme = false;
        ApplyThemeToAllDocuments();
    }

    void UltraCanvasTextEditor::ToggleTheme() {
        isDarkTheme = !isDarkTheme;
        ApplyThemeToAllDocuments();
    }

    void UltraCanvasTextEditor::SetDefaultFontSize(float size) {
        config.defaultFontSize = std::max(4.0f, std::min(72.0f, size));
        SetFontZoomLevel(fontZoomLevelIdx);
    }

    void UltraCanvasTextEditor::SetFontZoomLevel(int lvl) {
        if (lvl < 0 || lvl >= fontZoomLevels.size()) {
            return;
        }
        fontZoomLevelIdx = lvl;
        float fontSize = config.defaultFontSize * fontZoomLevels[fontZoomLevelIdx] / 100.0;
        fontSize = std::max(4.0f, std::min(72.0f, fontSize));

        // Apply to all open document TextAreas
        for (auto& doc : documents) {
            if (doc->textArea) {
                doc->textArea->SetFontSize(fontSize);
            }
        }

        UpdateZoomDropdownSelection();
        UpdateStatusBar();
    }

    void UltraCanvasTextEditor::IncreaseFontZoomLevel() {
        SetFontZoomLevel(fontZoomLevelIdx + 1);
    }

    void UltraCanvasTextEditor::DecreaseFontZoomLevel() {
        SetFontZoomLevel(fontZoomLevelIdx - 1);
    }

    void UltraCanvasTextEditor::ResetFontZoomLevel() {
        for(int i = 0; i < fontZoomLevels.size(); i++) {
            if (fontZoomLevels[i] == 100) {
                SetFontZoomLevel(i);
                break;
            }
        }
    }

    void UltraCanvasTextEditor::SetAutosaveEnabled(bool enable) {
        autosaveManager.SetEnabled(enable);
    }

    bool UltraCanvasTextEditor::IsAutosaveEnabled() const {
        return autosaveManager.IsEnabled();
    }

    void UltraCanvasTextEditor::SetAutosaveInterval(int seconds) {
        autosaveManager.SetInterval(seconds);
    }

    void UltraCanvasTextEditor::AutosaveNow() {
        PerformAutosave();
    }

// ===== FACTORY FUNCTIONS =====

    std::shared_ptr<UltraCanvasTextEditor> CreateTextEditor(
            const std::string& identifier,
            long id,
            int x, int y,
            int width, int height)
    {
        return std::make_shared<UltraCanvasTextEditor>(
                identifier, id, x, y, width, height, TextEditorConfig());
    }

    std::shared_ptr<UltraCanvasTextEditor> CreateTextEditor(
            const std::string& identifier,
            long id,
            int x, int y,
            int width, int height,
            const TextEditorConfig& config)
    {
        return std::make_shared<UltraCanvasTextEditor>(
                identifier, id, x, y, width, height, config);
    }

    std::shared_ptr<UltraCanvasTextEditor> CreateDarkTextEditor(
            const std::string& identifier,
            long id,
            int x, int y,
            int width, int height)
    {
        TextEditorConfig config;
        config.darkTheme = true;
        return std::make_shared<UltraCanvasTextEditor>(
                identifier, id, x, y, width, height, config);
    }

} // namespace UltraCanvas