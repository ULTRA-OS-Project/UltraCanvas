// Apps/UltraOffice/UltraOfficeHost.cpp
// Helpers every kind of tab shares.
//
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeHost.h"
#include "UltraOfficeToolbar.h"

#include "UltraCanvasConfig.h"
#include "UltraCanvasTheme.h"
#include "UltraCanvasUtils.h"

namespace UltraOffice {

using namespace UltraCanvas;

std::string IconPath(const std::string& relative) {
    return NormalizePath(GetResourcesDir() + "media/icons/" + relative);
}

std::shared_ptr<UltraCanvasToolbar> MakeToolbar(const std::string& id) {
    auto toolbar = std::make_shared<UltraCanvasToolbar>(id, 0, 0, 0, kToolbarHeight);
    // Flat, in the theme's colours; a window too narrow for every tool puts
    // the rest behind a chevron menu.
    toolbar->SetAppearance(ToolbarAppearance::FromTheme(UltraCanvasTheme::Current(), ToolbarAppearance::Flat()));
    toolbar->SetOverflowMode(ToolbarOverflowMode::Menu);
    toolbar->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(kToolbarHeight));
    return toolbar;
}

std::shared_ptr<UltraCanvasButton> AddTool(UltraCanvasToolbar& toolbar, const std::string& id, const std::string& text,
                                           const std::string& icon, const std::string& tip, std::function<void()> action) {
    auto button = toolbar.AddButton(id, text, icon.empty() ? std::string() : IconPath(icon),
                                    [action]() { if (action) action(); });
    button->SetTooltip(tip);
    button->SetAcceptsFocus(false);   // pressing a tool must not take the keyboard from the document
    return button;
}

std::shared_ptr<UltraCanvasButton> AddToggleTool(UltraCanvasToolbar& toolbar, const std::string& id, const std::string& text,
                                                 const std::string& icon, const std::string& tip, std::function<void()> action) {
    auto button = toolbar.AddToggleButton(id, text, icon.empty() ? std::string() : IconPath(icon),
                                          [action](bool) { if (action) action(); });
    button->SetTooltip(tip);
    button->SetAcceptsFocus(false);
    return button;
}

} // namespace UltraOffice
