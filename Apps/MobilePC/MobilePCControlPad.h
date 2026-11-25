// Apps/MobilePC/MobilePCControlPad.h
// What the phone shows while the desktop is on the TV or monitor: a touchpad
// (one finger moves the pointer there, a tap clicks, two fingers scroll, a tap
// of two fingers is a right click), left and right buttons, the keyboard
// button (the phone's keyboard types into the window in front on the TV), the
// Apps button and a line saying where the desktop is.
// Version: 0.1.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasContainer.h"

#include <functional>
#include <map>
#include <memory>

namespace UltraCanvas {
    class UltraCanvasWindowBase;
    class UltraCanvasLabel;
    class UltraCanvasButton;
}

namespace MobilePC {

    // The touchpad: a panel that turns the fingers on it into the desktop's
    // pointer motion, clicks and scrolling (UltraCanvasAndroidDesktop's
    // touchpad, which also does the gestures).
    class MobilePCTouchpad : public UltraCanvas::UltraCanvasContainer {
    public:
        explicit MobilePCTouchpad(const std::string& identifier);
        bool OnEvent(const UltraCanvas::UCEvent& event) override;

    private:
        // Where each finger is, in window coordinates, by its pointer id.
        std::map<int, UltraCanvas::Point2Df> fingers;
        UltraCanvas::Point2Df Centre() const;
    };

    class MobilePCControlPad {
    public:
        // The phone's window (UltraCanvasAndroidDesktop::CreatePhoneWindow).
        bool Create();
        // The desktop moved to the external display or back to the phone.
        void ShowOutput(bool external);

        // The Apps button.
        std::function<void()> onApps;

    private:
        std::shared_ptr<UltraCanvas::UltraCanvasWindowBase> window;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> status;
        std::shared_ptr<MobilePCTouchpad> touchpad;
    };

} // namespace MobilePC
