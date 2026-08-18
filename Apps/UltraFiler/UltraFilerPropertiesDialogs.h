// Apps/UltraFiler/UltraFilerPropertiesDialogs.h
// The two small windows behind the filer context menu's Extras entries:
// "Attributes" shows the details of the selected entries (name, type, size,
// dates, attribute flags; a summary plus per-item list for a multi
// selection), "Access" edits their permissions (the POSIX rwx matrix, or the
// read-only attribute on Windows) and applies the change to every selected
// entry.
// Version: 1.0.0
// Last Modified: 2026-08-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasFilerWidget.h"   // FilerEntry

#include <functional>
#include <vector>

namespace UltraCanvas {

class UltraCanvasWindowBase;

class UltraFilerPropertiesDialogs {
public:
    // Opens the Attributes window for `entries` (closing a previously open
    // one - its content belongs to an older selection).
    static void ShowAttributes(UltraCanvasWindowBase* parent,
                               const std::vector<FilerEntry>& entries);

    // Opens the Access (permissions) window for `entries`. Apply changes the
    // permissions of every entry and calls `onApplied` so the host can
    // refresh its listing.
    static void ShowAccess(UltraCanvasWindowBase* parent,
                           const std::vector<FilerEntry>& entries,
                           std::function<void()> onApplied);
};

} // namespace UltraCanvas
