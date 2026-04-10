// Apps/Texter/UltraCanvasTextEditor.cpp
// Complete text editor implementation with multi-file tabs and autosave
// Version: 2.0.8
// Last Modified: 2026-04-05
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
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasClipboard.h"
//#include "UltraCanvasDialogManager.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <fmt/os.h>
#include "UltraCanvasDebug.h"
#include "UltraCanvasUtilsUtf8.h"

namespace UltraCanvas {
    std::string UltraCanvasTextEditor::version = "1.0.15";
    
namespace {
    std::string GetAppDataDirectory() {
#ifdef _WIN32
        const char* localAppData = std::getenv("LOCALAPPDATA");
        if (localAppData) {
            return std::string(localAppData) + "\\TextEditor\\";
        }
        const char* appData = std::getenv("APPDATA");
        if (appData) {
            return std::string(appData) + "\\TextEditor\\";
        }
        return "C:\\TextEditor\\";
#elif __APPLE__
        const char* home = std::getenv("HOME");
        if (home) {
            return std::string(home) + "/Library/Application Support/TextEditor/";
        }
        return "/tmp/TextEditor/";
#else
        // Linux: XDG Base Directory Specification
        const char* xdgDataHome = std::getenv("XDG_DATA_HOME");
        if (xdgDataHome && xdgDataHome[0] != '\0') {
            return std::string(xdgDataHome) + "/TextEditor/";
        }
        const char* home = std::getenv("HOME");
        if (home) {
            return std::string(home) + "/.local/share/TextEditor/";
        }
        return "/tmp/TextEditor/";
#endif
    }
} // anonymous namespace

// ===== AUTOSAVE MANAGER IMPLEMENTATION =====

    std::string AutosaveManager::GetDirectory() const {
        if (!autosaveDirectory.empty()) {
            return autosaveDirectory;
        }
        return GetAppDataDirectory() + "Autosave/";
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
            debugOutput << "Failed to create autosave directory: " << dir << std::endl;
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

    bool AutosaveManager::SaveBackup(const std::string& backupPath, const std::string& content,
                                      const std::string& originalPath,
                                      const std::string& encoding,
                                      const std::string& language) {
        try {
            std::ofstream file(backupPath, std::ios::binary);
            if (!file.is_open()) {
                return false;
            }

            // Write metadata header
            file << "ULTRATEXTER_AUTOSAVE_V1\n";
            file << "TIMESTAMP=" << std::time(nullptr) << "\n";
            if (!originalPath.empty()) {
                file << "ORIGINAL_PATH=" << originalPath << "\n";
            }
            if (!encoding.empty()) {
                file << "ENCODING=" << encoding << "\n";
            }
            if (!language.empty()) {
                file << "LANGUAGE=" << language << "\n";
            }
            file << "---CONTENT---\n";
            file << content;
            file.close();

            return true;
        } catch (const std::exception& e) {
            debugOutput << "Autosave error: " << e.what() << std::endl;
            return false;
        }
    }

    bool AutosaveManager::LoadBackup(const std::string& backupPath, std::string& content,
                                      std::string& originalPath, std::string& encoding,
                                      std::string& language) {
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

            // Parse metadata
            originalPath.clear();
            encoding.clear();
            language.clear();
            while (std::getline(file, line)) {
                if (line == "---CONTENT---") {
                    break;
                }
                if (line.rfind("ORIGINAL_PATH=", 0) == 0) {
                    originalPath = line.substr(14);
                } else if (line.rfind("ENCODING=", 0) == 0) {
                    encoding = line.substr(9);
                } else if (line.rfind("LANGUAGE=", 0) == 0) {
                    language = line.substr(9);
                }
            }

            // Read content
            std::stringstream buffer;
            buffer << file.rdbuf();
            content = buffer.str();

            file.close();
            return true;
        } catch (const std::exception& e) {
            debugOutput << "Backup load error: " << e.what() << std::endl;
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

// ===== DESTRUCTOR =====
    UltraCanvasTextEditor::~UltraCanvasTextEditor() {
        CancelAsyncMatchCount();
    }

// ===== ASYNC MATCH COUNTING =====

    void UltraCanvasTextEditor::CancelAsyncMatchCount() {
        matchCountCancel.store(true);
        if (matchCountThread.joinable()) {
            matchCountThread.join();
        }
        matchCountReady.store(false);
    }

    void UltraCanvasTextEditor::StartAsyncMatchCount(
            const std::string& searchText, bool caseSensitive, int selectionPos) {
        CancelAsyncMatchCount();

        if (searchText.empty()) {
            if (searchBar) searchBar->UpdateMatchCount(0, 0);
            return;
        }

        // Show "..." while counting
        if (searchBar) searchBar->UpdateMatchCount(0, -1);

        // Snapshot the document text for the background thread
        auto doc = GetActiveDocument();
        if (!doc || !doc->textArea) return;
        bool isHexMode = doc->textArea->GetEditingMode() == TextAreaEditingMode::Hex;
        std::string textSnapshot;
        if (isHexMode) {
            auto bytes = doc->textArea->GetRawBytes();
            textSnapshot.assign(bytes.begin(), bytes.end());
        } else {
            textSnapshot = doc->textArea->GetText();
        }

        matchCountCancel.store(false);
        matchCountReady.store(false);

        matchCountThread = std::thread(
            [this, textSnapshot = std::move(textSnapshot), searchText, caseSensitive, selectionPos, isHexMode]() {
                int count = 0;
                int currentIndex = 0;
                debugOutput << "Count matches thread started" << std::endl;
                if (isHexMode) {
                    std::string haystack = textSnapshot;
                    std::string needle = searchText;
                    if (!caseSensitive) {
                        std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                        std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
                    }
                    size_t pos = 0;
                    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
                        if (matchCountCancel.load()) return;
                        count++;
                        if (static_cast<int>(pos) == selectionPos) {
                            currentIndex = count;
                        }
                        pos += needle.size();
                    }
                } else {
                    int pos = 0;
                    int searchLen = utf8_length(searchText);
                    while ((pos = utf8_find(textSnapshot, searchText, pos, caseSensitive)) >= 0) {
                        if (matchCountCancel.load()) return;
                        count++;
                        if (pos == selectionPos) {
                            currentIndex = count;
                        }
                        pos += searchLen;
                    }
                }

                if (matchCountCancel.load()) return;

                {
                    std::lock_guard<std::mutex> lock(matchCountMutex);
                    pendingMatchTotal = count;
                    pendingMatchCurrent = currentIndex;
                }
                matchCountReady.store(true);

                UCEvent ev;
                ev.targetElement = this;
                ev.type = UCEventType::Redraw;
                UltraCanvasApplication::GetInstance()->PushEvent(ev);
                debugOutput << "Count matches thread finished, matches=" << pendingMatchTotal << std::endl;
            }
        );
    }

// ===== CONSTRUCTOR =====
    UltraCanvasTextEditor::UltraCanvasTextEditor(
            const TextEditorConfig& cfg)
            : UltraCanvasWindow()
            , config(cfg)
            , isDarkTheme(cfg.darkTheme)
            , isDocumentClosing(false)
            , nextDocumentId(0)
            , activeDocumentIndex(-1)
            , hasCheckedForBackups(false)
            , menuBarHeight(24)
            , toolbarHeight(cfg.showToolbar ? 40 : 0)
            , markdownToolbarWidth(40)
            , statusBarHeight(22)
            , tabBarHeight(26)
    {
        LoadConfig();
        toolbarHeight = config.showToolbar ? 40 : 0;

        // Configure autosave
        autosaveManager.SetEnabled(config.enableAutosave);
        autosaveManager.SetInterval(config.autosaveIntervalSeconds);
        if (!config.autosaveDirectory.empty()) {
            autosaveManager.SetDirectory(config.autosaveDirectory);
        }

        configFile.LoadSearchHistory(searchHistory, replaceHistory);

        // Setup UI components in order
        if (config.showMenuBar) {
            SetupMenuBar();
        }

        SetupToolbar();
        if (!config.showToolbar && toolbarContainer) {
            toolbarContainer->SetVisible(false);
        }

        SetupTabContainer();

        if (config.showStatusBar) {
            SetupStatusBar();
        }
        SetupSearchBar();

        SetupLayout();

        // Create initial empty document
        CreateNewDocument();

        // Handle close request internally (prompt for unsaved changes)
        onWindowClosing = [this]() -> bool {
            if (HasAnyUnsavedChanges()) {
                ConfirmCloseWithUnsavedChanges([this](bool shouldClose) {
                    // Always persist autosave + session, even if the user
                    // cancelled a Save As dialog mid-chain. If the process
                    // is force-killed later, the next launch will still
                    // restore every open tab.
                    PerformAutosave(true);
                    SaveSession();
                    if (shouldClose) {
                        PerformClose();
                    }
                });
                return false;  // Don't close yet — the callback handles it
            }
            PerformAutosave(true);
            SaveSession();
            return true;  // Allow close
        };

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
                        MenuItemData::ActionWithShortcut("New", "Ctrl+N", GetResourcesDir() + "media/icons/texter/add-document.svg", [this]() {
                            OnFileNew();
                        }),
                        MenuItemData::ActionWithShortcut("New Window", "Ctrl+Shift+N", GetResourcesDir() + "media/icons/texter/add-document.svg", [this]() {
                            if (onNewWindowRequest) onNewWindowRequest();
                        }),
                        MenuItemData::ActionWithShortcut("Open...", "Ctrl+O", GetResourcesDir() + "media/icons/texter/folder-open.svg", [this]() {
                            OnFileOpen();
                        }),
                        MenuItemData::Submenu("Recent Files", GetResourcesDir() + "media/icons/texter/clock-five.svg",  {}),  // Empty — populated dynamically
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Save", "Ctrl+S", GetResourcesDir() + "media/icons/texter/save.svg", [this]() {
                            OnFileSave();
                        }),
                        MenuItemData::ActionWithShortcut("Save As...", "Ctrl+Shift+S", GetResourcesDir() + "media/icons/texter/save.svg", [this]() {
                            OnFileSaveAs();
                        }),
                        MenuItemData::Action("Save All", GetResourcesDir() + "media/icons/texter/save.svg", [this]() {
                            OnFileSaveAll();
                        }),
                        // ── ADD THESE TWO LINES ──
                        MenuItemData::ActionWithShortcut("Print...", "Ctrl+P", [this]() {
                            OnFilePrint();
                        }),                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Close Tab", "Ctrl+W", GetResourcesDir() + "media/icons/texter/close_tab.svg", [this]() {
                            OnFileClose();
                        }),
                        MenuItemData::Action("Close All", GetResourcesDir() + "media/icons/texter/close_tab.svg", [this]() {
                            OnFileCloseAll();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Quit", "Alt+F4", GetResourcesDir() + "media/icons/texter/exit.svg", [this]() {
                            OnFileQuit();
                        })
                })

                        // ===== EDIT MENU =====
                .AddSubmenu("Edit", {
                        // "↶ Undo"
                        MenuItemData::ActionWithShortcut("Undo", "Ctrl+Z", GetResourcesDir() + "media/icons/texter/undo.svg", [this]() {
                            OnEditUndo();
                        }),
                        // "↷ Redo"
                        MenuItemData::ActionWithShortcut("Redo", "Ctrl+Y", GetResourcesDir() + "media/icons/texter/redo.svg", [this]() {
                            OnEditRedo();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Cut", "Ctrl+X", GetResourcesDir() + "media/icons/texter/scissors.svg", [this]() {
                            OnEditCut();
                        }),
                        MenuItemData::ActionWithShortcut("Copy", "Ctrl+C", GetResourcesDir() + "media/icons/texter/copy.svg", [this]() {
                            OnEditCopy();
                        }),
                        MenuItemData::ActionWithShortcut("Paste", "Ctrl+V", GetResourcesDir() + "media/icons/texter/paste.svg", [this]() {
                            OnEditPaste();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Find...", "Ctrl+F", GetResourcesDir() + "media/icons/texter/search.svg", [this]() {
                            OnEditSearch();
                        }),
                        MenuItemData::ActionWithShortcut("Replace...", "Ctrl+H",  GetResourcesDir() + "media/icons/texter/replace.svg", [this]() {
                            OnEditReplace();
                        }),
                        MenuItemData::ActionWithShortcut("Go to Line...", "Ctrl+G", GetResourcesDir() + "media/icons/texter/gotoline.svg", [this]() {
                            OnEditGoToLine();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Select All", "Ctrl+A", [this]() {
                            OnEditSelectAll();
                        })
                })

                        // ===== VIEW MENU =====
                .AddSubmenu("View", {
                        MenuItemData::ActionWithShortcut("Increase Font Size", "Ctrl++", GetResourcesDir() + "media/icons/texter/zoom-in.svg", [this]() {
                            OnViewIncreaseFontSize();
                        }),
                        MenuItemData::ActionWithShortcut("Decrease Font Size", "Ctrl+-", GetResourcesDir() + "media/icons/texter/zoom-out.svg", [this]() {
                            OnViewDecreaseFontSize();
                        }),
                        MenuItemData::ActionWithShortcut("Reset Font Size", "Ctrl+0", [this]() {
                            OnViewResetFontSize();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("Toggle Theme", "Ctrl+T", GetResourcesDir() + "media/icons/texter/theme_mode.svg", [this]() {
                            OnViewToggleTheme();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::Checkbox("Line Numbers", config.showLineNumbers, [this](bool checked) {
                            OnViewToggleLineNumbers(checked);
                        }),
                        MenuItemData::Checkbox("Word Wrap", config.wordWrap, [this](bool checked) {
                            OnViewToggleWordWrap(checked);
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::Submenu("Toolbars", {
                            MenuItemData::Checkbox("Main Toolbar", config.showToolbar, [this](bool checked) {
                                OnViewToggleToolbar(checked);
                            }),
                            MenuItemData::Checkbox("Markdown Toolbar", config.showMarkdownToolbar, [this](bool checked) {
                                OnViewToggleMarkdownToolbar(checked);
                            })
                        })
                })

                        // ===== INFO MENU =====
                .AddSubmenu("Info", {
                        MenuItemData::Action("File Statistics", [this]() {
                         OnInfoFileStatistics();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::Action("About UltraTexter", [this]() {
                            OnInfoAbout();
                        })
                })

                .Build();

        // Store the index of "Recent Files" within the File menu items
        // It's at position 3 (0=New, 1=New Window, 2=Open, 3=Recent Files)
        recentFilesMenuIndex = 3;

        // Load and populate recent files
        LoadRecentFiles();
        RebuildRecentFilesSubmenu();

        MenuStyle menuStyle = MenuStyle::Default();
        menuStyle.font.fontSize = 11.0f;
        menuStyle.itemHeight     = 24;
        menuBar->SetStyle(menuStyle);

        AddChild(menuBar);
    }
  
    void UltraCanvasTextEditor::SetupToolbar() {
        int toolbarY = config.showMenuBar ? menuBarHeight : 0;

        toolbar = UltraCanvasToolbarBuilder("EditorToolbar", 200)
                .SetOrientation(ToolbarOrientation::Horizontal)
                .SetAppearance(ToolbarAppearance::Flat())
                .SetDimensions(0, 0, GetWidth(), toolbarHeight)
                .AddButton("new", "", GetResourcesDir() + "media/icons/texter/add-document.svg", [this]() { OnFileNew(); })
                .AddButton("open", "", GetResourcesDir() + "media/icons/texter/folder-open.svg", [this]() { OnFileOpen(); })
                .AddButton("save", "", GetResourcesDir() + "media/icons/texter/save.svg", [this]() { OnFileSave(); })
                .AddButton("recent", "", GetResourcesDir() + "media/icons/texter/clock-five.svg", [this]() { ShowRecentFilesPopup(); })
                .AddSeparator()
                .AddButton("copy", "", GetResourcesDir() + "media/icons/texter/copy.svg", [this]() { OnEditCopy(); })
                .AddButton("cut", "", GetResourcesDir() + "media/icons/texter/scissors.svg", [this]() { OnEditCut(); })
                .AddButton("paste", "", GetResourcesDir() + "media/icons/texter/paste.svg", [this]() { OnEditPaste(); })
                .AddSeparator()
                .AddButton("undo", "", GetResourcesDir() + "media/icons/texter/undo.svg", [this]() { OnEditUndo(); })
                .AddButton("redo", "", GetResourcesDir() + "media/icons/texter/redo.svg", [this]() { OnEditRedo(); })
                .AddSeparator()
                .AddButton("search", "", GetResourcesDir() + "media/icons/texter/search.svg", [this]() { OnEditSearch(); })
                .AddButton("replace", "", GetResourcesDir() + "media/icons/texter/replace.svg", [this]() { OnEditReplace(); })
                .AddSeparator()
                .AddButton("zoom-in", "", GetResourcesDir() + "media/icons/texter/zoom-in.svg", [this]() { OnViewIncreaseFontSize(); })
                .AddButton("zoom-out", "", GetResourcesDir() + "media/icons/texter/zoom-out.svg", [this]() { OnViewDecreaseFontSize(); })
                .Build();

        struct { const char* id; const char* tip; } toolbarTooltips[] = {
                { "new",     "New File (Ctrl+N)"          },
                { "open",    "Open File... (Ctrl+O)"       },
                { "save",    "Save (Ctrl+S)"               },
                { "recent",  "Recent Files"                },
                { "cut",     "Cut (Ctrl+X)"                },
                { "copy",    "Copy (Ctrl+C)"               },
                { "paste",   "Paste (Ctrl+V)"              },
                { "undo",    "Undo (Ctrl+Z)"               },
                { "redo",    "Redo (Ctrl+Y)"               },
                { "search",  "Find... (Ctrl+F)"            },
                { "replace", "Replace... (Ctrl+H)"         },
                { "zoom-in", "Increase Font Size (Ctrl++)" },
                { "zoom-out","Decrease Font Size (Ctrl+-)" },
        };

        for (auto& entry : toolbarTooltips) {
            auto item = toolbar->GetItem(entry.id);
            if (item) {
                auto btn = std::dynamic_pointer_cast<UltraCanvasButton>(item->GetWidget());
                if (btn) {
                    btn->SetTooltip(entry.tip);
                    btn->SetAcceptsFocus(false);
                }
            }
        }

        // Disable focus on toolbar buttons so they don't steal focus from the text area
//        for (int i = 0; i < toolbar->GetItemCount(); i++) {
//            auto item = toolbar->GetItemAt(i);
//            if (item) {
//                auto btn = std::dynamic_pointer_cast<UltraCanvasButton>(item->GetWidget());
//                if (btn) btn->SetAcceptsFocus(false);
//            }
//        }

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
        struct { const char* id; const char* tip; } mdTooltips[] = {
            { "md-bold",       "Bold (**text**)"                    },
            { "md-italic",     "Italic (*text*)"                    },
            { "md-superscript","Superscript (^text^)"               },
            { "md-subscript",  "Subscript (~text~)"                 },
            { "md-heading",    "Heading (## Heading)"               },
            { "md-ul",         "Unordered List (- item)"            },
            { "md-ol",         "Ordered List (1. item)"             },
            { "md-checklist",  "Checklist (- [ ] item)"             },
            { "md-quote",      "Blockquote (> text)"                },
            { "md-code",       "Code Block (```code```)"            },
            { "md-table",      "Insert Table"                       },
            { "md-link",       "Hyperlink ([title](url))"           },
            { "md-image",      "Image (![title](path))"             },
        };

        markdownToolbar = UltraCanvasToolbarBuilder("MarkdownToolbar", 202)
                .SetOrientation(ToolbarOrientation::Vertical)
                .SetAppearance(ToolbarAppearance::Flat())
                .SetDimensions(0, 0, markdownToolbarWidth, 400)
                .AddButton("md-bold", "", GetResourcesDir() + "media/icons/texter/md-bold.svg",
                    [this]() { InsertMarkdownSnippet("**", "**", "bold text"); })
                .AddButton("md-italic", "", GetResourcesDir() + "media/icons/texter/md-italic.svg",
                    [this]() { InsertMarkdownSnippet("*", "*", "emphasized text"); })
                .AddButton("md-superscript", "", GetResourcesDir() + "media/icons/texter/md-superscript.svg",
                    [this]() { InsertMarkdownSnippet("^", "^", "sup"); })
                .AddButton("md-subscript", "", GetResourcesDir() + "media/icons/texter/md-subscript.svg",
                    [this]() { InsertMarkdownSnippet("~", "~", "sub"); })
                .AddSeparator()
                .AddButton("md-heading", "", GetResourcesDir() + "media/icons/texter/md-heading.svg",
                    [this]() {
                        if (!headingSubToolbar) return;
                        if (headingSubToolbar->IsVisible()) {
                            headingSubToolbar->SetVisible(false);
                            GetWindow()->RemoveChild(headingSubToolbar);
                        } else {
                            if (headingSubToolbar && markdownToolbar && markdownToolbar->IsVisible()) {
                                auto item = markdownToolbar->GetItem("md-heading");
                                if (item && item->GetWidget()) {
                                    auto btn = item->GetWidget();
                                    auto pos = btn->GetPositionInWindow();
                                    int btnW = btn->GetWidth();
                                    headingSubToolbar->SetBounds(Rect2Di(pos.x + btnW, pos.y, 200, 36));
                                }
                            }
                            headingSubToolbar->SetVisible(true);
                            GetWindow()->AddChild(headingSubToolbar);
                        }
                        //UpdateChildLayout();
                    })
                .AddSeparator()
                .AddButton("md-ul", "", GetResourcesDir() + "media/icons/texter/md-list-unordered.svg",
                    [this]() { InsertMarkdownSnippet("- ", "", "list item"); })
                .AddButton("md-ol", "", GetResourcesDir() + "media/icons/texter/md-list-ordered.svg",
                    [this]() { InsertMarkdownSnippet("1. ", "", "list item"); })
                .AddButton("md-checklist", "", GetResourcesDir() + "media/icons/texter/md-list-check.svg",
                    [this]() { InsertMarkdownSnippet("- [ ] ", "", "list item"); })
                .AddSeparator()
                .AddButton("md-quote", "", GetResourcesDir() + "media/icons/texter/md-quote.svg",
                    [this]() { InsertMarkdownSnippet("> ", "", "quote"); })
                .AddButton("md-code", "", GetResourcesDir() + "media/icons/texter/md-code.svg",
                    [this]() { InsertMarkdownSnippet("```\n", "\n```", "code"); })
                .AddButton("md-table", "", GetResourcesDir() + "media/icons/texter/md-table.svg",
                    [this]() {
                        InsertMarkdownSnippet(
                            "| ", " | Column 2 |\n|----------|----------|\n|          |          |",
                            "Column 1");
                    })
                .AddSeparator()
                .AddButton("md-link", "", GetResourcesDir() + "media/icons/texter/md-link.svg",
                    [this]() { InsertMarkdownSnippet("[", "](http://example.com/)", "Link title"); })
                .AddButton("md-image", "", GetResourcesDir() + "media/icons/texter/md-image.svg",
                    [this]() { InsertMarkdownSnippet("![", "](image path)", "Image title"); })
                .Build();

        for (auto& entry : mdTooltips) {
            auto item = markdownToolbar->GetItem(entry.id);
            if (item) {
                auto btn = std::dynamic_pointer_cast<UltraCanvasButton>(item->GetWidget());
                if (btn) {
                    btn->SetTooltip(entry.tip);
                    btn->SetAcceptsFocus(false);
                }
            }
        }

        // Create heading sub-toolbar (horizontal strip shown when clicking the heading button)
        headingSubToolbar = UltraCanvasToolbarBuilder("HeadingSubToolbar", 203)
            .SetOrientation(ToolbarOrientation::Horizontal)
            .SetAppearance(ToolbarAppearance::Flat())
            .SetDimensions(0, 0, 200, 36)
            .AddButton("md-h1", "H1", "", [this]() { InsertMarkdownSnippet("# ", "", "Heading"); headingSubToolbar->SetVisible(false); })
            .AddButton("md-h2", "H2", "", [this]() { InsertMarkdownSnippet("## ", "", "Heading"); headingSubToolbar->SetVisible(false); })
            .AddButton("md-h3", "H3", "", [this]() { InsertMarkdownSnippet("### ", "", "Heading"); headingSubToolbar->SetVisible(false); })
            .AddButton("md-h4", "H4", "", [this]() { InsertMarkdownSnippet("#### ", "", "Heading"); headingSubToolbar->SetVisible(false); })
            .AddButton("md-h5", "H5", "", [this]() { InsertMarkdownSnippet("##### ", "", "Heading"); headingSubToolbar->SetVisible(false); })
            .Build();

        // Style buttons with decreasing font sizes to reflect heading hierarchy
        struct { const char* id; float fontSize; FontWeight weight; } headingStyles[] = {
            {"md-h1", 16.0f, FontWeight::Bold},
            {"md-h2", 14.0f, FontWeight::Bold},
            {"md-h3", 12.0f, FontWeight::Normal},
            {"md-h4", 11.0f, FontWeight::Normal},
            {"md-h5", 10.0f, FontWeight::Normal},
        };
        for (auto& s : headingStyles) {
            auto item = headingSubToolbar->GetItem(s.id);
            if (item) {
                auto btn = std::dynamic_pointer_cast<UltraCanvasButton>(item->GetWidget());
                if (btn) {
                    btn->SetFont("", s.fontSize, s.weight);
                    btn->SetAcceptsFocus(false);
                }
            }
        }
        headingSubToolbar->SetVisible(false);

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
        tabContainer->SetOverflowDropdownPosition(OverflowDropdownPosition::Left);
        tabContainer->SetDropdownSearchEnabled(true);
        tabContainer->SetDropdownSearchThreshold(5);
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

        // ===== V2.0.0: Enable tab reordering by drag =====
        tabContainer->allowTabReordering = true;

        // Sync documents[] vector when tabs are reordered so that
        // documents[i] always corresponds to tabContainer->tabs[i].
        tabContainer->onTabReorder = [this](int fromIndex, int toIndex) {
            if (fromIndex < 0 || fromIndex >= static_cast<int>(documents.size()) ||
                toIndex < 0 || toIndex >= static_cast<int>(documents.size())) {
                return;
            }

            // Move the document entry to match the new tab order
            auto doc = std::move(documents[fromIndex]);
            documents.erase(documents.begin() + fromIndex);
            documents.insert(documents.begin() + toIndex, std::move(doc));

            // Update activeDocumentIndex to follow the active tab
            // (TabbedContainer already updated its activeTabIndex internally
            // via ReorderTabs, so we just read it back)
            activeDocumentIndex = tabContainer->GetActiveTab();

            // Update status bar to reflect potential index change
            UpdateStatusBar();
        };

        // ===== Enable tab drag-out for multi-window support =====
        tabContainer->allowTabDragOut = true;

        tabContainer->onTabDragOut = [this](int tabIndex, int screenX, int screenY) -> bool {
            if (tabIndex < 0 || tabIndex >= static_cast<int>(documents.size())) {
                return false;
            }

            // Extract the document without save prompts
            auto doc = ExtractDocument(tabIndex);
            if (!doc) {
                return false;
            }

            // Ask the window manager to handle the document.
            // The manager will either drop it into a target window or create a new one.
            // If this editor is now empty, the manager will close this window.
            if (onTabDraggedOut) {
                onTabDraggedOut(doc, screenX, screenY);
            }

            return true;  // Tab already removed by ExtractDocument
        };

        SetupTabContextMenu();

        AddChild(tabContainer);
    }

    void UltraCanvasTextEditor::SetupTabContextMenu() {
        tabContextMenu = std::make_shared<UltraCanvasMenu>("TabContextMenu", 0, 0, 0, 200, 200);
        tabContextMenu->SetMenuType(MenuType::PopupMenu);

        tabContainer->SetTabContextMenu(tabContextMenu);

        tabContainer->onTabContextMenu = [this](int tabIndex) {
            tabContextMenu->Clear();

            // 1. Tab title — disabled display item, font reduced by 20% (9.6pt vs default 12pt)
            std::string title = (tabIndex >= 0 && tabIndex < static_cast<int>(documents.size()))
                                ? documents[tabIndex]->fileName : "Tab";
            FontStyle titleFont;
            titleFont.fontSize = 9.0f;                        // ← 20% smaller than default 12pt
            auto titleItem = MenuItemData::Action(title, titleFont, []() {});
            titleItem.enabled = false;
            tabContextMenu->AddItem(titleItem);

            // 2. Separator
            tabContextMenu->AddItem(MenuItemData::Separator());

            // 3. Open in new window
            tabContextMenu->AddItem(MenuItemData::Action("Open in New Window", [this, tabIndex]() {
                if (tabIndex >= 0 && tabIndex < static_cast<int>(documents.size())) {
                    auto doc = ExtractDocument(tabIndex);
                    if (doc && onTabDraggedOut) {
                        onTabDraggedOut(doc, 0, 0);
                    }
                }
            }));

            // 4. Separator between "Open in New Window" and "Close"  ← NEW
            tabContextMenu->AddItem(MenuItemData::Separator());

            // 5. Close
            tabContextMenu->AddItem(MenuItemData::Action("Close", [this, tabIndex]() {
                if (tabIndex >= 0 && tabIndex < static_cast<int>(documents.size())) {
                    CloseDocument(tabIndex);
                }
            }));

            // 6. Close other tabs
            tabContextMenu->AddItem(MenuItemData::Action("Close Other Tabs", [this, tabIndex]() {
                isDocumentClosing = true;
                for (int i = static_cast<int>(documents.size()) - 1; i >= 0; i--) {
                    if (i != tabIndex) {
                        if (!documents[i]->autosaveBackupPath.empty()) {
                            autosaveManager.DeleteBackup(documents[i]->autosaveBackupPath);
                        }
                        documents.erase(documents.begin() + i);
                        tabContainer->RemoveTab(i);
                    }
                }
                activeDocumentIndex = 0;
                tabContainer->SetActiveTab(0);
                isDocumentClosing = false;
                UpdateStatusBar();
            }));

            // 7. Close all tabs
            tabContextMenu->AddItem(MenuItemData::Action("Close All Tabs", [this]() {
                OnFileCloseAll();
            }));
        };
    }

    void UltraCanvasTextEditor::SetupStatusBar() {
        if (!config.showStatusBar) return;

        int yPos = GetHeight() - statusBarHeight;
        int languageDropdownWidth = 140;
        int encodingDropdownWidth = 160;
        int eolDropdownWidth = 80;
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
        languageDropdown->AddItem("Markdown", "Markdown");
        languageDropdown->AddSeparator();
 
        {
            UltraCanvasTextArea tempArea("_tmp", 0, 0, 0, 0, 0);
            auto languages = tempArea.GetSupportedLanguages();
            std::sort(languages.begin(), languages.end());
 
            const std::vector<std::pair<std::string, std::string>> assemblerMap = {
                    {"68000 Assembly", "Assembler (68000)"},
                    {"ARM Assembly",   "Assembler (ARM)"},
                    {"x86 Assembly",   "Assembler (x86)"},
                    {"Z80 Assembly",   "Assembler (Z80)"},
            };
 
            auto isAssembler = [&](const std::string& lang) -> bool {
                for (const auto& [tokenName, displayName] : assemblerMap) {
                    if (lang == tokenName) return true;
                }
                return false;
            };
 
            // All non-assembler programming languages (Markdown already added above)
            for (const auto& lang : languages) {
                if (lang == "Markdown" || isAssembler(lang)) continue;
                languageDropdown->AddItem(lang, lang);
            }
 
            // Assembler group
            languageDropdown->AddSeparator();
            for (const auto& [tokenName, displayName] : assemblerMap) {
                languageDropdown->AddItem(displayName, tokenName);
            }
        }
 
        // Hex/Binary at the very bottom in its own block —
        // binary files are a fundamentally different category from text modes
        languageDropdown->AddSeparator();
        languageDropdown->AddItem("Hex/Binary", "Hex/Binary");
        languageDropdown->SetSelectedIndex(0); // Plain Text
        languageDropdown->SetTooltip("File Type / Syntax Mode");

        DropdownStyle langStyle = languageDropdown->GetStyle();
        langStyle.fontSize = 10;
        langStyle.maxVisibleItems = -1;
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

        // Populate encoding dropdown with grouped charsets and separators
        auto encodings = GetSupportedEncodings();

        // Group boundaries (indices into GetSupportedEncodings())
        // Unicode: 0..4, Legacy: 5..12, East Asian: 13..19, Other: 20..22
        const int unicodeEnd = 5;    // first 5 entries (UTF-8, UTF-16 LE/BE, UTF-32 LE/BE)
        const int legacyEnd = 13;    // next 8 entries (ASCII through Windows-1251)
        const int eastAsianEnd = 20; // next 7 entries (Shift-JIS through EUC-KR)
        // remaining entries are "Other Notable"

        for (int i = 0; i < static_cast<int>(encodings.size()); i++) {
            // Insert separator between groups
            if (i == unicodeEnd || i == legacyEnd || i == eastAsianEnd) {
                encodingDropdown->AddSeparator();
            }
            encodingDropdown->AddItem(encodings[i].displayName, encodings[i].iconvName);
        }

        encodingDropdown->SetSelectedIndex(0); // Default: UTF-8
        encodingDropdown->SetTooltip("Character Encoding");

        DropdownStyle encStyle = encodingDropdown->GetStyle();
        encStyle.fontSize = 10;
        encStyle.maxVisibleItems = -1;
        encodingDropdown->SetStyle(encStyle);

        encodingDropdown->onSelectionChanged = [this](int index, const DropdownItem& item) {
            OnEncodingChanged(index, item);
        };

        AddChild(encodingDropdown);
        xPos += encodingDropdownWidth + gap;

        // Create EOL dropdown
        eolDropdown = std::make_shared<UltraCanvasDropdown>(
                "EOLDropdown", 304,
                xPos, yPos + 2,
                eolDropdownWidth, statusBarHeight - 4
        );

        eolDropdown->AddItem("LF", "LF");
        eolDropdown->AddItem("CRLF", "CRLF");
        eolDropdown->AddItem("CR", "CR");

        // Default to system default
        auto defaultEOL = UltraCanvasTextArea::GetSystemDefaultLineEnding();
        eolDropdown->SetSelectedIndex(static_cast<int>(defaultEOL));
        eolDropdown->SetTooltip("Line Ending Style (LF / CRLF / CR)");

        DropdownStyle eolStyle = eolDropdown->GetStyle();
        eolStyle.fontSize = 10;
        eolDropdown->SetStyle(eolStyle);

        eolDropdown->onSelectionChanged = [this](int index, const DropdownItem& item) {
            OnEOLChanged(index, item);
        };

        AddChild(eolDropdown);
        xPos += eolDropdownWidth + gap;

        // Create zoom dropdown
        zoomDropdown = std::make_shared<UltraCanvasDropdown>(
                "ZoomDropdown", 301,
                xPos, yPos + 2,
                zoomDropdownWidth, statusBarHeight - 4
        );

        for(size_t i = 0; i < config.fontZoomPercents.size(); i++) {
            auto zoomLabel = fmt::format("{}%", config.fontZoomPercents[i]);
            auto zoomValue = fmt::format("{}", config.fontZoomPercents[i]);
            zoomDropdown->AddItem(zoomLabel, zoomValue);
        }
        UpdateZoomDropdownSelection();

        DropdownStyle zoomStyle = zoomDropdown->GetStyle();
        zoomStyle.fontSize = 10;
        zoomStyle.maxVisibleItems = -1;
        zoomDropdown->SetStyle(zoomStyle);

        zoomDropdown->onSelectionChanged = [this](int index, const DropdownItem& item) {
            SetFontZoomPercent(std::stoi(item.value));
        };
        zoomDropdown->SetTooltip("Zoom Level");

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
        UltraCanvasWindow::SetBounds(b);
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

        // ===== Search bar height =====
        int searchBarH = (searchBar && searchBar->IsVisible()) ? searchBar->GetBarHeight() : 0;

        // ===== Tab container (fills remaining space minus status bar) =====
        if (tabContainer) {
            int reservedBottom = config.showStatusBar ? statusBarHeight : 0;
            int tabAreaHeight = h - yPos - reservedBottom;
            if (tabAreaHeight < 0) tabAreaHeight = 0;
            tabContainer->SetBounds(Rect2Di(mdToolbarW, yPos, w - mdToolbarW, tabAreaHeight));
            tabContainer->SetContentTopPadding(searchBarH);
        }

        // ===== Search bar (below tab strip, above content) =====
        if (searchBarH > 0) {
            int barY = yPos + tabBarHeight;
            searchBar->SetBounds(Rect2Di(mdToolbarW, barY, w - mdToolbarW, searchBarH));
        }

        // ===== Status bar =====
        if (config.showStatusBar) {
            int statusY = h - statusBarHeight;
            int langW = 140, encW = 160, eolW = 80, zoomW = 80;
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

            // EOL dropdown
            if (eolDropdown) {
                eolDropdown->SetBounds(Rect2Di(
                    xPos, statusY + 2, eolW, statusBarHeight - 4
                ));
                xPos += eolW + gap;
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

    std::string UltraCanvasTextEditor::SuggestFileNameFromFirstLine(const std::string& firstLine) const {
        // Max length of the returned stem (excluding any extension the caller appends).
        constexpr size_t kMaxLen = 40;

        auto isSpace = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };

        // Step 1: trim leading/trailing whitespace.
        size_t begin = 0;
        size_t end = firstLine.size();
        while (begin < end && isSpace(static_cast<unsigned char>(firstLine[begin]))) ++begin;
        while (end > begin && isSpace(static_cast<unsigned char>(firstLine[end - 1]))) --end;
        std::string s = firstLine.substr(begin, end - begin);
        if (s.empty()) return "";

        // Step 2: strip a single leading comment marker, then re-trim.
        // Order matters: check the longer markers before their prefixes.
        static const char* const kMarkers[] = {
            "<!--", "/*", "//", "--", "##", "#", ";;", ";", "*"
        };
        for (const char* marker : kMarkers) {
            size_t mlen = std::strlen(marker);
            if (s.size() >= mlen && s.compare(0, mlen, marker) == 0) {
                s.erase(0, mlen);
                break;
            }
        }
        // Re-trim after marker strip.
        begin = 0;
        end = s.size();
        while (begin < end && isSpace(static_cast<unsigned char>(s[begin]))) ++begin;
        while (end > begin && isSpace(static_cast<unsigned char>(s[end - 1]))) --end;
        s = s.substr(begin, end - begin);
        if (s.empty()) return "";

        // Step 3: walk characters — replace unsafe/control chars with '_',
        // keep printable ASCII and high-bit bytes (UTF-8 continuation) intact.
        // Also convert whitespace to '_' so Step 4 can collapse them.
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s) {
            if (c < 0x20 || c == 0x7F) {
                out.push_back('_');
            } else if (c == '<' || c == '>' || c == ':' || c == '"' ||
                       c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
                out.push_back('_');
            } else if (c == ' ' || c == '\t') {
                out.push_back('_');
            } else {
                out.push_back(static_cast<char>(c));
            }
        }

        // Avoid hidden-file leading dot on Unix.
        if (!out.empty() && out.front() == '.') {
            out.front() = '_';
        }

        // Step 4: collapse runs of '_' into a single '_'.
        {
            std::string collapsed;
            collapsed.reserve(out.size());
            bool prevUnderscore = false;
            for (char c : out) {
                if (c == '_') {
                    if (!prevUnderscore) collapsed.push_back('_');
                    prevUnderscore = true;
                } else {
                    collapsed.push_back(c);
                    prevUnderscore = false;
                }
            }
            out.swap(collapsed);
        }

        // Step 5: trim trailing '_', '.' and whitespace (Windows disallows trailing '.' / space).
        while (!out.empty()) {
            char c = out.back();
            if (c == '_' || c == '.' || c == ' ' || c == '\t') {
                out.pop_back();
            } else {
                break;
            }
        }
        // Also trim a leading underscore left over from the collapse step.
        if (!out.empty() && out.front() == '_') {
            out.erase(0, 1);
        }
        if (out.empty()) return "";

        // Step 6: truncate to kMaxLen, preferring a word boundary in the last 10 bytes.
        if (out.size() > kMaxLen) {
            out.resize(kMaxLen);
            size_t lastUnderscore = out.find_last_of('_');
            if (lastUnderscore != std::string::npos && lastUnderscore + 10 >= kMaxLen && lastUnderscore > 0) {
                out.resize(lastUnderscore);
            }
            // Re-trim trailing separators after truncation.
            while (!out.empty() && (out.back() == '_' || out.back() == '.')) {
                out.pop_back();
            }
        }

        return out;
    }

    std::string UltraCanvasTextEditor::GenerateUniqueDocumentName(const std::string& base) const {
        std::unordered_set<std::string> taken;
        taken.reserve(documents.size());
        for (const auto& d : documents) {
            if (d) taken.insert(d->fileName);
        }
        for (int i = 1; ; ++i) {
            std::string candidate = base + std::to_string(i);
            if (taken.find(candidate) == taken.end()) {
                return candidate;
            }
        }
    }

    int UltraCanvasTextEditor::CreateNewDocument(const std::string& fileName) {
        // Create new document tab
        auto doc = std::make_shared<DocumentTab>();
        doc->documentId = nextDocumentId++;
        doc->fileName = fileName.empty() ? GenerateUniqueDocumentName("Untitled") : fileName;
        doc->filePath = "";
        doc->language = config.defaultLanguage;
        doc->isModified = false;
        doc->isSaved = false;
        doc->isNewFile = true;
        
        // New files are always plain text — never inherit hex mode from the
        // currently active binary document. Hex/Binary is a file property,
        // not an editor preference that carries over to new documents.
        if (doc->language == "Hex/Binary") {
            doc->language = "Plain Text";
        }

        // New files use system default line ending
        doc->eolType = UltraCanvasTextArea::GetSystemDefaultLineEnding();

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
                        if (canCloseEmptyWindow && canCloseEmptyWindow()) {
                            PerformClose();
                            return;
                        }
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
                if (canCloseEmptyWindow && canCloseEmptyWindow()) {
                    if (onTabClosed) {
                        onTabClosed(index);
                    }
                    isDocumentClosing = false;
                    PerformClose();
                    return;
                }
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

    std::shared_ptr<DocumentTab> UltraCanvasTextEditor::ExtractDocument(int index) {
        if (index < 0 || index >= static_cast<int>(documents.size())) {
            return nullptr;
        }

        auto doc = documents[index];

        // Remove from documents vector
        documents.erase(documents.begin() + index);

        // Remove the tab from TabbedContainer (bypass onTabClose callback)
        isDocumentClosing = true;
        tabContainer->RemoveTab(index);
        isDocumentClosing = false;

        // Update active index without auto-creating a new document
        if (documents.empty()) {
            activeDocumentIndex = -1;
        } else if (activeDocumentIndex >= static_cast<int>(documents.size())) {
            activeDocumentIndex = static_cast<int>(documents.size()) - 1;
        }

        UpdateStatusBar();
        return doc;
    }

    int UltraCanvasTextEditor::AcceptDocument(std::shared_ptr<DocumentTab> doc) {
        if (!doc || !doc->textArea) return -1;

        // Assign new document ID to avoid conflicts
        doc->documentId = nextDocumentId++;

        // Add to documents vector
        documents.push_back(doc);
        int docIndex = static_cast<int>(documents.size()) - 1;

        // Rebind callbacks for this editor context
        SetupDocumentCallbacks(docIndex);

        // Apply this editor's current theme and view settings
        ApplyThemeToDocument(docIndex);
        doc->textArea->SetFontSize(GetFontSize());
        doc->textArea->SetShowLineNumbers(config.showLineNumbers);
        doc->textArea->SetWordWrap(config.wordWrap);

        // Add tab to TabbedContainer
        int tabIndex = tabContainer->AddTab(doc->fileName, doc->textArea);

        // Activate the new tab
        activeDocumentIndex = docIndex;
        tabContainer->SetActiveTab(tabIndex);

        UpdateTabBadge(docIndex);
        UpdateTabTitle(docIndex);
        UpdateStatusBar();
        UpdateEncodingDropdown();
        UpdateLanguageDropdown();
        UpdateMarkdownToolbarVisibility();

        return docIndex;
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
            documents[activeDocumentIndex]->textArea->SetFocus(true);
        }

        // Update status bar and dropdowns
        UpdateStatusBar();
        UpdateEncodingDropdown();
        UpdateEOLDropdown();
        UpdateLanguageDropdown();
        UpdateMarkdownToolbarVisibility();
        UpdateMenuStates();
        UpdateTitle();
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
        }
        // Refresh toolbar state when the active document's modified flag changes
        if (index == activeDocumentIndex) {
            UpdateMenuStates();
        }
    }

    std::string UltraCanvasTextEditor::FormatFullTabTooltip(int index) {
        auto& doc = documents[index];
        std::string tooltipText;
        if (doc->isNewFile) {
            tooltipText = "Never saved";   // ● red  (colour is visual only — text says it)
        } else if (doc->isModified) {
            tooltipText = "Unsaved";  // ● orange
        } else if (doc->isSaved) {
            tooltipText = "Saved";            // ● green
        }
        if (!doc->filePath.empty()) {
            if (!tooltipText.empty()) {
                tooltipText += "\n" + utf8_strreplace(doc->filePath, " ", "\u00A0");
            } else {
                tooltipText = utf8_strreplace(doc->filePath, " ", "\u00A0");
            }
        } else {
            tooltipText = doc->fileName;
        }
        return tooltipText;
    }

    void UltraCanvasTextEditor::UpdateTabTitle(int index) {
        if (index < 0 || index >= static_cast<int>(documents.size())) {
            return;
        }

        auto doc = documents[index];
        std::string title = doc->fileName;

        tabContainer->SetTabTitle(index, title);

        tabContainer->SetTabTooltip(index, FormatFullTabTooltip(index));
        //tabContainer->SetTabTooltip(index, doc->filePath);
    }

    void UltraCanvasTextEditor::UpdateTabBadge(int index) {
        if (index < 0 || index >= static_cast<int>(documents.size())) {
            return;
        }

        auto doc = documents[index];
        bool showMarker = true;
        if (doc->isNewFile) {
            // Never been saved — orange marker
            tabContainer->SetTabMarkerColor(index, Color(255, 165, 0, 255));
        } else if (doc->isModified) {
            // Modified since last save — red marker
            tabContainer->SetTabMarkerColor(index, Color(195, 30, 3, 255));
        } else if (doc->isSaved){
            // Saved and clean — green marker
            tabContainer->SetTabMarkerColor(index, Color(40, 167, 69, 255));
        } else {
            showMarker = false;
        }
        tabContainer->SetTabShowMarker(index, showMarker);
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
                debugOutput << "Failed to open file: " << filePath << std::endl;
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

            // Update filename from path
            std::filesystem::path p(filePath);
            doc->fileName = p.filename().string();
            doc->filePath = filePath;
            doc->textArea->SetDocumentFilePath(filePath);
            doc->isNewFile = false;
            doc->isModified = false;
            doc->lastSaveTime = std::chrono::steady_clock::now();

            // Get file extension
            std::string ext = p.extension().string();
            if (!ext.empty() && ext[0] == '.') {
                ext = ext.substr(1);
            }

            // Check if this is a binary (non-text) file
            if (IsBinaryFile(rawBytes, ext)) {
                // Load in hex mode directly with raw bytes
                doc->encoding = "BINARY";
                doc->hasBOM = false;
                if (rawBytes.size() <= MAX_RAW_BYTES_CACHE) {
                    doc->originalRawBytes = rawBytes;
                } else {
                    doc->originalRawBytes.clear();
                }

                doc->textArea->SetEditingMode(TextAreaEditingMode::Hex);
                doc->textArea->SetRawBytes(rawBytes);
                doc->textArea->SetHighlightSyntax(false);
                doc->language = "Hex/Binary";
            } else {
                // Text file: detect encoding and convert
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
                    debugOutput << "Encoding conversion failed for " << doc->encoding
                              << ", falling back to ISO-8859-1" << std::endl;
                    doc->encoding = "ISO-8859-1";
                    ConvertToUtf8(contentBytes, "ISO-8859-1", utf8Text);
                }

                // Set text in editor (auto-detects line ending type)
                doc->textArea->SetText(utf8Text, false);
                doc->eolType = doc->textArea->GetLineEnding();

                if (ext == "md") {
                    doc->textArea->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
                } else if (doc->textArea->SetProgrammingLanguageByExtension(ext)) {
                    doc->textArea->SetHighlightSyntax(true);
                } else {
                    doc->textArea->SetHighlightSyntax(false);
                }
                doc->language = doc->textArea->GetCurrentProgrammingLanguage();
            }

            UpdateTabTitle(docIndex);
            UpdateTabBadge(docIndex);
            UpdateTitle();
            UpdateEncodingDropdown();
            UpdateLanguageDropdown();
            UpdateMarkdownToolbarVisibility();

            // Track directory and recent files
            lastOpenedDirectory = p.parent_path().string();
            AddToRecentFiles(filePath);

            return true;

        } catch (const std::exception& e) {
            debugOutput << "Error loading file: " << e.what() << std::endl;
            return false;
        }
    }

    bool UltraCanvasTextEditor::IsBinaryFile(const std::vector<uint8_t>& rawBytes, const std::string& extension) const {
        // Known binary file extensions
        static const std::vector<std::string> binaryExtensions = {
                // Executables and libraries (Windows)
                "exe", "dll", "obj", "lib", "pdb", "cab", "msi",
                // Executables and libraries (Linux)
                "so", "o", "a", "elf", "ko", "deb", "rpm",
                // Executables and libraries (macOS)
                "dylib", "pkg",
                // Executables and libraries (cross-platform)
                "bin", "wasm",
                // Mobile packages
                "apk", "ipa",
                // Images (binary raster/raw — NOT svg which is XML text)
                "png", "jpg", "jpeg", "gif", "bmp", "ico", "tiff", "tif", "webp",
                "psd", "raw", "cr2", "nef", "arw", "dng", "heic", "heif", "jxl", "avif",
                "hdr", "exr", "tga",
                // Audio
                "mp3", "wav", "flac", "aac", "ogg", "wma", "m4a", "opus", "aiff",
                // Video
                "mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "mpg", "mpeg",
                // Archives
                "zip", "tar", "gz", "bz2", "xz", "7z", "rar", "zst", "lz4",
                // Documents (binary formats — NOT rtf/html/odt source which is text/xml)
                "pdf", "doc", "xls", "ppt",
                // Office Open XML (ZIP-based binary containers)
                "docx", "xlsx", "pptx", "odt", "ods",
                // Fonts
                "ttf", "otf", "woff", "woff2", "eot",
                // Database
                "db", "sqlite", "sqlite3",
                // Disk images
                "iso", "img", "dmg",
                // Compiled/bytecode
                "class", "pyc", "pyo",
                // Network capture
                "pcap", "pcapng"
        };

        // Check extension first
        std::string extLower = extension;
        std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
        for (const auto& bext : binaryExtensions) {
            if (extLower == bext) return true;
        }

        // Content-based detection: check for null bytes and high ratio of non-text bytes
        if (rawBytes.empty()) return false;

        size_t sampleSize = std::min(rawBytes.size(), size_t(8192));
        int nullCount = 0;
        int controlCount = 0;

        for (size_t i = 0; i < sampleSize; i++) {
            uint8_t b = rawBytes[i];
            if (b == 0x00) {
                nullCount++;
            } else if (b < 0x08 || (b > 0x0D && b < 0x20 && b != 0x1B)) {
                // Control chars excluding tab, newline, carriage return, escape
                controlCount++;
            }
        }

        // Null bytes are a strong indicator of binary content
        if (nullCount > 0) return true;

        // High ratio of control characters suggests binary
        double controlRatio = static_cast<double>(controlCount) / static_cast<double>(sampleSize);
        if (controlRatio > 0.10) return true;

        return false;
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

            // In hex mode, save raw bytes directly — no encoding conversion, no BOM
            if (doc->textArea->IsHexMode()) {
                std::vector<uint8_t> rawBytes = doc->textArea->GetRawBytes();
                std::ofstream file(filePath, std::ios::binary);
                if (!file.is_open()) {
                    debugOutput << "Failed to save file: " << filePath << std::endl;
                    return false;
                }
                file.write(reinterpret_cast<const char*>(rawBytes.data()),
                           static_cast<std::streamsize>(rawBytes.size()));
                file.close();
            } else {
                std::string utf8Text = doc->textArea->GetText();

                // Convert from UTF-8 to document encoding
                std::vector<uint8_t> outputBytes;

                if (doc->encoding == "UTF-8") {
                    outputBytes.assign(utf8Text.begin(), utf8Text.end());
                } else {
                    if (!ConvertFromUtf8(utf8Text, doc->encoding, outputBytes)) {
                        debugOutput << "Failed to convert to encoding " << doc->encoding
                                  << " while saving " << filePath
                                  << ", falling back to UTF-8" << std::endl;
                        outputBytes.assign(utf8Text.begin(), utf8Text.end());
                        doc->encoding = "UTF-8";
                        UpdateEncodingDropdown();
                    }
                }

                std::ofstream file(filePath, std::ios::binary);
                if (!file.is_open()) {
                    debugOutput << "Failed to save file: " << filePath << std::endl;
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
            }

            doc->filePath = filePath;
            doc->textArea->SetDocumentFilePath(filePath);
            bool wasNewFile = doc->isNewFile;
            doc->isNewFile = false;
            doc->isSaved = true;
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
                    doc->textArea->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
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

            // Track in recent files
//            recentFilesManager.AddFile(filePath);
//            UpdateRecentFilesMenu();
            AddToRecentFiles(filePath);

            if (onFileSaved) {
                onFileSaved(filePath, docIndex);
            }

            return true;

        } catch (const std::exception& e) {
            debugOutput << "Error saving file: " << e.what() << std::endl;
            return false;
        }
    }

// ===== AUTOSAVE =====

    void UltraCanvasTextEditor::PerformAutosave(bool force) {
        if (!force && !autosaveManager.ShouldAutosave()) {
            return;
        }

        for (size_t i = 0; i < documents.size(); i++) {
            if (documents[i]->isModified) {
                AutosaveDocument(static_cast<int>(i));
            }
        }
        autosaveManager.ResetTimer();
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
        if (autosaveManager.SaveBackup(doc->autosaveBackupPath, content,
                                       doc->filePath, doc->encoding, doc->language)) {
            debugOutput << "Autosaved: " << doc->fileName << std::endl;
        }
    }

    void UltraCanvasTextEditor::RestoreSessionAndRecoverBackups() {
        hasCheckedForBackups = true;

        // --- Phase 1: Restore session documents ---
        int savedActiveIndex = 0;
        auto sessionDocs = configFile.LoadSession(savedActiveIndex);

        int sessionOpened = 0;
        std::unordered_set<std::string> referencedBackups;

        for (const auto& sd : sessionDocs) {
            int docIndex = -1;

            if (!sd.filePath.empty()) {
                // Saved file — reopen from disk
                if (!std::filesystem::exists(sd.filePath)) {
                    debugOutput << "UltraTexter: session file no longer exists: "
                                << sd.filePath << std::endl;
                    continue;
                }
                docIndex = OpenDocumentFromPath(sd.filePath);
                if (docIndex < 0) continue;

                // If the user had unsaved edits on top of this file, overlay them
                if (sd.wasModified && !sd.backupPath.empty()
                    && std::filesystem::exists(sd.backupPath)) {
                    std::string content, origPath, enc, lang;
                    if (autosaveManager.LoadBackup(sd.backupPath, content, origPath, enc, lang)) {
                        RecoverBackupIntoDocument(docIndex, sd.backupPath, content, enc, lang);
                        referencedBackups.insert(sd.backupPath);
                    }
                }
            } else {
                // Unsaved tab — must have a backup to carry any content forward
                if (sd.backupPath.empty() || !std::filesystem::exists(sd.backupPath)) {
                    continue;
                }
                std::string content, origPath, enc, lang;
                if (!autosaveManager.LoadBackup(sd.backupPath, content, origPath, enc, lang)) {
                    continue;
                }
                // Use the stored display name (e.g. "Recovered1", "Untitled3")
                // or fall back to a fresh unique Recovered name.
                std::string name = !sd.displayName.empty()
                                   ? sd.displayName
                                   : GenerateUniqueDocumentName("Recovered");
                docIndex = CreateNewDocument(name);
                RecoverBackupIntoDocument(docIndex, sd.backupPath,
                                          content,
                                          !enc.empty() ? enc : sd.encoding,
                                          !lang.empty() ? lang : sd.language);
                referencedBackups.insert(sd.backupPath);
            }

            if (docIndex >= 0) sessionOpened++;
        }

        // Close the initial empty Untitled document if session restored anything
        if (sessionOpened > 0 && !documents.empty()
            && documents[0]->isNewFile && !documents[0]->isModified
            && documents[0]->filePath.empty()
            && documents[0]->autosaveBackupPath.empty()) {
            CloseDocument(0);
        }

        // Restore active tab from session
        if (sessionOpened > 0 && savedActiveIndex >= 0
            && savedActiveIndex < static_cast<int>(documents.size())) {
            SwitchToDocument(savedActiveIndex);
        }

        // --- Phase 2: Find orphan autosave backups (crash recovery only) ---
        std::vector<std::string> allBackups = autosaveManager.FindExistingBackups();

        // Also check legacy location (/tmp/) for migration
        std::string legacyDir = "/tmp/UltraTexter/Autosave/";
#ifdef _WIN32
        {
            char tempPath[MAX_PATH];
            GetTempPathA(MAX_PATH, tempPath);
            legacyDir = std::string(tempPath) + "UltraTexter\\Autosave\\";
        }
#endif
        if (legacyDir != autosaveManager.GetDirectory()) {
            try {
                if (std::filesystem::exists(legacyDir)) {
                    for (const auto& entry : std::filesystem::directory_iterator(legacyDir)) {
                        if (entry.is_regular_file()) {
                            std::string filename = entry.path().filename().string();
                            if (filename.find(".autosave") != std::string::npos) {
                                allBackups.push_back(entry.path().string());
                            }
                        }
                    }
                }
            } catch (...) {}
        }

        // Filter out backups that were already restored via the session —
        // only truly orphan backups (from a crash) trigger the recovery dialog.
        std::vector<std::string> backups;
        backups.reserve(allBackups.size());
        for (const auto& b : allBackups) {
            if (referencedBackups.find(b) == referencedBackups.end()) {
                backups.push_back(b);
            }
        }

        if (backups.empty()) return;

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
                        for (const auto& backupPath : backups) {
                            OfferRecoveryForBackup(backupPath);
                        }
                    } else {
                        for (const auto& backupPath : backups) {
                            autosaveManager.DeleteBackup(backupPath);
                        }
                    }
                },
                GetWindow()
        );
    }

    void UltraCanvasTextEditor::RecoverBackupIntoDocument(
            int docIndex, const std::string& backupPath,
            const std::string& content, const std::string& encoding,
            const std::string& language) {
        auto doc = documents[docIndex];
        doc->textArea->SetText(content, false);
        doc->isModified = true;
        doc->autosaveBackupPath = backupPath;

        if (!encoding.empty()) {
            doc->encoding = encoding;
        }
        if (!language.empty()) {
            doc->language = language;
            if (language == "Markdown") {
                doc->textArea->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
            } else if (language != "Plain Text") {
                doc->textArea->SetHighlightSyntax(true);
                doc->textArea->SetProgrammingLanguage(language);
            }
        }

        UpdateTabTitle(docIndex);
        UpdateTabBadge(docIndex);
        UpdateEncodingDropdown();
        UpdateLanguageDropdown();
        UpdateMarkdownToolbarVisibility();
    }

    void UltraCanvasTextEditor::OfferRecoveryForBackup(const std::string& backupPath) {
        std::string content, originalPath, encoding, language;
        if (!autosaveManager.LoadBackup(backupPath, content, originalPath, encoding, language)) {
            return;
        }

        // If this file is already open from session restore, replace its content
        if (!originalPath.empty()) {
            for (size_t i = 0; i < documents.size(); i++) {
                if (documents[i]->filePath == originalPath) {
                    RecoverBackupIntoDocument(static_cast<int>(i), backupPath,
                                              content, encoding, language);
                    return;
                }
            }
        }

        // No matching session tab — create a new document
        std::string displayName;
        if (!originalPath.empty()) {
            std::filesystem::path p(originalPath);
            displayName = p.filename().string();
        } else {
            displayName = GenerateUniqueDocumentName("Recovered");
        }

        int docIndex = CreateNewDocument(displayName);
        auto doc = documents[docIndex];

        if (!originalPath.empty()) {
            doc->filePath = originalPath;
            doc->isNewFile = false;
        }

        RecoverBackupIntoDocument(docIndex, backupPath, content, encoding, language);
    }

// ===== MARKDOWN TOOLBAR =====

    bool UltraCanvasTextEditor::IsMarkdownMode() const {
        auto doc = GetActiveDocument();
        return doc && doc->language == "Markdown";
    }

    void UltraCanvasTextEditor::UpdateMarkdownToolbarVisibility() {
        if (!markdownToolbar) return;
        bool show = IsMarkdownMode() && config.showMarkdownToolbar;
        if (markdownToolbar->IsVisible() != show) {
            markdownToolbar->SetVisible(show);
            if (!show && headingSubToolbar) {
                headingSubToolbar->SetVisible(false);
            }
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
        UltraCanvasDialogManager::ShowOpenMultipleFilesDialog(
                "Open File(s)",
                config.fileFilters,
                lastOpenedDirectory,
                [this](DialogResult result, const std::vector<std::string>& filePaths) {
                    if (result == DialogResult::OK) {
                        for (const auto& filePath : filePaths) {
                            if (!filePath.empty()) {
                                OpenDocumentFromPath(filePath);
                            }
                        }
                    }
                },
                GetWindow()
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
        UpdateMenuStates();
    }

    void UltraCanvasTextEditor::OnFileSaveAs() {
        auto doc = GetActiveDocument();
        if (!doc) return;

        std::string defaultName = doc->fileName;

        if (doc->isNewFile) {

            // Map language name → canonical first extension
            // Ordered by most common use; matches the extensions in fileFilters.
            static const std::vector<std::pair<std::string, std::string>> langToExt = {
                    { "Markdown",       "md"   },
                    { "C++",            "cpp"  },
                    { "C",              "c"    },
                    { "Python",         "py"   },
                    { "JavaScript",     "js"   },
                    { "TypeScript",     "ts"   },
                    { "Java",           "java" },
                    { "C#",             "cs"   },
                    { "Go",             "go"   },
                    { "Rust",           "rs"   },
                    { "Pascal",         "pas"  },
                    { "PHP",            "php"  },
                    { "Ruby",           "rb"   },
                    { "Swift",          "swift"},
                    { "Kotlin",         "kt"   },
                    { "HTML",           "html" },
                    { "CSS",            "css"  },
                    { "XML",            "xml"  },
                    { "JSON",           "json" },
                    { "YAML",           "yaml" },
                    { "Shell",          "sh"   },
                    { "Bash",           "sh"   },
                    { "SQL",            "sql"  },
                    { "Lua",            "lua"  },
                    { "x86 Assembly",   "asm"  },
                    { "ARM Assembly",   "asm"  },
                    { "68000 Assembly", "asm"  },
                    { "Z80 Assembly",   "asm"  },
                    { "Plain Text",     "txt"  },
            };

            std::string ext = "txt"; // fallback
            for (const auto& [lang, langExt] : langToExt) {
                if (doc->language == lang) {
                    ext = langExt;
                    break;
                }
            }

            // Prefer a fresh suggestion derived from the current first line;
            // fall back to whatever the tab is currently showing; finally to
            // the literal "untitled" when nothing useful exists yet.
            std::string stem;
            if (doc->textArea && doc->textArea->GetLineCount() > 0) {
                stem = SuggestFileNameFromFirstLine(doc->textArea->GetLine(0));
            }
            if (stem.empty()) {
                // doc->fileName is still "UntitledN" when the user hasn't
                // typed anything worth suggesting; strip the number so we
                // don't end up with "Untitled3.cpp".
                if (doc->fileName.rfind("Untitled", 0) == 0) {
                    stem = "untitled";
                } else {
                    stem = doc->fileName;
                }
            }

            defaultName = stem + "." + ext;
        }

        UltraCanvasDialogManager::ShowSaveFileDialog(
                "Save File As",
                config.fileFilters,
                lastOpenedDirectory,
                defaultName,
                [this, doc](DialogResult result, const std::string& filePath) {
                    if (result == DialogResult::OK && !filePath.empty()) {
                        SaveDocumentAs(activeDocumentIndex, filePath);
                    }
                },
                GetWindow()
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

    void UltraCanvasTextEditor::OnFilePrint() {
        auto doc = GetActiveDocument();
        if (!doc) return;

        std::string docName = doc->fileName.empty() ? "Untitled" : doc->fileName;
        std::string content = doc->textArea ? doc->textArea->GetText() : "";

        // Retrieve the native window handle for modal parenting
        UltraCanvasNativeDialogs::ShowPrintDialog(docName, content, GetWindow());
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

                if (canCloseEmptyWindow && canCloseEmptyWindow()) {
                    PerformClose();
                    return;
                }
                CreateNewDocument();
                UpdateStatusBar();
            }
        });
    }

    void UltraCanvasTextEditor::OnFileQuit() {
        Close();
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
         UpdateMenuStates();
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
        UpdateMenuStates();
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
        ShowSearchBar(SearchBarMode::Find);
    }

//    void UltraCanvasTextEditor::OnEditSearch() {
//        auto doc = GetActiveDocument();
//        if (!doc || !doc->textArea) return;
//
//        // Create find dialog if not exists
//        if (!findDialog) {
//            findDialog = CreateFindDialog();
//
//            // Wire up callbacks
//            findDialog->SetSearchHistory(searchHistory);
//
//            // In the onResult callback, save history back before destroying:
//            findDialog->onResult = [this](DialogResult res) {
//                if (findDialog) {
//                    searchHistory = findDialog->GetSearchHistory();
//                    configFile.SaveSearchHistory(searchHistory, replaceHistory);
//                }
//                findDialog.reset();
//            };
//
//            findDialog->onFindNext = [this](const std::string& searchText, bool caseSensitive, bool wholeWord) {
//                auto doc = GetActiveDocument();
//                if (doc && doc->textArea) {
//                    doc->textArea->SetTextToFind(searchText, caseSensitive);
//                    doc->textArea->FindNext();
//
//                    // Update status in the dialog
//                    int total = doc->textArea->CountMatches(searchText, caseSensitive);
//                    int current = doc->textArea->GetCurrentMatchIndex(searchText, caseSensitive);
//                    findDialog->UpdateStatus(current, total);
//                }
//            };
//
//            findDialog->onFindPrevious = [this](const std::string& searchText, bool caseSensitive, bool wholeWord) {
//                auto doc = GetActiveDocument();
//                if (doc && doc->textArea) {
//                    doc->textArea->SetTextToFind(searchText, caseSensitive);
//                    doc->textArea->FindPrevious();
//
//                    int total = doc->textArea->CountMatches(searchText, caseSensitive);
//                    int current = doc->textArea->GetCurrentMatchIndex(searchText, caseSensitive);
//                    findDialog->UpdateStatus(current, total);
//                }
//            };
//        }
//
//        // Show dialog
//        findDialog->ShowModal();
//    }

    void UltraCanvasTextEditor::OnEditReplace() {
        auto doc = GetActiveDocument();
        if (!doc || !doc->textArea) return;
        ShowSearchBar(SearchBarMode::Replace);
    }

//    void UltraCanvasTextEditor::OnEditReplace() {
//        auto doc = GetActiveDocument();
//        if (!doc || !doc->textArea) return;
//
//        // Create replace dialog if not exists
//        if (!replaceDialog) {
//            replaceDialog = CreateReplaceDialog();
//
//            // Wire up callbacks
//            replaceDialog->SetFindHistory(searchHistory);
//            replaceDialog->SetReplaceHistory(replaceHistory);
//
//            // In the onResult callback:
//            replaceDialog->onResult = [this](DialogResult res) {
//                if (replaceDialog) {
//                    searchHistory = replaceDialog->GetFindHistory();
//                    replaceHistory = replaceDialog->GetReplaceHistory();
//                    configFile.SaveSearchHistory(searchHistory, replaceHistory);
//                }
//                replaceDialog.reset();
//            };
//            replaceDialog->onFindNext = [this](const std::string& findText, bool caseSensitive, bool wholeWord) {
//                auto doc = GetActiveDocument();
//                if (doc && doc->textArea) {
//                    doc->textArea->SetTextToFind(findText, caseSensitive);
//                    doc->textArea->FindNext();
//
//                    int total = doc->textArea->CountMatches(findText, caseSensitive);
//                    int current = doc->textArea->GetCurrentMatchIndex(findText, caseSensitive);
//                    replaceDialog->UpdateStatus(current, total);
//                }
//            };
//
//            replaceDialog->onReplace = [this](const std::string& findText, const std::string& replaceText,
//                                              bool caseSensitive, bool wholeWord) {
//                auto doc = GetActiveDocument();
//                if (doc && doc->textArea) {
//                    // Find current occurrence
//                    doc->textArea->SetTextToFind(findText, caseSensitive);
//                    // Replace single occurrence
//                    doc->textArea->ReplaceText(findText, replaceText, false);
//
//                    // After replace, update counts (total may have decreased by 1)
//                    int total = doc->textArea->CountMatches(findText, caseSensitive);
//                    int current = doc->textArea->GetCurrentMatchIndex(findText, caseSensitive);
//                    replaceDialog->UpdateStatus(current, total);
//                }
//            };
//
//            replaceDialog->onReplaceAll = [this](const std::string& findText, const std::string& replaceText,
//                                                 bool caseSensitive, bool wholeWord) {
//                auto doc = GetActiveDocument();
//                if (doc && doc->textArea) {
//                    // Replace all occurrences
//                    doc->textArea->ReplaceText(findText, replaceText, true);
//
//                    // After replace all, show "0 found" (all replaced)
//                    int total = doc->textArea->CountMatches(findText, caseSensitive);
//                    if (total == 0) {
//                        replaceDialog->UpdateStatus(0, 0);
//                        // Optionally show a more descriptive message:
//                        // replaceDialog->SetStatusText("All replaced");
//                    } else {
//                        replaceDialog->UpdateStatus(0, total);
//                    }
//                }
//            };
//        }
//
//        // Show dialog
//        replaceDialog->ShowModal();
//    }

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
        IncreaseFontZoom();
    }

    void UltraCanvasTextEditor::OnViewDecreaseFontSize() {
        DecreaseFontZoom();
    }

    void UltraCanvasTextEditor::OnViewResetFontSize() {
        ResetFontZoom();
    }

    void UltraCanvasTextEditor::OnViewToggleTheme() {
        ToggleTheme();
    }

    void UltraCanvasTextEditor::OnViewToggleLineNumbers(bool checked) {
        config.showLineNumbers = checked;
        // Apply to all open document TextAreas
        for (auto& doc : documents) {
            if (doc->textArea) {
                doc->textArea->SetShowLineNumbers(config.showLineNumbers);
            }
        }
        SaveConfig();
    }

    void UltraCanvasTextEditor::OnViewToggleWordWrap(bool checked) {
        config.wordWrap = checked;
        // Apply to all open document TextAreas
        for (auto& doc : documents) {
            if (doc->textArea) {
                doc->textArea->SetWordWrap(config.wordWrap);
            }
        }
        SaveConfig();
    }

    void UltraCanvasTextEditor::OnViewToggleToolbar(bool checked) {
        config.showToolbar = checked;
        toolbarHeight = checked ? 40 : 0;
        if (toolbarContainer) {
            toolbarContainer->SetVisible(checked);
        }
        UpdateToolbarsSubmenu();
        UpdateChildLayout();
        SaveConfig();
    }

    void UltraCanvasTextEditor::OnViewToggleMarkdownToolbar(bool checked) {
        config.showMarkdownToolbar = checked;
        UpdateMarkdownToolbarVisibility();
        UpdateToolbarsSubmenu();
        SaveConfig();
    }

    void UltraCanvasTextEditor::UpdateToolbarsSubmenu() {
        if (!menuBar) return;
        // View menu is at index 2 (File=0, Edit=1, View=2)
        auto* viewItem = menuBar->GetItem(2);
        if (!viewItem) return;
        // "Toolbars" submenu is the last item in View's subItems
        auto& viewSubs = viewItem->subItems;
        for (auto& sub : viewSubs) {
            if (sub.type == MenuItemType::Submenu && sub.label == "Toolbars") {
                for (auto& tbItem : sub.subItems) {
                    if (tbItem.label == "Main Toolbar") {
                        tbItem.checked = config.showToolbar;
                    } else if (tbItem.label == "Markdown Toolbar") {
                        tbItem.checked = config.showMarkdownToolbar;
                    }
                }
                break;
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
        config.height = 526;


        aboutDialog = UltraCanvasDialogManager::CreateDialog(config);

        // Replace default layout with custom vertical layout
        auto mainLayout = CreateVBoxLayout(aboutDialog.get());
        mainLayout->SetSpacing(4);
        aboutDialog->SetPadding(20);

        // Logo image
        auto logo = std::make_shared<UltraCanvasImageElement>("AboutLogo", 0, 0, 0, 74, 74);
        logo->LoadFromFile(GetResourcesDir() + "media/Logo_Texter.png");
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
        descLabel->SetWrap(TextWrap::WrapWord);
        descLabel->SetAutoResize(true);
        descLabel->SetMargin(0, 20, 8, 20);
        mainLayout->AddUIElement(descLabel)->SetWidthMode(SizeMode::Fill);
        
        mainLayout->AddSpacing(10);

        // Copyright
        auto copyrightLabel = std::make_shared<UltraCanvasLabel>("AboutCopyright", 350, 20,
                "2026 UltraCanvas GUI API of ULTRA OS");
        copyrightLabel->SetFontSize(10);
        copyrightLabel->SetTextColor(Color(120, 120, 120));
        copyrightLabel->SetAlignment(TextAlignment::Center);
        mainLayout->AddUIElement(copyrightLabel)->SetWidthMode(SizeMode::Fill)->SetCrossAlignment(LayoutAlignment::Center)->SetMainAlignment(LayoutAlignment::Center);;

        // Clickable URL label
        auto urlLabel = std::make_shared<UltraCanvasLabel>("AboutURL", 300, 20);
        urlLabel->SetText("<span color=\"blue\">www.ultraos.eu</span>");
        urlLabel->SetTextIsMarkup(true);
        urlLabel->SetFontSize(11);
        urlLabel->SetAlignment(TextAlignment::Center);
        urlLabel->SetMouseCursor(UCMouseCursor::Hand);
        urlLabel->onClick = []() {
            OpenURL("https://www.ultraos.eu/");
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

        // Build status text
        std::stringstream status;

        if (doc->textArea->IsHexMode()) {
            int byteOffset = doc->textArea->GetHexCursorByteOffset();
            status << "Offset: 0x" << std::hex << std::uppercase << std::setfill('0')
                   << std::setw(8) << byteOffset << std::dec
                   << " (" << byteOffset << ")"
                   << " | Size: " << doc->textArea->GetRawBytes().size() << " bytes"
                   << " | HEX";
        } else {
            // Get cursor position
            int line = doc->textArea->GetCurrentLine();
            int col = doc->textArea->GetCurrentColumn();
            status << "Line: " << (line + 1) << ", Col: " << (col + 1);
        }

        // Add modified indicator
        if (doc->isModified) {
            status << " | Modified";
        }

        statusLabel->SetText(status.str());
    }

    void UltraCanvasTextEditor::UpdateZoomDropdownSelection() {
        if (!zoomDropdown) return;

        int fontZoomPercentIdx = 4;
        for(size_t i = 0; i < config.fontZoomPercents.size(); i++) {
            if (config.fontZoomPercents[i] == config.fontZoomPercent) {
                fontZoomPercentIdx = static_cast<int>(i);
            }
        }

        zoomDropdown->SetSelectedIndex(fontZoomPercentIdx, false);
    }

    void UltraCanvasTextEditor::UpdateLanguageDropdown() {
        if (!languageDropdown) return;

        auto doc = GetActiveDocument();
        if (!doc) return;

        // If in hex mode, select "Hex/Binary"
        std::string langToMatch = doc->language;
        if (doc->textArea && doc->textArea->IsHexMode()) {
            langToMatch = "Hex/Binary";
        }

        const auto& items = languageDropdown->GetItems();
        for (int i = 0; i < static_cast<int>(items.size()); i++) {
            if (items[i].value == langToMatch) {
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
 
        if (lang == "Hex/Binary") {
            doc->textArea->SetEditingMode(TextAreaEditingMode::Hex);
        } else if (lang == "Markdown") {
            doc->textArea->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        } else if (lang == "Plain Text") {
            doc->textArea->SetEditingMode(TextAreaEditingMode::PlainText);
            doc->textArea->SetHighlightSyntax(false);
        } else {
            doc->textArea->SetEditingMode(TextAreaEditingMode::PlainText);
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
                debugOutput << "Failed to re-interpret file as " << newEncoding << std::endl;
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

    void UltraCanvasTextEditor::UpdateEOLDropdown() {
        if (!eolDropdown) return;

        auto doc = GetActiveDocument();
        if (!doc) return;

        eolDropdown->SetSelectedIndex(static_cast<int>(doc->eolType), false);
    }

    void UltraCanvasTextEditor::OnEOLChanged(int index, const DropdownItem& item) {
        auto doc = GetActiveDocument();
        if (!doc || !doc->textArea) return;

        LineEndingType newType;
        if (item.value == "CRLF") newType = LineEndingType::CRLF;
        else if (item.value == "CR") newType = LineEndingType::CR;
        else newType = LineEndingType::LF;

        if (newType == doc->eolType) return;

        doc->eolType = newType;
        doc->textArea->SetLineEnding(newType);
        SetDocumentModified(activeDocumentIndex, true);
        UpdateStatusBar();
    }

    void UltraCanvasTextEditor::UpdateMenuStates() {
        if (!toolbar) return;
 
        // ── Save: disabled when document has no unsaved changes ──
        bool canSave = HasUnsavedChanges();
        if (auto item = toolbar->GetItem("save")) {
            item->SetEnabled(canSave);
        }
 
        // ── Undo / Redo ──
        bool canUndo = CanUndo();
        bool canRedo = CanRedo();
        if (auto item = toolbar->GetItem("undo")) {
            item->SetEnabled(canUndo);
        }
        if (auto item = toolbar->GetItem("redo")) {
            item->SetEnabled(canRedo);
        }
 
        // ── Paste: disabled when clipboard has no text ──
        std::string clipboardText;
        bool canPaste = GetClipboardText(clipboardText) && !clipboardText.empty();
        if (auto item = toolbar->GetItem("paste")) {
            item->SetEnabled(canPaste);
        }
    }

    void UltraCanvasTextEditor::UpdateTitle() {
        std::string title = config.title;
        std::string filePath = GetActiveFilePath();
        if (!filePath.empty()) {
            title += " - " + filePath;
        }
        if (HasUnsavedChanges()) {
            title += " *";
        }
        SetWindowTitle(title);
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
            doc->textArea->ApplyLightTheme();
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
            auto applyDarkDropdownStyle = [](std::shared_ptr<UltraCanvasDropdown>& dd) {
                if (!dd) return;
                DropdownStyle s = dd->GetStyle();
                s.normalColor = Color(40, 40, 40, 255);
                s.hoverColor = Color(55, 55, 55, 255);
                s.pressedColor = Color(50, 50, 50, 255);
                s.normalTextColor = Color(200, 200, 200, 255);
                s.borderColor = Color(60, 60, 60, 255);
                s.listBackgroundColor = Color(45, 45, 45, 255);
                s.listBorderColor = Color(60, 60, 60, 255);
                s.itemHoverColor = Color(65, 65, 65, 255);
                s.itemSelectedColor = Color(55, 55, 55, 255);
                dd->SetStyle(s);
            };
            applyDarkDropdownStyle(zoomDropdown);
            applyDarkDropdownStyle(eolDropdown);
            applyDarkDropdownStyle(encodingDropdown);
            applyDarkDropdownStyle(languageDropdown);
            if (toolbarContainer) {
                toolbarContainer->SetBackgroundColor(Color(40, 40, 40, 255));
            }
            if (tabContainer) {
                tabContainer->tabBarColor        = Color(40, 40, 40, 255);
                tabContainer->activeTabColor     = Color(60, 60, 60, 255);
                tabContainer->inactiveTabColor   = Color(45, 45, 45, 255);
                tabContainer->activeTabTextColor   = Colors::White;
                tabContainer->inactiveTabTextColor = Color(160, 160, 160, 255); // readable mid-grey
                tabContainer->newTabButtonColor  = Color(45, 45, 45, 255);            }
        } else {
            SetBackgroundColor(Color(240, 240, 240, 255));
            if (statusLabel) {
                statusLabel->SetBackgroundColor(Color(240, 240, 240, 255));
                statusLabel->SetTextColor(Color(80, 80, 80, 255));
            }
            auto applyLightDropdownStyle = [](std::shared_ptr<UltraCanvasDropdown>& dd) {
                if (!dd) return;
                DropdownStyle s = dd->GetStyle();
                s.normalColor = Color(240, 240, 240, 255);
                s.hoverColor = Color(225, 225, 225, 255);
                s.pressedColor = Color(225, 235, 255, 255);
                s.normalTextColor = Color(80, 80, 80, 255);
                s.borderColor = Color(200, 200, 200, 255);
                s.listBackgroundColor = Color(255, 255, 255, 255);
                s.listBorderColor = Color(200, 200, 200, 255);
                s.itemHoverColor = Color(230, 230, 230, 255);
                s.itemSelectedColor = Color(220, 220, 220, 255);
                dd->SetStyle(s);
            };
            applyLightDropdownStyle(zoomDropdown);
            applyLightDropdownStyle(eolDropdown);
            applyLightDropdownStyle(encodingDropdown);
            applyLightDropdownStyle(languageDropdown);
            if (toolbarContainer) {
                toolbarContainer->SetBackgroundColor(Color(240, 240, 240, 255));
            }
            if (tabContainer) {
                tabContainer->tabBarColor = Color(240, 240, 240, 255);
                tabContainer->activeTabColor = Color(255, 255, 255, 255);
                tabContainer->inactiveTabColor = Color(220, 220, 220, 255);
                tabContainer->activeTabTextColor = Colors::Black;
                tabContainer->newTabButtonColor = Color(240, 240, 240);
            }
        }
        if (searchBar) {
            if (isDarkTheme) searchBar->ApplyDarkTheme();
            else             searchBar->ApplyLightTheme();
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

                // For brand-new unsaved documents, keep the tab title in sync with
                // the first line of content so the user can see a meaningful name
                // before they ever hit Save As. Intentionally sticky: if the first
                // line later becomes empty we keep the last suggestion to avoid
                // jitter.
                auto d = documents[currentIndex];
                if (d->isNewFile && d->filePath.empty() && d->textArea &&
                    d->textArea->GetLineCount() > 0) {
                    std::string suggestion =
                        SuggestFileNameFromFirstLine(d->textArea->GetLine(0));
                    if (!suggestion.empty() && suggestion != d->fileName) {
                        d->fileName = suggestion;
                        UpdateTabTitle(currentIndex);
                        if (currentIndex == activeDocumentIndex) {
                            UpdateTitle();
                        }
                    }
                }
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

        // Markdown link click — open URL in browser
        doc->textArea->onMarkdownLinkClick = [](const std::string& url) {
#if defined(_WIN32)
            std::string command = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
            std::string command = "open \"" + url + "\"";
#else
            std::string command = "xdg-open \"" + url + "\" &";
#endif
            system(command.c_str());
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
                                    GetWindow()
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
                GetWindow()
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
                        // First, save all documents that already have a file path
                        bool allSaved = true;
                        std::vector<int> unsavedNewDocs;
                        for (int idx : modifiedDocs) {
                            if (!documents[idx]->filePath.empty()) {
                                if (!SaveDocument(idx)) {
                                    allSaved = false;
                                }
                            } else {
                                unsavedNewDocs.push_back(idx);
                            }
                        }

                        if (unsavedNewDocs.empty()) {
                            if (onComplete) {
                                onComplete(allSaved);
                            }
                            return;
                        }

                        // Chain Save As dialogs for new documents one at a time
                        auto remaining = std::make_shared<std::vector<int>>(unsavedNewDocs);
                        auto savedFlag = std::make_shared<bool>(allSaved);
                        std::function<void()> saveNext;
                        saveNext = [this, remaining, savedFlag, onComplete, saveNext]() {
                            if (remaining->empty()) {
                                if (onComplete) {
                                    onComplete(*savedFlag);
                                }
                                return;
                            }
                            int idx = remaining->front();
                            remaining->erase(remaining->begin());
                            auto doc = documents[idx];
                            UltraCanvasDialogManager::ShowSaveFileDialog(
                                    "Save File",
                                    config.fileFilters,
                                    "",
                                    doc->fileName,
                                    [this, idx, remaining, savedFlag, onComplete, saveNext](DialogResult saveResult, const std::string& filePath) {
                                        if (saveResult == DialogResult::OK && !filePath.empty()) {
                                            if (!SaveDocumentAs(idx, filePath)) {
                                                *savedFlag = false;
                                            }
                                        }
                                        // Continue to next unsaved document (skip if user cancelled this one)
                                        saveNext();
                                    },
                                    GetWindow()
                            );
                        };
                        saveNext();
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
                GetWindow()
        );
    }

// ===== PUBLIC API =====

    void UltraCanvasTextEditor::Render(IRenderContext* ctx) {
        // Poll for async match count results
        //debugOutput << "UltraCanvasTextEditor::Render" << std::endl;
        if (matchCountReady.load()) {
            debugOutput << "UltraCanvasTextEditor::Render Update Matches" << std::endl;
            matchCountReady.store(false);
            int total, current;
            {
                std::lock_guard<std::mutex> lock(matchCountMutex);
                total = pendingMatchTotal;
                current = pendingMatchCurrent;
            }
            if (searchBar && searchBar->IsVisible()) {
                searchBar->UpdateMatchCount(current, total);
            }
        }

        if (autosaveManager.IsEnabled() && autosaveManager.ShouldAutosave()) {
            PerformAutosave();
        }

        UltraCanvasWindow::Render(ctx);

        if (isDragOverActive) {
            RenderDropOverlay(ctx);
        }
    }

    bool UltraCanvasTextEditor::OnEvent(const UCEvent& event) {
        switch(event.type) {
            case UCEventType::KeyDown:
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
                if (event.virtualKey == UCKeys::Escape) {
                    if (searchBar && searchBar->IsVisible()) {
                        HideSearchBar();
                        return true;
                    }
                }
                if (event.ctrl && (event.virtualKey == UCKeys::Plus || event.virtualKey == UCKeys::NumPadPlus)) {
                    OnViewIncreaseFontSize();
                    return true;
                }
                if (event.ctrl && (event.virtualKey == UCKeys::Minus || event.virtualKey == UCKeys::NumPadMinus)) {
                    OnViewDecreaseFontSize();
                    return true;
                }
                break;
            case UCEventType::DragEnter:
                HandleDragEnter(event);
                return true;

            case UCEventType::DragOver:
                HandleDragOver(event);
                return true;

            case UCEventType::DragLeave:
                HandleDragLeave(event);
                return true;

            case UCEventType::Drop:
                HandleFileDrop(event);
                return true;
            default:
                break;
        }

        return UltraCanvasWindow::OnEvent(event);
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
        SaveConfig();
        if (onThemeChanged) {
            onThemeChanged(isDarkTheme);
        }
    }

    void UltraCanvasTextEditor::SetDefaultFontSize(float size) {
        config.defaultFontSize = std::max(4.0f, std::min(72.0f, size));
        SetFontZoomPercent(config.fontZoomPercent);
    }

    void UltraCanvasTextEditor::SetFontZoomPercent(int percent) {
        config.fontZoomPercent = percent;
        float fontSize = config.defaultFontSize * percent / 100.0;
        fontSize = std::max(4.0f, std::min(72.0f, fontSize));

        // Apply to all open document TextAreas
        for (auto& doc : documents) {
            if (doc->textArea) {
                doc->textArea->SetFontSize(fontSize);
            }
        }

        UpdateZoomDropdownSelection();
        UpdateStatusBar();
        SaveConfig();
    }

    void UltraCanvasTextEditor::IncreaseFontZoom() {
        for(int i = 0; i < config.fontZoomPercents.size() - 2; i++) {
            if (config.fontZoomPercents[i] == config.fontZoomPercent) {
                SetFontZoomPercent(config.fontZoomPercents[i+1]);
                break;
            }
        }
    }

    void UltraCanvasTextEditor::DecreaseFontZoom() {
        for(int i = config.fontZoomPercents.size(); i > 1; i--) {
            if (config.fontZoomPercents[i] == config.fontZoomPercent) {
                SetFontZoomPercent(config.fontZoomPercents[i-1]);
                break;
            }
        }
    }

    void UltraCanvasTextEditor::ResetFontZoom() {
        SetFontZoomPercent(100);
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


    void UltraCanvasTextEditor::SetupSearchBar() {
        searchBar = std::make_shared<UltraCanvasSearchBar>(
                "SearchBar", 600,
                0, 0, GetWidth()
        );
        searchBar->Initialize();

        // ── Find Next ──
        searchBar->onFindNext = [this](const std::string& text, bool cs, bool ww) {
            auto doc = GetActiveDocument();
            if (!doc || !doc->textArea) return;
            doc->textArea->SetTextToFind(text, cs);
            doc->textArea->HighlightMatches(text);
            doc->textArea->FindNext();
            bool hex = doc->textArea->GetEditingMode() == TextAreaEditingMode::Hex;
            int selPos = hex ? doc->textArea->GetHexCursorByteOffset() : doc->textArea->GetSelectionMinGrapheme();
            StartAsyncMatchCount(text, cs, selPos);
        };

        // ── Find First ──
        searchBar->onFindFirst = [this](const std::string& text, bool cs, bool ww) {
            auto doc = GetActiveDocument();
            if (!doc || !doc->textArea) return;
            doc->textArea->SetTextToFind(text, cs);
            doc->textArea->HighlightMatches(text);
            doc->textArea->FindFirst();
            bool hex = doc->textArea->GetEditingMode() == TextAreaEditingMode::Hex;
            int selPos = hex ? doc->textArea->GetHexCursorByteOffset() : doc->textArea->GetSelectionMinGrapheme();
            StartAsyncMatchCount(text, cs, selPos);
        };

        // ── Find Previous ──
        searchBar->onFindPrevious = [this](const std::string& text, bool cs, bool ww) {
            auto doc = GetActiveDocument();
            if (!doc || !doc->textArea) return;
            doc->textArea->SetTextToFind(text, cs);
            doc->textArea->HighlightMatches(text);
            doc->textArea->FindPrevious();
            bool hex = doc->textArea->GetEditingMode() == TextAreaEditingMode::Hex;
            int selPos = hex ? doc->textArea->GetHexCursorByteOffset() : doc->textArea->GetSelectionMinGrapheme();
            StartAsyncMatchCount(text, cs, selPos);
        };

        // ── Replace ──
        searchBar->onReplace = [this](const std::string& find, const std::string& replace, bool cs, bool ww) {
            auto doc = GetActiveDocument();
            if (!doc || !doc->textArea) return;
            doc->textArea->SetTextToFind(find, cs);
            doc->textArea->ReplaceText(find, replace, false);
            doc->textArea->HighlightMatches(find);
            bool hex = doc->textArea->GetEditingMode() == TextAreaEditingMode::Hex;
            int selPos = hex ? doc->textArea->GetHexCursorByteOffset() : doc->textArea->GetSelectionMinGrapheme();
            StartAsyncMatchCount(find, cs, selPos);
        };

        // ── Replace All ──
        searchBar->onReplaceAll = [this](const std::string& find, const std::string& replace, bool cs, bool ww) {
            auto doc = GetActiveDocument();
            if (!doc || !doc->textArea) return;
            doc->textArea->SetTextToFind(find, cs);
            doc->textArea->ReplaceText(find, replace, true);
            doc->textArea->HighlightMatches(find);
            StartAsyncMatchCount(find, cs, -1);
        };

        // ── Search text changed (handles clearing) ──
        searchBar->onSearchTextChanged = [this](const std::string& text) {
            if (text.empty()) {
                CancelAsyncMatchCount();
                auto doc = GetActiveDocument();
                if (doc && doc->textArea) {
                    doc->textArea->ClearHighlights();
                }
                searchBar->UpdateMatchCount(0, 0);
            }
        };

        // ── Persist history on every change ──
        searchBar->onHistoryChanged = [this]() {
            searchHistory  = searchBar->GetSearchHistory();
            replaceHistory = searchBar->GetReplaceHistory();
            configFile.SaveSearchHistory(searchHistory, replaceHistory);
        };

        // ── Save histories on close ──
        searchBar->onClose = [this]() {
            CancelAsyncMatchCount();
            searchHistory  = searchBar->GetSearchHistory();
            replaceHistory = searchBar->GetReplaceHistory();
            configFile.SaveSearchHistory(searchHistory, replaceHistory);
            HideSearchBar();
        };

        AddChild(searchBar);
    }


// ── CHANGE 3: ShowSearchBar() / HideSearchBar() ──────────────

    void UltraCanvasTextEditor::ShowSearchBar(SearchBarMode mode) {
        if (!searchBar) return;

        // Restore histories
        searchBar->SetSearchHistory(searchHistory);
        searchBar->SetReplaceHistory(replaceHistory);

        searchBar->SetMode(mode);
        searchBar->SetVisible(true);

        // Apply current theme
        if (isDarkTheme) searchBar->ApplyDarkTheme();
        else             searchBar->ApplyLightTheme();

        // Pre-fill with selected text if any
        auto doc = GetActiveDocument();
        if (doc && doc->textArea && doc->textArea->HasSelection()) {
            std::string sel = doc->textArea->GetSelectedText();
            // Only pre-fill if single-line selection
            if (sel.find('\n') == std::string::npos && !sel.empty()) {
                searchBar->SetSearchText(sel);
            }
        }

        searchBar->FocusSearchInput();
        UpdateChildLayout();
        RequestRedraw();
    }

    void UltraCanvasTextEditor::HideSearchBar() {
        if (!searchBar) return;
        CancelAsyncMatchCount();
        searchBar->SetVisible(false);

        // Return focus to active text area
        auto doc = GetActiveDocument();
        if (doc && doc->textArea) {
            doc->textArea->SetFocus(true);
            // Clear search highlights
            doc->textArea->ClearHighlights();
        }

        UpdateChildLayout();
        RequestRedraw();
    }

// ===== FACTORY FUNCTIONS =====

    static bool InitTextEditorWindow(std::shared_ptr<UltraCanvasTextEditor>& editor, const TextEditorConfig& cfg) {
        WindowConfig windowConfig;
        windowConfig.title = cfg.title;
        windowConfig.width = cfg.width;
        windowConfig.height = cfg.height;
        windowConfig.backgroundColor = cfg.darkTheme ? Color(30, 30, 30) : Color(240, 240, 240);
        windowConfig.deleteOnClose = true;
        if (!editor->Create(windowConfig)) {
            return false;
        }
        editor->SetBackgroundColor(cfg.darkTheme ? Color(30, 30, 30, 255) : Color(240, 240, 240, 255));
        editor->Show();
        return true;
    }

    std::shared_ptr<UltraCanvasTextEditor> CreateTextEditor(
            const TextEditorConfig& config)
    {
        auto editor = std::make_shared<UltraCanvasTextEditor>(config);
        if (!InitTextEditorWindow(editor, config)) {
            return nullptr;
        }
        return editor;
    }

    std::shared_ptr<UltraCanvasTextEditor> CreateDarkTextEditor()
    {
        TextEditorConfig config;
        config.darkTheme = true;
        auto editor = std::make_shared<UltraCanvasTextEditor>(config);
        if (!InitTextEditorWindow(editor, config)) {
            return nullptr;
        }
        return editor;
    }

    void UltraCanvasTextEditor::HandleDragEnter(const UCEvent& event) {
        isDragOverActive = true;
        RequestRedraw();
    }

    void UltraCanvasTextEditor::HandleDragOver(const UCEvent& event) {
        dragOverX = event.x;
        dragOverY = event.y;
        // Redraw to update the overlay position (drop indicator follows cursor)
        RequestRedraw();
    }

    void UltraCanvasTextEditor::HandleDragLeave(const UCEvent& event) {
        isDragOverActive = false;
        RequestRedraw();
    }

    void UltraCanvasTextEditor::HandleFileDrop(const UCEvent& event) {
        isDragOverActive = false;

        const auto& files = event.droppedFiles;
        if (files.empty()) {
            // Fallback: try parsing dragData as newline-separated paths
            if (!event.dragData.empty()) {
                std::istringstream stream(event.dragData);
                std::string line;
                while (std::getline(stream, line)) {
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (!line.empty()) {
                        OpenFile(line);
                    }
                }
            }
        } else {
            // Open each dropped file in a new tab
            for (const auto& filePath : files) {
                debugOutput << "Opening dropped file: " << filePath << std::endl;
                OpenFile(filePath);
            }
        }

        // Notify via callback
        if (onFileDrop) {
            onFileDrop(files.empty() ? std::vector<std::string>{event.dragData} : files);
        }

        RequestRedraw();
    }

    void UltraCanvasTextEditor::RenderDropOverlay(IRenderContext* ctx) {
        // Semi-transparent overlay covering the entire editor        
        int w = GetWidth();
        int h = GetHeight();
        ctx->PushState();
        // Dark translucent background
        ctx->SetFillPaint(Color(0, 0, 0, 100));
        ctx->FillRectangle(0, 0, w, h);

        // Central drop zone indicator
        int zoneMargin = 40;
        int zoneX = zoneMargin;
        int zoneY = zoneMargin;
        int zoneW = w - zoneMargin * 2;
        int zoneH = h - zoneMargin * 2;

        // Dashed border effect — draw corner brackets instead of full border
        // This gives a clean, modern "drop here" visual
        Color accentColor = isDarkTheme ? Color(100, 180, 255, 200) : Color(0, 120, 215, 200);
        ctx->SetStrokePaint(accentColor);
        ctx->SetStrokeWidth(3.0f);

        int bracketLen = 40; // Length of each corner bracket arm

        // Top-left corner
        ctx->DrawLine(zoneX, zoneY, zoneX + bracketLen, zoneY);
        ctx->DrawLine(zoneX, zoneY, zoneX, zoneY + bracketLen);

        // Top-right corner
        ctx->DrawLine(zoneX + zoneW, zoneY, zoneX + zoneW - bracketLen, zoneY);
        ctx->DrawLine(zoneX + zoneW, zoneY, zoneX + zoneW, zoneY + bracketLen);

        // Bottom-left corner
        ctx->DrawLine(zoneX, zoneY + zoneH, zoneX + bracketLen, zoneY + zoneH);
        ctx->DrawLine(zoneX, zoneY + zoneH, zoneX, zoneY + zoneH - bracketLen);

        // Bottom-right corner
        ctx->DrawLine(zoneX + zoneW, zoneY + zoneH, zoneX + zoneW - bracketLen, zoneY + zoneH);
        ctx->DrawLine(zoneX + zoneW, zoneY + zoneH, zoneX + zoneW, zoneY + zoneH - bracketLen);

        // Central icon — document/file icon using simple drawing
        int iconCenterX = w / 2;
        int iconCenterY = h / 2 - 20;
        int iconW = 40;
        int iconH = 50;

        // Document body
        ctx->SetFillPaint(accentColor);
        ctx->FillRectangle(iconCenterX - iconW / 2, iconCenterY - iconH / 2,
                        iconW, iconH);

        // Document fold corner (top-right triangle overlay)
        int foldSize = 12;
        Color bgColor = isDarkTheme ? Color(0, 0, 0, 100) : Color(0, 0, 0, 100);
        ctx->SetFillPaint(bgColor);
        // Approximate triangle with a small rectangle overlay at top-right
        ctx->FillRectangle(iconCenterX + iconW / 2 - foldSize,
                        iconCenterY - iconH / 2,
                        foldSize, foldSize);

        // Lines on document (simulate text)
        Color lineColor = isDarkTheme ? Color(30, 30, 30, 180) : Color(255, 255, 255, 180);
        ctx->SetFillPaint(lineColor);
        int lineY = iconCenterY - iconH / 2 + 18;
        for (int i = 0; i < 4; i++) {
            int lineW = (i == 3) ? iconW - 20 : iconW - 12;
            ctx->FillRectangle(iconCenterX - iconW / 2 + 6, lineY, lineW, 2);
            lineY += 8;
        }

        // "Drop files here" text
        Color textColor = isDarkTheme ? Color(200, 220, 255, 230) : Color(0, 80, 180, 230);
        ctx->SetTextPaint(textColor);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetFontSize(18.0f);

        std::string dropText = "Drop files to open in new tabs";
        int textWidth = ctx->GetTextLineWidth(dropText);
        ctx->DrawText(dropText, iconCenterX - textWidth / 2, iconCenterY + iconH / 2 + 16);

        // restore state
        ctx->PopState();
    }

    //  Recent Files Management Methods
    void UltraCanvasTextEditor::AddToRecentFiles(const std::string& filePath) {
        if (filePath.empty()) return;

        // Remove if already present (will re-add at front)
        auto it = std::find(recentFiles.begin(), recentFiles.end(), filePath);
        if (it != recentFiles.end()) {
            recentFiles.erase(it);
        }

        // Insert at front (newest first)
        recentFiles.insert(recentFiles.begin(), filePath);

        // Trim to configured maximum
        int maxFiles = config.maxRecentFiles;
        if (static_cast<int>(recentFiles.size()) > maxFiles) {
            recentFiles.resize(maxFiles);
        }

        // Persist and update menu
        SaveRecentFiles();
        RebuildRecentFilesSubmenu();
    }

    void UltraCanvasTextEditor::RemoveFromRecentFiles(const std::string& filePath) {
        auto it = std::find(recentFiles.begin(), recentFiles.end(), filePath);
        if (it != recentFiles.end()) {
            recentFiles.erase(it);
            SaveRecentFiles();
            RebuildRecentFilesSubmenu();
        }
    }

    void UltraCanvasTextEditor::RebuildRecentFilesSubmenu() {
        if (!menuBar || recentFilesMenuIndex < 0) return;

        // Get the File menu (first submenu in the menubar)
        // The menubar items[0] is "File" which has subItems
        auto* fileMenuItem = menuBar->GetItem(0);
        if (!fileMenuItem || recentFilesMenuIndex >= static_cast<int>(fileMenuItem->subItems.size())) {
            return;
        }

        // Build the submenu items from the recent files list
        std::vector<MenuItemData> recentItems;

        if (recentFiles.empty()) {
            // Show a disabled "(No recent files)" entry
            MenuItemData emptyItem;
            emptyItem.type = MenuItemType::Action;
            emptyItem.label = "(No recent files)";
            emptyItem.enabled = false;
            recentItems.push_back(emptyItem);
        } else {
            // Add each recent file as an action item
            int displayCount = std::min(static_cast<int>(recentFiles.size()),
                                        config.maxRecentFiles);

            for (int i = 0; i < displayCount; i++) {
                const std::string& fullPath = recentFiles[i];

                // Show just the filename for the label, with full path as tooltip
                std::filesystem::path p(fullPath);
                std::string displayName = p.filename().string();

                // Add numbering for quick access: "1. filename.txt"
                std::string label = std::to_string(i + 1) + ". " + displayName;

                // Capture by value for the lambda
                std::string pathCopy = fullPath;
                recentItems.push_back(
                        MenuItemData::Action(label, [this, pathCopy]() {
                            // Check if file still exists before opening
                            if (std::filesystem::exists(pathCopy)) {
                                OpenDocumentFromPath(pathCopy);
                            } else {
                                // File no longer exists — remove from list and notify
                                RemoveFromRecentFiles(pathCopy);
                                debugOutput << "Recent file no longer exists: " << pathCopy << std::endl;
                            }
                        })
                );
            }

            // Add separator + "Clear Recent Files" at the bottom
            recentItems.push_back(MenuItemData::Separator());
            recentItems.push_back(
                    MenuItemData::Action("Clear Recent Files", [this]() {
                        recentFiles.clear();
                        SaveRecentFiles();
                        RebuildRecentFilesSubmenu();
                    })
            );
        }

        // Update the submenu in-place
        fileMenuItem->subItems[recentFilesMenuIndex].subItems = recentItems;
    }

    void UltraCanvasTextEditor::ShowRecentFilesPopup() {
        auto* win = GetWindow();
        if (!win) return;

        // Lazy-create the popup menu (allocated once, reused)
        if (!recentFilesPopupMenu) {
            recentFilesPopupMenu = std::make_shared<UltraCanvasMenu>(
                "RecentFilesPopup", 0, 0, 0, 300, 100);
            recentFilesPopupMenu->SetMenuType(MenuType::PopupMenu);
        }

        // Clear and rebuild with current recent files
        recentFilesPopupMenu->Clear();

        if (recentFiles.empty()) {
            MenuItemData emptyItem;
            emptyItem.type = MenuItemType::Action;
            emptyItem.label = "(No recent files)";
            emptyItem.enabled = false;
            recentFilesPopupMenu->AddItem(emptyItem);
        } else {
            int displayCount = std::min(static_cast<int>(recentFiles.size()),
                                        config.maxRecentFiles);
            for (int i = 0; i < displayCount; i++) {
                const std::string& fullPath = recentFiles[i];
                std::filesystem::path p(fullPath);
                std::string displayName = p.filename().string();
                std::string label = std::to_string(i + 1) + ". " + displayName;

                std::string pathCopy = fullPath;
                recentFilesPopupMenu->AddItem(
                    MenuItemData::Action(label, [this, pathCopy]() {
                        if (std::filesystem::exists(pathCopy)) {
                            OpenDocumentFromPath(pathCopy);
                        } else {
                            RemoveFromRecentFiles(pathCopy);
                            debugOutput << "Recent file no longer exists: "
                                        << pathCopy << std::endl;
                        }
                    })
                );
            }

            recentFilesPopupMenu->AddItem(MenuItemData::Separator());
            recentFilesPopupMenu->AddItem(
                MenuItemData::Action("Clear Recent Files", [this]() {
                    recentFiles.clear();
                    SaveRecentFiles();
                    RebuildRecentFilesSubmenu();
                })
            );
        }

        // Position below the toolbar button and open
        auto item = toolbar->GetItem("recent");
        if (!item) return;
        auto btn = item->GetWidget();
        if (!btn) return;

        recentFilesPopupMenu->OpenMenu(
            Point2Di(btn->GetXInWindow(),
                     btn->GetYInWindow() + btn->GetHeight() + 1),
            *win, PopupElementSettings());
    }

    void UltraCanvasTextEditor::LoadRecentFiles() {
        recentFiles = configFile.LoadRecentFiles();

        // Trim to configured max
        if (static_cast<int>(recentFiles.size()) > config.maxRecentFiles) {
            recentFiles.resize(config.maxRecentFiles);
        }
    }

    void UltraCanvasTextEditor::SaveRecentFiles() {
        configFile.SaveRecentFiles(recentFiles);
    }


// -------------------------------------------------------------------------
// 4c: Config File Load/Save
// -------------------------------------------------------------------------


    void UltraCanvasTextEditor::LoadConfig() {
        configFile.Load();

        // Apply loaded settings to TextEditorConfig
        config.darkTheme = configFile.GetBool("darkTheme", config.darkTheme);
        config.showLineNumbers = configFile.GetBool("showLineNumbers", config.showLineNumbers);
        config.wordWrap = configFile.GetBool("wordWrap", config.wordWrap);
        config.defaultFontSize = configFile.GetInt("defaultFontSize", config.defaultFontSize);
        config.fontZoomPercent = configFile.GetInt("fontZoomPercent", 100);
        config.maxRecentFiles = configFile.GetInt("maxRecentFiles", config.maxRecentFiles);
        config.enableAutosave = configFile.GetBool("enableAutosave", config.enableAutosave);
        config.autosaveIntervalSeconds = configFile.GetInt("autosaveInterval", config.autosaveIntervalSeconds);
        config.defaultLanguage = configFile.GetString("defaultLanguage", config.defaultLanguage);
        config.showToolbar = configFile.GetBool("showToolbar", config.showToolbar);
        config.showMarkdownToolbar = configFile.GetBool("showMarkdownToolbar", config.showMarkdownToolbar);
    }

    void UltraCanvasTextEditor::SaveConfig() {
        configFile.SetBool("darkTheme", isDarkTheme);
        configFile.SetBool("showLineNumbers", config.showLineNumbers);
        configFile.SetBool("wordWrap", config.wordWrap);
        configFile.SetInt("defaultFontSize", config.defaultFontSize);
        configFile.SetInt("fontZoomPercent", config.fontZoomPercent);
        configFile.SetInt("maxRecentFiles", config.maxRecentFiles);
        configFile.SetBool("enableAutosave", config.enableAutosave);
        configFile.SetInt("autosaveInterval", config.autosaveIntervalSeconds);
        configFile.SetString("defaultLanguage", config.defaultLanguage);
        configFile.SetBool("showToolbar", config.showToolbar);
        configFile.SetBool("showMarkdownToolbar", config.showMarkdownToolbar);
        configFile.Save();
    }

// -------------------------------------------------------------------------
// 4d: Session Save/Restore
// -------------------------------------------------------------------------

    void UltraCanvasTextEditor::SaveSession() {
        std::vector<TextEditorConfigFile::SessionDocument> entries;
        int activeAmongSaved = 0;

        for (size_t i = 0; i < documents.size(); i++) {
            const auto& doc = documents[i];
            if (!doc) continue;

            // Skip untouched empty unsaved tabs — nothing to restore
            if (doc->filePath.empty() && doc->autosaveBackupPath.empty()) {
                continue;
            }

            TextEditorConfigFile::SessionDocument entry;
            entry.filePath    = doc->filePath;
            entry.displayName = doc->filePath.empty() ? doc->fileName : std::string();
            // Only persist a backup reference when it will actually be meaningful
            // on restore (i.e. the document has content the user hasn't saved to disk).
            if (doc->isModified && !doc->autosaveBackupPath.empty()) {
                entry.backupPath = doc->autosaveBackupPath;
            } else if (doc->filePath.empty() && !doc->autosaveBackupPath.empty()) {
                // Unsaved tab with a backup (e.g. recovered) — always carry it forward
                entry.backupPath = doc->autosaveBackupPath;
            }
            entry.language    = doc->language;
            entry.encoding    = doc->encoding;
            entry.eolType     = static_cast<int>(doc->eolType);
            entry.isNewFile   = doc->isNewFile;
            entry.wasModified = doc->isModified;

            if (static_cast<int>(i) == activeDocumentIndex) {
                activeAmongSaved = static_cast<int>(entries.size());
            }
            entries.push_back(std::move(entry));
        }

        configFile.SaveSession(entries, activeAmongSaved);
    }

} // namespace UltraCanvas