# UltraCanvasModelFormats — reading 3D files

The **Models plugin** reads a 3D file of any supported format into one
structure: `ModelStorage::ModelDocument`. There is no per-format document type
and no per-format API — a caller names a path, and what comes back is a scene
graph with meshes, materials, units and, for the CAD formats, exact solids.

**Headers:** `Models/UltraCanvasModelFormatsPlugin.h` (dispatch and
registration), `DataFormats/UltraCanvasModelStorage.h` (`ModelDocument` and
everything in it), `DataFormats/UltraCanvasModelConverter.h`
(`IModelFormatConverter`, `ConversionOptions`, `FormatCapabilities`),
`Models/UltraCanvasModelMesh3D.h` (`ModelDocument` ⟷ flat `Mesh3D`).

**Demo:** *3D Graphics → 3D Model Formats*
(`Apps/DemoApp/UltraCanvasModelFormatsExamples.cpp`).

STL is documented separately in
[UltraCanvasSTLElement.md](UltraCanvasSTLElement.md): its loader lives in the
core library rather than in this plugin, because the Filer's thumbnails and the
media viewer need one reader that is always present.

## Reading a file

```cpp
#include "Models/UltraCanvasModelFormatsPlugin.h"
#include "Models/UltraCanvasModelMesh3D.h"
using namespace UltraCanvas;

ModelConverter::ConversionOptions options;
options.TriangulateOnImport = true;   // a renderer wants triangles, not n-gons
options.TessellateOnImport  = true;   // …including from a STEP file's exact solids
options.WarningCallback = [](const std::string& message) {
    debugOutput << "model: " << message << std::endl;
};

auto document = UltraCanvasModelFormatsPlugin::LoadModelDocument(path, options);
if (document && !document->Empty()) {
    Mesh3D mesh = ModelDocumentToMesh3D(*document);   // flat, for a viewer
}
```

`LoadModelDocument` dispatches on the file extension. To go through the
framework's plugin registry instead — so a 3D file opens like any other
graphics file — call `RegisterModelFormatsPlugin()` once at start-up and then
use `LoadGraphicsFile`. One call covers the whole `Model3D` category, STL
included.

## What each format carries

Ask the converter rather than assuming: `GetCapabilities()` is computed from
the implementation, and a flag that lies is worse than one that is absent.

```cpp
auto converter = UltraCanvasModelFormatsPlugin::CreateConverterForExtension(".dae");
if (converter) {
    const auto caps = converter->GetCapabilities();   // .SceneGraph, .Materials, .Brep, …
    const bool writable = converter->CanExport();
}
```

Reported by the converters themselves in a build with tinyxml2 and zlib:

| Format | Scene graph | Materials | B-rep | Write |
|---|---|---|---|---|
| Wavefront OBJ | — | ✓ | — | ✓ |
| Stanford PLY | — | — | — | ✓ |
| Autodesk 3D Studio (`.3ds`) | — | ✓ | — | — |
| COLLADA (`.dae`) | ✓ | ✓ | — | — |
| Autodesk FBX | ✓ | ✓ | — | — |
| X3D / VRML (`.x3d .x3dv .wrl .vrml`) | ✓ | ✓ | — | — |
| DirectX (`.x`) | ✓ | ✓ | — | — |
| MilkShape 3D (`.ms3d`) | ✓ | ✓ | — | — |
| Alembic (`.abc`) | ✓ (first time sample) | — | — | — |
| Blender (`.blend`) | ✓ | ✓ | — | — |
| STEP (`.step .stp .p21`) | ✓ | ✓ | ✓ | ✓ |
| AutoCAD DXF — 3D entities | — | ✓ | — | — |

Three formats are writable — OBJ, PLY and STEP — and everything else is
read-only. That is a property of the writers this repository has, not of the
formats.

The table is a summary; the build decides the rest. COLLADA and X3D need
tinyxml2, and the `.blend` and FBX readers need zlib, so each is a separate
build option. `SupportedLoadExtensions()` and `SupportedSaveExtensions()` are
the authority for a given binary, and `CreateConverterForExtension` returns
null for a format this build does not carry.

## Writing

`SaveModelDocument` dispatches the same way `LoadModelDocument` does:

```cpp
UltraCanvasModelFormatsPlugin::SaveModelDocument(*document, "out.step", options);
```

**Writing a mesh to STEP does not produce a CAD model.** STEP describes
surfaces, and a mesh has none of its own, so the writer makes them: one planar
face per facet, edges shared between neighbours. That is what STEP has for a
mesh and what a CAD system will read back, but the result carries the mesh's
accuracy rather than a model's — and it is large, because every triangle
becomes its own face. The writer says so through `WarningCallback` rather than
leaving you to notice. A document that already holds exact bodies is written as
those bodies, losslessly, and does not go through faceting at all.

`.dxf` is deliberately **not** claimed by this plugin: a DXF is a drawing far
more often than a model, so the Vector plugin's reader stays the default for
it. A caller that wants the 3D entities asks for the converter by name —
`CreateConverterForExtension(".dxf")` does answer.

## Two things that surprise people

**A STEP file contains no triangles.** It carries trimmed NURBS and analytic
surfaces with the topology that closes them into solids, in
`ModelDocument::Brep`. A document holding only exact bodies is *not* empty, and
`TotalFaceCount()` is zero until something tessellates it — which is what
`TessellateOnImport` asks for, at the tolerance in
`ConversionOptions::Tessellation`. A converter writing STEP to IGES wants it
off, because tessellating and discarding is exactly the loss the B-rep
structure exists to prevent.

**Readers do not rescale geometry.** A millimetre drawing stays in
millimetres. What the file declared is recorded in `SourceUnit` /
`UnitScaleToMeters`, and the up-axis and handedness likewise in `Up` and
`Chirality`, so a consumer can recover physical size without guessing. In the
demo page the same aircraft arrives in centimetres from FBX, in metres from
COLLADA and unitless from MilkShape — all three correct.

## Displaying a model

`ModelDocumentToMesh3D` flattens the document to one triangle buffer with node
transforms applied. It is the lossy direction on purpose — a viewer wants a
buffer to upload, not a scene — so materials, texture coordinates, skins and
animation do not survive it.

```cpp
auto viewer = std::make_shared<UltraCanvasSTLElement>("Viewer", 10, 10, 600, 430);
viewer->SetMesh(ModelDocumentToMesh3D(*document));
```

The element is named for STL but takes a `Mesh3D` and knows nothing about where
it came from: a mouse-orbited GL view on a build with
`-DULTRACANVAS_ENABLE_GL=ON`, and a shaded software still otherwise.

Code that only needs "turn this path into a mesh" should use the core seam
`UltraCanvasModelPreview.h` (`CanPreviewModelExtension`,
`LoadModelPreviewMesh`) instead of calling this plugin. Core cannot call a
plugin — the plugin links against core — so the seam is how the Filer's
thumbnails and the media viewer reach these readers, and it degrades to
STL-only when the plugin is not built.

## Warnings

Every fallback, approximation and dropped feature is reported through
`ConversionOptions::WarningCallback`. A converter that cannot represent
something must call it rather than fail silently, so a caller that ignores it
gets a plausible-looking document with no indication of what was lost. Real
examples from the demo samples:

```
FBX: more than one texture is connected to 'DiffuseColor' on material 'ship';
     FBX layers them and the document holds one, so the first is kept
3DS: texture name 'E-45_glass_n' is at the format's 12-character limit and is
     probably truncated
```

## See also

- [UltraCanvasSTLElement.md](UltraCanvasSTLElement.md) — the STL reader/writer in core, and the mesh viewer both pages use
- [UltraCanvasFilerWidget.md](UltraCanvasFilerWidget.md) — 3D thumbnails through the preview seam
- [UltraCanvasMediaViewer.md](UltraCanvasMediaViewer.md) — opening a 3D file as media
- `Docs/Research/UltraCanvas3DModelProposal.md` — why the document structure is shaped the way it is
