// UltraCanvasProceduralBackgroundPlugin.h
// Procedural background generation system with shader-like formula support
// Version: 1.0.0
// Last Modified: 2025-07-15
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasGraphicsPluginSystem.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>

namespace UltraCanvas {

// ===== PROCEDURAL BACKGROUND TYPES =====
enum class ProceduralBackgroundType {
    Static,         // Single frame generation
    Animated,       // Time-based animation
    Interactive,    // Mouse/input responsive
    Realtime        // Continuous generation
};

enum class FormulaLanguage {
    GLSL,          // OpenGL Shading Language syntax
    HLSL,          // DirectX HLSL syntax  
    Mathematical,   // Pure mathematical expressions
    Custom         // Plugin-defined language
};

enum class RenderingMethod {
    CPU,           // CPU-based calculation
    GPU_Compute,   // GPU compute shaders
    GPU_Fragment,  // GPU fragment shaders
    Hybrid         // CPU + GPU combination
};

// ===== OVERLAY POSITIONING SYSTEM =====
enum class OverlayPosition {
    TopLeft,
    TopCenter,
    TopRight,
    MiddleLeft,
    Center,
    MiddleRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
    Custom          // Use custom X, Y coordinates
};

enum class OverlayAnimation {
    None,
    FadeInOut,      // Opacity animation
    Pulse,          // Scale pulsing
    Rotate,         // Continuous rotation
    Float,          // Gentle floating movement
    Breathe,        // Combined scale and opacity
    Custom          // User-defined animation
};

// ===== PROCEDURAL FORMULA DEFINITION =====
struct ProceduralFormula {
    std::string name;
    std::string description;
    std::string author;
    
    FormulaLanguage language = FormulaLanguage::GLSL;
    RenderingMethod renderMethod = RenderingMethod::CPU;
    ProceduralBackgroundType backgroundType = ProceduralBackgroundType::Static;
    
    std::string formulaCode;
    std::string vertexShader;   // For GPU rendering
    std::string fragmentShader; // For GPU rendering
    
    // Parameters that can be adjusted
    std::unordered_map<std::string, float> floatParameters;
    std::unordered_map<std::string, int> intParameters;
    std::unordered_map<std::string, Color> colorParameters;
    std::unordered_map<std::string, bool> boolParameters;
    
    // Animation properties
    float animationSpeed = 1.0f;
    bool looping = true;
    float duration = 10.0f; // seconds
    
    // Resolution and performance
    int preferredWidth = 1920;
    int preferredHeight = 1080;
    int maxWidth = 3840;
    int maxHeight = 2160;
    float qualityScale = 1.0f; // 0.1 to 2.0
    
    // Metadata
    std::vector<std::string> tags;
    std::string previewImage;
    float complexity = 1.0f; // 0.1 (simple) to 10.0 (very complex)
    
    bool IsValid() const {
        return !name.empty() && !formulaCode.empty();
    }
    
    bool RequiresGPU() const {
        return renderMethod == RenderingMethod::GPU_Compute || 
               renderMethod == RenderingMethod::GPU_Fragment ||
               renderMethod == RenderingMethod::Hybrid;
    }
    
    bool IsAnimated() const {
        return backgroundType == ProceduralBackgroundType::Animated ||
               backgroundType == ProceduralBackgroundType::Interactive ||
               backgroundType == ProceduralBackgroundType::Realtime;
    }
};

// ===== FORMULA INTERPRETER =====
class ProceduralFormulaInterpreter {
public:
    virtual ~ProceduralFormulaInterpreter() = default;
    
    virtual bool SupportsLanguage(FormulaLanguage language) const = 0;
    virtual bool SupportsRenderMethod(RenderingMethod method) const = 0;
    
    virtual bool CompileFormula(const ProceduralFormula& formula) = 0;
    virtual bool IsCompiled() const = 0;
    
    virtual void SetParameters(const std::unordered_map<std::string, float>& params) = 0;
    virtual void SetTime(float time) = 0;
    virtual void SetResolution(int width, int height) = 0;
    virtual void SetMousePosition(float x, float y) = 0;
    
    virtual bool RenderToBuffer(uint32_t* pixelBuffer, int width, int height) = 0;
    virtual std::string GetLastError() const = 0;
};

// ===== CPU-BASED MATHEMATICAL INTERPRETER =====
class CPUMathematicalInterpreter : public ProceduralFormulaInterpreter {
private:
    struct Vec3 {
        float x, y, z;
        Vec3(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}
        Vec3 operator+(const Vec3& v) const { return Vec3(x+v.x, y+v.y, z+v.z); }
        Vec3 operator-(const Vec3& v) const { return Vec3(x-v.x, y-v.y, z-v.z); }
        Vec3 operator*(float s) const { return Vec3(x*s, y*s, z*s); }
        float dot(const Vec3& v) const { return x*v.x + y*v.y + z*v.z; }
        float length() const { return std::sqrt(x*x + y*y + z*z); }
        Vec3 normalize() const { float l = length(); return l > 0 ? *this * (1.0f/l) : *this; }
    };
    
    struct Vec4 {
        float x, y, z, w;
        Vec4(float x = 0, float y = 0, float z = 0, float w = 0) : x(x), y(y), z(z), w(w) {}
    };
    
    ProceduralFormula currentFormula;
    bool compiled = false;
    float currentTime = 0.0f;
    int currentWidth = 0, currentHeight = 0;
    float mouseX = 0.0f, mouseY = 0.0f;
    std::unordered_map<std::string, float> parameters;
    std::string lastError;
    
    // Mathematical functions
    float cos(float x) const { return std::cos(x); }
    float sin(float x) const { return std::sin(x); }
    float tan(float x) const { return std::tan(x); }
    float tanh(float x) const { return std::tanh(x); }
    float dot(const Vec3& a, const Vec3& b) const { return a.dot(b); }
    Vec3 normalize(const Vec3& v) const { return v.normalize(); }
    
public:
    bool SupportsLanguage(FormulaLanguage language) const override {
        return language == FormulaLanguage::Mathematical || 
               language == FormulaLanguage::GLSL; // Basic GLSL subset
    }
    
    bool SupportsRenderMethod(RenderingMethod method) const override {
        return method == RenderingMethod::CPU;
    }
    
    bool CompileFormula(const ProceduralFormula& formula) override {
        currentFormula = formula;
        compiled = true; // For now, assume all formulas compile
        lastError.clear();
        return true;
    }
    
    bool IsCompiled() const override { return compiled; }
    
    void SetParameters(const std::unordered_map<std::string, float>& params) override {
        parameters = params;
    }
    
    void SetTime(float time) override { currentTime = time; }
    void SetResolution(int width, int height) override { 
        currentWidth = width; 
        currentHeight = height; 
    }
    void SetMousePosition(float x, float y) override { 
        mouseX = x; 
        mouseY = y; 
    }
    
    bool RenderToBuffer(uint32_t* pixelBuffer, int width, int height) override {
        if (!compiled || !pixelBuffer) return false;
        
        // Execute the "Dust" formula as an example
        return RenderDustFormula(pixelBuffer, width, height);
    }
    
    std::string GetLastError() const override { return lastError; }
    
private:
    bool RenderDustFormula(uint32_t* pixelBuffer, int width, int height) {
        // Implement the "Dust" formula:
        // vec3 p;for(float i,z,d;i++<2e1;o+=(cos(p.y/(.1+.05*z)+vec4(6,5,4,0))+1.)*d/z/7.)
        // p=z*normalize(FC.rgb*2.-r.xyy),p.x-=t,p.xy*=.4,z+=d=(dot(cos(p/.6),sin(p+sin(p*7.)/4.).zyx)*.4+p.y/.7+.7);
        // o=tanh(o*o);
        
        float t = currentTime; // time variable
        
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                // Fragment coordinate (normalized to -1..1)
                Vec3 FC((float)x / width * 2.0f - 1.0f, 
                       (float)y / height * 2.0f - 1.0f, 0);
                
                Vec3 r(FC.x, FC.y, FC.y); // r.xyy equivalent
                Vec3 o(0, 0, 0); // output color
                
                Vec3 p;
                float i = 0, z = 0, d = 0;
                
                // Main iteration loop (i++<2e1 means i < 20)
                for (i = 0; i < 20; ++i) {
                    // p=z*normalize(FC.rgb*2.-r.xyy)
                    Vec3 FC_rgb = Vec3(FC.x, FC.y, FC.z) * 2.0f - r;
                    p = FC_rgb.normalize() * z;
                    
                    // p.x-=t
                    p.x -= t;
                    
                    // p.xy*=.4
                    p.x *= 0.4f;
                    p.y *= 0.4f;
                    
                    // Complex calculation for d
                    Vec3 cos_p(cos(p.x/0.6f), cos(p.y/0.6f), cos(p.z/0.6f));
                    Vec3 sin_p_complex(sin(p.z + sin(p.z*7.0f)/4.0f), 
                                      sin(p.y + sin(p.y*7.0f)/4.0f), 
                                      sin(p.x + sin(p.x*7.0f)/4.0f));
                    
                    d = dot(cos_p, sin_p_complex) * 0.4f + p.y/0.7f + 0.7f;
                    z += d;
                    
                    // o+=(cos(p.y/(.1+.05*z)+vec4(6,5,4,0))+1.)*d/z/7.
                    if (z > 0) {
                        Vec4 color_offset(6, 5, 4, 0);
                        float cos_term = cos(p.y/(0.1f + 0.05f*z) + color_offset.x) + 1.0f;
                        o.x += cos_term * d / z / 7.0f;
                        
                        cos_term = cos(p.y/(0.1f + 0.05f*z) + color_offset.y) + 1.0f;
                        o.y += cos_term * d / z / 7.0f;
                        
                        cos_term = cos(p.y/(0.1f + 0.05f*z) + color_offset.z) + 1.0f;
                        o.z += cos_term * d / z / 7.0f;
                    }
                }
                
                // o=tanh(o*o)
                o.x = tanh(o.x * o.x);
                o.y = tanh(o.y * o.y);
                o.z = tanh(o.z * o.z);
                
                // Convert to pixel color (0-255 range)
                uint8_t r = (uint8_t)(std::clamp(o.x, 0.0f, 1.0f) * 255);
                uint8_t g = (uint8_t)(std::clamp(o.y, 0.0f, 1.0f) * 255);
                uint8_t b = (uint8_t)(std::clamp(o.z, 0.0f, 1.0f) * 255);
                uint8_t a = 255;
                
                pixelBuffer[y * width + x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
        
        return true;
    }
};

// ===== PROCEDURAL BACKGROUND ELEMENT =====
class UltraCanvasProceduralBackground : public UltraCanvasElement {
private:
    ProceduralFormula currentFormula;
    std::unique_ptr<ProceduralFormulaInterpreter> interpreter;
    
    std::vector<uint32_t> pixelBuffer;
    bool needsRegeneration = true;
    bool isAnimating = false;
    
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastFrameTime;
    
    // Performance settings
    int renderWidth = 512;  // Internal render resolution
    int renderHeight = 512;
    float frameRate = 5.0f;  // Low frame rate for energy efficiency
    bool useThreadedGeneration = true;
    
    // Video cache system for energy efficiency
    bool enableVideoCache = true;
    std::string videoCacheFormat = "heic"; // HEIC animation format
    std::string videoCachePath = "/tmp/ultracanvas_backgrounds/";
    bool useCachedVideo = false;
    std::chrono::seconds videoDuration{30}; // 30 second loops
    
    std::thread generationThread;
    std::atomic<bool> generationInProgress{false};
    std::atomic<bool> shouldStopGeneration{false};
    
    std::thread generationThread;
    std::atomic<bool> generationInProgress{false};
    std::atomic<bool> shouldStopGeneration{false};
    
    // Overlay graphic system
    struct OverlayGraphic {
        std::string imagePath;
        bool enabled = false;
        OverlayPosition position = OverlayPosition::Center;
        float marginX = 50.0f;  // Margin from edges in pixels
        float marginY = 50.0f;
        float scale = 1.0f;     // Scale factor (0.1 to 5.0)
        float opacity = 1.0f;   // Transparency (0.0 to 1.0)
        bool maintainAspectRatio = true;
        int customX = 0;        // For custom positioning
        int customY = 0;
        
        // Animation properties
        bool animated = false;
        float animationSpeed = 1.0f;
        OverlayAnimation animationType = OverlayAnimation::None;
        float animationPhase = 0.0f;
        
        bool IsValid() const {
            return !imagePath.empty() && enabled;
        }
    };
    
    OverlayGraphic overlayGraphic;
    
public:
    UltraCanvasProceduralBackground(int id, int x, int y, int width, int height) 
        : UltraCanvasElement(id, x, y, width, height) {
        
        interpreter = std::make_unique<CPUMathematicalInterpreter>();
        startTime = std::chrono::steady_clock::now();
        
        // Load default "Dust" formula
        LoadDustFormula();
    }
    
    ~UltraCanvasProceduralBackground() {
        StopGeneration();
    }
    
    // ===== FORMULA MANAGEMENT =====
    bool LoadFormula(const ProceduralFormula& formula) {
        if (!interpreter->SupportsLanguage(formula.language) ||
            !interpreter->SupportsRenderMethod(formula.renderMethod)) {
            return false;
        }
        
        StopGeneration();
        
        currentFormula = formula;
        bool compiled = interpreter->CompileFormula(formula);
        
        if (compiled) {
            UpdateRenderResolution();
            needsRegeneration = true;
            
            if (formula.IsAnimated()) {
                StartAnimation();
            }
        }
        
        return compiled;
    }
    
    void LoadDustFormula() {
        ProceduralFormula dustFormula;
        dustFormula.name = "Dust";
        dustFormula.description = "Swirling dust particles in cosmic space";
        dustFormula.author = "UltraCanvas";
        dustFormula.language = FormulaLanguage::Mathematical;
        dustFormula.renderMethod = RenderingMethod::CPU;
        dustFormula.backgroundType = ProceduralBackgroundType::Animated;
        dustFormula.formulaCode = "vec3 p;for(float i,z,d;i++<2e1;o+=(cos(p.y/(.1+.05*z)+vec4(6,5,4,0))+1.)*d/z/7.)p=z*normalize(FC.rgb*2.-r.xyy),p.x-=t,p.xy*=.4,z+=d=(dot(cos(p/.6),sin(p+sin(p*7.)/4.).zyx)*.4+p.y/.7+.7);o=tanh(o*o);";
        dustFormula.animationSpeed = 0.5f;
        dustFormula.complexity = 7.5f;
        dustFormula.tags = {"space", "particles", "cosmic", "animated"};
        
        LoadFormula(dustFormula);
    }
    
    void LoadHiveFormula() {
        ProceduralFormula hiveFormula;
        hiveFormula.name = "Hive";
        hiveFormula.description = "Hexagonal patterns reminiscent of honeycomb structures";
        hiveFormula.author = "UltraCanvas";
        hiveFormula.language = FormulaLanguage::Mathematical;
        hiveFormula.renderMethod = RenderingMethod::CPU;
        hiveFormula.backgroundType = ProceduralBackgroundType::Animated;
        hiveFormula.formulaCode = "vec3 p;for(float i,z,d;i++<2e1;o+=(cos(2e1*p.y/z+vec4(6,1,2,0))+1.)*d/z/4.)p=z*normalize(FC.rgb*2.-r.xyy),p.x-=t,p.xy*=.4,z+=d=max(p.z+6.,dot(cos(p.xy),sin(p.yx/.4)));o=tanh(o*o);";
        hiveFormula.animationSpeed = 0.3f;
        hiveFormula.complexity = 6.8f;
        hiveFormula.tags = {"geometric", "hexagonal", "patterns", "organic"};
        
        LoadFormula(hiveFormula);
    }
    
    void LoadDropletsFormula() {
        ProceduralFormula dropletsFormula;
        dropletsFormula.name = "Droplets";
        dropletsFormula.description = "Flowing water droplets with liquid dynamics";
        dropletsFormula.author = "UltraCanvas";
        dropletsFormula.language = FormulaLanguage::Mathematical;
        dropletsFormula.renderMethod = RenderingMethod::CPU;
        dropletsFormula.backgroundType = ProceduralBackgroundType::Animated;
        dropletsFormula.formulaCode = "for(float i,z,d;i++<1e2;){vec3 p=z*normalize(FC.rgb*2.-r.xyy),a=normalize(cos(vec3(0,2,4)+t/4.));p.z+=9.,a=a*dot(a,p)-cross(a,p);z+=d=.01+.3*abs(max(dot(cos(a),sin(a/.6).yzx),length(a)-7.)+1.5-i/8e1);o+=sin(i/6.+z*vec4(0,1,2,0)/5e1)/d;}o=tanh(1.+o/2e3);";
        dropletsFormula.animationSpeed = 0.4f;
        dropletsFormula.complexity = 8.2f;
        dropletsFormula.tags = {"water", "liquid", "flowing", "organic"};
        
        LoadFormula(dropletsFormula);
    }
    
    void LoadAquifierFormula() {
        ProceduralFormula aquifierFormula;
        aquifierFormula.name = "Aquifier";
        aquifierFormula.description = "Underground water flows and geological formations";
        aquifierFormula.author = "UltraCanvas";
        aquifierFormula.language = FormulaLanguage::Mathematical;
        aquifierFormula.renderMethod = RenderingMethod::CPU;
        aquifierFormula.backgroundType = ProceduralBackgroundType::Animated;
        aquifierFormula.formulaCode = "for(float i,z,d,s;i++<1e2;o+=(cos(.1*i+vec4(0,1,2,0))+2.)/d*z){vec3 p=z*normalize(FC.rgb*2.-r.xyy),a=normalize(cos(vec3(0,2,4)+t-s*.3));p.z+=9.;s=log(length(a=a*dot(a,p)-cross(a,p)))*5.;z+=d=.01+.1*abs(a.x+sin(s/.2-t/.2)*.2);}o=tanh(o/5e4);";
        aquifierFormula.animationSpeed = 0.25f;
        aquifierFormula.complexity = 7.9f;
        aquifierFormula.tags = {"geological", "water", "underground", "natural"};
        
        LoadFormula(aquifierFormula);
    }
    
    void LoadSpinner2Formula() {
        ProceduralFormula spinner2Formula;
        spinner2Formula.name = "Spinner 2";
        spinner2Formula.description = "Rotating spiral patterns with mathematical precision";
        spinner2Formula.author = "UltraCanvas";
        spinner2Formula.language = FormulaLanguage::Mathematical;
        spinner2Formula.renderMethod = RenderingMethod::CPU;
        spinner2Formula.backgroundType = ProceduralBackgroundType::Animated;
        spinner2Formula.formulaCode = "for(float i,z,d;i++<1e2;o+=(cos(i*.2+vec4(0,1,2,0))+1.)/d){vec3 p=z*normalize(FC.rgb*2.-r.xyy),a=normalize(cos(vec3(0,2,3)+t/2.-i/1e2));p.z+=9.;a=abs(a*dot(a,p)-cross(a,p));z+=d=abs(cos(length(a+a)))/4.+a.y/1e2;}o=tanh(o/4e3);";
        spinner2Formula.animationSpeed = 0.6f;
        spinner2Formula.complexity = 6.5f;
        spinner2Formula.tags = {"spiral", "rotation", "geometric", "hypnotic"};
        
        LoadFormula(spinner2Formula);
    }
    
    void LoadSpinnerFormula() {
        ProceduralFormula spinnerFormula;
        spinnerFormula.name = "Spinner";
        spinnerFormula.description = "Classic spinning vortex with dynamic motion";
        spinnerFormula.author = "UltraCanvas";
        spinnerFormula.language = FormulaLanguage::Mathematical;
        spinnerFormula.renderMethod = RenderingMethod::CPU;
        spinnerFormula.backgroundType = ProceduralBackgroundType::Animated;
        spinnerFormula.formulaCode = "for(float i,z,d,s;i++<1e2;o+=(cos(i*.4+vec4(0,1,2,0))+1.)/d){vec3 p=z*normalize(FC.rgb*2.-r.xyy),a=normalize(cos(vec3(0,1,4)+t-.4*s-i/1e2));p.z+=9.;a=abs(a*dot(a,p)-cross(a,p));s=length(a);z+=d=.1*(abs(sin(s*4.-t))+a.y);}o=tanh(o/1e3);";
        spinnerFormula.animationSpeed = 0.7f;
        spinnerFormula.complexity = 7.2f;
        spinnerFormula.tags = {"vortex", "spinning", "dynamic", "mesmerizing"};
        
        LoadFormula(spinnerFormula);
    }
    
    void LoadSmoothWavesFormula() {
        ProceduralFormula smoothWavesFormula;
        smoothWavesFormula.name = "Smooth Waves";
        smoothWavesFormula.description = "Gentle ocean waves with smooth transitions";
        smoothWavesFormula.author = "UltraCanvas";
        smoothWavesFormula.language = FormulaLanguage::Mathematical;
        smoothWavesFormula.renderMethod = RenderingMethod::CPU;
        smoothWavesFormula.backgroundType = ProceduralBackgroundType::Animated;
        smoothWavesFormula.formulaCode = "float f,b,x,y;float3 c=float3(0,.08,.15);for(float i=1;i<8;i++){x=uv.x*1.6+t*sin(i*3.3)*.4+i*4.3;f=.3*cos(i)*sin(x);b=sin(uv.x*sin(i+t*.2)*3.4+cos(i+7.1))*.1+.896;y=smoothstep(b,1,1-abs(uv.y-f))*(b-.8)*2.5;c+=float3(y,y-.04,y*i*.4);}return float4(c,1);";
        smoothWavesFormula.animationSpeed = 0.2f;
        smoothWavesFormula.complexity = 5.5f;
        smoothWavesFormula.tags = {"waves", "ocean", "calm", "soothing"};
        
        LoadFormula(smoothWavesFormula);
    }
    
    void LoadChaosUniverseFormula() {
        ProceduralFormula chaosUniverseFormula;
        chaosUniverseFormula.name = "Chaos Universe";
        chaosUniverseFormula.description = "Chaotic cosmic formations and stellar phenomena";
        chaosUniverseFormula.author = "UltraCanvas";
        chaosUniverseFormula.language = FormulaLanguage::Mathematical;
        chaosUniverseFormula.renderMethod = RenderingMethod::CPU;
        chaosUniverseFormula.backgroundType = ProceduralBackgroundType::Animated;
        chaosUniverseFormula.formulaCode = "for(float i,z,d,n;i++<1e2;o+=(cos(t+vec4(0,2,4,0))+9.-n*8.)/d/d){vec3 p=z*normalize(FC.rgb*2.-r.xyy);p.z+=7.;n=fract(.3*t+pow(fract(d*1e2),.1));p.y+=n*(n+n-3.)/.1+9.;d=length(p);z+=d=.3*length(vec3(d*.3-n*n*n,cos(n+atan(vec2(p.z,d),p.xy)*mat2(4,-3,3,4))*.3));}o=tanh(o/1e4);";
        chaosUniverseFormula.animationSpeed = 0.8f;
        chaosUniverseFormula.complexity = 9.5f;
        chaosUniverseFormula.tags = {"chaos", "universe", "cosmic", "complex"};
        
        LoadFormula(chaosUniverseFormula);
    }
    
    const ProceduralFormula& GetCurrentFormula() const { return currentFormula; }
    
    // ===== ANIMATION CONTROL =====
    void StartAnimation() {
        if (!currentFormula.IsAnimated()) return;
        
        isAnimating = true;
        startTime = std::chrono::steady_clock::now();
        
        if (useThreadedGeneration && !generationInProgress) {
            shouldStopGeneration = false;
            generationThread = std::thread(&UltraCanvasProceduralBackground::AnimationThread, this);
        }
    }
    
    void StopAnimation() {
        isAnimating = false;
        StopGeneration();
    }
    
    void StopGeneration() {
        shouldStopGeneration = true;
        if (generationThread.joinable()) {
            generationThread.join();
        }
        generationInProgress = false;
    }
    
    bool IsAnimating() const { return isAnimating; }
    
    // ===== PARAMETER CONTROL =====
    void SetFloatParameter(const std::string& name, float value) {
        currentFormula.floatParameters[name] = value;
        interpreter->SetParameters(currentFormula.floatParameters);
        needsRegeneration = true;
    }
    
    void SetAnimationSpeed(float speed) {
        currentFormula.animationSpeed = speed;
    }
    
    void SetQuality(float quality) {
        currentFormula.qualityScale = std::clamp(quality, 0.1f, 2.0f);
        UpdateRenderResolution();
        needsRegeneration = true;
    }
    
    // ===== OVERLAY GRAPHIC MANAGEMENT =====
    void SetOverlayGraphic(const std::string& imagePath, OverlayPosition position = OverlayPosition::Center) {
        overlayGraphic.imagePath = imagePath;
        overlayGraphic.position = position;
        overlayGraphic.enabled = !imagePath.empty();
    }
    
    void EnableOverlayGraphic(bool enable) {
        overlayGraphic.enabled = enable;
    }
    
    void SetOverlayPosition(OverlayPosition position, float marginX = 50.0f, float marginY = 50.0f) {
        overlayGraphic.position = position;
        overlayGraphic.marginX = marginX;
        overlayGraphic.marginY = marginY;
    }
    
    void SetOverlayCustomPosition(int x, int y) {
        overlayGraphic.position = OverlayPosition::Custom;
        overlayGraphic.customX = x;
        overlayGraphic.customY = y;
    }
    
    void SetOverlayScale(float scale) {
        overlayGraphic.scale = std::clamp(scale, 0.1f, 5.0f);
    }
    
    void SetOverlayOpacity(float opacity) {
        overlayGraphic.opacity = std::clamp(opacity, 0.0f, 1.0f);
    }
    
    void SetOverlayAnimation(OverlayAnimation animationType, float speed = 1.0f) {
        overlayGraphic.animationType = animationType;
        overlayGraphic.animationSpeed = speed;
        overlayGraphic.animated = (animationType != OverlayAnimation::None);
    }
    
    void SetOverlayMaintainAspectRatio(bool maintain) {
        overlayGraphic.maintainAspectRatio = maintain;
    }
    
    const OverlayGraphic& GetOverlayGraphic() const { return overlayGraphic; }
    
    // ===== ULTRA OS LOGO CONVENIENCE FUNCTIONS =====
    void SetUltraOSLogo(OverlayPosition position = OverlayPosition::BottomRight, 
                       float margin = 50.0f, float scale = 0.8f) {
        SetOverlayGraphic("/system/assets/logos/ultra_os_logo.png", position);
        SetOverlayPosition(position, margin, margin);
        SetOverlayScale(scale);
        SetOverlayOpacity(0.7f); // Slightly transparent for desktop backgrounds
    }
    
    void SetUltraOSLogoAnimated(OverlayPosition position = OverlayPosition::BottomRight,
                               OverlayAnimation animation = OverlayAnimation::Breathe) {
        SetUltraOSLogo(position);
        SetOverlayAnimation(animation, 0.5f); // Gentle animation
    }
    
    // ===== VIDEO CACHE SYSTEM FOR ENERGY EFFICIENCY =====
    void EnableVideoCache(bool enable) {
        enableVideoCache = enable;
    }
    
    void SetVideoCacheFormat(const std::string& format) {
        videoCacheFormat = format; // "heic", "mp4", "webm", etc.
    }
    
    void SetVideoCachePath(const std::string& path) {
        videoCachePath = path;
    }
    
    void SetVideoDuration(int seconds) {
        videoDuration = std::chrono::seconds(seconds);
    }
    
    void SetFrameRate(float fps) {
        frameRate = std::clamp(fps, 1.0f, 60.0f);
    }
    
    bool GenerateVideoCache() {
        if (!currentFormula.IsAnimated()) return false;
        
        std::string cacheFilename = GenerateCacheFilename();
        std::string fullPath = videoCachePath + cacheFilename;
        
        // Check if cache already exists
        if (CacheFileExists(fullPath)) {
            useCachedVideo = true;
            return true;
        }
        
        // Generate video cache
        return CreateVideoCache(fullPath);
    }
    
    bool IsUsingVideoCache() const { return useCachedVideo && enableVideoCache; }
    
    std::string GetCurrentFormulaCacheFile() const {
        return videoCachePath + GenerateCacheFilename();
    }
    
    // ===== FORMULA SELECTION HELPERS =====
    void LoadFormulaByName(const std::string& formulaName) {
        if (formulaName == "Dust") LoadDustFormula();
        else if (formulaName == "Hive") LoadHiveFormula();
        else if (formulaName == "Droplets") LoadDropletsFormula();
        else if (formulaName == "Aquifier") LoadAquifierFormula();
        else if (formulaName == "Spinner 2") LoadSpinner2Formula();
        else if (formulaName == "Spinner") LoadSpinnerFormula();
        else if (formulaName == "Smooth Waves") LoadSmoothWavesFormula();
        else if (formulaName == "Chaos Universe") LoadChaosUniverseFormula();
        else LoadDustFormula(); // Default fallback
        
        // Auto-generate video cache for energy efficiency
        if (enableVideoCache && currentFormula.IsAnimated()) {
            std::thread([this]() {
                GenerateVideoCache();
            }).detach();
        }
    }
    
    std::vector<std::string> GetAvailableFormulas() const {
        return {"Dust", "Hive", "Droplets", "Aquifier", "Spinner 2", 
                "Spinner", "Smooth Waves", "Chaos Universe"};
    }
    
    // ===== RENDERING =====
    void Render() override {
        if (!IsVisible()) return;
        
        ULTRACANVAS_RENDER_SCOPE();
        
        // Update animation time
        if (isAnimating) {
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(
                currentTime - startTime).count();
            
            interpreter->SetTime(elapsed * currentFormula.animationSpeed);
        }
        
        // Use video cache if available for energy efficiency
        if (IsUsingVideoCache()) {
            DrawCachedVideo();
            
            // Draw overlay graphic if enabled
            if (overlayGraphic.IsValid()) {
                DrawOverlayGraphic();
            }
            return;
        }
        
        // Generate frame if needed (fallback to real-time generation)
        if (needsRegeneration && !generationInProgress) {
            if (useThreadedGeneration && isAnimating) {
                // Threading handled by AnimationThread
            } else {
                RegenerateBackground();
            }
        }
        
        // Draw the generated background
        if (!pixelBuffer.empty()) {
            DrawGeneratedBackground();
        }
        
        // Draw overlay graphic if enabled
        if (overlayGraphic.IsValid()) {
            DrawOverlayGraphic();
        }
    }
    
    // ===== EVENT HANDLING =====
    bool HandleEvent(const UCEvent& event) override {
        if (currentFormula.backgroundType == ProceduralBackgroundType::Interactive) {
            if (event.type == UCEventType::MouseMove) {
                float normalizedX = (float)event.x / width;
                float normalizedY = (float)event.y / height;
                interpreter->SetMousePosition(normalizedX, normalizedY);
                needsRegeneration = true;
                return true;
            }
        }
        
        return UltraCanvasElement::HandleEvent(event);
    }
    
private:
    void UpdateRenderResolution() {
        renderWidth = (int)(width * currentFormula.qualityScale);
        renderHeight = (int)(height * currentFormula.qualityScale);
        
        // Clamp to reasonable limits
        renderWidth = std::clamp(renderWidth, 64, currentFormula.maxWidth);
        renderHeight = std::clamp(renderHeight, 64, currentFormula.maxHeight);
        
        pixelBuffer.resize(renderWidth * renderHeight);
        interpreter->SetResolution(renderWidth, renderHeight);
    }
    
    void RegenerateBackground() {
        if (pixelBuffer.empty() || !interpreter->IsCompiled()) return;
        
        generationInProgress = true;
        
        bool success = interpreter->RenderToBuffer(
            pixelBuffer.data(), renderWidth, renderHeight);
        
        if (!success) {
            // Handle error - maybe fill with solid color
            std::fill(pixelBuffer.begin(), pixelBuffer.end(), 0xFF000000);
        }
        
        needsRegeneration = false;
        generationInProgress = false;
    }
    
    void AnimationThread() {
        auto frameDuration = std::chrono::duration<float>(1.0f / frameRate);
        
        while (!shouldStopGeneration && isAnimating) {
            auto frameStart = std::chrono::steady_clock::now();
            
            // Skip generation if using cached video
            if (!IsUsingVideoCache() && needsRegeneration) {
                RegenerateBackground();
            }
            
            auto frameEnd = std::chrono::steady_clock::now();
            auto elapsed = frameEnd - frameStart;
            
            if (elapsed < frameDuration) {
                std::this_thread::sleep_for(frameDuration - elapsed);
            }
        }
    }
    
    void DrawCachedVideo() {
        // In a real implementation, this would:
        // 1. Calculate current time position in the video loop
        // 2. Extract the appropriate frame from the HEIC animation
        // 3. Draw that frame as the background
        
        // Placeholder implementation
        std::string cacheFile = GetCurrentFormulaCacheFile();
        if (CacheFileExists(cacheFile)) {
            // Calculate which frame to show based on time
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(
                currentTime - startTime).count();
            
            float loopTime = static_cast<float>(videoDuration.count());
            float normalizedTime = fmod(elapsed * currentFormula.animationSpeed, loopTime) / loopTime;
            
            // This would use UltraCanvas video/animation support to draw the frame
            // DrawAnimationFrame(cacheFile, normalizedTime);
            
            // For now, fallback to indicating cached video is being used
            SetFillColor(Color(20, 20, 40, 255)); // Dark blue background
            DrawRect(Rect2D(x, y, width, height));
            
            // Add some text to indicate this is cached
            SetTextColor(Colors::White);
            DrawText("CACHED VIDEO: " + currentFormula.name, 
                    Point2D(x + 10, y + 10));
        }
    }
    
    std::string GenerateCacheFilename() const {
        // Generate a unique filename based on formula and parameters
        std::string filename = currentFormula.name;
        filename += "_" + std::to_string(renderWidth) + "x" + std::to_string(renderHeight);
        filename += "_" + std::to_string(static_cast<int>(frameRate)) + "fps";
        filename += "_" + std::to_string(videoDuration.count()) + "s";
        filename += "_" + std::to_string(static_cast<int>(currentFormula.animationSpeed * 100));
        
        // Add overlay info if present
        if (overlayGraphic.IsValid()) {
            filename += "_overlay";
        }
        
        filename += "." + videoCacheFormat;
        
        // Replace spaces and special characters
        std::replace(filename.begin(), filename.end(), ' ', '_');
        
        return filename;
    }
    
    bool CacheFileExists(const std::string& filepath) const {
        // In a real implementation, check if file exists
        // std::ifstream file(filepath);
        // return file.good();
        return false; // For now, always regenerate
    }
    
    bool CreateVideoCache(const std::string& outputPath) {
        // This would be a complex implementation that:
        // 1. Generates frames for the entire video duration
        // 2. Encodes them into HEIC animation format
        // 3. Saves to the cache path
        
        float totalFrames = frameRate * videoDuration.count();
        float timeStep = 1.0f / frameRate;
        
        // Create cache directory if needed
        // CreateDirectoryIfNotExists(videoCachePath);
        
        std::vector<std::vector<uint32_t>> frames;
        frames.reserve(static_cast<size_t>(totalFrames));
        
        // Generate all frames
        for (float t = 0; t < videoDuration.count(); t += timeStep) {
            interpreter->SetTime(t * currentFormula.animationSpeed);
            
            std::vector<uint32_t> frameBuffer(renderWidth * renderHeight);
            bool success = interpreter->RenderToBuffer(
                frameBuffer.data(), renderWidth, renderHeight);
            
            if (success) {
                frames.push_back(std::move(frameBuffer));
            }
        }
        
        // Encode to HEIC animation
        // This would require HEIC encoding library integration
        // bool success = EncodeHEICAnimation(frames, outputPath, frameRate);
        
        return true; // Placeholder - assume success
    }
    
    void DrawOverlayGraphic() {
        if (!overlayGraphic.IsValid()) return;
        
        // Update animation if enabled
        if (overlayGraphic.animated) {
            UpdateOverlayAnimation();
        }
        
        // Calculate overlay position
        Point2D overlayPos = CalculateOverlayPosition();
        
        // Calculate overlay size (this would need image dimensions from UltraCanvas image system)
        // For now, assume we can get image dimensions
        int imageWidth = 200;   // These would come from loading the actual image
        int imageHeight = 200;  // GetImageDimensions(overlayGraphic.imagePath)
        
        float scaledWidth = imageWidth * overlayGraphic.scale;
        float scaledHeight = imageHeight * overlayGraphic.scale;
        
        if (!overlayGraphic.maintainAspectRatio) {
            // Could allow separate width/height scaling here
        }
        
        // Apply animation transformations
        float finalOpacity = overlayGraphic.opacity;
        float finalScale = overlayGraphic.scale;
        float rotation = 0.0f;
        Point2D finalPos = overlayPos;
        
        ApplyOverlayAnimationTransforms(finalOpacity, finalScale, rotation, finalPos);
        
        // Set rendering properties
        SetGlobalAlpha(finalOpacity);
        
        // For rotation, we'd need transform support:
        // if (rotation != 0.0f) {
        //     PushTransform();
        //     Translate(finalPos.x + scaledWidth/2, finalPos.y + scaledHeight/2);
        //     Rotate(rotation);
        //     Translate(-scaledWidth/2, -scaledHeight/2);
        //     DrawImage(overlayGraphic.imagePath, Rect2D(0, 0, scaledWidth * finalScale, scaledHeight * finalScale));
        //     PopTransform();
        // } else {
        
        // Simple case without rotation
        DrawImage(overlayGraphic.imagePath, 
                 Rect2D(finalPos.x, finalPos.y, 
                       scaledWidth * finalScale, scaledHeight * finalScale));
        
        // Reset alpha
        SetGlobalAlpha(1.0f);
    }
    
    Point2D CalculateOverlayPosition() {
        // These would be actual image dimensions in a real implementation
        int imageWidth = 200;  
        int imageHeight = 200;
        
        float scaledWidth = imageWidth * overlayGraphic.scale;
        float scaledHeight = imageHeight * overlayGraphic.scale;
        
        Point2D position;
        
        switch (overlayGraphic.position) {
            case OverlayPosition::TopLeft:
                position.x = x + overlayGraphic.marginX;
                position.y = y + overlayGraphic.marginY;
                break;
                
            case OverlayPosition::TopCenter:
                position.x = x + (width - scaledWidth) / 2;
                position.y = y + overlayGraphic.marginY;
                break;
                
            case OverlayPosition::TopRight:
                position.x = x + width - scaledWidth - overlayGraphic.marginX;
                position.y = y + overlayGraphic.marginY;
                break;
                
            case OverlayPosition::MiddleLeft:
                position.x = x + overlayGraphic.marginX;
                position.y = y + (height - scaledHeight) / 2;
                break;
                
            case OverlayPosition::Center:
                position.x = x + (width - scaledWidth) / 2;
                position.y = y + (height - scaledHeight) / 2;
                break;
                
            case OverlayPosition::MiddleRight:
                position.x = x + width - scaledWidth - overlayGraphic.marginX;
                position.y = y + (height - scaledHeight) / 2;
                break;
                
            case OverlayPosition::BottomLeft:
                position.x = x + overlayGraphic.marginX;
                position.y = y + height - scaledHeight - overlayGraphic.marginY;
                break;
                
            case OverlayPosition::BottomCenter:
                position.x = x + (width - scaledWidth) / 2;
                position.y = y + height - scaledHeight - overlayGraphic.marginY;
                break;
                
            case OverlayPosition::BottomRight:
                position.x = x + width - scaledWidth - overlayGraphic.marginX;
                position.y = y + height - scaledHeight - overlayGraphic.marginY;
                break;
                
            case OverlayPosition::Custom:
                position.x = x + overlayGraphic.customX;
                position.y = y + overlayGraphic.customY;
                break;
        }
        
        return position;
    }
    
    void UpdateOverlayAnimation() {
        if (!overlayGraphic.animated) return;
        
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(
            currentTime - startTime).count();
        
        overlayGraphic.animationPhase = elapsed * overlayGraphic.animationSpeed;
    }
    
    void ApplyOverlayAnimationTransforms(float& opacity, float& scale, float& rotation, Point2D& position) {
        if (!overlayGraphic.animated) return;
        
        float phase = overlayGraphic.animationPhase;
        
        switch (overlayGraphic.animationType) {
            case OverlayAnimation::FadeInOut:
                opacity *= (sin(phase) * 0.3f + 0.7f); // Oscillate between 0.4 and 1.0
                break;
                
            case OverlayAnimation::Pulse:
                scale *= (sin(phase) * 0.1f + 1.0f); // Oscillate between 0.9 and 1.1
                break;
                
            case OverlayAnimation::Rotate:
                rotation = phase; // Continuous rotation
                break;
                
            case OverlayAnimation::Float:
                position.y += sin(phase) * 5.0f; // Gentle vertical floating
                position.x += cos(phase * 0.7f) * 2.0f; // Subtle horizontal movement
                break;
                
            case OverlayAnimation::Breathe:
                opacity *= (sin(phase * 0.8f) * 0.2f + 0.8f); // Breathe opacity
                scale *= (sin(phase * 0.6f) * 0.05f + 1.0f);   // Breathe scale
                break;
                
            case OverlayAnimation::Custom:
                // User could define custom animation logic here
                break;
                
            default:
                break;
        }
    }
    
    void DrawGeneratedBackground() {
        // For now, implement a simple pixel-by-pixel drawing
        // In a real implementation, this would be optimized
        // to upload to a texture and draw as a quad
        
        for (int y = 0; y < height && y < renderHeight; ++y) {
            for (int x = 0; x < width && x < renderWidth; ++x) {
                uint32_t pixel = pixelBuffer[y * renderWidth + x];
                
                Color color(
                    (pixel >> 16) & 0xFF, // R
                    (pixel >> 8) & 0xFF,  // G
                    pixel & 0xFF,         // B
                    (pixel >> 24) & 0xFF  // A
                );
                
                SetFillColor(color);
                DrawRect(Rect2D(this->x + x, this->y + y, 1, 1));
            }
        }
    }
};

// ===== PROCEDURAL BACKGROUND PLUGIN =====
class ProceduralBackgroundPlugin : public IGraphicsPlugin {
public:
    std::string GetPluginName() const override {
        return "UltraCanvas Procedural Background Plugin";
    }
    
    std::string GetPluginVersion() const override {
        return "1.0.0";
    }
    
    std::string GetDescription() const override {
        return "Procedural background generation with mathematical formulas";
    }
    
    std::vector<std::string> GetSupportedExtensions() const override {
        return {"pbg", "proc", "shader", "formula"};
    }
    
    bool CanHandle(const std::string& extension) const override {
        auto extensions = GetSupportedExtensions();
        return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
    }
    
    bool CanHandle(const GraphicsFileInfo& fileInfo) const override {
        return fileInfo.formatType == GraphicsFormatType::Procedural;
    }
    
    std::shared_ptr<UltraCanvasElement> LoadGraphics(const std::string& filePath) override {
        // Load formula from file and create background element
        auto background = std::make_shared<UltraCanvasProceduralBackground>(
            rand(), 0, 0, 1920, 1080);
        
        // TODO: Parse formula file and load into background
        
        return background;
    }
    
    std::shared_ptr<UltraCanvasElement> LoadGraphics(const GraphicsFileInfo& fileInfo) override {
        return LoadGraphics(fileInfo.filename);
    }
    
    std::shared_ptr<UltraCanvasElement> CreateGraphics(int width, int height, GraphicsFormatType type) override {
        if (type == GraphicsFormatType::Procedural) {
            return std::make_shared<UltraCanvasProceduralBackground>(
                rand(), 0, 0, width, height);
        }
        return nullptr;
    }
    
    GraphicsManipulation GetSupportedManipulations() const override {
        return GraphicsManipulation::Move | GraphicsManipulation::Scale;
    }
    
    GraphicsFileInfo GetFileInfo(const std::string& filePath) override {
        GraphicsFileInfo info(filePath);
        info.formatType = GraphicsFormatType::Procedural;
        info.supportedManipulations = GetSupportedManipulations();
        return info;
    }
    
    bool ValidateFile(const std::string& filePath) override {
        return CanHandle(GraphicsFileInfo(filePath).extension);
    }
};

// ===== CONVENIENCE FUNCTIONS =====
inline std::shared_ptr<UltraCanvasProceduralBackground> CreateProceduralBackground(
    int id, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasProceduralBackground>(id, x, y, width, height);
}

inline std::shared_ptr<UltraCanvasProceduralBackground> CreateDustBackground(
    int id, int x, int y, int width, int height) {
    auto background = CreateProceduralBackground(id, x, y, width, height);
    background->LoadDustFormula();
    return background;
}

inline std::shared_ptr<UltraCanvasProceduralBackground> CreateUltraOSDesktopBackground(
    int id, int x, int y, int width, int height, 
    OverlayPosition logoPosition = OverlayPosition::BottomRight) {
    auto background = CreateDustBackground(id, x, y, width, height);
    background->SetUltraOSLogoAnimated(logoPosition);
    return background;
}

inline std::shared_ptr<UltraCanvasProceduralBackground> CreateCustomOverlayBackground(
    int id, int x, int y, int width, int height,
    const std::string& overlayImagePath, OverlayPosition position = OverlayPosition::Center) {
    auto background = CreateDustBackground(id, x, y, width, height);
    background->SetOverlayGraphic(overlayImagePath, position);
    background->SetOverlayScale(0.5f);
    background->SetOverlayOpacity(0.8f);
    return background;
}

// ===== ENERGY-EFFICIENT BACKGROUND CREATION =====
inline std::shared_ptr<UltraCanvasProceduralBackground> CreateEfficientBackground(
    int id, int x, int y, int width, int height, const std::string& formulaName,
    bool enableCache = true, float fps = 5.0f) {
    auto background = CreateProceduralBackground(id, x, y, width, height);
    background->LoadFormulaByName(formulaName);
    background->SetFrameRate(fps);
    background->EnableVideoCache(enableCache);
    return background;
}

inline std::shared_ptr<UltraCanvasProceduralBackground> CreateEfficientUltraOSBackground(
    int id, int x, int y, int width, int height, const std::string& formulaName = "Dust") {
    auto background = CreateEfficientBackground(id, x, y, width, height, formulaName);
    background->SetUltraOSLogoAnimated(OverlayPosition::BottomRight, OverlayAnimation::Breathe);
    return background;
}

// ===== FORMULA-SPECIFIC CONVENIENCE FUNCTIONS =====
inline std::shared_ptr<UltraCanvasProceduralBackground> CreateHiveBackground(
    int id, int x, int y, int width, int height) {
    return CreateEfficientBackground(id, x, y, width, height, "Hive");
}

inline std::shared_ptr<UltraCanvasProceduralBackground> CreateDropletsBackground(
    int id, int x, int y, int width, int height) {
    return CreateEfficientBackground(id, x, y, width, height, "Droplets");
}

inline std::shared_ptr<UltraCanvasProceduralBackground> CreateAquifierBackground(
    int id, int x, int y, int width, int height) {
    return CreateEfficientBackground(id, x, y, width, height, "Aquifier");
}

inline std::shared_ptr<UltraCanvasProceduralBackground> CreateSpinnerBackground(
    int id, int x, int y, int width, int height, bool variant2 = false) {
    std::string formulaName = variant2 ? "Spinner 2" : "Spinner";
    return CreateEfficientBackground(id, x, y, width, height, formulaName);
}

inline std::shared_ptr<UltraCanvasProceduralBackground> CreateSmoothWavesBackground(
    int id, int x, int y, int width, int height) {
    return CreateEfficientBackground(id, x, y, width, height, "Smooth Waves");
}

inline std::shared_ptr<UltraCanvasProceduralBackground> CreateChaosUniverseBackground(
    int id, int x, int y, int width, int height) {
    return CreateEfficientBackground(id, x, y, width, height, "Chaos Universe");
}

// ===== PLUGIN REGISTRATION =====
inline void RegisterProceduralBackgroundPlugin() {
    UltraCanvasGraphicsPluginRegistry::RegisterPlugin(
        std::make_shared<ProceduralBackgroundPlugin>());
}