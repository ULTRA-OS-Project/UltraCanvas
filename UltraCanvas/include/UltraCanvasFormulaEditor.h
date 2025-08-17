// UltraCanvasFormulaEditor.h
// Advanced procedural formula editor with syntax highlighting and live preview
// Version: 1.0.1
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
        Number,         // 1e2, 2e1, .1, 4.
        Operator,       // +, -, *, /, =, ++, <
        Parenthesis,    // (, ), [, ]
        Semicolon,      // ;
        Comma,          // ,
        String,         // "text"
        Comment,        // // comment
        Error,          // Invalid syntax
        Normal          // Regular text
    };

    struct SyntaxToken {
        SyntaxTokenType type;
        std::string text;
        int startPos;
        int endPos;
        Color color;

        SyntaxToken(SyntaxTokenType t, const std::string& txt, int start, int end)
                : type(t), text(txt), startPos(start), endPos(end) {
            color = GetTokenColor(t);
        }

        static Color GetTokenColor(SyntaxTokenType type) {
            switch (type) {
                case SyntaxTokenType::Keyword:     return Color(86, 156, 214, 255);  // Blue
                case SyntaxTokenType::Function:    return Color(220, 220, 170, 255); // Yellow
                case SyntaxTokenType::Variable:    return Color(156, 220, 254, 255); // Light Blue
                case SyntaxTokenType::Number:      return Color(181, 206, 168, 255); // Green
                case SyntaxTokenType::Operator:    return Color(212, 212, 212, 255); // Light Gray
                case SyntaxTokenType::Parenthesis: return Color(255, 215, 0, 255);   // Gold
                case SyntaxTokenType::String:      return Color(206, 145, 120, 255); // Orange
                case SyntaxTokenType::Comment:     return Color(106, 153, 85, 255);  // Dark Green
                case SyntaxTokenType::Error:       return Color(244, 71, 71, 255);   // Red
                default:                           return Color(212, 212, 212, 255); // Default
            }
        }
    };

// ===== FORMULA VALIDATION =====
    struct FormulaValidationResult {
        bool isValid = false;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        float estimatedComplexity = 1.0f;
        int loopCount = 0;
        std::vector<std::string> usedVariables;
        std::vector<std::string> usedFunctions;

        bool HasErrors() const { return !errors.empty(); }
        bool HasWarnings() const { return !warnings.empty(); }

        std::string GetSummary() const {
            if (isValid) {
                return "Formula is valid. Complexity: " + std::to_string(estimatedComplexity);
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
                                  R"(("(?:[^"\\]|\\.)*")|)"
                                  R"((\s+))");

            std::sregex_iterator iter(code.begin(), code.end(), tokenRegex);
            std::sregex_iterator end;

            for (; iter != end; ++iter) {
                const std::smatch& match = *iter;
                std::string tokenText = match.str();
                int startPos = static_cast<int>(match.position());
                int endPos = startPos + static_cast<int>(tokenText.length());

                SyntaxTokenType tokenType = SyntaxTokenType::Normal;

                if (match[1].matched) tokenType = SyntaxTokenType::Keyword;
                else if (match[2].matched) tokenType = SyntaxTokenType::Function;
                else if (match[3].matched) tokenType = SyntaxTokenType::Variable;
                else if (match[4].matched) tokenType = SyntaxTokenType::Number;
                else if (match[5].matched) tokenType = SyntaxTokenType::Operator;
                else if (match[6].matched) tokenType = SyntaxTokenType::Parenthesis;
                else if (match[7].matched) tokenType = SyntaxTokenType::Semicolon;
                else if (match[8].matched) tokenType = SyntaxTokenType::Comma;
                else if (match[9].matched) tokenType = SyntaxTokenType::Comment;
                else if (match[10].matched) tokenType = SyntaxTokenType::String;
                else if (match[11].matched) continue; // Skip whitespace
                else tokenType = SyntaxTokenType::Normal;

                tokens.emplace_back(tokenType, tokenText, startPos, endPos);
            }

            return tokens;
        }
    };

// ===== FORMULA VALIDATOR =====
    class FormulaValidator {
    public:
        FormulaValidationResult ValidateFormula(const std::string& code) {
            FormulaValidationResult result;

            // Basic syntax validation
            if (code.empty()) {
                result.errors.push_back("Formula cannot be empty");
                return result;
            }

            // Check for required elements
            if (code.find("o") == std::string::npos) {
                result.errors.push_back("Formula must define output variable 'o'");
            }

            // Check parentheses balance
            if (!CheckParenthesesBalance(code)) {
                result.errors.push_back("Unbalanced parentheses");
            }

            // Estimate complexity
            result.estimatedComplexity = EstimateComplexity(code);

            // Count loops
            result.loopCount = CountLoops(code);

            // Extract used variables and functions
            ExtractUsedElements(code, result.usedVariables, result.usedFunctions);

            // If no errors, mark as valid
            result.isValid = result.errors.empty();

            return result;
        }

    private:
        bool CheckParenthesesBalance(const std::string& code) {
            int balance = 0;
            for (char c : code) {
                if (c == '(' || c == '[' || c == '{') balance++;
                else if (c == ')' || c == ']' || c == '}') balance--;
                if (balance < 0) return false;
            }
            return balance == 0;
        }

        float EstimateComplexity(const std::string& code) {
            float complexity = 1.0f;

            // Count mathematical operations
            complexity += std::count(code.begin(), code.end(), '+') * 0.1f;
            complexity += std::count(code.begin(), code.end(), '*') * 0.2f;
            complexity += std::count(code.begin(), code.end(), '/') * 0.3f;

            // Count function calls
            if (code.find("cos") != std::string::npos) complexity += 0.5f;
            if (code.find("sin") != std::string::npos) complexity += 0.5f;
            if (code.find("normalize") != std::string::npos) complexity += 0.3f;

            // Count loops
            complexity += CountLoops(code) * 2.0f;

            return std::min(complexity, 10.0f);
        }

        int CountLoops(const std::string& code) {
            int count = 0;
            size_t pos = 0;
            while ((pos = code.find("for", pos)) != std::string::npos) {
                count++;
                pos += 3;
            }
            return count;
        }

        void ExtractUsedElements(const std::string& code,
                                 std::vector<std::string>& variables,
                                 std::vector<std::string>& functions) {
            // This would be a more sophisticated implementation
            // For now, basic extraction
            std::vector<std::string> commonVars = {"i", "z", "d", "p", "o", "t", "FC", "r", "a", "s"};
            std::vector<std::string> commonFuncs = {"cos", "sin", "normalize", "dot", "cross", "tanh"};

            for (const auto& var : commonVars) {
                if (code.find(var) != std::string::npos) {
                    variables.push_back(var);
                }
            }

            for (const auto& func : commonFuncs) {
                if (code.find(func) != std::string::npos) {
                    functions.push_back(func);
                }
            }
        }
    };

// ===== FORMULA LIBRARY MANAGER =====
    class FormulaLibraryManager {
    private:
        std::vector<ProceduralFormula> userFormulas;
        std::string libraryPath = "/home/.ultracanvas/formulas/";

    public:
        void SaveFormula(const ProceduralFormula& formula) {
            // Add to user formulas
            auto it = std::find_if(userFormulas.begin(), userFormulas.end(),
                                   [&](const ProceduralFormula& f) { return f.name == formula.name; });

            if (it != userFormulas.end()) {
                *it = formula; // Update existing
            } else {
                userFormulas.push_back(formula); // Add new
            }

            // Save to file
            SaveToFile();
        }

        bool LoadFormula(const std::string& name, ProceduralFormula& formula) {
            auto it = std::find_if(userFormulas.begin(), userFormulas.end(),
                                   [&](const ProceduralFormula& f) { return f.name == name; });

            if (it != userFormulas.end()) {
                formula = *it;
                return true;
            }
            return false;
        }

        std::vector<std::string> GetFormulaNames() const {
            std::vector<std::string> names;
            for (const auto& formula : userFormulas) {
                names.push_back(formula.name);
            }
            return names;
        }

        std::vector<ProceduralFormula> GetAllFormulas() const {
            return userFormulas;
        }

        bool DeleteFormula(const std::string& name) {
            auto it = std::remove_if(userFormulas.begin(), userFormulas.end(),
                                     [&](const ProceduralFormula& f) { return f.name == name; });

            if (it != userFormulas.end()) {
                userFormulas.erase(it, userFormulas.end());
                SaveToFile();
                return true;
            }
            return false;
        }

        void LoadFromFile() {
            // Load formulas from JSON file
            std::ifstream file(libraryPath + "user_formulas.json");
            if (file.is_open()) {
                Json::Value root;
                file >> root;

                userFormulas.clear();
                for (const auto& item : root["formulas"]) {
                    ProceduralFormula formula;
                    formula.name = item["name"].asString();
                    formula.description = item["description"].asString();
                    formula.author = item["author"].asString();
                    formula.formula = item["code"].asString(); // FIXED: Use 'formula' not 'formulaCode'
                    formula.animationSpeed = item["animationSpeed"].asFloat();
                    formula.complexity = item["complexity"].asFloat();

                    for (const auto& tag : item["tags"]) {
                        formula.tags.push_back(tag.asString());
                    }

                    userFormulas.push_back(formula);
                }
            }
        }

        void SaveToFile() {
            // Create directory if it doesn't exist
            // std::filesystem::create_directories(libraryPath);

            Json::Value root;
            Json::Value formulasArray(Json::arrayValue);

            for (const auto& formula : userFormulas) {
                Json::Value item;
                item["name"] = formula.name;
                item["description"] = formula.description;
                item["author"] = formula.author;
                item["code"] = formula.formula; // FIXED: Use 'formula' not 'formulaCode'
                item["animationSpeed"] = formula.animationSpeed;
                item["complexity"] = formula.complexity;

                Json::Value tagsArray(Json::arrayValue);
                for (const auto& tag : formula.tags) {
                    tagsArray.append(tag);
                }
                item["tags"] = tagsArray;

                formulasArray.append(item);
            }

            root["formulas"] = formulasArray;

            std::ofstream file(libraryPath + "user_formulas.json");
            if (file.is_open()) {
                file << root;
            }
        }
    };

// ===== SYNTAX-HIGHLIGHTED TEXT EDITOR =====
    class UltraCanvasSyntaxTextEditor : public UltraCanvasTextArea {
    private:
        FormulaSyntaxHighlighter highlighter;
        std::vector<SyntaxToken> tokens;
        bool syntaxHighlightingEnabled = true;

    public:
        // FIXED: Added identifier parameter and proper constructor signature
        UltraCanvasSyntaxTextEditor(const std::string& identifier, int id, int x, int y, int width, int height)
                : UltraCanvasTextArea(identifier, id, x, y, width, height) {

            // Set appropriate styling for code editor
            SetFont("Consolas", 14); // Monospace font
            // Note: These methods need to be implemented in UltraCanvasTextArea
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

            std::string text = GetContent(); // FIXED: Use GetContent() instead of GetText()
            tokens = highlighter.HighlightSyntax(text);
        }

        void Render() override {
            // Call base render first
            UltraCanvasTextArea::Render();

            // Apply syntax highlighting
            if (syntaxHighlightingEnabled && !tokens.empty()) {
                RenderSyntaxHighlighting();
            }
        }

        // FIXED: Removed override as base class doesn't have this method
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
        void RenderSyntaxHighlighting() {
            // This would be a complex implementation that overlays
            // colored text on top of the base text area
            // For now, indicate that syntax highlighting is active

            ULTRACANVAS_RENDER_SCOPE();

            // FIXED: Use inherited properties instead of undefined variables
            Rect2D bounds = GetBounds();

            // Draw a small indicator that syntax highlighting is enabled
            SetFillColor(Color(0, 255, 0, 100));
            DrawRect(Rect2D(bounds.x + bounds.width - 20, bounds.y + 5, 15, 10));
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
        // FIXED: Added identifier parameter to constructor
        UltraCanvasFormulaEditor(const std::string& identifier, int id, int x, int y, int width, int height)
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

    private:
        void CreateUI() {
            // Main layout: left side editor, right side preview
            Rect2D bounds = GetBounds(); // FIXED: Use GetBounds() instead of undefined width
            int editorWidth = static_cast<int>(bounds.width * 0.6f);
            int previewWidth = static_cast<int>(bounds.width * 0.4f);

            // FIXED: Use proper CreateLabel and CreateTextInput signatures with identifiers
            nameLabel = CreateLabel("nameLabel", 1001, 10, 10, 100, 25, "Name:");
            nameInput = CreateTextInput("nameInput", 1002, 120, 10, 200, 25);

            descriptionLabel = CreateLabel("descLabel", 1003, 10, 40, 100, 25, "Description:");
            descriptionInput = CreateTextInput("descInput", 1004, 120, 40, 200, 25);

            // Code editor (main editing area)
            codeEditor = std::make_shared<UltraCanvasSyntaxTextEditor>("codeEditor", 1005, 10, 80, editorWidth - 20, 400);

            // Control buttons
            validateButton = CreateButton("validateBtn", 1006, 10, 490, 80, 30, "Validate");
            previewButton = CreateButton("previewBtn", 1007, 100, 490, 80, 30, "Preview");
            saveButton = CreateButton("saveBtn", 1008, 190, 490, 80, 30, "Save");
            loadButton = CreateButton("loadBtn", 1009, 280, 490, 80, 30, "Load");

            // Status display
            statusLabel = CreateLabel("statusLabel", 1010, 10, 530, editorWidth - 20, 25, "Ready");
            complexityLabel = CreateLabel("complexityLabel", 1011, 10, 560, editorWidth - 20, 25, "Complexity: 0.0");

            // Animation controls
            animationSpeedSlider = CreateSlider("animSlider", 1012, 10, 590, 200, 25);

            // Formula library dropdown
            formulaLibrary = CreateDropdown("formulaLib", 1013, 220, 590, 150, 25);

            // Live preview area
            livePreview = CreateProceduralBackground("livePreview", 1014, editorWidth + 10, 10, previewWidth - 20, 400);

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
            AddChild(livePreview);
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

            // Animation speed slider
            animationSpeedSlider->onValueChanged = [this](float value) {
                currentFormula.animationSpeed = value;
                if (livePreview) {
                    livePreview->SetAnimationSpeed(value);
                }
            };

            // Code editor changes - need to implement proper text change events
            // For now, commenting out as this needs proper event system
            /*
            codeEditor->onTextChanged = [this](const std::string& text) {
                currentFormula.formula = text; // FIXED: Use 'formula' not 'formulaCode'

                if (autoValidation) {
                    ValidateCurrentFormula();
                }

                if (previewEnabled) {
                    UpdatePreview();
                }

                if (onFormulaChanged) {
                    onFormulaChanged(currentFormula);
                }
            };
            */

            // Formula library selection - need to implement proper selection events
            /*
            formulaLibrary->onSelectionChanged = [this](int index, const std::string& text) {
                LoadFormulaFromLibrary(text);
            };
            */
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
            currentFormula.formula = "for(float z,d,i;i++<1e2;o+=(cos(i*.2+vec4(0,1,2,0))+1.)/d*i){vec3 p=z*normalize(FC.rgb*2.-r.xyy),a=normalize(cos(vec3(0,1,2)+t));p.z+=4.;a=abs(a*dot(a,p)-cross(a,p))-i/2e2;z+=d=.01+.2*abs(max(max(a+=.6*a.yzx,a.y).x,a.z)-2.);}o=1.-tanh(o*o/4e11);"; // FIXED: Use 'formula' not 'formulaCode'
            currentFormula.animationSpeed = 0.4f;
            currentFormula.complexity = 8.7f;
            currentFormula.tags = {"crystal", "geometric", "complex", "beautiful"};

            UpdateUIFromFormula();
            ValidateCurrentFormula();
            UpdatePreview();
        }

        void UpdateUIFromFormula() {
            nameInput->SetText(currentFormula.name);
            descriptionInput->SetText(currentFormula.description);
            codeEditor->SetContent(currentFormula.formula); // FIXED: Use 'formula' not 'formulaCode'
            animationSpeedSlider->SetValue(currentFormula.animationSpeed);
        }

        void ValidateCurrentFormula() {
            lastValidation = validator.ValidateFormula(currentFormula.formula); // FIXED: Use 'formula' not 'formulaCode'

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

            livePreview->LoadFormula(currentFormula);
            livePreview->SetAnimationSpeed(currentFormula.animationSpeed);
        }

        void SaveCurrentFormula() {
            // Update formula from UI
            currentFormula.name = nameInput->GetText();
            currentFormula.description = descriptionInput->GetText();
            currentFormula.formula = codeEditor->GetContent(); // FIXED: Use 'formula' not 'formulaCode'
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

        void LoadFormulaFromLibrary(const std::string& formulaName) {
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
    };

// ===== CONVENIENCE FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasFormulaEditor> CreateFormulaEditor(
            const std::string& identifier, int id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasFormulaEditor>(identifier, id, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasFormulaEditor> CreateFullScreenFormulaEditor(const std::string& identifier, int id) {
        // Assuming full screen is 1920x1080
        return std::make_shared<UltraCanvasFormulaEditor>(identifier, id, 0, 0, 1920, 1080);
    }

} // namespace UltraCanvas