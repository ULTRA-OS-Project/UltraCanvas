// Apps/UltraOffice/UltraOfficeToolbar.h
// Toolbars built the same way for every kind of tab: the framework's
// UltraCanvasToolbar, icons from media/icons, tools that never take the
// keyboard from the document. Each kind's view group - how the document is
// looked at: fits, zoom, panes - is a toolbar of its own at the right end
// (MakeViewToolbar), which stays whichever command tab is chosen.
//
// Version: 0.3.0 - AddActionGroup (several tools in one slot); an icon-only tool is narrower
// Version: 0.2.0 - MakeViewToolbar; the zoom level box and the percent lists (AddPercentBox)
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasToolbar.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraOffice {

constexpr int kToolbarHeight = 40;

std::shared_ptr<UltraCanvas::UltraCanvasToolbar> MakeToolbar(const std::string& id);

// A tool: `icon` is a path under media/icons ("menu/cut.svg"); `text` may be
// empty when the icon says it.
std::shared_ptr<UltraCanvas::UltraCanvasButton> AddTool(UltraCanvas::UltraCanvasToolbar& toolbar, const std::string& id,
                                                        const std::string& text, const std::string& icon,
                                                        const std::string& tip, std::function<void()> action);
// A tool with an on/off state the host keeps in step (SetPressed).
std::shared_ptr<UltraCanvas::UltraCanvasButton> AddToggleTool(UltraCanvas::UltraCanvasToolbar& toolbar, const std::string& id,
                                                              const std::string& text, const std::string& icon,
                                                              const std::string& tip, std::function<void()> action);

// Several tools in one slot of the bar (UltraCanvasToolGroupButton): the slot
// does the tool picked last, and resting the pointer on it folds out the
// others - alignment, lists, a shape's alignment - so a bar keeps its tools
// at a window's usual width.
struct GroupTool {
    std::string id;
    std::string icon;   // under media/icons
    std::string tip;
    std::function<void()> action;
};
std::shared_ptr<UltraCanvas::UltraCanvasToolGroupButton> AddActionGroup(UltraCanvas::UltraCanvasToolbar& toolbar,
                                                                         const std::string& id,
                                                                         const std::vector<GroupTool>& tools);

// The view group's bar: as wide as its tools, never shrunk and never cut
// behind a chevron - the command tab's bar beside it gives way instead.
std::shared_ptr<UltraCanvas::UltraCanvasToolbar> MakeViewToolbar(const std::string& id);

// A list of percentages on a toolbar - the zoom level, the width share -
// with "Other..." at its end, which asks for a whole number between
// `minimum` and `maximum` (a value typed in). `onPick` gets the percent
// chosen. The box reads the value ShowPercent last gave it.
std::shared_ptr<UltraCanvas::UltraCanvasDropdown> AddPercentBox(UltraCanvas::UltraCanvasToolbar& toolbar, const std::string& id,
                                                                 const std::vector<int>& choices, int minimum, int maximum,
                                                                 const std::string& tip, const std::string& askTitle,
                                                                 UltraCanvas::UltraCanvasWindowBase* window,
                                                                 std::function<void(int percent)> onPick);
// Shows `percent` in a box AddPercentBox made: its item when it has one,
// else the number alone.
void ShowPercent(UltraCanvas::UltraCanvasDropdown& box, const std::vector<int>& choices, int percent);
// "60 %", as the boxes write a percentage.
std::string PercentText(int percent);

// The zoom levels the zoom box offers, in percent.
inline std::vector<int> ZoomLevelChoices() { return {25, 50, 75, 100, 125, 150, 200, 300, 400}; }

} // namespace UltraOffice
