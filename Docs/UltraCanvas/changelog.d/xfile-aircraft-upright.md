- **The DirectX .x aircraft on the demo's 3D Model Formats page stood on its
  head, and its glass canopy was missing.** The reader was right; the sample
  was not. Both meshes in the source `.blend` hang from armature bones, and
  Blender's exporter wrote each mesh frame relative to its bone without
  writing the bones. Walked as written, those frames roll the hull 180
  degrees about X and leave the canopy inside it. No reader can recover bones
  that are not in the file, so
  `media/3D/XFile/E-45-Aircraft.x` now carries the placements the `.blend`
  gives the objects: the hull at the armature's origin, and the canopy
  translated onto the nose by (0, -0.101452, 1.525604). Those are the numbers
  Blender's own X3D export of the scene writes. The aircraft now stands the
  same way up as the OBJ, 3DS, DXF, PLY, MS3D, Alembic and FBX samples.
  - The same file was also incomplete in a way the picture did not show.
    When it was mirrored to the whole aeroplane in 0.8.90, its vertices,
    faces and normals were doubled but its UVs (372 for 744 vertices, 3683
    for 7366) and its per-face material indices (93 for 186 faces, 937 for
    1874) were not. The reader warned about the UVs and filled the gap with
    zeros. The mirrored half now has its twin's UVs, which is what Blender's
    Mirror modifier gives it, and every face has its material index.
  - `ModelXFileTest` now also reads the demo copy and asserts that it loads
    without a warning, that the hull is the right way up, and that the canopy
    sits on its nose. Run against the old file, it fails all three. The
    untouched export in `Tests/data/3D` keeps its bone-relative frames, and
    the suite still pins the reader reproducing them.
