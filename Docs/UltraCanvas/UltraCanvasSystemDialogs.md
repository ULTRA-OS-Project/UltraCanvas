# ULTRA OS system dialogs

The dialogs every application asks the system for — open a file, save a file,
pick a folder, print, tell the user something, ask for a value — reached
through one API whatever platform the application runs on. The DemoApp shows
them all live under *ULTRA OS modules → System dialogs*
(`Apps/DemoApp/UltraCanvasSystemDialogsExamples.cpp`).

## Two implementations, one call

Every dialog below comes in two forms, and the application picks with one
switch:

| `UltraCanvasDialogManager::SetUseNativeDialogs(...)` | What the user sees |
|---|---|
| `false` | the **ULTRA OS dialogs** — the framework's own, built from UltraCanvas elements (`UltraCanvasFileDialog`, `UltraCanvasModalDialog`, `UltraCanvasInputDialog`). They look and behave the same on every platform, and on ULTRA OS they *are* the system dialogs. |
| `true` | the host platform's dialogs — GTK on Linux, the common dialogs on Windows, `NSOpenPanel` / `NSAlert` on macOS. |

The ULTRA OS file dialog remembers its view, window size, column widths and
last folder between showings and between applications
(`UltraCanvasFileDialogSettings.h`); UOS-Settings edits where the last folder
is kept.

Printing has no framework-drawn dialog yet: `RequestPrintSettings()` always
shows the platform's print dialog, and the job goes through IODeviceManager.

## File dialogs — `UltraCanvasFileLoader`

All four follow the native-dialogs switch and report through a callback
(immediately for a native dialog, which blocks; when the user closes it for
the framework's, which does not).

<!-- doc-check: UltraCanvasWindowBase* window; -->

```cpp
#include "UltraCanvasFileLoader.h"

// `window` is the application's UltraCanvasWindowBase* (window.get() on the
// shared_ptr CreateWindow returned).
FileDialogOptions opts;
opts.SetTitle("Open a picture")
    .SetParentWindow(window)                 // modal to the application window
    .AddFilter("Images", {"png", "jpg", "jpeg", "webp"})
    .AddFilter("All files", "*");

UltraCanvasFileLoader::OpenFileDialog(opts,
    [](DialogResult result, const std::string& path) {
        if (result == DialogResult::OK) { /* path is UTF-8 */ }
    });

UltraCanvasFileLoader::OpenMultipleFilesDialog(opts,
    [](DialogResult result, const std::vector<std::string>& paths) { /* ... */ });

opts.SetDefaultFileName("untitled.png");
UltraCanvasFileLoader::SaveFileDialog(opts,
    [](DialogResult result, const std::string& path) { /* write to path */ });

UltraCanvasFileLoader::SelectFolderDialog(FileDialogOptions().SetTitle("Choose a folder"),
    [](DialogResult result, const std::string& folder) { /* ... */ });
```

`FileDialogOptions::SetFilterToggles(true)` shows the filters of the ULTRA OS
dialog as a row of toggle buttons ("Show: Images | Audio | Video") rather than
a dropdown; a native dialog gets them as a list headed by one that matches
all of them. `SetShowHidden(true)` lists hidden files from the start.
Its modes, the overwrite question in Save and building it directly are in
[UltraCanvasFileDialog.md](UltraCanvasFileDialog.md).

Paths come back as UTF-8. Convert them with `PathFromUtf8` before handing
them to `std::filesystem` (see *Core conventions* in `AGENTS.md`).

## Print dialog — `UltraCanvasNativeDialogs` + IODeviceManager

```cpp
#include "UltraCanvasNativeDialogs.h"
#include "IODeviceManager/UltraCanvasIODevicePrintDialog.h"

// Ask only: what printer, how many copies, which paper, duplex, page range.
NativePrintResult choice = UltraCanvasNativeDialogs::RequestPrintSettings("Report.txt", window);
if (choice) {
    IODeviceResult sent = PrintTextWithSettings(choice, "Report.txt", text);
}

// Ask and print in one call. A cancelled dialog comes back as
// IODeviceResultCode::Cancelled, not as a failure.
IODeviceResult result = PrintTextWithDialog("Report.txt", text, window);

// A formatted document: PDF bytes, plus pages for renderers that cannot lay out a PDF.
PrintDocumentWithDialog("Report.pdf", pdfBytes, "application/pdf", window, pages);
```

`NativePrintResult` (`IOPrintDialogChoice`) carries the printer's queue name,
the `IOPrintOptions` the user chose (copies, collation, paper, orientation,
duplex, colour, quality), the page range, and whether the user picked *Print
to File* instead of a printer.

## Message and input dialogs — `UltraCanvasDialogManager`

```cpp
UltraCanvasDialogManager::ShowInformation("Saved.", "Report", nullptr, window);
UltraCanvasDialogManager::ShowWarning("The disk is nearly full.", "Disk", nullptr, window);
UltraCanvasDialogManager::ShowError("The file could not be written.", "Save", nullptr, window);

UltraCanvasDialogManager::ShowQuestion("Discard the changes?", "Close",
    [](DialogResult r) { if (r == DialogResult::Yes) { /* ... */ } }, window);

UltraCanvasDialogManager::ShowInputDialog("Name of the new folder:", "New folder",
    "Untitled", InputType::Text,
    [](DialogResult r, const std::string& value) { /* ... */ }, window);
```

`InputType::Password` masks the field. Native message dialogs block and call
the callback before returning; the framework's do not block.

## See also

- `UltraCanvasModalDialog.h` — `UltraCanvasDialogManager`, `UltraCanvasFileDialog`, `UltraCanvasInputDialog`
- `UltraCanvasNativeDialogs.h` — the platform dialogs, called directly
- `UltraCanvasFileLoader.h` — file dialogs and typed loaders (`OpenImage`, `OpenAudio`, ...)
- `IODeviceManager/UltraCanvasIODevicePrintDialog.h` — printing what a print dialog chose
- [UltraCanvasDialogKeyboard.md](UltraCanvasDialogKeyboard.md) — keyboard handling inside dialogs
