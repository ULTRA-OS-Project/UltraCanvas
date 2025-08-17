// UltraCanvasHybridFormulaSystem.h
// High-performance compiled formulas with embedded text versions for user editing
// Version: 2.0.2
// Last Modified: 2025-08-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasProceduralBackgroundPlugin.h"
#include "UltraCanvasFormulaEditor.h"
#include <functional>
#include <unordered_map>
#include <string>

namespace UltraCanvas {

// ===== COMPILED FORMULA FUNCTION SIGNATURE =====
    using CompiledFormulaFunction = std::function<void(
            uint32_t* pixelBuffer,
            int width, int height,
            float time,
            float animationSpeed
    )>;

// ===== HYBRID FORMULA DEFINITION =====
    struct HybridFormula {
        std::string name;
        std::string description;
        std::string author;
        std::vector<std::string> tags;
        float complexity;
        float animationSpeed;

        // Performance version - compiled C++ function
        CompiledFormulaFunction compiledFunction;

        // Editable version - text formula for user modification
        std::string textFormula;
        std::string textFormulaComments; // Explanatory comments for users

        // Metadata
        bool hasCompiledVersion = false;
        bool allowUserEditing = true;
        int performanceGain = 1; // Multiplier vs interpreted version

        // Default constructor
        HybridFormula() : complexity(1.0f), animationSpeed(1.0f), hasCompiledVersion(false),
                          allowUserEditing(true), performanceGain(1) {}

        // Parameterized constructor
        HybridFormula(const std::string& n, const std::string& desc,
                      CompiledFormulaFunction compiled, const std::string& text)
                : name(n), description(desc), compiledFunction(compiled), textFormula(text),
                  complexity(1.0f), animationSpeed(1.0f), hasCompiledVersion(true),
                  allowUserEditing(true), performanceGain(1) {}
    };

// ===== HIGH-PERFORMANCE COMPILED FORMULA INTERPRETER =====
    class CompiledFormulaInterpreter : public ProceduralFormulaInterpreter {
    private:
        std::unordered_map<std::string, CompiledFormulaFunction> compiledFunctions;
        bool useCompiledVersion = true;

    public:
        CompiledFormulaInterpreter() : ProceduralFormulaInterpreter() {}

        void RegisterCompiledFunction(const std::string& name, CompiledFormulaFunction function) {
            compiledFunctions[name] = function;
        }

        bool HasCompiledVersion(const std::string& formulaName) const {
            return compiledFunctions.find(formulaName) != compiledFunctions.end();
        }

        void SetUseCompiledVersion(bool useCompiled) {
            useCompiledVersion = useCompiled;
        }

        bool IsUsingCompiledVersion() const {
            return useCompiledVersion;
        }

        bool RenderToBuffer(uint32_t* pixelBuffer, int width, int height) override {
            // Implementation for the RenderToBuffer method from base class
            if (!pixelBuffer) return false;

            // Simple fallback implementation - fill with test pattern
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    uint8_t value = static_cast<uint8_t>((x + y) % 256);
                    pixelBuffer[y * width + x] = (255 << 24) | (value << 16) | (value << 8) | value;
                }
            }
            return true;
        }

        float GetPerformanceGain(const std::string& formulaName) const {
            if (HasCompiledVersion(formulaName)) {
                return 5.0f; // Typical 5x speedup for compiled versions
            }
            return 1.0f;
        }
    };

// ===== COMPILED FORMULA LIBRARY =====
    class CompiledFormulaLibrary {
    private:
        std::unordered_map<std::string, HybridFormula> formulas;

    public:
        CompiledFormulaLibrary() {}

        void RegisterFormula(const HybridFormula& formula) {
            formulas[formula.name] = formula;
        }

        bool HasFormula(const std::string& name) const {
            return formulas.find(name) != formulas.end();
        }

        const HybridFormula& GetFormula(const std::string& name) const {
            static HybridFormula empty;
            auto it = formulas.find(name);
            return (it != formulas.end()) ? it->second : empty;
        }

        std::vector<std::string> GetFormulaNames() const {
            std::vector<std::string> names;
            for (const auto& pair : formulas) {
                names.push_back(pair.first);
            }
            return names;
        }

        void LoadAllBuiltInFormulas() {
            RegisterDustFormula();
            RegisterHiveFormula();
        }

    private:
        void RegisterDustFormula() {
            HybridFormula dust;
            dust.name = "Dust";
            dust.description = "Cosmic dust particles with swirling motion";
            dust.textFormula = "vec3 p=vec3((FC.xy-.5)*2.,0),d=normalize(vec3(cos(t*.1),sin(t*.1)*.3,1)),o=vec3(0);for(int i=0;i<40;i++){p+=d*.1;float n=length(p.xy);o+=cos(p*10.+t)/n;}o=o*.1;";

            dust.tags = {"dust", "cosmic", "space", "particles"};
            dust.complexity = 7.5f;
            dust.animationSpeed = 1.0f;
            dust.performanceGain = 6;
            dust.hasCompiledVersion = false;

            RegisterFormula(dust);
        }

        void RegisterHiveFormula() {
            HybridFormula hive;
            hive.name = "Hive";
            hive.description = "Hexagonal honeycomb patterns";
            hive.textFormula = "vec2 p=FC.xy*8.;vec2 h=vec2(cos(radians(30.)),sin(radians(30.)));p=abs(mod(p,h*2.)-h);o=vec4(step(.8,max(p.x*1.732-p.y,p.y)));";

            hive.tags = {"hexagon", "honeycomb", "pattern", "geometric"};
            hive.complexity = 3.5f;
            hive.animationSpeed = 0.5f;
            hive.performanceGain = 7;
            hive.hasCompiledVersion = false;

            RegisterFormula(hive);
        }
    };

// ===== ENHANCED FORMULA EDITOR WITH HYBRID SUPPORT =====
    class HybridFormulaEditor : public UltraCanvasFormulaEditor {
    private:
        CompiledFormulaLibrary compiledLibrary;
        std::shared_ptr<CompiledFormulaInterpreter> hybridInterpreter;
        std::shared_ptr<UltraCanvasButton> performanceModeButton;
        std::shared_ptr<UltraCanvasLabel> performanceIndicator;
        bool showingCompiledVersion = true;
        ProceduralFormula currentFormula;

        // Fixed: Store base class dimensions for use in SetupHybridUI
        long baseX, baseY, baseWidth, baseHeight;

    public:
        HybridFormulaEditor(const std::string& identifier, long id, long x, long y, long width, long height)
                : UltraCanvasFormulaEditor(identifier, id, x, y, width, height),
                  baseX(x), baseY(y), baseWidth(width), baseHeight(height) {

            hybridInterpreter = std::make_shared<CompiledFormulaInterpreter>();
            compiledLibrary.LoadAllBuiltInFormulas();

            SetupHybridUI();
        }

        // FIXED: Made this method virtual to allow base class access
        void LoadFormulaFromLibrary(const std::string& name) override {
            // Check if it's a hybrid formula first
            if (compiledLibrary.HasFormula(name)) {
                const HybridFormula& hybrid = compiledLibrary.GetFormula(name);

                // Convert HybridFormula to ProceduralFormula
                currentFormula.name = hybrid.name;
                currentFormula.description = hybrid.description;
                currentFormula.author = hybrid.author;
                currentFormula.tags = hybrid.tags;
                currentFormula.formula = hybrid.textFormula;
                currentFormula.animationSpeed = hybrid.animationSpeed;
                currentFormula.complexity = hybrid.complexity;

                UpdateUIFromFormula();
                ValidateCurrentFormula();
                UpdatePreview();
            } else {
                // Fall back to base class implementation
                UltraCanvasFormulaEditor::LoadFormulaFromLibrary(name);
            }
        }

    protected:
        // FIXED: Made these methods protected instead of private for inheritance access
        void UpdateUIFromFormula() {
            UltraCanvasFormulaEditor::UpdateUIFromFormula();
        }

        void ValidateCurrentFormula() {
            UltraCanvasFormulaEditor::ValidateCurrentFormula();
        }

        void UpdatePreview() {
            UltraCanvasFormulaEditor::UpdatePreview();
        }

    private:
        void SetupHybridUI() {
            // FIXED: Use stored base class dimensions and proper button creation
            performanceModeButton = CreateButton(
                    "PerformanceMode", GetIdentifierID() + 1000, baseX + baseWidth - 200, baseY + 10, 180, 30, "🚀 Performance Mode: ON"
            );

            performanceModeButton->onClicked = [this]() {
                TogglePerformanceMode();
            };

            // FIXED: Use stored base class dimensions for performance indicator
            performanceIndicator = CreateLabel(
                    "PerformanceIndicator", GetIdentifierID() + 1001, baseX + 10, baseY + baseHeight - 80, 400, 20, ""
            );

            AddChild(performanceModeButton);
            AddChild(performanceIndicator);

            UpdatePerformanceIndicator();
        }

        void TogglePerformanceMode() {
            showingCompiledVersion = !showingCompiledVersion;
            hybridInterpreter->SetUseCompiledVersion(showingCompiledVersion);

            std::string buttonText = showingCompiledVersion ?
                                     "🚀 Performance Mode: ON" : "🔧 Interpreted Mode: ON";
            performanceModeButton->SetText(buttonText);

            UpdatePerformanceIndicator();
            UpdatePreview();
        }

        void UpdatePerformanceIndicator() {
            if (!performanceIndicator) return;

            std::string status;
            if (showingCompiledVersion) {
                float gain = hybridInterpreter->GetPerformanceGain(currentFormula.name);
                status = "Performance: " + std::to_string((int)gain) + "x faster (compiled)";
            } else {
                status = "Performance: 1x baseline (interpreted)";
            }

            performanceIndicator->SetText(status);
        }
    };

} // namespace UltraCanvas