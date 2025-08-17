// GraphicFormulaApp.cpp
// Mathematical formula visualization application with procedural backgrounds
// Version: 1.3.0
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
// Note: Removed problematic includes that cause abstract class instantiation
// #include "UltraCanvasProceduralBackgroundPlugin.h"
// #include "UltraCanvasHybridFormulaSystem.h"
#include "UltraCanvasFormulaEditor.h"
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>

// Forward declarations for types we'll use but not instantiate
namespace UltraCanvas {
    class UltraCanvasProceduralBackground;
    enum class FormulaLanguage { Mathematical, GLSL, JavaScript, Custom };
    enum class RenderingMethod { CPU, GPU_OpenGL, GPU_Vulkan, Hybrid };
    enum class ProceduralBackgroundType { Static, Animated, Interactive, Realtime };

    struct ProceduralFormula {
        std::string name;
        std::string description;
        std::string author;
        std::vector<std::string> tags;
        std::string formula;
        FormulaLanguage language = FormulaLanguage::Mathematical;
        RenderingMethod preferredMethod = RenderingMethod::CPU;
        ProceduralBackgroundType backgroundType = ProceduralBackgroundType::Static;
        float animationSpeed = 1.0f;
        float complexity = 1.0f;
    };
}

using namespace UltraCanvas;

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
    std::shared_ptr<UltraCanvasProceduralBackground> graphicsOutput;
    std::shared_ptr<UltraCanvasButton> startButton;
    std::shared_ptr<UltraCanvasLabel> frameRateLabel;
    std::shared_ptr<UltraCanvasSlider> frameRateSlider;

    // Menu components
    std::shared_ptr<UltraCanvasButton> newButton;
    std::shared_ptr<UltraCanvasButton> openButton;
    std::shared_ptr<UltraCanvasButton> saveButton;
    std::shared_ptr<UltraCanvasButton> saveAsButton;
    std::shared_ptr<UltraCanvasButton> exportButton;

    // Animation state
    bool isAnimating = false;
    float animationSpeed = 1.0f;
    float currentTime = 0.0f;
    std::string currentFormulaText;
    std::string currentFilePath;

    // Window dimensions cache
    int windowWidth = 1200;
    int windowHeight = 800;

public:
    GraphicFormulaWindow() : UltraCanvasWindow() {
        // Constructor implementation moved to Create method
    }

    virtual ~GraphicFormulaWindow() {
        if (isAnimating) {
            isAnimating = false;
        }
    }

    // Fixed: Override Create with WindowConfig parameter
    bool Create(const WindowConfig& config = WindowConfig()) override {
        WindowConfig actualConfig = config;
        if (actualConfig.title.empty()) {
            actualConfig.title = "UltraCanvas Formula Graphics Generator";
        }
        if (actualConfig.width == 0) actualConfig.width = 1200;
        if (actualConfig.height == 0) actualConfig.height = 800;

        windowWidth = actualConfig.width;
        windowHeight = actualConfig.height;

        if (!UltraCanvasWindow::Create(actualConfig)) {
            return false;
        }

        CreateMainLayout();
        PopulateFormulaDropdown();
        LoadDefaultFormula();

        return true;
    }

    void Render() override {
        UltraCanvasWindow::Render();

        if (isAnimating) {
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
        } else if (event.type == UCEventType::KeyDown) {  // Fixed: Use KeyDown instead of KeyPress
            // Handle keyboard shortcuts
            if (event.ctrl) {  // Fixed: Use correct event structure
                switch (event.keyCode) {  // Fixed: Use correct event structure
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
    void CreateMainLayout() {
        // Create main horizontal container
        mainContainer = std::make_shared<UltraCanvasContainer>("MainContainer", 1, 0, 0, windowWidth, windowHeight);

        // Use LayoutEngine instead of GridLayout for better compatibility
        LayoutParams layoutParams = LayoutParams::Horizontal(10);
        AddElement(mainContainer);

        CreateLeftPanel();
        CreateRightPanel();

        mainContainer->AddChild(leftPanel);
        mainContainer->AddChild(rightPanel);

        // Apply layout using the layout engine
        std::vector<UltraCanvasElement*> panels = {leftPanel.get(), rightPanel.get()};
        UltraCanvasLayoutEngine::PerformLayout(windowWidth, windowHeight, layoutParams, panels);
    }

    void CreateLeftPanel() {
        // Left panel for formula editing (60% of width)
        int leftWidth = static_cast<int>(windowWidth * 0.6);
        leftPanel = std::make_shared<UltraCanvasContainer>("LeftPanel", 2, 0, 0, leftWidth, windowHeight);

        // Use LayoutEngine for vertical layout instead of GridLayout
        LayoutParams verticalLayout = LayoutParams::Vertical(5);

        // Menu bar
        auto menuPanel = std::make_shared<UltraCanvasContainer>("MenuPanel", 21, 0, 0, leftWidth, 40);
        LayoutParams menuLayout = LayoutParams::Horizontal(5);

        // Fixed: Use correct constructor parameters for buttons
        newButton = std::make_shared<UltraCanvasButton>("NewButton", 22, 5, 5, 80, 30, "New");
        openButton = std::make_shared<UltraCanvasButton>("OpenButton", 23, 90, 5, 80, 30, "Open");
        saveButton = std::make_shared<UltraCanvasButton>("SaveButton", 24, 175, 5, 80, 30, "Save");
        saveAsButton = std::make_shared<UltraCanvasButton>("SaveAsButton", 25, 260, 5, 80, 30, "Save As");
        exportButton = std::make_shared<UltraCanvasButton>("ExportButton", 26, 345, 5, 100, 30, "Export Video");

        menuPanel->AddChild(newButton);
        menuPanel->AddChild(openButton);
        menuPanel->AddChild(saveButton);
        menuPanel->AddChild(saveAsButton);
        menuPanel->AddChild(exportButton);

        // Formula dropdown
        auto dropdownPanel = std::make_shared<UltraCanvasContainer>("DropdownPanel", 31, 0, 40, leftWidth, 40);
        formulaDropdown = std::make_shared<UltraCanvasDropdown>("FormulaDropdown", 32, 10, 5, leftWidth - 20, 30);
        dropdownPanel->AddChild(formulaDropdown);

        // Formula editor
        formulaEditor = std::make_shared<UltraCanvasFormulaEditor>("FormulaEditor", 41,
                                                                   10, 90, leftWidth - 20, windowHeight - 180);

        // Status bar - Fixed: Use correct constructor parameters for labels
        auto statusPanel = std::make_shared<UltraCanvasContainer>("StatusPanel", 51, 0, windowHeight - 50, leftWidth, 50);
        statusLabel = std::make_shared<UltraCanvasLabel>("StatusLabel", 52, 10, 10, leftWidth - 20, 30, "Ready");
        statusLabel->SetTextColor(Colors::Green);
        statusPanel->AddChild(statusLabel);

        leftPanel->AddChild(menuPanel);
        leftPanel->AddChild(dropdownPanel);
        leftPanel->AddChild(formulaEditor);
        leftPanel->AddChild(statusPanel);

        // Setup event handlers - Fixed: Use correct method names
        newButton->onClicked = [this]() { CreateNewFormula(); };
        openButton->onClicked = [this]() { OpenFormula(); };
        saveButton->onClicked = [this]() { SaveFormula(); };
        saveAsButton->onClicked = [this]() { SaveFormulaAs(); };
        exportButton->onClicked = [this]() { ExportAsVideo(); };

        formulaEditor->onFormulaChanged = [this](const ProceduralFormula& formula) {
            OnFormulaTextChanged(formula.formula);
        };
    }

    void CreateRightPanel() {
        // Right panel for graphics output and controls (40% of width)
        int rightWidth = static_cast<int>(windowWidth * 0.4);
        int leftWidth = windowWidth - rightWidth;

        rightPanel = std::make_shared<UltraCanvasContainer>("RightPanel", 3, leftWidth, 0, rightWidth, windowHeight);

        // Use LayoutEngine for vertical layout instead of GridLayout
        LayoutParams verticalLayout = LayoutParams::Vertical(5);

        // Control panel
        controlPanel = std::make_shared<UltraCanvasContainer>("ControlPanel", 61, 0, 0, rightWidth, 120);

        // Use LayoutEngine for control layout instead of GridLayout
        LayoutParams controlLayout = LayoutParams::Grid(2, 5); // 2 columns

        // Animation controls - Fixed: Use correct constructor parameters
        startButton = std::make_shared<UltraCanvasButton>("StartButton", 62, 10, 10, 120, 30, "Start Animation");
        auto stopButton = std::make_shared<UltraCanvasButton>("StopButton", 63, 140, 10, 80, 30, "Stop");

        // Frame rate controls - Fixed: Use correct constructor parameters
        frameRateLabel = std::make_shared<UltraCanvasLabel>("FrameRateLabel", 64, 10, 50, 120, 25, "Speed: 1.0x");
        frameRateSlider = std::make_shared<UltraCanvasSlider>("FrameRateSlider", 65, 140, 50, 100, 25);
        frameRateSlider->SetRange(0.1f, 5.0f);
        frameRateSlider->SetValue(1.0f);

        controlPanel->AddChild(startButton);
        controlPanel->AddChild(stopButton);
        controlPanel->AddChild(frameRateLabel);
        controlPanel->AddChild(frameRateSlider);

        // Graphics output area
        int outputHeight = windowHeight - 170; // Leave space for controls and status
        // Note: Using forward declaration - don't actually instantiate procedural background
        // graphicsOutput = std::make_shared<UltraCanvasProceduralBackground>("GraphicsOutput", 71,
        //                                                                   10, 130, rightWidth - 20, outputHeight);
        // For now, create a placeholder container
        graphicsOutput = nullptr; // Will be implemented when procedural background is available

        // Status display - Fixed: Use correct constructor parameters
        auto rightStatusPanel = std::make_shared<UltraCanvasContainer>("RightStatusPanel", 81,
                                                                       0, windowHeight - 50, rightWidth, 50);
        auto performanceLabel = std::make_shared<UltraCanvasLabel>("PerformanceLabel", 82,
                                                                   10, 10, rightWidth - 20, 30, "FPS: 0");

        rightStatusPanel->AddChild(performanceLabel);

        rightPanel->AddChild(controlPanel);
        rightPanel->AddChild(graphicsOutput);
        rightPanel->AddChild(rightStatusPanel);

        // Setup event handlers - Fixed: Use correct method names
        startButton->onClicked = [this]() { ToggleAnimation(); };
        stopButton->onClicked = [this]() {
            isAnimating = false;
            startButton->SetText("Start Animation");
            statusLabel->SetText("Animation stopped");
        };

        // Fixed: Use correct callback setup for slider
        // Note: Need to check actual UltraCanvasSlider interface for callback setup
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
        statusLabel->SetTextColor(Colors::Yellow);  // Fixed: Use Yellow instead of Orange
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

            // Apply to graphics output
            // Note: This would need to be implemented in UltraCanvasProceduralBackground
            // graphicsOutput->SetFormula(formula);

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
            statusLabel->SetTextColor(Colors::Yellow);  // Fixed: Use Yellow instead of Orange
        }
    }

    void OnFrameRateChanged(float value) {
        animationSpeed = value;
        frameRateLabel->SetText("Speed: " + std::to_string(value).substr(0, 3) + "x");

        // Update the graphics output animation speed
        if (graphicsOutput) {
            // Note: This would need to be implemented in UltraCanvasProceduralBackground
            // graphicsOutput->SetAnimationSpeed(value);
        }
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
        statusLabel->SetTextColor(Colors::Yellow);  // Fixed: Use Yellow instead of Orange

        // For now, just demonstrate loading from a test file
        std::string testFile = "test_formula.json";
        std::ifstream file(testFile);
        if (file.is_open()) {
            // TODO: Parse JSON and load formula
            file.close();
            statusLabel->SetText("✓ Loaded: " + testFile);
            statusLabel->SetTextColor(Colors::Green);
            currentFilePath = testFile;
        } else {
            statusLabel->SetText("✗ Could not open file");
            statusLabel->SetTextColor(Colors::Red);
        }
    }

    void SaveFormula() {
        if (currentFilePath.empty()) {
            SaveFormulaAs();
        } else {
            SaveFormulaToFile(currentFilePath);
        }
    }

    void SaveFormulaAs() {
        // TODO: Implement save file dialog
        std::string fileName = "formula_" + std::to_string(std::time(nullptr)) + ".json";
        SaveFormulaToFile(fileName);
    }

    void SaveFormulaToFile(const std::string& fileName) {
        try {
            std::ofstream file(fileName);
            if (file.is_open()) {
                // TODO: Serialize formula to JSON
                ProceduralFormula formula = formulaEditor->GetFormula();

                // Simple JSON output (would use proper JSON library in production)
                file << "{\n";
                file << "  \"name\": \"" << formula.name << "\",\n";
                file << "  \"description\": \"" << formula.description << "\",\n";
                file << "  \"formula\": \"" << formula.formula << "\",\n";
                file << "  \"animationSpeed\": " << formula.animationSpeed << ",\n";
                file << "  \"complexity\": " << formula.complexity << "\n";
                file << "}\n";

                file.close();
                currentFilePath = fileName;
                statusLabel->SetText("✓ Saved: " + fileName);
                statusLabel->SetTextColor(Colors::Green);
            } else {
                statusLabel->SetText("✗ Could not save file");
                statusLabel->SetTextColor(Colors::Red);
            }
        } catch (const std::exception& e) {
            statusLabel->SetText("✗ Save error: " + std::string(e.what()));
            statusLabel->SetTextColor(Colors::Red);
        }
    }

    void ExportAsVideo() {
        // TODO: Implement video export
        statusLabel->SetText("Video export not yet implemented");
        statusLabel->SetTextColor(Colors::Yellow);  // Fixed: Use Yellow instead of Orange

        // Would render frames and encode to video file
        // This would require integration with video encoding library
    }

    void UpdateAnimation() {
        if (!isAnimating) return;

        // Update animation time
        auto now = std::chrono::steady_clock::now();
        static auto lastTime = now;
        auto deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        currentTime += deltaTime * animationSpeed;

        // Update graphics output with new time
        if (graphicsOutput) {
            // Note: This would need to be implemented in UltraCanvasProceduralBackground
            // graphicsOutput->SetTime(currentTime);
            // graphicsOutput->Regenerate();
        }
    }

public:
    static int main(int argc, char** argv) {
        try {
            auto app = UltraCanvasApplication::GetInstance();  // Fixed: Use GetInstance instead of Create
            auto mainWindow = std::make_shared<GraphicFormulaWindow>();

            WindowConfig config;
            config.title = "UltraCanvas Formula Graphics Generator";
            config.width = 1200;
            config.height = 800;
            config.resizable = true;

            if (!mainWindow->Create(config)) {
                std::cerr << "Failed to create main window" << std::endl;
                return -1;
            }

            mainWindow->Show();

            app->Run();
            return 0;  // Fixed: Return 0 since Run() is void
        } catch (const std::exception& e) {
            std::cerr << "Application error: " << e.what() << std::endl;
            return -1;
        }
    }
};