# STL 3D Model Plugin

Self-contained loader and saver for the **STL (stereolithography)** 3D file
format. No external dependencies — the parser/writer is plain C++.

## Capabilities

| Feature            | Support |
|--------------------|---------|
| Load ASCII STL     | ✅ |
| Load binary STL    | ✅ (auto-detected) |
| Save ASCII STL     | ✅ |
| Save binary STL    | ✅ |
| 3D display         | ✅ OpenGL (`UltraCanvasGLSurface`) when built with `-DULTRACANVAS_ENABLE_GL=ON` |
| Fallback display   | ✅ 2D info placeholder when GL is disabled |
| Mouse orbit / zoom | ✅ (GL build) left-drag to rotate, wheel to zoom |

## Files

- `UltraCanvas3DTypes.h` — dependency-free 3D types (`Vec3`, `Mat4`, `Mesh3D`,
  `BoundingBox3D`). Reusable by future 3D model plugins.
- `UltraCanvasSTLLoader.{h,cpp}` — STL parse/write (ASCII + binary, both ways).
- `UltraCanvasSTLElement.{h,cpp}` — UI element that displays a loaded mesh.
- `UltraCanvasSTLPlugin.{h,cpp}` — `IGraphicsPlugin` implementation + save API.

## Registering with FileLoader

The plugin integrates with the graphics plugin registry that backs
`FileLoader` / `LoadGraphicsFile`. Register it once at startup:

```cpp
#include "Models/STL/UltraCanvasSTLPlugin.h"

UltraCanvas::InitializeGraphicsPluginSystem();
UltraCanvas::RegisterSTLPlugin();
```

After registration, `.stl` files are handled transparently:

```cpp
auto element = UltraCanvas::LoadGraphicsFile("model.stl"); // returns the viewer
bool ok = UltraCanvas::CanHandleGraphicsFile("model.stl"); // true
auto info = UltraCanvas::GetGraphicsFileInfo("model.stl");  // triangle count, bbox...
```

## Loading / saving meshes directly

```cpp
#include "Models/STL/UltraCanvasSTLPlugin.h"
using namespace UltraCanvas;

Mesh3D mesh;
std::string err;
if (UltraCanvasSTLPlugin::LoadModel("in.stl", mesh, &err)) {
    // ... modify mesh ...
    UltraCanvasSTLPlugin::SaveModel("out.stl", mesh, STLFormat::Binary, &err);
}
```

`STLFormat::Auto` (the default for saving) writes **binary** STL, which is
compact and exact. Use `STLFormat::ASCII` for a human-readable file.

## Build

The sources are compiled into the UltraCanvas core library (see the project
`CMakeLists.txt`). The 3D viewer uses OpenGL when `ULTRACANVAS_ENABLE_GL` is on
(the default); otherwise the element renders a 2D placeholder while still
loading/saving mesh data fully.
