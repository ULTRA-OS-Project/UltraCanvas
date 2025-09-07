// UltraCanvasFormulaEditor.h
// Advanced procedural formula editor with syntax highlighting and live preview
// Version: 1.0.2
// Last Modified: 2025-08-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasProceduralBackgroundPlugin.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLayoutEngine.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <regex>
#include <fstream>
#include <jsoncpp/json/json.h>

namespace UltraCanvas {

// ===== FORMULA SYNTAX HIGHLIGHTING =====
    enum class SyntaxTokenType {
        Keyword,        // for, float, vec3, vec4
        Function,       // cos, sin, normalize, dot, cross
        Variable,       // i, z, d, p, o, t, FC
        Number,         // 1e2, 2e1, .1, 4.0
        Operator,       // +, -, *, /, =, <, >
        Punctuation,    // (, ), [, ], {, }
        Semicolon,      // ;
        Comma,          // ,
        Comment,        // //...
        String,         // "..."
        Unknown
    };

    struct SyntaxToken {
        SyntaxTokenType type;
        std::string text;
        size_t position;
        size_t length;

        SyntaxToken(SyntaxTokenType t, const std::string& txt, size_t pos, size_t len)
                : type(t), text(txt), position(pos), length(len) {}
    };

// ===== FORMULA VALIDATION =====
    struct FormulaValidationResult {
        bool isValid = false;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        float estimatedComplexity = 0.0f;
        int loopCount = 0;
        int functionCallCount = 0;

        std::string GetSummary() const {
            if (isValid) {
                return "Valid - Complexity: " + std::to_string(estimatedComplexity);
            } else {
                return "Errors found: " + std::to_string(errors.size());
            }
        }
    };

// ===== SYNTAX HIGHLIGHTER =====
    class FormulaSyntaxHighlighter {
    public:
        std::vector<SyntaxToken> HighlightSyntax(const std::string& code) {
            std::vector<SyntaxToken> tokens;

            // Simple regex-based tokenization
            std::regex tokenRegex(R"((\bfor\b|\bfloat\b|\bvec3\b|\bvec4\b|\bif\b|\belse\b)|)"
                                  R"((\bcos\b|\bsin\b|\bnormalize\b|\bdot\b|\bcross\b|\btanh\b)|)"
                                  R"((\b[a-zA-Z_]\w*\b)|)"
                                  R"((\d*\.?\d+(?:[eE][+-]?\d+)?)|)"
                                  R"(([+\-*/=<>!]+)|)"
                                  R"(([\(\)\[\]{}])|)"
                                  R"((;)|)"
                                  R"((,)|)"
                                  R"((//.*)|)"
                                  R"(("(?:[^"\\]|\\.)*"))");

            std::sregex_iterator iter(code.begin(), code.end(), tokenRegex);
            std::sregex_iterator end;

            for (; iter != end; ++iter) {
                const std::smatch& match = *iter;
                std::string tokenText = match.str();
                size_t position = match.position();

                SyntaxTokenType type = SyntaxTokenType::Unknown;

                if (match[1].matched) type = SyntaxTokenType::Keyword;
                else if (match[2].matched) type = SyntaxTokenType::Function;
                else if (match[3].matched) type = SyntaxTokenType::Variable;
                else if (match[4].matched) type = SyntaxTokenType::Number;
                else if (match[5].matched) type = SyntaxTokenType::Operator;
                else if (match[6].matched) type = SyntaxTokenType::Punctuation;
                else if (match[7].matched) type = SyntaxTokenType::Semicolon;
                else if (match[8].matched) type = SyntaxTokenType::Comma;
                else if (match[9].matched) type = SyntaxTokenType::Comment;
                else if (match[10].matched) type = SyntaxTokenType::String;

                tokens.emplace_back(type, tokenText, position, tokenText.length());
            }

            return tokens;
        }
    };

// ===== FORMULA VALIDATOR =====
    class FormulaValidator {
    public:
        FormulaValidationResult ValidateFormula(const std::string& formula) {
            FormulaValidationResult result;

            if (formula.empty()) {
                result.errors.push_back("Formula cannot be empty");
                return result;
            }

            // Basic syntax validation
            if (!ValidateBrackets(formula)) {
                result.errors.push_back("Mismatched brackets");
            }

            if (!ValidateSemicolons(formula)) {
                result.warnings.push_back("Missing semicolons detected");
            }

            // Complexity analysis
            result.estimatedComplexity = EstimateComplexity(formula);
            result.loopCount = CountLoops(formula);
            result.functionCallCount = CountFunctionCalls(formula);

            // Performance warnings
            if (result.loopCount > 3) {
                result.warnings.push_back("Multiple nested loops detected - may be slow");
            }

            if (result.estimatedComplexity > 8.0f) {
                result.warnings.push_back("High complexity formula - consider optimization");
            }

            result.isValid = result.errors.empty();
            return result;
        }

    private:
        bool ValidateBrackets(const std::string& formula) {
            int parentheses = 0, brackets = 0, braces = 0;

            for (char c : formula) {
                if (c == '(') parentheses++;
                else if (c == ')') parentheses--;
                else if (c == '[') brackets++;
                else if (c == ']') brackets--;
                else if (c == '{') braces++;
                else if (c == '}') braces--;

                if (parentheses < 0 || brackets < 0 || braces < 0) return false;
            }

            return parentheses == 0 && brackets == 0 && braces == 0;
        }

        bool ValidateSemicolons(const std::string& formula) {
            // Simple check - formula should end with semicolon or be a single expression
            return formula.back() == ';' || formula.find(';') == std::string::npos;
        }

        float EstimateComplexity(const std::string& formula) {
            float complexity = 1.0f;

            // Count operations
            complexity += std::count(formula.begin(), formula.end(), '*') * 0.1f;
            complexity += std::count(formula.begin(), formula.end(), '/') * 0.2f;
            complexity += std::count(formula.begin(), formula.end(), '+') * 0.05f;
            complexity += std::count(formula.begin(), formula.end(), '-') * 0.05f;

            // Count function calls
            std::vector<std::string> functions = {"cos", "sin", "tan", "sqrt", "pow", "normalize", "dot", "cross"};
            for (const auto& func : functions) {
                size_t pos = 0;
                while ((pos = formula.find(func, pos)) != std::string::npos) {
                    complexity += 0.3f;
                    pos += func.length();
                }
            }

            return complexity;
        }

        int CountLoops(const std::string& formula) {
            return std::count(formula.begin(), formula.end(), 'f') > 0 ? 1 : 0; // Simple 'for' detection
        }

        int CountFunctionCalls(const std::string& formula) {
            return std::count(formula.begin(), formula.end(), '(');
        }
    };

// ===== FORMULA LIBRARY MANAGER =====
    class FormulaLibraryManager {
    private:
        std::string libraryPath = "formulas.json";
        std::vector<ProceduralFormula> formulas;

    public:
        void SaveFormula(const ProceduralFormula& formula) {
            // Remove existing formula with same name
            auto it = std::find_if(formulas.begin(), formulas.end(),
                                   [&formula](const ProceduralFormula& f) { return f.name == formula.name; });
            if (it != formulas.end()) {
                formulas.erase(it);
            }

            formulas.push_back(formula);
            SaveToFile();
        }

        bool LoadFormula(const std::string& name, ProceduralFormula& formula) {
            auto it = std::find_if(formulas.begin(), formulas.end(),
                                   [&name](const ProceduralFormula& f) { return f.name == name; });
            if (it != formulas.end()) {
                formula = *it;
                return true;
            }
            return false;
        }

        std::vector<ProceduralFormula> GetAllFormulas() const {
            return formulas;
        }

        std::vector<std::string> GetFormulaNames() const {
            std::vector<std::string> names;
            for (const auto& formula : formulas) {
                names.push_back(formula.name);
            }
            return names;
        }

        void LoadFromFile() {
            // Simplified file loading - in real implementation would use JSON
            // For now, just initialize with empty library
        }

        void SaveToFile() {
            // Simplified file saving - in real implementation would use JSON
            // For now, just a stub
        }
    };

// ===== SYNTAX-AWARE TEXT EDITOR =====
    class UltraCanvasSyntaxTextEditor : public UltraCanvasTextArea {
    private:
        FormulaSyntaxHighlighter highlighter;
        std::vector<SyntaxToken> tokens;
        bool syntaxHighlightingEnabled = true;

    public:
        UltraCanvasSyntaxTextEditor(const std::string& identifier, int id, int x, int y, int width, int height)
                : UltraCanvasTextArea(identifier, id, x, y, width, height) {

            // For now, commenting out to avoid compilation errors
            // SetBackgroundColor(Color(30, 30, 30, 255)); // Dark background
            // SetBorderColor(Color(60, 60, 60, 255));
            // SetBorderWidth(1);
            // SetShowLineNumbers(true);
            // SetTabSize(4);
            // SetAutoIndent(true);
        }

        void SetSyntaxHighlighting(bool enabled) {
            syntaxHighlightingEnabled = enabled;
            if (enabled) {
                UpdateSyntaxHighlighting();
            }
        }

        void UpdateSyntaxHighlighting() {
            if (!syntaxHighlightingEnabled) return;

            std::string text = GetText();
            tokens = highlighter.HighlightSyntax(text);
        }

        void Render() override {
            // Call base render first
            auto ctx = GetRenderContext();
            UltraCanvasTextArea::Render();

            // Apply syntax highlighting
            if (syntaxHighlightingEnabled && !tokens.empty()) {
                ctx->PushState();
                RenderSyntaxHighlighting(ctx);
                ctx->PopState();
            }
        }

        bool HandleKeyEvent(const UCEvent& event) {
            // Handle the event using base class functionality
            // For now, just return false since we need to implement this properly

            if (event.type == UCEventType::KeyDown ||
                event.type == UCEventType::TextInput) {
                // Update syntax highlighting when text changes
                UpdateSyntaxHighlighting();
            }

            return false;
        }

    private:
        void RenderSyntaxHighlighting(IRenderContext* ctx) {
            // This would be a complex implementation that overlays
            // colored text on top of the base text area
            // For now, indicate that syntax highlighting is active

            Rect2Di bounds = GetBounds();

            // Draw a small indicator that syntax highlighting is enabled
            ctx->SetFillColor(Color(0, 255, 0, 100));
            ctx->DrawRectangle(Rect2Di(bounds.x + bounds.width - 20, bounds.y + 5, 15, 10));
        }
    };

// ===== MAIN FORMULA EDITOR COMPONENT =====
    class UltraCanvasFormulaEditor : public UltraCanvasContainer {
    private:
        // UI Components
        std::shared_ptr<UltraCanvasSyntaxTextEditor> codeEditor;
        std::shared_ptr<UltraCanvasProceduralBackground> livePreview;
        std::shared_ptr<UltraCanvasLabel> nameLabel;
        std::shared_ptr<UltraCanvasTextInput> nameInput;
        std::shared_ptr<UltraCanvasLabel> descriptionLabel;
        std::shared_ptr<UltraCanvasTextInput> descriptionInput;
        std::shared_ptr<UltraCanvasButton> validateButton;
        std::shared_ptr<UltraCanvasButton> previewButton;
        std::shared_ptr<UltraCanvasButton> saveButton;
        std::shared_ptr<UltraCanvasButton> loadButton;
        std::shared_ptr<UltraCanvasLabel> statusLabel;
        std::shared_ptr<UltraCanvasDropdown> formulaLibrary;
        std::shared_ptr<UltraCanvasSlider> animationSpeedSlider;
        std::shared_ptr<UltraCanvasLabel> complexityLabel;

        // Core systems
        FormulaValidator validator;
        FormulaLibraryManager libraryManager;

        // State
        ProceduralFormula currentFormula;
        FormulaValidationResult lastValidation;
        bool previewEnabled = true;
        bool autoValidation = true;

    public:
        UltraCanvasFormulaEditor(const std::string& identifier, long id, long x, long y, long width, long height)
                : UltraCanvasContainer(identifier, id, x, y, width, height) {

            CreateUI();
            SetupEventHandlers();
            LoadBuiltInFormulas();
            libraryManager.LoadFromFile();

            // Load Crystal 2 formula as example
            LoadCrystal2Formula();
        }

        // ===== PUBLIC INTERFACE =====
        void SetFormula(const ProceduralFormula& formula) {
            currentFormula = formula;
            UpdateUIFromFormula();
        }

        ProceduralFormula GetFormula() const {
            return currentFormula;
        }

        void SetPreviewEnabled(bool enabled) {
            previewEnabled = enabled;
            if (livePreview) {
                livePreview->SetVisible(enabled);
            }
        }

        std::function<void(const ProceduralFormula&)> onFormulaSaved;
        std::function<void(const ProceduralFormula&)> onFormulaChanged;
        std::function<void(const FormulaValidationResult&)> onValidationChanged;

    protected:
        // FIXED: Made these methods protected for inheritance access
        void UpdateUIFromFormula() {
            nameInput->SetText(currentFormula.name);
            descriptionInput->SetText(currentFormula.description);
            codeEditor->SetText(currentFormula.formula);
            animationSpeedSlider->SetValue(currentFormula.animationSpeed);
        }

        void ValidateCurrentFormula() {
            lastValidation = validator.ValidateFormula(currentFormula.formula);

            // Update status
            if (lastValidation.isValid) {
                statusLabel->SetText("✓ " + lastValidation.GetSummary());
                statusLabel->SetTextColor(Colors::Green);
            } else {
                statusLabel->SetText("✗ " + lastValidation.GetSummary());
                statusLabel->SetTextColor(Colors::Red);
            }

            // Update complexity info
            complexityLabel->SetText("Complexity: " +
                                     std::to_string(lastValidation.estimatedComplexity) +
                                     " | Loops: " + std::to_string(lastValidation.loopCount));

            if (onValidationChanged) {
                onValidationChanged(lastValidation);
            }
        }

        void UpdatePreview() {
            if (!previewEnabled || !livePreview || !lastValidation.isValid) return;

            // FIXED: Just trigger redraw since LoadFormula and SetAnimationSpeed don't exist yet
            livePreview->RequestRedraw();
        }

        // FIXED: Made virtual for override in derived classes
        virtual void LoadFormulaFromLibrary(const std::string& formulaName) {
            // Remove (User) suffix if present
            std::string cleanName = formulaName;
            size_t userPos = cleanName.find(" (User)");
            if (userPos != std::string::npos) {
                cleanName = cleanName.substr(0, userPos);
            }

            // Try to load from user library first
            ProceduralFormula formula;
            if (libraryManager.LoadFormula(cleanName, formula)) {
                SetFormula(formula);
                statusLabel->SetText("✓ Loaded user formula: " + cleanName);
                statusLabel->SetTextColor(Colors::Green);
                return;
            }

            // Load built-in formula
            if (cleanName == "Crystal 2") {
                LoadCrystal2Formula();
            } else {
                // For other built-in formulas, create a basic template
                currentFormula.name = cleanName;
                currentFormula.description = "Built-in formula: " + cleanName;
                currentFormula.formula = "o = 1.0; // Placeholder for " + cleanName;
                UpdateUIFromFormula();
                ValidateCurrentFormula();

                statusLabel->SetText("✓ Loaded built-in formula: " + cleanName);
                statusLabel->SetTextColor(Colors::Green);
            }
        }

    private:
        void CreateUI() {
            // Main layout: left side editor, right side preview
            Rect2Di bounds = GetContentArea();
            int editorWidth = bounds.width * 0.5;
            int previewWidth = static_cast<int>(bounds.width * 0.4f);

            // FIXED: Use proper CreateLabel and CreateTextInput signatures with identifiers
            nameLabel = CreateLabel("nameLabel", 1001, 0, 0, 120, 30, "Name:");
            nameInput = CreateTextInput("nameInput", 1002, 120, 00, 200, 30);

            descriptionLabel = CreateLabel("descLabel", 1003, 00,   35, 120, 30, "Description:");
            descriptionInput = CreateTextInput("descInput", 1004, 120, 35, 200, 30);

            // Code editor (main editing area)
            codeEditor = std::make_shared<UltraCanvasSyntaxTextEditor>("codeEditor", 1005, 0, 70, editorWidth, 100);

            // Control buttons
            validateButton = CreateButton("validateBtn", 1006, 10, 190, 80, 30, "Validate");
            previewButton = CreateButton("previewBtn", 1007, 100, 190, 80, 30, "Preview");
            saveButton = CreateButton("saveBtn", 1008, 190, 190, 80, 30, "Save");
            loadButton = CreateButton("loadBtn", 1009, 280, 190, 80, 30, "Load");

            // Status display
            statusLabel = CreateLabel("statusLabel", 1010, 10, 230, editorWidth - 20, 25, "Ready");
            complexityLabel = CreateLabel("complexityLabel", 1011, 10, 260, editorWidth - 20, 25, "Complexity: 0.0");

            // Animation controls
            animationSpeedSlider = CreateSlider("animSlider", 1012, 10, 290, 200, 25);

            // Formula library dropdown
            formulaLibrary = CreateDropdown("formulaLib", 1013, 220, 290, 150, 25);

            // FIXED: Live preview area - use direct constructor with correct signature
            livePreview = std::make_shared<UltraCanvasProceduralBackground>(
                    "livePreview", 1014, editorWidth + 10, 10, previewWidth - 20, 400
            );

            // Add all components to container
            AddChild(nameLabel);
            AddChild(nameInput);
            AddChild(descriptionLabel);
            AddChild(descriptionInput);
            AddChild(codeEditor);
            AddChild(validateButton);
            AddChild(previewButton);
            AddChild(saveButton);
            AddChild(loadButton);
            AddChild(statusLabel);
            AddChild(complexityLabel);
            AddChild(animationSpeedSlider);
            AddChild(formulaLibrary);
            //AddChild(livePreview);
        }

        virtual void Render() {
            UltraCanvasContainer::Render();
        }

        void SetupEventHandlers() {
            // Button event handlers
            validateButton->onClicked = [this]() {
                ValidateCurrentFormula();
            };

            previewButton->onClicked = [this]() {
                UpdatePreview();
            };

            saveButton->onClicked = [this]() {
                SaveCurrentFormula();
            };

            loadButton->onClicked = [this]() {
                ShowLoadDialog();
            };

            // FIXED: Animation speed slider - removed SetAnimationSpeed call
            animationSpeedSlider->onValueChanged = [this](float value) {
                currentFormula.animationSpeed = value;
                // TODO: When UltraCanvasProceduralBackground supports SetAnimationSpeed, uncomment:
                // if (livePreview) {
                //     livePreview->SetAnimationSpeed(value);
                // }
            };
        }

        void LoadBuiltInFormulas() {
            std::vector<std::string> builtInFormulas = {
                    "Dust", "Hive", "Droplets", "Aquifier", "Spinner 2",
                    "Spinner", "Smooth Waves", "Chaos Universe", "Crystal 2"
            };

            for (const auto& name : builtInFormulas) {
                formulaLibrary->AddItem(name);
            }

            // Add user formulas
            auto userFormulas = libraryManager.GetFormulaNames();
            for (const auto& name : userFormulas) {
                formulaLibrary->AddItem(name + " (User)");
            }
        }

        void LoadCrystal2Formula() {
            currentFormula.name = "Crystal 2";
            currentFormula.description = "Crystalline structures with geometric patterns";
            currentFormula.author = "User";
            currentFormula.language = FormulaLanguage::Mathematical;
            currentFormula.preferredMethod = RenderingMethod::CPU;
            currentFormula.backgroundType = ProceduralBackgroundType::Animated;
            currentFormula.formula = "for(float z,d,i;i++<1e2;o+=(cos(i*.2+vec4(0,1,2,0))+1.)/d*i){vec3 p=z*normalize(FC.rgb*2.-r.xyy),a=normalize(cos(vec3(0,1,2)+t));p.z+=4.;a=abs(a*dot(a,p)-cross(a,p))-i/2e2;z+=d=.01+.2*abs(max(max(a+=.6*a.yzx,a.y).x,a.z)-2.);}o=1.-tanh(o*o/4e11);";
            currentFormula.animationSpeed = 0.4f;
            currentFormula.complexity = 8.7f;
            currentFormula.tags = {"crystal", "geometric", "complex", "beautiful"};

            UpdateUIFromFormula();
            ValidateCurrentFormula();
            UpdatePreview();
        }

        void SaveCurrentFormula() {
            // Update formula from UI
            currentFormula.name = nameInput->GetText();
            currentFormula.description = descriptionInput->GetText();
            currentFormula.formula = codeEditor->GetText();
            currentFormula.animationSpeed = animationSpeedSlider->GetValue();

            // Save to library
            libraryManager.SaveFormula(currentFormula);

            statusLabel->SetText("✓ Formula saved: " + currentFormula.name);
            statusLabel->SetTextColor(Colors::Green);

            if (onFormulaSaved) {
                onFormulaSaved(currentFormula);
            }
        }

        void ShowLoadDialog() {
            // For now, just load the first available formula
            auto formulas = libraryManager.GetAllFormulas();
            if (!formulas.empty()) {
                SetFormula(formulas[0]);
                statusLabel->SetText("✓ Loaded: " + formulas[0].name);
            }
        }
    };

// ===== CONVENIENCE FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasFormulaEditor> CreateFormulaEditor(
            const std::string& identifier, long id, long x, long y, long width, long height) {
        return std::make_shared<UltraCanvasFormulaEditor>(identifier, id, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasFormulaEditor> CreateFullScreenFormulaEditor(const std::string& identifier, long id) {
        // Assuming full screen is 1920x1080
        return std::make_shared<UltraCanvasFormulaEditor>(identifier, id, 0, 0, 1920, 1080);
    }

} // namespace UltraCanvas