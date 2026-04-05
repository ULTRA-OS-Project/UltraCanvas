// core/UltraCanvasSplashScreen.cpp
// Reusable splash screen component
// Version: 1.0.1
// Last Modified: 2026-04-05
// Author: UltraCanvas Framework

#include "UltraCanvasSplashScreen.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasBoxLayout.h"
#include "UltraCanvasUtils.h"

namespace UltraCanvas {

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
        wc.modal = true;
        wc.parentWindow = parentWin;
        wc.backgroundColor = config.backgroundColor;

        window = std::make_shared<UltraCanvasWindow>();
        if (!window->Create(wc)) {
            window.reset();
            return;
        }

        // Build layout
        auto layout = CreateVBoxLayout(window.get());
        layout->SetSpacing(4);
        window->SetPadding(20);
        window->SetBorders(1, Colors::Black);

        layout->AddStretch(1);

        // Logo image
        if (!config.imagePath.empty()) {
            auto logo = std::make_shared<UltraCanvasImageElement>("SplashLogo", 0, 0, 0, 250, 250);
            logo->LoadFromFile(config.imagePath);
            logo->SetFitMode(ImageFitMode::Contain);
            logo->SetMargin(0, 0, 12, 0);
            layout->AddUIElement(logo)->SetCrossAlignment(LayoutAlignment::Center);
        }

        // Title
        if (!config.title.empty()) {
            auto titleLabel = std::make_shared<UltraCanvasLabel>("SplashTitle", 300, 25, config.title);
            titleLabel->SetFontSize(20);
            titleLabel->SetFontWeight(FontWeight::Bold);
            titleLabel->SetAlignment(TextAlignment::Center);
            titleLabel->SetMargin(0, 0, 4, 0);
            layout->AddUIElement(titleLabel)->SetWidthMode(SizeMode::Fill);
        }

        // Version
        if (!config.version.empty()) {
            auto versionLabel = std::make_shared<UltraCanvasLabel>("SplashVersion", 300, 20, "Version " + config.version);
            versionLabel->SetFontSize(11);
            versionLabel->SetTextColor(Color(100, 100, 100));
            versionLabel->SetAlignment(TextAlignment::Center);
            versionLabel->SetMargin(0, 0, 10, 0);
            layout->AddUIElement(versionLabel)->SetWidthMode(SizeMode::Fill);
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
            layout->AddUIElement(urlLabel)->SetWidthMode(SizeMode::Fill)->SetCrossAlignment(LayoutAlignment::Center);
        }

        layout->AddStretch(1);

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
        if (config.showTimeout.count() > 0) {
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
