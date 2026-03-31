// include/UltraCanvasSplashScreen.h
// Reusable splash screen component
// Version: 1.0.0
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasWindow.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {

    struct SplashScreenConfig {
        std::string imagePath;
        std::string title;
        std::string version;
        std::string websiteURL;
        std::string websiteDisplay;
        int width = 400;
        int height = 300;
        Color backgroundColor = Color(255, 255, 255);
    };

    class UltraCanvasSplashScreen {
    public:
        UltraCanvasSplashScreen() = default;
        ~UltraCanvasSplashScreen();

        void Show(const SplashScreenConfig& config);
        void Close();
        bool IsVisible() const;

        std::function<void()> onClosed;

    private:
        std::shared_ptr<UltraCanvasWindow> window;
    };

}
