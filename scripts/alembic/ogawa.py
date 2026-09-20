"""Alembic's Ogawa container: read it, and write it back with blocks replaced.

This is the Python counterpart of
`UltraCanvas/Plugins/Models/Alembic/UltraCanvasOgawaFile.cpp`, and it reads the
format the same way: a flat file of groups and data blocks addressed by
absolute offset, where a group is a count followed by that many 64-bit child
pointers and the pointer's top bit says whether the child is another group or a
block of bytes. Names, types and sample counts live in a header blob that is
the last child of every group.

It exists because nothing available writes Alembic. The framework's writers
cover 3DS, OBJ, PLY, STEP, COLLADA and X3D, Ubuntu ships no Alembic library,
and Debian's Blender is built without the exporter - so when a shipped `.abc`
needs fixing, the only honest option is to rewrite the archive it already is.
`Rewriter` does exactly that and no more: it copies the tree out block for
block, preserving names, types, metadata, time sampling and even which blocks
are shared, and changes only the bytes of the blocks it is told to change.
Feeding it no overrides reproduces its input byte for byte in content, which is
the test that it is copying rather than inventing.

`SampleBlock` builds the block an array or scalar property expects: Alembic's
128-bit sample key followed by the payload. The key is MurmurHash3 x64 128 with
seed 0 over the payload - not a guess: `VerifyKeys` recomputes the key of every
sample already in an archive and reports whether they match, and they do for
every array in the samples this repository ships.

What this does NOT do, on purpose: interpret AbcGeom. Whether a compound named
".geom" is a polygon mesh is `replace_mesh.py`'s problem, exactly as the split
between UltraCanvasOgawaFile.cpp and UltraCanvasAlembicConverter.cpp has it.

Read `python3 scripts/alembic/ogawa.py dump <file.abc>` for the tree.
"""

import struct
import sys

DATA_FLAG = 1 << 63
SAMPLE_KEY_SIZE = 16

# Alembic's plain-old-data types, in the order the format numbers them.
POD_NAMES = ["bool", "uint8", "int8", "uint16", "int16", "uint32", "int32",
             "uint64", "int64", "float16", "float32", "float64", "string", "wstring"]
POD_SIZE = {0: 1, 1: 1, 2: 1, 3: 2, 4: 2, 5: 4, 6: 4, 7: 8, 8: 8, 9: 2, 10: 4, 11: 8}
POD_FORMAT = {5: "<%dI", 6: "<%di", 7: "<%dQ", 8: "<%dq", 10: "<%df", 11: "<%dd"}

COMPOUND, SCALAR, ARRAY = 0, 1, 2


# ===== THE HASH ALEMBIC KEYS SAMPLES WITH =====

_MASK = 0xFFFFFFFFFFFFFFFF


def _rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & _MASK


def _fmix(k):
    k &= _MASK
    k ^= k >> 33
    k = (k * 0xff51afd7ed558ccd) & _MASK
    k ^= k >> 33
    k = (k * 0xc4ceb9fe1a85ec53) & _MASK
    k ^= k >> 33
    return k


def MurmurHash3_x64_128(data, seed=0):
    """The digest Alembic stores in front of every sample, little-endian."""
    length = len(data)
    h1 = h2 = seed & _MASK
    c1, c2 = 0x87c37b91114253d5, 0x4cf5ad432745937f

    for i in range(length // 16):
        k1, k2 = struct.unpack_from("<QQ", data, i * 16)
        k1 = (_rotl((k1 * c1) & _MASK, 31) * c2) & _MASK
        h1 ^= k1
        h1 = ((_rotl(h1, 27) + h2) * 5 + 0x52dce729) & _MASK
        k2 = (_rotl((k2 * c2) & _MASK, 33) * c1) & _MASK
        h2 ^= k2
        h2 = ((_rotl(h2, 31) + h1) * 5 + 0x38495ab5) & _MASK

    tail = data[length // 16 * 16:]
    k1 = k2 = 0
    for i in range(len(tail) - 1, 7, -1):
        k2 ^= tail[i] << (8 * (i - 8))
    if len(tail) > 8:
        k2 = (_rotl((k2 * c2) & _MASK, 33) * c1) & _MASK
        h2 ^= k2
    for i in range(min(len(tail), 8) - 1, -1, -1):
        k1 ^= tail[i] << (8 * i)
    if tail:
        k1 = (_rotl((k1 * c1) & _MASK, 31) * c2) & _MASK
        h1 ^= k1

    h1 ^= length
    h2 ^= length
    h1 = (h1 + h2) & _MASK
    h2 = (h2 + h1) & _MASK
    h1, h2 = _fmix(h1), _fmix(h2)
    h1 = (h1 + h2) & _MASK
    h2 = (h2 + h1) & _MASK
    return struct.pack("<QQ", h1, h2)


def SampleBlock(payload):
    """One sample as Ogawa stores it: Alembic's 128-bit key, then the data."""
    return MurmurHash3_x64_128(payload, 0) + payload


# ===== THE CONTAINER =====

class Archive:
    """An Ogawa file, parsed lazily by offset - nothing is copied until asked."""

    def __init__(self, path):
        with open(path, "rb") as handle:
            self.bytes = handle.read()
        if self.bytes[:5] != b"Ogawa":
            raise ValueError(f"{path}: not an Ogawa archive (Alembic's HDF5 backend is a "
                             f"different format and is not read here)")
        self.path = path
        self.root = struct.unpack_from("<Q", self.bytes, 8)[0]
        self.pool = []

        count = self.ChildCount(self.root)
        version = self.ChildData(self.root, 1)
        self.fileVersion = struct.unpack_from("<I", version, 0)[0] if len(version) >= 4 else 0
        self.metadata = self.ChildData(self.root, 3).decode("utf8", "replace") if count > 3 else ""
        if count > 5:
            # The string pool every metadata reference indexes into: length-
            # prefixed strings back to back.
            blob = self.ChildData(self.root, 5)
            at = 0
            while at < len(blob):
                size = blob[at]
                at += 1
                self.pool.append(blob[at:at + size].decode("utf8", "replace"))
                at += size

    # --- groups and blocks ---

    def ChildCount(self, group):
        if group == 0 or group + 8 > len(self.bytes):
            return 0
        count = struct.unpack_from("<Q", self.bytes, group)[0]
        # A corrupt pointer read as a count would ask for terabytes of
        # children; the file itself bounds how many there can be.
        return 0 if count > (len(self.bytes) - group) // 8 else count

    def ChildPointer(self, group, index):
        at = group + 8 + index * 8
        return struct.unpack_from("<Q", self.bytes, at)[0] if at + 8 <= len(self.bytes) else 0

    def ChildGroup(self, group, index):
        pointer = self.ChildPointer(group, index)
        return 0 if pointer & DATA_FLAG else pointer

    def ChildData(self, group, index):
        pointer = self.ChildPointer(group, index)
        if not pointer & DATA_FLAG:
            return b""
        at = pointer & ~DATA_FLAG
        if at == 0 or at + 8 > len(self.bytes):
            return b""
        size = struct.unpack_from("<Q", self.bytes, at)[0]
        return self.bytes[at + 8:at + 8 + size]

    # --- headers ---

    def MetadataAt(self, index):
        # 0 means "none"; the pool is 1-based.
        return "" if index == 0 or index > len(self.pool) else self.pool[index - 1]

    def ObjectHeaders(self, blob):
        """The names and metadata of one object's children."""
        headers = []
        # The blob ends with two 128-bit hashes Alembic uses to tell whether a
        # subtree changed. They carry nothing a reader needs.
        end = len(blob) - 32 if len(blob) >= 32 else len(blob)
        at = 0
        while at + 4 <= end:
            size = struct.unpack_from("<I", blob, at)[0]
            at += 4
            if size > end - at:
                break
            name = blob[at:at + size].decode("utf8", "replace")
            at += size
            if at >= end:
                headers.append((name, ""))
                break
            index = blob[at]
            at += 1
            if index == 0xff:
                length = struct.unpack_from("<I", blob, at)[0]
                at += 4
                metadata = blob[at:at + length].decode("utf8", "replace")
                at += length
            else:
                metadata = self.MetadataAt(index)
            headers.append((name, metadata))
        return headers

    def PropertyHeaders(self, blob):
        """One compound's property list, from its packed header blob."""
        headers = []
        at, size = 0, len(blob)
        while at + 4 <= size:
            info = struct.unpack_from("<I", blob, at)[0]
            at += 4
            kind = info & 0x3
            pod, extent, samples = 255, 1, 0
            if kind != COMPOUND:
                pod = (info >> 4) & 0xF
                extent = (info >> 12) & 0xFF
                # Two bits choose how wide the sample count is.
                width = [1, 2, 4, 8][(info >> 2) & 0x3]
                samples = int.from_bytes(blob[at:at + width], "little")
                at += width
                if (info >> 9) & 1:      # the range of samples that differ
                    at += width * 2
                if (info >> 8) & 1:      # an explicit time-sampling index
                    at += 4
            if at >= size:
                break
            length = blob[at]
            at += 1
            name = blob[at:at + length].decode("utf8", "replace")
            at += length
            index = (info >> 20) & 0xFFF
            if index == 0xFFF:
                inline = blob[at]
                at += 1
                if inline == 0xff:
                    length = struct.unpack_from("<I", blob, at)[0]
                    at += 4
                    metadata = blob[at:at + length].decode("utf8", "replace")
                    at += length
                else:
                    metadata = self.MetadataAt(inline)
            else:
                metadata = self.MetadataAt(index)
            headers.append(dict(kind=kind, name=name, metadata=metadata,
                                pod=pod, extent=extent, samples=samples))
        return headers

    def Top(self):
        """The archive's top object, whose children are the scene's roots."""
        return Object(self, self.ChildGroup(self.root, 2), "", "")


class Object:
    """A transform, a mesh, a face set: what it *is* comes from its metadata."""

    def __init__(self, archive, group, name, metadata):
        self.archive, self.group = archive, group
        self.name, self.metadata = name, metadata
        # An object group is: its property compound, then its child objects,
        # then one data block naming those children.
        count = archive.ChildCount(group)
        self.children = archive.ObjectHeaders(archive.ChildData(group, count - 1)) if count else []

    def Schema(self):
        schema = MetadataValue(self.metadata, "schema") or \
                 MetadataValue(self.metadata, "schemaObjTitle")
        return schema.split(":")[0]

    def Properties(self):
        count = self.archive.ChildCount(self.group)
        if count == 0 or self.archive.ChildPointer(self.group, 0) & DATA_FLAG:
            return None
        return Compound(self.archive, self.archive.ChildGroup(self.group, 0))

    def Child(self, index):
        # Child object i is group child i + 1: child 0 is the property compound.
        group = self.archive.ChildGroup(self.group, index + 1)
        if group == 0:
            return None
        return Object(self.archive, group, *self.children[index])

    def FindChild(self, name):
        for i, (childName, _) in enumerate(self.children):
            if childName == name:
                return self.Child(i)
        return None


class Compound:
    """An ordered list of properties, each a sub-compound or a stream of samples."""

    def __init__(self, archive, group):
        self.archive, self.group = archive, group
        count = archive.ChildCount(group)
        self.headers = archive.PropertyHeaders(archive.ChildData(group, count - 1)) if count else []

    def Find(self, name):
        for i, header in enumerate(self.headers):
            if header["name"] == name:
                return i
        return -1

    def Child(self, name):
        index = self.Find(name)
        if index < 0 or self.headers[index]["kind"] != COMPOUND:
            return None
        group = self.archive.ChildGroup(self.group, index)
        return Compound(self.archive, group) if group else None

    def SampleSlot(self, name, sample=0):
        """Where a property's sample block lives, as (group offset, child index).

        This is what `Rewriter` overrides: the address of one block, not a copy
        of it.
        """
        index = self.Find(name)
        if index < 0:
            raise KeyError(name)
        return (self.archive.ChildGroup(self.group, index), sample)

    def Sample(self, name, sample=0):
        """One sample's payload, with Alembic's 16-byte key already stripped."""
        group, slot = self.SampleSlot(name, sample)
        if group == 0:
            return b""
        block = self.archive.ChildData(group, slot)
        return block[SAMPLE_KEY_SIZE:] if len(block) >= SAMPLE_KEY_SIZE else b""

    def Values(self, name, sample=0):
        """One sample reinterpreted as its declared type."""
        header = self.headers[self.Find(name)]
        raw = self.Sample(name, sample)
        form = POD_FORMAT.get(header["pod"])
        if not form:
            return raw
        size = POD_SIZE[header["pod"]]
        count = len(raw) // size
        return list(struct.unpack(form % count, raw[:count * size]))


def MetadataValue(metadata, key):
    """The value of one key in Alembic's "a=b;c=d" metadata string, or ""."""
    for entry in metadata.split(";"):
        name, sep, value = entry.partition("=")
        if sep and name == key:
            return value
    return ""


# ===== WRITING IT BACK =====

class Rewriter:
    """Copies an archive out with chosen sample blocks replaced.

    Ogawa addresses everything by absolute offset, so a block cannot grow in
    place and the whole tree has to be written afresh. Everything but the named
    blocks is copied verbatim, and blocks the source shared stay shared.
    """

    def __init__(self, archive, overrides):
        # overrides: {(group offset, child index): new block bytes}
        self.archive = archive
        self.overrides = dict(overrides)
        self.out = bytearray(archive.bytes[:16])   # header; root offset patched at the end
        self.dataSeen = {}
        self.groupSeen = {}
        self.used = set()

    def _EmitData(self, payload):
        at = len(self.out)
        self.out += struct.pack("<Q", len(payload))
        self.out += payload
        return at

    def _CopyData(self, pointer):
        source = pointer & ~DATA_FLAG
        if source in self.dataSeen:
            return self.dataSeen[source]
        size = struct.unpack_from("<Q", self.archive.bytes, source)[0]
        at = self._EmitData(self.archive.bytes[source + 8:source + 8 + size])
        self.dataSeen[source] = at
        return at

    def _CopyGroup(self, group):
        if group in self.groupSeen:
            return self.groupSeen[group]
        pointers = []
        for index in range(self.archive.ChildCount(group)):
            pointer = self.archive.ChildPointer(group, index)
            if (group, index) in self.overrides:
                self.used.add((group, index))
                pointers.append(DATA_FLAG | self._EmitData(self.overrides[(group, index)]))
            elif pointer in (0, DATA_FLAG):
                pointers.append(pointer)                  # an empty group, or empty data
            elif pointer & DATA_FLAG:
                pointers.append(DATA_FLAG | self._CopyData(pointer))
            else:
                pointers.append(self._CopyGroup(pointer))
        at = len(self.out)
        self.out += struct.pack("<Q", len(pointers))
        for pointer in pointers:
            self.out += struct.pack("<Q", pointer)
        self.groupSeen[group] = at
        return at

    def Run(self):
        root = self._CopyGroup(self.archive.root)
        struct.pack_into("<Q", self.out, 8, root)
        missing = set(self.overrides) - self.used
        if missing:
            raise RuntimeError(f"overrides that no block matched: {sorted(missing)} - the "
                               f"archive's shape is not what the caller assumed")
        return bytes(self.out)


# ===== INSPECTION =====

def Walk(obj, visit, depth=0):
    """Calls visit(object, depth) for every object in the tree, top first."""
    visit(obj, depth)
    for i in range(len(obj.children)):
        child = obj.Child(i)
        if child:
            Walk(child, visit, depth + 1)


def VerifyKeys(archive):
    """Recomputes every sample key in the archive. Returns (matched, total).

    The point of the exercise: if this comes back all-matched on a file written
    by Alembic itself, then the keys this module writes are the keys Alembic
    would have written.
    """
    matched = total = 0

    def CheckCompound(compound):
        nonlocal matched, total
        for header in compound.headers:
            if header["kind"] == COMPOUND:
                child = compound.Child(header["name"])
                if child:
                    CheckCompound(child)
                continue
            group, _ = compound.SampleSlot(header["name"])
            for sample in range(max(header["samples"], 1)):
                block = archive.ChildData(group, sample) if group else b""
                if len(block) <= SAMPLE_KEY_SIZE:
                    continue
                total += 1
                if block[:SAMPLE_KEY_SIZE] == MurmurHash3_x64_128(block[SAMPLE_KEY_SIZE:], 0):
                    matched += 1

    def Visit(obj, _depth):
        properties = obj.Properties()
        if properties:
            CheckCompound(properties)

    Walk(archive.Top(), Visit)
    return matched, total


def _Dump(path):
    archive = Archive(path)
    print(f"{path}: Alembic file version {archive.fileVersion}")
    print(f"  {archive.metadata}")

    def DumpCompound(compound, depth):
        pad = "  " * depth
        for header in compound.headers:
            if header["kind"] == COMPOUND:
                print(f"{pad}{header['name']} (compound)")
                child = compound.Child(header["name"])
                if child:
                    DumpCompound(child, depth + 1)
                continue
            pod = POD_NAMES[header["pod"]] if header["pod"] < len(POD_NAMES) else "?"
            payload = compound.Sample(header["name"])
            count = len(payload) // max(POD_SIZE.get(header["pod"], 1), 1)
            kind = "array" if header["kind"] == ARRAY else "scalar"
            print(f"{pad}{header['name']} ({kind} {pod}[{header['extent']}], "
                  f"{header['samples']} sample(s), {count} values)")

    def Visit(obj, depth):
        pad = "  " * depth
        print(f"{pad}[{obj.Schema() or 'archive'}] {obj.name or '(top)'}")
        properties = obj.Properties()
        if properties:
            DumpCompound(properties, depth + 1)

    Walk(archive.Top(), Visit)
    matched, total = VerifyKeys(archive)
    print(f"sample keys: {matched} of {total} match MurmurHash3 x64 128 of their payload")


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "dump":
        sys.exit("usage: python3 scripts/alembic/ogawa.py dump <file.abc>")
    try:
        _Dump(sys.argv[2])
    except (ValueError, OSError) as reason:
        sys.exit(str(reason))
