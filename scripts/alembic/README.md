# Alembic repair tools

Two scripts that read and rewrite an Alembic `.abc` archive, and the record of
what they were used for.

| File | What it is |
|---|---|
| `ogawa.py` | The Ogawa container: read an archive, verify its sample keys, write it back with chosen blocks replaced. `python3 scripts/alembic/ogawa.py dump <file.abc>` prints the tree. |
| `replace_mesh.py` | Replaces one polygon mesh's geometry in an archive with a mesh evaluated from a `.blend`. |

## Why these exist

Nothing available writes Alembic. The framework's writers cover 3DS, OBJ, PLY,
STEP, COLLADA and X3D; Ubuntu ships no Alembic library; and Debian's Blender is
built without the exporter (`bpy.ops.wm.alembic_export` exists as a name but
fails its poll). So when `media/3D/Alembic/E-45-Aircraft.abc` turned out to be
half an aeroplane, it could not be re-exported the way the `.fbx` was. The
archive had to be rewritten around a corrected mesh instead, and a repair
nobody can reproduce is not much of a repair — hence these.

`ogawa.py` is the Python counterpart of
`UltraCanvas/Plugins/Models/Alembic/UltraCanvasOgawaFile.cpp` and reads the
format the same way. It interprets no AbcGeom: what a compound named `.geom`
means is `replace_mesh.py`'s problem, the same split the C++ side has.

## How the shipped sample was repaired

`media/3D/Alembic/E-45-Aircraft.abc` is the output of exactly this, run on the
untouched 2017 export kept at `Tests/data/3D/Alembic/E-45-Aircraft.abc`:

```sh
cp Tests/data/3D/Alembic/E-45-Aircraft.abc media/3D/Alembic/E-45-Aircraft.abc
python3 scripts/alembic/replace_mesh.py \
    --archive media/3D/Alembic/E-45-Aircraft.abc \
    --blend   media/3D/Blend/E-45-Aircraft.blend \
    --object  Cube.021 --shape Cube_021Shape --subdivision 1
```

The hull mesh had been exported without applying its Mirror modifier, so it
stopped dead at X=0 with 937 faces of the unevaluated cage; the command above
replaces it with the 7366-face mesh Blender evaluates from the same `.blend`
with the whole stack applied — the count the OBJ and FBX exports of that scene
carry. Everything else in the archive is the bytes Blender wrote in 2017.

`ModelAlembicTest` reads both files: the fixture for the assertions that pin
the original's half hull, and this one for `TestTheDemoCopy()`, which holds it
to the OBJ export's 8110 faces and to symmetry about X.

## What to expect when re-running it

- **Not byte-identical.** Blender's mesh evaluation is not bit-deterministic
  between runs: about 1% of the corner normals come back differing in the last
  bit or two, up to 1.2e-7. Positions, indices and UVs do reproduce exactly.
  Compare meshes, not checksums.
- **Sample keys are real.** Every block written gets Alembic's own key —
  MurmurHash3 x64 128, seed 0, over the payload. `ogawa.VerifyKeys` recomputes
  the key of every sample in an archive, and reports 34 of 34 matching on the
  untouched Blender export, which is what says the algorithm is the right one.
  `replace_mesh.py` runs that check on its own output before it finishes.
- **Structure is preserved, not rebuilt.** Objects, properties, metadata
  strings, time sampling, the string pool and even blocks the source shared all
  survive; only the named sample blocks change. Rewriting with no overrides
  reproduces the input's content exactly.

## Limits

- The first time sample only. An animated archive keeps its other samples but
  they are not touched, so a replaced mesh would disagree with them.
- Rank-1 arrays, which is what AbcGeom's geometry properties are.
- `.childBnds` composes translation down the transform chain, which is all
  these archives use above a shape. A rotated or scaled parent would need the
  full matrix — the tool would have to grow, and it says so where it computes.
- Alembic's HDF5 backend is a different format and is not read.
