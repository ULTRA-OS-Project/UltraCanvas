# UltraCanvas Alert Documentation

## Overview

An **Alert** is a floating, always-on-top, **modal** message box that the user
cannot miss: it blocks interaction with the rest of the application until it is
dismissed. `UltraCanvasAlert` is a thin, purpose-built façade over the
UltraCanvas dialog system (`UltraCanvasModalDialog` / `UltraCanvasDialogManager`)
that gives every alert a clear **severity** — Info, Success, Warning, Error or
Question — with a matching coloured icon, and a one-call API.

Alerts are **non-blocking**: they show immediately and deliver the user's choice
through a callback, so the application main loop keeps running underneath.

**File Location**: `include/UltraCanvasAlert.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

## Why it lives in the dialog system

In UltraCanvas an alert is deliberately *not* an inline banner — it is a modal
dialog window so it can never be scrolled past or overlooked. It reuses the
existing modal-dialog machinery (native OS or internal UltraCanvas dialogs,
centering, focus/mouse capture, escape-to-close) and simply adds a clean,
severity-first API on top.

## Severities

| `AlertSeverity` | `DialogType` | Icon | Accent colour |
|-----------------|--------------|------|---------------|
| `Info`          | Information  | `i`  | Steel blue |
| `Successful`*   | Successful*  | ✓    | Green |
| `Warning`       | Warning      | `!`  | Amber |
| `Error`         | Error        | `X`  | Red |
| `Question`      | Question     | `?`  | Steel blue |

\* Spelled `Successful` (not `Success`) because `Success` is an X11 macro
(`#define Success 0`); the framework follows the same rename convention used for
`DialogButton::NoneButton` and `DialogResult::NoResult`.

The **Successful** severity is new — it was added to `DialogType` (with a green
check icon) so success acknowledgements read differently from plain information.

## Quick Start

```cpp
#include "UltraCanvasAlert.h"
using namespace UltraCanvas;

// One-liners (title defaults to the severity name)
UltraCanvasAlert::Info("Opened in read-only mode.");
UltraCanvasAlert::Successful("Export complete.");
UltraCanvasAlert::Warning("You have unsaved changes.");
UltraCanvasAlert::Error("Could not save the file.");

// Yes/No confirmation -> bool
UltraCanvasAlert::Confirm("Delete this item?", "Confirm Delete",
                          [](bool yes) { if (yes) deleteItem(); });

// Capture which button was pressed
UltraCanvasAlert::Warning("Leave without saving?", "Unsaved changes",
                          [](DialogResult r) { /* r == DialogResult::OK ... */ },
                          parentWindow);
```

## Alerts without the severity icon

The coloured severity badge sits in its own column on the left of the message,
so the message column — and everything the caller adds to it — starts to the
right of it. When the alert's body carries **its own graphic** (a progress ring,
a chart, an image preview) that offset leaves the graphic visibly off centre.
Set `showIcon = false` (or use the `Plain()` one-liner) and the badge is removed
from the layout entirely, so the content column spans the full width of the
dialog and a centred element is centred **in the window**:

```cpp
// One-liner: no badge, OK button.
UltraCanvasAlert::Plain("Unpacking \"photos.zip\"", "Extracting");

// Rich form: no badge, with a graphic of the alert's own centred in it.
AlertOptions opts;
opts.title    = "Extracting";
opts.message  = "Unpacking \"photos.zip\"";
opts.showIcon = false;          // <- content column gets the whole width
UltraCanvasAlert::Show(opts);
```

`showIcon` only hides the badge; the severity still drives the window title and
the accent colour used elsewhere. The same switch exists one level down as
`DialogConfig::showIcon`, and on a live dialog as
`UltraCanvasModalDialog::SetIconVisible(bool)` / `IsIconVisible()`.

This is what [`UltraCanvasProgressDialog`](UltraCanvasProgressDialog.md) uses:
its ring is the dialog's graphic, so it opens without a badge and the ring is
horizontally centred.

## Rich form

```cpp
AlertOptions opts;
opts.severity      = AlertSeverity::Warning;
opts.title         = "Update available";
opts.message       = "A new version is ready to install.";
opts.details       = "Version 4.2.0 - the app will restart to finish.";
opts.buttons       = DialogButtons::OKCancel;
opts.defaultButton = DialogButton::OK;
opts.parent        = parentWindow;
opts.onResult      = [](DialogResult r) { /* ... */ };
UltraCanvasAlert::Show(opts);
```

## API

```cpp
enum class AlertSeverity { Info, Successful, Warning, Error, Question };

struct AlertOptions {
    std::string message;
    std::string title;        // empty => derived from severity
    std::string details;      // optional secondary line
    AlertSeverity severity = AlertSeverity::Info;
    bool showIcon = true;     // false => no severity badge, content full width
    DialogButtons buttons = DialogButtons::OK;
    DialogButton  defaultButton = DialogButton::OK;
    UltraCanvasWindowBase* parent = nullptr;
    std::function<void(DialogResult)> onResult;
};

class UltraCanvasAlert {
    static void Show(const AlertOptions& opts);

    static void Info      (const std::string& message, const std::string& title = "",
                           std::function<void(DialogResult)> onResult = nullptr,
                           UltraCanvasWindowBase* parent = nullptr);
    static void Successful(/* same signature */);
    static void Warning   (/* same signature */);
    static void Error     (/* same signature */);

    // Icon-less message box: no severity badge, so the content column is
    // centred in the full width of the dialog.
    static void Plain(const std::string& message, const std::string& title = "",
                      std::function<void(DialogResult)> onResult = nullptr,
                      UltraCanvasWindowBase* parent = nullptr,
                      AlertSeverity severity = AlertSeverity::Info);

    static void Confirm(const std::string& message, const std::string& title,
                        std::function<void(bool confirmed)> onConfirmed,
                        UltraCanvasWindowBase* parent = nullptr);

    static DialogType ToDialogType(AlertSeverity s);
};
```

## Notes

- **`parent`** centres the alert over that window and is the window whose input
  is blocked. Pass `nullptr` to centre on screen / make it application-modal.
- Alerts honour `UltraCanvasDialogManager::SetUseNativeDialogs(...)`: when native
  dialogs are enabled they render as the OS message box (GTK / Win32 / Cocoa),
  otherwise as the internal UltraCanvas modal dialog. The **Successful** severity
  maps to an informational style on native back-ends (which have no success
  type) and to the green check styling in the internal dialog.
- The default `buttons` for the severity one-liners is `OK`; use `Confirm` or the
  rich `Show` form for Yes/No/Cancel and other combinations.
- Alerts inherit the dialog keyboard model: Return activates the default button,
  Escape cancels, and each button carries an underlined mnemonic letter that
  activates it. See [`UltraCanvasDialogKeyboard.md`](UltraCanvasDialogKeyboard.md).
- **`showIcon = false`** (and `Plain()`) removes the severity badge from the
  layout, not just from view: the message column then spans the whole content
  width, which is what makes a centred element in the alert body — a progress
  ring, a chart — centre in the window. It applies to the internal dialog;
  native OS message boxes draw their own icon.
- For transient, non-blocking status messages that should *not* interrupt the
  user, prefer a Toast (`UltraCanvasToast`) instead — an Alert is intentionally
  interruptive.
