// Apps/DemoApp/UltraCanvasTextEditorExample.cpp
// Demo example showing the complete Ultra Text Editor application
// Version: 1.0.0
// Last Modified: 2025-12-20
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTextEditor.h"
#include <memory>
#include <iostream>

namespace UltraCanvas {

/**
 * @brief Creates a demo showcasing the complete Ultra Text Editor application
 *
 * This demo displays:
 * - Full text editor with menu bar (File, Edit, Info)
 * - Toolbar with common actions
 * - Syntax-highlighted text area
 * - Status bar with position, encoding, syntax mode
 *
 * @return Container with the complete demo
 */
    std::shared_ptr<UltraCanvasContainer> UltraCanvasDemoApplication::CreateTextEditorDemo() {
        // Main container
        auto container = std::make_shared<UltraCanvasContainer>("TextEditorDemo", 5000, 0, 0, 1024, 700);
        container->SetBackgroundColor(Color(230, 230, 230, 255));

        // ===== TITLE =====
        auto titleLabel = std::make_shared<UltraCanvasLabel>("Title", 5001, 20, 10, 984, 30);
        titleLabel->SetText("Ultra Text Editor - Complete Application Demo");
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(titleLabel);

        // ===== DESCRIPTION =====
        auto descLabel = std::make_shared<UltraCanvasLabel>("Description", 5002, 20, 45, 984, 25);
        descLabel->SetText("Full-featured text editor with menu bar, toolbar, syntax highlighting, and status bar");
        descLabel->SetFontSize(11);
        descLabel->SetTextColor(Color(100, 100, 100, 255));
        descLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(descLabel);

        // ===== TEXT EDITOR APPLICATION =====

        // Configure the text editor
        TextEditorConfig config;
        config.title = "Ultra Text Editor";
        config.showMenuBar = true;
        config.showToolbar = true;
        config.showStatusBar = true;
        config.showLineNumbers = true;
        config.defaultLanguage = "C++";
        config.darkTheme = false;

        // Create the text editor application
        auto textEditor = CreateTextEditor("DemoEditor", 5100, 20, 80, 984, 550, config);

        // Set sample content
        textEditor->SetText(
                "// Ultra Text Editor - Sample Code\n"
                "// This demonstrates the full-featured text editor\n"
                "\n"
                "#include <iostream>\n"
                "#include <string>\n"
                "#include <vector>\n"
                "#include <memory>\n"
                "\n"
                "namespace UltraCanvas {\n"
                "\n"
                "/**\n"
                " * @brief Example class demonstrating syntax highlighting\n"
                " */\n"
                "class TextEditorDemo {\n"
                "private:\n"
                "    std::string documentName;\n"
                "    std::vector<std::string> lines;\n"
                "    bool isModified = false;\n"
                "\n"
                "public:\n"
                "    TextEditorDemo(const std::string& name) \n"
                "        : documentName(name) {\n"
                "        std::cout << \"Creating document: \" << name << std::endl;\n"
                "    }\n"
                "\n"
                "    void AddLine(const std::string& line) {\n"
                "        lines.push_back(line);\n"
                "        isModified = true;\n"
                "    }\n"
                "\n"
                "    size_t GetLineCount() const {\n"
                "        return lines.size();\n"
                "    }\n"
                "\n"
                "    bool HasUnsavedChanges() const {\n"
                "        return isModified;\n"
                "    }\n"
                "};\n"
                "\n"
                "} // namespace UltraCanvas\n"
                "\n"
                "int main() {\n"
                "    UltraCanvas::TextEditorDemo demo(\"MyDocument\");\n"
                "    \n"
                "    demo.AddLine(\"Hello, UltraCanvas!\");\n"
                "    demo.AddLine(\"This is a text editor demo.\");\n"
                "    \n"
                "    std::cout << \"Lines: \" << demo.GetLineCount() << std::endl;\n"
                "    \n"
                "    return 0;\n"
                "}\n"
        );

        // Set up callbacks
        textEditor->onFileLoaded = [](const std::string& path) {
            std::cout << "File loaded: " << path << std::endl;
        };

        textEditor->onFileSaved = [](const std::string& path) {
            std::cout << "File saved: " << path << std::endl;
        };

        textEditor->onModifiedChange = [](bool modified) {
            std::cout << "Document modified: " << (modified ? "yes" : "no") << std::endl;
        };

        textEditor->onQuitRequest = []() {
            std::cout << "Quit requested" << std::endl;
        };

        container->AddChild(textEditor);

        // ===== CONTROL BUTTONS =====
        int buttonY = 640;

        // Theme toggle button
        auto themeButton = std::make_shared<UltraCanvasButton>("ThemeButton", 5200, 20, buttonY, 120, 30);
        themeButton->SetText("Dark Theme");

        std::weak_ptr<UltraCanvasTextEditor> weakEditor = textEditor;
        bool isDark = false;

        themeButton->onClick = [=]() mutable {
            auto editor = weakEditor.lock();
            if (!editor) return;

            isDark = !isDark;
            if (isDark) {
                editor->ApplyDarkTheme();
                themeButton->SetText("Light Theme");
            } else {
                editor->ApplyLightTheme();
                themeButton->SetText("Dark Theme");
            }
        };
        container->AddChild(themeButton);

        // Language selector buttons
        auto cppButton = std::make_shared<UltraCanvasButton>("CppButton", 5201, 160, buttonY, 80, 30);
        cppButton->SetText("C++");
        cppButton->onClick = [weakEditor]() {
            if (auto editor = weakEditor.lock()) {
                editor->SetLanguage("C++");
                editor->SetText(
                        "// Ultra Text Editor - Sample Code\n"
                        "// This demonstrates the full-featured text editor\n"
                        "\n"
                        "#include <iostream>\n"
                        "#include <string>\n"
                        "#include <vector>\n"
                        "#include <memory>\n"
                        "\n"
                        "namespace UltraCanvas {\n"
                        "\n"
                        "/**\n"
                        " * @brief Example class demonstrating syntax highlighting\n"
                        " */\n"
                        "class TextEditorDemo {\n"
                        "private:\n"
                        "    std::string documentName;\n"
                        "    std::vector<std::string> lines;\n"
                        "    bool isModified = false;\n"
                        "\n"
                        "public:\n"
                        "    TextEditorDemo(const std::string& name) \n"
                        "        : documentName(name) {\n"
                        "        std::cout << \"Creating document: \" << name << std::endl;\n"
                        "    }\n"
                        "\n"
                        "    void AddLine(const std::string& line) {\n"
                        "        lines.push_back(line);\n"
                        "        isModified = true;\n"
                        "    }\n"
                        "\n"
                        "    size_t GetLineCount() const {\n"
                        "        return lines.size();\n"
                        "    }\n"
                        "\n"
                        "    bool HasUnsavedChanges() const {\n"
                        "        return isModified;\n"
                        "    }\n"
                        "};\n"
                        "\n"
                        "} // namespace UltraCanvas\n"
                        "\n"
                        "int main() {\n"
                        "    UltraCanvas::TextEditorDemo demo(\"MyDocument\");\n"
                        "    \n"
                        "    demo.AddLine(\"Hello, UltraCanvas!\");\n"
                        "    demo.AddLine(\"This is a text editor demo.\");\n"
                        "    \n"
                        "    std::cout << \"Lines: \" << demo.GetLineCount() << std::endl;\n"
                        "    \n"
                        "    return 0;\n"
                        "}\n"
                );
            }
        };
        container->AddChild(cppButton);

        auto pythonButton = std::make_shared<UltraCanvasButton>("PythonButton", 5202, 250, buttonY, 80, 30);
        pythonButton->SetText("Python");
        pythonButton->onClick = [weakEditor]() {
            if (auto editor = weakEditor.lock()) {
                editor->SetLanguage("Python");
                editor->SetText(
                        "# Python Example\n"
                        "import os\n"
                        "import sys\n"
                        "\n"
                        "class TextEditor:\n"
                        "    \"\"\"A simple text editor class\"\"\"\n"
                        "    \n"
                        "    def __init__(self, name: str):\n"
                        "        self.name = name\n"
                        "        self.lines = []\n"
                        "        self.modified = False\n"
                        "    \n"
                        "    def add_line(self, line: str) -> None:\n"
                        "        self.lines.append(line)\n"
                        "        self.modified = True\n"
                        "    \n"
                        "    @property\n"
                        "    def line_count(self) -> int:\n"
                        "        return len(self.lines)\n"
                        "\n"
                        "if __name__ == '__main__':\n"
                        "    editor = TextEditor('MyDocument')\n"
                        "    editor.add_line('Hello, World!')\n"
                        "    print(f'Lines: {editor.line_count}')\n"
                );
            }
        };
        container->AddChild(pythonButton);

        auto jsButton = std::make_shared<UltraCanvasButton>("JsButton", 5203, 340, buttonY, 100, 30);
        jsButton->SetText("JavaScript");
        jsButton->onClick = [weakEditor]() {
            if (auto editor = weakEditor.lock()) {
                editor->SetLanguage("JavaScript");
                editor->SetText(
                        "// JavaScript Example\n"
                        "class TextEditor {\n"
                        "    constructor(name) {\n"
                        "        this.name = name;\n"
                        "        this.lines = [];\n"
                        "        this.modified = false;\n"
                        "    }\n"
                        "    \n"
                        "    addLine(line) {\n"
                        "        this.lines.push(line);\n"
                        "        this.modified = true;\n"
                        "    }\n"
                        "    \n"
                        "    get lineCount() {\n"
                        "        return this.lines.length;\n"
                        "    }\n"
                        "}\n"
                        "\n"
                        "// Create editor instance\n"
                        "const editor = new TextEditor('MyDocument');\n"
                        "editor.addLine('Hello, World!');\n"
                        "console.log(`Lines: ${editor.lineCount}`);\n"
                );
            }
        };
        container->AddChild(jsButton);

        // Info label
        auto infoLabel = std::make_shared<UltraCanvasLabel>("InfoLabel", 5210, 500, buttonY + 5, 500, 20);
        infoLabel->SetText("Menu: File (New, Open, Save, Quit) | Edit (Search, Replace, Copy, Cut, Paste) | Info (Help, About)");
        infoLabel->SetFontSize(9);
        infoLabel->SetTextColor(Color(80, 80, 80, 255));
        container->AddChild(infoLabel);

        return container;
    }

/**
 * @brief Creates a standalone text editor window demo
 *
 * This creates just the text editor without the demo wrapper,
 * suitable for use as a standalone application component.
 *
 * @return The text editor application component
 */
    std::shared_ptr<UltraCanvasTextEditor> CreateStandaloneTextEditor() {
        TextEditorConfig config;
        config.title = "Ultra Text Editor";
        config.showMenuBar = true;
        config.showToolbar = true;
        config.showStatusBar = true;
        config.showLineNumbers = true;
        config.darkTheme = false;

        return CreateTextEditor("StandaloneEditor", 1, 0, 0, 1024, 768, config);
    }

/**
 * Demo registration info:
 *
 * Add to demo application:
 * RegisterDemo("Ultra Text Editor App", DemoCategory::Applications, CreateTextEditorDemo);
 */

} // namespace UltraCanvas