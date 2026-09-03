// core/UltraCanvasSplashScreen.cpp
// Reusable splash screen component
// Version: 1.1.0 - Optional attribution block ("GUI by" / logo / name), plus
//                 configurable logo sizes and type scale, for hosts that
//                 credit the toolkit under their own branding
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework

#include "UltraCanvasSplashScreen.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasUtils.h"

namespace UltraCanvas {

    namespace {
        // A label's box grows with its font, so raising the type scale cannot
        // clip the text. At the default font size this returns exactly the
        // height the splash has always used, leaving existing callers alone.
        float SplashLabelHeight(int fontSize, int defaultFontSize, float defaultHeight) {
            if (fontSize <= defaultFontSize) return defaultHeight;
            return defaultHeight * (float)fontSize / (float)defaultFontSize;
        }
    }

    UltraCanvasSplashScreen::~UltraCanvasSplashScreen() {
        Close();
    }

    void UltraCanvasSplashScreen::Show(const SplashScreenConfig& config, UltraCanvasWindowBase* parentWin) {
        if (window) return;

        // Create borderless window
        WindowConfig wc;
        wc.title = config.title;
        wc.width = config.width;
        wc.height = config.height;
        wc.type = WindowType::Borderless;
        wc.resizable = false;
        wc.minimizable = false;
        wc.maximizable = false;
        wc.closable = false;
        wc.deleteOnClose = true;
        wc.alwaysOnTop = true;
        wc.parentWindow = parentWin;
        wc.backgroundColor = config.backgroundColor;

        window = std::make_shared<UltraCanvasWindow>();
        if (!window->Create(wc)) {
            window.reset();
            return;
        }

        // Default-constructed config: the reference type scale the label box
        // heights below are proportioned against.
        static const SplashScreenConfig defaults{};

        // Build layout: column flex, items horizontally stretched to container.
        window->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        window->SetPadding(20);
        window->SetBorders(1, Colors::Black);

        window->AddStretchSpacer(1);

        // Logo image
        if (!config.imagePath.empty()) {
            auto logo = std::make_shared<UltraCanvasImageElement>("SplashLogo",
                                                                  (float)config.logoSize,
                                                                  (float)config.logoSize);
            logo->LoadFromFile(config.imagePath);
            logo->SetFitMode(ImageFitMode::Contain);
            logo->SetMargin(0, 0, 12, 0);
            window->AddChild(logo);
            logo->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        }

        // Title
        if (!config.title.empty()) {
            auto titleLabel = std::make_shared<UltraCanvasLabel>("SplashTitle", 300,
                    SplashLabelHeight(config.titleFontSize, defaults.titleFontSize, 25.0f),
                    config.title);
            titleLabel->SetFontSize((float)config.titleFontSize);
            titleLabel->SetFontWeight(FontWeight::Bold);
            titleLabel->SetAlignment(TextAlignment::Center);
            titleLabel->SetMargin(0, 0, 4, 0);
            window->AddChild(titleLabel);
        }

        // Version
        if (!config.version.empty()) {
            auto versionLabel = std::make_shared<UltraCanvasLabel>("SplashVersion", 300,
                    SplashLabelHeight(config.versionFontSize, defaults.versionFontSize, 20.0f),
                    "Version " + config.version);
            versionLabel->SetFontSize((float)config.versionFontSize);
            versionLabel->SetTextColor(config.secondaryTextColor);
            versionLabel->SetAlignment(TextAlignment::Center);
            versionLabel->SetMargin(0, 0, 10, 0);
            window->AddChild(versionLabel);
        }

        // Attribution block: a caption, a logo and the credited name, e.g.
        // "GUI by" / the UltraCanvas logo / "Ultra Canvas". Any part may be
        // omitted; omitting all three leaves the splash exactly as it was.
        if (!config.attributionText.empty()) {
            auto attributionLabel = std::make_shared<UltraCanvasLabel>("SplashAttribution", 300,
                    SplashLabelHeight(config.attributionFontSize, defaults.attributionFontSize, 20.0f),
                    config.attributionText);
            attributionLabel->SetFontSize((float)config.attributionFontSize);
            attributionLabel->SetAlignment(TextAlignment::Center);
            attributionLabel->SetMargin(0, 0, 8, 0);
            window->AddChild(attributionLabel);
        }

        if (!config.attributionImagePath.empty()) {
            auto attributionLogo = std::make_shared<UltraCanvasImageElement>("SplashAttributionLogo",
                                                                            (float)config.attributionLogoSize,
                                                                            (float)config.attributionLogoSize);
            attributionLogo->LoadFromFile(config.attributionImagePath);
            attributionLogo->SetFitMode(ImageFitMode::Contain);
            attributionLogo->SetMargin(0, 0, 6, 0);
            window->AddChild(attributionLogo);
            attributionLogo->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        }

        if (!config.attributionName.empty()) {
            auto attributionName = std::make_shared<UltraCanvasLabel>("SplashAttributionName", 300,
                    SplashLabelHeight(config.attributionNameFontSize, defaults.attributionNameFontSize, 20.0f),
                    config.attributionName);
            attributionName->SetFontSize((float)config.attributionNameFontSize);
            attributionName->SetTextColor(config.secondaryTextColor);
            attributionName->SetAlignment(TextAlignment::Center);
            attributionName->SetMargin(0, 0, 10, 0);
            window->AddChild(attributionName);
        }

        // Website URL
        if (!config.websiteURL.empty()) {
            std::string displayText = config.websiteDisplay.empty() ? config.websiteURL : config.websiteDisplay;
            auto urlLabel = std::make_shared<UltraCanvasLabel>("SplashURL", 300, 20);
            urlLabel->SetText("<span color=\"blue\">" + displayText + "</span>");
            urlLabel->SetTextIsMarkup(true);
            urlLabel->SetFontSize(11);
            urlLabel->SetAlignment(TextAlignment::Center);
            urlLabel->SetMouseCursor(UCMouseCursor::Hand);
            std::string url = config.websiteURL;
            urlLabel->onClick = [url]() {
                OpenURL(url);
            };
            window->AddChild(urlLabel);
        }

        window->AddStretchSpacer(1);

        // Click anywhere to dismiss
        window->eventCallback = [this](const UCEvent& event) -> bool {
            if (event.type == UCEventType::MouseDown) {
                Close();
                return true;
            }
            return false;
        };

        // Fire onSplashClosed when the window is closed and deleted (by any means)
        window->onWindowClosed = [this]() {
            if (timeoutTimerId != InvalidTimerId) {
                UltraCanvasApplication::GetInstance()->StopTimer(timeoutTimerId);
                timeoutTimerId = InvalidTimerId;
            }
            window.reset();
            if (onSplashClosed) {
                onSplashClosed();
            }
        };

        window->Show();
        // Center on the parent window so the splash stays on the same monitor as
        // the main window in multi-monitor setups. CenterOnParent falls back to
        // CenterOnScreen when parentWin is null.
        window->CenterOnScreenOfWindow(parentWin);

        // Start auto-close timer if a timeout was specified
        if (config.showTimeout > 0) {
            timeoutTimerId = UltraCanvasApplication::GetInstance()->StartTimer(
                config.showTimeout, false, [this](TimerId) {
                    timeoutTimerId = InvalidTimerId;
                    Close();
                });
        }
    }

    void UltraCanvasSplashScreen::Close() {
        if (!window) return;

        auto w = window;
        window.reset();
        w->PerformClose();
    }

    bool UltraCanvasSplashScreen::IsVisible() const {
        return window != nullptr;
    }
}
