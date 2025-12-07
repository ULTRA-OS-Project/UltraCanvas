// include/UltraCanvasToast.h
// Toast notification system for non-intrusive user messaging
// Version: 1.2.1
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>

namespace UltraCanvas {

// ===== TOAST TYPES =====
    enum class ToastType {
        Info,       // Blue - informational messages
        Success,    // Green - success confirmations
        Warning,    // Yellow/Orange - warnings
        Error       // Red - error messages
    };

    enum class ToastPosition {
        TopLeft,
        TopCenter,
        TopRight,
        MiddleLeft,
        MiddleCenter,
        MiddleRight,
        BottomLeft,
        BottomCenter,
        BottomRight
    };

    enum class ToastAnimation {
        NoAnimation,
        Fade,
        Slide,
        Bounce,
        Scale
    };

// ===== TOAST CONFIGURATION =====
    struct ToastStyle {
        // Appearance
        Color backgroundColor = Color(45, 45, 45, 240);
        Color borderColor = Color(70, 70, 70, 255);
        Color textColor = Colors::White;
        Color iconColor = Colors::White;

        // Typography
        std::string fontFamily = "Sans";
        float fontSize = 12.0f;
        FontWeight fontWeight = FontWeight::Normal;

        // Layout
        int paddingLeft = 16;
        int paddingRight = 16;
        int paddingTop = 12;
        int paddingBottom = 12;
        int marginBetween = 8;
        int maxWidth = 400;
        int minWidth = 200;
        float cornerRadius = 6.0f;
        int borderWidth = 1;

        // Animation
        ToastAnimation animation = ToastAnimation::Slide;
        float animationDuration = 0.3f;
        float fadeOutDuration = 0.2f;

        // Shadow
        bool hasShadow = true;
        Color shadowColor = Color(0, 0, 0, 128);
        Point2D shadowOffset = Point2D(0, 2);
        float shadowBlur = 8.0f;

        ToastStyle() = default;
    };

    struct ToastConfig {
        ToastType type = ToastType::Info;
        std::string message;
        std::string title;
        int duration = 3000; // milliseconds, 0 = persistent
        ToastPosition position = ToastPosition::TopRight;
        ToastStyle style;
        bool showIcon = true;
        bool showCloseButton = true;
        bool pauseOnHover = true;
        std::function<void()> onClick;
        std::function<void()> onClose;

        ToastConfig() = default;
        ToastConfig(const std::string& msg, ToastType t = ToastType::Info, int dur = 3000)
                : type(t), message(msg), duration(dur) {}
    };

// ===== TOAST ELEMENT CLASS =====
    class UltraCanvasToast : public UltraCanvasUIElement {
    private:
        StandardProperties properties;
        ToastConfig config;
        std::chrono::steady_clock::time_point creationTime;
        std::chrono::steady_clock::time_point startTime;
        bool isHovered;
        bool isClosing;
        float currentOpacity;
        Point2D currentOffset;

        // Layout areas
        Rect2D iconRect;
        Rect2D textRect;
        Rect2D closeButtonRect;
        Rect2D contentRect;

    public:
        UltraCanvasToast(const std::string& identifier, long id, long x, long y, long w, long h);
        virtual ~UltraCanvasToast();

        // ===== CONFIGURATION =====
        void SetConfig(const ToastConfig& toastConfig);
        ToastConfig GetConfig() const;
        void SetMessage(const std::string& message);
        void SetTitle(const std::string& title);
        void SetType(ToastType type);
        void SetDuration(int durationMs);

        // ===== LIFECYCLE =====
        void Show();
        void Close();
        void Reset();
        bool IsExpired() const;
        bool IsVisible() const override;
        int GetRemainingTime() const;
        float GetProgress() const; // 0.0 - 1.0

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx) override;
        bool OnEvent(const UCEvent& event) override;
        void Update(float deltaTime);

        // ===== PROPERTIES =====
        ULTRACANVAS_STANDARD_PROPERTIES_ACCESSORS()

    private:
        // ===== INTERNAL METHODS =====
        void CalculateLayout();
        void UpdateAnimation(float deltaTime);
        void RenderBackground();
        void RenderIcon();
        void RenderText();
        void RenderCloseButton();
        Color GetTypeColor() const;
        std::string GetTypeIcon() const;
        void HandleClick(const Point2D& clickPos);
    };

// ===== TOAST MANAGER CLASS =====
    class UltraCanvasToastManager {
    private:
        // Static member declarations only - definitions in .cpp
        static std::vector<std::shared_ptr<UltraCanvasToast>> activeToasts;
        static ToastPosition defaultPosition;
        static ToastStyle defaultStyle;
        static bool enabled;
        static Point2D screenBounds;
        static int maxToasts;
        static bool pauseOnAnyHover;

    public:
        // ===== TOAST CREATION =====
        static std::shared_ptr<UltraCanvasToast> ShowToast(const std::string& message,
                                                           ToastType type = ToastType::Info,
                                                           int duration = 3000);

        static std::shared_ptr<UltraCanvasToast> ShowToast(const ToastConfig& config);

        static std::shared_ptr<UltraCanvasToast> ShowInfo(const std::string& message, int duration = 3000);
        static std::shared_ptr<UltraCanvasToast> ShowSuccess(const std::string& message, int duration = 3000);
        static std::shared_ptr<UltraCanvasToast> ShowWarning(const std::string& message, int duration = 4000);
        static std::shared_ptr<UltraCanvasToast> ShowError(const std::string& message, int duration = 5000);

        // ===== TOAST MANAGEMENT =====
        static void CloseToast(std::shared_ptr<UltraCanvasToast> toast);
        static void CloseAllToasts();
        static void ClearExpiredToasts();
        static int GetActiveToastCount();
        static std::vector<std::shared_ptr<UltraCanvasToast>> GetActiveToasts();

        // ===== CONFIGURATION =====
        static void SetDefaultPosition(ToastPosition position);
        static void SetDefaultStyle(const ToastStyle& style);
        static void SetMaxToasts(int maxCount);
        static void SetScreenBounds(const Point2D& bounds);
        static void SetEnabled(bool enable);
        static void SetPauseOnAnyHover(bool pause);

        static ToastPosition GetDefaultPosition();
        static ToastStyle GetDefaultStyle();
        static int GetMaxToasts();
        static Point2D GetScreenBounds();
        static bool IsEnabled();

        // ===== SYSTEM METHODS =====
        static void Update(float deltaTime);
        static void Render(IRenderContext* ctx);
        static void Initialize();
        static void Shutdown();

    private:
        // ===== INTERNAL HELPERS =====
        static Point2D CalculateToastPosition(const ToastConfig& config, const Point2D& toastSize, int index);
        static void RepositionToasts();
        static void AddToast(std::shared_ptr<UltraCanvasToast> toast);
        static void RemoveToast(std::shared_ptr<UltraCanvasToast> toast);
        static void EnforceMaxToasts();
    };

// ===== CONVENIENCE FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasToast> ShowToast(const std::string& message,
                                                       ToastType type = ToastType::Info,
                                                       int duration = 3000) {
        return UltraCanvasToastManager::ShowToast(message, type, duration);
    }

    inline std::shared_ptr<UltraCanvasToast> CreateToast(const std::string& identifier, long id,
                                                         const std::string& message,
                                                         long x, long y, long w = 300, long h = 60) {
        auto toast = std::make_shared<UltraCanvasToast>(identifier, id, x, y, w, h);
        toast->SetMessage(message);
        return toast;
    }

// ===== CONVENIENCE FUNCTIONS =====
    inline void ShowInfoToast(const std::string& message, int duration = 3000) {
        UltraCanvasToastManager::ShowToast(message, ToastType::Info, duration);
    }

    inline void ShowSuccessToast(const std::string& message, int duration = 3000) {
        UltraCanvasToastManager::ShowToast(message, ToastType::Success, duration);
    }

    inline void ShowWarningToast(const std::string& message, int duration = 4000) {
        UltraCanvasToastManager::ShowToast(message, ToastType::Warning, duration);
    }

    inline void ShowErrorToast(const std::string& message, int duration = 5000) {
        UltraCanvasToastManager::ShowToast(message, ToastType::Error, duration);
    }

// ===== LEGACY C-STYLE INTERFACE =====
    extern "C" {
    void ShowToast(const char* message, int durationMs);
    void ShowToastType(const char* message, int type, int durationMs);
    void ClearAllToasts();
    void UpdateToasts(float deltaTime);
    }

} // namespace UltraCanvas