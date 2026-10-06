- **A Save dialog gives the name the chosen file type's extension.** It
  handed back the name exactly as typed, so "photo" with JPEG chosen came
  back without an extension. Applications pick the format from the
  extension, and each one patched the gap with its own default: UltraPaint
  saved that "photo" as a PNG called `photo.png`, whatever type was
  chosen. The patch also came after the dialog had asked about replacing
  "photo", so an existing `photo.png` was overwritten without a question.
  The name now carries the chosen type's extension before the Replace File
  question is asked: added when it has none ("photo" -> "photo.jpg"),
  swapped when it has another offered type's ("photo.png" -> "photo.jpg"),
  kept when it already fits, and left alone under All files. The new
  `ApplySaveExtension` and `FindFilterForName` (`UltraCanvasModalDialog.h`)
  hold the rule for every dialog:
  - **The framework dialog** applies it on OK and shows the result in the
    name field, rewrites the name when the type is switched, and opens on
    the type of `defaultFileName` when the caller's type does not fit it,
    so "holiday.jpg" offered under PNG is not saved as `holiday.png`.
  - **The GTK chooser** applies it to the name it returns. It already swapped
    the extension when the type changed, but it swapped any trailing
    extension, so "Report v1.2" became "Report v1.png"; it now uses the same
    rule. It opens on the suggested name's type, and when the corrected name
    is an existing file it asks before replacing it - **No** leaves the
    chooser open on that name.
  - **The Windows dialog** is given the chosen type's extension as its
    default (`SetDefaultExtension`), so it adds and follows the extension
    itself and asks about the final name; it also opens on the suggested
    name's type. A name that still does not fit, such as one ending in
    another type's extension, is corrected afterwards, with the question
    asked there if the corrected name exists (**No** cancels).
  - The macOS panel already insisted on an offered extension and is
    unchanged.
- `FileDialogTest` (was `FileDialogLayoutTest`) checks the rule on its own,
  which runs everywhere, and the framework dialog's Save under a display.
