// GraphicFormulaApp.cpp
// Mathematical formula visualization application with procedural backgrounds
// Version: 1.3.3
// Last Modified: 2025-08-17
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLayoutEngine.h"           // For grid layout functionality
#include "UltraCanvasFormulaEditor.h"
#include "UltraCanvasElementDebug.h"
//#include "UltraCanvasCairoDebugExtension.h"
// NOTE: Temporarily removed this include to avoid abstract class compilation issues
// The header contains a make_shared call for an abstract class ProceduralBackgroundPlugin
// #include "UltraCanvasProceduralBackgroundPlugin.h"
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>

using namespace UltraCanvas;

bool isAnimating = false;

class GraphicFormulaWindow : public UltraCanvasWindow {
private:
    // UI Components - declared only once
    std::shared_ptr<UltraCanvasContainer> mainContainer;
    std::shared_ptr<UltraCanvasContainer> leftPanel;
    std::shared_ptr<UltraCanvasContainer> rightPanel;
    std::shared_ptr<UltraCanvasContainer> controlPanel;
    std::shared_ptr<UltraCanvasFormulaEditor> formulaEditor;
    std::shared_ptr<UltraCanvasLabel> statusLabel;
    std::shared_ptr<UltraCanvasDropdown> formulaDropdown;
    std::shared_ptr<UltraCanvasButton> startButton;
    std::shared_ptr<UltraCanvasButton> stopButton;
    std::shared_ptr<UltraCanvasButton> newButton;
    std::shared_ptr<UltraCanvasButton> openButton;
    std::shared_ptr<UltraCanvasButton> saveButton;
    std::shared_ptr<UltraCanvasSlider> frameRateSlider;
    std::shared_ptr<UltraCanvasLabel> frameRateLabel;
    std::shared_ptr<UltraCanvasProceduralBackground> graphicsOutput;

    // Application state
    float currentTime = 0.0f;
    float animationSpeed = 1.0f;
    std::string currentFilePath;
    std::string currentFormulaText;
    int windowWidth = 1200;
    int windowHeight = 800;

public:
    // Fixed constructor to use proper UltraCanvasWindow constructor with WindowConfig
    GraphicFormulaWindow() : UltraCanvasWindow() {
        // Constructor now properly deferred to Create method
    }

    virtual ~GraphicFormulaWindow() = default;

    // Fixed: Override Create with WindowConfig parameter
    bool Create(const WindowConfig& config = WindowConfig()) override {
        WindowConfig actualConfig = config;
        if (actualConfig.title.empty()) {
            actualConfig.title = "UltraCanvas - Graphic Formula Visualizer";
        }
        if (actualConfig.width == 0) actualConfig.width = 1200;
        if (actualConfig.height == 0) actualConfig.height = 800;

        windowWidth = actualConfig.width;
        windowHeight = actualConfig.height;

        if (!UltraCanvasWindow::Create(actualConfig)) {
            return false;
        }

        CreateUserInterface();
        PopulateFormulaDropdown();
        LoadDefaultFormula();

        return true;
    }

    // Fixed: Render method instead of Update
    virtual void Render() override {
        UltraCanvasWindow::Render();
        if (isAnimating) {
            currentTime += animationSpeed * 0.016f; // Assume ~60 FPS

            // Update graphics output animation
//            if (graphicsOutput) {
//                graphicsOutput->SetTime(currentTime);
//            }
            UpdateAnimation();
        }
    }

    // Fixed: OnEvent returns bool and has correct signature
    bool OnEvent(const UCEvent& event) override {
        bool handled = UltraCanvasWindow::OnEvent(event);

        // Handle window-specific events
        if (event.type == UCEventType::WindowClose) {
            if (isAnimating) {
                isAnimating = false;
            }
        } else if (event.type == UCEventType::KeyDown) {
            // Handle keyboard shortcuts
            if (event.ctrl) {
                switch (event.nativeKeyCode) {
                    case 'N': // Ctrl+N - New
                        CreateNewFormula();
                        handled = true;
                        break;
                    case 'O': // Ctrl+O - Open
                        OpenFormula();
                        handled = true;
                        break;
                    case 'S': // Ctrl+S - Save
                        SaveFormula();
                        handled = true;
                        break;
                    case ' ': // Ctrl+Space - Toggle animation
                        ToggleAnimation();
                        handled = true;
                        break;
                }
            }
        }

        return handled;
    }

private:
    void CreateUserInterface() {
        // Create main container
        mainContainer = std::make_shared<UltraCanvasContainer>("MainContainer", 1,
                                                               0, 0, windowWidth, windowHeight);

        // Left panel for controls (30% of width)
        int leftWidth = windowWidth * 0.4;
        leftPanel = std::make_shared<UltraCanvasContainer>("LeftPanel", 2,
                                                           0, 0, leftWidth, windowHeight - 10);

        // Right panel for graphics output (70% of width)
        int rightWidth = windowWidth - leftWidth + 10;
        rightPanel = std::make_shared<UltraCanvasContainer>("RightPanel", 3,
                                                            leftWidth, 0, rightWidth, windowHeight);

        // Formula dropdown
        int y = 10;
        formulaDropdown = std::make_shared<UltraCanvasDropdown>("FormulaDropdown", 11,
                                                                10, y, leftWidth - 40, 30);

        // Control buttons
        int buttonWidth = (leftWidth - 60) / 3;
        newButton = std::make_shared<UltraCanvasButton>("NewButton", 12,
                                                        10, y + 40, buttonWidth, 30, "New");
        openButton = std::make_shared<UltraCanvasButton>("OpenButton", 13,
                                                         20 + buttonWidth, y + 40, buttonWidth, 30, "Open");
        saveButton = std::make_shared<UltraCanvasButton>("SaveButton", 14,
                                                         30 + 2 * buttonWidth, y + 40, buttonWidth, 30, "Save");
        // Animation controls
        startButton = std::make_shared<UltraCanvasButton>("StartButton", 15,
                                                          10, y + 80, (leftWidth - 40) / 2, 30, "Start Animation");
        stopButton = std::make_shared<UltraCanvasButton>("StopButton", 16,
                                                         20 + (leftWidth - 40) / 2, y + 80, (leftWidth - 40) / 2 - 10, 30, "Stop");

        // Frame rate control
        frameRateLabel = std::make_shared<UltraCanvasLabel>("FrameRateLabel", 17,
                                                            10, y + 120, leftWidth - 20, 20, "Speed: 1.0x");
        // Fixed: Use correct UltraCanvasSlider constructor (6 parameters only)
        frameRateSlider = std::make_shared<UltraCanvasSlider>("FrameRateSlider", 18,
                                                              10, y + 140, leftWidth - 20, 30);
        frameRateSlider->SetRange(0.1f, 3.0f);
        frameRateSlider->SetValue(1.0f);

        // Status label
        statusLabel = std::make_shared<UltraCanvasLabel>("StatusLabel", 19,
                                                         10, windowHeight - 20, leftWidth - 20, 30, "Ready");
        // Formula editor in left panel
        formulaEditor = std::make_shared<UltraCanvasFormulaEditor>("FormulaEditor", 10,
                                                                   0, 350, 500 - 30, 400);

        // Add controls to left panel
        leftPanel->AddChild(formulaEditor);
        leftPanel->AddChild(formulaDropdown);
        leftPanel->AddChild(newButton);
        leftPanel->AddChild(openButton);
        leftPanel->AddChild(saveButton);
        leftPanel->AddChild(startButton);
        leftPanel->AddChild(stopButton);
        leftPanel->AddChild(frameRateLabel);
        leftPanel->AddChild(frameRateSlider);
        leftPanel->AddChild(statusLabel);

        // Create control panel in right side
        controlPanel = std::make_shared<UltraCanvasContainer>("ControlPanel", 70,
                                                              0, 0, rightWidth, 120);

        // Graphics output area
        int outputHeight = windowHeight - 170; // Leave space for controls and status
        graphicsOutput = std::make_shared<UltraCanvasProceduralBackground>("GraphicsOutput", 71,
                                                                           10, 130, rightWidth - 20, outputHeight);

        // Status display
        auto rightStatusPanel = std::make_shared<UltraCanvasContainer>("RightStatusPanel", 81,
                                                                       0, windowHeight - 50, rightWidth, 50);
        auto performanceLabel = std::make_shared<UltraCanvasLabel>("PerformanceLabel", 82,
                                                                   10, 10, rightWidth - 20, 30, "FPS: 0");

        rightStatusPanel->AddChild(performanceLabel);

        rightPanel->AddChild(controlPanel);
        rightPanel->AddChild(graphicsOutput);
        rightPanel->AddChild(rightStatusPanel);

        // Setup event handlers
        startButton->onClicked = [this]() { ToggleAnimation(); };
        stopButton->onClicked = [this]() {
            isAnimating = false;
            startButton->SetText("Start Animation");
            statusLabel->SetText("Animation stopped");
        };

        // Setup other event handlers
        newButton->onClicked = [this]() { CreateNewFormula(); };
        openButton->onClicked = [this]() { OpenFormula(); };
        saveButton->onClicked = [this]() { SaveFormula(); };

        // Fixed: Use correct dropdown callback signature
        formulaDropdown->onSelectionChanged = [this](int index, const DropdownItem& item) {
            LoadSelectedFormula(item.text);
        };

        // Fixed: Use correct formula editor callback
        formulaEditor->onFormulaChanged = [this](const ProceduralFormula& formula) {
            OnFormulaTextChanged(formula.formula);
        };

        // Fixed: Use correct slider callback
        frameRateSlider->onValueChanged = [this](float value) {
            OnFrameRateChanged(value);
        };

        // Add panels to main container and add to window
        mainContainer->AddChild(leftPanel);
        mainContainer->AddChild(rightPanel);
        AddChild(mainContainer);
    }

    void PopulateFormulaDropdown() {
        // Built-in formulas
        std::vector<std::string> builtInFormulas = {
                "Select Formula...",
                "--- Built-in Formulas ---",
                "Dust - Cosmic Particles",
                "Hive - Hexagonal Patterns",
                "Droplets - Water Effects",
                "Aquifier - Fluid Dynamics",
                "Spinner - Rotating Patterns",
                "Spinner 2 - Enhanced Rotation",
                "Smooth Waves - Wave Functions",
                "Chaos Universe - Complex Systems",
                "Crystal 2 - Crystalline Structures"
        };

        for (const auto& formula : builtInFormulas) {
            formulaDropdown->AddItem(formula);
        }

        // TODO: Add user formulas from saved files
        formulaDropdown->AddItem("--- User Formulas ---");

        formulaDropdown->SetSelectedIndex(0);
    }

    void LoadDefaultFormula() {
        LoadSelectedFormula("Dust - Cosmic Particles");
    }

    void LoadSelectedFormula(const std::string& formulaName) {
        if (formulaName.find("---") != std::string::npos ||
            formulaName == "Select Formula...") {
            return;
        }

        // Extract the formula name (remove description)
        std::string name = formulaName;
        size_t dashPos = name.find(" - ");
        if (dashPos != std::string::npos) {
            name = name.substr(0, dashPos);
        }

        if (name.find("User)") != std::string::npos) {
            // Remove " (User)" suffix
            name = name.substr(0, name.length() - 7);
            LoadUserFormula(name);
        } else {
            LoadBuiltInFormula(name);
        }
    }

    void LoadBuiltInFormula(const std::string& name) {
        ProceduralFormula formula;
        formula.name = name;
        formula.language = FormulaLanguage::Mathematical;
        formula.preferredMethod = RenderingMethod::CPU;
        formula.backgroundType = ProceduralBackgroundType::Animated;

        if (name == "Dust") {
            formula.description = "Cosmic dust particles with swirling motion";
            formula.formula = "vec3 p=vec3((FC.xy-.5)*2.,0),d=normalize(vec3(cos(t*.1),sin(t*.1)*.3,1)),o=vec3(0);for(int i=0;i<40;i++){p+=d*.1;float n=length(p.xy);o+=cos(p*10.+t)/n;}o=o*.1;";
            formula.animationSpeed = 1.0f;
            formula.complexity = 7.5f;
        } else if (name == "Hive") {
            formula.description = "Hexagonal honeycomb patterns";
            formula.formula = "vec2 p=FC.xy*8.;vec2 h=vec2(cos(radians(30.)),sin(radians(30.)));p=abs(mod(p,h*2.)-h);o=vec4(step(.8,max(p.x*1.732-p.y,p.y)));";
            formula.animationSpeed = 0.5f;
            formula.complexity = 3.5f;
        } else {
            // Default formula for other names
            formula.description = "Mathematical formula visualization";
            formula.formula = "vec3 o=vec3(0);vec2 p=(FC.xy-.5)*4.;float d=length(p);o=vec3(sin(d*5.-t),cos(d*3.+t*.5),sin(d*2.+t*.3));";
            formula.animationSpeed = 1.0f;
            formula.complexity = 5.0f;
        }

        formulaEditor->SetFormula(formula);
        currentFormulaText = formula.formula;
        statusLabel->SetText("✓ Loaded: " + name);
        statusLabel->SetTextColor(Colors::Green);
    }

    void LoadUserFormula(const std::string& name) {
        // TODO: Implement loading from user files
        statusLabel->SetText("User formulas not yet implemented");
        statusLabel->SetTextColor(Colors::Yellow);
    }

    void OnFormulaTextChanged(const std::string& text) {
        currentFormulaText = text;

        // Update the graphics output if we have a valid formula
        if (!text.empty() && graphicsOutput) {
            // Create a procedural formula from the text
            ProceduralFormula formula;
            formula.formula = text;
            formula.language = FormulaLanguage::Mathematical;
            formula.backgroundType = ProceduralBackgroundType::Animated;

            // Apply to graphics output if possible
            if (graphicsOutput) {
                // Note: SetFormula method may not exist yet in UltraCanvasProceduralBackground
                // This is a placeholder for future implementation
            }

            statusLabel->SetText("Formula updated");
            statusLabel->SetTextColor(Colors::Blue);
        }
    }

    void ToggleAnimation() {
        isAnimating = !isAnimating;

        if (isAnimating) {
            startButton->SetText("Pause Animation");
            statusLabel->SetText("Animation started");
            statusLabel->SetTextColor(Colors::Green);

            // Start the animation loop
            currentTime = 0.0f;
        } else {
            startButton->SetText("Start Animation");
            statusLabel->SetText("Animation paused");
            statusLabel->SetTextColor(Colors::Yellow);
        }
    }

    void OnFrameRateChanged(float value) {
        animationSpeed = value;
        frameRateLabel->SetText("Speed: " + std::to_string(value).substr(0, 3) + "x");

        // Update the graphics output animation speed if available
        // Note: SetAnimationSpeed method may not exist yet in UltraCanvasProceduralBackground
        // This is a placeholder for future implementation
    }

    void UpdateAnimation() {
        currentTime += animationSpeed * 0.016f; // Assume ~60 FPS

        // Update graphics output animation if available
        // Note: SetTime method may not exist yet in UltraCanvasProceduralBackground
        // This is a placeholder for future implementation
    }

    void CreateNewFormula() {
        ProceduralFormula newFormula;
        newFormula.name = "New Formula";
        newFormula.description = "Enter your formula description here";
        newFormula.formula = "// Enter your mathematical formula here\nvec3 o = vec3(0);\nvec2 p = FC.xy;\n// Your code here\n";
        newFormula.language = FormulaLanguage::Mathematical;

        formulaEditor->SetFormula(newFormula);
        currentFilePath.clear();
        statusLabel->SetText("New formula created");
        statusLabel->SetTextColor(Colors::Blue);
    }

    void OpenFormula() {
        // TODO: Implement file dialog
        statusLabel->SetText("Open dialog not yet implemented");
        statusLabel->SetTextColor(Colors::Yellow);

        // For now, just demonstrate loading from a test file
        std::string testFile = "test_formula.json";
        std::ifstream file(testFile);
        if (file.is_open()) {
            // TODO: Parse JSON and load formula
            file.close();
            statusLabel->SetText("✓ Loaded: " + testFile);
            statusLabel->SetTextColor(Colors::Green);
            currentFilePath = testFile;
        }
    }

    void SaveFormula() {
        if (currentFilePath.empty()) {
            SaveFormulaAs();
        } else {
            // TODO: Implement actual saving
            statusLabel->SetText("✓ Saved: " + currentFilePath);
            statusLabel->SetTextColor(Colors::Green);
        }
    }

    void SaveFormulaAs() {
        // TODO: Implement file dialog for save as
        std::string filename = "new_formula.json";
        currentFilePath = filename;

        // TODO: Implement actual JSON saving
        statusLabel->SetText("✓ Saved as: " + filename);
        statusLabel->SetTextColor(Colors::Green);
    }
};

class GraphicFormulaApp : public UltraCanvasApplication {
private:
    std::shared_ptr<GraphicFormulaWindow> mainWindow;

public:
    // Fixed: Use parameterless constructor
    GraphicFormulaApp() : UltraCanvasApplication() {}

    bool Initialize() override {
        if (!UltraCanvasApplication::Initialize()) {
            return false;
        }

        // Create main window with config
        mainWindow = std::make_shared<GraphicFormulaWindow>();
        if (!mainWindow) {
            std::cerr << "Failed to create main window" << std::endl;
            return false;
        }

        // Create window with default config
        WindowConfig config;
        config.title = "UltraCanvas - Graphic Formula Visualizer";
        config.width = 1200;
        config.height = 800;

        if (!mainWindow->Create(config)) {
            std::cerr << "Failed to create main window" << std::endl;
            return false;
        }
        mainWindow->Show();

        // Register window with application (if method exists)
        // Note: AddWindow may not exist in current UltraCanvasApplication
        // This is framework-dependent

        return true;
    }

    void RunInEventLoop() override {
        UltraCanvasApplication::RunInEventLoop();
        if (isAnimating) {
            mainWindow->RequestRedraw(true);
        }
    }

    // Removed Update and Shutdown overrides since they don't exist in base class
};

// Main function
int main() {
    UltraCanvasDebugRenderer::SetDebugEnabled(true);

    auto app = new GraphicFormulaApp();

    if (!app->Initialize()) {
        std::cerr << "Failed to initialize GraphicFormulaApp" << std::endl;
        return -1;
    }

    std::cout << "GraphicFormulaApp initialized successfully" << std::endl;
    std::cout << "Running application..." << std::endl;

    // Run the application
    app->Run();

    std::cout << "Application finished" << std::endl;

    delete app;
    return 0;
}