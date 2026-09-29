// Apps/UltraDesktop/ui/UltraDesktopSettingsWindow.cpp
// The desktop's settings page. See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopSettingsWindow.h"
#include "UltraDesktopWindow.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <algorithm>

using namespace UltraCanvas;

namespace UltraDesktop {
namespace {

constexpr int kWindowWidth = 560;
constexpr int kWindowHeight = 380;
constexpr float kLabelWidth = 150;
constexpr float kRowHeight = 30;

std::shared_ptr<UltraCanvasContainer> MakeRow(const std::string& id, const std::string& caption,
                                              const std::shared_ptr<UltraCanvasUIElement>& field,
                                              const std::shared_ptr<UltraCanvasUIElement>& extra = nullptr) {
    auto row = CreateContainer(id, 0, 0, 0, kRowHeight);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexShrink(0.0f);
    auto label = CreateLabel(id + ".label", caption);
    label->SetElementSize(CSSLayout::Dimension::Px(kLabelWidth), CSSLayout::Dimension::Auto());
    label->layoutItem.SetFlexShrink(0.0f);
    row->AddChild(label);
    field->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    row->AddChild(field);
    if (extra) {
        extra->layoutItem.SetFlexShrink(0.0f);
        row->AddChild(extra);
    }
    return row;
}

} // namespace

UltraDesktopSettingsWindow::UltraDesktopSettingsWindow(UltraDesktopWindow* desktop) : desktop_(desktop) {}
UltraDesktopSettingsWindow::~UltraDesktopSettingsWindow() = default;

void UltraDesktopSettingsWindow::Open() {
    if (!desktop_) return;
    const DesktopSettings& settings = desktop_->Settings();

    WindowConfig config;
    config.title = "ULTRA OS settings - Desktop";
    config.width = kWindowWidth;
    config.height = kWindowHeight;
    config.minWidth = 460;
    config.minHeight = 320;
    window_ = CreateWindow(config);
    if (!window_) return;
    open_ = true;
    window_->onWindowClosed = [this]() { open_ = false; };

    auto page = CreateContainer("dsPage", 0, 0, 0, 0);
    page->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(16);

    auto title = CreateLabel("dsTitle", "Desktop");
    title->SetFontSize(18);
    title->layoutItem.SetFlexShrink(0.0f);
    page->AddChild(title);

    edge_ = CreateDropdown("dsEdge", 0, 0, 200, 28);
    edge_->AddItem("Left", "left");
    edge_->AddItem("Top", "top");
    edge_->AddItem("Bottom", "bottom");
    edge_->SetSelectedIndex(settings.taskbarEdge == TaskbarEdge::Top ? 1
                            : settings.taskbarEdge == TaskbarEdge::Bottom ? 2 : 0, false);
    page->AddChild(MakeRow("dsEdgeRow", "Taskbar", edge_));

    wallpaper_ = CreateTextInput("dsWallpaper", 0, 0, 260, 28);
    wallpaper_->SetPlaceholder("The framework's picture");
    wallpaper_->SetText(settings.wallpaper);
    auto browse = CreateButton("dsBrowse", 0, 0, 90, 28, "Browse…");
    browse->onClick = [this]() { BrowseWallpaper(); };
    page->AddChild(MakeRow("dsWallpaperRow", "Wallpaper", wallpaper_, browse));

    ramDisc_ = CreateTextInput("dsRamDisc", 0, 0, 260, 28);
    ramDisc_->SetText(settings.ramDiscPath);
    page->AddChild(MakeRow("dsRamDiscRow", "RAM disc", ramDisc_));

    filer_ = CreateTextInput("dsFiler", 0, 0, 260, 28);
    filer_->SetText(settings.filerProgram);
    page->AddChild(MakeRow("dsFilerRow", "File manager", filer_));

    desktops_ = CreateDropdown("dsDesktops", 0, 0, 120, 28);
    for (int i = 1; i <= 9; ++i) desktops_->AddItem(std::to_string(i), std::to_string(i));
    desktops_->SetSelectedIndex(std::clamp(settings.virtualDesktops, 1, 9) - 1, false);
    page->AddChild(MakeRow("dsDesktopsRow", "Virtual desktops", desktops_));

    status_ = CreateLabel("dsStatus", "");
    status_->SetTextColor(Color(110, 110, 110, 255));
    status_->layoutItem.SetFlexGrow(1.0f);
    page->AddChild(status_);

    auto buttons = CreateContainer("dsButtons", 0, 0, 0, 34);
    buttons->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center)
                    .SetFlexJustifyContent(CSSLayout::JustifyContent::End);
    buttons->layoutItem.SetFlexShrink(0.0f);
    auto apply = CreateButton("dsApply", 0, 0, 90, 30, "Apply");
    apply->onClick = [this]() { Apply(); };
    auto close = CreateButton("dsClose", 0, 0, 90, 30, "Close");
    close->onClick = [this]() { if (window_) window_->Close(); };
    buttons->AddChild(apply);
    buttons->AddChild(close);
    page->AddChild(buttons);

    window_->AddChild(page);
    {
        // Sized to the window explicitly and on every resize: a percentage
        // would resolve against nothing at the window level.
        auto fit = [page = page.get(), win = window_.get()](int w, int h) {
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
    window_->Show();
}

void UltraDesktopSettingsWindow::Raise() {
    if (window_) window_->RaiseAndFocus();
}

void UltraDesktopSettingsWindow::BrowseWallpaper() {
    std::vector<FileFilter> filters;
    filters.emplace_back("Pictures", std::vector<std::string>{"jpg", "jpeg", "png", "webp", "avif", "heic", "bmp"});
    const std::string chosen = UltraCanvasNativeDialogs::OpenFile("Choose a wallpaper", filters, "", window_.get());
    if (!chosen.empty() && wallpaper_) wallpaper_->SetText(chosen);
}

void UltraDesktopSettingsWindow::Apply() {
    if (!desktop_) return;
    DesktopSettings& settings = desktop_->Settings();
    TaskbarEdge edge = TaskbarEdge::Left;
    if (edge_) {
        const int index = edge_->GetSelectedIndex();
        edge = index == 1 ? TaskbarEdge::Top : index == 2 ? TaskbarEdge::Bottom : TaskbarEdge::Left;
    }
    settings.taskbarEdge = edge;
    if (wallpaper_) settings.wallpaper = wallpaper_->GetText();
    if (ramDisc_ && !ramDisc_->GetText().empty()) settings.ramDiscPath = ramDisc_->GetText();
    if (filer_ && !filer_->GetText().empty()) settings.filerProgram = filer_->GetText();
    if (desktops_) settings.virtualDesktops = std::clamp(desktops_->GetSelectedIndex() + 1, 1, 9);
    desktop_->ApplySettings();
    if (status_) status_->SetText("Applied.");
}

} // namespace UltraDesktop
