#!/usr/bin/env python3
"""Remove the MethodParameters attribute from Java class files, in place.

Why: JDK 21's javac writes MethodParameters with unnamed entries (the
mandated and synthetic parameters of an anonymous class's constructor), and
the d8 of Android build-tools 34 (R8 8.2) stops on them with a
NullPointerException - so scripts/android-package-apk.sh could not dex
UltraCanvasActivity on a machine with JDK 21 and build-tools 34, whatever
the debug options. The attribute only names parameters for reflection, which
nothing in an UltraCanvas app reads; without it d8 dexes the classes.

Usage: strip_method_parameters.py CLASS_FILE...
Prints how many attributes it removed; a file it cannot parse is left alone
and named.
"""

import struct
import sys

# Constant pool entry sizes after the tag byte; Utf8 is variable.
FIXED = {3: 4, 4: 4, 5: 8, 6: 8, 7: 2, 8: 2, 9: 4, 10: 4, 11: 4, 12: 4,
         15: 3, 16: 2, 17: 4, 18: 4, 19: 2, 20: 2}


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, n):
        chunk = self.data[self.pos:self.pos + n]
        if len(chunk) != n:
            raise ValueError("truncated class file")
        self.pos += n
        return chunk

    def u2(self):
        return struct.unpack(">H", self.take(2))[0]

    def u4(self):
        return struct.unpack(">I", self.take(4))[0]


def strip(data):
    """The class file without MethodParameters, and how many were removed."""
    r = Reader(data)
    if r.u4() != 0xCAFEBABE:
        raise ValueError("not a class file")
    r.take(4)                                   # minor, major
    count = r.u2()
    utf8 = {}
    index = 1
    while index < count:
        tag = r.take(1)[0]
        if tag == 1:
            length = r.u2()
            utf8[index] = r.take(length)
        elif tag in FIXED:
            r.take(FIXED[tag])
        else:
            raise ValueError(f"unknown constant pool tag {tag}")
        index += 2 if tag in (5, 6) else 1
    out = bytearray(data[:r.pos])
    removed = 0

    def copy(n):
        out.extend(r.take(n))

    def attributes(drop):
        nonlocal removed
        n = r.u2()
        kept = []
        for _ in range(n):
            name = r.u2()
            length = r.u4()
            body = r.take(length)
            if drop and utf8.get(name) == b"MethodParameters":
                removed += 1
                continue
            kept.append(struct.pack(">HI", name, length) + body)
        out.extend(struct.pack(">H", len(kept)))
        for k in kept:
            out.extend(k)

    copy(6)                                     # access, this, super
    interfaces = r.u2()
    out.extend(struct.pack(">H", interfaces))
    copy(2 * interfaces)
    for drop in (False, True):                  # fields, then methods
        members = r.u2()
        out.extend(struct.pack(">H", members))
        for _ in range(members):
            copy(6)                             # access, name, descriptor
            attributes(drop)
    attributes(False)                           # the class's own
    if r.pos != len(data):
        raise ValueError("trailing bytes")
    return bytes(out), removed


def main(paths):
    total = 0
    for path in paths:
        try:
            with open(path, "rb") as f:
                data = f.read()
            stripped, removed = strip(data)
        except (OSError, ValueError) as error:
            print(f"strip_method_parameters: {path}: left as it is ({error})", file=sys.stderr)
            continue
        if removed:
            with open(path, "wb") as f:
                f.write(stripped)
            total += removed
    print(f"strip_method_parameters: {total} attribute(s) removed from {len(paths)} file(s)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
