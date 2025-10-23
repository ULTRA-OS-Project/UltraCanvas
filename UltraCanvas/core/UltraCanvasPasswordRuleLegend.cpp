// core/UltraCanvasPasswordRuleLegend.cpp
// Interactive password requirements checklist component
// Version: 1.0.0
// Last Modified: 2025-10-21
// Author: UltraCanvas Framework
#include "UltraCanvasPasswordRuleLegend.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>

namespace UltraCanvas {

    UltraCanvasPasswordRuleLegend::UltraCanvasPasswordRuleLegend(const std::string &id, long uid, int x, int y, int w,
                                                                 int h)
            : UltraCanvasUIElement(id, uid, x, y, w, h) {
        lastUpdateTime = std::chrono::steady_clock::now();
        SetupDefaultRules();
    }

    void UltraCanvasPasswordRuleLegend::SetConfig(const PasswordRuleLegendConfig &newConfig) {
        config = newConfig;
        RequestRedraw();
    }

    void UltraCanvasPasswordRuleLegend::SetStyle(LegendStyle style) {
        config.style = style;
        RequestRedraw();
    }

    void UltraCanvasPasswordRuleLegend::SetShowMetRules(bool show) {
        config.showMetRules = show;
        RequestRedraw();
    }

    void UltraCanvasPasswordRuleLegend::SetAnimationEnabled(bool enabled) {
        config.animateChanges = enabled;
    }

    void UltraCanvasPasswordRuleLegend::ClearRules() {
        rules.clear();
        RequestRedraw();
    }

    void UltraCanvasPasswordRuleLegend::AddRule(const std::string &name, const std::string &displayText,
                                                std::function<bool(const std::string &)> validator) {
        rules.emplace_back(name, displayText, validator);
        RequestRedraw();
    }

    void UltraCanvasPasswordRuleLegend::AddRule(const ValidationRule &validationRule) {
        rules.emplace_back(validationRule.name, validationRule.errorMessage, validationRule.validator);
        RequestRedraw();
    }

    void UltraCanvasPasswordRuleLegend::SetupDefaultRules() {
        ClearRules();
        AddRule(ValidationRule::MinLength(8, "At least 8 characters"));
        AddRule(ValidationRule::RequireUppercase(1, "At least 1 uppercase letter"));
        AddRule(ValidationRule::RequireLowercase(1, "At least 1 lowercase letter"));
        AddRule(ValidationRule::RequireDigit(1, "At least 1 number"));
        AddRule(ValidationRule::RequireSpecialChar(1, "At least 1 special character"));
    }

    void UltraCanvasPasswordRuleLegend::SetupStrictRules() {
        ClearRules();
        AddRule(ValidationRule::MinLength(12, "At least 12 characters"));
        AddRule(ValidationRule::RequireUppercase(2, "At least 2 uppercase letters"));
        AddRule(ValidationRule::RequireLowercase(2, "At least 2 lowercase letters"));
        AddRule(ValidationRule::RequireDigit(2, "At least 2 numbers"));
        AddRule(ValidationRule::RequireSpecialChar(1, "At least 1 special character"));
        AddRule(ValidationRule::NoRepeatingChars(2, "No more than 2 repeating characters"));
        AddRule(ValidationRule::NoSequentialChars(3, "No more than 3 sequential characters"));
    }

    void UltraCanvasPasswordRuleLegend::SetupBasicRules() {
        ClearRules();
        AddRule(ValidationRule::MinLength(6, "At least 6 characters"));
        AddRule(ValidationRule::RequireUppercase(1, "At least 1 uppercase letter"));
        AddRule(ValidationRule::RequireLowercase(1, "At least 1 lowercase letter"));
    }

    void UltraCanvasPasswordRuleLegend::LinkToInput(UltraCanvasTextInput *input) {
        linkedInput = input;
        if (linkedInput) {
            UpdateRules(linkedInput->GetText());
        }
    }

    void UltraCanvasPasswordRuleLegend::UnlinkInput() {
        linkedInput = nullptr;
    }

    void UltraCanvasPasswordRuleLegend::UpdateFromPassword(const std::string &password) {
        UpdateRules(password);
    }

    bool UltraCanvasPasswordRuleLegend::AreAllRulesMet() const {
        for (const auto& rule : rules) {
            if (!rule.isMet) return false;
        }
        return true;
    }

    int UltraCanvasPasswordRuleLegend::GetMetRulesCount() const {
        int count = 0;
        for (const auto& rule : rules) {
            if (rule.isMet) count++;
        }
        return count;
    }

    int UltraCanvasPasswordRuleLegend::GetTotalRulesCount() const {
        return rules.size();
    }

    std::vector<std::string> UltraCanvasPasswordRuleLegend::GetUnmetRules() const {
        std::vector<std::string> unmet;
        for (const auto& rule : rules) {
            if (!rule.isMet) {
                unmet.push_back(rule.displayText);
            }
        }
        return unmet;
    }

    void UltraCanvasPasswordRuleLegend::Render() {
        auto ctx = GetRenderContext();
        if (!ctx) return;

        Rect2Di bounds = GetBounds();

        // Update from linked input if connected
        if (linkedInput) {
            std::string currentPassword = linkedInput->GetText();
            if (currentPassword != lastPassword) {
                UpdateRules(currentPassword);
                lastPassword = currentPassword;
            }
        }

        // Update animations
        if (config.animateChanges && needsAnimation) {
            UpdateAnimations();
        }

        // Draw background
        if (config.backgroundColor.a > 0) {
            ctx->SetFillPaint(config.backgroundColor);
            ctx->FillRoundedRectangle(bounds.x, bounds.y, bounds.width, bounds.height, 4);
        }

        // Render based on style
        switch (config.style) {
            case LegendStyle::Checklist:
                DrawChecklistStyle(ctx, bounds);
                break;
            case LegendStyle::Bullets:
                DrawBulletsStyle(ctx, bounds);
                break;
            case LegendStyle::Detailed:
                DrawDetailedStyle(ctx, bounds);
                break;
        }
    }

    void UltraCanvasPasswordRuleLegend::UpdateRules(const std::string &password) {
        bool statusChanged = false;
        int previousMetCount = GetMetRulesCount();

        for (auto& rule : rules) {
            bool previousState = rule.isMet;
            rule.isMet = rule.validator(password);

            if (previousState != rule.isMet) {
                statusChanged = true;
                if (config.animateChanges) {
                    rule.animationProgress = 0.0f;
                    needsAnimation = true;
                }
            }
        }

        if (statusChanged) {
            int currentMetCount = GetMetRulesCount();
            bool allMet = AreAllRulesMet();

            if (onRuleStatusChanged) {
                onRuleStatusChanged(currentMetCount, GetTotalRulesCount());
            }

            if (onAllRulesMet && (previousMetCount != currentMetCount)) {
                if (allMet) {
                    onAllRulesMet(true);
                } else if (previousMetCount == GetTotalRulesCount()) {
                    onAllRulesMet(false);
                }
            }
        }

        RequestRedraw();
    }

    void UltraCanvasPasswordRuleLegend::UpdateAnimations() {
        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastUpdateTime).count();
        lastUpdateTime = now;

        bool stillAnimating = false;

        for (auto& rule : rules) {
            if (rule.animationProgress < 1.0f) {
                rule.animationProgress += deltaTime / config.animationDuration;
                if (rule.animationProgress > 1.0f) {
                    rule.animationProgress = 1.0f;
                } else {
                    stillAnimating = true;
                }
            }
        }

        if (!stillAnimating) {
            needsAnimation = false;
        } else {
            RequestRedraw();
        }
    }

    void UltraCanvasPasswordRuleLegend::DrawChecklistStyle(IRenderContext *ctx, const Rect2Di &bounds) {
        int currentY = bounds.y + 10;

        ctx->SetFontSize(12);

        for (const auto& rule : rules) {
            if (!config.showMetRules && rule.isMet) continue;

            // Calculate alpha for fade animation
            float alpha = config.animateChanges ? rule.animationProgress : 1.0f;

            // Draw icon
            Color iconColor = rule.isMet ? config.metColor : config.unmetColor;
            iconColor.a = static_cast<uint8_t>(255 * alpha);
            ctx->SetTextPaint(iconColor);
            ctx->SetFontSize(config.iconSize);
            std::string icon = rule.isMet ? config.metIcon : config.unmetIcon;
            ctx->DrawText(icon, bounds.x + 10, currentY - (config.iconSize - 12));

            // Draw text
            Color textColor = config.textColor;
            textColor.a = static_cast<uint8_t>(255 * alpha);
            ctx->SetTextPaint(textColor);
            ctx->SetFontSize(12);
            ctx->DrawText(rule.displayText, bounds.x + 30, currentY);

            currentY += config.itemSpacing + 16;
        }
    }

    void UltraCanvasPasswordRuleLegend::DrawBulletsStyle(IRenderContext *ctx, const Rect2Di &bounds) {
        int currentY = bounds.y + 10;

        ctx->SetFontSize(12);

        for (const auto& rule : rules) {
            if (!config.showMetRules && rule.isMet) continue;

            float alpha = config.animateChanges ? rule.animationProgress : 1.0f;

            // Draw colored bullet
            Color bulletColor = rule.isMet ? config.metColor : config.unmetColor;
            bulletColor.a = static_cast<uint8_t>(255 * alpha);
            ctx->SetTextPaint(bulletColor);
            ctx->DrawText(config.bulletIcon, bounds.x + 10, currentY);

            // Draw text with color indicating status
            Color textColor = rule.isMet ? config.metColor : config.textColor;
            textColor.a = static_cast<uint8_t>(255 * alpha);
            ctx->SetTextPaint(textColor);
            ctx->DrawText(rule.displayText, bounds.x + 25, currentY);

            currentY += config.itemSpacing + 14;
        }
    }

    void UltraCanvasPasswordRuleLegend::DrawDetailedStyle(IRenderContext *ctx, const Rect2Di &bounds) {
        int currentY = bounds.y + 15;

        ctx->SetFontSize(12);

        for (const auto& rule : rules) {
            if (!config.showMetRules && rule.isMet) continue;

            float alpha = config.animateChanges ? rule.animationProgress : 1.0f;

            // Draw background box
            Color bgColor = rule.isMet ?
                            Color(config.metColor.r, config.metColor.g, config.metColor.b, 20) :
                            Color(config.unmetColor.r, config.unmetColor.g, config.unmetColor.b, 20);
            bgColor.a = static_cast<uint8_t>(bgColor.a * alpha);
            ctx->SetFillPaint(bgColor);
            ctx->FillRoundedRectangle(bounds.x + 10, currentY - 5,
                                      bounds.width - 20, 30, 4);

            // Draw icon
            Color iconColor = rule.isMet ? config.metColor : config.unmetColor;
            iconColor.a = static_cast<uint8_t>(255 * alpha);
            ctx->SetTextPaint(iconColor);
            ctx->SetFontSize(config.iconSize);
            std::string icon = rule.isMet ? config.metIcon : config.unmetIcon;
            ctx->DrawText(icon, bounds.x + 20, currentY - (config.iconSize - 12));

            // Draw text
            Color textColor = config.textColor;
            textColor.a = static_cast<uint8_t>(255 * alpha);
            ctx->SetTextPaint(textColor);
            ctx->SetFontSize(12);
            ctx->DrawText(rule.displayText, bounds.x + 45, currentY);

            currentY += config.itemSpacing + 35;
        }
    }
}