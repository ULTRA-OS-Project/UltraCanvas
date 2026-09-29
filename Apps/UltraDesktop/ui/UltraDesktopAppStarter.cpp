// Apps/UltraDesktop/ui/UltraDesktopAppStarter.cpp
// The Apps window. See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopAppStarter.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <cctype>

using namespace UltraCanvas;

namespace UltraDesktop {
namespace {

constexpr int kWindowWidth = 760;
constexpr int kWindowHeight = 560;
constexpr float kTile = 112;
constexpr float kTileIcon = 48;
constexpr float kGap = 10;

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool Matches(const UCDesktopEntry& entry, const std::string& filter) {
    if (filter.empty()) return true;
    return Lower(entry.name).find(filter) != std::string::npos ||
           Lower(entry.genericName).find(filter) != std::string::npos ||
           Lower(entry.comment).find(filter) != std::string::npos;
}

} // namespace

UltraDesktopAppStarter::UltraDesktopAppStarter() = default;
UltraDesktopAppStarter::~UltraDesktopAppStarter() = default;

void UltraDesktopAppStarter::Open() {
    WindowConfig config;
    config.title = "Applications";
    config.width = kWindowWidth;
    config.height = kWindowHeight;
    config.minWidth = 420;
    config.minHeight = 300;
    window_ = CreateWindow(config);
    if (!window_) return;
    open_ = true;
    window_->onWindowClosed = [this]() { open_ = false; };

    auto page = CreateContainer("appsPage", 0, 0, 0, 0);
    page->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(10);

    auto head = CreateContainer("appsHead", 0, 0, 0, 0);
    head->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    head->layoutItem.SetFlexShrink(0.0f);
    filterBox_ = CreateTextInput("appsFilter", 0, 0, 260, 28);
    filterBox_->SetPlaceholder("Find an application…");
    filterBox_->onTextChanged = [this](const std::string& text) {
        filter_ = Lower(text);
        RebuildTiles();
    };
    filterBox_->layoutItem.SetFlexGrow(1.0f);
    head->AddChild(filterBox_);
    status_ = CreateLabel("appsStatus", "");
    status_->SetTextColor(Color(110, 110, 110, 255));
    head->AddChild(status_);
    page->AddChild(head);

    tiles_ = CreateScrollableContainer("appsTiles", 0, 0, 0, 0, true, false);
    tiles_->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap).SetFlexGap(kGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Start);
    tiles_->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    tiles_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    page->AddChild(tiles_);

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

    apps_ = UltraCanvasDesktopShell::ListApplications(static_cast<int>(kTileIcon));
    RebuildTiles();
    window_->Show();
    filterBox_->SetFocus(true);
}

void UltraDesktopAppStarter::Raise() {
    if (window_) window_->RaiseAndFocus();
}

void UltraDesktopAppStarter::RebuildTiles() {
    if (!tiles_) return;
    tiles_->ClearChildren();
    int shown = 0;
    for (const UCDesktopEntry& entry : apps_) {
        if (!Matches(entry, filter_)) continue;
        auto tile = std::make_shared<UltraCanvasButton>("app:" + entry.program + ":" + std::to_string(shown),
                                                        0, 0, kTile, kTile, entry.name);
        if (!entry.iconFile.empty()) {
            tile->SetIcon(entry.iconFile);
            tile->SetIconSize(static_cast<int>(kTileIcon), static_cast<int>(kTileIcon));
            tile->SetIconPosition(ButtonIconPosition::Top);
        }
        ButtonStyle style = tile->GetStyle();
        style.normalColor = Colors::Transparent;
        style.borderWidth = 0;
        style.cornerRadius = 10;
        style.fontSize = 12;
        tile->SetStyle(style);
        tile->SetTooltip(entry.comment.empty() ? entry.genericName : entry.comment);
        tile->layoutItem.SetFlexShrink(0.0f);
        const UCDesktopEntry launched = entry;
        tile->onClick = [this, launched]() { Launch(launched); };
        tiles_->AddChild(tile);
        ++shown;
    }
    if (status_) {
        status_->SetText(std::to_string(shown) + " of " + std::to_string(apps_.size()) + " applications");
    }
    tiles_->InvalidateLayout();
    tiles_->RequestRedraw();
}

void UltraDesktopAppStarter::Launch(const UCDesktopEntry& entry) {
    std::string error;
    if (!UltraCanvasDesktopShell::LaunchApplication(entry, {}, &error)) {
        debugOutput << "UltraDesktop: could not start " << entry.name << ": " << error << std::endl;
        if (status_) status_->SetText("Could not start " + entry.name + ": " + error);
        return;
    }
    if (window_) window_->Close();
}

} // namespace UltraDesktop
