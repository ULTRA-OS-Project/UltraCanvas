// include/UltraCanvasSplashScreen.h
// Reusable splash screen component
// Version: 1.1.0
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasWindow.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasTimer.h"
#include <string>
#include <memory>
#include <functional>
#include <chrono>

namespace UltraCanvas { class UltraCanvasApplicationBase; }

namespace UltraCanvas {

    struct SplashScreenConfig {
        std::string imagePath;
        std::string title;
        std::string version;

        // Attribution block, drawn between the version and the website line:
        // a caption, a logo and the name of whoever is being credited — e.g.
        // "GUI by" / the UltraCanvas logo / "Ultra Canvas". Each part is
        // optional; leave all three empty for no attribution at all.
        std::string attributionText;
        std::string attributionImagePath;
        std::string attributionName;

        std::string websiteURL;
        std::string websiteDisplay;

        int logoSize = 250;            // Main logo box, square, in pixels
        int attributionLogoSize = 90;  // Attribution logo box, square, in pixels

        // Type scale. The defaults reproduce the layout every existing splash
        // already has; a host with its own design raises the ones it needs.
        int titleFontSize = 20;
        int versionFontSize = 11;
        int attributionFontSize = 13;
        int attributionNameFontSize = 11;

        // The two secondary lines — the version and the attributed name.
        Color secondaryTextColor = Color(100, 100, 100);

        int width = 400;
        int height = 300;
        unsigned int showTimeout = 0;  // Auto-close after this duration (0 = no timeout)
        Color backgroundColor = Color(255, 255, 255);
    };

    class UltraCanvasSplashScreen {
    public:
        UltraCanvasSplashScreen() = default;
        ~UltraCanvasSplashScreen();

        void Show(const SplashScreenConfig& config, UltraCanvasWindowBase* parentWin = nullptr);
        void Close();
        bool IsVisible() const;

        std::function<void()> onSplashClosed;

    private:
        std::shared_ptr<UltraCanvasWindow> window;
        TimerId timeoutTimerId = InvalidTimerId;
    };

}
