// Apps/TabbedContainerDemo.cpp
// Demo application showcasing UltraCanvasTabbedContainer with dropdown search functionality
// Version: 1.0.0
// Last Modified: 2025-09-14
// Author: UltraCanvas Framework

#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasContainer.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>

using namespace UltraCanvas;

// ===== DEMO CONTENT PANELS =====

class DemoTextPanel : public UltraCanvasContainer {
private:
    std::shared_ptr<UltraCanvasLabel> titleLabel;
    std::shared_ptr<UltraCanvasLabel> contentLabel;
    std::shared_ptr<UltraCanvasTextInput> textInput;

public:
    DemoTextPanel(const std::string& title, const std::string& content)
            : UltraCanvasContainer("text_panel", 0, 0, 0, 400, 300) {

        // Title label
        titleLabel = std::make_shared<UltraCanvasLabel>("title", 0, 10, 10, 380, 30);
        titleLabel->SetText(title);
        titleLabel->SetTextColor(Color(0, 100, 200));
        titleLabel->SetFontSize(16);
        AddChild(titleLabel);

        // Content description
        contentLabel = std::make_shared<UltraCanvasLabel>("content", 0, 10, 50, 380, 60);
        contentLabel->SetText(content);
        contentLabel->SetTextColor(Colors::Black);
        contentLabel->SetWordWrap(true);
        AddChild(contentLabel);

        // Interactive text area
        textInput = std::make_shared<UltraCanvasTextInput>("input", 0, 10, 120, 380, 160);
        textInput->SetText("Type here to test the tab content...\n\nThis demonstrates how each tab can contain different interactive elements.");
        textInput->SetInputType(TextInputType::Multiline);
        AddChild(textInput);
    }
};

class DemoButtonPanel : public UltraCanvasContainer {
private:
    std::vector<std::shared_ptr<UltraCanvasButton>> buttons;
    std::shared_ptr<UltraCanvasLabel> statusLabel;
    int buttonClickCount = 0;

public:
    DemoButtonPanel(const std::string& panelName)
            : UltraCanvasContainer("button_panel", 0, 0, 0, 400, 300) {

        // Create title
        auto titleLabel = std::make_shared<UltraCanvasLabel>("title", 0, 10, 10, 380, 30);
        titleLabel->SetText("Interactive Button Panel: " + panelName);
        titleLabel->SetTextColor(Color(0, 150, 0));
        titleLabel->SetFontSize(16);
        AddChild(titleLabel);

        // Create grid of buttons
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 4; col++) {
                int x = 10 + col * 90;
                int y = 50 + row * 40;

                auto button = std::make_shared<UltraCanvasButton>(
                        "btn_" + std::to_string(row * 4 + col), 0, x, y, 80, 30
                );

                std::ostringstream buttonText;
                buttonText << "Btn " << (row * 4 + col + 1);
                button->SetText(buttonText.str());

                // Set up click handler
                button->onClick = [this, row, col]() {
                    buttonClickCount++;
                    std::ostringstream status;
                    status << "Button (" << row << "," << col << ") clicked! Total clicks: " << buttonClickCount;
                    statusLabel->SetText(status.str());
                };

                buttons.push_back(button);
                AddChild(button);
            }
        }

        // Status label
        statusLabel = std::make_shared<UltraCanvasLabel>("status", 0, 10, 180, 380, 60);
        statusLabel->SetText("Click any button to see interaction feedback...");
        statusLabel->SetTextColor(Color(100, 100, 100));
        statusLabel->SetWordWrap(true);
        AddChild(statusLabel);
    }
};

class DemoInfoPanel : public UltraCanvasContainer {
private:
    std::shared_ptr<UltraCanvasLabel> infoLabel;

public:
    DemoInfoPanel(const std::string& title, const std::string& info, const Color& accentColor)
            : UltraCanvasContainer("info_panel", 0, 0, 0, 400, 300) {

        // Title
        auto titleLabel = std::make_shared<UltraCanvasLabel>("title", 0, 10, 10, 380, 30);
        titleLabel->SetText(title);
        titleLabel->SetTextColor(accentColor);
        titleLabel->SetFontSize(16);
        AddChild(titleLabel);

        // Info content
        infoLabel = std::make_shared<UltraCanvasLabel>("info", 0, 10, 50, 380, 240);
        infoLabel->SetText(info);
        infoLabel->SetTextColor(Colors::Black);
        infoLabel->SetWordWrap(true);
        AddChild(infoLabel);
    }
};

// ===== MAIN DEMO APPLICATION =====

class TabbedContainerDemoWindow : public UltraCanvasWindow {
private:
    std::shared_ptr<UltraCanvasTabbedContainer> mainTabbedContainer;
    std::shared_ptr<UltraCanvasTabbedContainer> subTabbedContainer;
    std::shared_ptr<UltraCanvasButton> addTabButton;
    std::shared_ptr<UltraCanvasButton> removeTabButton;
    std::shared_ptr<UltraCanvasButton> toggleDropdownButton;
    std::shared_ptr<UltraCanvasButton> toggleSearchButton;
    std::shared_ptr<UltraCanvasLabel> statusLabel;

    int tabCounter = 0;

public:
    TabbedContainerDemoWindow() = default;

    bool Initialize() {
        CreateMainTabbedContainer();
        CreateSubTabbedContainer();
        CreateControlButtons();
        CreateStatusLabel();
        PopulateWithDemoTabs();
        SetupEventHandlers();
        return true;
    }

private:
    void CreateMainTabbedContainer() {
        // Create main tabbed container with dropdown search enabled
        mainTabbedContainer = CreateTabbedContainerWithDropdown(
                "main_tabs", 1001, 10, 10, 980, 500,
                OverflowDropdownPosition::Left,  // Dropdown on left
                false,                            // Enable search
                5                                // Show search when >5 tabs
        );

        // Configure appearance
        mainTabbedContainer->SetTabPosition(TabPosition::Top);
        mainTabbedContainer->SetTabStyle(TabStyle::Modern);
        mainTabbedContainer->SetCloseMode(TabCloseMode::Closable);
        mainTabbedContainer->allowTabReordering = true;

        // Set colors for better visual distinction
        mainTabbedContainer->tabBarColor = Color(240, 248, 255);
        mainTabbedContainer->activeTabColor = Colors::White;
        mainTabbedContainer->inactiveTabColor = Color(230, 238, 245);
        mainTabbedContainer->hoveredTabColor = Color(250, 250, 255);

        AddChild(mainTabbedContainer);
    }

    void CreateSubTabbedContainer() {
        // Create a nested tabbed container for one of the main tabs
        subTabbedContainer = CreateTabbedContainerWithDropdown(
                "sub_tabs", 1002, 0, 0, 400, 300,
                OverflowDropdownPosition::Right,  // Dropdown on right
                false,                             // Enable search
                8                                 // Show search when >8 tabs
        );

        // Configure as vertical tabs
        subTabbedContainer->SetTabPosition(TabPosition::Left);
        subTabbedContainer->SetTabStyle(TabStyle::Flat);
        subTabbedContainer->SetCloseMode(TabCloseMode::ClosableExceptFirst);

        // Different color scheme
        subTabbedContainer->tabBarColor = Color(248, 255, 248);
        subTabbedContainer->activeTabColor = Color(255, 255, 255);
        subTabbedContainer->inactiveTabColor = Color(238, 245, 238);
    }

    void CreateControlButtons() {
        // Add tab button
        addTabButton = std::make_shared<UltraCanvasButton>("add_tab", 2001, 10, 520, 100, 30);
        addTabButton->SetText("Add Tab");
        AddChild(addTabButton);

        // Remove tab button
        removeTabButton = std::make_shared<UltraCanvasButton>("remove_tab", 2002, 120, 520, 100, 30);
        removeTabButton->SetText("Remove Tab");
        AddChild(removeTabButton);

        // Toggle dropdown position
        toggleDropdownButton = std::make_shared<UltraCanvasButton>("toggle_dropdown", 2003, 230, 520, 120, 30);
        toggleDropdownButton->SetText("Toggle Dropdown");
        AddChild(toggleDropdownButton);

        // Toggle search
        toggleSearchButton = std::make_shared<UltraCanvasButton>("toggle_search", 2004, 360, 520, 120, 30);
        toggleSearchButton->SetText("Toggle Search");
        AddChild(toggleSearchButton);
    }

    void CreateStatusLabel() {
        statusLabel = std::make_shared<UltraCanvasLabel>("status", 0, 500, 520, 480, 30);
        statusLabel->SetText("Demo loaded. Try adding tabs, using dropdown search, and tab reordering!");
        statusLabel->SetTextColor(Color(0, 100, 0));
        AddChild(statusLabel);
    }

    void PopulateWithDemoTabs() {
        // Tab 1: Welcome/Instructions
        auto welcomePanel = std::make_shared<DemoTextPanel>(
                "Welcome to UltraCanvas Tabbed Container Demo!",
                "This demo showcases the enhanced tabbed container with dropdown search functionality. "
                "Key features: automatic dropdown when tabs overflow, real-time search, tab reordering, "
                "and configurable positioning."
        );
        mainTabbedContainer->AddTab("🏠 Welcome", welcomePanel);

        // Tab 2: Feature Overview
        auto featuresInfo =
                "ENHANCED FEATURES:\n\n"
                "• Overflow Dropdown: Automatically appears when tabs don't fit\n"
                "• Smart Search: Real-time filtering with 🔍 icon (threshold: >5 tabs)\n"
                "• Position Control: Left/Right dropdown positioning\n"
                "• Visual Indicators: Active (●), Disabled ([]) tab markers\n"
                "• Keyboard Support: Escape, Enter, Backspace, Arrow keys\n"
                "• Tab Management: Add, remove, reorder, enable/disable\n"
                "• Multiple Layouts: Top, Bottom, Left, Right tab positions\n"
                "• Style Options: Classic, Modern, Flat, Rounded themes\n"
                "• Close Buttons: Configurable (None, All, Except First)\n"
                "• Event Callbacks: onChange, onSelect, onClose, onReorder";

        auto featuresPanel = std::make_shared<DemoInfoPanel>(
                "Enhanced Features Overview", featuresInfo, Color(200, 100, 0)
        );
        mainTabbedContainer->AddTab("⚡ Features", featuresPanel);

        // Tab 3: Nested Tabs Demo
        PopulateSubTabbedContainer();
        auto nestedContainer = std::make_shared<UltraCanvasContainer>("nested", 0, 0, 0, 400, 300);
        nestedContainer->AddChild(subTabbedContainer);
        mainTabbedContainer->AddTab("📁 Nested Tabs", nestedContainer);

        // Tab 4-8: Interactive Demo Panels
        std::vector<std::string> demoTabNames = {
                "🎮 Interactive Demo", "📊 Dashboard", "⚙️ Settings", "📈 Analytics", "💾 Data Manager"
        };

        for (int i = 0; i < demoTabNames.size(); i++) {
            auto panel = std::make_shared<DemoButtonPanel>("Panel " + std::to_string(i + 1));
            mainTabbedContainer->AddTab(demoTabNames[i], panel);
        }

        // Tab 9-15: Additional tabs to demonstrate search functionality
        for (int i = 9; i <= 15; i++) {
            std::ostringstream tabName, tabInfo;
            tabName << "📄 Document " << i;
            tabInfo << "This is document " << i << " content.\n\n"
                    << "Search functionality demonstration:\n"
                    << "• Type 'doc' to find all document tabs\n"
                    << "• Type numbers to find specific documents\n"
                    << "• Search is case-insensitive\n"
                    << "• Real-time filtering updates dropdown list\n\n"
                    << "Tab features:\n"
                    << "• Unique tab ID: " << i << "\n"
                    << "• Search keywords: document, doc, " << i << "\n"
                    << "• Content type: Information panel";

            auto infoPanel = std::make_shared<DemoInfoPanel>(
                    "Document " + std::to_string(i), tabInfo.str(), Color(100, 0, 200)
            );
            mainTabbedContainer->AddTab(tabName.str(), infoPanel);
        }

        // Set initial active tab
        mainTabbedContainer->SetActiveTab(0);
    }

    void PopulateSubTabbedContainer() {
        // Add some nested tabs to demonstrate hierarchical organization
        std::vector<std::pair<std::string, std::string>> subTabs = {
                {"Home", "Nested tab home page with navigation options."},
                {"Config", "Configuration settings for nested functionality."},
                {"Data", "Data management and processing tools."},
                {"Reports", "Reporting and analytics dashboard."},
                {"Tools", "Utility tools and helper functions."},
                {"Help", "Help documentation and support resources."}
        };

        for (const auto& [name, desc] : subTabs) {
            auto panel = std::make_shared<DemoInfoPanel>(
                    "Nested: " + name,
                    desc + "\n\nThis demonstrates nested tabbed containers with different positioning and search thresholds.",
                    Color(0, 150, 100)
            );
            subTabbedContainer->AddTab(name, panel);
        }

        subTabbedContainer->SetActiveTab(0);
    }

    void SetupEventHandlers() {
        // Main tabbed container callbacks
        mainTabbedContainer->onTabChange = [this](int oldIndex, int newIndex) {
            std::ostringstream status;
            status << "Tab changed: " << oldIndex << " → " << newIndex
                   << " (\"" << mainTabbedContainer->GetTabTitle(newIndex) << "\")";
            statusLabel->SetText(status.str());
            statusLabel->SetTextColor(Color(0, 100, 0));
        };

        mainTabbedContainer->onTabCloseRequest = [this](int index) {
            std::ostringstream status;
            status << "Tab \"" << mainTabbedContainer->GetTabTitle(index) << "\" closed (index " << index << ")";
            statusLabel->SetText(status.str());
            statusLabel->SetTextColor(Color(150, 100, 0));
        };

        mainTabbedContainer->onTabReorder = [this](int fromIndex, int toIndex) {
            std::ostringstream status;
            status << "Tab reordered: " << fromIndex << " → " << toIndex;
            statusLabel->SetText(status.str());
            statusLabel->SetTextColor(Color(100, 0, 150));
        };

        // Control button handlers
        addTabButton->onClick = [this]() {
            tabCounter++;
            std::ostringstream tabName;
            tabName << "🆕 Dynamic " << tabCounter;

            std::ostringstream tabContent;
            tabContent << "This is dynamically added tab #" << tabCounter << ".\n\n"
                       << "Features demonstrated:\n"
                       << "• Runtime tab creation\n"
                       << "• Automatic dropdown updates\n"
                       << "• Search integration\n"
                       << "• Layout recalculation\n\n"
                       << "Try adding more tabs to see the search functionality activate!";

            auto panel = std::make_shared<DemoInfoPanel>(
                    "Dynamic Tab " + std::to_string(tabCounter),
                    tabContent.str(),
                    Color(200, 0, 100)
            );

            int newTabIndex = mainTabbedContainer->AddTab(tabName.str(), panel);
            mainTabbedContainer->SetActiveTab(newTabIndex);

            std::ostringstream status;
            status << "Added new tab: \"" << tabName.str() << "\" (total: " << mainTabbedContainer->GetTabCount() << ")";
            statusLabel->SetText(status.str());
            statusLabel->SetTextColor(Color(0, 150, 0));
        };

        removeTabButton->onClick = [this]() {
            int activeTab = mainTabbedContainer->GetActiveTab();
            if (activeTab >= 0 && mainTabbedContainer->GetTabCount() > 1) {
                std::string tabTitle = mainTabbedContainer->GetTabTitle(activeTab);
                mainTabbedContainer->RemoveTab(activeTab);

                std::ostringstream status;
                status << "Removed tab: \"" << tabTitle << "\" (remaining: " << mainTabbedContainer->GetTabCount() << ")";
                statusLabel->SetText(status.str());
                statusLabel->SetTextColor(Color(150, 0, 0));
            } else {
                statusLabel->SetText("Cannot remove: need at least one tab!");
                statusLabel->SetTextColor(Color(200, 0, 0));
            }
        };

        toggleDropdownButton->onClick = [this]() {
            auto currentPos = mainTabbedContainer->GetOverflowDropdownPosition();
            OverflowDropdownPosition newPos;
            std::string posName;

            switch (currentPos) {
                case OverflowDropdownPosition::Off:
                    newPos = OverflowDropdownPosition::Left;
                    posName = "Left";
                    break;
                case OverflowDropdownPosition::Left:
                    newPos = OverflowDropdownPosition::Right;
                    posName = "Right";
                    break;
                case OverflowDropdownPosition::Right:
                    newPos = OverflowDropdownPosition::Off;
                    posName = "Off";
                    break;
            }

            mainTabbedContainer->SetOverflowDropdownPosition(newPos);

            std::ostringstream status;
            status << "Dropdown position changed to: " << posName;
            statusLabel->SetText(status.str());
            statusLabel->SetTextColor(Color(0, 100, 150));
        };

        toggleSearchButton->onClick = [this]() {
            bool currentSearch = mainTabbedContainer->IsDropdownSearchEnabled();
            mainTabbedContainer->SetDropdownSearchEnabled(!currentSearch);

            std::ostringstream status;
            status << "Dropdown search " << (currentSearch ? "disabled" : "enabled");
            statusLabel->SetText(status.str());
            statusLabel->SetTextColor(Color(150, 0, 100));
        };
    }

    // Override OnEvent to handle application-level events
    bool OnEvent(const UCEvent& event) override {
        // Handle escape key to exit
        if (event.type == UCEventType::KeyDown && event.virtualKey == UCKeys::Escape) {
            auto app = UltraCanvasApplication::GetInstance();
            if (app) {
                app->Exit();
            }
            return true;
        }

        // Forward to base class
        return UltraCanvasWindow::OnEvent(event);
    }
};

// ===== MAIN APPLICATION CLASS =====

class TabbedContainerDemoApp : public UltraCanvasApplication {
private:
    std::shared_ptr<TabbedContainerDemoWindow> demoWindow;

public:
    bool Initialize() override {
        std::cout << "Initializing UltraCanvas Tabbed Container Demo..." << std::endl;

        // Initialize base application
        if (!UltraCanvasApplication::Initialize()) {
            std::cerr << "Failed to initialize UltraCanvas application" << std::endl;
            return false;
        }

        // Create demo window
        demoWindow = std::make_shared<TabbedContainerDemoWindow>();
        if (!demoWindow) {
            std::cerr << "Failed to create demo window" << std::endl;
            return false;
        }

        // Configure window
        WindowConfig config;
        config.title = "UltraCanvas Enhanced Tabbed Container Demo";
        config.width = 1000;
        config.height = 600;
        config.resizable = true;
        config.backgroundColor = Color(250, 250, 250);

        if (!demoWindow->Create(config)) {
            std::cerr << "Failed to create demo window with config" << std::endl;
            return false;
        }

        // Initialize demo content
        if (!demoWindow->Initialize()) {
            std::cerr << "Failed to initialize demo window content" << std::endl;
            return false;
        }

        // Show window
        demoWindow->Show();

        std::cout << "Demo initialized successfully!" << std::endl;
        PrintUsageInstructions();

        return true;
    }

private:
    void PrintUsageInstructions() {
        std::cout << "\n=== UltraCanvas Tabbed Container Demo ===" << std::endl;
        std::cout << "\nFEATURES TO TEST:" << std::endl;
        std::cout << "• Overflow Dropdown: Add tabs until dropdown appears" << std::endl;
        std::cout << "• Search Functionality: Type in dropdown to filter tabs" << std::endl;
        std::cout << "• Tab Reordering: Drag tabs to reorder (if enabled)" << std::endl;
        std::cout << "• Close Buttons: Click × to close tabs" << std::endl;
        std::cout << "• Nested Tabs: Check the 'Nested Tabs' tab" << std::endl;
        std::cout << "\nCONTROLS:" << std::endl;
        std::cout << "• Add Tab: Creates new dynamic tab" << std::endl;
        std::cout << "• Remove Tab: Removes active tab" << std::endl;
        std::cout << "• Toggle Dropdown: Cycles dropdown position (Left/Right/Off)" << std::endl;
        std::cout << "• Toggle Search: Enables/disables search functionality" << std::endl;
        std::cout << "\nKEYBOARD:" << std::endl;
        std::cout << "• Arrow Keys: Navigate between tabs" << std::endl;
        std::cout << "• Ctrl+W: Close active tab (if closable)" << std::endl;
        std::cout << "• Escape: Exit application" << std::endl;
        std::cout << "\nSEARCH DEMO:" << std::endl;
        std::cout << "• Type 'doc' to find document tabs" << std::endl;
        std::cout << "• Type numbers to find specific tabs" << std::endl;
        std::cout << "• Search appears when >5 tabs (configurable)" << std::endl;
        std::cout << "=======================================" << std::endl;
    }
};

// ===== MAIN FUNCTION =====

int main(int argc, char* argv[]) {
    std::cout << "Starting UltraCanvas Enhanced Tabbed Container Demo..." << std::endl;

    try {
        // Create and initialize demo application
        auto app = std::make_shared<TabbedContainerDemoApp>();

        if (!app->Initialize()) {
            std::cerr << "Failed to initialize demo application!" << std::endl;
            return -1;
        }

        std::cout << "Demo application initialized. Starting main loop..." << std::endl;

        // Run the application main loop
        app->Run();

        std::cout << "Demo application finished successfully." << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Exception in main: " << e.what() << std::endl;
        return -1;
    } catch (...) {
        std::cerr << "Unknown exception in main!" << std::endl;
        return -1;
    }
}
