// ExampleClipboardApp.cpp
// Example showing how to use the new modular clipboard system
// Version: 1.0.0
// Last Modified: 2025-08-13
// Author: UltraCanvas Framework

#include "UltraCanvasClipboard.h"
#include "UltraCanvasClipboardUI.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTextInput.h"
#include <iostream>

using namespace UltraCanvas;

class ModularClipboardDemoApp {
private:
    UltraCanvasApplication* application;
    std::shared_ptr<UltraCanvasWindow> mainWindow;
    std::shared_ptr<UltraCanvasTextInput> textInput;
    std::shared_ptr<UltraCanvasButton> copyButton;
    std::shared_ptr<UltraCanvasButton> pasteButton;
    std::shared_ptr<UltraCanvasButton> showClipboardButton;
    bool isRunning = true;

public:
    bool Initialize() {
        std::cerr << "=== UltraCanvas Modular Clipboard Demo ===" << std::endl;
        
        // Step 1: Initialize the application
        application = new UltraCanvasApplication();
        if (!application->Initialize()) {
            std::cerr << "Failed to initialize UltraCanvas application" << std::endl;
            return false;
        }
        
        // Step 2: Initialize the platform-independent clipboard
        if (!InitializeClipboard()) {
            std::cerr << "Failed to initialize clipboard system" << std::endl;
            return false;
        }
        
        // Step 3: Initialize the clipboard UI (optional)
        InitializeClipboardUI();
        
        // Step 4: Create main demo window
        CreateMainWindow();
        CreateUI();
        //AddSampleData();
        
        std::cerr << "✅ Modular clipboard system initialized successfully!" << std::endl;
        std::cerr << "📋 Features available:" << std::endl;
        std::cerr << "   • Platform-independent clipboard core" << std::endl;
        std::cerr << "   • X11 backend for Linux" << std::endl;
        std::cerr << "   • Separate UI component" << std::endl;
        std::cerr << "   • History management (up to 100 entries)" << std::endl;
        std::cerr << "   • Multiple data types support" << std::endl;
        std::cerr << "   • File save functionality" << std::endl;
        std::cerr << "🚀 Press ALT+P to show clipboard history window" << std::endl;
        
        return true;
    }
    
    void CreateMainWindow() {
        WindowConfig config;
        config.title = "UltraCanvas Modular Clipboard Demo";
        config.width = 800;
        config.height = 600;
        config.resizable = true;
        config.type = WindowType::Standard;

        mainWindow = std::make_shared<UltraCanvasWindow>();
        if (!mainWindow->Create(config)) {
            std::cerr << "Failed to create main window" << std::endl;
            return;
        }

        mainWindow->onWindowClosing = [this]() {
            isRunning = false;
        };
    }
    
    void CreateUI() {
        if (!mainWindow) return;
        
        // Text input area
        textInput = CreateTextInput("textInput", 2001, 20, 80, 760, 200);
        textInput->SetInputType(TextInputType::Multiline);
        textInput->SetText("Welcome to the UltraCanvas Modular Clipboard System!\n\n"
                          "This demonstrates the new architecture where:\n"
                          "• UltraCanvasClipboard = Platform-independent core\n"
                          "• UltraCanvasLinuxClipboard = X11-specific implementation\n"
                          "• UltraCanvasClipboardUI = Visual interface\n\n"
                          "Type some text here and test the clipboard functions below.");
        
        TextInputStyle style = TextInputStyle::Default();
        style.fontFamily = "Consolas";
        style.fontSize = 12.0f;
        textInput->SetStyle(style);
        mainWindow->AddElement(textInput);
        
        // Control buttons
        CreateControlButtons();
        
        // Status display area would go here
        CreateStatusDisplay();
    }
    
    void CreateControlButtons() {
        if (!mainWindow) return;
        
        // Copy button - uses the new modular system
        copyButton = CreateButton("copyBtn", 2002, 20, 300, 150, 30, "Copy to Clipboard");
        copyButton->onClick = [this]() {
            if (textInput) {
                std::string text = textInput->GetText();
                if (!text.empty()) {
                    // Use the new modular clipboard API
                    if (SetClipboardText(text)) {
                        std::cerr << "✅ Text copied to clipboard using modular system" << std::endl;
                    } else {
                        std::cerr << "❌ Failed to copy text" << std::endl;
                    }
                }
            }
        };
        mainWindow->AddElement(copyButton);
        
        // Paste button - uses the new modular system
        pasteButton = CreateButton("pasteBtn", 2003, 200, 300, 150, 30, "Paste from Clipboard");
        pasteButton->onClick = [this]() {
            std::string clipboardText;
            if (GetClipboardText(clipboardText)) {
                if (textInput) {
                    textInput->SetText(clipboardText);
                    std::cerr << "✅ Text pasted from clipboard using modular system" << std::endl;
                }
            } else {
                std::cerr << "❌ Failed to get clipboard text" << std::endl;
            }
        };
        mainWindow->AddElement(pasteButton);
        
        // Show clipboard history button
        showClipboardButton = CreateButton("showClipboardBtn", 2004, 380, 300, 180, 30, "Show Clipboard History");
        showClipboardButton->onClick = [this]() {
            ShowClipboard();
            std::cerr << "📋 Clipboard history window opened" << std::endl;
        };
        mainWindow->AddElement(showClipboardButton);
        
        // Add entry button - demonstrates programmatic addition
        auto addEntryButton = CreateButton("addEntryBtn", 2005, 580, 300, 150, 30, "Add Sample Entry");
        addEntryButton->onClick = [this]() {
            ClipboardData sampleEntry(ClipboardDataType::Text, "Sample programmatically added entry");
            AddClipboardEntry(sampleEntry);
            std::cerr << "✅ Sample entry added to clipboard history" << std::endl;
        };
        mainWindow->AddElement(addEntryButton);
    }
    
    void CreateStatusDisplay() {
        // This would show clipboard status, entry count, etc.
        // For now, we'll just use console output
    }
    
    void AddSampleData() {
        // Add some sample entries to demonstrate the system
        UltraCanvasClipboard* clipboard = GetClipboard();
        if (!clipboard) return;
        
        std::vector<std::string> sampleTexts = {
            "Sample clipboard entry #1 - Short text",
            "Here's a longer piece of text that demonstrates how the modular clipboard handles multi-line content and longer strings that might need to be truncated in the preview display.",
            "function calculateArea(radius) {\n    return Math.PI * radius * radius;\n}",
            "Email: user@example.com\nPhone: +1-555-0123\nAddress: 123 Main St, City, State 12345",
            "JSON Example: {\"name\": \"UltraCanvas\", \"version\": \"2.1.2\", \"modular\": true}",
        };

        for (const std::string& text : sampleTexts) {
            ClipboardData entry(ClipboardDataType::Text, text);
            clipboard->AddEntry(entry);
        }
        
        std::cerr << "📋 Added " << sampleTexts.size() << " sample entries to clipboard history" << std::endl;
    }
    
    void Run() {
        std::cerr << "🚀 Starting modular clipboard demo..." << std::endl;
        
        if (mainWindow) {
            mainWindow->Show();
        }
        
        // Start clipboard monitoring
        UltraCanvasClipboard* clipboard = GetClipboard();
        if (clipboard) {
            clipboard->StartMonitoring();
        }
        
        // Main application loop
        if (application) {
            application->Run();
        }
        
        std::cerr << "👋 Application shutting down..." << std::endl;
    }

    void Shutdown() {
        std::cerr << "🔄 Shutting down modular clipboard system..." << std::endl;
        
        // Stop clipboard monitoring
        UltraCanvasClipboard* clipboard = GetClipboard();
        if (clipboard) {
            clipboard->StopMonitoring();
        }
        
        // Shutdown UI
        ShutdownClipboardUI();
        
        // Shutdown core
        ShutdownClipboard();
        
        std::cerr << "✅ Modular clipboard system shut down cleanly" << std::endl;
    }
    
    // Custom window class for rendering status
    class ModularClipboardDemoWindow : public UltraCanvasWindow {
    private:
        ModularClipboardDemoApp* app;
        
    public:
        ModularClipboardDemoWindow(ModularClipboardDemoApp* demoApp) : app(demoApp) {}
        
        void Render(IRenderContext* ctx) override {
            UltraCanvasWindow::Render(IRenderContext* ctx); // Render base window and elements
            if (app) {
                app->DrawCustomUI(); // Draw custom overlay
            }
        }
    };
    
    void DrawCustomUI() {
        ULTRACANVAS_RENDER_SCOPE();
        
        // Draw title
        SetColor(Color(50, 50, 150, 255));
        SetFont("Sans", 16.0f);
        DrawText("UltraCanvas Modular Clipboard Demo", Point2D(20, 40));
        
        // Draw architecture info
        SetColor(Colors::DarkGray);
        SetFont("Sans", 12.0f);
        DrawText("Architecture: Core + Backend + UI separation", Point2D(20, 350));
        
        // Draw clipboard status
        UltraCanvasClipboard* clipboard = GetClipboard();
        if (clipboard) {
            SetColor(Colors::Blue);
            SetFont("Sans", 12.0f);
            
            size_t entryCount = clipboard->GetEntryCount();
            std::string statusText = "Clipboard entries: " + std::to_string(entryCount) + " / " + 
                                   std::to_string(UltraCanvasClipboard::MAX_ENTRIES);
            DrawText(statusText, Point2D(20, 370));
            
            // Show available formats
            auto formats = clipboard->GetAvailableFormats();
            if (!formats.empty()) {
                std::string formatsText = "Available formats: ";
                for (size_t i = 0; i < std::min(formats.size(), size_t(3)); ++i) {
                    if (i > 0) formatsText += ", ";
                    formatsText += formats[i];
                }
                if (formats.size() > 3) formatsText += "...";
                DrawText(formatsText, Point2D(20, 390));
            }
        }
        
        // Draw instructions
        SetColor(Colors::Gray);
        SetFont("Sans", 11.0f);
        
        std::vector<std::string> instructions = {
            "✨ New Modular Architecture Features:",
            "• Platform-independent core (UltraCanvasClipboard)",
            "• X11-specific backend (UltraCanvasLinuxClipboard)", 
            "• Separate UI component (UltraCanvasClipboardUI)",
            "• Clean separation of concerns",
            "• Easy to port to other platforms",
            "",
            "🎯 Usage Instructions:",
            "• Type text above and click 'Copy to Clipboard'",
            "• Click 'Paste from Clipboard' to retrieve text",
            "• Press ALT+P to open the clipboard history window",
            "• In history window: 'C'=copy, 'S'=save to file, 'X'=delete",
            "• Copy from other apps to see automatic detection",
            "",
            "🔧 Developer Benefits:",
            "• Core logic is platform-independent",
            "• Easy to add Windows/macOS backends",
            "• UI can be customized or replaced",
            "• Better testability and maintainability"
        };
        
        float yPos = 420;
        for (const std::string& instruction : instructions) {
            if (instruction.empty()) {
                yPos += 10;
                continue;
            }
            
            if (instruction[0] == '✨' || instruction[0] == '🎯' || instruction[0] == '🔧') {
                SetColor(Color(100, 50, 150, 255));
                SetFont("Sans", 12.0f);
            } else {
                SetColor(Colors::Gray);
                SetFont("Sans", 11.0f);
            }
            
            DrawText(instruction, Point2D(20, yPos));
            yPos += 18;
        }
    }
};

// ===== MAIN FUNCTION =====
int main() {
    try {
        std::cerr << "🎉 UltraCanvas Modular Clipboard System Demo" << std::endl;
        std::cerr << "=============================================" << std::endl;
        
        // Initialize keyboard manager first
        if (!UltraCanvasKeyboardManager::Initialize()) {
            std::cerr << "Failed to initialize keyboard manager" << std::endl;
            return -1;
        }
        
        // Create and run the demo application
        ModularClipboardDemoApp app;
        if (!app.Initialize()) {
            std::cerr << "Failed to initialize modular clipboard demo" << std::endl;
            return -1;
        }
        
        app.Run();
        app.Shutdown();
        
        // Cleanup
        UltraCanvasKeyboardManager::Shutdown();
        
        std::cerr << "🎉 Demo completed successfully!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Application error: " << e.what() << std::endl;
        return -1;
    } catch (...) {
        std::cerr << "Unknown application error" << std::endl;
        return -1;
    }
}
