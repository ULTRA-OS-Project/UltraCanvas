# UltraCanvasMediaViewerWindow

One file, full size, in its own window.

```cpp
#include "UltraCanvasMediaViewerWindow.h"

UltraCanvasMediaViewerWindow viewerWindow;   // a member, not a local
viewerWindow.Show("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                  hostWindow.get());
```

## What it is for

The companion to a preview pane. A pane sized to the side of a file manager
answers *is this the file I meant*; a window answers *let me actually look at
it*. For a font that is the whole difference — the pane shows a two-letter
specimen, and what you double-clicked for is every glyph the file contains. For
a spreadsheet or a document it is the difference between a thumbnail and the
thing itself.

It hosts an `UltraCanvasMediaViewer`, so it needs no per-format case of its own:
images, video, audio, documents, spreadsheets, e-books, 3D models and fonts all
open the same way, and Prev / Next walk the rest of the folder.

`UltraCanvasImageViewer` is still the right choice for a picture that wants zoom
and pan over a dark lightbox. This is the general one.

## Using it

Hold the object, not a local — it owns the window, and a viewer window that goes
out of scope closes immediately.

```cpp
class MyWindow {
    ...
    UltraCanvasMediaViewerWindow mediaWindow;
};

void MyWindow::OnFileActivated(const std::string& path) {
    MediaViewerWindowOptions options;
    options.title = std::filesystem::path(path).filename().string();
    mediaWindow.Show(path, window.get(), options);
}
```

| Call | What it does |
| --- | --- |
| `Show(path, host, options)` | Opens `path`. Returns false only when the window could not be created. |
| `Close()` | Closes the window and releases the file. |
| `IsOpen()` | Whether a window is up. |
| `GetViewer()` | The `UltraCanvasMediaViewer` inside, or null. |

`MediaViewerWindowOptions`:

| Field | Default | Meaning |
| --- | --- | --- |
| `title` | empty | Window title; empty means the file's own name. |
| `browseFolder` | `true` | Open the rest of the folder as a playlist, so the next file is one key away. `false` shows the one file alone. |
| `width` / `height` | 1040 × 720 | Size used when there is no `host`. |
| `background` | dark | The window's background. |

**The host window matters.** Given one, the viewer opens *over* it — same
position, same size, owned by it — so it appears where the user is looking
rather than at the screen origin. Without one it is centred at the default size.

**One window per instance.** `Show()` on an instance that already has a window
replaces what is in it. That is deliberate: a user double-clicking through a
folder gets one window that keeps up, not a window per file. An application that
wants several open at once holds several instances.

**Escape closes it**, as it does in every other viewer window in the framework.

## A file the viewer cannot show

`Show()` opens the window anyway and lets the viewer say so inside it, because
a double-click that appears to do nothing is worse than a window saying the
format is not supported. A caller that would rather decide first asks
`UltraCanvasMediaViewer::IsSupportedMedia(path)` and does something else with
the ones that come back false.

## Closing the file

`Close()` calls `CloseFile()` on the viewer before dropping the window, so no
document engine is left holding the file — on Windows an open handle blocks a
rename, which is exactly the trap a file manager falls into. A host that shows
the same file in a preview pane should close *that* before opening the window,
for the same reason.

## See also

- [UltraCanvasMediaViewer](UltraCanvasMediaViewer.md) — the viewer inside it
- [UltraCanvasImageViewer](UltraCanvasImageViewer.md) — the zoom/pan lightbox
- [UltraCanvasFontViewer](UltraCanvasFontViewer.md) — what a font opens as
