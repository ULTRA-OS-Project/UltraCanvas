# 3D reader fixtures

These are the E-45 aircraft exports **as they came out of Blender**, kept here
because their defects are the point.

Four of them are incomplete. Both meshes in `media/3D/Blend/E-45-Aircraft.blend`
carry a Mirror modifier about X=0, and these exports were made without applying
modifiers, so the `.dae`, `.x` and binary `.fbx` hold half an aeroplane and the
`.ms3d` holds only the glass canopy. `ModelColladaTest`, `ModelXFileTest`,
`ModelMS3DTest` and `ModelFbxTest` assert exactly that, on purpose:

> This export is NOT the same geometry as the OBJ/3DS/DXF ones, and that is a
> property of the file rather than of the reader. Asserting it stops a later
> change "fixing" the reader to match the others.

The suite also cross-checks them against each other — the MilkShape canopy is
"exactly twice the Alembic canopy's 744 faces", and the two FBX exports are one
scene "on opposite sides of the mirror-modifier split" — so the corpus only
works while every file keeps the shape it has.

The copies under `media/3D/` are a different thing: they are the demo's
showcase assets, and were completed from the same `.blend` with the modifier
applied so the 3D pages show a whole aircraft rather than half of one. Those
are free to change with the demo. **These are not** — edit a file here only
together with the assertions that pin it.
