#!/usr/bin/env python3
"""Replace one polygon mesh inside an Alembic archive with a mesh from a .blend.

This is the tool that repaired `media/3D/Alembic/E-45-Aircraft.abc`. That file
is a 2017 Blender export whose hull was written without applying the Mirror
modifier, so it held half an aeroplane - the same defect the `.dae`, `.x`,
`.fbx` and `.ms3d` exports of the same scene had. The other four could be
mirrored or re-exported. Alembic could not: nothing available writes it, so
the archive was rewritten around the mesh Blender evaluates today.

    python3 scripts/alembic/replace_mesh.py \\
        --archive media/3D/Alembic/E-45-Aircraft.abc \\
        --blend   media/3D/Blend/E-45-Aircraft.blend \\
        --object  Cube.021 --shape Cube_021Shape --subdivision 1

What it changes in the archive: the named shape's `P`, `.faceIndices`,
`.faceCounts`, `N`, `uv/.vals`, `uv/.indices` and `.selfBnds`, its face set's
`.faces`, and the archive's `.childBnds`. What it leaves alone: every other
object, every property, the metadata strings, the time sampling, the string
pool - the bytes Blender wrote, still in place. Each replaced sample gets a
real Alembic sample key (see `ogawa.py`).

Two conventions this has to get right, because both are silent corruption:

  * **Up axis.** Blender is Z-up; Blender's own Alembic exporter writes Y-up,
    which is what the existing samples in the file are in. Positions and
    normals are converted (x, y, z) -> (x, z, -y) to match them.
  * **Winding.** Alembic winds a face's indices the opposite way round from the
    outward-normal convention Blender and OBJ use, and
    `UltraCanvasAlembicConverter` reverses every face on import for that
    reason. Corners are therefore emitted in reverse loop order, and the tool
    refuses to write a mesh that does not end up disagreeing with its own
    stored normals - which is what "wound the Alembic way" looks like from
    outside.

It needs Blender on PATH to read the `.blend` (4.0 was used; any version whose
`bpy` evaluates a depsgraph will do). It re-runs itself inside Blender to do
that, writes the evaluated mesh to a temporary file, and rewrites the archive
in a plain Python process afterwards.
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ogawa  # noqa: E402  (after sys.path, so the tool runs from any directory)


# ===== THE MESH, AS BLENDER EVALUATES IT =====
#
# This half runs inside Blender: `blender -b <file.blend> --python <this file>`.
# It writes a flat little-endian file the other half reads back - vertices,
# face sizes, corner indices, corner normals and corner UVs, already converted
# to Alembic's frame and winding.

def ExtractUnderBlender(objectName, subdivision, output):
    import bpy

    obj = bpy.data.objects.get(objectName)
    if obj is None:
        names = ", ".join(sorted(o.name for o in bpy.data.objects if o.type == "MESH"))
        raise SystemExit(f"no object named {objectName!r} in the .blend; meshes are: {names}")

    for modifier in obj.modifiers:
        if modifier.type == "SUBSURF" and subdivision >= 0:
            # The level decides how dense the result is; 1 is what the OBJ and
            # FBX exports of this scene carry.
            modifier.levels = subdivision
        print(f"  modifier {modifier.type}"
              + (f" levels={modifier.levels}" if modifier.type == "SUBSURF" else ""))

    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh()
    try:
        mesh.calc_normals_split()
    except AttributeError:
        pass                        # Blender 4.1+ computes them itself
    uvLayer = mesh.uv_layers.active

    def ToAlembic(v):
        """Blender's Z-up into the Y-up frame its Alembic exporter writes."""
        return (v[0], v[2], -v[1])

    vertices = [ToAlembic(v.co) for v in mesh.vertices]
    counts, corners, normals, uvs = [], [], [], []
    for polygon in mesh.polygons:
        loops = list(polygon.loop_indices)
        counts.append(len(loops))
        for loop in reversed(loops):        # Alembic's winding
            corners.append(mesh.loops[loop].vertex_index)
            normals.append(ToAlembic(mesh.loops[loop].normal))
            uvs.append(tuple(uvLayer.data[loop].uv) if uvLayer else (0.0, 0.0))

    with open(output, "wb") as handle:
        handle.write(struct.pack("<I", len(vertices)))
        for v in vertices:
            handle.write(struct.pack("<3f", *v))
        handle.write(struct.pack("<I", len(counts)))
        handle.write(struct.pack("<%di" % len(counts), *counts))
        handle.write(struct.pack("<I", len(corners)))
        handle.write(struct.pack("<%di" % len(corners), *corners))
        for n in normals:
            handle.write(struct.pack("<3f", *n))
        for uv in uvs:
            handle.write(struct.pack("<2f", *uv))

    print(f"  evaluated: {len(vertices)} vertices, {len(counts)} faces, {len(corners)} corners")
    evaluated.to_mesh_clear()


def ReadExtract(path):
    with open(path, "rb") as handle:
        raw = handle.read()
    at = 0

    def Take(form, count):
        nonlocal at
        size = struct.calcsize(form % count)
        values = struct.unpack_from(form % count, raw, at)
        at += size
        return list(values)

    vertexCount = Take("<%dI", 1)[0]
    positions = Take("<%df", vertexCount * 3)
    faceCount = Take("<%dI", 1)[0]
    counts = Take("<%di", faceCount)
    cornerCount = Take("<%dI", 1)[0]
    corners = Take("<%di", cornerCount)
    normals = Take("<%df", cornerCount * 3)
    uvs = Take("<%df", cornerCount * 2)
    if at != len(raw):
        raise ValueError("the extracted mesh file is longer than its header says")
    return positions, counts, corners, normals, uvs


# ===== CHECKING THE MESH BEFORE IT GOES IN =====

def NewellNormal(points):
    nx = ny = nz = 0.0
    for i, a in enumerate(points):
        b = points[(i + 1) % len(points)]
        nx += (a[1] - b[1]) * (a[2] + b[2])
        ny += (a[2] - b[2]) * (a[0] + b[0])
        nz += (a[0] - b[0]) * (a[1] + b[1])
    return nx, ny, nz


def CheckWinding(positions, counts, corners, normals):
    """How many faces disagree with their own stored normal.

    Nearly all of them should: that is what Alembic's winding means, and it is
    the assertion `ModelAlembicTest` makes about the file from the other side.
    """
    agree = disagree = 0
    at = 0
    for count in counts:
        face = corners[at:at + count]
        points = [(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]) for i in face]
        n = NewellNormal(points)
        s = (normals[at * 3], normals[at * 3 + 1], normals[at * 3 + 2])
        dot = n[0] * s[0] + n[1] * s[1] + n[2] * s[2]
        if dot > 0:
            agree += 1
        elif dot < 0:
            disagree += 1
        at += count
    return agree, disagree


# ===== THE REWRITE =====

def FindShape(archive, shapeName):
    """The (shape object, its .geom compound) for a named AbcGeom_PolyMesh."""
    found = []

    def Visit(obj, _depth):
        if obj.name == shapeName and obj.Schema() == "AbcGeom_PolyMesh_v1":
            found.append(obj)

    ogawa.Walk(archive.Top(), Visit)
    if not found:
        shapes = []
        ogawa.Walk(archive.Top(), lambda o, d: shapes.append(o.name)
                   if o.Schema() == "AbcGeom_PolyMesh_v1" else None)
        raise SystemExit(f"no polygon mesh named {shapeName!r} in the archive; it holds: "
                         f"{', '.join(shapes)}")
    shape = found[0]
    return shape, shape.Properties().Child(".geom")


def LocalBox(geom):
    """A mesh's own bounds, from the positions the archive holds for it."""
    positions = geom.Values("P")
    return [min(positions[0::3]), min(positions[1::3]), min(positions[2::3]),
            max(positions[0::3]), max(positions[1::3]), max(positions[2::3])]


def ArchiveBox(archive, replacedShape, replacedBox):
    """The box over every mesh in the archive, with one shape's box replaced.

    Only translation is composed, which is all these archives use above a
    shape; a rotated parent would need the full matrix and this would have to
    grow. The value feeds `.childBnds`, which no reader here depends on - it is
    recomputed because leaving a stale one behind would be its own small lie.
    """
    offsets = [[0.0, 0.0, 0.0]]     # the translation accumulated down to depth i
    box = None

    def Visit(obj, depth):
        nonlocal box
        inherited = offsets[depth]
        own = list(inherited)
        properties = obj.Properties()
        xform = properties.Child(".xform") if properties else None
        if xform is not None and xform.Find(".vals") >= 0:
            values = xform.Values(".vals")
            if len(values) >= 16:
                # Alembic's matrix is row-major and row-vector: the last row
                # is the translation.
                for axis in range(3):
                    own[axis] += values[12 + axis]
        del offsets[depth + 1:]
        offsets.append(own)

        if obj.Schema() != "AbcGeom_PolyMesh_v1":
            return
        local = replacedBox if obj.name == replacedShape \
            else LocalBox(properties.Child(".geom"))
        world = [local[i] + own[i % 3] for i in range(6)]
        box = world if box is None else \
            [min(box[i], world[i]) for i in range(3)] + \
            [max(box[i], world[i]) for i in range(3, 6)]

    ogawa.Walk(archive.Top(), Visit)
    return box


def Repair(archivePath, extractPath, shapeName, outputPath):
    positions, counts, corners, normals, uvs = ReadExtract(extractPath)
    cornerCount = len(corners)
    print(f"replacement: {len(positions) // 3} vertices, {len(counts)} faces, "
          f"{cornerCount} corners")

    agree, disagree = CheckWinding(positions, counts, corners, normals)
    if agree > disagree:
        raise SystemExit(f"the mesh is wound the wrong way for Alembic: {agree} faces agree "
                         f"with their own normals and {disagree} disagree, where almost all "
                         f"should disagree. Emitting corners in reverse loop order is what "
                         f"makes that true; something upstream stopped doing it.")
    print(f"  winding: {disagree} of {len(counts)} faces disagree with their stored normals, "
          f"as Alembic's convention requires")

    # UVs: the distinct values, plus one index per corner, the way the file
    # already stores them.
    uvValues, uvIndices, seen = [], [], {}
    for i in range(cornerCount):
        key = struct.pack("<2f", uvs[i * 2], uvs[i * 2 + 1])
        index = seen.get(key)
        if index is None:
            index = len(uvValues)
            seen[key] = index
            uvValues.append(key)
        uvIndices.append(index)
    print(f"  {len(uvValues)} distinct UVs over {cornerCount} corners")

    archive = ogawa.Archive(archivePath)
    shape, geom = FindShape(archive, shapeName)
    uv = geom.Child("uv")

    box = [min(positions[0::3]), min(positions[1::3]), min(positions[2::3]),
           max(positions[0::3]), max(positions[1::3]), max(positions[2::3])]
    print(f"  bounds: X[{box[0]:.4f}, {box[3]:.4f}] Y[{box[1]:.4f}, {box[4]:.4f}] "
          f"Z[{box[2]:.4f}, {box[5]:.4f}]")

    overrides = {
        geom.SampleSlot("P"):
            ogawa.SampleBlock(struct.pack("<%df" % len(positions), *positions)),
        geom.SampleSlot(".faceIndices"):
            ogawa.SampleBlock(struct.pack("<%di" % cornerCount, *corners)),
        geom.SampleSlot(".faceCounts"):
            ogawa.SampleBlock(struct.pack("<%di" % len(counts), *counts)),
        geom.SampleSlot("N"):
            ogawa.SampleBlock(struct.pack("<%df" % len(normals), *normals)),
        geom.SampleSlot(".selfBnds"):
            ogawa.SampleBlock(struct.pack("<6d", *box)),
        uv.SampleSlot(".vals"): ogawa.SampleBlock(b"".join(uvValues)),
        uv.SampleSlot(".indices"):
            ogawa.SampleBlock(struct.pack("<%dI" % cornerCount, *uvIndices)),
    }

    # A face set names a subset of the mesh's faces, so one that covered the
    # whole mesh has to be renumbered to still cover it.
    for index in range(len(shape.children)):
        child = shape.Child(index)
        if not child or child.Schema() != "AbcGeom_FaceSet_v1":
            continue
        faceSet = child.Properties().Child(".faceset")
        existing = faceSet.Values(".faces")
        if len(existing) != len(geom.Values(".faceCounts")):
            print(f"  face set {child.name!r} covers {len(existing)} of the old faces, not all "
                  f"of them - left alone, and it now indexes a mesh it was not written for")
            continue
        overrides[faceSet.SampleSlot(".faces")] = \
            ogawa.SampleBlock(struct.pack("<%di" % len(counts), *range(len(counts))))
        print(f"  face set {child.name!r} renumbered over {len(counts)} faces")

    # The archive's own box, over every mesh it holds - this one from the new
    # geometry, the rest as they are.
    top = archive.Top()
    childBox = ArchiveBox(archive, shapeName, box)

    if childBox and top.Properties() and top.Properties().Find(".childBnds") >= 0:
        overrides[top.Properties().SampleSlot(".childBnds")] = \
            ogawa.SampleBlock(struct.pack("<6d", *childBox))
        print(f"  archive bounds: X[{childBox[0]:.4f}, {childBox[3]:.4f}] "
              f"Y[{childBox[1]:.4f}, {childBox[4]:.4f}] Z[{childBox[2]:.4f}, {childBox[5]:.4f}]")

    data = ogawa.Rewriter(archive, overrides).Run()
    with open(outputPath, "wb") as handle:
        handle.write(data)
    print(f"wrote {outputPath}: {len(data)} bytes, was {len(archive.bytes)}")

    written = ogawa.Archive(outputPath)
    matched, total = ogawa.VerifyKeys(written)
    if matched != total:
        raise SystemExit(f"{total - matched} sample keys in the result do not match their "
                         f"payload - the archive would lie about its own contents")
    print(f"  every one of its {total} sample keys matches its payload")


# ===== ENTRY POINTS =====

def Main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--archive", required=True, help="the .abc to repair")
    parser.add_argument("--blend", required=True, help="the .blend the mesh comes from")
    parser.add_argument("--object", required=True, help="the object in the .blend, e.g. Cube.021")
    parser.add_argument("--shape", required=True,
                        help="the AbcGeom_PolyMesh in the archive, e.g. Cube_021Shape")
    parser.add_argument("--subdivision", type=int, default=-1,
                        help="Subdivision Surface viewport level to evaluate at "
                             "(default: whatever the .blend says)")
    parser.add_argument("--blender", default="blender", help="the Blender executable")
    parser.add_argument("--out", help="where to write the result (default: in place)")
    arguments = parser.parse_args()

    output = arguments.out or arguments.archive
    with tempfile.TemporaryDirectory() as workspace:
        extract = os.path.join(workspace, "mesh.bin")
        print(f"evaluating {arguments.object} from {arguments.blend}")
        result = subprocess.run(
                [arguments.blender, "-b", arguments.blend, "--factory-startup", "-noaudio",
                 "--python", os.path.abspath(__file__), "--",
                 "--extract", arguments.object, str(arguments.subdivision), extract],
                capture_output=True, text=True)
        for line in result.stdout.splitlines():
            if line.startswith("  "):
                print(line)
        if result.returncode != 0 or not os.path.exists(extract):
            sys.stderr.write(result.stdout + result.stderr)
            raise SystemExit("Blender could not evaluate the mesh")
        Repair(arguments.archive, extract, arguments.shape, output)


if __name__ == "__main__":
    # Inside Blender the arguments arrive after a bare "--".
    if "--extract" in sys.argv:
        at = sys.argv.index("--extract")
        ExtractUnderBlender(sys.argv[at + 1], int(sys.argv[at + 2]), sys.argv[at + 3])
    else:
        Main()
