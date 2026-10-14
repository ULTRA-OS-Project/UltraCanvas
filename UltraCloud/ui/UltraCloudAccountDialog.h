// UltraCloud/ui/UltraCloudAccountDialog.h
// The shared "Add cloud account" dialog: provider, name, server URL, user,
// password (app password), public URL (WebDAV), upload folder, default flag.
// Verifies the sign-in through the provider before storing. One dialog for
// every app that links UltraCloudUI.
// Version: 0.3.0 - AddAccountWatch: a host can watch the sign-in it runs, and
//                  call it off; Cancel and closing the dialog call it off
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers before module headers (X11 macro ordering).
#include "UltraCanvasWindow.h"

#include <UltraCloud/UltraCloudService.h>

#include <functional>
#include <string>

namespace UltraCloud {

// A host's view of the sign-in the dialog runs when Add is pressed. Both are
// called on the thread that signs in - not the UI thread - right before and
// right after it, so a host can set that thread's log sinks in onBegin
// (UltraCloud::SetThreadLog, UltraNet_SetThreadFtpLog) and put them back in
// onEnd: UltraFiler records each attempt in its connection log that way.
// `account` is what was entered - an Account holds no password - and, in
// onEnd, what the sign-in filled in; `result` is how it ended. Once per press
// of Add; a second press waits for the first sign-in to end.
//
// Cancel, or closing the dialog, calls a running sign-in off
// (SetThreadSignInCancel): the wait for the browser ends at once, no account
// is stored, and onEnd comes with ResultCode::Cancelled. `cancelled`, when
// set, is asked from the sign-in thread as well and calls it off the same way
// once it says true - a host that is shutting down. onEnd may come after the
// dialog and its window are gone; neither callback may count on them.
struct AddAccountWatch {
    std::function<void(const Account& account)> onBegin;
    std::function<void(const Account& account, const Result& result)> onEnd;
    std::function<bool()> cancelled;
};

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
//
// `watch`, when set, is told when each sign-in starts and how it ended
// (AddAccountWatch).
void ShowAddAccountDialog(UltraCanvas::UltraCanvasWindowBase* parent, CloudService& service,
                          std::function<void(const Account&)> onAdded,
                          std::function<bool(const std::string& providerId)> providerFilter = {},
                          const std::string& title = std::string(),
                          AddAccountWatch watch = {});

} // namespace UltraCloud
