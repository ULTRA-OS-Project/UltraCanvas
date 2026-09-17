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
#include <string>

namespace UltraCloud {

// Show the dialog modally over `parent`. `onAdded` receives the stored
// account; nothing is called on cancel.
//
// `providerFilter`, when set, limits the provider list to the ids it accepts.
// A host that offers adding an FTP server and adding a cloud account as two
// separate commands passes one so that each shows only its own providers -
// the two are configured so differently (a host and a password against a
// browser sign-in) that one combined list explains neither. Unset, every
// registered provider is offered, which is what it has always done.
//
// `title`, when not empty, replaces the window title for the same reason: a
// dialog reached through "add an FTP drive" should not call itself "Add cloud
// account".
void ShowAddAccountDialog(UltraCanvas::UltraCanvasWindowBase* parent, CloudService& service,
                          std::function<void(const Account&)> onAdded,
                          std::function<bool(const std::string& providerId)> providerFilter = {},
                          const std::string& title = std::string());

} // namespace UltraCloud
