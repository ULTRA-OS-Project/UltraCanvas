// Apps/MobilePC/MobilePCControlPad.cpp
// The phone's control pad: built from UltraCanvas elements, its touchpad a
// container that passes the fingers on to the desktop's touchpad.
// Version: 0.1.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework / ULTRA OS

#include "MobilePCControlPad.h"

// The application header first: the Android one it ends with needs its base.
#include "UltraCanvasApplication.h"
#include "UltraCanvasAndroidDesktop.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTheme.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <chrono>

#ifndef MOBILEPC_VERSION
#error "MOBILEPC_VERSION is not defined: build through CMake, which reads it from Docs/MobilePC/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace MobilePC {

    namespace {
        int64_t NowMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }

    // ===== TOUCHPAD =====

    MobilePCTouchpad::MobilePCTouchpad(const std::string& identifier)
        : UltraCanvasContainer(identifier) {}

    Point2Df MobilePCTouchpad::Centre() const {
        Point2Df sum(0, 0);
        for (const auto& [id, at] : fingers) {
            sum.x += at.x;
            sum.y += at.y;
        }
        const float n = fingers.empty() ? 1.0f : static_cast<float>(fingers.size());
        return Point2Df(sum.x / n, sum.y / n);
    }

    bool MobilePCTouchpad::OnEvent(const UCEvent& event) {
        UltraCanvasAndroidDesktop& desktop = UltraCanvasAndroidDesktop::Get();
        using Phase = UltraCanvasAndroidDesktop::TouchPhase;
        const Point2Df at(static_cast<float>(event.pointerWindow.x), static_cast<float>(event.pointerWindow.y));
        switch (event.type) {
            case UCEventType::TouchStart:
                fingers[event.pointerId] = at;
                desktop.TouchpadInput(Phase::Down, static_cast<int>(fingers.size()),
                                      Centre().x, Centre().y, NowMs());
                return true;
            case UCEventType::TouchMove:
                if (!fingers.count(event.pointerId)) return true;
                fingers[event.pointerId] = at;
                desktop.TouchpadInput(Phase::Move, static_cast<int>(fingers.size()),
                                      Centre().x, Centre().y, NowMs());
                return true;
            case UCEventType::TouchEnd:
                fingers.erase(event.pointerId);
                desktop.TouchpadInput(Phase::Up, static_cast<int>(fingers.size()),
                                      Centre().x, Centre().y, NowMs());
                return true;
            // The same finger also arrives as the mouse (the phone backend
            // drives every element through it); here the touches say it all.
            case UCEventType::MouseDown:
            case UCEventType::MouseUp:
            case UCEventType::MouseMove:
            case UCEventType::MouseDoubleClick:
            case UCEventType::PinchZoom:
                return true;
            default:
                return UltraCanvasContainer::OnEvent(event);
        }
    }

    // ===== THE PAD =====

    bool MobilePCControlPad::Create() {
        WindowConfig config;
        config.title = "MobilePC " MOBILEPC_VERSION;
        config.type = WindowType::Fullscreen;
        config.width = 400;
        config.height = 800;
        window = UltraCanvasAndroidDesktop::Get().CreatePhoneWindow(config);
        if (!window) return false;

        const ThemeRoles& roles = UltraCanvasTheme::Current().roles;
        window->SetBackgroundColor(roles.ground);
        window->layout.SetFlexColumn().SetFlexGap(10);
        window->SetPadding(14);

        status = std::make_shared<UltraCanvasLabel>("PadStatus", Tr("Connect a TV or monitor"));
        status->SetFontWeight(FontWeight::Bold);
        status->SetAlignment(TextAlignment::Center);
        window->AddChild(status);

        // Keyboard and Apps, above the touchpad.
        auto top = std::make_shared<UltraCanvasContainer>("PadTop");
        top->layout.SetFlexRow().SetFlexGap(10);
        auto keyboard = std::make_shared<UltraCanvasButton>("PadKeyboard", Tr("Keyboard"));
        keyboard->layoutItem.SetFlexGrow(1);
        keyboard->onClick = []() {
            // The phone's keyboard; what it types goes to the window in front
            // on the TV.
            if (auto* app = UltraCanvasAndroidApplication::GetInstance()) app->ShowSoftKeyboard();
        };
        auto apps = std::make_shared<UltraCanvasButton>("PadApps", Tr("Apps"));
        apps->layoutItem.SetFlexGrow(1);
        apps->onClick = [this]() { if (onApps) onApps(); };
        top->AddChild(keyboard);
        top->AddChild(apps);
        window->AddChild(top);

        touchpad = std::make_shared<MobilePCTouchpad>("PadTouchpad");
        touchpad->layoutItem.SetFlexGrow(1);
        touchpad->SetBackgroundColor(roles.well);
        touchpad->SetBorders(1.0f, roles.line, 14.0f);
        touchpad->layout.SetFlexColumn().SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
        auto hint = std::make_shared<UltraCanvasLabel>(
                "PadHint", Tr("Move one finger to move the pointer. Tap to click, tap with two fingers for "
                              "the right button, move two fingers to scroll."));
        hint->SetAlignment(TextAlignment::Center);
        hint->SetTextColor(roles.inkMuted);
        hint->SetWrap(TextWrap::WrapWord);
        hint->SetMargin(0, 24, 0, 24);
        touchpad->AddChild(hint);
        window->AddChild(touchpad);

        // The touchpad's buttons, as on a laptop.
        auto bottom = std::make_shared<UltraCanvasContainer>("PadButtons");
        bottom->layout.SetFlexRow().SetFlexGap(10);
        auto left = std::make_shared<UltraCanvasButton>("PadLeft", 0, 64, Tr("Left click"));
        left->layoutItem.SetFlexGrow(1);
        left->onClick = []() { UltraCanvasAndroidDesktop::Get().Click(UCMouseButton::Left); };
        auto right = std::make_shared<UltraCanvasButton>("PadRight", 0, 64, Tr("Right click"));
        right->layoutItem.SetFlexGrow(1);
        right->onClick = []() { UltraCanvasAndroidDesktop::Get().Click(UCMouseButton::Right); };
        bottom->AddChild(left);
        bottom->AddChild(right);
        window->AddChild(bottom);

        window->Show();
        ShowOutput(UltraCanvasAndroidDesktop::Get().GetDesktopOutput() ==
                   UltraCanvasAndroidDesktop::Output::External);
        return true;
    }

    void MobilePCControlPad::ShowOutput(bool external) {
        if (!status) return;
        status->SetText(external ? Tr("The desktop is on the TV or monitor")
                                 : Tr("Connect a TV or monitor"));
    }

} // namespace MobilePC
