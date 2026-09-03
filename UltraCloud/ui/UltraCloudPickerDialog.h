// UltraCloud/ui/UltraCloudPickerDialog.h
// The shared "Attach cloud link" picker: choose an account (the default is
// preselected), browse it, upload a local file into the current folder, and
// get a share link for the selected file. Offers the add-account dialog when
// no account exists yet. One dialog for every app that links UltraCloudUI.
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers before module headers (X11 macro ordering).
#include "UltraCanvasWindow.h"

#include <UltraCloud/UltraCloudService.h>

#include <functional>

namespace UltraCloud {

struct CloudLinkPick {
    Account   account;
    Entry     entry;    // the shared file
    ShareLink link;
};

// Show the picker modally over `parent`. `onPicked` receives the link when
// the user confirms; nothing is called on cancel.
void ShowCloudLinkPicker(UltraCanvas::UltraCanvasWindowBase* parent, CloudService& service,
                         std::function<void(const CloudLinkPick&)> onPicked);

} // namespace UltraCloud
