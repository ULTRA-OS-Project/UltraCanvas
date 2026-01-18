// core/UltraCanvasTextEditor.cpp
// Complete text editor application implementation
// Version: 1.0.0
// Last Modified: 2025-12-20
// Author: UltraCanvas Framework

#include "UltraCanvasTextEditor.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextEditorHelpers.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace UltraCanvas {

// ===== CONSTRUCTOR =====

    UltraCanvasTextEditor::UltraCanvasTextEditor(
            const std::string& identifier, long id,
            int x, int y, int width, int height,
            const TextEditorConfig& cfg)
            : UltraCanvasContainer(identifier, id, x, y, width, height)
            , config(cfg)
            , currentLanguage(cfg.defaultLanguage)
            , isDarkTheme(cfg.darkTheme)
    {
        SetBackgroundColor(Color(240, 240, 240, 255));

        // Setup components in order
        if (config.showMenuBar) {
            SetupMenuBar();
        }

        if (config.showToolbar) {
            SetupToolbar();
        }

        SetupEditor();

        if (config.showStatusBar) {
            SetupStatusBar();
        }

        SetupLayout();
        UpdateTitle();
    }

// ===== SETUP METHODS =====

    void UltraCanvasTextEditor::SetupMenuBar() {
        // Create menu bar using MenuBuilder
        menuBar = MenuBuilder("EditorMenuBar", 100, 0, 0, GetWidth(), 28)
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
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("🚪 Quit", "Alt+F4", [this]() {
                            OnFileQuit();
                        })
                })

                        // ===== EDIT MENU =====
                .AddSubmenu("Edit", {
                        MenuItemData::ActionWithShortcut("🔍 Search...", "Ctrl+F", [this]() {
                            OnEditSearch();
                        }),
                        MenuItemData::ActionWithShortcut("🔄 Replace...", "Ctrl+H", [this]() {
                            OnEditReplace();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("📋 Copy", "Ctrl+C", [this]() {
                            OnEditCopy();
                        }),
                        MenuItemData::ActionWithShortcut("✂️ Cut", "Ctrl+X", [this]() {
                            OnEditCut();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("📌 Paste All", "Ctrl+V", [this]() {
                            OnEditPasteAll();
                        }),
                        MenuItemData::ActionWithShortcut("📝 Paste Text", "Ctrl+Shift+V", [this]() {
                            OnEditPasteText();
                        })
                })

                        // ===== INFO MENU =====
                .AddSubmenu("Info", {
                        MenuItemData::ActionWithShortcut("❓ Help", "F1", [this]() {
                            OnInfoHelp();
                        }),
                        MenuItemData::Separator(),
                        MenuItemData::Action("ℹ️ About Ultra Text Editor", [this]() {
                            OnInfoAbout();
                        })
                })

                .Build();

        AddChild(menuBar);
    }

    void UltraCanvasTextEditor::SetupToolbar() {
        int toolbarY = config.showMenuBar ? 28 : 0;

        toolbar = UltraCanvasToolbarBuilder("EditorToolbar", 200)
                .SetOrientation(ToolbarOrientation::Horizontal)
                .SetStyle(ToolbarStyle::Standard)
                .SetDimensions(0, toolbarY, GetWidth(), 36)

                .AddButton("new", "New", "", [this]() { OnFileNew(); })
                .AddButton("open", "Open", "", [this]() { OnFileOpen(); })
                .AddButton("save", "Save", "", [this]() { OnFileSave(); })
                .AddSeparator()
                .AddButton("cut", "Cut", "", [this]() { OnEditCut(); })
                .AddButton("copy", "Copy", "", [this]() { OnEditCopy(); })
                .AddButton("paste", "Paste", "", [this]() { OnEditPasteAll(); })
                .AddSeparator()
                .AddButton("search", "Search", "", [this]() { OnEditSearch(); })
                .AddButton("replace", "Replace", "", [this]() { OnEditReplace(); })

                .Build();

        AddChild(toolbar);
    }

    void UltraCanvasTextEditor::SetupEditor() {
        // Calculate editor position and size
        int editorY = 0;
        if (config.showMenuBar) editorY += 28;
        if (config.showToolbar) editorY += 36;

        int editorHeight = GetHeight() - editorY;
        if (config.showStatusBar) editorHeight -= 24;

        // Create the text editor
        if (isDarkTheme) {
            textArea = CreateDarkCodeEditor("TextEditor", 300,
                                            0, editorY, GetWidth(), editorHeight,
                                            currentLanguage);
        } else {
            textArea = CreateCodeEditor("TextEditor", 300,
                                        0, editorY, GetWidth(), editorHeight,
                                        currentLanguage);
        }

        textArea->SetShowLineNumbers(config.showLineNumbers);
//        textEditor->SetCharsetEncoding(config.defaultEncoding);

        // Track modifications
        textArea->SetOnTextChanged([this](const std::string& text) {
            this->SetModified(true);
        });

        AddChild(textArea);
    }

    void UltraCanvasTextEditor::SetupStatusBar() {
        int statusBarY = GetHeight() - 24;

        // Create status bar linked to editor
        statusBar = CreateTextEditorStatusBar("EditorStatusBar", 400, textArea);
        statusBar->SetBounds(Rect2Di(0, statusBarY, GetWidth(), 24));

        // Update syntax mode display
        UpdateStatusBarSyntaxMode(statusBar, currentLanguage);

        AddChild(statusBar);
    }

    void UltraCanvasTextEditor::SetupLayout() {
        // Layout is handled by fixed positioning in setup methods
        // This method can be extended for responsive layout if needed
    }

// ===== FILE OPERATIONS =====

    bool UltraCanvasTextEditor::LoadFile(const std::string& filePath) {
        try {
            std::ifstream file(filePath);
            if (!file.is_open()) {
                std::cerr << "Failed to open file: " << filePath << std::endl;
                return false;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            file.close();

            textArea->SetText(buffer.str());
            currentFilePath = filePath;

            // Auto-detect language from file extension
            std::string detectedLang = DetectLanguageFromExtension(filePath);
            if (!detectedLang.empty()) {
                SetLanguage(detectedLang);
            }

            // Auto-detect encoding
//            std::string detectedEncoding = textEditor->DetectCharsetEncoding();
//            textEditor->SetCharsetEncoding(detectedEncoding);

            SetModified(false);
            UpdateTitle();

            if (onFileLoaded) {
                onFileLoaded(filePath);
            }

            return true;

        } catch (const std::exception& e) {
            std::cerr << "Error loading file: " << e.what() << std::endl;
            return false;
        }
    }

    bool UltraCanvasTextEditor::SaveFile() {
        if (currentFilePath.empty()) {
            return false; // No file path set, use SaveFileAs
        }

        return SaveFileAs(currentFilePath);
    }

    bool UltraCanvasTextEditor::SaveFileAs(const std::string& filePath) {
        try {
            std::ofstream file(filePath);
            if (!file.is_open()) {
                std::cerr << "Failed to save file: " << filePath << std::endl;
                return false;
            }

            file << textArea->GetText();
            file.close();

            currentFilePath = filePath;
            SetModified(false);
            UpdateTitle();

            if (onFileSaved) {
                onFileSaved(filePath);
            }

            return true;

        } catch (const std::exception& e) {
            std::cerr << "Error saving file: " << e.what() << std::endl;
            return false;
        }
    }

    void UltraCanvasTextEditor::NewFile() {
        if (isModified && !ConfirmSaveChanges()) {
            return;
        }

        textArea->SetText("");
        currentFilePath = "";
        SetLanguage("Plain Text");
//        textEditor->SetCharsetEncoding("UTF-8");
        SetModified(false);
        UpdateTitle();
    }

    std::string UltraCanvasTextEditor::GetText() const {
        return textArea ? textArea->GetText() : "";
    }

    void UltraCanvasTextEditor::SetText(const std::string& text) {
        if (textArea) {
            textArea->SetText(text);
            SetModified(true);
        }
    }

// ===== SYNTAX HIGHLIGHTING =====

    void UltraCanvasTextEditor::SetLanguage(const std::string& language) {
        currentLanguage = language;

        if (textArea) {
            if (language == "Plain Text" || language.empty()) {
                textArea->SetHighlightSyntax(false);
            } else {
                textArea->SetHighlightSyntax(true);
                textArea->SetProgrammingLanguage(language);
            }
        }

        if (statusBar) {
            UpdateStatusBarSyntaxMode(statusBar, language);
        }
    }

// ===== THEME =====

    void UltraCanvasTextEditor::ApplyDarkTheme() {
        isDarkTheme = true;
        if (textArea) {
            textArea->ApplyDarkCodeStyle(currentLanguage);
        }
        SetBackgroundColor(Color(45, 45, 45, 255));
    }

    void UltraCanvasTextEditor::ApplyLightTheme() {
        isDarkTheme = false;
        if (textArea) {
            textArea->ApplyCodeStyle(currentLanguage);
        }
        SetBackgroundColor(Color(240, 240, 240, 255));
    }

// ===== MENU ACTION HANDLERS =====

    void UltraCanvasTextEditor::OnFileNew() {
        NewFile();
    }

    void UltraCanvasTextEditor::OnFileOpen() {
        if (isModified && !ConfirmSaveChanges()) {
            return;
        }

        // Show file dialog
        std::string filePath = UltraCanvasDialogManager::ShowOpenFileDialog(
                "Open File",
                {{"All Files (*.*)", "*"}, {"Text Files (*.txt)", "txt"}, {"Source Code (*.cpp;*.h;*.py)", {"cpp", "h", "py"}}},
                ""
        );

        if (!filePath.empty()) {
            LoadFile(filePath);
        }
    }

    void UltraCanvasTextEditor::OnFileSave() {
        if (currentFilePath.empty()) {
            OnFileSaveAs();
        } else {
            SaveFile();
        }
    }

    void UltraCanvasTextEditor::OnFileSaveAs() {
        std::string defaultName = currentFilePath.empty() ? "untitled.txt" : currentFilePath;

        std::string filePath = UltraCanvasDialogManager::ShowSaveFileDialog(
                "Save File As",
                {{"All Files", "*"},{"Text Files", "txt"}},
                "",
                defaultName
        );

        if (!filePath.empty()) {
            SaveFileAs(filePath);
        }
    }

    void UltraCanvasTextEditor::OnFileQuit() {
        if (isModified && !ConfirmSaveChanges()) {
            return;
        }

        if (onQuitRequest) {
            onQuitRequest();
        }
    }

    void UltraCanvasTextEditor::OnEditSearch() {
        // Show search dialog
        std::string searchText = UltraCanvasDialogManager::ShowInputDialog(
                "Search",
                "Enter text to search:",
                ""
        );

        if (!searchText.empty() && textArea) {
            textArea->FindText(searchText, false);
        }
    }

    void UltraCanvasTextEditor::OnEditReplace() {
        // Show replace dialog - simplified version
        std::string searchText = UltraCanvasDialogManager::ShowInputDialog(
                "Replace",
                "Enter text to find:",
                ""
        );

        if (!searchText.empty()) {
            std::string replaceText = UltraCanvasDialogManager::ShowInputDialog(
                    "Replace",
                    "Replace with:",
                    ""
            );

            if (textArea) {
                textArea->ReplaceText(searchText, replaceText, false);
            }
        }
    }

    void UltraCanvasTextEditor::OnEditCopy() {
        if (textArea) {
            textArea->CopySelection();
        }
    }

    void UltraCanvasTextEditor::OnEditCut() {
        if (textArea) {
            textArea->CutSelection();
        }
    }

    void UltraCanvasTextEditor::OnEditPasteAll() {
        if (textArea) {
            textArea->PasteClipboard();
        }
    }

    void UltraCanvasTextEditor::OnEditPasteText() {
        // Paste as plain text (strip formatting)
        // For text editor, this is the same as regular paste
        if (textArea) {
            textArea->PasteClipboard();
        }
    }

    void UltraCanvasTextEditor::OnInfoHelp() {
        if (onHelpRequest) {
            onHelpRequest();
        } else {
            UltraCanvasDialogManager::ShowInformation(
                    "Ultra Text Editor Help\n\n"
                    "Keyboard Shortcuts:\n"
                    "• Ctrl+N - New file\n"
                    "• Ctrl+O - Open file\n"
                    "• Ctrl+S - Save file\n"
                    "• Ctrl+Shift+S - Save As\n"
                    "• Ctrl+F - Search\n"
                    "• Ctrl+H - Replace\n"
                    "• Ctrl+C - Copy\n"
                    "• Ctrl+X - Cut\n"
                    "• Ctrl+V - Paste\n"
                    "• F1 - Help",
                    "Help"
            );
        }
    }

    void UltraCanvasTextEditor::OnInfoAbout() {
        if (onAboutRequest) {
            onAboutRequest();
        } else {
            UltraCanvasDialogManager::ShowInformation(
                    "Ultra Text Editor\n\n"
                    "Version: 1.0.0\n"
                    "A powerful, cross-platform text editor\n"
                    "built with UltraCanvas Framework.\n\n"
                    "Features:\n"
                    "• Syntax highlighting for 30+ languages\n"
                    "• Line numbers\n"
                    "• Search and replace\n"
                    "• Multiple encodings support\n"
                    "• Dark and light themes\n\n"
                    "© 2025 UltraCanvas Framework",
                    "About Ultra Text Editor"
            );
        }
    }

// ===== HELPER METHODS =====

    void UltraCanvasTextEditor::UpdateTitle() {
        std::string title = config.title;

        if (!currentFilePath.empty()) {
            // Extract filename from path
            size_t lastSlash = currentFilePath.find_last_of("/\\");
            std::string filename = (lastSlash != std::string::npos)
                                   ? currentFilePath.substr(lastSlash + 1)
                                   : currentFilePath;
            title = filename + " - " + config.title;
        }

        if (isModified) {
            title = "* " + title;
        }

        // Update window title if we have access to parent window
        // This would require window access - for now just store the title
    }

    void UltraCanvasTextEditor::SetModified(bool modified) {
        if (isModified != modified) {
            isModified = modified;
            UpdateTitle();

            if (onModifiedChange) {
                onModifiedChange(modified);
            }
        }
    }

    std::string UltraCanvasTextEditor::DetectLanguageFromExtension(const std::string& filePath) {
        // Extract extension
        size_t lastDot = filePath.find_last_of('.');
        if (lastDot == std::string::npos) {
            return "Plain Text";
        }

        std::string ext = filePath.substr(lastDot + 1);

        // Convert to lowercase
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // Map extension to language
        static const std::unordered_map<std::string, std::string> extToLang = {
                // C/C++
                {"c", "C"}, {"h", "C"}, {"cpp", "C++"}, {"cxx", "C++"},
                {"cc", "C++"}, {"hpp", "C++"}, {"hxx", "C++"},
                // Python
                {"py", "Python"}, {"pyw", "Python"}, {"pyx", "Python"},
                // JavaScript/TypeScript
                {"js", "JavaScript"}, {"jsx", "JavaScript"},
                {"ts", "TypeScript"}, {"tsx", "TypeScript"},
                // Java
                {"java", "Java"},
                // C#
                {"cs", "C#"},
                // Go
                {"go", "Go"},
                // Rust
                {"rs", "Rust"},
                // Pascal
                {"pas", "Pascal"}, {"pp", "Pascal"}, {"dpr", "Pascal"},
                // Web
                {"html", "HTML"}, {"htm", "HTML"},
                {"css", "CSS"},
                {"xml", "XML"},
                {"json", "JSON"},
                // SQL
                {"sql", "SQL"},
                // PHP
                {"php", "PHP"},
                // Ruby
                {"rb", "Ruby"},
                // Perl
                {"pl", "Perl"}, {"pm", "Perl"},
                // Shell
                {"sh", "Bash"}, {"bash", "Bash"},
                // Markdown
                {"md", "Markdown"}, {"markdown", "Markdown"},
                // Lua
                {"lua", "Lua"},
                // Swift
                {"swift", "Swift"},
                // Kotlin
                {"kt", "Kotlin"}, {"kts", "Kotlin"},
                // Assembly
                {"asm", "Assembly"}, {"s", "Assembly"},
                // Plain text
                {"txt", "Plain Text"}, {"log", "Plain Text"}
        };

        auto it = extToLang.find(ext);
        return (it != extToLang.end()) ? it->second : "Plain Text";
    }

    bool UltraCanvasTextEditor::ConfirmSaveChanges() {
        DialogResult result = UltraCanvasDialogManager::ShowQuestion(
                "The document has unsaved changes.\n"
                "Do you want to save before continuing?",
                "Save Changes?"
        );

        if (result == DialogResult::Yes) {
            OnFileSave();
            return !isModified; // Return true if save was successful
        }

        return (result == DialogResult::No); // Return true if user chose No
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