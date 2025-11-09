// include/UltraCanvasPasswordStrengthMeter.h
// Visual password strength indicator component
// Version: 1.0.0
// Last Modified: 2025-10-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTextInput.h"
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {

// ===== STRENGTH METER DISPLAY STYLES =====
    enum class StrengthMeterStyle {
        Bar,            // Horizontal progress bar
        Circular,       // Circular progress indicator
    };

// ===== STRENGTH METER CONFIGURATION =====
    struct StrengthMeterConfig {
        StrengthMeterStyle style = StrengthMeterStyle::Bar;
        bool showLabel = true;                  // Show "Weak", "Strong", etc.
        bool showPercentage = false;            // Show "45%" text
        bool animateTransitions = false;         // Smooth color transitions
        float animationDuration = 0.3f;         // Animation duration in seconds
        int borderRadius = 4;                   // Border radius in pixels
        int height = 8;                         // Height for bar styles

        // Thresholds for strength levels (0-100)
        float veryWeakThreshold = 20.0f;
        float weakThreshold = 40.0f;
        float fairThreshold = 60.0f;
        float goodThreshold = 80.0f;
        float strongThreshold = 95.0f;

        // Custom colors (optional - defaults use ValidationRule::GetPasswordStrengthColor)
        Color veryWeakColor = Color(220, 53, 69);    // Red
        Color weakColor = Color(255, 107, 0);        // Orange
        Color fairColor = Color(255, 193, 7);        // Yellow
        Color goodColor = Color(163, 203, 56);       // Lime
        Color strongColor = Color(40, 167, 69);      // Green
        Color veryStrongColor = Color(25, 135, 84);  // Dark Green
        Color backgroundColor = Color(240, 240, 240); // Empty segment color
        Color textColor = Color(60, 60, 60);         // Label text color
    };

// ===== PASSWORD STRENGTH METER COMPONENT =====
    class UltraCanvasPasswordStrengthMeter : public UltraCanvasUIElement {
    private:
        // Linked input field
        UltraCanvasTextInput* linkedInput = nullptr;
        std::string lastPassword;

        // Strength data
        float currentStrength = 0.0f;
        float displayedStrength = 0.0f;  // For smooth animation
        std::string strengthLabel = "No Password";
        Color currentColor = Color(200, 200, 200);

        // Configuration
        StrengthMeterConfig config;

        // Animation
        std::chrono::steady_clock::time_point animationStartTime;
        float animationStartStrength = 0.0f;
        float animationTargetStrength = 0.0f;
        bool isAnimating = false;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasPasswordStrengthMeter(const std::string& id, long uid, int x, int y, int w, int h);

        virtual ~UltraCanvasPasswordStrengthMeter() = default;

        // ===== CONFIGURATION =====
        void SetConfig(const StrengthMeterConfig& newConfig);

        StrengthMeterConfig GetConfig() const { return config; }

        void SetStyle(StrengthMeterStyle style);

        void SetShowLabel(bool show);

        void SetShowPercentage(bool show);

        void SetAnimationEnabled(bool enabled);

        // ===== LINK TO TEXT INPUT =====
        void LinkToInput(UltraCanvasTextInput* input);

        void UnlinkInput() {
            linkedInput = nullptr;
        }

        // ===== MANUAL STRENGTH UPDATE =====
        void SetStrength(float strength);

        void UpdateFromPassword(const std::string& password);

        // ===== GETTERS =====
        float GetCurrentStrength() const { return currentStrength; }
        std::string GetStrengthLabel() const { return strengthLabel; }
        Color GetStrengthColor() const { return currentColor; }

        // ===== CALLBACKS =====
        std::function<void(float)> onStrengthChanged;                    // Called when strength changes
        std::function<void(const std::string&)> onStrengthLevelChanged;  // Called when level changes

    protected:
        // ===== CORE RENDERING =====
        void Render(IRenderContext* ctx) override;

    private:
        // ===== STRENGTH CALCULATION =====
        void UpdateStrength(const std::string& password);

        void UpdateStrengthValue(float newStrength);

        // ===== ANIMATION =====
        void UpdateAnimation();

        // ===== COLOR HELPER =====
        Color GetColorForStrength(float strength) const;

        // ===== RENDERING STYLES =====
        void DrawBarStyle(IRenderContext* ctx, const Rect2Di& bounds);

        void DrawCircularStyle(IRenderContext* ctx, const Rect2Di& bounds);

        void DrawLabel(IRenderContext* ctx, const Rect2Di& bounds, int y);
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasPasswordStrengthMeter> CreatePasswordStrengthMeter(
            const std::string& id, long uid, int x, int y, int w, int h,
            StrengthMeterStyle style = StrengthMeterStyle::Bar) {

        auto meter = std::make_shared<UltraCanvasPasswordStrengthMeter>(id, uid, x, y, w, h);
        meter->SetStyle(style);
        return meter;
    }

    inline std::shared_ptr<UltraCanvasPasswordStrengthMeter> CreateBarStrengthMeter(
            const std::string& id, long uid, int x, int y, int w, int h = 20) {
        return CreatePasswordStrengthMeter(id, uid, x, y, w, h, StrengthMeterStyle::Bar);
    }

    inline std::shared_ptr<UltraCanvasPasswordStrengthMeter> CreateCircularStrengthMeter(
            const std::string& id, long uid, int x, int y, int size = 80) {
        return CreatePasswordStrengthMeter(id, uid, x, y, size, size, StrengthMeterStyle::Circular);
    }

} // namespace UltraCanvas

/*
=== USAGE EXAMPLES ===

// Create password input
auto passwordInput = CreatePasswordInput("password", 1001, 10, 10, 300, 30);

// Create bar-style strength meter
auto strengthMeter = CreateBarStrengthMeter("strength", 1002, 10, 50, 300, 20);
strengthMeter->LinkToInput(passwordInput.get());
strengthMeter->SetShowLabel(true);

// Create circular strength meter
auto circularMeter = CreateCircularStrengthMeter("circular", 1004, 320, 10, 80);
circularMeter->LinkToInput(passwordInput.get());

// Custom configuration
StrengthMeterConfig config;
config.style = StrengthMeterStyle::Bar;
config.showLabel = true;
config.showPercentage = true;
config.animateTransitions = true;
strengthMeter->SetConfig(config);

// Callbacks
strengthMeter->onStrengthChanged = [](float strength) {
    std::cout << "Strength: " << strength << "%" << std::endl;
};

strengthMeter->onStrengthLevelChanged = [](const std::string& level) {
    std::cout << "Level: " << level << std::endl;
};

window->AddElement(passwordInput.get());
window->AddElement(strengthMeter.get());
*/