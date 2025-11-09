// include/UltraCanvasPasswordRuleLegend.h
// Interactive password requirements checklist component
// Version: 1.0.0
// Last Modified: 2025-10-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTextInput.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>

namespace UltraCanvas {

// ===== PASSWORD RULE ITEM =====
    struct PasswordRule {
        std::string ruleName;
        std::string displayText;
        std::function<bool(const std::string&)> validator;
        bool isMet = false;
        float animationProgress = 0.0f;  // For fade-in animation (0.0 to 1.0)

        PasswordRule(const std::string& name, const std::string& text,
                     std::function<bool(const std::string&)> validatorFunc)
                : ruleName(name), displayText(text), validator(validatorFunc) {}
    };

// ===== LEGEND DISPLAY STYLE =====
    enum class LegendStyle {
        Checklist,      // Checkbox-style ✓/✗
        Bullets,        // Bullet points with color
        Detailed        // Full descriptions with icons
    };

// ===== LEGEND CONFIGURATION =====
    struct PasswordRuleLegendConfig {
        LegendStyle style = LegendStyle::Checklist;
        bool showMetRules = true;           // Show rules that are already met
        bool animateChanges = false;         // Animate when rules change state
        float animationDuration = 0.3f;     // Animation duration in seconds
        int itemSpacing = 8;                // Spacing between items
        int iconSize = 16;                  // Size of checkmark/cross icons

        // Colors
        Color metColor = Color(40, 167, 69);        // Green for met rules
        Color unmetColor = Color(220, 53, 69);      // Red for unmet rules
        Color textColor = Color(60, 60, 60);        // Normal text color

        // Icons (Unicode characters)
        std::string metIcon = "✓";          // Checkmark
        std::string unmetIcon = "✗";        // Cross
        std::string bulletIcon = "•";       // Bullet point
    };

// ===== PASSWORD RULE LEGEND COMPONENT =====
    class UltraCanvasPasswordRuleLegend : public UltraCanvasUIElement {
    private:
        // Linked input field
        UltraCanvasTextInput* linkedInput = nullptr;
        std::string lastPassword = "xxxxxxxxxxxxxxxxxxxxxxxxxx";

        // Rules
        std::vector<PasswordRule> rules;

        // Configuration
        PasswordRuleLegendConfig config;

        // Animation tracking
        std::chrono::steady_clock::time_point lastUpdateTime;
        bool needsAnimation = false;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasPasswordRuleLegend(const std::string& id, long uid, int x, int y, int w, int h);

        virtual ~UltraCanvasPasswordRuleLegend() = default;

        // ===== CONFIGURATION =====
        void SetConfig(const PasswordRuleLegendConfig& newConfig);

        PasswordRuleLegendConfig GetConfig() const { return config; }

        void SetStyle(LegendStyle style);

        void SetShowMetRules(bool show);

        void SetAnimationEnabled(bool enabled);

        // ===== RULES MANAGEMENT =====
        void ClearRules();

        void AddRule(const std::string& name, const std::string& displayText,
                     std::function<bool(const std::string&)> validator);

        void AddRule(const ValidationRule& validationRule);

        void SetupDefaultRules();
        void SetupStrictRules();
        void SetupBasicRules();

        // ===== LINK TO TEXT INPUT =====
        void LinkToInput(UltraCanvasTextInput* input);

        void UnlinkInput();

        // ===== MANUAL UPDATE =====
        void UpdateFromPassword(const std::string& password);

        // ===== STATUS =====
        bool AreAllRulesMet() const;

        int GetMetRulesCount() const;

        int GetTotalRulesCount() const;

        std::vector<std::string> GetUnmetRules() const;

        // ===== CALLBACKS =====
        std::function<void(bool)> onAllRulesMet;                    // Called when all rules met/unmet
        std::function<void(int, int)> onRuleStatusChanged;          // Called with (met count, total count)

    protected:
        // ===== CORE RENDERING =====
        void Render(IRenderContext* ctx) override;

    private:
        // ===== RULES UPDATE =====
        void UpdateRules(const std::string& password);

        // ===== ANIMATION =====
        void UpdateAnimations();

        // ===== RENDERING STYLES =====
        void DrawChecklistStyle(IRenderContext* ctx, const Rect2Di& bounds);

        void DrawBulletsStyle(IRenderContext* ctx, const Rect2Di& bounds);

        void DrawDetailedStyle(IRenderContext* ctx, const Rect2Di& bounds);
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasPasswordRuleLegend> CreatePasswordRuleLegend(
            const std::string& id, long uid, int x, int y, int w, int h,
            LegendStyle style = LegendStyle::Checklist) {

        auto legend = std::make_shared<UltraCanvasPasswordRuleLegend>(id, uid, x, y, w, h);
        legend->SetStyle(style);
        return legend;
    }

    inline std::shared_ptr<UltraCanvasPasswordRuleLegend> CreateChecklistLegend(
            const std::string& id, long uid, int x, int y, int w, int h) {
        return CreatePasswordRuleLegend(id, uid, x, y, w, h, LegendStyle::Checklist);
    }

} // namespace UltraCanvas
