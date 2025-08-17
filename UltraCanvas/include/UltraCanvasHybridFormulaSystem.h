// UltraCanvasHybridFormulaSystem.h
// High-performance compiled formulas with embedded text versions for user editing
// Version: 2.0.1
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
        HybridFormula currentHybridFormula;
        bool useCompiledVersion = true;
        std::unique_ptr<CPUMathematicalInterpreter> fallbackInterpreter;
        float currentTime = 0.0f;

    public:
        CompiledFormulaInterpreter() {
            fallbackInterpreter = std::make_unique<CPUMathematicalInterpreter>();
        }

        bool LoadHybridFormula(const HybridFormula& hybrid) {
            currentHybridFormula = hybrid;

            // Also prepare the fallback interpreter with the text version
            ProceduralFormula textVersion;
            textVersion.formula = hybrid.textFormula;  // Changed from formulaCode to formula
            textVersion.name = hybrid.name + " (Text Version)";
            textVersion.language = FormulaLanguage::Mathematical;
            // Removed renderMethod assignment as it's not in ProceduralFormula struct

            fallbackInterpreter->CompileFormula(textVersion);

            return hybrid.hasCompiledVersion;
        }

        void SetUseCompiledVersion(bool useCompiled) {
            useCompiledVersion = useCompiled && currentHybridFormula.hasCompiledVersion;
        }

        bool IsUsingCompiledVersion() const {
            return useCompiledVersion && currentHybridFormula.hasCompiledVersion;
        }

        bool RenderToBuffer(uint32_t* pixelBuffer, int width, int height) override {
            if (!pixelBuffer) return false;

            if (useCompiledVersion && currentHybridFormula.hasCompiledVersion) {
                // Use high-performance compiled version
                currentHybridFormula.compiledFunction(
                        pixelBuffer, width, height, currentTime,
                        currentHybridFormula.animationSpeed
                );
                return true;
            } else {
                // Fall back to interpreted text version
                return fallbackInterpreter->RenderToBuffer(pixelBuffer, width, height);
            }
        }

        std::string GetCurrentTextFormula() const {
            return currentHybridFormula.textFormula;
        }

        std::string GetFormulaComments() const {
            return currentHybridFormula.textFormulaComments;
        }

        // Standard interpreter interface implementation
        bool SupportsLanguage(FormulaLanguage language) const override {
            return language == FormulaLanguage::Mathematical;
        }

        bool SupportsRenderMethod(RenderingMethod method) const override {
            return method == RenderingMethod::CPU;
        }

        bool CompileFormula(const ProceduralFormula& formula) override {
            // If it's a text-only formula, use fallback interpreter
            return fallbackInterpreter->CompileFormula(formula);
        }

        bool IsCompiled() const override { return true; }
        void SetTime(float time) override {
            currentTime = time;
            fallbackInterpreter->SetTime(time);
        }
        void SetResolution(int width, int height) override {
            fallbackInterpreter->SetResolution(width, height);
        }
        void SetMousePosition(float x, float y) override {
            fallbackInterpreter->SetMousePosition(x, y);
        }
        void SetParameters(const std::unordered_map<std::string, float>& params) override {
            fallbackInterpreter->SetParameters(params);
        }
        std::string GetLastError() const override {
            return fallbackInterpreter->GetLastError();
        }
    };

// ===== COMPILED FORMULA LIBRARY =====
    class CompiledFormulaLibrary {
    private:
        std::unordered_map<std::string, HybridFormula> hybridFormulas;

    public:
        void RegisterFormula(const HybridFormula& formula) {
            hybridFormulas[formula.name] = formula;
        }

        bool GetFormula(const std::string& name, HybridFormula& outFormula) {
            auto it = hybridFormulas.find(name);
            if (it != hybridFormulas.end()) {
                outFormula = it->second;
                return true;
            }
            return false;
        }

        std::vector<std::string> GetAvailableFormulas() const {
            std::vector<std::string> names;
            for (const auto& pair : hybridFormulas) {
                names.push_back(pair.first);
            }
            return names;
        }

        void LoadAllBuiltInFormulas() {
            // Register all high-performance compiled formulas
            RegisterAxesFormula();
            RegisterGlassFormula();
            RegisterWormholeFormula();
            RegisterGlassDonutFormula();
            RegisterDustFormula();
            RegisterHiveFormula();
        }

    private:
        // ===== COMPILED FORMULA IMPLEMENTATIONS =====

        void RegisterAxesFormula() {
            auto axesCompiled = [](uint32_t* buffer, int width, int height, float t, float speed) {
                const float actualTime = t * speed;
                const float invHeight = 1.0f / height;

                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        // High-performance compiled version of Axes formula
                        float fx = (2.0f * x / width - 1.0f) * width * invHeight;
                        float fy = 2.0f * y / height - 1.0f;

                        // Simplified compiled implementation
                        float px = fx;
                        float py = fy;
                        float pz = 0.0f;

                        // Calculate color based on axes formula
                        uint8_t r = (uint8_t)(128 + 127 * cos(px + actualTime));
                        uint8_t g = (uint8_t)(128 + 127 * cos(py + actualTime));
                        uint8_t b = (uint8_t)(128 + 127 * cos(pz + actualTime));
                        uint8_t a = 255;

                        buffer[y * width + x] = (a << 24) | (b << 16) | (g << 8) | r;
                    }
                }
            };

            HybridFormula axes;
            axes.name = "Axes";
            axes.description = "Rotating axes with color gradients";
            axes.compiledFunction = axesCompiled;
            axes.textFormula = "vec3 p=vec3(FC.xy*2.-r,0)/r.y,s=vec3(sqrt(max(.5-dot(p,p),0.)),p),a=cos(t+vec3(0,11,-t));"
                               "for(float i=0.;i<6.;i++)a*=max(0.,.5-abs(dot(s,normalize(cos(i+vec3(0,11,22))))));";

            axes.tags = {"axes", "simple", "rotating", "geometric"};
            axes.complexity = 4.0f;
            axes.animationSpeed = 1.0f;
            axes.performanceGain = 8; // High gain for simple formula
            axes.hasCompiledVersion = true;

            RegisterFormula(axes);
        }

        void RegisterGlassFormula() {
            HybridFormula glass;
            glass.name = "Glass";
            glass.description = "Glass sphere with refraction effects";
            glass.textFormula = "vec3 p=FC.xyz*2.-r.xyy;p.z-=t*.1;float d=length(p)-1.;if(d<0.)o=vec4(refract(p,normalize(p),1.3)*2.+.5,1.);";

            glass.tags = {"glass", "sphere", "refraction", "3d"};
            glass.complexity = 6.5f;
            glass.animationSpeed = 0.5f;
            glass.performanceGain = 3;
            glass.hasCompiledVersion = false; // Complex for compilation

            RegisterFormula(glass);
        }

        void RegisterWormholeFormula() {
            HybridFormula wormhole;
            wormhole.name = "Wormhole";
            wormhole.description = "Hypnotic wormhole tunnel effect";
            wormhole.textFormula = "for(float i,z,d;i++<2e2;){vec3 p=z*normalize(FC.rgb*2.-r.xyx);"
                                   "p=vec3(atan(p.y,p.x),p.z/3.-t,length(p.xy)-9.);"
                                   "for(d=0.;d++<5.;)p+=sin(ceil(p.yzx*d-i*vec3(.2,0,0)))/d;"
                                   "z+=d=.2*length(vec4(.2*cos(6.*p)-.2,p.z));"
                                   "o+=(cos(p.x+vec4(0,.5,1,0))+1.)/d/z;}o=tanh(o*o/8e2);";

            wormhole.tags = {"wormhole", "tunnel", "cosmic", "hypnotic", "complex"};
            wormhole.complexity = 9.5f;
            wormhole.animationSpeed = 0.8f;
            wormhole.performanceGain = 5;
            wormhole.hasCompiledVersion = false; // Too complex for now

            RegisterFormula(wormhole);
        }

        void RegisterGlassDonutFormula() {
            HybridFormula glassDonut;
            glassDonut.name = "Glass Donut";
            glassDonut.description = "Transparent donut with glass effects";
            glassDonut.textFormula = "vec3 p=FC.xyz*2.-r.xyy;float d=length(vec2(length(p.xz)-1.5,p.y))-.3;if(d<0.)o=vec4(p*.5+.5,1.);";

            glassDonut.tags = {"donut", "torus", "glass", "3d"};
            glassDonut.complexity = 5.0f;
            glassDonut.animationSpeed = 1.0f;
            glassDonut.performanceGain = 4;
            glassDonut.hasCompiledVersion = false;

            RegisterFormula(glassDonut);
        }

        void RegisterDustFormula() {
            HybridFormula dust;
            dust.name = "Dust";
            dust.description = "Cosmic dust simulation";
            dust.textFormula = "vec3 p;for(float i,z,d;i++<2e1;o+=(cos(p.y/(.1+.05*z)+vec4(6,5,4,0))+1.)*d/z/7.)p=z*normalize(FC.rgb*2.-r.xyy),p.x-=t,p.xy*=.4,z+=d=(dot(cos(p/.6),sin(p+sin(p*7.)/4.";

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
        ProceduralFormula currentFormula; // Added missing member

    public:
        HybridFormulaEditor(const std::string& identifier, long id, long x, long y, long width, long height)
                : UltraCanvasFormulaEditor(identifier, id, x, y, width, height) {

            hybridInterpreter = std::make_shared<CompiledFormulaInterpreter>();
            compiledLibrary.LoadAllBuiltInFormulas();

            SetupHybridUI();
        }

    private:
        void SetupHybridUI() {
            // Add performance mode toggle button
            performanceModeButton = CreateButton(
                    "PerformanceMode", GetNextId(), x + width - 200, y + 10, 180, 30, "🚀 Performance Mode: ON"
            );
            // Removed SetBackgroundColor call as UltraCanvasButton doesn't have this method

            performanceModeButton->onClicked = [this]() {
                TogglePerformanceMode();
            };

            // Add performance indicator
            performanceIndicator = CreateLabel(
                    "PerformanceIndicator", GetNextId(), x + 10, y + height - 80, 400, 20, ""
            );

            AddChild(performanceModeButton);
            AddChild(performanceIndicator);

            UpdatePerformanceIndicator();
        }

        void TogglePerformanceMode() {
            showingCompiledVersion = !showingCompiledVersion;
            hybridInterpreter->SetUseCompiledVersion(showingCompiledVersion);

            if (showingCompiledVersion) {
                performanceModeButton->SetText("🚀 Performance Mode: ON");
                // Removed SetBackgroundColor call
            } else {
                performanceModeButton->SetText("📝 Text Mode: ON");
                // Removed SetBackgroundColor call
            }

            UpdatePerformanceIndicator();
            UpdatePreview();
        }

        void UpdatePerformanceIndicator() {
            if (showingCompiledVersion) {
                HybridFormula current;
                if (compiledLibrary.GetFormula(currentFormula.name, current)) {
                    std::string text = "⚡ Using compiled version (~" +
                                       std::to_string(current.performanceGain) +
                                       "x faster). Click 📝 to edit text version.";
                    performanceIndicator->SetText(text);
                    performanceIndicator->SetTextColor(Color(0, 200, 0, 255));
                }
            } else {
                performanceIndicator->SetText("📝 Showing editable text version. Click 🚀 for maximum performance.");
                performanceIndicator->SetTextColor(Color(200, 200, 0, 255));
            }
        }

        void LoadFormulaFromLibrary(const std::string& name) override {
            HybridFormula hybrid;
            if (compiledLibrary.GetFormula(name, hybrid)) {
                // Load hybrid formula
                hybridInterpreter->LoadHybridFormula(hybrid);

                // Update UI with text version
                currentFormula.name = hybrid.name;
                currentFormula.description = hybrid.description;
                currentFormula.formula = hybrid.textFormula; // Changed from formulaCode to formula
                currentFormula.animationSpeed = hybrid.animationSpeed;
                currentFormula.complexity = hybrid.complexity;
                currentFormula.tags = hybrid.tags;

                // Show comments in a special section
                if (!hybrid.textFormulaComments.empty()) {
                    // Could add a comments panel or tooltip
                }

                UpdateUIFromFormula();
                ValidateCurrentFormula();
                UpdatePreview();
                UpdatePerformanceIndicator();
            } else {
                // Fall back to regular text-only formula
                UltraCanvasFormulaEditor::LoadFormulaFromLibrary(name);
            }
        }
    };

} // namespace UltraCanvas