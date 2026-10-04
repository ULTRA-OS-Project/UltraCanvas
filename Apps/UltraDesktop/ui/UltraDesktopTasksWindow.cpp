// Apps/UltraDesktop/ui/UltraDesktopTasksWindow.cpp
// The Task Manager window. See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

// Before the window header: on Linux that one reaches X11, whose `None`
// macro would otherwise break HardwareQuery::None in this header.
#include "UltraCanvasHardwareInfoPanel.h"

#include "UltraDesktopTasksWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <sstream>

using namespace UltraCanvas;

namespace UltraDesktop {
namespace {

constexpr int kWindowWidth = 820;
constexpr int kWindowHeight = 600;
constexpr unsigned kTimerMs = 500;
constexpr int kListTicks = 4;        // the window list every 2 s
constexpr int kSensorTicks = 6;      // the sensors every 3 s

} // namespace

UltraDesktopTasksWindow::UltraDesktopTasksWindow() = default;

UltraDesktopTasksWindow::~UltraDesktopTasksWindow() {
    if (timer_ != 0) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(timer_);
    }
}

void UltraDesktopTasksWindow::Open() {
    WindowConfig config;
    config.title = "Task Manager";
    config.width = kWindowWidth;
    config.height = kWindowHeight;
    config.minWidth = 520;
    config.minHeight = 360;
    window_ = CreateWindow(config);
    if (!window_) return;
    open_ = true;
    window_->onWindowClosed = [this]() {
        open_ = false;
        if (timer_ != 0) {
            if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(timer_);
            timer_ = 0;
        }
    };

    auto tabs = CreateTabbedContainer("tmTabs", 0, 0, 0, 0);

    windowRows_ = CreateScrollableContainer("tmWindows", 0, 0, 0, 0, true, false);
    windowRows_->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    windowRows_->SetPadding(8);
    tabs->AddTab("Windows", windowRows_);

    hardware_ = CreateHardwareInfoPanel("tmHardware", 0, 0, 0, 0, HardwareQuery::All);
    tabs->AddTab("Hardware", hardware_);

    window_->AddChild(tabs);
    {
        // Sized to the window explicitly and on every resize: a percentage
        // would resolve against nothing at the window level.
        auto fit = [page = tabs.get(), win = window_.get()](int w, int h) {
            page->SetElementAbsolutePosition(Point2Df(0, 0));
            page->SetElementSize(Size2Df(static_cast<float>(std::max(1, w)), static_cast<float>(std::max(1, h))));
            page->InvalidateLayout();
            win->AddDirtyRectangle(Rect2Di(0, 0, w, h));
        };
        int w = 0, h = 0;
        window_->GetWindowSize(w, h);
        fit(w, h);
        window_->onWindowResize = fit;
    }
    RebuildWindowList();
    window_->Show();

    if (auto* app = UltraCanvasApplication::GetInstance()) {
        timer_ = app->StartTimer(kTimerMs, /*periodic=*/true, [this](TimerId) { OnTimer(); });
    }
}

void UltraDesktopTasksWindow::Raise() {
    if (window_) window_->RaiseAndFocus();
}

void UltraDesktopTasksWindow::OnTimer() {
    if (!open_) return;
    ++ticks_;
    if (ticks_ % kListTicks == 0) RebuildWindowList();
    if (ticks_ % kSensorTicks == 0 && hardware_) hardware_->RefreshSensors();
}

void UltraDesktopTasksWindow::RebuildWindowList() {
    if (!windowRows_) return;
    const auto windows = UltraCanvasDesktopShell::ListWindows();

    // Rebuild only when the list changed: rows are buttons the user may be
    // about to click.
    std::ostringstream signature;
    for (const DesktopWindowInfo& w : windows) {
        if (w.skipTaskbar) continue;
        signature << w.id << '|' << w.title << '|' << w.active << '|' << w.minimized << '|' << w.virtualDesktop << '\n';
    }
    if (signature.str() == lastListing_) return;
    lastListing_ = signature.str();

    windowRows_->ClearChildren();
    int shown = 0;
    for (const DesktopWindowInfo& w : windows) {
        if (w.skipTaskbar) continue;
        auto row = CreateContainer("tmRow" + std::to_string(w.id), 0, 0, 0, 34);
        row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(34));
        row->layoutItem.SetFlexShrink(0.0f);
        row->SetBackgroundColor(w.active ? Color(225, 235, 255, 255) : Color(245, 245, 245, 255));
        row->SetBorders(0, Colors::Transparent, 6);
        row->SetPadding(4, 8);

        auto name = CreateLabel("tmName" + std::to_string(w.id),
                                (w.title.empty() ? w.appClass : w.title));
        name->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
        name->SetTooltip(w.appClass + (w.processId > 0 ? " (pid " + std::to_string(w.processId) + ")" : ""));
        row->AddChild(name);

        std::string where = w.virtualDesktop < 0 ? "every desktop" : "desktop " + std::to_string(w.virtualDesktop + 1);
        if (w.minimized) where += ", minimized";
        auto state = CreateLabel("tmState" + std::to_string(w.id), where);
        state->SetTextColor(Color(110, 110, 110, 255));
        row->AddChild(state);

        const uint64_t id = w.id;
        auto activate = CreateButton("tmActivate" + std::to_string(w.id), 0, 0, 80, 26, "Activate");
        activate->onClick = [id]() { UltraCanvasDesktopShell::ActivateWindow(id); };
        row->AddChild(activate);
        auto close = CreateButton("tmClose" + std::to_string(w.id), 0, 0, 70, 26, "Close");
        close->onClick = [id]() { UltraCanvasDesktopShell::CloseWindow(id); };
        row->AddChild(close);

        windowRows_->AddChild(row);
        ++shown;
    }
    if (shown == 0) {
        auto none = CreateLabel("tmNone", UltraCanvasDesktopShell::IsAvailable()
                ? "No application windows are open." : "The window list is not available on this platform.");
        none->SetTextColor(Color(110, 110, 110, 255));
        windowRows_->AddChild(none);
    }
    windowRows_->InvalidateLayout();
    windowRows_->RequestRedraw();
}

} // namespace UltraDesktop
