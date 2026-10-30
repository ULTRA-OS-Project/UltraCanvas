// Apps/UltraDesktop/ui/UltraDesktopPopupPanel.cpp
// The bars' popup panels' shared window. See the header.
// Version: 0.1.1 - SetScreenArea
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopPopupPanel.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasEvent.h"

#include <algorithm>

using namespace UltraCanvas;

namespace UltraDesktop {

namespace {
const Color kPanel(31, 31, 31);
} // namespace

UltraDesktopPopupPanel::UltraDesktopPopupPanel(std::string title, int width, int height)
    : title_(std::move(title)), width_(width), height_(height) {}

UltraDesktopPopupPanel::~UltraDesktopPopupPanel() {
    if (window_) {
        window_->onWindowClosed = nullptr;
        window_->onWindowBlur = nullptr;
        if (open_) window_->Close();
    }
}

void UltraDesktopPopupPanel::Open(int rightX, int topY) {
    if (IsOpen()) {
        window_->RaiseAndFocus();
        return;
    }
    WindowConfig config;
    config.title = title_;
    config.type = WindowType::Borderless;
    config.width = width_;
    config.height = height_;
    config.resizable = false;
    config.minimizable = false;
    config.maximizable = false;
    config.minWidth = width_;
    config.minHeight = height_;
    config.backgroundColor = kPanel;
    int areaX = areaX_, areaY = areaY_, areaW = areaW_, areaH = areaH_;
    if (areaW <= 0 || areaH <= 0) {
        areaX = areaY = 0;
        UltraCanvasDesktopShell::GetScreenSize(areaW, areaH);
    }
    window_ = CreateWindow(config);
    if (!window_) return;
    const int physW = window_->LogicalToPhysical(width_);
    const int physH = window_->LogicalToPhysical(height_);
    int x = rightX - physW;
    int y = topY;
    if (areaW > 0) x = std::clamp(x, areaX, std::max(areaX, areaX + areaW - physW));
    if (areaH > 0) y = std::clamp(y, areaY, std::max(areaY, areaY + areaH - physH));
    window_->SetWindowPosition(x, y);

    window_->SetBackgroundColor(kPanel);
    Build(*window_);
    open_ = true;
    closing_ = false;
    window_->onWindowClosed = [this]() {
        open_ = false;
        Unbuild();
        window_.reset();
    };
    // Gone when something else is chosen, like a menu or another window.
    window_->onWindowBlur = [this]() { CloseSoon(); };
    window_->InstallEventFilter("popup-panel-keys", [this](const UCEvent& event) { return OnKey(event); },
                                {UCEventType::KeyDown});
    window_->Show();
    window_->RaiseAndFocus();
}

void UltraDesktopPopupPanel::SetScreenArea(int x, int y, int width, int height) {
    areaX_ = x;
    areaY_ = y;
    areaW_ = width;
    areaH_ = height;
}

void UltraDesktopPopupPanel::Close() {
    if (!window_) return;
    open_ = false;
    window_->onWindowBlur = nullptr;
    window_->Close();
}

void UltraDesktopPopupPanel::CloseSoon() {
    if (closing_ || !IsOpen()) return;
    closing_ = true;
    UltraCanvasApplication::GetInstance()->StartTimer(1, false, [this](TimerId) {
        closing_ = false;
        Close();
    });
}

bool UltraDesktopPopupPanel::OnKey(const UCEvent& event) {
    if (event.type == UCEventType::KeyDown && event.virtualKey == UCKeys::Escape) {
        CloseSoon();
        return true;
    }
    return false;
}

} // namespace UltraDesktop
