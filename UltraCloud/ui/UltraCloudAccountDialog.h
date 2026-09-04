// UltraCloud/ui/UltraCloudAccountDialog.h
// The shared "Add cloud account" dialog: provider, name, server URL, user,
// password (app password), public URL (WebDAV), upload folder, default flag.
// Verifies the sign-in through the provider before storing. One dialog for
// every app that links UltraCloudUI.
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers before module headers (X11 macro ordering).
#include "UltraCanvasWindow.h"

#include <UltraCloud/UltraCloudService.h>

#include <functional>

namespace UltraCloud {

// Show the dialog modally over `parent`. `onAdded` receives the stored
// account; nothing is called on cancel.
void ShowAddAccountDialog(UltraCanvas::UltraCanvasWindowBase* parent, CloudService& service,
                          std::function<void(const Account&)> onAdded);

} // namespace UltraCloud
