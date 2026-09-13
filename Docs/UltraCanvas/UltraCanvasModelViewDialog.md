# Importing a 3D model as a bitmap

`UltraCanvasModelViewDialog` (`dialogs/UltraCanvasModelViewDialog.h`) is the
dialog that stands between a 3D model file and a raster layer. It exists
because a model, unlike a drawing, cannot be rasterized from a size alone —
somebody has to say **where the camera stands**.

```cpp
#include "UltraCanvasModelViewDialog.h"

ShowModelViewDialog(
        path,
        { { "merge", "Merge image", false }, { "window", "Open new window", true } },
        parentWindow,
        [this](UltraCanvasModelViewDialog& dialog, const ModelViewResult& result) {
            std::string error;
            auto layer = dialog.Rasterize(error);     // the view that was on screen
            if (layer) Place(layer, result.actionId, result.background);
            else ShowError(error);
        });
```

The callback is handed the dialog because `Rasterize()` lives on it and it is
alive for exactly as long as the callback runs — the dialog closes as soon as
that returns.

## What it shows

The 3D pane is the framework's own [media
viewer](UltraCanvasMediaViewer.md) with its top bars off
(`SetTopBarsVisible(false)`), which is the whole reason the dialog is small:
orbiting, zooming, the GL renderer where the build has one and the software
still where it does not, and **every model format the build can read** all
come from there rather than being reimplemented here. Drag turns the model,
the wheel dollies.

Under it: the bitmap size (two linked spinners — changing one keeps the aspect
ratio), the background (Transparent / White / Black), the model's own triangle
count, and `Reset view`.

## The buttons are the caller's

`ModelViewAction{ id, label, primary }` — the accept buttons, in order. An
editor that asked because a file was dropped offers *Merge image* and *Open
new window*; one that asked from a menu offers *Open*. Cancel is always
present, closes silently and is never reported. `onAccept` receives
`ModelViewResult` carrying the `actionId` that was pressed, the size, the
`ModelViewPose` and the background.

## Getting the pixels

`Rasterize(error)` renders what is on screen into a `UCRasterLayer`, using the
mesh the viewer already holds, so the model is not parsed a second time. Call
it from inside `onAccept` — the dialog is still alive there, and closes as soon
as the callback returns. Everything it does is
[`UltraCanvasModelRaster`](UltraCanvasModelRaster.md); a caller that has its
own view already (a saved pose, a batch job) should call that header directly
and skip the dialog.

## Consumers

UltraPaint: `File ▸ Import`, a model dropped on the canvas, a model on the
command line — which is also what a model dropped on the UltraPaint icon in a
dock or taskbar becomes. See `Apps/UltraPaint/UltraPaintWindow.cpp`
(`ImportModel`).

## See also

- [UltraCanvasModelRaster](UltraCanvasModelRaster.md) — the rendering and the
  pose behind this dialog
- [UltraCanvasMediaViewer](UltraCanvasMediaViewer.md) — the viewer embedded in
  it, and `GetModelViewPose` / `SetModelViewPose` / `GetModelMesh`
- [UltraCanvasVectorRaster](UltraCanvasVectorRaster.md) — the same import path
  for drawings, which need only a size
- [UltraCanvasFormLayout](UltraCanvasFormLayout.md) — the caption grid its
  fields are laid out in
