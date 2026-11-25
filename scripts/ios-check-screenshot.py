#!/usr/bin/env python3
"""Check that a simulator screenshot shows something drawn.

Usage: scripts/ios-check-screenshot.py <screenshot.png|.bmp>

.github/workflows/ios-build.yml runs it on the screenshot it takes of
UltraCanvasDemo 45 s after launch. An app that runs but never presents a
frame leaves the screen one colour (black: the root view's background), and
"still running" alone would call that a pass - the Android emulator test
fails such a frame for the same reason (scripts/android-emulator-smoke.sh).

A PNG is converted to BMP with macOS's `sips`, so no image library is
needed on the runner; a BMP is read as it is. The check: the most common
colour must cover less than 98% of the sampled pixels, and at least 16
colours must occur. Prints what it found; exits 1 when the frame looks
empty, 2 when the file cannot be read.
"""

import os
import struct
import subprocess
import sys
import tempfile
from collections import Counter


def read_bmp(path):
    """(width, height, sampled RGB tuples) of an uncompressed 24/32-bit BMP."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"BM":
        raise ValueError("not a BMP file")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if bpp not in (24, 32) or compression not in (0, 3):
        raise ValueError(f"unsupported BMP: {bpp} bits per pixel, compression {compression}")
    rows = abs(height)
    bytes_pp = bpp // 8
    stride = (width * bytes_pp + 3) & ~3
    # A grid of about 200 x 200 samples is plenty to tell a drawn frame from
    # an empty one, and keeps a 3x screenshot quick in pure Python.
    step_x = max(1, width // 200)
    step_y = max(1, rows // 200)
    pixels = []
    for y in range(0, rows, step_y):
        row = offset + y * stride
        for x in range(0, width, step_x):
            i = row + x * bytes_pp
            b, g, r = data[i], data[i + 1], data[i + 2]
            pixels.append((r, g, b))
    return width, rows, pixels


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    path = argv[1]
    bmp = path
    tmp = None
    try:
        if not path.lower().endswith(".bmp"):
            tmp = tempfile.NamedTemporaryFile(suffix=".bmp", delete=False)
            tmp.close()
            bmp = tmp.name
            subprocess.run(["sips", "-s", "format", "bmp", path, "--out", bmp],
                           check=True, stdout=subprocess.DEVNULL)
        width, height, pixels = read_bmp(bmp)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"ios-check-screenshot: cannot read {path}: {error}", file=sys.stderr)
        return 2
    finally:
        if tmp is not None and os.path.exists(tmp.name):
            os.unlink(tmp.name)

    counts = Counter(pixels)
    top_colour, top_count = counts.most_common(1)[0]
    share = top_count / len(pixels)
    print(f"ios-check-screenshot: {width}x{height}, {len(pixels)} samples, "
          f"{len(counts)} colours, most common #{top_colour[0]:02x}{top_colour[1]:02x}"
          f"{top_colour[2]:02x} on {share:.1%}")
    if share >= 0.98 or len(counts) < 16:
        print("ios-check-screenshot: the screen looks empty - nothing was drawn", file=sys.stderr)
        return 1
    print("ios-check-screenshot: something was drawn")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
