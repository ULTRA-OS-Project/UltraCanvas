// UltraCanvasFormulaEditor.h
// Advanced procedural formula editor with syntax highlighting and live preview
// Version: 1.0.0
// Last Modified: 2025-07-15
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
            return std::to_string(errors.size()) + " error(s), " + 
                   std::to_string(warnings.size()) + " warning(s)";
        }
    }
};

// ===== SYNTAX HIGHLIGHTER =====
class FormulaSyntaxHighlighter {
private:
    std::vector<std::string> keywords = {
        "for", "float", "vec3", "vec4", "int", "bool", "if", "else", "while", "return"
    };
    
    std::vector<std::string> functions = {
        "cos", "sin", "tan", "acos", "asin", "atan", "atan2",
        "exp", "log", "sqrt", "pow", "abs", "min", "max", "clamp",
        "floor", "ceil", "fract", "mod", "step", "smoothstep",
        "mix", "length", "distance", "dot", "cross", "normalize",
        "reflect", "refract", "tanh", "sinh", "cosh"
    };
    
    std::vector<std::string> variables = {
        "i", "z", "d", "p", "o", "t", "FC", "r", "uv", "a", "s", "n", "f", "b", "x", "y", "c"
    };
    
public:
    std::vector<SyntaxToken> HighlightSyntax(const std::string& code) {
        std::vector<SyntaxToken> tokens;
        
        std::regex tokenRegex(R"((\b(?:for|float|vec3|vec4|int|bool|if|else|while|return)\b)|(\b(?:cos|sin|tan|acos|asin|atan|atan2|exp|log|sqrt|pow|abs|min|max|clamp|floor|ceil|fract|mod|step|smoothstep|mix|length|distance|dot|cross|normalize|reflect|refract|tanh|sinh|cosh)\b)|(\b(?:i|z|d|p|o|t|FC|r|uv|a|s|n|f|b|x|y|c)\b)|(\b\d+\.?\d*(?:[eE][+-]?\d+)?\b)|([+\-*/=<>!&|^%]|\+\+|--|<=|>=|==|!=|&&|\|\|)|([(){}[\]])|([;])|([,])|(//.*$)|(".*?")|(\s+)|(.))");
        
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
        
        // Performance warnings
        if (result.estimatedComplexity > 8.0f) {
            result.warnings.push_back("High complexity - may impact performance");
        }
        
        if (result.loopCount > 2) {
            result.warnings.push_back("Multiple loops detected - consider optimization");
        }
        
        // Advanced validation could include:
        // - Variable usage before declaration
        // - Function parameter validation
        // - Type checking
        // - Dead code detection
        
        result.isValid = result.errors.empty();
        return result;
    }
    
private:
    bool CheckParenthesesBalance(const std::string& code) {
        int balance = 0;
        for (char c : code) {
            if (c == '(') balance++;
            else if (c == ')') balance--;
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
        std::vector<std::string> complexFunctions = {"cos", "sin", "exp", "log", "sqrt", "pow"};
        for (const auto& func : complexFunctions) {
            size_t pos = 0;
            while ((pos = code.find(func, pos)) != std::string::npos) {
                complexity += 0.5f;
                pos += func.length();
            }
        }
        
        // Count loops
        size_t pos = 0;
        while ((pos = code.find("for", pos)) != std::string::npos) {
            complexity += 2.0f;
            pos += 3;
        }
        
        return complexity;
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
                formula.formulaCode = item["code"].asString();
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
            item["code"] = formula.formulaCode;
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
    UltraCanvasSyntaxTextEditor(int id, int x, int y, int width, int height)
        : UltraCanvasTextArea(id, x, y, width, height) {
        
        // Set appropriate styling for code editor
        SetFont("Consolas", 14); // Monospace font
        SetBackgroundColor(Color(30, 30, 30, 255)); // Dark background
        SetTextColor(Color(212, 212, 212, 255)); // Light text
        SetBorderColor(Color(60, 60, 60, 255));
        SetBorderWidth(1);
        
        // Enable line numbers and other code editor features
        SetShowLineNumbers(true);
        SetTabSize(4);
        SetAutoIndent(true);
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
        UltraCanvasTextArea::Render();
        
        // Apply syntax highlighting
        if (syntaxHighlightingEnabled && !tokens.empty()) {
            RenderSyntaxHighlighting();
        }
    }
    
    bool HandleEvent(const UCEvent& event) override {
        bool handled = UltraCanvasTextArea::HandleEvent(event);
        
        if (handled && (event.type == UCEventType::KeyDown || 
                       event.type == UCEventType::TextInput)) {
            // Update syntax highlighting when text changes
            UpdateSyntaxHighlighting();
        }
        
        return handled;
    }
    
private:
    void RenderSyntaxHighlighting() {
        // This would be a complex implementation that overlays
        // colored text on top of the base text area
        // For now, indicate that syntax highlighting is active
        
        ULTRACANVAS_RENDER_SCOPE();
        
        // Draw a small indicator that syntax highlighting is enabled
        SetFillColor(Color(0, 255, 0, 100));
        DrawRect(Rect2D(x + width - 20, y + 5, 15, 10));
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
    UltraCanvasFormulaEditor(int id, int x, int y, int width, int height)
        : UltraCanvasContainer(id, x, y, width, height) {
        
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
        int editorWidth = width * 0.6f;
        int previewWidth = width * 0.4f;
        
        // Code editor section
        nameLabel = CreateLabel(1001, 10, 10, 100, 25, "Name:");
        nameInput = CreateTextInput(1002, 120, 10, 200, 25);
        
        descriptionLabel = CreateLabel(1003, 10, 45, 100, 25, "Description:");
        descriptionInput = CreateTextInput(1004, 120, 45, 300, 25);
        
        codeEditor = std::make_shared<UltraCanvasSyntaxTextEditor>(
            1005, 10, 80, editorWidth - 20, height - 200);
        
        // Control buttons
        validateButton = CreateButton(1006, 10, height - 110, 80, 30, "Validate");
        previewButton = CreateButton(1007, 100, height - 110, 80, 30, "Preview");
        saveButton = CreateButton(1008, 190, height - 110, 80, 30, "Save");
        loadButton = CreateButton(1009, 280, height - 110, 80, 30, "Load");
        
        // Animation speed control
        auto speedLabel = CreateLabel(1010, 10, height - 70, 120, 25, "Animation Speed:");
        animationSpeedSlider = CreateSlider(1011, 140, height - 70, 150, 25);
        animationSpeedSlider->SetRange(0.1f, 2.0f);
        animationSpeedSlider->SetValue(0.5f);
        
        // Status and info
        statusLabel = CreateLabel(1012, 10, height - 40, editorWidth - 20, 25, "Ready");
        complexityLabel = CreateLabel(1013, 10, height - 15, editorWidth - 20, 15, "");
        
        // Formula library dropdown
        formulaLibrary = CreateDropdown(1014, 370, height - 110, 150, 30);
        
        // Live preview
        livePreview = std::make_shared<UltraCanvasProceduralBackground>(
            1015, editorWidth + 10, 10, previewWidth - 20, height - 20);
        
        // Add all components
        AddChild(nameLabel);
        AddChild(nameInput);
        AddChild(descriptionLabel);
        AddChild(descriptionInput);
        AddChild(codeEditor);
        AddChild(validateButton);
        AddChild(previewButton);
        AddChild(saveButton);
        AddChild(loadButton);
        AddChild(speedLabel);
        AddChild(animationSpeedSlider);
        AddChild(statusLabel);
        AddChild(complexityLabel);
        AddChild(formulaLibrary);
        AddChild(livePreview);
    }
    
    void SetupEventHandlers() {
        // Validation button
        validateButton->onClicked = [this]() {
            ValidateCurrentFormula();
        };
        
        // Preview button
        previewButton->onClicked = [this]() {
            UpdatePreview();
        };
        
        // Save button
        saveButton->onClicked = [this]() {
            SaveCurrentFormula();
        };
        
        // Load button
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
        
        // Code editor changes
        codeEditor->onTextChanged = [this](const std::string& text) {
            currentFormula.formulaCode = text;
            
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
        
        // Name input
        nameInput->onTextChanged = [this](const std::string& text) {
            currentFormula.name = text;
        };
        
        // Description input
        descriptionInput->onTextChanged = [this](const std::string& text) {
            currentFormula.description = text;
        };
        
        // Formula library selection
        formulaLibrary->onSelectionChanged = [this](int index, const std::string& text) {
            LoadFormulaFromLibrary(text);
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
        currentFormula.renderMethod = RenderingMethod::CPU;
        currentFormula.backgroundType = ProceduralBackgroundType::Animated;
        currentFormula.formulaCode = "for(float z,d,i;i++<1e2;o+=(cos(i*.2+vec4(0,1,2,0))+1.)/d*i){vec3 p=z*normalize(FC.rgb*2.-r.xyy),a=normalize(cos(vec3(0,1,2)+t));p.z+=4.;a=abs(a*dot(a,p)-cross(a,p))-i/2e2;z+=d=.01+.2*abs(max(max(a+=.6*a.yzx,a.y).x,a.z)-2.);}o=1.-tanh(o*o/4e11);";
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
        codeEditor->SetText(currentFormula.formulaCode);
        animationSpeedSlider->SetValue(currentFormula.animationSpeed);
    }
    
    void ValidateCurrentFormula() {
        lastValidation = validator.ValidateFormula(currentFormula.formulaCode);
        
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
        livePreview->StartAnimation();
    }
    
    void SaveCurrentFormula() {
        if (!lastValidation.isValid) {
            statusLabel->SetText("Cannot save: Formula has errors");
            statusLabel->SetTextColor(Colors::Red);
            return;
        }
        
        // Update formula from UI
        currentFormula.name = nameInput->GetText();
        currentFormula.description = descriptionInput->GetText();
        currentFormula.formulaCode = codeEditor->GetText();
        currentFormula.animationSpeed = animationSpeedSlider->GetValue();
        currentFormula.complexity = lastValidation.estimatedComplexity;
        
        // Save to library
        libraryManager.SaveFormula(currentFormula);
        
        // Update dropdown
        formulaLibrary->Clear();
        LoadBuiltInFormulas();
        
        statusLabel->SetText("✓ Formula saved successfully");
        statusLabel->SetTextColor(Colors::Green);
        
        if (onFormulaSaved) {
            onFormulaSaved(currentFormula);
        }
    }
    
    void ShowLoadDialog() {
        // For now, just use the dropdown
        // In a full implementation, this could show a dialog with preview thumbnails
        statusLabel->SetText("Select formula from dropdown above");
        statusLabel->SetTextColor(Colors::Blue);
    }
    
    void LoadFormulaFromLibrary(const std::string& name) {
        std::string formulaName = name;
        
        // Remove "(User)" suffix if present
        size_t userPos = formulaName.find(" (User)");
        if (userPos != std::string::npos) {
            formulaName = formulaName.substr(0, userPos);
        }
        
        // Try to load from user library first
        ProceduralFormula formula;
        if (libraryManager.LoadFormula(formulaName, formula)) {
            SetFormula(formula);
            statusLabel->SetText("✓ Loaded user formula: " + formulaName);
            statusLabel->SetTextColor(Colors::Green);
            return;
        }
        
        // Load built-in formula
        if (formulaName == "Crystal 2") {
            LoadCrystal2Formula();
        } else {
            // Create a temporary background to load the built-in formula
            auto tempBackground = CreateProceduralBackground(9999, 0, 0, 100, 100);
            tempBackground->LoadFormulaByName(formulaName);
            
            currentFormula = tempBackground->GetCurrentFormula();
            UpdateUIFromFormula();
            ValidateCurrentFormula();
            UpdatePreview();
            
            statusLabel->SetText("✓ Loaded built-in formula: " + formulaName);
            statusLabel->SetTextColor(Colors::Green);
        }
    }
};

// ===== CONVENIENCE FUNCTIONS =====
inline std::shared_ptr<UltraCanvasFormulaEditor> CreateFormulaEditor(
    int id, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasFormulaEditor>(id, x, y, width, height);
}

inline std::shared_ptr<UltraCanvasFormulaEditor> CreateFullScreenFormulaEditor(int id) {
    // Assuming full screen is 1920x1080
    return std::make_shared<UltraCanvasFormulaEditor>(id, 0, 0, 1920, 1080);
}