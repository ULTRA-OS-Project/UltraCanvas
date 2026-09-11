# UltraCanvas 3D Model Structure — Survey and Proposal

**Status:** structure defined and merged; 3DS, OBJ, DXF and COLLADA read into it, OBJ writes from it; glTF next
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
| `OBJConverter` | `Plugins/Models/OBJ/` | Reads **and writes** Wavefront OBJ + MTL: n-gons kept as n-gons, the three index streams resolved into unique corners, objects, groups, `usemtl`, vertex colours, and the MTL PBR extension. The first writer on the structure. |
| `ColladaConverter` | `Plugins/Models/COLLADA/` | Reads COLLADA 1.4/1.5: `<unit>`, `<up_axis>`, the node hierarchy with ordered transform elements, `<polylist>`/`<triangles>`/`<polygons>`, `profile_COMMON` materials with transparency and textures, vertex colours, and matrix or TRS animation channels. The first format that states everything the structure holds. |
| `BlendConverter` | `Plugins/Models/Blend/` | Recognises a Blender `.blend`, reports what it contains — version, objects, meshes, materials, modifiers, stored vertex count — and **declines to import geometry**, with the reason. See §2.6. |
| `DXFModelConverter` | `Plugins/Models/DXF/` | Reads the 3D entity set — `3DFACE`, polyface meshes, polygon meshes, 3D polylines, lines and points — into `ModelDocument`, one mesh per layer with the layer's ACI colour as its material. The complement of the reader below, not a replacement. |
| `FbxConverter` | `Plugins/Models/FBX/` | Reads FBX 7.x binary and 6.x/7.x ASCII: the connection graph, the full transform chain, meshes with independently indexed layers, Phong materials with textures, and animation. The only format here whose scene is a graph rather than a tree, and the only extension naming two object models — see §2.8. Read-only, needs zlib. |
| `XFileConverter` | `Plugins/Models/XFile/` | Reads Direct3D retained mode's `.x`, text and binary: the Frame hierarchy, n-gon meshes, per-face material lists, Phong materials with a texture name. The one **left-handed** format in the set — see §2.7. Read-only, geometry only. |
| 3D CAD entities | `Plugins/Vector/UltraCanvasDXFReader.cpp` | DXF/DWG `3DFACE`, polyface and polygon meshes are **read and then flattened to 2D** — correct for a drawing; `DXFModelConverter` is where the same entities go when the file is a model. `3DSOLID`, `REGION`, `BODY`, `SURFACE` are counted and skipped by both. |

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
| **DirectX .x** | `Frame` tree, one 4x4 matrix each | `Mesh` with n-gon `MeshFace`s, `MeshNormals`, `MeshTextureCoords`, `MeshVertexColors` | `MeshMaterialList` + `Material` (faceColor, power, specular, emissive) + `TextureFilename` | `AnimationSet` → `Animation` → `AnimationKey` | none stated; Y-up, **left-handed** |
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

### 2.5 CAD / B-rep — held exactly, not tessellated away

**STEP** (ISO 10303 AP203/214/242), **IGES**, **ACIS/SAT**, **Parasolid**,
**3DM/OpenNURBS**, and the **`3DSOLID`/`REGION`/`BODY`/`SURFACE`** entities in
DWG and DXF do not contain meshes. They contain *boundary representations*:
trimmed NURBS surfaces, topological faces, edges, loops and vertices, solid
bodies, assemblies and PMI annotation.

> **This section previously argued the opposite**, and the reversal is worth
> recording rather than quietly editing away. The original decision was that
> B-rep was out of scope, that a reader should tessellate on import and warn,
> and that parametric solids could have their own document later if anyone
> wanted them. That was wrong on the point that matters: **a triangle mesh is a
> *view* of a B-rep, taken at a tolerance the file never stated.** A reader that
> tessellates and discards has decided, on the user's behalf and irreversibly,
> that the model is approximate from now on. Every later question — is this
> hole exactly 12 mm, does this face meet that one, what is the volume, can
> this be written back out as STEP — is then unanswerable, and no amount of
> warning at import time gives the answer back. The structure is called
> universal; a universal 3D structure that cannot hold what half of engineering
> ships is not.

**The decision: `ModelDocument` holds B-rep exactly, alongside meshes.**
`ModelDocument::Brep` is a `BrepData` — the pool declared in
`UltraCanvasBrepStorage.h` — and a `ModelNode` points at a solid in it exactly
as it points at a mesh. The two coexist: a node may carry both the exact body
and the mesh that was made from it.

The hierarchy is the one every B-rep kernel and every B-rep file format agrees
on, so a reader copies rather than converts:

```
Solid  -> Shells    the first is the outer boundary, the rest are voids
Shell  -> Faces
Face   -> Surface + Loops   the first bounds it, the rest are holes
Loop   -> Coedges           an ordered, closed circuit
Coedge -> Edge + orientation + optional parameter-space curve
Edge   -> Curve + two Vertices + a parameter range
Vertex -> a point
```

Geometry is analytic where the format states it analytically and NURBS where it
does not, because that is how the formats themselves are written and converting
either way loses something:

| | analytic | general |
|---|---|---|
| curves | line, circle, ellipse, parabola, hyperbola, polyline | rational B-spline: degree, knots, control points, weights |
| surfaces | plane, cylinder, cone, sphere, torus, extrusion, revolution, ruled | rational B-spline tensor product |

That table is the intersection of what STEP's `geometry_schema`, IGES entities
100–198, the ACIS surface types and OpenNURBS all carry. A cylinder is two
numbers and a placement in all four; storing it as a control net would be
lossy, larger and slower, and would make "is this hole round" unanswerable.

**Trimming** is the part a mesh structure has no analogue for, and the part
that decides whether a face can be meshed at all. A `BrepCoedge` carries an
optional `ParameterCurve` — STEP's pcurve, IGES entity 142, the ACIS coedge's
own curve — because the 3D edge curve alone does not say which side of itself
is material. Where a file carries none, `BrepSurface::Project` inverts the
surface: closed-form for every analytic type, and a seeded descent for NURBS.

**Tessellation moves out of the reader and becomes an operation:**
`ModelDocument::TessellateBreps(options)`. That is the whole difference. It can
be run at a tolerance the caller picks, run twice for two levels of detail,
re-run finer without re-reading the file, or never run at all by a consumer
that wants the surfaces. The exact bodies stay. What the old rule called
"tessellates and warns" is now something the caller asks for and the document
does — and `BrepData::Validate` is what a reader owes its caller instead:
structural soundness, every index in range, every loop closed, and every edge
of a closed shell used exactly twice in opposite directions.

What is still out of scope, and stated as such rather than implied: PMI
(dimensions, tolerances, annotation), constructive-solid history trees, and
assembly-level constraints. `BrepSolid::Extras` and `BrepFace::Extras` carry
those as text so a reader loses nothing silently, but nothing interprets them.

### 2.6 Application-native files — the second deliberate exclusion

**`.blend`** (and by the same argument `.max`, `.ma`/`.mb`, `.c4d`) is not an
interchange format. It is a dump of the application's in-memory structures,
and the model an artist sees is not in it: the file stores the *unevaluated*
scene, with modifiers, generators and procedural nodes still to run. Producing
the visible geometry means reimplementing the application's evaluation, which
is the application.

The E-45 sample settles it with numbers rather than principle. Its `.blend`
stores **1 147 vertices** behind Mirror, Subsurf and EdgeSplit modifiers. The
same model exported to OBJ, with those modifiers applied, is **11 749**. A
reader of the stored mesh would deliver a tenth of the aircraft — and half of
it in X, since the mirror is one of the unapplied modifiers. The `.dae` export
in §2.2 shows the same thing from the other side: it was exported without
applying the mirror, and holds exactly the 1 147 positions the `.blend` does.

The `.abc` export is a third witness, and the clearest one, because Alembic
carries geometry that has already been evaluated: it holds **2 934 vertices in
1 681 faces**, and the hull's own X range stops dead at zero. The archive's
top-level bound is symmetric at ±0.9732 while the mesh inside it only spans
[-0.9732, 0] — Blender wrote the bound from the evaluated model and the mesh
from the cage. So even the format whose whole purpose is baked geometry came
out of this scene half-mirrored. The `.blend` is not an unlucky case; this
scene's exports are, and the OBJ is the only one of the four that had the
modifiers applied.

The **7.4 binary `.fbx`** export lands on the same side, and adds the detail
that settles what the split tracks. Its geometry is **2934 positions in 1681
polygons — the `.abc` export's, to the vertex** — while its scene is titled
`E-45_GLSL` and its `ArmatureAction` runs 0.8333333 s, both of which the `.dae`
also carries. So the groups are not per-format at all: FBX shares its geometry
with Alembic and its scene metadata with COLLADA, because all three came out of
the same unapplied-modifier path on the same day. `Tests/ModelFbxTest.cpp`
asserts each of those three figures against the suite that already owned it, so
no two of these readers can drift apart without one of them failing.

The **6.1 ASCII `.fbx`** export then removes the last doubt, because it is the
same format landing on the *other* side: 8110 faces in 11749 positions,
symmetric about X, which is the OBJ/PLY/X3D group exactly — while still carrying
the same 0.8333333 s `ArmatureAction` as the binary. Two files with the same
extension, holding the same scene, on opposite sides of the split. Whatever the
groups track, it is not the format; it is which export session the file came out
of. That is also why the reader must never normalise the difference away: the
only honest thing a reader can do with two files that disagree is report what
each one says.
The `.x` export is a fourth witness, and it identifies what the split actually
tracks. It holds **1995 triangles in two meshes with the hull's X range stopping
at zero** — which is the `.dae` export's count, mesh division and half-hull
exactly. So these are not four independent exports that happened to differ:
they are two groups, and membership is decided by which export path was taken,
not by what the format is capable of. `Tests/ModelXFileTest.cpp` asserts the
1995 against the COLLADA suite's own figure, so the two readers cannot drift
apart without one of them failing.

`.blend` is also self-describing through an embedded SDNA block, which makes
the *file* readable even though the *model* is not. So the decision is:

**Recognise and describe, never import.** `BlendConverter` reads the header,
the block index and the SDNA — the three parts that have been stable across
many Blender releases — and reports version, pointer size, compression,
datablock names, modifier types and the stored vertex count. Then it declines,
with a sentence naming the modifiers and pointing at the export that would
work. Its `FormatCapabilities` are all false, because a capability report says
what a converter does, not what its format could hold.

That is more useful than either alternative: a silent failure tells the user
nothing, and a geometry reader tells them something false.

### 2.7 Handedness — the one thing a format can get wrong invisibly

Every format above is right-handed except one. Direct3D's space is left-handed,
so an exporter writing `.x` from a right-handed application does two things: it
puts a **reflection** in the root frame's matrix — determinant −1, not a
rotation — and it writes each face's indices the other way round. Both are in
the file, and they cancel.

That matters because each half, seen alone, looks like a bug. The E-45's `.x`
meshes have *negative* signed volume in their own object space, and their
winding disagrees with the file's own `MeshNormals` on 93 of 93 and 925 of 937
faces. A reader that trusted either observation and "corrected" the winding
would produce a model that is inside out — because through the frame chain the
volume is positive and the normals agree, on exactly those same faces.

So the rule for this structure is: **carry what the file says, including a
reflecting node transform, and let world space be where handedness is resolved.**
`ModelDocument` needs nothing new for it — `Matrix4x4::DecomposeTRS` holds a
reflection as a negative scale on one axis, which reproduces the matrix exactly.

The check is worth keeping, though, because the cancellation only holds for a
well-formed export. `XFileConverter` compares each face's winding against the
file's own stored normals *through the node's world transform* — which costs
only the sign of that transform's determinant — and reports a file whose faces
really are inside out rather than loading it in silence. That is the same
principle as the Alembic reader's winding assertion, reaching the opposite
conclusion because the file is built the opposite way: Alembic stores no
compensating reflection, so its faces genuinely must be reversed on import.

### 2.8 Connections, not nesting — what FBX does differently

Every other format in this survey writes its scene as a tree: a node contains
its children, a shape contains its geometry. FBX writes neither. Its `Objects`
block is a flat list — models, geometries, materials, textures, animation curves,
all side by side with 64-bit ids — and a separate `Connections` block wires them
together after the fact:

```
C: "OO", 667650837, 205769600     ; this geometry is drawn by that model
C: "OO",   5873026, 205769600     ; this material is used by that model
C: "OP", 516486545, 121849744, "Lcl Translation"   ; this curve node drives that
```

Nothing can be read in file order. The reader indexes the objects by id, turns
the connections into a parent-to-children map, and only then walks from the
scene root — which FBX spells as parent id `0`. Three consequences worth
recording, because they are what the structure had to absorb:

- **A geometry's material assignment is not global.** `LayerElementMaterial`
  indexes the materials connected *to the model*, not a document-wide list. One
  geometry connected to two models with different materials is therefore two
  document meshes, since the material lives on the primitive.
- **Instancing is free and invisible.** One geometry connected to several models
  is the format's normal way of repeating a prop, and falls straight onto
  `ModelNode::Mesh` sharing an index.
- **Animation is three hops, not a channel.** A stack holds layers, a layer holds
  curve nodes, a curve node is connected to one property of one model and to one
  curve per axis. Only after following all of it does a channel exist.

The transform is the other place FBX is alone. It is not TRS but

```
T · Roff · Rp · Rpre · R · Rpost⁻¹ · Rp⁻¹ · Soff · Sp · S · Sp⁻¹
```

— rotation and scaling pivots, offsets, and pre- and post-rotations, all of
which Maya and 3ds Max use constantly and a Blender export leaves at identity.
`ModelDocument` needs nothing new for it: the chain is composed and then
decomposed, so the common case comes back as exactly the three fields that were
written. What the structure genuinely cannot hold is the *geometric* transform,
which places a mesh without being inherited by the node's children; that becomes
a child node carrying the mesh, which is exact and costs one node.

One more thing FBX does that no other format here does: **`.fbx` names two file
formats.** The 7.x binary above is a length-prefixed record tree. The 6.x ASCII
is indented text — and had it been only an encoding, a second lexer would have
finished it. It is not. The object model underneath differs:

| | 7.x | 6.x |
|---|---|---|
| object identity | 64-bit ids | `"Class::Name"` strings |
| geometry | its own object, connected | nested inside the `Model` |
| property records | `P:` in `Properties70`, 5 fields | `Property:` in `Properties60`, 4 |
| texture → material | an `OP` connection to the property | inferred from sharing a model |
| animation | `AnimationStack` → layer → curve node → curve | `Takes` → `Channel` per axis |
| scene settings | `GlobalSettings`, a root record | inside `Objects` |

Almost all of it absorbs into the reader rather than the structure. Synthesising
an id per `"Class::Name"` makes 6.x's connections look like 7.x's, so everything
downstream of the index is shared. The transform chain, the layer mappings, the
colour × factor rule and the 46186158000-tick second are identical in both, which
the tests assert by reading the same aircraft twice.

Two things did not absorb. The first is that binary writes an array as one
property while ASCII writes one property per number, so the container has to
offer a `ValueCount`/`ValueAt` view that closes over both — otherwise every
geometry reads as empty, which is exactly the bug that showed up first. The
second is content that is not content: every 6.x export carries a `Camera
Switcher` and seven `Producer` cameras with no connection to anything, which are
skipped and counted into `Metadata` rather than dropped in silence.

And the two exports settle a question §2.6 left open. The 6.1 ASCII export of
this aircraft holds **8110 faces in 11749 positions and is symmetric about X**;
the 7.4 binary holds **1681 polygons in 2934 positions and is not**. Same scene,
same format, opposite sides of the mirror-modifier split — which is the clearest
possible statement that the split tracks the export *session*, not the format.
The 0.8333333 s `ArmatureAction` in both is what proves they are the same scene.
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
│                               Mesh, Solid, Camera, Light, Skin,
│                               MorphWeights[], Extras{} }
├─ Meshes[]      ModelMesh    { Name, Primitives[] }
│                MeshPrimitive{ Mode, Positions[], Normals[], Tangents[],
│                               Attributes[], Indices[], FaceStarts[],
│                               SmoothingGroups[], Material, Targets[] }
├─ Brep          BrepData     { Curves[], Curves2D[], Surfaces[], Vertices[],
│                               Edges[], Loops[], Faces[], Shells[], Solids[] }
│                              exact trimmed surfaces and topology - §2.5
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

**Exact bodies live beside meshes, not instead of them.** `Brep` is a member of
the document rather than a separate document, and a node points at a solid the
same way it points at a mesh, so a CAD file's exact surfaces and the mesh made
from them are one object. See §2.5, which reverses the earlier decision and
says why. The vector and matrix types both halves are built from moved to
`UltraCanvasModelMath.h`, because a document owns its `BrepData` and the B-rep
header therefore cannot include the mesh one.

### 4.2 Operations the structure provides

`Triangulate()` / `TriangulateAll()`, `RecomputeNormals()` (area-weighted, and
smoothing-group-aware), `WeldVertices(tolerance)`, `FlattenTransforms()`,
`ConvertUpAxis()`, `GlobalTransform(node)`, `ComputeBounds()`,
`DecomposeTRS()`, `TessellateBreps(options)`, and on the B-rep pool
`Validate()`, `TessellateFace()` and `ComputeBounds()`.

These exist because every converter would otherwise write its own. Four are
worth explaining:

- **`Triangulate` ear-clips rather than fans.** A fan about the first vertex is
  right for a convex face and wrong for a concave one, where it emits triangles
  outside the polygon. Convex faces still take the fan — the same answer, far
  cheaper — and only a concave face pays for the clipper.
- **`TessellateBreps` is where a B-rep becomes triangles**, and it is the
  caller's decision rather than a reader's. It leaves the exact bodies in
  place, meshes an instanced solid once, splits by material, and reports
  per-face failures rather than aborting. §2.5 has the argument.

- **`FlattenTransforms`** bakes world transforms into vertices and reduces the
  graph to one node per mesh — what a writer for a format with no scene graph
  (STL, OBJ, PLY, OFF) needs, and what the GL viewer wants. It drops skins and
  animations, which are meaningless once a pose is baked, and reports how many.
- **`WeldVertices` searches a neighbourhood, not a cell.** Candidates come from
  the vertex's lattice cell and its 26 neighbours, and the merge is decided by
  distance — because rounding is discontinuous exactly where geometry sits, so
  bucketing alone failed on the seams most worth welding.
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
up-axis conversion, unit bookkeeping, both directions of the material
derivation, concave ear clipping by area, welding across a lattice boundary,
and smoothing groups through both normal generation and triangulation — then
runs the whole path against the real STL sample when given one.

`Tests/BrepStorageTest.cpp` (also CTest-registered, and needing no sample file
because it builds its bodies) covers the exact half. Its assertions are
deliberately geometric, because a B-rep that holds the right numbers and meshes
them wrongly produces a model that looks plausible and is not the part:

- a rational quadratic B-spline reproduces a circular arc to 10⁻¹², and drops
  visibly off it when the weights are removed — so the rational path is real;
- analytic surfaces invert: a point evaluated on a sphere or torus projects
  back to the parameters it came from;
- validation catches a dangling surface index, a loop that does not close, and
  an edge used twice in the same direction — while leaving an open shell alone,
  because a sheet body is legitimate;
- a meshed box has signed volume exactly 48 for a 2×4×6 body, which is only
  true if every one of its six faces is wound outward, and exactly 12
  triangles, which is only true if the sampler decimates what the tolerance
  does not need;
- a meshed cylinder puts every vertex on the exact surface and brackets the
  true lateral area from below, at 96 / 384 / 1534 triangles for tolerances of
  0.1 / 0.01 / 0.001;
- a plate with a square hole meshes to area exactly 96 with no triangle inside
  the hole — area alone would pass with overlapping triangles, which is what a
  naive triangulation of a bridged polygon produces;
- an untrimmed bicubic patch meshes with every edge midpoint inside the chord
  tolerance of the true surface;
- one solid placed by two nodes is meshed once and shared, and a face with its
  own material becomes its own primitive.

`Tests/ModelAlembicTest.cpp` covers the Ogawa container and AbcGeom against the
same aircraft. Two of its assertions are worth naming. The first is the face
winding: Alembic winds a face's indices the opposite way from the outward-normal
convention, and because the file carries its own per-corner normals, comparing
each face's computed normal against the stored one is an *independent* check of
a decision that is otherwise invisible until a model renders inside out — 1 680
of 1 681 faces disagree without the reversal, 10 with it. The second is the
cross-format comparison in §2.6: the suite asserts that the OBJ is symmetric
about X and the Alembic is not, so a later change cannot quietly "fix" the
difference between two exports of one scene.

`Tests/ModelFbxTest.cpp` covers FBX, and is the suite that has to build its own
input: nothing about the binary container, the deflate-compressed arrays, the
layer mapping combinations, the transform chain's pivots or the malformed-file
refusals is reachable from an exported sample, so the synthetic half writes
binary FBX by hand. Two of its assertions are worth naming. The first is that
the same scene written with raw arrays and with compressed ones must produce
*identical* geometry — compression is the container's business and must not
reach the reader. The second is that `RotationOrder` changes the result: two
documents differing only in that field must place a vertex differently, which is
the only way to prove the field is being read rather than assumed to be XYZ.
The sample half pins the cross-format facts in §2.6 and §4.5's animation
figure: the same title as the `.dae`, the same 0.8333333 s duration, the same
1681 polygons as the `.abc`. It runs against two samples, because `.fbx` names
two formats — and the second one is where that pays: the 6.1 ASCII export gives
8110 faces symmetric about X where the 7.4 binary gives 1681 that are not, so
the suite pins the fact that one extension, one scene and one aircraft can still
be two different exports.
`Tests/ModelXFileTest.cpp` covers the `.x` reader, and is the suite where the
synthetic half matters most: one text export reaches neither the binary encoding
nor any of the cases that make the format treacherous. So the binary encoding is
built byte by byte and asserted to produce the same document as the same scene in
text — one grammar, two spellings — and the handedness cases are constructed
directly: a triangle wound against its own normal under an identity frame *must*
be reported as inside out, and the same triangle under a reflecting frame *must
not* be, because there the two cancel. Two refusals are pinned as well, both of
the same shape: a count is never trusted over the bytes present, whether it is a
`Mesh` claiming a hundred thousand vertices it does not carry or a binary
`FLOAT_LIST` longer than the file.

`Tests/ModelStepTest.cpp` covers the first B-rep reader in two halves. The Part
21 grammar is unit-tested on text written inline — doubled quotes, `\X2\`
escapes, comments between any two tokens, `$` and `*`, out-of-order ids, a
truncated file, and the complex instance that a rational NURBS can only be
written as. The entity layer runs against three **hand-authored** samples in
`media/models/STEP`, hand-authored precisely because a reader tested only
against files its own writer produced proves nothing:

- `Box.step` — a 10×20×30 mm block whose left face states its loop backwards
  with a `.F.` bound orientation. It meshes to exactly 12 triangles with signed
  volume 6000 and area 2200; a reader that ignores that flag builds one face
  inside out, and the signed volume is the only measure that notices.
- `Pin.step` — a cylinder in **inches**, through a `conversion_based_unit`
  rather than an SI one (the case that arrives a thousand times the wrong size),
  with a seam edge used twice by one loop and a presentation chain colouring the
  body brass with one face overridden red. Its meshed volume is under π·r²·h and
  closes on it as the tolerance tightens: 4998.8 → 5019.5 → 5026.1 of 5026.5.
- `NurbsSheet.step` — a quarter cylinder as a *rational* B-spline surface, the
  complex-instance form. The middle control column carries weight cos 45°, which
  is what makes the patch an exact arc rather than a parabola, so every point of
  it is 5 from the axis to 10⁻¹⁵. Its boundary carries no pcurves, so the meshed
  area being a quarter cylinder's proves the surface was inverted correctly.

Then each is written back out and re-read: same solids, faces, edges, surfaces,
unit and colours, and — because the surfaces are exact on both sides — the *same
meshed volume to 10⁻⁹*, not merely a close one. A unit cube of twelve triangles
written as a faceted b-rep comes back as 12 faces, 8 vertices and 18 edges,
which is Euler's formula for a triangulated cube and only holds if neighbouring
facets share their edges.

## 5. Proposal — converters, in order

Each step is independently mergeable and comes with a test and a demo page.

0. **3DS — done.** `Plugins/Models/3DS/UltraCanvas3DSConverter.cpp` reads the
   chunked binary format into `ModelDocument`, validated against the E-45
   aircraft sample in `media/models/3DS`
   (`Tests/Model3DSTest.cpp`, 25 assertions). It landed first because it was
   the sample to hand and because it exercises far more of the structure than
   STL does: two named meshes, per-object matrices, two materials with four
   texture maps, and per-face material groups. What it taught is in §6.

0.5. **OBJ + MTL — done.** `Plugins/Models/OBJ/UltraCanvasOBJConverter.cpp`
   reads and writes it, validated against the same aircraft exported as OBJ
   (`Tests/ModelOBJTest.cpp`, 34 assertions). Being the first format with a
   writer, it is where the round trip lives — OBJ → `ModelDocument` → OBJ →
   `ModelDocument` returns the same 12 227 vertices, 8 110 faces and bounds,
   with the quads still quads. Still to fold in: the demo app's private
   `LoadOBJ` in `UltraCanvasGLDemoSupport.h`, which should be deleted in favour
   of this converter.

1. **STL on the new structure.** Re-express the existing loader as an
   `IModelFormatConverter`. No new parsing — it is the smallest possible proof
   that the structure and the interface fit, and it retires the bridge for that
   format.
3. **PLY.** Exercises the open-ended attribute design harder than anything
   else, and brings point clouds with it. ASCII and both binary orders.
3.5. **COLLADA — done.** `Plugins/Models/COLLADA/UltraCanvasColladaConverter.cpp`
   reads it, validated against the aircraft's `.dae`
   (`Tests/ModelColladaTest.cpp`, 41 assertions). It landed before glTF because
   the sample arrived, and it turned out to be the format that finally
   exercises the whole structure: the first with a declared unit
   (`<unit meter="1"/>`), the first with a node hierarchy four deep, the first
   with real animation. Nothing in the structure had to change to hold it,
   which is the strongest evidence so far that the design is right.

3.7. **DirectX .x — done.** `Plugins/Models/XFile/` reads it, text and binary,
   validated against the aircraft's `.x` (`Tests/ModelXFileTest.cpp`, 74
   assertions). It is split into a container layer and an object layer for the
   same reason STEP and Alembic are. What it added to this document is §2.7:
   it is the first left-handed format, and the first where the *right* answer
   is to change nothing and check instead. Read-only and geometry only —
   `AnimationSet` is reported rather than read, because an `.x` rotation key is
   a quaternion in a convention no sample here can verify, and the capability
   report says `Animations = false` rather than implying otherwise. **Still to
   do:** the two MSZIP encodings, and animation once a file with some exists.

3.8. **FBX — done.** `Plugins/Models/FBX/` reads the 7.1–7.7 binary encoding and
   the 6.x/7.x ASCII one, validated against two exports of the aircraft
   (`Tests/ModelFbxTest.cpp`, 111 assertions). It was the largest remaining hole
   in the matrix and the one most likely to be asked for, since FBX is what the
   animation industry actually exchanges. It is split into a container layer and
   an object layer for the same reason STEP, Alembic and `.x` are. What it added
   to this document is §2.8: it is the only format here whose scene is a
   connection graph rather than a tree, the only one with a transform chain
   rather than a TRS triple, and the only extension naming two object models —
   and `ModelDocument` needed nothing new for any of the three. Read-only, and
   gated on zlib because its arrays are deflate streams. **Still to do:** skin
   deformers and blend shapes, cameras and lights, and embedded media.

4. **glTF 2.0 / GLB.** The interchange target: scene graph, PBR materials,
   skins, animation, morph targets. Once this reads and writes, UltraCanvas can
   exchange with the rest of the industry. JSON is already available through
   `UltraCanvasJSON`.
5. **DXF 3D entities — done.**
   `Plugins/Models/DXF/UltraCanvasDXFModelConverter.cpp` reads `3DFACE`,
   polyface meshes, polygon meshes, 3D polylines, lines and points into
   `ModelDocument`, one mesh per layer, validated against the aircraft's DXF
   export (`Tests/ModelDXFTest.cpp`, 41 assertions). It is a separate reader
   rather than a second output path threaded through the Vector plugin's
   `UltraCanvasDXFReader`: that one is a large 2D machine built around
   `VectorDocument`, and the two formats-of-DXF — drawing and model — want
   genuinely different readers. **Still to do:** DWG, whose native decoder
   already produces DXF text (`DWGConverter::DecodeToDxf`), so pointing that
   text at this reader is nearly free; and unifying the two DXF tag scanners,
   which are now duplicated.
6. **3MF.** The manufacturing pair for STL, and the reason `UnitScaleToMeters`
   and components/instancing exist. ZIP is available (`UltraCanvasZipPackage`).
7. **OFF, then AMF** — small, and they close out the mesh-format set.
8. **FileLoader integration — done.** `UltraCanvasModelFormatsPlugin`
   (`Plugins/Models/`) exposes the matrix to `LoadGraphicsFile` /
   `SaveGraphicsFile`, so the `Model3D` category is no longer STL-only and the
   Filer's Model badges have something behind them.
   `RegisterModelFormatsPlugin()` is called from `Apps/DemoApp/main.cpp`
   beside `RegisterVectorFormatsPlugin()`, and registers the STL plugin too —
   which fixes the older bug that `.stl` was invisible to FileLoader unless a
   particular demo page had been opened.

   The converters also moved out of the core library into a real plugin target
   (`Plugins/Models/CMakeLists.txt`, `UltraCanvasModelsPlugin`,
   `ULTRACANVAS_HAS_MODELS_PLUGIN=1`), built like the Vector, CDR, XAR and EPS
   plugins and listed in `ULTRACANVAS_PLUGIN_TARGETS`. COLLADA and `.blend` are
   options within it, since each pulls a dependency (tinyxml2, zlib) the rest
   does not need, and each gets its own define so a caller can `#ifdef` on
   exactly what was built.

   Two details worth knowing. **`.dxf` is dispatchable but not claimed**: a DXF
   is a drawing far more often than a model, so `LoadGraphicsFile` keeps giving
   the Vector plugin's 2D reader, and a caller that wants the 3D entities asks
   for the converter by name. And **extension dispatch is a separate
   translation unit** from the `IGraphicsPlugin` façade
   (`UltraCanvasModelFormatsDispatch.cpp` against
   `UltraCanvasModelFormatsPlugin.cpp`), because the façade hands back a viewer
   element and therefore needs the UI stack, while conversion needs nothing but
   the converters and the document. A tool that only converts files links a
   fraction of the framework, and the dispatch stays testable without a
   display.

Then, closing the 1.0 gate from the versioning investigation:

9. **Retire the duplicate 3D types.** Promote or delete
   `Plugins/Models/STL/UltraCanvas3DTypes.h`, repoint `UltraCanvasScatterPlot3D`
   and `UltraCanvasContourSurface3D` (which currently reach into a file-format
   plugin's directory), delete the demo app's private `Vec3`/`Mat4`/`Mesh`, and
   collapse the three 3D-chart cameras into one.

## 6. Known gaps in the structure as merged

Recorded rather than hidden:

- ~~**Concave n-gons triangulate by fan**~~ — fixed. `MeshPrimitive::Triangulate`
  ear-clips in the plane Newell's method fits to the face, so an L-shaped or
  slotted n-gon comes out as its own area rather than its convex hull. A
  triangle or a convex polygon still takes the fan, which is the same answer
  for a fraction of the work, so nothing got slower. The measure in the test is
  area: the L-shaped face is 5 units, and a fan about its first vertex gives 7.
  The ear clipper lives in `UltraCanvasPolygonTriangulation.h` because the
  B-rep face tessellator needs the same thing with holes, and used to have half
  of it.
- ~~**OBJ smoothing groups** are resolved into normals and not carried~~ —
  fixed. `MeshPrimitive::SmoothingGroups` is a 32-bit mask per face — the same
  idea as the 3DS `SMOOTH_GROUP` chunk, so both formats can use it — and the
  OBJ reader fills it while the writer emits `s` statements only where the
  value changes, `s` being stateful. `RecomputeNormals` now honours the groups
  rather than ignoring them: faces sharing a group average, faces sharing none
  crease, and the vertex where the two meet is *split* so both normals can
  exist, with attributes, tangents and morph deltas following the split.
  Triangulation carries each face's mask onto the triangles it produces.
- **`KHR_materials_*` extension blocks** land in `ModelMaterial::Extras` as
  text rather than typed fields. Correct for the long tail; the common ones
  (transmission, clearcoat, sheen, ior beyond the existing field) may deserve
  promotion once a glTF reader exists to fill them.
- **No native serialisation yet.** `ModelFormat::UltraCanvas` is declared but
  has no writer; the document is not yet persistable in its own right.
- ~~**Welding is a hash-grid merge**~~ — fixed. The lattice is still how
  candidates are found, but a vertex is now compared against its own cell *and
  its 26 neighbours*, and the merge is decided by real distance rather than by
  two quantised coordinates being equal. That matters more than it sounds:
  rounding is discontinuous exactly where geometry likes to sit — on the axis
  planes, at the origin, on every round number a CAD user typed — so
  single-cell bucketing failed on precisely the seams worth welding. Attributes
  still gate the merge exactly, because a normal or UV seam is a discontinuity
  the file meant, and a tolerance of zero still merges only bit-identical
  vertices.
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
- ~~**OBJ writes at the stream's default precision**~~ — fixed.
  `ConversionOptions::Precision` (`NumericPrecision::Compact` / `Full`) chooses
  between the stream default of 6 significant digits and enough digits to
  round-trip every value exactly: 17 for the double positions, 9 for the float
  attributes (`max_digits10` for each). Compact remains the default, because an
  OBJ is a deliverable more often than an intermediate. The difference is not
  academic — a survey coordinate of `1234567.8912345678` comes back as
  `1234570` under Compact, 2.11 units lost — and the cost is about a third more
  file size (the E-45 aircraft goes from 1.37 MB to 1.87 MB). The option lives
  on `ConversionOptions` rather than on the OBJ converter because every text
  format the matrix gains — PLY ASCII, glTF's JSON, COLLADA, X3D — faces the
  same choice.
- **OBJ `l` and `p` elements are not read**, so a file's polylines and points
  are dropped with a warning even though the document has modes for both.
- **`WeldVertices` after normal generation barely merges anything**, because
  the generated normals differ per corner in an unindexed soup and welding is
  attribute-aware. Measured on the aircraft DXF: 1 104 of 32 440 vertices. The
  real fix is crease-angle normal generation — weld positions first, then split
  normals only across edges sharper than a threshold — which would give both
  correct faceting and real de-duplication. Until then the reader keeps the
  faithful order (normals from the file's own faces, then weld) and welding is
  weak for STL, DXF and any other unindexed source.
- **DXF `BLOCKS`/`INSERT` are not expanded**, so 3D geometry placed through a
  block is missing. The reader warns when it finds 3D entities in `BLOCKS`.
- **Two DXF tag scanners now exist** — this one and the Vector plugin's. They
  should share one.
- **COLLADA `<library_controllers>` is not read**, so a skinned mesh arrives in
  its bind pose. The document has `ModelSkin` and the joints are already read
  as nodes, so this is a bounded piece of work rather than a design gap.
- **COLLADA per-axis rotation tracks** (`rotationX`/`Y`/`Z` channels) are not
  read: the document animates whole rotations as quaternions, and combining
  three separate angle tracks needs all of them together. Matrix-valued
  channels — what Blender writes, and what the sample uses — are read.
- **COLLADA cameras and lights are not read**, though the document has fields
  for both and the sample has neither.
- **Alembic reads its first time sample only.** An archive holding an animation
  arrives as its first frame. The document has `ModelAnimation` and morph
  targets to carry the rest, and the reader's `FormatCapabilities` says
  `Animations = false` rather than implying otherwise. Cameras, curves, points
  and NuPatch objects are skipped with a warning naming the schema.
- **Alembic `SubD` is read as its control cage.** The subdivided surface is not
  in the file — it is what the receiving application is meant to compute — so
  the cage is what a reader can honestly return, with a warning saying so.
  Reading it as a mesh silently under-reports the model, and subdividing it
  here would invent geometry the file does not contain.
- **Alembic states no unit and no up axis.** Y-up is recorded because every
  writer of the format works that way, but it is a convention rather than
  something the file said, and a Z-up archive would arrive on its side.
- **The COLLADA texture chain is unexercised.** The sampler2D → surface → image
  indirection is implemented, but the sample's `<library_images/>` is empty, so
  no test covers it against a real file.
- ~~**No B-rep reader exists yet**~~ — **STEP now reads and writes.**
  `Plugins/Models/STEP/` is a Part 21 parser (`UltraCanvasStepFile.h`, syntax
  only, including the complex instances every rational NURBS is written as) and
  an AP203/214/242 entity layer (`UltraCanvasStepConverter.h`) that fills
  `ModelDocument::Brep`. What it reads is listed in that header; what it does
  not is below. It writes too, as an `advanced_brep_shape_representation`, and a
  document holding meshes rather than solids is written as a faceted b-rep with
  shared edges — which is what STEP has for a mesh and what a CAD system will
  open. IGES, ACIS/SAT, Parasolid XT and OpenNURBS remain unread; the structure
  holds what they carry, so each is now a parsing job.
- **STEP assemblies arrive unplaced.** `next_assembly_usage_occurrence` and the
  `representation_relationship_with_transformation` that goes with it are not
  followed, so every part of a multi-part file arrives in its own coordinates.
  The reader warns when it sees them rather than putting everything at the
  origin and letting the user find out. Applying them means walking the product
  structure, which is a bounded piece of work — but one with no sample here to
  verify against, and untested assembly maths is worse than a warning.
- **STEP PMI, tolerances and construction history are not read.** They are what
  AP242 adds over AP203, and the geometry does not depend on them.
  `BrepSolid::Extras` and `BrepFace::Extras` exist for them; nothing fills them
  yet beyond the source instance id.
- **A degenerate STEP edge without a pcurve is skipped.** A `vertex_loop` — a
  cone's apex, a sphere's pole — bounds nothing that can be walked in parameter
  space, so the face is bounded by its other loops and the parameterisation near
  the pole is the tessellator's guess. Files that carry the pcurve are fine; the
  hand-authored samples do not exercise it.
- **The STEP writer emits no pcurves.** It relies on the reader inverting the
  surface, which is exact for the analytic types and iterative for NURBS. That
  round-trips through this reader — the test proves the meshed volume is
  identical — but a receiving system with a stricter trimming model may prefer
  them, and a face on a wildly non-monotonic NURBS patch is where projection
  would be worst.
- **B-rep tessellation is bounded, not optimal.** The face mesher samples the
  trimming loops to the chord tolerance, decimates what the tolerance does not
  need, ear-clips the region with its holes bridged in, seeds a grid of interior
  points sized from the surface's own curvature, and then refines by red-green
  splitting whatever a uniform grid missed. A cylinder of radius 5 and height 10
  comes out at 96 / 384 / 1534 triangles for tolerances of 0.1 / 0.01 / 0.001,
  which is within about 1.5x of the ideal strip; a box comes out at exactly 12.
  Where it stops short: the refinement has a depth limit, and a face that hits
  it says so through the warning callback rather than silently under-meshing.
- **Degenerate edges without a pcurve are skipped.** A sphere's pole or a cone's
  apex has no extent in space, and reconstructing its span in parameter space
  needs the pcurve the file may not have carried. The loop closes anyway
  because the seam is unwrapped, but the parameterisation near the pole is the
  tessellator's guess rather than the file's statement.
- **The B-rep `Extras` maps are inert.** PMI, product ids and assembly paths
  are carried as text so nothing is lost, and nothing reads them.
- **`ModelDocument::FlattenTransforms` does not bake solids.** It reduces the
  graph to one node per mesh, so a solid that has not been tessellated is left
  behind. Call `TessellateBreps` first; transforming a NURBS control net is
  well-defined but nothing needs it yet.

### What the sample files confirmed

The aircraft eventually arrived in three formats — 3DS, OBJ and DXF — of the
same Blender scene, which is as close to a controlled experiment as file-format
work gets. All three now agree: the DXF and 3DS land on identical bounds
without any conversion (both Z-up), the OBJ lands on them after
`ConvertUpAxis`, the DXF and OBJ both hold 8 110 quads, and triangulating
either gives the 3DS's 16 220 triangles exactly.



The aircraft came as **both** a 3DS and an OBJ export of the same Blender
scene, which turned out to be the strongest check available: the OBJ is Y-up
with 8 110 quads, the 3DS Z-up with 16 220 triangles, and two independently
written readers must land on the same model.

They do. `ConvertUpAxis(ZUp)` on the OBJ document reproduces the 3DS bounds to
four decimals on every axis, both readers find 12 227 vertices, and the OBJ's
quad count is exactly half the 3DS's triangle count. That exercises the
up-axis conversion, the n-gon representation and the vertex-splitting rule
against ground truth rather than against itself.

Two OBJ-specific findings worth recording:

- **The three index streams really do diverge.** The sample has 11 749
  positions against 12 227 texture coordinates — a position used with two
  different UVs must become two document vertices, and the resolved count
  (12 227) is what matches the 3DS. A reader that assumed parallel streams
  would produce a subtly wrong mesh that still looks plausible.
- **A missing `.mtl` is the normal case, not an error.** The sample references
  `E 45 Aircraft_obj.mtl`, which does not travel with the model. The reader
  keeps the `usemtl` names as placeholder materials and reports the missing
  library, so the material *assignment* survives even when the definitions do
  not. The reference also has spaces in it, which is why the library and map
  filenames are parsed as the remainder of the line rather than as a token.

### The DAE that is not the same aircraft

Worth recording because it looks like a reader bug and is not. The `.dae`
export differs from the OBJ, 3DS and DXF ones in three ways, all properties of
the file:

- it holds **half the hull** — X spans `[-0.9732, 0]` where the others span
  `[-0.9732, 0.9732]`, a mirror modifier that was never applied;
- it is **far lower-poly** — 1 995 triangles against the 3DS's 16 220;
- its two meshes are **displaced from each other** by an armature's node
  transforms, so the canopy sits well clear of the hull.

The reader's world bounds were checked against an independent walk of the same
node chain and agree exactly, so the composition is right and the file is what
it is. `Tests/ModelColladaTest.cpp` asserts these differences deliberately, so
that a later change cannot quietly "fix" the reader into matching the other
exports.

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

- **Application-native scene files** — `.blend`, `.max`, `.ma`/`.mb`, `.c4d`.
  See §2.6: the geometry an artist sees is produced by the application's
  evaluation and is not in the file. `.blend` is recognised and described so a
  user gets an answer instead of a failure, but never imported as geometry.
- ~~**B-rep and parametric solids**~~ — **no longer a non-goal.** See §2.5:
  `ModelDocument::Brep` holds trimmed NURBS and analytic surfaces with their
  topology, and tessellation became an operation the caller asks for rather
  than a loss a reader imposes. What remains out of scope within B-rep is PMI,
  CSG history and assembly constraints — carried as `Extras` text, interpreted
  by nothing.
- **A renderer.** `ModelDocument` is an exchange and editing structure.
  Rendering stays with `UltraCanvasSTLElement` / `UltraCanvasGLSurface`, which
  consume a flattened mesh.
- **A scene-authoring API** — physics, constraints, LOD, materials graphs.
  What a file format carries is the boundary.
- **Replacing `Mesh3D` in one step.** The bridge exists so formats migrate
  individually; a flag-day rewrite of the STL viewer, the Filer thumbnails and
  the 3D charts is exactly the kind of change the versioning investigation
  wants staged across `0.6.x`.
