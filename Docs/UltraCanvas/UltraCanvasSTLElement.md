# UltraCanvasSTLElement — STL 3D models

`UltraCanvasSTLElement` displays a triangle mesh loaded from an **STL**
(stereolithography) file — the interchange format of 3D printing and CAM. The
reader and writer behind it, `UltraCanvasSTLLoader`, are self-contained plain
C++ with no external dependencies, and handle both STL encodings in both
directions.

**Headers:** `Models/STL/UltraCanvasSTLElement.h` (the element),
`Models/STL/UltraCanvasSTLLoader.h` (parse/write),
`Models/STL/UltraCanvas3DTypes.h` (`Vec3`, `Mat4`, `Mesh3D`, `BoundingBox3D`),
`Models/STL/UltraCanvasSTLPlugin.h` (`IGraphicsPlugin` registration + save API).

**Demo:** *Vector Graphics → STL 3D Models*
(`Apps/DemoApp/UltraCanvasSTLExamples.cpp`), which reads every `.stl` file in
`media/vector/STL`.

## What is supported

| Feature | Support |
|---|---|
| Read ASCII STL | ✓ |
| Read binary STL | ✓ — auto-detected from the `84 + 50 × facets` size identity |
| Write ASCII STL | ✓ (`STLFormat::ASCII`) |
| Write binary STL | ✓ (`STLFormat::Binary`, and what `STLFormat::Auto` writes) |
| Per-vertex normals | ✓ — recomputed from the geometry when a file ships degenerate `(0,0,0)` facet normals |
| Bounding box, triangle and vertex counts | ✓ — on `Mesh3D` |
| Shaded 3D display, mouse orbit, wheel dolly | ✓ on GL builds (`-DULTRACANVAS_ENABLE_GL=ON`) |
| Fallback display | ✓ — a 2D mesh summary, so the element builds and loads data everywhere |
| Colour / material | Single model colour (`SetModelColor`); STL itself carries no colour |

STL stores unstructured triangles with no units, colours, layers or scene
graph, so a `Mesh3D` is all there is to read: positions, normals, indices and
the bounds computed from them.

## Displaying a model

```cpp
#include "Models/STL/UltraCanvasSTLElement.h"
using namespace UltraCanvas;

auto viewer = std::make_shared<UltraCanvasSTLElement>("Viewer", 10, 10, 600, 430);
viewer->onLoadError = [](const std::string& error) {
    debugOutput << "STL: " << error << std::endl;
};
viewer->LoadFromFile("model.stl");
viewer->SetModelColor(Vec3(0.85f, 0.68f, 0.32f));   // brass
viewer->SetAutoRotate(true);
container->AddChild(viewer);
```

On a GL build the element is an `UltraCanvasGLSurface`: left-drag orbits the
camera, the wheel dollies it (eased, so a wheel spin reads as one continuous
move), and the camera frames the model from its bounds on load. Without GL the
element draws the mesh name and triangle count instead — the mesh is still
parsed and available through `GetMesh()`.

`SetMesh` takes a mesh you already have, which avoids parsing the file twice
when you also want its statistics:

```cpp
Mesh3D mesh;
std::string error;
if (UltraCanvasSTLLoader::Load("model.stl", mesh, &error)) {
    viewer->SetMesh(mesh);
    debugOutput << mesh.TriangleCount() << " triangles, "
                << mesh.VertexCount() << " vertices" << std::endl;
}
```

## Reading and writing meshes

```cpp
#include "Models/STL/UltraCanvasSTLLoader.h"
using namespace UltraCanvas;

Mesh3D mesh;
std::string error;

// Load: ASCII vs binary is detected, the path is not trusted to say which.
if (!UltraCanvasSTLLoader::Load("in.stl", mesh, &error)) return;

// Inspect.
const Vec3 extent = Vec3(mesh.bounds.max.x - mesh.bounds.min.x,
                         mesh.bounds.max.y - mesh.bounds.min.y,
                         mesh.bounds.max.z - mesh.bounds.min.z);

// Save. STLFormat::Auto writes binary — compact and lossless.
UltraCanvasSTLLoader::Save("out.stl", mesh, STLFormat::Binary, &error);
UltraCanvasSTLLoader::Save("out-text.stl", mesh, STLFormat::ASCII, &error);
```

`LoadFromMemory` takes an already-read byte buffer, and `LooksLikeBinary`
answers the encoding question on its own for callers that need it before
parsing. `HasSTLExtension` is the lightweight `.stl` check used when scanning a
directory.

## Through FileLoader

`UltraCanvasSTLPlugin` registers STL with the graphics plugin registry, so
`.stl` files open like any other graphics file:

```cpp
#include "Models/STL/UltraCanvasSTLPlugin.h"
using namespace UltraCanvas;

InitializeGraphicsPluginSystem();
RegisterSTLPlugin();

auto element = LoadGraphicsFile("model.stl");   // an UltraCanvasSTLElement
bool handled  = CanHandleGraphicsFile("model.stl");
auto info     = GetGraphicsFileInfo("model.stl");
```

The plugin also exposes `UltraCanvasSTLPlugin::LoadModel` /
`SaveModel` for mesh round-trips without touching the loader directly.

## Build

The loader, element and plugin are part of the core library — nothing extra to
enable. The 3D view needs GL:

```bash
cmake -B build -DULTRACANVAS_ENABLE_GL=ON
```

Without it, `UltraCanvasSTLElement.cpp` compiles to the 2D fallback
automatically, so code written against the element keeps building on every
platform.

## See also

- `UltraCanvas/Plugins/Models/STL/README.md` — plugin-level notes
- [UltraCanvasGLSurfaceExamples](UltraCanvasGLSurfaceExamples.md) — the GL surface the viewer builds on
- [UltraCanvasVectorConverters](UltraCanvasVectorConverters.md) — the 2D vector format matrix (SVG, DXF, DWG, …)
