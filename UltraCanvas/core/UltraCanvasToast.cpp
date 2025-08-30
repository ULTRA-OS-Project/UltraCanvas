// UltraCanvasToast.cpp
// Cross-platform toast notification system implementation
// Version: 2.1.0
// Last Modified: 2025-07-08
// Author: UltraCanvas Framework

#include "UltraCanvasToast.h"
#include "UltraCanvasRenderInterface.h"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <random>

namespace UltraCanvas {

// ===== TOAST MANAGER STATIC DEFINITIONS =====
    std::vector<std::unique_ptr<UltraCanvasToast>> UltraCanvasToastManager::activeToasts;
    ToastConfig UltraCanvasToastManager::defaultConfig;
    Rect2D UltraCanvasToastManager::screenBounds = Rect2D(0, 0, 1920, 1080);
    int UltraCanvasToastManager::maxToasts = 10;
    bool UltraCanvasToastManager::enabled = true;
    bool UltraCanvasToastManager::debugMode = false;

    std::vector<UltraCanvasToast*> UltraCanvasToastManager::topLeftStack;
    std::vector<UltraCanvasToast*> UltraCanvasToastManager::topCenterStack;
    std::vector<UltraCanvasToast*> UltraCanvasToastManager::topRightStack;
    std::vector<UltraCanvasToast*> UltraCanvasToastManager::middleLeftStack;
    std::vector<UltraCanvasToast*> UltraCanvasToastManager::middleCenterStack;
    std::vector<UltraCanvasToast*> UltraCanvasToastManager::middleRightStack;
    std::vector<UltraCanvasToast*> UltraCanvasToastManager::bottomLeftStack;
    std::vector<UltraCanvasToast*> UltraCanvasToastManager::bottomCenterStack;
    std::vector<UltraCanvasToast*> UltraCanvasToastManager::bottomRightStack;

// ===== TOAST CONFIG IMPLEMENTATION =====

    void ToastConfig::SetDefaultColors() {
        switch (type) {
            case ToastType::Info:
                backgroundColor = Color(52, 144, 220, 240);  // Blue
                textColor = Colors::White;
                borderColor = Color(41, 128, 185, 255);
                iconColor = Colors::White;
                break;

            case ToastType::Success:
                backgroundColor = Color(46, 204, 113, 240);  // Green
                textColor = Colors::White;
                borderColor = Color(39, 174, 96, 255);
                iconColor = Colors::White;
                break;

            case ToastType::Warning:
                backgroundColor = Color(241, 196, 15, 240);  // Yellow
                textColor = Color(52, 73, 94, 255);  // Dark gray
                borderColor = Color(243, 156, 18, 255);
                iconColor = Color(52, 73, 94, 255);
                break;

            case ToastType::Error:
                backgroundColor = Color(231, 76, 60, 240);   // Red
                textColor = Colors::White;
                borderColor = Color(192, 57, 43, 255);
                iconColor = Colors::White;
                break;

            case ToastType::Custom:
                backgroundColor = Color(236, 240, 241, 240); // Light gray
                textColor = Color(52, 73, 94, 255);
                borderColor = Color(189, 195, 199, 255);
                iconColor = Color(52, 73, 94, 255);
                break;
        }
    }

// ===== TOAST ELEMENT IMPLEMENTATION =====

    UltraCanvasToast::UltraCanvasToast(const ToastConfig& config)
            : config(config)
            , isVisible(false)
            , isHovered(false)
            , isAnimating(false)
            , animationProgress(0.0f)
            , totalPauseTime(0.0f)
            , shouldClose(false)
            , isPaused(false) {

        // Generate unique ID
        static int nextId = 1;
        id = "toast_" + std::to_string(nextId++);

        // Set initial size
        SetBounds(Rect2D(0, 0, config.width, config.minHeight));

        // Calculate layout
        CalculateLayout();
    }

    UltraCanvasToast::~UltraCanvasToast() {
        if (config.onHide) {
            config.onHide();
        }
    }

// ===== CORE FUNCTIONALITY IMPLEMENTATION =====

    void UltraCanvasToast::Show() {
        if (isVisible) return;

        isVisible = true;
        isAnimating = true;
        animationProgress = 0.0f;
        showTime = std::chrono::steady_clock::now();

        if (config.onShow) {
            config.onShow();
        }
    }

    void UltraCanvasToast::Hide() {
        if (!isVisible) return;

        shouldClose = true;
        hideTime = std::chrono::steady_clock::now();

        if (config.onHide) {
            config.onHide();
        }
    }

    void UltraCanvasToast::Close() {
        isVisible = false;
        shouldClose = true;

        if (config.onClose) {
            config.onClose();
        }
    }

    bool UltraCanvasToast::IsVisible() const {
        return isVisible;
    }

    bool UltraCanvasToast::IsExpired() const {
        if (config.duration <= 0.0f) return false; // Permanent toast
        if (isPaused) return false;

        return GetElapsedTime() >= config.duration;
    }

// ===== PROPERTIES IMPLEMENTATION =====

    std::string UltraCanvasToast::GetId() const {
        return id;
    }

    ToastConfig& UltraCanvasToast::GetConfig() {
        return config;
    }

    const ToastConfig& UltraCanvasToast::GetConfig() const {
        return config;
    }

    void UltraCanvasToast::SetConfig(const ToastConfig& newConfig) {
        config = newConfig;
        CalculateLayout();
    }

    void UltraCanvasToast::SetTitle(const std::string& title) {
        config.title = title;
        CalculateLayout();
    }

    void UltraCanvasToast::SetMessage(const std::string& message) {
        config.message = message;
        CalculateLayout();
    }

    void UltraCanvasToast::SetType(ToastType type) {
        config.type = type;
        config.SetDefaultColors();
    }

    void UltraCanvasToast::SetDuration(float seconds) {
        config.duration = seconds;
    }

    std::string UltraCanvasToast::GetTitle() const {
        return config.title;
    }

    std::string UltraCanvasToast::GetMessage() const {
        return config.message;
    }

    ToastType UltraCanvasToast::GetType() const {
        return config.type;
    }

    float UltraCanvasToast::GetDuration() const {
        return config.duration;
    }

// ===== TIMING IMPLEMENTATION =====

    void UltraCanvasToast::ResetTimer() {
        showTime = std::chrono::steady_clock::now();
        totalPauseTime = 0.0f;
        isPaused = false;
    }

    void UltraCanvasToast::PauseTimer() {
        if (!isPaused) {
            pauseTime = std::chrono::steady_clock::now();
            isPaused = true;
        }
    }

    void UltraCanvasToast::ResumeTimer() {
        if (isPaused) {
            auto now = std::chrono::steady_clock::now();
            totalPauseTime += std::chrono::duration<float>(now - pauseTime).count();
            isPaused = false;
        }
    }

    bool UltraCanvasToast::IsPaused() const {
        return isPaused;
    }

    float UltraCanvasToast::GetRemainingTime() const {
        if (config.duration <= 0.0f) return -1.0f; // Permanent
        return std::max(0.0f, config.duration - GetElapsedTime());
    }

    float UltraCanvasToast::GetElapsedTime() const {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - showTime).count();

        if (isPaused) {
            elapsed -= std::chrono::duration<float>(now - pauseTime).count();
        }

        return elapsed - totalPauseTime;
    }

    float UltraCanvasToast::GetProgress() const {
        if (config.duration <= 0.0f) return 0.0f;
        return std::min(1.0f, GetElapsedTime() / config.duration);
    }

// ===== ANIMATION IMPLEMENTATION =====

    void UltraCanvasToast::Update(float deltaTime) {
        if (!isVisible) return;

        // Handle hover pause
        if (config.pauseOnHover) {
            if (isHovered && !isPaused) {
                PauseTimer();
            } else if (!isHovered && isPaused) {
                ResumeTimer();
            }
        }

        // Update animation
        UpdateAnimation(deltaTime);

        // Check if should close
        if (config.duration > 0.0f && IsExpired()) {
            Hide();
        }
    }

    void UltraCanvasToast::UpdateAnimation(float deltaTime) {
        if (!isAnimating) return;

        if (shouldClose) {
            // Fade out animation
            animationProgress -= deltaTime / config.fadeOutTime;
            if (animationProgress <= 0.0f) {
                animationProgress = 0.0f;
                isAnimating = false;
                isVisible = false;
            }
        } else {
            // Fade in animation
            animationProgress += deltaTime / config.fadeInTime;
            if (animationProgress >= 1.0f) {
                animationProgress = 1.0f;
                isAnimating = false;
            }
        }
    }

    float UltraCanvasToast::CalculateAnimationAlpha() const {
        if (!isVisible) return 0.0f;

        switch (config.animation) {
            case ToastAnimation::Fade:
                return animationProgress;

            case ToastAnimation::None:
            default:
                return 1.0f;
        }
    }

    Point2D UltraCanvasToast::CalculateAnimationOffset() const {
        if (config.animation != ToastAnimation::Slide) {
            return Point2D(0, 0);
        }

        float offset = (1.0f - animationProgress) * 50.0f; // Slide in from edge

        switch (config.position) {
            case ToastPosition::TopLeft:
            case ToastPosition::MiddleLeft:
            case ToastPosition::BottomLeft:
                return Point2D(-offset, 0);

            case ToastPosition::TopRight:
            case ToastPosition::MiddleRight:
            case ToastPosition::BottomRight:
                return Point2D(offset, 0);

            case ToastPosition::TopCenter:
                return Point2D(0, -offset);

            case ToastPosition::BottomCenter:
                return Point2D(0, offset);

            default:
                return Point2D(0, 0);
        }
    }

    float UltraCanvasToast::CalculateAnimationScale() const {
        if (config.animation != ToastAnimation::Scale) {
            return 1.0f;
        }

        return 0.8f + (0.2f * animationProgress); // Scale from 80% to 100%
    }

// ===== RENDERING IMPLEMENTATION =====

    void UltraCanvasToast::Render() {
        if (!isVisible) return;

        ULTRACANVAS_RENDER_SCOPE();

        // Apply animation effects
        float alpha = CalculateAnimationAlpha();
        UltraCanvasRenderInterface::SetOpacity(alpha);

        Point2D animOffset = CalculateAnimationOffset();
        Point2D renderPos = GetPosition() + animOffset;

        // Temporarily adjust position for animation
        Point2D originalPos = GetPosition();
        SetPosition(renderPos);

        // Render components
        RenderBackground();
        RenderBorder();
        RenderIcon();
        RenderTitle();
        RenderMessage();

        if (config.closable) {
            RenderCloseButton();
        }

        if (config.showProgressBar && config.duration > 0.0f) {
            RenderProgressBar();
        }

        // Restore original position
        SetPosition(originalPos);
    }

// ===== EVENT HANDLING IMPLEMENTATION =====

    bool UltraCanvasToast::HandleEvent(const UCEvent& event) {
        if (!isVisible) return false;

        switch (event.type) {
            case UCEventType::MouseMove: {
                Point2D mousePos(event.mouse.x, event.mouse.y);
                bool wasHovered = isHovered;
                isHovered = Contains(mousePos);

                if (isHovered && !wasHovered) {
                    OnMouseEnter();
                } else if (!isHovered && wasHovered) {
                    OnMouseLeave();
                }
                break;
            }

            case UCEventType::MouseDown: {
                Point2D mousePos(event.mouse.x, event.mouse.y);
                if (Contains(mousePos)) {
                    if (config.closable && IsPointInCloseButton(mousePos)) {
                        OnCloseButtonClick();
                    } else if (config.clickToClose) {
                        OnClick();
                    }
                    return true;
                }
                break;
            }

            default:
                break;
        }

        return false;
    }

    void UltraCanvasToast::OnMouseEnter() {
        isHovered = true;
    }

    void UltraCanvasToast::OnMouseLeave() {
        isHovered = false;
    }

    void UltraCanvasToast::OnClick() {
        if (config.onClick) {
            config.onClick();
        }

        if (config.clickToClose) {
            Hide();
        }
    }

    void UltraCanvasToast::OnCloseButtonClick() {
        Hide();
    }

// ===== LAYOUT IMPLEMENTATION =====

    void UltraCanvasToast::CalculateLayout() {
        CalculateContentRect();
        CalculateIconRect();
        CalculateTitleRect();
        CalculateMessageRect();
        CalculateCloseButtonRect();
        CalculateProgressBarRect();

        // Adjust height based on content
        float contentHeight = config.padding * 2;

        if (!config.title.empty()) {
            contentHeight += config.fontSize * 1.2f;
        }

        if (!config.message.empty()) {
            std::vector<std::string> lines = WrapText(config.message, config.width - config.padding * 2 - config.iconSize - config.spacing);
            contentHeight += lines.size() * config.fontSize * 1.1f;
        }

        float finalHeight = std::max(config.minHeight, std::min(config.maxHeight, contentHeight));
        SetSize(Point2D(config.width, finalHeight));
    }

    void UltraCanvasToast::CalculateContentRect() {
        contentRect = Rect2D(
                GetPosition().x + config.padding,
                GetPosition().y + config.padding,
                GetSize().x - config.padding * 2,
                GetSize().y - config.padding * 2
        );
    }

    void UltraCanvasToast::CalculateIconRect() {
        iconRect = Rect2D(
                contentRect.x,
                contentRect.y + (contentRect.height - config.iconSize) / 2,
                config.iconSize,
                config.iconSize
        );
    }

    void UltraCanvasToast::CalculateTitleRect() {
        float leftMargin = contentRect.x + config.iconSize + config.spacing;
        float rightMargin = config.closable ? config.iconSize + config.spacing : 0;

        titleRect = Rect2D(
                leftMargin,
                contentRect.y,
                contentRect.width - config.iconSize - config.spacing - rightMargin,
                config.fontSize * 1.2f
        );
    }

    void UltraCanvasToast::CalculateMessageRect() {
        float leftMargin = contentRect.x + config.iconSize + config.spacing;
        float rightMargin = config.closable ? config.iconSize + config.spacing : 0;
        float topMargin = config.title.empty() ? 0 : config.fontSize * 1.2f + config.spacing / 2;

        messageRect = Rect2D(
                leftMargin,
                contentRect.y + topMargin,
                contentRect.width - config.iconSize - config.spacing - rightMargin,
                contentRect.height - topMargin
        );
    }

    void UltraCanvasToast::CalculateCloseButtonRect() {
        closeButtonRect = Rect2D(
                GetPosition().x + GetSize().x - config.iconSize - config.padding,
                GetPosition().y + config.padding,
                config.iconSize,
                config.iconSize
        );
    }

    void UltraCanvasToast::CalculateProgressBarRect() {
        progressBarRect = Rect2D(
                GetPosition().x,
                GetPosition().y + GetSize().y - 3,
                GetSize().x,
                3
        );
    }

// ===== RENDERING HELPERS IMPLEMENTATION =====

    void UltraCanvasToast::RenderBackground() {
        UltraCanvasRenderInterface::DrawFilledRect(GetBounds(), config.backgroundColor);
    }

    void UltraCanvasToast::RenderBorder() {
        if (config.borderWidth > 0) {
            UltraCanvasRenderInterface::SetStrokeColor(config.borderColor);
            UltraCanvasRenderInterface::SetStrokeWidth(config.borderWidth);
            UltraCanvasRenderInterface::DrawRectangle(GetBounds());
        }
    }

    void UltraCanvasToast::RenderIcon() {
        // Simple geometric icon based on type
        UltraCanvasRenderInterface::SetFillColor(config.iconColor);

        Point2D center(iconRect.x + iconRect.width / 2, iconRect.y + iconRect.height / 2);

        switch (config.type) {
            case ToastType::Info:
                // "i" circle
                DrawFilledCircle(center, iconRect.width / 3, config.iconColor);
                break;

            case ToastType::Success:
                // Checkmark (simplified)
                DrawFilledCircle(center, iconRect.width / 3, config.iconColor);
                break;

            case ToastType::Warning:
                // Triangle with "!"
                DrawFilledCircle(center, iconRect.width / 3, config.iconColor);
                break;

            case ToastType::Error:
                // "X" circle
                DrawFilledCircle(center, iconRect.width / 3, config.iconColor);
                break;

            default: