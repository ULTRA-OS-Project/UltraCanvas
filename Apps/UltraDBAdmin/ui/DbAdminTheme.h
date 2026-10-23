// Apps/UltraDBAdmin/ui/DbAdminTheme.h
// The look of UltraDBAdmin in one place: the framework theme it sets at
// start-up (the blue of its app icon for the accent, a grey ground with
// white cards on it), the icon set in media/icons/dbadmin, and the
// factories every page and dialog builds its buttons, cards and bars from,
// so a button looks the same wherever it is.
//
// The theme is set once, before the window is built, and never switched:
// the elements are styled from it as they are made, so a button can carry a
// colour of its own beyond the theme's variants (a destructive one's red).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTheme.h"

#include <memory>
#include <string>

namespace UltraCanvas {
class UltraCanvasListView;
class UltraCanvasTreeView;
}

namespace UltraDBAdmin {

// The theme the app runs in; main() makes it current before the window.
UltraCanvas::UltraCanvasTheme AppTheme();

// An icon of media/icons/dbadmin by its name ("add", "node-table", ...).
std::string IconPath(const std::string& name);

// What a button does, which decides how it looks: Standard for most,
// Primary for the one action a bar exists for (Commit, Apply, Run, a
// dialog's OK), Danger for one that removes something.
enum class ButtonRole { Standard, Primary, Danger };

constexpr float kButtonHeight = 30.0f;

// A bar's button: its icon (an IconPath name, or "" for none) beside its
// label, as wide as both; the tooltip says what it does in full.
std::shared_ptr<UltraCanvas::UltraCanvasButton> MakeActionButton(const std::string& id, const std::string& text,
                                                                 const std::string& icon,
                                                                 const std::string& tooltip,
                                                                 ButtonRole role = ButtonRole::Standard);

// A dialog's button, `width` wide (a minimum: a longer label grows it).
std::shared_ptr<UltraCanvas::UltraCanvasButton> MakeDialogButton(const std::string& id, const std::string& text,
                                                                 float width, ButtonRole role = ButtonRole::Standard);

// The theme's look on a button made elsewhere, keeping its icon.
void StyleButton(UltraCanvas::UltraCanvasButton& button, ButtonRole role);

// A white card on the ground: the panel colour, a hairline border, round corners.
void MakeCard(UltraCanvas::UltraCanvasContainer& container);

// A thin upright line between two groups of a bar's buttons.
std::shared_ptr<UltraCanvas::UltraCanvasContainer> MakeBarDivider(const std::string& id);

// The theme's colours for a message: an error - and a production
// connection, a slip on which is the costly kind - in its danger red, any
// other status in its muted ink.
UltraCanvas::Color ErrorColor();
UltraCanvas::Color StatusColor();

// The lists' and the tree's colours, rows and header from the theme.
void StyleList(UltraCanvas::UltraCanvasListView& view);
void StyleTree(UltraCanvas::UltraCanvasTreeView& tree);

} // namespace UltraDBAdmin
