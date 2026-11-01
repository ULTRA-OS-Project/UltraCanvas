// Apps/UltraDesktop/ui/UltraDesktopTasksWindow.cpp
// The Task Manager window. See the header.
// Version: 0.2.0 - the window list: a column header; a row has its
//                  application's icon first, its desktop as a number in a
//                  bordered box, "Show" for "Activate"
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasHardwareInfoPanel.h"

#include "UltraDesktopTasksWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <cctype>
#include <sstream>

using namespace UltraCanvas;

namespace UltraDesktop {
namespace {

constexpr int kWindowWidth = 820;
constexpr int kWindowHeight = 600;
constexpr unsigned kTimerMs = 500;
constexpr int kListTicks = 4;        // the window list every 2 s
constexpr int kSensorTicks = 6;      // the sensors every 3 s

// The window list's columns, shared by the header and the rows.
constexpr int kColumnGap = 8;
constexpr int kIconColumn = 22;
constexpr int kStateColumn = 80;
constexpr int kDesktopColumn = 76;
constexpr int kButtonWidth = 70;
constexpr int kActionsColumn = kButtonWidth * 2 + kColumnGap;

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

    // The Windows page: the column header, then the rows, which scroll
    // under it.
    auto windowsPage = CreateContainer("tmWindowsPage", 0, 0, 0, 0);
    windowsPage->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    auto header = CreateContainer("tmHeader", 0, 0, 0, 28);
    header->layout.SetFlexRow().SetFlexGap(kColumnGap).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    header->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(28));
    header->layoutItem.SetFlexShrink(0.0f);
    header->SetBackgroundColor(Color(232, 232, 232, 255));
    // Lined up with the rows: the page's padding plus the row's.
    header->SetPadding(4, 8 + 8, 4, 8 + 8);
    auto column = [&header](const std::string& id, const std::string& text, int width, bool grow) {
        auto label = CreateLabel(id, 0, 0, width, 20, text);
        label->SetFontWeight(FontWeight::Bold);
        label->SetTextColor(Color(70, 70, 70, 255));
        if (grow) {
            label->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
        } else {
            label->layoutItem.SetFlexShrink(0.0f);
        }
        header->AddChild(label);
        return label;
    };
    column("tmHeadIcon", "", kIconColumn, false);
    column("tmHeadName", "Window", 100, true);
    column("tmHeadState", "State", kStateColumn, false);
    column("tmHeadDesktop", "Desktop", kDesktopColumn, false)->SetAlignment(TextAlignment::Center);
    column("tmHeadActions", "", kActionsColumn, false);
    windowsPage->AddChild(header);

    windowRows_ = CreateScrollableContainer("tmWindows", 0, 0, 0, 0, true, false);
    windowRows_->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    windowRows_->SetPadding(8);
    windowRows_->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    windowsPage->AddChild(windowRows_);
    tabs->AddTab("Windows", windowsPage);

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
        signature << w.id << '|' << w.title << '|' << w.active << '|' << w.minimized << '|' << w.virtualDesktop
                  << '|' << w.iconFile << '\n';
    }
    if (signature.str() == lastListing_) return;
    lastListing_ = signature.str();

    windowRows_->ClearChildren();
    int shown = 0;
    for (const DesktopWindowInfo& w : windows) {
        if (w.skipTaskbar) continue;
        auto row = CreateContainer("tmRow" + std::to_string(w.id), 0, 0, 0, 34);
        row->layout.SetFlexRow().SetFlexGap(kColumnGap).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(34));
        row->layoutItem.SetFlexShrink(0.0f);
        row->SetBackgroundColor(w.active ? Color(225, 235, 255, 255) : Color(245, 245, 245, 255));
        row->SetBorders(0, Colors::Transparent, 6);
        row->SetPadding(4, 8);

        const std::string key = std::to_string(w.id);

        // The application's own icon, in its own colours; its first letter
        // in a box where it has none.
        if (!w.iconFile.empty()) {
            auto icon = CreateImageElement("tmIcon" + key, 0, 0, kIconColumn, kIconColumn);
            icon->LoadFromFile(w.iconFile);
            icon->SetFitMode(ImageFitMode::Contain);
            icon->layoutItem.SetFlexShrink(0.0f);
            row->AddChild(icon);
        } else {
            const std::string& source = !w.appClass.empty() ? w.appClass : w.title;
            std::string letter = source.empty() ? "?" : source.substr(0, 1);
            std::transform(letter.begin(), letter.end(), letter.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            auto icon = CreateLabel("tmIcon" + key, 0, 0, kIconColumn, kIconColumn, letter);
            icon->SetAlignment(TextAlignment::Center);
            icon->SetFontWeight(FontWeight::Bold);
            icon->SetBackgroundColor(Color(210, 220, 235, 255));
            icon->SetBorders(0, Colors::Transparent, 4);
            icon->layoutItem.SetFlexShrink(0.0f);
            row->AddChild(icon);
        }

        auto name = CreateLabel("tmName" + key, (w.title.empty() ? w.appClass : w.title));
        name->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
        name->SetTooltip(w.appClass + (w.processId > 0 ? " (pid " + std::to_string(w.processId) + ")" : ""));
        row->AddChild(name);

        // Always there, empty for a window that is not minimized: the
        // columns stay under the header.
        auto state = CreateLabel("tmState" + key, 0, 0, kStateColumn, 22, w.minimized ? "minimized" : "");
        state->SetTextColor(Color(110, 110, 110, 255));
        state->layoutItem.SetFlexShrink(0.0f);
        row->AddChild(state);

        // The desktop as its number in a bordered box, as the right bar
        // shows the desktops; "all" for a window on every desktop.
        const bool everywhere = w.virtualDesktop < 0;
        auto desktop = CreateLabel("tmDesktop" + key, 0, 0, everywhere ? 34 : 24, 24,
                                   everywhere ? "all" : std::to_string(w.virtualDesktop + 1));
        desktop->SetAlignment(TextAlignment::Center);
        desktop->SetBorders(1, Color(140, 140, 140, 255), 4);
        desktop->SetBackgroundColor(Colors::White);
        desktop->SetTooltip(everywhere ? "On every desktop" : "Desktop " + std::to_string(w.virtualDesktop + 1));
        auto desktopCell = CreateContainer("tmDesktopCell" + key, 0, 0, kDesktopColumn, 26);
        desktopCell->layout.SetFlexRow()
            .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
            .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        desktopCell->layoutItem.SetFlexShrink(0.0f);
        desktopCell->AddChild(desktop);
        row->AddChild(desktopCell);

        const uint64_t id = w.id;
        auto show = CreateButton("tmShow" + key, 0, 0, kButtonWidth, 26, "Show");
        show->SetTooltip("Bring the window to the front (switching to its desktop)");
        show->onClick = [id]() { UltraCanvasDesktopShell::ActivateWindow(id); };
        row->AddChild(show);
        auto close = CreateButton("tmClose" + key, 0, 0, kButtonWidth, 26, "Close");
        close->SetTooltip("Ask the program to close the window - it may ask to save first");
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
