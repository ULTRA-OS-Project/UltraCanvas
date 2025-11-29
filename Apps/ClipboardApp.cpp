// ClipboardApp.cpp
// Sample application demonstrating the UltraCanvas Multi-Entry Clipboard
// Version: 2.0.0 - Updated for latest UltraCanvas framework
// Last Modified: 2025-07-25
// Author: UltraCanvas Framework

#include "UltraCanvasUI.h"
#include "UltraCanvasClipboardManager.h"
#include "UltraCanvasTextInput.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace UltraCanvas;

class ClipboardDemoApp {
private:
    std::unique_ptr<UltraCanvasApplication> application;
    std::shared_ptr<UltraCanvasWindow> mainWindow;
    std::shared_ptr<UltraCanvasTextInput> textInput; // ✅ UPDATED: Changed from UltraCanvasTextArea
    std::shared_ptr<UltraCanvasButton> copyButton;
    std::shared_ptr<UltraCanvasButton> showClipboardButton;
    std::shared_ptr<UltraCanvasButton> clearHistoryButton;
    std::shared_ptr<UltraCanvasButton> addSampleButton;
    bool isRunning = true;
    
public:
    ClipboardDemoApp() {
        CreateApplication();
        CreateMainWindow();
        CreateUI();
        SetupEventHandlers();
    }
    
    void CreateApplication() {
        // ✅ UPDATED: Use new application initialization
        application = std::make_unique<UltraCanvasApplication>();
        if (!application->Initialize()) {
            throw std::runtime_error("Failed to initialize UltraCanvas application");
        }
    }
    
    void CreateMainWindow() {
        WindowConfig config;
        config.title = "UltraCanvas Clipboard Demo";
        config.width = 800;
        config.height = 600;
        config.resizable = true;
        config.type = WindowType::Standard;
        
        // ✅ UPDATED: Use new window creation API
        mainWindow = std::make_shared<UltraCanvasWindow>();
        if (!mainWindow->Create(config)) {
            throw std::runtime_error("Failed to create main window");
        }
        
        // ✅ UPDATED: Use new callback name
        mainWindow->onWindowClosing = [this]() {
            isRunning = false;
        };
    }
    
    void CreateUI() {
        if (!mainWindow) return;
        
        // ✅ UPDATED: Use UltraCanvasTextInput with multiline mode instead of UltraCanvasTextArea
        textInput = CreateTextInput("textInput", 2001, 20, 80, 760, 200);
        textInput->SetInputType(TextInputType::Multiline); // Enable multiline mode
        textInput->SetText("Type some text here and click 'Copy to Clipboard' to test the multi-entry clipboard.\n\n"
                          "Then press ALT+P to open the clipboard history window.\n\n"
                          "You can also copy text from other applications and see it appear in the clipboard history.\n\n"
                          "Each clipboard entry has three action buttons:\n"
                          "• 'C' - Copy the entry back to clipboard\n"
                          "• 'S' - Save the entry to a file\n"
                          "• 'X' - Delete the entry from history");
        
        // Set font and colors using new API
        TextInputStyle style = TextInputStyle::Default();
        style.fontFamily = "Consolas";
        style.fontSize = 12.0f;
        style.normalBackgroundColor = Colors::White;
        style.normalBorderColor = Colors::Gray;
        style.normalTextColor = Colors::Black;
        textInput->SetStyle(style);
        
        mainWindow->AddElement(textInput);
        
        // Create control buttons
        CreateControlButtons();
    }
    
    void CreateControlButtons() {
        if (!mainWindow) return;
        
        // Copy button
        copyButton = CreateButton("copyBtn", 2002, 20, 300, 150, 30, "Copy Text to Clipboard");
        copyButton->onClick = [this]() {
            if (textInput) {
                std::string text = textInput->GetText();
                if (!text.empty()) {
                    AddClipboardText(text);
                    std::cout << "Text copied to clipboard history" << std::endl;
                }
            }
        };
        mainWindow->AddElement(copyButton);
        
        // Show clipboard button
        showClipboardButton = CreateButton("showBtn", 2003, 200, 300, 180, 30, "Show Clipboard (ALT+P)");
        showClipboardButton->onClick = [this]() {
            ShowClipboard();
        };
        mainWindow->AddElement(showClipboardButton);
        
        // Clear history button
        clearHistoryButton = CreateButton("clearBtn", 2004, 400, 300, 150, 30, "Clear History");
        clearHistoryButton->onClick = [this]() {
            ClearClipboardHistory();
            std::cout << "Clipboard history cleared" << std::endl;
        };
        mainWindow->AddElement(clearHistoryButton);
        
        // Add sample data button
        addSampleButton = CreateButton("sampleBtn", 2005, 570, 300, 150, 30, "Add Sample Data");
        addSampleButton->onClick = [this]() {
            AddSampleClipboardData();
        };
        mainWindow->AddElement(addSampleButton);
    }
    
    void SetupEventHandlers() {
        if (!mainWindow) return;
        
        // ✅ UPDATED: Use render override instead of callback
        // Custom rendering is now handled by overriding the Render method
        // For demo purposes, we'll use a simple render callback approach
        mainWindow->SetEventHandler([this](const UCEvent& event) -> bool {
            // Handle custom events if needed
            return false; // Let other handlers process the event
        });
    }
    
    void DrawCustomUI() {
        ULTRACANVAS_RENDER_SCOPE();
        
        // Draw title
        SetColor(Colors::Black);
        SetFont("Arial", 24.0f);
        DrawText("UltraCanvas Multi-Entry Clipboard Demo", Point2D(20, 40));
        
        // Draw status
        SetColor(Colors::Blue);
        SetFont("Arial", 12.0f);
        
        size_t entryCount = GetClipboardEntryCount();
        std::string statusText = "Clipboard entries: " + std::to_string(entryCount) + " / 100";
        DrawText(statusText, Point2D(20, 350));
        
        // Draw instructions
        SetColor(Colors::Gray);
        SetFont("Arial", 11.0f);
        
        std::vector<std::string> instructions = {
            "Instructions:",
            "• Type text in the text area and click 'Copy to Clipboard'",
            "• Press ALT+P to open/close the clipboard history window",
            "• Copy text from other applications to add to history automatically",
            "• In the clipboard window:",
            "  - Click 'C' to copy an entry back to clipboard",
            "  - Click 'S' to save an entry to a file (Downloads folder)",
            "  - Click 'X' to delete an entry from history",
            "• The clipboard remembers the last 100 entries",
            "• Supports 50+ formats: text, images, vectors, animations, videos, 3D models, documents",
            "• Save feature works for all supported formats with appropriate file extensions"
        };
        
        float yPos = 380;
        for (const std::string& instruction : instructions) {
            DrawText(instruction, Point2D(20, yPos));
            yPos += 18;
        }
    }
    
    void AddSampleClipboardData() {
        // Add some sample clipboard entries for testing
        std::vector<std::string> sampleTexts = {
            "Sample clipboard entry #1",
            "Here's a longer piece of text that demonstrates how the clipboard handles multi-line content and longer strings that might need to be truncated in the preview.",
            "Short text",
            "function calculateArea(radius) {\n    return Math.PI * radius * radius;\n}",
            "Email: user@example.com\nPhone: +1-555-0123\nAddress: 123 Main St, City, State 12345",
            "TODO:\n- Implement image support\n- Add search functionality\n- Improve UI styling\n- Test save functionality",
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit.",
            "JSON Data: {\"name\": \"test\", \"value\": 42, \"active\": true}",
            "CSV Data: Name,Age,City\nJohn,25,New York\nJane,30,London"
        };
        
        for (const std::string& text : sampleTexts) {
            AddClipboardText(text);
        }
        
        std::cout << "Added " << sampleTexts.size() << " sample clipboard entries" << std::endl;
        std::cout << "Try pressing ALT+P to see the clipboard window" << std::endl;
        std::cout << "Each entry will have 'C' (copy), 'S' (save), and 'X' (delete) buttons" << std::endl;
    }
    
    void Run() {
        std::cout << "Clipboard Demo App starting..." << std::endl;
        std::cout << "Press ALT+P to open the clipboard window" << std::endl;
        
        if (mainWindow) {
            mainWindow->Show();
        }
        
        // ✅ UPDATED: Use application run loop
        if (application) {
            application->Run();
        }
        
        std::cout << "Application shutting down..." << std::endl;
    }
    
    // Custom window class for rendering
    class ClipboardDemoWindow : public UltraCanvasWindow {
    private:
        ClipboardDemoApp* app;
        
    public:
        ClipboardDemoWindow(ClipboardDemoApp* demoApp) : app(demoApp) {}
        
        void Render(IRenderContext* ctx) override {
            UltraCanvasWindow::Render(IRenderContext* ctx); // Render base window and elements
            if (app) {
                app->DrawCustomUI(); // Draw custom overlay
            }
        }
    };
};

// ===== MAIN FUNCTION =====
int main() {
    try {
        // ✅ UPDATED: Initialize keyboard manager first
        if (!UltraCanvasKeyboardManager::Initialize()) {
            std::cerr << "Failed to initialize keyboard manager" << std::endl;
            return -1;
        }
        
        // Initialize clipboard manager
        InitializeClipboardManager();
        
        // Create and run the demo application
        ClipboardDemoApp app;
        app.Run();
        
        // Cleanup
        ShutdownClipboardManager();
        UltraCanvasKeyboardManager::Shutdown();
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Application error: " << e.what() << std::endl;
        return -1;
    } catch (...) {
        std::cerr << "Unknown application error" << std::endl;
        return -1;
    }
}