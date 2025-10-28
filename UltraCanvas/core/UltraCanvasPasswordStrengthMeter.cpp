// core/UltraCanvasPasswordStrengthMeter.cpp
// Visual password strength indicator component
// Version: 1.0.0
// Last Modified: 2025-10-21
// Author: UltraCanvas Framework

#include "UltraCanvasPasswordStrengthMeter.h"
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {
    UltraCanvasPasswordStrengthMeter::UltraCanvasPasswordStrengthMeter(const std::string &id, long uid, int x, int y,
                                                                       int w, int h)
            : UltraCanvasUIElement(id, uid, x, y, w, h) {
        animationStartTime = std::chrono::steady_clock::now();
    }

    void UltraCanvasPasswordStrengthMeter::SetConfig(const StrengthMeterConfig &newConfig) {
        config = newConfig;
        RequestRedraw();
    }

    void UltraCanvasPasswordStrengthMeter::SetStyle(StrengthMeterStyle style) {
        config.style = style;
        RequestRedraw();
    }

    void UltraCanvasPasswordStrengthMeter::SetShowLabel(bool show) {
        config.showLabel = show;
        RequestRedraw();
    }

    void UltraCanvasPasswordStrengthMeter::SetShowPercentage(bool show) {
        config.showPercentage = show;
        RequestRedraw();
    }

    void UltraCanvasPasswordStrengthMeter::SetAnimationEnabled(bool enabled) {
        config.animateTransitions = enabled;
    }

    void UltraCanvasPasswordStrengthMeter::LinkToInput(UltraCanvasTextInput *input) {
        linkedInput = input;
        if (linkedInput) {
            UpdateStrength(linkedInput->GetText());
        }
    }

    void UltraCanvasPasswordStrengthMeter::SetStrength(float strength) {
        UpdateStrengthValue(strength);
    }

    void UltraCanvasPasswordStrengthMeter::UpdateFromPassword(const std::string &password) {
        UpdateStrength(password);
    }

    void UltraCanvasPasswordStrengthMeter::Render() {
        auto ctx = GetRenderContext();
        if (!ctx) return;

        Rect2Di bounds = GetBounds();

        // Update animation
        if (isAnimating) {
            UpdateAnimation();
        }

        // Update from linked input if connected
        if (linkedInput) {
            std::string currentPassword = linkedInput->GetText();
            if (currentPassword != lastPassword) {
                UpdateStrength(currentPassword);
                lastPassword = currentPassword;
            }
        }

        // Render based on style
        switch (config.style) {
            case StrengthMeterStyle::Bar:
                DrawBarStyle(ctx, bounds);
                break;
            case StrengthMeterStyle::Circular:
                DrawCircularStyle(ctx, bounds);
                break;
        }
    }

    void UltraCanvasPasswordStrengthMeter::UpdateStrength(const std::string &password) {
        if (password.empty()) {
            UpdateStrengthValue(0.0f);
            strengthLabel = "No Password";
            return;
        }

        // Calculate strength using ValidationRule helper
        float newStrength = ValidationRule::CalculatePasswordStrength(password);
        UpdateStrengthValue(newStrength);

        // Update label
        strengthLabel = ValidationRule::GetPasswordStrengthLevel(newStrength);

        // Get color
        currentColor = GetColorForStrength(newStrength);

        // Trigger callback
        if (onStrengthLevelChanged) {
            onStrengthLevelChanged(strengthLabel);
        }
    }

    void UltraCanvasPasswordStrengthMeter::UpdateStrengthValue(float newStrength) {
        if (currentStrength == newStrength) return;

        if (config.animateTransitions) {
            // Start animation
            animationStartStrength = displayedStrength;
            animationTargetStrength = newStrength;
            animationStartTime = std::chrono::steady_clock::now();
            isAnimating = true;
        } else {
            displayedStrength = newStrength;
        }

        currentStrength = newStrength;
        RequestRedraw();

        if (onStrengthChanged) {
            onStrengthChanged(newStrength);
        }
    }

    void UltraCanvasPasswordStrengthMeter::UpdateAnimation() {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - animationStartTime).count();
        float progress = elapsed / config.animationDuration;

        if (progress >= 1.0f) {
            displayedStrength = animationTargetStrength;
            isAnimating = false;
        } else {
            // Smooth easing function (ease-out)
            float eased = 1.0f - std::pow(1.0f - progress, 3.0f);
            displayedStrength = animationStartStrength +
                                (animationTargetStrength - animationStartStrength) * eased;
            RequestRedraw();
        }
    }

    Color UltraCanvasPasswordStrengthMeter::GetColorForStrength(float strength) const {
        if (strength < config.veryWeakThreshold) return config.veryWeakColor;
        if (strength < config.weakThreshold) return config.weakColor;
        if (strength < config.fairThreshold) return config.fairColor;
        if (strength < config.goodThreshold) return config.goodColor;
        if (strength < config.strongThreshold) return config.strongColor;
        return config.veryStrongColor;
    }

    void UltraCanvasPasswordStrengthMeter::DrawBarStyle(IRenderContext *ctx, const Rect2Di &bounds) {
        int barHeight = config.height;
        int barY = bounds.y + (bounds.height - barHeight) / 2;

        // Draw background bar
        ctx->SetFillPaint(config.backgroundColor);
        ctx->FillRoundedRectangle(bounds.x, barY, bounds.width, barHeight, config.borderRadius);

        // Draw strength bar
        int fillWidth = static_cast<int>(bounds.width * (displayedStrength / 100.0f));
        if (fillWidth > 0) {
            ctx->SetFillPaint(currentColor);
            ctx->FillRoundedRectangle(bounds.x, barY, fillWidth, barHeight, config.borderRadius);
        }

        // Draw label
        if (config.showLabel || config.showPercentage) {
            DrawLabel(ctx, bounds, barY + barHeight + 5);
        }
    }

    void UltraCanvasPasswordStrengthMeter::DrawCircularStyle(IRenderContext *ctx, const Rect2Di &bounds) {
        int centerX = bounds.x + bounds.width / 2;
        int centerY = bounds.y + bounds.height / 2;
        int radius = std::min(bounds.width, bounds.height) / 2 - 5;

        // Draw background circle
        ctx->SetStrokePaint(config.backgroundColor);
        ctx->SetStrokeWidth(8.0f);
        ctx->DrawCircle(centerX, centerY, radius);

        // Draw strength arc
        if (displayedStrength > 0) {
            ctx->SetStrokePaint(currentColor);
            float angle = (displayedStrength / 100.0f) * 360.0f;
            ctx->Arc(centerX, centerY, radius, (M_PI / 180) * -90.0f, M_PI / 180 * (-90.0f + angle));
            ctx->Stroke();
        }

        // Draw percentage in center
        if (config.showPercentage) {
            ctx->SetTextPaint(config.textColor);
            ctx->SetFontSize(14);
            ctx->SetFontWeight(FontWeight::Bold);
            std::string percentText = std::to_string(static_cast<int>(currentStrength)) + "%";
            int textWidth, textHeight;
            ctx->GetTextLineDimensions(percentText, textWidth, textHeight);
            ctx->DrawText(percentText, centerX - textWidth / 2, centerY - textHeight / 2);
        }

        // Draw label below
        if (config.showLabel) {
            ctx->SetFontSize(10);
            ctx->SetFontWeight(FontWeight::Normal);
            int labelWidth = ctx->GetTextLineWidth(strengthLabel);
            ctx->DrawText(strengthLabel, centerX - labelWidth / 2, centerY + radius + 5);
        }
    }

    void UltraCanvasPasswordStrengthMeter::DrawLabel(IRenderContext *ctx, const Rect2Di &bounds, int y) {
        ctx->SetTextPaint(config.textColor);
        ctx->SetFontSize(11);

        std::string displayText;
        if (config.showLabel && config.showPercentage) {
            displayText = strengthLabel + " (" + std::to_string(static_cast<int>(currentStrength)) + "%)";
        } else if (config.showLabel) {
            displayText = strengthLabel;
        } else if (config.showPercentage) {
            displayText = std::to_string(static_cast<int>(currentStrength)) + "%";
        }

        if (!displayText.empty()) {
            int textWidth = ctx->GetTextLineWidth(displayText);
            ctx->DrawText(displayText, bounds.x + (bounds.width - textWidth) / 2, y);
        }
    }
}