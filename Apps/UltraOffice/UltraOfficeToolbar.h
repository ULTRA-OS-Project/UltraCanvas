// Apps/UltraOffice/UltraOfficeToolbar.h
// Toolbars built the same way for every kind of tab: the framework's
// UltraCanvasToolbar, icons from media/icons, tools that never take the
// keyboard from the document. Each kind's view group - how the document is
// looked at: fits, zoom, panes - is a toolbar of its own at the right end
// (MakeViewToolbar), which stays whichever command tab is chosen.
//
// Version: 0.4.0 - AddFontList (the framework's font list on a bar, the recent fonts in the settings),
//                  FontListSettings for a font field, ApplyFontChoice (a choice onto rich text)
// Version: 0.3.0 - AddActionGroup (several tools in one slot); an icon-only tool is narrower
// Version: 0.2.0 - MakeViewToolbar; the zoom level box and the percent lists (AddPercentBox)
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFontPicker.h"
#include "UltraCanvasToolbar.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
class UltraCanvasAppSettings;
class UltraCanvasRichTextEdit;
}

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

// ===== FONT LISTS =====
// Every font list of UltraOffice is the framework's UltraCanvasFontPicker:
// every installed family, each in its own font, the document's fonts and
// the recent ones on top. On a bar it is 130 px wide closed and opens 300 px
// wide, and leaves the keyboard with the document. `settings` gives the
// window's settings (null until a tab is attached), whose recent fonts the
// list shows and keeps; `documentFonts` the families the tab's document
// uses, asked as the list opens.
std::shared_ptr<UltraCanvas::UltraCanvasFontPicker> AddFontList(
        UltraCanvas::UltraCanvasToolbar& toolbar, const std::string& id,
        std::function<UltraCanvas::UltraCanvasAppSettings*()> settings,
        std::function<std::vector<std::string>()> documentFonts);
// A font list outside a bar (a dialog's or the Settings window's field)
// keeping the same recent fonts.
void FontListSettings(UltraCanvas::UltraCanvasFontPicker& list,
                      std::function<UltraCanvas::UltraCanvasAppSettings*()> settings);
// A font chosen in a list, onto rich text - the selection, or what is typed
// next: a family's row changes the family alone (bold and italic stay); a
// style sets bold and italic as it is, and a weight or width they cannot
// reach (Light, SemiBold, Condensed) by the face's own family name where it
// has one the text engine gives back ("Ubuntu Thin", what Word writes too),
// with the bold that name needs, else the nearest of bold
// or not.
void ApplyFontChoice(UltraCanvas::UltraCanvasRichTextEdit& edit, const UltraCanvas::FontPickerChoice& choice);
// What ApplyFontChoice gives a run: the family to store and whether it is
// bold and italic (for a model of its own, the slides' runs).
struct FontChoiceRun {
    std::string family;
    bool setsStyle = false;   // false: the family alone changes
    bool bold = false;
    bool italic = false;
};
FontChoiceRun FontChoiceForRun(const UltraCanvas::FontPickerChoice& choice);

// The zoom levels the zoom box offers, in percent.
inline std::vector<int> ZoomLevelChoices() { return {25, 50, 75, 100, 125, 150, 200, 300, 400}; }

} // namespace UltraOffice
