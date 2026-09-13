# 3D models → pixels

`UltraCanvasModelRaster.h` turns a mesh into pixels, and a 3D model file into
an editable [`UCRasterLayer`](UltraCanvasPaintSurface.md) — the 3D counterpart
of [vector artwork → pixels](UltraCanvasVectorRaster.md).

A drawing has no pixels but does have a natural size, so rasterizing one only
needs a number. A model has neither: somebody has to choose a **view** as
well. That extra question is `ModelViewPose` (`Plugins/Models/STL/UltraCanvas3DTypes.h`)
— yaw, pitch and camera distance — and it is the same struct
`UltraCanvasSTLElement` orbits with, so the still a caller saves is the view
the user set on screen.

Everything here is software: it transforms, projects, z-buffers and shades the
triangles itself, with no GL dependency and no window. That is what lets the
Filer call it from background decode workers, what makes it the STL element's
fallback in a build without `ULTRACANVAS_ENABLE_GL`, and what makes "save this
view as a bitmap" work identically on every build.

```cpp
#include "UltraCanvasModelRaster.h"

if (IsModelGraphicsPath(path)) {
    const ModelSourceInfo info = InspectModelFile(path);     // triangles, bounds
    ModelRasterOptions options;
    options.width  = 1024;
    options.height = 768;
    options.pose   = viewer->GetModelViewPose();             // what the user framed
    std::string error;
    if (auto layer = RasterizeModelFile(path, options, error)) {
        document->AddLayer(layer);
    } else {
        ShowError(error);
    }
}
```

## Two levels

| Call | Gives | For |
|---|---|---|
| `RenderMeshPreviewPixmap(mesh, w, h, scale)` | `UCPixmap` from the standard three-quarter pose | thumbnails and placeholders: the Filer's 3D tiles, the media viewer's non-GL still |
| `RenderMeshPixmap(mesh, w, h, pose, colour, scale)` | `UCPixmap` from a pose and colour the caller chooses | an element drawing its own orbit without GL |
| `RasterizeMesh(mesh, options, error)` | `UCRasterLayer` | a viewer that already holds the mesh — the model is not parsed twice |
| `RasterizeModelFile(path, options, error)` | `UCRasterLayer` named after the file | the whole path from a file to something editable |

`InspectModelFile()` answers what the file holds — triangle and vertex counts,
the bounding box in the file's own units — without rendering anything.

## Which formats

`IsModelGraphicsPath()` is an **extension** test, and a runtime one: core
reads STL by itself, and OBJ, PLY, 3DS, COLLADA, FBX, X3D/VRML, Alembic,
MilkShape, DirectX `.x`, `.blend` and STEP arrive once the application has
called `RegisterModelFormatsPlugin()`. The seam is
`include/UltraCanvasModelPreview.h`, so core never links the plugin.
`GetModelRasterExtensions()` lists what *this* process can actually read,
which is what a file filter should show.

## The camera

`ModelViewPose` is an orbit around the model's centre, which is the only
camera a viewer of a single object needs:

| Field | Meaning | Default |
|---|---|---|
| `yaw` | radians around the model's up axis | `0.6` |
| `pitch` | radians above the horizon; ±1.5 is straight up / down | `0.4` |
| `distance` | camera distance **in model radii** | `3.0` |

The model is normalised to a unit radius first, so one pose frames any model
whatever units the file is in, and a model saved from a viewer at a given pose
looks the same whether it is a 4 mm screw or a 40 m building. The projection
matches the GL viewer's to the letter — rotate by yaw then pitch, eye at
`(0, 0, distance)`, 45° vertical field of view — which is what makes the
bitmap the view that was on screen.

`ModelViewPose::Default()` is the framing every viewer opens at, and what
"Reset view" goes back to.

## Sizing and the rest of `ModelRasterOptions`

| Field | Default | Notes |
|---|---|---|
| `width` / `height` | 1024 × 768 | a model has no natural size, so these are always the caller's |
| `pose` | `ModelViewPose::Default()` | the view |
| `background` | transparent | painted *under* the model, so one dropped on an image keeps what is beneath it |
| `modelColor` | `kModelDefaultColor` (light blue-grey) | the same colour `UltraCanvasSTLElement` uses, so a still matches the viewer it was framed in |
| `maxPixels` | 256 Mpx | counted on the target size before anything is rendered, so a mistyped size is an error rather than a 40 GB allocation |

Meshes over `kModelPreviewTriangleCap` (2 M triangles) are refused: the cost
that guards is the *load*, not the raster.

## What it is not

A renderer. One head-light, flat two-sided shading, one colour, no material
and no texture — deliberately. Two-sided because most STL in the wild has
inconsistent winding, and a preview that culls backfaces shows such a model
full of holes; flat shading from the triangle geometry rather than stored
normals because STL facet normals are so often wrong or absent that trusting
them lights the model from inside. A build with GL still uses the real viewer
for interaction; this is what that view is *saved* through.

## Consumers

- `UltraCanvasFilerWidget` — 3D thumbnails, on decode workers
- [`UltraCanvasMediaViewer`](UltraCanvasMediaViewer.md) `MediaKind::Model`, and
  `UltraCanvasSTLElement` in a build without GL
- [`UltraCanvasModelViewDialog`](UltraCanvasModelViewDialog.md) — the import
  dialog that lets the user frame the model first
- UltraPaint — File ▸ Import, a dropped model, a model on the command line
  (which is what a drop on the application's icon becomes)

## Tests

`Tests/ModelRasterTest.cpp` (CTest target `ModelRasterTest`). The mesh half:
what it refuses to draw (empty, no extent, over the cap), the size and scale
it honours, the shading (a cube shows at least three face shades, lit from
the pose), determinism, and that winding is not culled. The file half, against
a binary STL the test writes itself: recognising a model by extension,
reporting the geometry a file holds, delivering the requested size exactly,
the pose deciding the picture (a quarter turn narrows a box twice as wide as
it is deep; more distance shrinks it), the background composited under the
model, the colour being the caller's, and absurd or unreadable requests
refused with a reason.

## See also

- [UltraCanvasVectorRaster](UltraCanvasVectorRaster.md) — the same idea for
  drawings, where only a size is missing
- [UltraCanvasModelViewDialog](UltraCanvasModelViewDialog.md) — the UI that
  asks for the view
- [UltraCanvasMediaViewer](UltraCanvasMediaViewer.md) — the viewer that opens
  every model format the build has
- [UltraCanvasPaintSurface](UltraCanvasPaintSurface.md) — `UCRasterLayer` and
  the editing surface
