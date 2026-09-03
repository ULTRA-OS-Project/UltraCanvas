#!/usr/bin/env python3
"""Render media/appicon/Ladybird.png -- the gradient disc plus the black
line-art mark used by the Ladybird port's splash screen and window icon.

The mark is two congruent lens (vesica) rings crossing at +-26 degrees; each
ring is an outer lens with an inner lens punched out of it, so the tips stay
pointed and the strokes read as one continuous knot where they overlap.
"""
from PIL import Image, ImageDraw, ImageChops
import math, sys

SIZE = 512
SS = 4                      # supersample factor for the mask work
W = SIZE * SS

# Corner colours of the disc gradient (top-left, top-right, bottom-left, bottom-right)
TL = (252, 252, 252)
TR = (231, 242, 236)
BL = (201, 166, 255)
BR = (106, 138, 210)

ANGLES = (24.0, -24.0)
OUTER_A, OUTER_B = 0.94, 0.50   # lens half-length / half-height, in disc radii
THICKNESS = 0.085               # stroke weight, in disc radii
POWER = 0.85                    # <0.5 rounds the tips, >0.5 sharpens them
FIT = 0.975                     # keep the tips this far inside the disc


def Gradient(size):
    """Bilinear corner blend, built small and scaled up so it stays smooth."""
    n = 64
    g = Image.new("RGB", (n, n))
    px = g.load()
    for y in range(n):
        v = y / (n - 1)
        left = [TL[i] + (BL[i] - TL[i]) * v for i in range(3)]
        right = [TR[i] + (BR[i] - TR[i]) * v for i in range(3)]
        for x in range(n):
            u = x / (n - 1)
            px[x, y] = tuple(int(round(left[i] + (right[i] - left[i]) * u)) for i in range(3))
    return g.resize((size, size), Image.BICUBIC)


def LensPoints(a, b, steps=720):
    """A lens/vesica outline: pointed at (+-a, 0), bulging to +-b."""
    upper, lower = [], []
    for i in range(steps + 1):
        t = -1.0 + 2.0 * i / steps
        y = b * math.pow(max(0.0, 1.0 - t * t), POWER)
        upper.append((t * a, y))
        lower.append((t * a, -y))
    return upper + lower[::-1]


def Rotate(points, angleDeg):
    r = math.radians(angleDeg)
    cos, sin = math.cos(r), math.sin(r)
    return [(x * cos - y * sin, x * sin + y * cos) for x, y in points]


def ToPixels(points, cx, cy, scale):
    return [(cx + x * scale, cy + y * scale) for x, y in points]


def MarkMask():
    """Union of the two lens rings, as a full-resolution alpha mask."""
    outer = LensPoints(OUTER_A, OUTER_B)
    inner = LensPoints(OUTER_A - THICKNESS, OUTER_B - THICKNESS)

    # Normalise so the widest point of the assembled mark sits at FIT * radius.
    reach = max(math.hypot(x, y) for a in ANGLES for x, y in Rotate(outer, a))
    scale = (W / 2.0) * FIT / reach
    cx = cy = W / 2.0

    mask = Image.new("L", (W, W), 0)
    for angle in ANGLES:
        ring = Image.new("L", (W, W), 0)
        d = ImageDraw.Draw(ring)
        d.polygon(ToPixels(Rotate(outer, angle), cx, cy, scale), fill=255)
        d.polygon(ToPixels(Rotate(inner, angle), cx, cy, scale), fill=0)
        mask = ImageChops.lighter(mask, ring)
    return mask.resize((SIZE, SIZE), Image.LANCZOS)


def Main(outPath):
    discMask = Image.new("L", (W, W), 0)
    ImageDraw.Draw(discMask).ellipse((0, 0, W - 1, W - 1), fill=255)
    discMask = discMask.resize((SIZE, SIZE), Image.LANCZOS)

    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    out.paste(Gradient(SIZE).convert("RGBA"), (0, 0), discMask)

    inside = Image.new("L", (SIZE, SIZE), 0)
    inside.paste(MarkMask(), (0, 0), discMask)     # clip the mark to the disc
    out.paste(Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 255)), (0, 0), inside)

    out.save(outPath)
    print("wrote", outPath, out.size)


if __name__ == "__main__":
    Main(sys.argv[1] if len(sys.argv) > 1 else "Ladybird.png")
