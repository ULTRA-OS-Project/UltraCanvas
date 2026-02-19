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
#include "UltraCanvasEncoding.h"
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
            , markdownToolbarWidth(44)
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
                        MenuItemData::ActionWithShortcut("New", "Ctrl+N", "media/icons/texter/add-document.svg", [this]() {
                            OnFileNew();
                        }),
                        MenuItemData::ActionWithShortcut("Open...", "Ctrl+O", "media/icons/texter/folder-open.svg", [this]() {
                            OnFileOpen();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Save", "Ctrl+S", "media/icons/texter/save.svg", [this]() {
                            OnFileSave();
                        }),
                        MenuItemData::ActionWithShortcut("Save As...", "Ctrl+Shift+S", "media/icons/texter/save.svg", [this]() {
                            OnFileSaveAs();
                        }),
                        MenuItemData::Action("Save All", "media/icons/texter/save.svg", [this]() {
                            OnFileSaveAll();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Close Tab", "Ctrl+W", "media/icons/texter/close_tab.svg", [this]() {
                            OnFileClose();
                        }),
                        MenuItemData::Action("Close All", "media/icons/texter/close_tab.svg", [this]() {
                            OnFileCloseAll();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Quit", "Alt+F4", "media/icons/texter/exit.svg", [this]() {
                            OnFileQuit();
                        })
                })

                        // ===== EDIT MENU =====
                .AddSubmenu("Edit", {
                        // "↶ Undo"
                        MenuItemData::ActionWithShortcut("Undo", "Ctrl+Z", "media/icons/texter/undo.svg", [this]() {
                            OnEditUndo();
                        }),
                        // "↷ Redo"
                        MenuItemData::ActionWithShortcut("Redo", "Ctrl+Y", "media/icons/texter/redo.svg", [this]() {
                            OnEditRedo();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Cut", "Ctrl+X", "media/icons/texter/scissors.svg", [this]() {
                            OnEditCut();
                        }),
                        MenuItemData::ActionWithShortcut("Copy", "Ctrl+C", "media/icons/texter/copy.svg", [this]() {
                            OnEditCopy();
                        }),
                        MenuItemData::ActionWithShortcut("Paste", "Ctrl+V", "media/icons/texter/paste.svg", [this]() {
                            OnEditPaste();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Find...", "Ctrl+F", "media/icons/texter/search.svg", [this]() {
                            OnEditSearch();
                        }),
                        MenuItemData::ActionWithShortcut("Replace...", "Ctrl+H",  "media/icons/texter/replace.svg", [this]() {
                            OnEditReplace();
                        }),
                        MenuItemData::ActionWithShortcut("Go to Line...", "Ctrl+G", "media/icons/texter/gotoline.svg", [this]() {
                            OnEditGoToLine();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Select All", "Ctrl+A", [this]() {
                            OnEditSelectAll();
                        })
                })

                        // ===== VIEW MENU =====
                .AddSubmenu("View", {
                        MenuItemData::ActionWithShortcut("Increase Font Size", "Ctrl++", "media/icons/texter/zoom-in.svg", [this]() {
                            OnViewIncreaseFontSize();
                        }),
                        MenuItemData::ActionWithShortcut("Decrease Font Size", "Ctrl+-", "media/icons/texter/zoom-out.svg", [this]() {
                            OnViewDecreaseFontSize();
                        }),
                        MenuItemData::ActionWithShortcut("Reset Font Size", "Ctrl+0", [this]() {
                            OnViewResetFontSize();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Toggle Theme", "Ctrl+T", "media/icons/texter/theme_mode.svg", [this]() {
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
                .SetDimensions(0, 0, GetWidth(), toolbarHeight)
                .AddButton("new", "", "media/icons/texter/add-document.svg", [this]() { OnFileNew(); })
                .AddButton("open", "", "media/icons/texter/folder-open.svg", [this]() { OnFileOpen(); })
                .AddButton("save", "", "media/icons/texter/save.svg", [this]() { OnFileSave(); })
                .AddSeparator()
                .AddButton("cut", "", "media/icons/texter/scissors.svg", [this]() { OnEditCut(); })
                .AddButton("copy", "", "media/icons/texter/copy.svg", [this]() { OnEditCopy(); })
                .AddButton("paste", "", "media/icons/texter/paste.svg", [this]() { OnEditPaste(); })
                .AddSeparator()
                .AddButton("undo", "", "media/icons/texter/undo.svg", [this]() { OnEditUndo(); })
                .AddButton("redo", "", "media/icons/texter/redo.svg", [this]() { OnEditRedo(); })
                .AddSeparator()
                .AddButton("search", "", "media/icons/texter/search.svg", [this]() { OnEditSearch(); })
                .AddButton("replace", "", "media/icons/texter/replace.svg", [this]() { OnEditReplace(); })
                .AddSeparator()
                .AddButton("zoom-in", "", "media/icons/texter/zoom-in.svg", [this]() { OnViewIncreaseFontSize(); })
                .AddButton("zoom-out", "", "media/icons/texter/zoom-out.svg", [this]() { OnViewDecreaseFontSize(); })
                .Build();

        // Disable focus on toolbar buttons so they don't steal focus from the text area
        for (int i = 0; i < toolbar->GetItemCount(); i++) {
            auto item = toolbar->GetItemAt(i);
            if (item) {
                auto btn = std::dynamic_pointer_cast<UltraCanvasButton>(item->GetWidget());
                if (btn) btn->SetAcceptsFocus(false);
            }
        }

        // Wrap toolbar(s) in an HBox container
        toolbarContainer = std::make_shared<UltraCanvasContainer>(
                "ToolbarContainer", 201, 0, toolbarY, GetWidth(), toolbarHeight);
        auto* hbox = CreateHBoxLayout(toolbarContainer.get());
        hbox->SetSpacing(0);
        hbox->AddUIElement(toolbar)->SetStretch(1)->SetHeightMode(SizeMode::Fill);

        // Build and add the markdown toolbar (initially hidden)
        SetupMarkdownToolbar();

        AddChild(toolbarContainer);
    }

    void UltraCanvasTextEditor::SetupMarkdownToolbar() {
        markdownToolbar = UltraCanvasToolbarBuilder("MarkdownToolbar", 202)
                .SetOrientation(ToolbarOrientation::Vertical)
                .SetAppearance(ToolbarAppearance::Flat())
                .SetDimensions(0, 0, markdownToolbarWidth, 400)
                .AddButton("md-bold", "", "media/icons/texter/md-bold.svg",
                    [this]() { InsertMarkdownSnippet("**", "**", "bold text"); })
                .AddButton("md-italic", "", "media/icons/texter/md-italic.svg",
                    [this]() { InsertMarkdownSnippet("*", "*", "emphasized text"); })
                .AddSeparator()
                .AddButton("md-heading", "", "media/icons/texter/md-heading.svg",
                    [this]() { InsertMarkdownSnippet("## ", "", "Heading"); })
                .AddSeparator()
                .AddButton("md-ul", "", "media/icons/texter/md-list-unordered.svg",
                    [this]() { InsertMarkdownSnippet("- ", "", "list item"); })
                .AddButton("md-ol", "", "media/icons/texter/md-list-ordered.svg",
                    [this]() { InsertMarkdownSnippet("1. ", "", "list item"); })
                .AddButton("md-checklist", "", "media/icons/texter/md-checklist.svg",
                    [this]() { InsertMarkdownSnippet("- [ ] ", "", "list item"); })
                .AddSeparator()
                .AddButton("md-quote", "", "media/icons/texter/md-quote.svg",
                    [this]() { InsertMarkdownSnippet("> ", "", "quote"); })
                .AddButton("md-code", "", "media/icons/texter/md-code.svg",
                    [this]() { InsertMarkdownSnippet("```\n", "\n```", "code"); })
                .AddButton("md-table", "", "media/icons/texter/md-table.svg",
                    [this]() {
                        InsertMarkdownSnippet(
                            "| ", " | Column 2 |\n|----------|----------|\n|          |          |",
                            "Column 1");
                    })
                .Build();

        // Disable focus on markdown toolbar buttons
        for (int i = 0; i < markdownToolbar->GetItemCount(); i++) {
            auto item = markdownToolbar->GetItemAt(i);
            if (item) {
                auto btn = std::dynamic_pointer_cast<UltraCanvasButton>(item->GetWidget());
                if (btn) btn->SetAcceptsFocus(false);
            }
        }

        markdownToolbar->SetVisible(false);

        AddChild(markdownToolbar);
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
        int languageDropdownWidth = 140;
        int encodingDropdownWidth = 160;
        int zoomDropdownWidth = 80;
        int gap = 4;
        int xPos = gap;

        // Create language dropdown (leftmost)
        languageDropdown = std::make_shared<UltraCanvasDropdown>(
                "LanguageDropdown", 303,
                xPos, yPos + 2,
                languageDropdownWidth, statusBarHeight - 4
        );

        languageDropdown->AddItem("Plain Text", "Plain Text");
        {
            // Get supported languages and sort them
            UltraCanvasTextArea tempArea("_tmp", 0, 0, 0, 0, 0);
            auto languages = tempArea.GetSupportedLanguages();
            std::sort(languages.begin(), languages.end());
            for (const auto& lang : languages) {
                languageDropdown->AddItem(lang, lang);
            }
        }
        languageDropdown->SetSelectedIndex(0); // Plain Text

        DropdownStyle langStyle = languageDropdown->GetStyle();
        langStyle.fontSize = 10;
        languageDropdown->SetStyle(langStyle);

        languageDropdown->onSelectionChanged = [this](int index, const DropdownItem& item) {
            OnLanguageChanged(index, item);
        };

        AddChild(languageDropdown);
        xPos += languageDropdownWidth + gap;

        // Create encoding dropdown
        encodingDropdown = std::make_shared<UltraCanvasDropdown>(
                "EncodingDropdown", 302,
                xPos, yPos + 2,
                encodingDropdownWidth, statusBarHeight - 4
        );

        auto encodings = GetSupportedEncodings();
        for (size_t i = 0; i < encodings.size(); i++) {
            encodingDropdown->AddItem(encodings[i].displayName, encodings[i].iconvName);
        }
        encodingDropdown->SetSelectedIndex(0); // Default: UTF-8

        DropdownStyle encStyle = encodingDropdown->GetStyle();
        encStyle.fontSize = 10;
        encodingDropdown->SetStyle(encStyle);

        encodingDropdown->onSelectionChanged = [this](int index, const DropdownItem& item) {
            OnEncodingChanged(index, item);
        };

        AddChild(encodingDropdown);
        xPos += encodingDropdownWidth + gap;

        // Create zoom dropdown
        zoomDropdown = std::make_shared<UltraCanvasDropdown>(
                "ZoomDropdown", 301,
                xPos, yPos + 2,
                zoomDropdownWidth, statusBarHeight - 4
        );
        fontZoomLevelIdx = 4;
        for(size_t i = 0; i < fontZoomLevels.size(); i++) {
            auto zoomLabel = fmt::format("{}%", fontZoomLevels[i]);
            auto zoomValue = fmt::format("{}", i);
            zoomDropdown->AddItem(zoomLabel, zoomValue);
            if (fontZoomLevels[i] == 100) {
                fontZoomLevelIdx = static_cast<int>(i);
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
        xPos += zoomDropdownWidth + gap;

        // Status label fills remaining space to the right
        statusLabel = std::make_shared<UltraCanvasLabel>(
                "StatusBar", 300,
                xPos, yPos + 4,
                GetWidth() - xPos - 4, statusBarHeight - 8
        );
        statusLabel->SetText("Ready");
        statusLabel->SetFontSize(10);
        statusLabel->SetTextColor(Color(80, 80, 80, 255));
        statusLabel->SetBackgroundColor(Color(240, 240, 240, 255));

        AddChild(statusLabel);
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
        if (toolbarContainer && config.showToolbar) {
            toolbarContainer->SetBounds(Rect2Di(0, yPos, w, toolbarHeight));
            yPos += toolbarHeight;
        }

        // ===== Markdown toolbar (vertical, left side) =====
        int mdToolbarW = 0;
        if (markdownToolbar && markdownToolbar->IsVisible()) {
            int contentH = h - yPos - (config.showStatusBar ? statusBarHeight : 0);
            if (contentH < 0) contentH = 0;
            markdownToolbar->SetBounds(Rect2Di(0, yPos, markdownToolbarWidth, contentH));
            mdToolbarW = markdownToolbarWidth;
        }

        // ===== Tab container (fills remaining space minus status bar) =====
        if (tabContainer) {
            int tabAreaHeight = h - yPos - (config.showStatusBar ? statusBarHeight : 0);
            if (tabAreaHeight < 0) tabAreaHeight = 0;
            tabContainer->SetBounds(Rect2Di(mdToolbarW, yPos, w - mdToolbarW, tabAreaHeight));
        }

        // ===== Status bar =====
        if (config.showStatusBar) {
            int statusY = h - statusBarHeight;
            int langW = 140, encW = 160, zoomW = 80;
            int gap = 4;
            int xPos = gap;

            // Language dropdown: leftmost
            if (languageDropdown) {
                languageDropdown->SetBounds(Rect2Di(
                    xPos, statusY + 2, langW, statusBarHeight - 4
                ));
                xPos += langW + gap;
            }

            // Encoding dropdown
            if (encodingDropdown) {
                encodingDropdown->SetBounds(Rect2Di(
                    xPos, statusY + 2, encW, statusBarHeight - 4
                ));
                xPos += encW + gap;
            }

            // Zoom dropdown
            if (zoomDropdown) {
                zoomDropdown->SetBounds(Rect2Di(
                    xPos, statusY + 2, zoomW, statusBarHeight - 4
                ));
                xPos += zoomW + gap;
            }

            // Status label: fills remaining space to the right
            if (statusLabel) {
                statusLabel->SetBounds(Rect2Di(
                    xPos, statusY + 4,
                    w - xPos - 4, statusBarHeight - 8
                ));
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
        UpdateMarkdownToolbarVisibility();

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

        // Update status bar and dropdowns
        UpdateStatusBar();
        UpdateEncodingDropdown();
        UpdateLanguageDropdown();
        UpdateMarkdownToolbarVisibility();

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
            // Read raw bytes from file in binary mode
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                std::cerr << "Failed to open file: " << filePath << std::endl;
                return false;
            }

            std::streamsize fileSize = file.tellg();
            file.seekg(0, std::ios::beg);

            std::vector<uint8_t> rawBytes(static_cast<size_t>(fileSize));
            if (fileSize > 0) {
                file.read(reinterpret_cast<char*>(rawBytes.data()), fileSize);
            }
            file.close();

            auto doc = documents[docIndex];

            // Detect encoding
            DetectionResult detection = DetectEncoding(rawBytes);
            doc->encoding = detection.encoding;

            // Handle BOM
            size_t bomLength = 0;
            std::string bomEncoding = DetectBOM(rawBytes, bomLength);
            doc->hasBOM = !bomEncoding.empty();

            // Store raw bytes for potential re-encoding (if not too large)
            if (rawBytes.size() <= MAX_RAW_BYTES_CACHE) {
                doc->originalRawBytes = rawBytes;
            } else {
                doc->originalRawBytes.clear();
            }

            // Prepare content bytes (strip BOM if present)
            std::vector<uint8_t> contentBytes;
            if (bomLength > 0 && bomLength < rawBytes.size()) {
                contentBytes.assign(rawBytes.begin() + bomLength, rawBytes.end());
            } else if (bomLength == 0) {
                contentBytes = std::move(rawBytes);
            }
            // else: file is only BOM bytes, contentBytes stays empty

            // Convert to UTF-8
            std::string utf8Text;
            if (!ConvertToUtf8(contentBytes, doc->encoding, utf8Text)) {
                // Conversion failed; fallback to ISO-8859-1 (always succeeds)
                std::cerr << "Encoding conversion failed for " << doc->encoding
                          << ", falling back to ISO-8859-1" << std::endl;
                doc->encoding = "ISO-8859-1";
                ConvertToUtf8(contentBytes, "ISO-8859-1", utf8Text);
            }

            // Set text in editor
            doc->textArea->SetText(utf8Text, false);
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
            UpdateEncodingDropdown();
            UpdateLanguageDropdown();
            UpdateMarkdownToolbarVisibility();

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
            auto doc = documents[docIndex];
            std::string utf8Text = doc->textArea->GetText();

            // Convert from UTF-8 to document encoding
            std::vector<uint8_t> outputBytes;

            if (doc->encoding == "UTF-8") {
                outputBytes.assign(utf8Text.begin(), utf8Text.end());
            } else {
                if (!ConvertFromUtf8(utf8Text, doc->encoding, outputBytes)) {
                    std::cerr << "Failed to convert to encoding " << doc->encoding
                              << " while saving " << filePath
                              << ", falling back to UTF-8" << std::endl;
                    outputBytes.assign(utf8Text.begin(), utf8Text.end());
                    doc->encoding = "UTF-8";
                    UpdateEncodingDropdown();
                }
            }

            std::ofstream file(filePath, std::ios::binary);
            if (!file.is_open()) {
                std::cerr << "Failed to save file: " << filePath << std::endl;
                return false;
            }

            // Write BOM if the original file had one
            if (doc->hasBOM) {
                if (doc->encoding == "UTF-8") {
                    const uint8_t bom[] = {0xEF, 0xBB, 0xBF};
                    file.write(reinterpret_cast<const char*>(bom), 3);
                } else if (doc->encoding == "UTF-16LE") {
                    const uint8_t bom[] = {0xFF, 0xFE};
                    file.write(reinterpret_cast<const char*>(bom), 2);
                } else if (doc->encoding == "UTF-16BE") {
                    const uint8_t bom[] = {0xFE, 0xFF};
                    file.write(reinterpret_cast<const char*>(bom), 2);
                }
            }

            file.write(reinterpret_cast<const char*>(outputBytes.data()),
                       static_cast<std::streamsize>(outputBytes.size()));
            file.close();

            doc->filePath = filePath;
            bool wasNewFile = doc->isNewFile;
            doc->isNewFile = false;
            doc->lastSaveTime = std::chrono::steady_clock::now();

            // Update filename
            std::filesystem::path p(filePath);
            doc->fileName = p.filename().string();

            // Detect language from file extension on first save
            if (wasNewFile) {
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
                UpdateLanguageDropdown();
                UpdateMarkdownToolbarVisibility();
            }

            // Clear raw bytes cache since we just saved a fresh version
            doc->originalRawBytes.clear();

            SetDocumentModified(docIndex, false);

            // Delete autosave backup
            if (!doc->autosaveBackupPath.empty()) {
                autosaveManager.DeleteBackup(doc->autosaveBackupPath);
                doc->autosaveBackupPath = "";
            }

            UpdateTabTitle(docIndex);
            UpdateTitle();
            UpdateStatusBar();

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
        doc->textArea->SetText(content, false);
        doc->isModified = true;
        doc->autosaveBackupPath = backupPath; // Keep backup until saved

        UpdateTabTitle(docIndex);
        UpdateTabBadge(docIndex);
    }

// ===== MARKDOWN TOOLBAR =====

    bool UltraCanvasTextEditor::IsMarkdownMode() const {
        auto doc = GetActiveDocument();
        return doc && doc->language == "Markdown";
    }

    void UltraCanvasTextEditor::UpdateMarkdownToolbarVisibility() {
        if (!markdownToolbar) return;
        bool show = IsMarkdownMode();
        if (markdownToolbar->IsVisible() != show) {
            markdownToolbar->SetVisible(show);
            UpdateChildLayout();
        }
    }

    void UltraCanvasTextEditor::InsertMarkdownSnippet(
            const std::string& prefix, const std::string& suffix,
            const std::string& sampleText) {
        auto doc = GetActiveDocument();
        if (!doc || !doc->textArea) return;

        if (doc->textArea->HasSelection()) {
            // Wrap selected text with markdown syntax
            std::string selectedText = doc->textArea->GetSelectedText();
            doc->textArea->DeleteSelection();
            doc->textArea->InsertText(prefix + selectedText + suffix);
        } else {
            // Insert snippet with sample text, then select the sample
            int cursorPos = doc->textArea->GetCursorPosition();
            doc->textArea->InsertText(prefix + sampleText + suffix);

            // Select just the sample text so user can type to replace it
            int selStart = cursorPos + static_cast<int>(prefix.size());
            int selEnd = selStart + static_cast<int>(sampleText.size());
            doc->textArea->SetSelection(selStart, selEnd);
        }
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
        config.height = 520;

        aboutDialog = UltraCanvasDialogManager::CreateDialog(config);

        // Replace default layout with custom vertical layout
        auto mainLayout = CreateVBoxLayout(aboutDialog.get());
        mainLayout->SetSpacing(4);
        aboutDialog->SetPadding(20);

        // Logo image
        auto logo = std::make_shared<UltraCanvasImageElement>("AboutLogo", 0, 0, 0, 74, 74);
        logo->LoadFromFile("media/Logo_Texter.png");
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
        
        mainLayout->AddSpacing(10);

        // Copyright
        auto copyrightLabel = std::make_shared<UltraCanvasLabel>("AboutCopyright", 350, 20,
                "\u00A9 2026 UltraCanvas GUI API of ULTRA OS");
        copyrightLabel->SetFontSize(10);
        copyrightLabel->SetTextColor(Color(120, 120, 120));
        copyrightLabel->SetAlignment(TextAlignment::Center);
        mainLayout->AddUIElement(copyrightLabel)->SetWidthMode(SizeMode::Fill)->SetCrossAlignment(LayoutAlignment::Center)->SetMainAlignment(LayoutAlignment::Center);;

        // Clickable URL label
        auto urlLabel = std::make_shared<UltraCanvasLabel>("AboutURL", 300, 20);
        urlLabel->SetText("<span color=\"blue\">http://www.ultraos.eu/</span>");
        urlLabel->SetTextIsMarkup(true);
        urlLabel->SetFontSize(11);
        urlLabel->SetAlignment(TextAlignment::Center);
        urlLabel->SetMouseCursor(UCMouseCursor::Hand);
        urlLabel->onClick = []() {
            system("xdg-open http://www.ultraos.eu/");
        };
        urlLabel->SetMargin(0, 0, 10, 20);
        mainLayout->AddUIElement(urlLabel)->SetWidthMode(SizeMode::Fill)->SetCrossAlignment(LayoutAlignment::Center);

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

        zoomDropdown->SetSelectedIndex(fontZoomLevelIdx, false);
    }

    void UltraCanvasTextEditor::UpdateLanguageDropdown() {
        if (!languageDropdown) return;

        auto doc = GetActiveDocument();
        if (!doc) return;

        const auto& items = languageDropdown->GetItems();
        for (int i = 0; i < static_cast<int>(items.size()); i++) {
            if (items[i].value == doc->language) {
                languageDropdown->SetSelectedIndex(i, false);
                return;
            }
        }
        // Fallback to "Plain Text" (index 0)
        languageDropdown->SetSelectedIndex(0, false);
    }

    void UltraCanvasTextEditor::OnLanguageChanged(int /*index*/, const DropdownItem& item) {
        auto doc = GetActiveDocument();
        if (!doc || !doc->textArea) return;

        std::string lang = item.value;
        if (lang == doc->language) return;

        if (lang == "Plain Text") {
            doc->textArea->SetHighlightSyntax(false);
            doc->textArea->SetMarkdownHybridMode(false);
        } else if (lang == "Markdown") {
            doc->textArea->SetMarkdownHybridMode(true);
        } else {
            doc->textArea->SetMarkdownHybridMode(false);
            doc->textArea->SetHighlightSyntax(true);
            doc->textArea->SetProgrammingLanguage(lang);
        }
        doc->language = lang;
        UpdateStatusBar();
        UpdateMarkdownToolbarVisibility();
    }

    void UltraCanvasTextEditor::UpdateEncodingDropdown() {
        if (!encodingDropdown) return;

        auto doc = GetActiveDocument();
        if (!doc) return;

        int idx = FindEncodingIndex(doc->encoding);
        if (idx >= 0) {
            encodingDropdown->SetSelectedIndex(idx, false);
        }
    }

    void UltraCanvasTextEditor::OnEncodingChanged(int /*index*/, const DropdownItem& item) {
        auto doc = GetActiveDocument();
        if (!doc) return;

        std::string newEncoding = item.value;
        if (newEncoding == doc->encoding) return;

        // Case 1: Document has unmodified raw bytes — re-interpret
        if (!doc->originalRawBytes.empty()) {
            std::string utf8Text;

            // Strip BOM if present
            size_t bomLength = 0;
            DetectBOM(doc->originalRawBytes, bomLength);

            std::vector<uint8_t> contentBytes;
            if (bomLength > 0 && bomLength < doc->originalRawBytes.size()) {
                contentBytes.assign(doc->originalRawBytes.begin() + bomLength,
                                    doc->originalRawBytes.end());
            } else {
                contentBytes = doc->originalRawBytes;
            }

            if (ConvertToUtf8(contentBytes, newEncoding, utf8Text)) {
                doc->encoding = newEncoding;
                doc->textArea->SetText(utf8Text, false);
                doc->isModified = false;
                UpdateTabBadge(activeDocumentIndex);
            } else {
                // Conversion failed: revert dropdown selection
                std::cerr << "Failed to re-interpret file as " << newEncoding << std::endl;
                UpdateEncodingDropdown();
                return;
            }
        }
        // Case 2: Document has been modified or raw bytes not available
        //         Just change the save encoding (no re-interpretation)
        else {
            doc->encoding = newEncoding;
            SetDocumentModified(activeDocumentIndex, true);
        }

        UpdateStatusBar();
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
            if (encodingDropdown) {
                DropdownStyle eStyle = encodingDropdown->GetStyle();
                eStyle.normalColor = Color(40, 40, 40, 255);
                eStyle.hoverColor = Color(55, 55, 55, 255);
                eStyle.normalTextColor = Color(200, 200, 200, 255);
                eStyle.borderColor = Color(60, 60, 60, 255);
                eStyle.listBackgroundColor = Color(45, 45, 45, 255);
                eStyle.listBorderColor = Color(60, 60, 60, 255);
                eStyle.itemHoverColor = Color(65, 65, 65, 255);
                eStyle.itemSelectedColor = Color(55, 55, 55, 255);
                encodingDropdown->SetStyle(eStyle);
            }
            if (languageDropdown) {
                DropdownStyle lStyle = languageDropdown->GetStyle();
                lStyle.normalColor = Color(40, 40, 40, 255);
                lStyle.hoverColor = Color(55, 55, 55, 255);
                lStyle.normalTextColor = Color(200, 200, 200, 255);
                lStyle.borderColor = Color(60, 60, 60, 255);
                lStyle.listBackgroundColor = Color(45, 45, 45, 255);
                lStyle.listBorderColor = Color(60, 60, 60, 255);
                lStyle.itemHoverColor = Color(65, 65, 65, 255);
                lStyle.itemSelectedColor = Color(55, 55, 55, 255);
                languageDropdown->SetStyle(lStyle);
            }
            if (toolbarContainer) {
                toolbarContainer->SetBackgroundColor(Color(40, 40, 40, 255));
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
            if (encodingDropdown) {
                DropdownStyle eStyle = encodingDropdown->GetStyle();
                eStyle.normalColor = Color(240, 240, 240, 255);
                eStyle.hoverColor = Color(225, 225, 225, 255);
                eStyle.normalTextColor = Color(80, 80, 80, 255);
                eStyle.borderColor = Color(200, 200, 200, 255);
                eStyle.listBackgroundColor = Color(255, 255, 255, 255);
                eStyle.listBorderColor = Color(200, 200, 200, 255);
                eStyle.itemHoverColor = Color(230, 230, 230, 255);
                eStyle.itemSelectedColor = Color(220, 220, 220, 255);
                encodingDropdown->SetStyle(eStyle);
            }
            if (languageDropdown) {
                DropdownStyle lStyle = languageDropdown->GetStyle();
                lStyle.normalColor = Color(240, 240, 240, 255);
                lStyle.hoverColor = Color(225, 225, 225, 255);
                lStyle.normalTextColor = Color(80, 80, 80, 255);
                lStyle.borderColor = Color(200, 200, 200, 255);
                lStyle.listBackgroundColor = Color(255, 255, 255, 255);
                lStyle.listBorderColor = Color(200, 200, 200, 255);
                lStyle.itemHoverColor = Color(230, 230, 230, 255);
                lStyle.itemSelectedColor = Color(220, 220, 220, 255);
                languageDropdown->SetStyle(lStyle);
            }
            if (toolbarContainer) {
                toolbarContainer->SetBackgroundColor(Color(240, 240, 240, 255));
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
                // // Clear raw bytes on first edit (no longer useful for re-interpretation)
                // if (!documents[currentIndex]->originalRawBytes.empty()) {
                //     documents[currentIndex]->originalRawBytes.clear();
                //     documents[currentIndex]->originalRawBytes.shrink_to_fit();
                // }
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