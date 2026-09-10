# UltraCanvas 3D Model Structure — Survey and Proposal

**Status:** structure defined and merged; 3DS reads into it; more converters to follow
**Scope:** `ModelStorage::ModelDocument`
(`UltraCanvas/include/DataFormats/UltraCanvasModelStorage.h`) and the
converters that will read into and write out of it.

UltraCanvas has one 3D file format (STL) and no common 3D structure. What it
has instead is `Mesh3D` — a flat position/normal/index triple living inside
the STL plugin — plus a second private `Mesh` and OBJ reader in the demo app,
and three separate camera structs in the 3D charts.
[VersioningInvestigation](../UltraCanvas/VersioningInvestigation.md) §6 records
this as one of the three gates on a 1.0 release: *"the 3D types promoted out of
`Plugins/Models/STL/` into core"*.

This document is the survey behind the structure that answers it: what each 3D
file format actually contains, which of those elements the structure must
carry, what it deliberately does not carry, and in what order the converters
should land.

The 2D side solved the same problem with `VectorStorage::VectorDocument`, and
[UltraCanvasVectorModelProposal](UltraCanvasVectorModelProposal.md) is its
equivalent survey. Its central lesson shapes this one: *a field that exists but
is never honoured is worse than one that does not exist*. Every field in the
new structure is annotated in the header with the formats that fill it, and
this document is where those annotations come from.

## 1. What exists today

| | Where | What it is |
|---|---|---|
| `Mesh3D`, `Vec3`, `Mat4`, `BoundingBox3D` | `Plugins/Models/STL/UltraCanvas3DTypes.h` | Flat float mesh: positions, normals, indices, bounds. No scene, no material, no transform. |
| `UltraCanvasSTLLoader` | `Plugins/Models/STL/` | ASCII + binary STL, read and write. The only 3D format in the framework. |
| `UltraCanvasSTLElement` | `Plugins/Models/STL/` | GL viewer with orbit; 2D summary fallback. |
| `UltraCanvasSTLPlugin` | `Plugins/Models/STL/` | `IGraphicsPlugin`; the `Model3D` category's only member. |
| `Mesh`, `LoadOBJ` | `Apps/DemoApp/UltraCanvasGLDemoSupport.h` | A second, private mesh type and OBJ reader for the GL showcase. Not registered anywhere. |
| Extension → `GraphicsFormatType::ThreeD` | `UltraCanvasGraphicsPluginSystem.h:108-111` | `3dm/3ds/pov/stl/obj/fbx/dae/gltf` classified as 3D — with no plugin behind any but STL. |
| `FilerFileCategory::Model3D` | `UltraCanvasFilerWidget.cpp:272-280` | obj/ply/3ds/3mf/gltf/glb/dae/fbx get a Model badge. Labels only. |
| `ThreeDSConverter` | `Plugins/Models/3DS/` | Reads Autodesk 3DS into `ModelDocument`: meshes, object matrices, materials with texture maps, per-face material groups, cameras and lights. The first scene format on the structure. |
| 3D CAD entities | `Plugins/Vector/UltraCanvasDXFReader.cpp` | DXF/DWG `3DFACE`, polyface and polygon meshes are **read and then flattened to 2D**; `3DSOLID`, `REGION`, `BODY`, `SURFACE` are counted and skipped. |

Two observations drive the design:

- **The classification and badge tables already promise formats nothing can
  open.** A user sees a Model badge on a `.gltf` and gets nothing. The
  structure is what closes that gap.
- **The DXF/DWG readers already parse real 3D geometry and throw the Z away**
  because `VectorDocument` is 2D. Once a 3D document exists, that geometry has
  somewhere to go — the highest-value converter in the list below is one the
  repository has already written most of.

## 2. Format survey — what each format contains

Compiled from the format specifications and from the elements real files use.
This is the list the structure has to be able to hold.

### 2.1 Mesh-only formats

| Format | Geometry | Topology | Attributes | Materials | Units | Notes |
|---|---|---|---|---|---|---|
| **STL** | triangle soup, unindexed | triangles only | per-facet normal | none | none | ASCII + binary. Colour only via non-standard attribute-byte extensions (VisCAM, Materialise). One solid per file. |
| **OFF** | vertices + faces | n-gons | optional per-face colour | none | none | The simplest indexed format. |
| **OBJ** (+MTL) | indexed vertices | n-gons, groups (`g`), objects (`o`) | `vt` texture coords, `vn` normals, smoothing groups (`s`) | MTL: `Ka/Kd/Ks/Ns/d/Ni/illum` + `map_*` | none (convention only) | No transforms, no scene graph, no animation. Material switches mid-face-list (`usemtl`). |
| **PLY** | indexed vertices | n-gons | **arbitrary declared properties** — `nx/ny/nz`, `s/t`, `red/green/blue/alpha`, `quality`, `confidence`, anything | none | none | ASCII + binary LE/BE. The scanned-mesh and point-cloud workhorse. Its property lists are why the structure needs open-ended attributes. |

### 2.2 Scene formats

| Format | Scene graph | Geometry | Materials | Animation | Units / axis |
|---|---|---|---|---|---|
| **glTF 2.0 / GLB** | scenes → nodes (TRS or matrix) → meshes → primitives | indexed, modes points/lines/strips/fans/triangles; `POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD_n`, `COLOR_n`, `JOINTS_n`, `WEIGHTS_n`; morph targets | PBR metallic-roughness, normal/occlusion/emissive maps, alpha modes, `KHR_materials_*` | skins with inverse bind matrices; channels on TRS + morph weights; linear/step/cubic | metres, Y-up, right-handed |
| **COLLADA** (.dae) | `library_visual_scenes`, nested nodes | polylist/triangles/polygons, arbitrary named inputs | `<phong>`, `<lambert>`, `<blinn>`, effects and images | skin + morph controllers, animation library | `<unit meter=>`, `<up_axis>` |
| **FBX** | full node hierarchy, properties | meshes, per-polygon material mapping, layered elements | Lambert/Phong + textures | skeletons, animation stacks and layers | unit scale factor property |
| **3DS** | node keyframe hierarchy | meshes, 65 536-vertex limit | fixed-function, 8.3 names | keyframe tracks | none |
| **X3D / VRML** | grouping + `Transform` nodes | `IndexedFaceSet` | `Appearance`/`Material` | routes and interpolators | metres |
| **USD / USDZ** | prim hierarchy with composition | `UsdGeomMesh`, subdivision | `UsdPreviewSurface` | skeletons, time samples | metres per unit, up axis |

### 2.3 Manufacturing formats

| Format | Content | Why it matters here |
|---|---|---|
| **3MF** | ZIP + XML: mesh objects, **components** (instancing with transforms), base materials, colour groups, texture groups, build items, metadata; production/beam-lattice/slice extensions | The modern replacement for STL. Declares units and is Z-up. |
| **AMF** | XML: objects, volumes, materials with colour, **constellations** (instancing), metadata, curved triangles | Declares units. |

### 2.4 Point clouds

| Format | Content |
|---|---|
| **PLY**, **PCD** | points with position, colour, normal, and arbitrary named fields |
| **LAS / LAZ** | LiDAR: position (scaled integers, georeferenced), intensity, return number, classification, GPS time |
| **XYZ** | plain ASCII triples, sometimes with colour |

Point clouds are not a separate structure: they are a primitive whose mode is
`Points` and whose extra channels are named attributes.

### 2.5 CAD / B-rep — the deliberate exclusion

**STEP** (ISO 10303 AP203/214/242), **IGES**, **ACIS/SAT**, **Parasolid**,
**3DM/OpenNURBS**, and the **`3DSOLID`/`REGION`/`BODY`/`SURFACE`** entities in
DWG and DXF do not contain meshes. They contain *boundary representations*:
trimmed NURBS surfaces, topological faces, edges, loops and vertices, solid
bodies, assemblies and PMI annotation. Holding those losslessly requires a
topology model that has nothing in common with the one below — a different
structure, not more fields on this one.

**The decision: B-rep is out of scope for `ModelDocument`.** A B-rep reader
tessellates to triangles on import (which is what every viewer does anyway) and
records the loss through the warning callback. If UltraCanvas later needs
parametric solids, they get their own `BrepStorage` document alongside this
one, exactly as 2D vector and 3D mesh are separate documents today.

## 3. What the structure must therefore carry

Reading down the survey, the union of elements is smaller than it looks,
because the formats agree more than they differ:

1. **Indexed vertex data with open-ended attributes.** Positions, normals and
   tangents are named fields because every consumer needs them by name.
   Everything else — texture coordinate sets, colour sets, skin joints and
   weights, PLY's arbitrary properties, LAS intensity and classification — is a
   named `VertexAttribute`. Without this, PLY and the point-cloud formats lose
   data on every import.
2. **Every topology real files use**, including **n-gons**. OBJ, PLY, OFF and
   3MF emit quads and larger faces; triangulating on import makes an
   OBJ → OBJ round trip lossy for no reason. `FaceStarts` keeps the faces the
   file had and `Triangulate()` produces the triangle form when a consumer
   needs it.
3. **A scene graph with instancing.** glTF, COLLADA, FBX, 3MF components and
   AMF constellations all draw one mesh from many places with different
   transforms. A structure that cannot express this has to duplicate geometry
   on import — for a mechanical assembly, a large multiplier.
4. **Both material models.** PBR metallic-roughness is the interchange
   standard, but OBJ/MTL, 3DS, COLLADA and FBX state materials in
   fixed-function Phong terms. Converting Phong → PBR → Phong does not return
   the original, so the structure keeps whichever the file had and derives the
   other on demand (`ModelMaterial::DeriveMissingModel`).
5. **Skinning, morph targets and animation.** Present in glTF, COLLADA, FBX and
   USD; absent from every mesh-only format. Kept because half the scene formats
   have them and a structure that drops them cannot claim to be an interchange
   format.
6. **Units, up axis and handedness.** The three facts that decide whether an
   imported model is the right size and the right way up. 3MF, AMF, COLLADA,
   FBX and USD state them; STL, OBJ and PLY do not; glTF fixes them by
   specification. **Readers record what the file said and do not rescale** —
   the same contract `VectorDocument` uses for `SourceUnit` /
   `PointsPerSourceUnit`.
7. **Metadata that survives.** 3MF and AMF metadata, glTF `asset` and `extras`,
   FBX custom properties. A `map<string, string>` on the document, on nodes and
   on materials, so a converter never has to choose between dropping something
   and inventing a field for it.

## 4. The structure

`UltraCanvas/include/DataFormats/UltraCanvasModelStorage.h`, namespace
`UltraCanvas::ModelStorage`. It is a **core service**, not a plugin's private
type: `Masterfile_modules.md` requires framework-wide facilities to live in
`UltraCanvas/{include,core}` and *"never inside a file-type plugin"*, which is
precisely what disqualified `Plugins/Models/STL/UltraCanvas3DTypes.h`.

```
ModelDocument
├─ Title, Author, Generator, Copyright, SourceFormat, Metadata{}
├─ SourceUnit, UnitScaleToMeters, Up, Chirality
├─ Scenes[]      ModelScene   { Name, Roots[] }              + DefaultScene
├─ Nodes[]       ModelNode    { TRS or Matrix, Children[], Parent,
│                               Mesh, Camera, Light, Skin, MorphWeights[], Extras{} }
├─ Meshes[]      ModelMesh    { Name, Primitives[] }
│                MeshPrimitive{ Mode, Positions[], Normals[], Tangents[],
│                               Attributes[], Indices[], FaceStarts[],
│                               Material, Targets[] }
├─ Materials[]   ModelMaterial{ PBR block, optional Phong block, textures,
│                               AlphaMode, DoubleSided, IOR, Extras{} }
├─ Images[]      ModelImage   { Uri or embedded Data, MimeType }
├─ Samplers[]    ModelSampler { filters, wrap modes }
├─ Skins[]       ModelSkin    { Joints[], InverseBindMatrices[] }
├─ Animations[]  ModelAnimation { Channels[], Samplers[] }
├─ Cameras[]     ModelCamera
└─ Lights[]      ModelLight
```

### 4.1 Three decisions worth defending

**Flat arrays addressed by index, not a `shared_ptr` tree.** This diverges from
`VectorStorage`, deliberately. 3D formats are natively reference-based — glTF,
COLLADA and FBX all address geometry and materials by id — so indices are what
a reader already holds and what a writer must emit; converting to pointers on
import and back on export buys nothing. Instancing (one mesh, many nodes) is
the normal case rather than the exception, and index storage expresses it
without aliasing questions. The document also stays trivially copyable and
serialisable, which a graph of `shared_ptr` with parent back-references is not.

**Positions are `double`; everything else is `float`.** CAD, survey and
geospatial sources place geometry far from the origin, where float has already
lost millimetres — a coordinate of 10⁶ resolves to about 0.06 units. The 2D
model reached the same conclusion and moved to double. Normals, tangents,
texture coordinates, colours and weights stay float because they are bounded
and small. The cost is 12 bytes per vertex, paid only in the exchange
structure: the GL element narrows to float when it uploads.

**Materials keep both models rather than normalising to one.** See §3.4.

### 4.2 Operations the structure provides

`Triangulate()` / `TriangulateAll()`, `RecomputeNormals()` (area-weighted),
`WeldVertices(tolerance)`, `FlattenTransforms()`, `ConvertUpAxis()`,
`GlobalTransform(node)`, `ComputeBounds()`, `DecomposeTRS()`.

These exist because every converter would otherwise write its own. Two are
worth explaining:

- **`FlattenTransforms`** bakes world transforms into vertices and reduces the
  graph to one node per mesh — what a writer for a format with no scene graph
  (STL, OBJ, PLY, OFF) needs, and what the GL viewer wants. It drops skins and
  animations, which are meaningless once a pose is baked, and reports how many.
- **`WeldVertices` is attribute-aware.** Two corners at the same position with
  different normals do *not* merge, because merging them would destroy the
  faceting the file specified. Measured on the 510 671-triangle STL sample in
  `media/vector/STL`: attribute-aware welding merges **3.4 %** of the 1 532 013
  vertices (the genuinely coplanar neighbours), while ignoring normals merges
  **82.9 %** (1 532 013 → 261 504). Both numbers are correct answers to
  different questions; a caller wanting topology for simplification clears the
  normals first.

### 4.3 The converter interface

`UltraCanvas/include/DataFormats/UltraCanvasModelConverter.h` mirrors
`IVectorFormatConverter` — same shape, same contract: `Import`/`Export` in
file, memory and stream forms, `ValidateFile`/`ValidateData` by signature
rather than extension, a `FormatCapabilities` report, and a
`WarningCallback` that a converter **must** call rather than silently dropping
what it cannot represent.

`FormatCapabilities` must be computed from what a converter actually
implements. The 2D survey found capability flags that no longer matched their
converters, and a flag that lies is worse than one that is absent.

### 4.4 Migration seam

`UltraCanvas/Plugins/Models/UltraCanvasModelMesh3D.{h,cpp}` converts between
`ModelDocument` and the existing flat `Mesh3D`, so the STL viewer and the
Filer's thumbnails keep working unchanged while converters move over one format
at a time. It lives on the plugin side so core never depends on a plugin type.

### 4.5 Tests

`Tests/ModelStorageTest.cpp` (registered with CTest) covers TRS composition and
decomposition including shear rejection, double-precision retention at 10⁶,
every primitive topology, n-gon preservation and triangulation, strip winding,
point clouds with custom attributes, instancing, transform flattening,
up-axis conversion, unit bookkeeping, and both directions of the material
derivation — then runs the whole path against the real STL sample when given
one. 30 assertions, all passing.

## 5. Proposal — converters, in order

Each step is independently mergeable and comes with a test and a demo page.

0. **3DS — done.** `Plugins/Models/3DS/UltraCanvas3DSConverter.cpp` reads the
   chunked binary format into `ModelDocument`, validated against the E-45
   aircraft sample in `media/models/3DS`
   (`Tests/Model3DSTest.cpp`, 25 assertions). It landed first because it was
   the sample to hand and because it exercises far more of the structure than
   STL does: two named meshes, per-object matrices, two materials with four
   texture maps, and per-face material groups. What it taught is in §6.

1. **STL on the new structure.** Re-express the existing loader as an
   `IModelFormatConverter`. No new parsing — it is the smallest possible proof
   that the structure and the interface fit, and it retires the bridge for that
   format.
2. **OBJ + MTL.** The most-requested missing format, and the demo app already
   contains a working reader (`UltraCanvasGLDemoSupport.h`) to fold in and
   delete. Exercises n-gons, groups, texture coordinates, the Phong block and
   external texture references.
3. **PLY.** Exercises the open-ended attribute design harder than anything
   else, and brings point clouds with it. ASCII and both binary orders.
4. **glTF 2.0 / GLB.** The interchange target: scene graph, PBR materials,
   skins, animation, morph targets. Once this reads and writes, UltraCanvas can
   exchange with the rest of the industry. JSON is already available through
   `UltraCanvasJSON`.
5. **DXF/DWG 3D entities into `ModelDocument`.** The highest value per line of
   new code in the list: `UltraCanvasDXFReader` already parses `3DFACE`,
   polyface and polygon meshes and currently flattens them to 2D. Give the
   reader a 3D output path and the framework gains CAD model import from
   readers it already owns.
6. **3MF.** The manufacturing pair for STL, and the reason `UnitScaleToMeters`
   and components/instancing exist. ZIP is available (`UltraCanvasZipPackage`).
7. **OFF, then AMF** — small, and they close out the mesh-format set.
8. **FileLoader integration**: a `UltraCanvasModelFormatsPlugin` exposing the
   matrix to `LoadGraphicsFile`/`SaveGraphicsFile`, so the `Model3D` category
   stops being STL-only and the Filer's Model badges become real. Register it
   from framework init rather than from a demo page — today `RegisterSTLPlugin()`
   has exactly one caller, in `UltraCanvasFileLoaderExamples.cpp`, so `.stl` is
   invisible to FileLoader until that demo page is opened.

Then, closing the 1.0 gate from the versioning investigation:

9. **Retire the duplicate 3D types.** Promote or delete
   `Plugins/Models/STL/UltraCanvas3DTypes.h`, repoint `UltraCanvasScatterPlot3D`
   and `UltraCanvasContourSurface3D` (which currently reach into a file-format
   plugin's directory), delete the demo app's private `Vec3`/`Mat4`/`Mesh`, and
   collapse the three 3D-chart cameras into one.

## 6. Known gaps in the structure as merged

Recorded rather than hidden:

- **Concave n-gons triangulate by fan** about the first vertex, which is
  correct for the convex faces mesh formats emit in practice but wrong for a
  concave face. Ear clipping is the fix, needed before a PLY or OBJ reader
  meets hand-authored concave geometry.
- **OBJ smoothing groups** are resolved into normals at import and not carried,
  so an OBJ → OBJ round trip loses the `s` statements. Adding a per-face
  smoothing-group array is cheap if the round trip turns out to matter.
- **`KHR_materials_*` extension blocks** land in `ModelMaterial::Extras` as
  text rather than typed fields. Correct for the long tail; the common ones
  (transmission, clearcoat, sheen, ior beyond the existing field) may deserve
  promotion once a glTF reader exists to fill them.
- **No native serialisation yet.** `ModelFormat::UltraCanvas` is declared but
  has no writer; the document is not yet persistable in its own right.
- **Welding is a hash-grid merge**, so two vertices within tolerance but
  straddling a lattice boundary do not merge. Exact enough for de-duplicating
  soup, not a substitute for a proper spatial merge.
- **3DS KFDATA is not read.** The keyframer section carries the node
  hierarchy, pivots and position/rotation/scale tracks. The reader warns when
  it is present, so a scene with a hierarchy is known to arrive as a flat list
  of siblings rather than silently mis-assembled. The sample has none.
- **3DS cameras have a position but no orientation.** The format states a
  look-at target and a bank angle; `ModelNode` orients by transform, and the
  conversion is not done yet, so target and bank are kept as node metadata and
  the reader says so. Untested — the sample has no camera or light.
- **Smoothing groups are resolved into normals** on 3DS import as they are for
  OBJ, so neither round-trips them.

### What the first real file changed

Two things the E-45 sample corrected, both worth recording because the next
converter would have hit them too:

- **Phong specular does not imply metal.** `DeriveMissingModel` originally read
  a bright specular as metallic, so every painted panel on the aircraft — white
  specular, bright diffuse, the most common material in MTL, 3DS and COLLADA
  files — imported as raw metal, which renders black without an environment.
  Metal is now required to have a *dark diffuse* as well, since what physically
  distinguishes a metal is having no diffuse albedo; and a metal takes its base
  colour from the specular rather than the black diffuse. Pinned by
  `Tests/ModelStorageTest.cpp`.
- **`Matrix4x4::InverseAffine`** had to be added. 3DS stores vertices already
  in world space beside the object's own matrix; putting the matrix on the node
  and the inverse-transformed vertices in the mesh is what keeps the object
  frame as real structure and composes back to exactly the file's world
  coordinates (asserted against the raw `POINT_ARRAY` extents).

## 7. Non-goals

- **B-rep and parametric solids** — STEP, IGES, ACIS, Parasolid, OpenNURBS and
  the DWG `3DSOLID` family. See §2.5: they need their own structure, and a
  mesh document that pretended to hold them would lie about what it round-trips.
- **A renderer.** `ModelDocument` is an exchange and editing structure.
  Rendering stays with `UltraCanvasSTLElement` / `UltraCanvasGLSurface`, which
  consume a flattened mesh.
- **A scene-authoring API** — physics, constraints, LOD, materials graphs.
  What a file format carries is the boundary.
- **Replacing `Mesh3D` in one step.** The bridge exists so formats migrate
  individually; a flag-day rewrite of the STL viewer, the Filer thumbnails and
  the 3D charts is exactly the kind of change the versioning investigation
  wants staged across `0.6.x`.
