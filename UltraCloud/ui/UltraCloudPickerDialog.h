// UltraCloud/ui/UltraCloudPickerDialog.h
// The shared "Attach cloud link" picker: choose an account (the default is
// preselected), browse it, upload a local file into the current folder, and
// get a share link for the selected file. Offers the add-account dialog when
// no account exists yet. One dialog for every app that links UltraCloudUI.
// Version: 0.2.0 - a shared_ptr form, whose worker threads hold the service
//                  until they end
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers before module headers (X11 macro ordering).
#include "UltraCanvasWindow.h"

#include <UltraCloud/UltraCloudService.h>

#include <functional>
#include <memory>

namespace UltraCloud {

struct CloudLinkPick {
    Account   account;
    Entry     entry;    // the shared file
    ShareLink link;
};

// Show the picker modally over `parent`. `onPicked` receives the link when
// the user confirms; nothing is called on cancel.
//
// The picker lists folders, uploads and makes the link on threads of its
// own, and its "Add account…" opens the add-account dialog, whose sign-in
// can wait minutes on the browser - any of them can still be running after
// the picker and the window behind it have closed. In this form each of them
// holds `service` until it ends: a service that owns its stores
// (CloudService's shared_ptr constructor) is never used after it is gone.
void ShowCloudLinkPicker(UltraCanvas::UltraCanvasWindowBase* parent,
                         std::shared_ptr<CloudService> service,
                         std::function<void(const CloudLinkPick&)> onPicked);

// The same over a service the caller keeps: it must outlive every upload,
// listing and sign-in the picker starts, which can be after the picker has
// closed. A caller that cannot promise that uses the shared_ptr form.
void ShowCloudLinkPicker(UltraCanvas::UltraCanvasWindowBase* parent, CloudService& service,
                         std::function<void(const CloudLinkPick&)> onPicked);

} // namespace UltraCloud
