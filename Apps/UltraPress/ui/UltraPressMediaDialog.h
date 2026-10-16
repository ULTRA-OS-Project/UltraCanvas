// Apps/UltraPress/ui/UltraPressMediaDialog.h
// The media library as a dialog: every file under <site>/media with a
// thumbnail, its kind, size and uses; Add file... brings one in through the
// file dialog (engine/UltraPressMedia.h does the copying and naming), Remove
// deletes an unused one, and in pick mode Choose hands the selected file's
// media path back - what a block's picture property wants.
// Version: 0.6.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers before the engine's (X11 macro ordering).
#include "UltraCanvasWindow.h"

#include "UltraPressModel.h"

#include <functional>
#include <string>

namespace UltraPress {

// Shows the library modally over `parent`. `site` is read while the dialog
// is open (for the use counts and the refusal to remove a used file) and
// must outlive it. `onPicked` runs with the chosen media path when `pick`
// is set and Choose is pressed; `onChanged` after every add or remove, so
// the host re-renders a page whose picture just arrived. `preselect` is
// the media path to start on, when it is in the library.
void ShowMediaLibrary(UltraCanvas::UltraCanvasWindowBase* parent, const std::string& siteDirectory, const Site& site,
                      bool pick, std::function<void(const std::string& mediaPath)> onPicked,
                      std::function<void()> onChanged, const std::string& preselect = std::string());

} // namespace UltraPress
