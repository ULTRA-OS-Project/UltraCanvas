// UltraCanvasProceduralBackgroundPlugin.h
// Procedural background generation with mathematical formulas
// Version: 1.2.6
// Last Modified: 2025-08-17
// Author: UltraCanvas Framework

// Fixed X11 constant clashes:
// - Changed OverlayAnimation::None to OverlayAnimation::NoAnimation
// - Uses proper UltraCanvas rendering interface
// - Compatible with Linux X11 environment
// - Correctly inherits from IGraphicsPlugin interface

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasGraphicsPluginSystem.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <random>

namespace UltraCanvas {

// ===== ENUMS AND STRUCTURES =====
    enum class FormulaLanguage {
        Mathematical,    // Basic math expressions
        GLSL,           // GLSL-like syntax
        JavaScript,     // JavaScript expressions
        Custom          // Plugin-defined language
    };

    enum class RenderingMethod {
        CPU,            // Software rendering
        GPU_OpenGL,     // OpenGL shaders
        GPU_Vulkan,     // Vulkan compute
        Hybrid          // CPU + GPU optimization
    };

    enum class ProceduralBackgroundType {
        Static,         // One-time generation
        Animated,       // Time-based animation
        Interactive,    // Mouse/keyboard responsive
        Realtime        // Continuous updates
    };

    enum class OverlayPosition {
        TopLeft, TopCenter, TopRight,
        MiddleLeft, MiddleCenter, MiddleRight,
        BottomLeft, BottomCenter, BottomRight,
        Custom
    };

    enum class OverlayAnimation {
        NoAnimation,    // Changed from "None" to avoid X11 clash
        FadeInOut,      // Opacity animation
        Pulse,          // Scale pulsing
        Rotate,         // Continuous rotation
        Float,          // Gentle floating movement
        Breathe,        // Scale and opacity breathing
        Custom          // User-defined animation
    };

// ===== OVERLAY GRAPHIC STRUCTURE =====
    struct OverlayGraphic {
        std::string imagePath;
        OverlayPosition position = OverlayPosition::BottomRight;
        OverlayAnimation animation = OverlayAnimation::NoAnimation;
        float opacity = 0.8f;
        float scale = 1.0f;
        int marginX = 20;
        int marginY = 20;
        bool enabled = false;

        // Animation properties
        float animationSpeed = 1.0f;
        float animationPhase = 0.0f;

        OverlayGraphic() = default;
        OverlayGraphic(const std::string& path, OverlayPosition pos = OverlayPosition::BottomRight)
                : imagePath(path), position(pos), enabled(true) {}
    };

// ===== PROCEDURAL FORMULA STRUCTURE =====
    struct ProceduralFormula {
        std::string name;
        std::string description;
        std::string author;
        std::vector<std::string> tags;

        FormulaLanguage language = FormulaLanguage::Mathematical;
        RenderingMethod preferredMethod = RenderingMethod::CPU;
        ProceduralBackgroundType backgroundType = ProceduralBackgroundType::Static;

        std::string formula;
        std::unordered_map<std::string, float> defaultParameters;

        float complexity = 1.0f;        // 0.0-10.0 scale
        float qualityScale = 1.0f;      // Resolution multiplier
        float animationSpeed = 1.0f;    // Time multiplier for animations

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

        virtual bool RenderToBuffer(UltraCanvasPixelBuffer& pixelBuffer, int width, int height) = 0;
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
            Vec3 normalize() const { float l = length(); return l > 0 ?
                                                                *this * (1.0f/l) : *this; }
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

        bool RenderToBuffer(UltraCanvasPixelBuffer& pixelBuffer, int width, int height) override {
            if (!compiled || !pixelBuffer.IsValid()) return false;

            // Execute the "Dust" formula as an example
            //return RenderWorkHoleFormula(pixelBuffer, width, height);
            return RenderAxesFormula(pixelBuffer, width, height);
        }

        std::string GetLastError() const override { return lastError; }

    private:


        bool RenderWorkHoleFormula(UltraCanvasPixelBuffer& pixelBuffer, int width, int height) {
            const float actualTime = currentTime;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    float fx = 2.0f * x / width - 1.0f;
                    float fy = 2.0f * y / height - 1.0f;
                    float angle = std::atan2(fy, fx);
                    float radius = std::sqrt(fx*fx + fy*fy);

                    float intensity = std::sin(angle * 8.0f + radius * 10.0f - actualTime * 2.0f) * 0.5f + 0.5f;
                    uint8_t gray = static_cast<uint8_t>(intensity * 255.0f);
                    pixelBuffer.SetPixel(x, y, (0xFF000000 | (gray << 16) | (gray << 8) | gray));
                }
            }
        }

        bool RenderGlassFormula(UltraCanvasPixelBuffer& pixelBuffer, int width, int height) {
            const float actualTime = currentTime;
            const float invAspect = static_cast<float>(height) / width;

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    // Compiled version of Glass formula
                    // vec2 p=(FC.xy*2.-r)/r.y/.9;float l=length(p)-1.;
                    // o=.5+.5*tanh(.1/max(l/.1,-l)-sin(l+p.y*max(1.,-l/.1)+t+vec4(0,1,2,0)));

                    float px = ((2.0f * x / width - 1.0f) * width / height) / 0.9f;
                    float py = (2.0f * y / height - 1.0f) / 0.9f;

                    float l = std::sqrt(px*px + py*py) - 1.0f;
                    float l_div_01 = l / 0.1f;
                    float max_term = std::max(l_div_01, -l);

                    float sin_arg = l + py * std::max(1.0f, -l_div_01) + actualTime;
                    float sin_val = std::sin(sin_arg);

                    float tanh_arg = 0.1f / max_term - sin_val;
                    float result = 0.5f + 0.5f * std::tanh(tanh_arg);

                    uint8_t gray = static_cast<uint8_t>(std::clamp(result * 255.0f, 0.0f, 255.0f));
                    pixelBuffer.SetPixel(x, y, (0xFF000000 | (gray << 16) | (gray << 8) | gray));
                }
            }
        }

        bool RenderAxesFormula(UltraCanvasPixelBuffer& pixelBuffer, int width, int height) {
            // axes
//            const float actualTime = t * speed;
            const float actualTime = currentTime;
            const float invHeight = 1.0f / height;

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    // High-performance compiled version of Axes formula
                    // vec3 p=vec3(FC.xy*2.-r,0)/r.y,s=vec3(sqrt(max(.5-dot(p,p),0.)),p),a=cos(t+vec3(0,11,-t))

                    float fx = (2.0f * x / width - 1.0f) * width * invHeight;
                    float fy = 2.0f * y / height - 1.0f;

                    // p = vec3(FC.xy*2.-r,0)/r.y
                    float px = fx;
                    float py = fy;
                    float pz = 0.0f;

                    // s = vec3(sqrt(max(.5-dot(p,p),0.)), p)
                    float dot_pp = px * px + py * py + pz * pz;
                    float sx = std::sqrt(std::max(0.5f - dot_pp, 0.0f));
                    float sy = px;
                    float sz = py;

                    // a = cos(t+vec3(0,11,-t))
                    float ax = std::cos(actualTime);
                    float ay = std::cos(actualTime + 11.0f);
                    float az = std::cos(actualTime - actualTime); // cos(0) = 1

                    // a*dot(a,s)
                    float dot_as = ax * sx + ay * sy + az * sz;
                    float term1_x = ax * dot_as;
                    float term1_y = ay * dot_as;
                    float term1_z = az * dot_as;

                    // cross(a,s)
                    float cross_x = ay * sz - az * sy;
                    float cross_y = az * sx - ax * sz;
                    float cross_z = ax * sy - ay * sx;

                    // mix(a*dot(a,s),s,.8)
                    float mix_x = term1_x + 0.8f * (sx - term1_x);
                    float mix_y = term1_y + 0.8f * (sy - term1_y);
                    float mix_z = term1_z + 0.8f * (sz - term1_z);

                    // .6*cross(a,s)
                    float scaled_cross_x = 0.6f * cross_x;
                    float scaled_cross_y = 0.6f * cross_y;
                    float scaled_cross_z = 0.6f * cross_z;

                    // mix(...) - .6*cross(a,s)
                    float diff_x = mix_x - scaled_cross_x;
                    float diff_y = mix_y - scaled_cross_y;
                    float diff_z = mix_z - scaled_cross_z;

                    // abs(...)
                    float abs_x = std::abs(diff_x);
                    float abs_y = std::abs(diff_y);
                    float abs_z = std::abs(diff_z);

                    // .1/abs(...)/(1.+dot(p,p))
                    float denominator = (1.0f + dot_pp);
                    float r = 0.1f / abs_x / denominator;
                    float g = 0.1f / abs_y / denominator;
                    float b = 0.1f / abs_z / denominator;

                    // o=tanh(o+length(o*.2))
                    float length_scaled = std::sqrt(r * r * 0.04f + g * g * 0.04f + b * b * 0.04f);
                    r = std::tanh(r + length_scaled);
                    g = std::tanh(g + length_scaled);
                    b = std::tanh(b + length_scaled);

                    // Clamp and convert to 8-bit
                    uint8_t red = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
                    uint8_t green = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
                    uint8_t blue = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));

                    pixelBuffer.SetPixel(x, y, (0xFF000000 | (red << 16) | (green << 8) | blue));
                }
            }

            return true;
        }

        bool RenderDustFormula(UltraCanvasPixelBuffer& pixelBuffer, int width, int height) {
            // Implement the "Dust" formula:
            // vec3 p;for(float i,z,d;i++<2e1;o+=(cos(p.y/(.1+.05*z)+vec4(6,5,4,0))+1.)*d/z/7.)
            // p=z*normalize(FC.rgb*2.-r.xyy),p.x-=t,p.xy*=.4,z+=d=(dot(cos(p/.6),sin(p+sin(p*7.)/4.

            // axes
//            const float actualTime = t * speed;
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
                    uint8_t red = (uint8_t)(std::clamp(o.x, 0.0f, 1.0f) * 255);
                    uint8_t g = (uint8_t)(std::clamp(o.y, 0.0f, 1.0f) * 255);
                    uint8_t b = (uint8_t)(std::clamp(o.z, 0.0f, 1.0f) * 255);
                    uint8_t a = 255;

                    pixelBuffer.SetPixel(x,y, (a << 24) | (red << 16) | (g << 8) | b);
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

        UltraCanvasPixelBuffer pixelBuffer;
        bool needsRegeneration = true;
        bool isAnimating = true;

        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point lastFrameTime;

        // Performance tracking
        int renderWidth = 100;
        int renderHeight = 100;
        float frameTime = 0.0f;
        int frameCount = 0;

        // Overlay support
        OverlayGraphic overlayGraphic;

        // Video recording (for animated backgrounds)
        bool isRecordingVideo = false;
        std::vector<UltraCanvasPixelBuffer> cachedFrames;
        int currentFrame = 0;
        int maxCachedFrames = 300; // 10 seconds at 30fps

    public:
        // Fixed constructor to match UltraCanvasElement base class signature
        UltraCanvasProceduralBackground(const std::string& identifier, long id = 0,
                                        long x = 0, long y = 0, long w = 100, long h = 30)
                : UltraCanvasElement(identifier, id, x, y, w, h) {
            interpreter = std::make_unique<CPUMathematicalInterpreter>();
            startTime = std::chrono::steady_clock::now();
            lastFrameTime = startTime;

            // Load default "Dust" formula
            LoadDustFormula();
            UpdateRenderResolution();
        }

        // Convenience constructor for old-style usage
        UltraCanvasProceduralBackground(int id, int x, int y, int width, int height)
                : UltraCanvasProceduralBackground("ProceduralBG_" + std::to_string(id), id, x, y, width, height) {
        }

        virtual ~UltraCanvasProceduralBackground() = default;

        // ===== FORMULA MANAGEMENT =====
        void LoadDustFormula() {
            ProceduralFormula dustFormula;
            dustFormula.name = "Dust";
            dustFormula.description = "Cosmic dust simulation with swirling patterns";
            dustFormula.author = "UltraCanvas Team";
            dustFormula.tags = {"space", "dust", "cosmic", "animated"};
            dustFormula.language = FormulaLanguage::Mathematical;
            dustFormula.backgroundType = ProceduralBackgroundType::Animated;
            dustFormula.complexity = 7.5f;
            dustFormula.qualityScale = 0.5f; // Render at half resolution for performance
            dustFormula.animationSpeed = 1.0f;
            dustFormula.formula = "vec3 p;for(float i,z,d;i++<2e1;o+=(cos(p.y/(.1+.05*z)+vec4(6,5,4,0))+1.)*d/z/7.)p=z*normalize(FC.rgb*2.-r.xyy),p.x-=t,p.xy*=.4,z+=d=(dot(cos(p/.6),sin(p+sin(p*7.)/4.";

            SetFormula(dustFormula);
        }

        void SetFormula(const ProceduralFormula& formula) {
            currentFormula = formula;
            if (interpreter && interpreter->SupportsLanguage(formula.language)) {
                interpreter->CompileFormula(formula);
            }
            UpdateRenderResolution();
            needsRegeneration = true;

            isAnimating = formula.IsAnimated();
            if (isAnimating) {
                startTime = std::chrono::steady_clock::now();
            }
        }

        const ProceduralFormula& GetFormula() const { return currentFormula; }

        // ===== OVERLAY MANAGEMENT =====
        void SetOverlay(const OverlayGraphic& overlay) {
            overlayGraphic = overlay;
            needsRegeneration = true;
        }

        void EnableUltraOSLogo(OverlayPosition position = OverlayPosition::BottomRight) {
            overlayGraphic.imagePath = "assets/ultraos_logo.png";
            overlayGraphic.position = position;
            overlayGraphic.opacity = 0.8f;
            overlayGraphic.scale = 1.0f;
            overlayGraphic.animation = OverlayAnimation::Float;
            overlayGraphic.enabled = true;
        }

        // ===== RENDERING =====
        void Render() override {
            auto ctx = GetRenderContext();
            if (!interpreter || !interpreter->IsCompiled() || !ctx) return;

            auto currentTime = std::chrono::steady_clock::now();
            ctx->PushState();
            if (isAnimating || needsRegeneration) {
                auto elapsed = std::chrono::duration<float>(currentTime - startTime).count();
                interpreter->SetTime(elapsed * currentFormula.animationSpeed);

                if (isRecordingVideo && cachedFrames.size() < static_cast<size_t>(maxCachedFrames)) {
                    GenerateAndCacheFrame();
                } else if (!isRecordingVideo) {
                    GenerateBackground();
                }

                needsRegeneration = false;
            }

            if (isRecordingVideo && !cachedFrames.empty()) {
                DrawCachedVideo(ctx);
            } else {
                DrawGeneratedBackground(ctx);
            }

            if (overlayGraphic.enabled) {
                DrawOverlay(ctx);
            }
            ctx->PopState();
            frameCount++;
            lastFrameTime = currentTime;
        }

        // Fixed OnEvent method to match base class
        bool OnEvent(const UCEvent& event) override {
            if (event.type == UCEventType::MouseMove && currentFormula.backgroundType == ProceduralBackgroundType::Interactive) {
                // Access width and height from base class properties
                float normalizedX = (float)event.x / GetWidth();
                float normalizedY = (float)event.y / GetHeight();
                if (interpreter) {
                    interpreter->SetMousePosition(normalizedX, normalizedY);
                    needsRegeneration = true;
                }
            }

            // Call base class event handling
            return false; // Let parent handle the event
        }

        void UpdateRenderResolution() {
            renderWidth = (int)(GetWidth() * currentFormula.qualityScale);
            renderHeight = (int)(GetHeight() * currentFormula.qualityScale);

            if (renderWidth <= 0) renderWidth = 1;
            if (renderHeight <= 0) renderHeight = 1;

            pixelBuffer.Init(renderWidth, renderHeight);

            if (interpreter) {
                interpreter->SetResolution(renderWidth, renderHeight);
            }
        }

        // ===== PERFORMANCE TRACKING =====
        float GetFrameTime() const { return frameTime; }
        int GetFrameCount() const { return frameCount; }
        float GetFPS() const {
            auto elapsed = std::chrono::duration<float>(lastFrameTime - startTime).count();
            return elapsed > 0 ? frameCount / elapsed : 0;
        }

        // ===== VIDEO CACHING =====
        void StartVideoRecording(int maxFrames = 300) {
            maxCachedFrames = maxFrames;
            cachedFrames.clear();
            currentFrame = 0;
            isRecordingVideo = true;
        }

        void StopVideoRecording() {
            isRecordingVideo = false;
        }

        bool IsRecordingVideo() const { return isRecordingVideo; }
        int GetCachedFrameCount() const { return cachedFrames.size(); }

    private:
        void GenerateBackground() {
            if (!interpreter || !pixelBuffer.IsValid()) return;

            auto start = std::chrono::high_resolution_clock::now();

            interpreter->RenderToBuffer(pixelBuffer, renderWidth, renderHeight);

            auto end = std::chrono::high_resolution_clock::now();
            frameTime = std::chrono::duration<float>(end - start).count();
        }

        void GenerateAndCacheFrame() {
            GenerateBackground();
            if (pixelBuffer.IsValid()) {
                cachedFrames.push_back(pixelBuffer);
            }
        }

        void DrawCachedVideo(IRenderContext* ctx) {
            if (cachedFrames.empty()) return;

            // Cycle through cached frames
            auto elapsed = std::chrono::duration<float>(
                    std::chrono::steady_clock::now() - startTime).count();
            int frameIndex = (int)(elapsed * 30.0f) % cachedFrames.size(); // 30 FPS

            // Use the cached frame as pixelBuffer
            if (static_cast<size_t>(frameIndex) < cachedFrames.size()) {
                pixelBuffer = cachedFrames[frameIndex];

//                ctx->SetFillColor(Colors::Black);
//                ctx->FillRectangle(Rect2Di(GetX(), GetY(), GetWidth(), GetHeight()));
                ctx->PaintPixelBuffer(GetX(), GetY(), pixelBuffer);
            }
        }

        void DrawOverlay(IRenderContext* ctx) {
            if (!overlayGraphic.enabled || overlayGraphic.imagePath.empty()) return;

            // Calculate position and size
            Point2Di position = CalculateOverlayPosition();
            float animatedOpacity = overlayGraphic.opacity;
            float animatedScale = overlayGraphic.scale;

            // Apply animation
            if (overlayGraphic.animation != OverlayAnimation::NoAnimation) {
                ApplyOverlayAnimation(position, animatedOpacity, animatedScale);
            }

            // TODO: Load and draw the actual image
            // For now, draw a placeholder rectangle
            ctx->SetFillColor(Color(255, 255, 255, (uint8_t)(animatedOpacity * 255)));
            int scaledWidth = (int)(100 * animatedScale); // Placeholder size
            int scaledHeight = (int)(50 * animatedScale);
            ctx->FillRectangle(Rect2Di(position.x, position.y, scaledWidth, scaledHeight));
        }

        Point2Di CalculateOverlayPosition() {
            Point2Di position;
            int scaledWidth = 100;  // Placeholder - would be actual image width * scale
            int scaledHeight = 50;  // Placeholder - would be actual image height * scale

            switch (overlayGraphic.position) {
                case OverlayPosition::TopLeft:
                    position.x = GetX() + overlayGraphic.marginX;
                    position.y = GetY() + overlayGraphic.marginY;
                    break;

                case OverlayPosition::TopCenter:
                    position.x = GetX() + (GetWidth() - scaledWidth) / 2;
                    position.y = GetY() + overlayGraphic.marginY;
                    break;

                case OverlayPosition::TopRight:
                    position.x = GetX() + GetWidth() - scaledWidth - overlayGraphic.marginX;
                    position.y = GetY() + overlayGraphic.marginY;
                    break;

                case OverlayPosition::MiddleLeft:
                    position.x = GetX() + overlayGraphic.marginX;
                    position.y = GetY() + (GetHeight() - scaledHeight) / 2;
                    break;

                case OverlayPosition::MiddleCenter:
                    position.x = GetX() + (GetWidth() - scaledWidth) / 2;
                    position.y = GetY() + (GetHeight() - scaledHeight) / 2;
                    break;

                case OverlayPosition::MiddleRight:
                    position.x = GetX() + GetWidth() - scaledWidth - overlayGraphic.marginX;
                    position.y = GetY() + (GetHeight() - scaledHeight) / 2;
                    break;

                case OverlayPosition::BottomLeft:
                    position.x = GetX() + overlayGraphic.marginX;
                    position.y = GetY() + GetHeight() - scaledHeight - overlayGraphic.marginY;
                    break;

                case OverlayPosition::BottomCenter:
                    position.x = GetX() + (GetWidth() - scaledWidth) / 2;
                    position.y = GetY() + GetHeight() - scaledHeight - overlayGraphic.marginY;
                    break;

                case OverlayPosition::BottomRight:
                default:
                    position.x = GetX() + GetWidth() - scaledWidth - overlayGraphic.marginX;
                    position.y = GetY() + GetHeight() - scaledHeight - overlayGraphic.marginY;
                    break;
            }

            return position;
        }

        void ApplyOverlayAnimation(Point2Di& position, float& opacity, float& scale) {
            auto elapsed = std::chrono::duration<float>(
                    std::chrono::steady_clock::now() - startTime).count();
            float phase = elapsed * overlayGraphic.animationSpeed;

            switch (overlayGraphic.animation) {
                case OverlayAnimation::FadeInOut:
                    opacity *= (sin(phase) * 0.3f + 0.7f); // Fade between 40%-100%
                    break;

                case OverlayAnimation::Pulse:
                    scale *= (sin(phase * 2.0f) * 0.1f + 1.0f); // Pulse size ±10%
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

                case OverlayAnimation::NoAnimation:
                default:
                    break;
            }
        }

        void DrawGeneratedBackground(IRenderContext* ctx) {
            ctx->PaintPixelBuffer(GetXInWindow(), GetYInWindow(), pixelBuffer);
        }
    };

// ===== PROCEDURAL BACKGROUND PLUGIN =====
// Simplified plugin that implements only the basic IGraphicsPlugin interface
    class ProceduralBackgroundPlugin : public IGraphicsPlugin {
    public:
        std::string GetPluginName() const override {
            return "UltraCanvas Procedural Background Plugin";
        }

        std::string GetPluginVersion() const override {
            return "1.0.0";
        }

        std::vector<std::string> GetSupportedExtensions() const override {
            return {"pbg", "proc", "shader", "formula"};
        }

        bool CanHandle(const std::string& filePath) const override {
            auto extensions = GetSupportedExtensions();
            std::string ext = GetFileExtension(filePath);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
        }

        bool CanHandle(const GraphicsFileInfo& fileInfo) const override {
            return fileInfo.formatType == GraphicsFormatType::Procedural ||
                   CanHandle(fileInfo.filename);
        }

        std::shared_ptr<UltraCanvasElement> LoadGraphics(const std::string& filePath) override {
            auto background = std::make_shared<UltraCanvasProceduralBackground>(
                    "procedural_" + std::to_string(rand()),
                    rand(), 0, 0, 400, 300);
            return background;
        }

        std::shared_ptr<UltraCanvasElement> LoadGraphics(const GraphicsFileInfo& fileInfo) override {
            return LoadGraphics(fileInfo.filename);
        }

        std::shared_ptr<UltraCanvasElement> CreateGraphics(int width, int height, GraphicsFormatType type) override {
            if (type == GraphicsFormatType::Procedural) {
                return std::make_shared<UltraCanvasProceduralBackground>(
                        "new_procedural_" + std::to_string(rand()),
                        rand(), 0, 0, width, height);
            }
            return nullptr;
        }

        GraphicsManipulation GetSupportedManipulations() const override {
            return GraphicsManipulation::Move |
                   GraphicsManipulation::Scale |
                   GraphicsManipulation::Resize |
                   GraphicsManipulation::ColorAdjust |
                   GraphicsManipulation::Filter;
        }

        GraphicsFileInfo GetFileInfo(const std::string& filePath) override {
            GraphicsFileInfo info(filePath);
            info.formatType = GraphicsFormatType::Procedural;
            info.supportedManipulations = GetSupportedManipulations();
            return info;
        }

        bool ValidateFile(const std::string& filePath) override {
            if (!CanHandle(filePath)) return false;
            std::ifstream file(filePath);
            return file.is_open();
        }
    private:
        std::string GetFileExtension(const std::string& filePath) const {
            size_t dotPos = filePath.find_last_of('.');
            if (dotPos == std::string::npos) return "";
            std::string ext = filePath.substr(dotPos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return ext;
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
        background->EnableUltraOSLogo(logoPosition);
        return background;
    }

    inline std::shared_ptr<UltraCanvasProceduralBackground> CreateEfficientBackground(
            int id, int x, int y, int width, int height, const std::string& formulaName) {
        auto background = CreateProceduralBackground(id, x, y, width, height);

        // Load different formulas based on name
        if (formulaName == "Dust" || formulaName == "Cosmic Dust") {
            background->LoadDustFormula();
        }
        // Add more formula types here as needed

        return background;
    }

    inline std::shared_ptr<UltraCanvasProceduralBackground> CreateSpinnerBackground(
            int id, int x, int y, int width, int height, int spinnerType = 1) {
        std::string formulaName = (spinnerType == 2) ? "Spinner 2" : "Spinner";
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

} // namespace UltraCanvas