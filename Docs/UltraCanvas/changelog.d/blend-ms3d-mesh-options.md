- **The .blend and .ms3d readers ignored three more of the mesh import
  options: `TriangulateOnImport`, `WeldTolerance` and
  `GenerateMissingNormals`.** Every other mesh reader applies them, so a
  caller asking the dispatch for triangles got Blender's quads and n-gons
  back. Both readers now finish the way the others do: generate the normals
  a surface is missing, weld, triangulate, then turn to the requested up
  axis.
  - Blender's stored normals are a cache the reader never reads, so it
    always generated its own, even when a caller asked for none. It now
    generates them only when `GenerateMissingNormals` is on, which is the
    default, so a default load returns exactly what it did before.
  - `ModelFormatsPluginTest` holds every reader in the dispatch to the three
    options. Each option's result must equal a plain load with the same
    operation applied by hand, a check only a reader that applies the
    option can pass. All twelve formats pass. On the samples in
    `media/3D` only Blender's triangulation tells the old readers apart:
    MilkShape is triangles with normals already, and neither sample has
    coincident vertices to weld. So `ModelBlendTest` and `ModelMS3DTest`
    also build files that do tell them apart: a `.blend` quad read with
    normals off and triangulated, and an `.ms3d` whose two corners
    0.00001 apart weld into one.
