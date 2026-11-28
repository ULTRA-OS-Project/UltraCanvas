"""A QR code from a text: the modules, and a picture of them as SVG or PNG.

The portal draws its own QR codes - an affiliate's link to print or to show
in a video - so that a web page and the native store get the same picture
from the same address, and so that it needs no fourth dependency: the PNG is
written with Pillow, which the portal has for checking uploaded pictures.

The encoder is a port of the QR Code generator library by Project Nayuki
(MIT License, https://www.nayuki.io/page/qr-code-generator-library), the
library the framework bundles for its own QR plugin
(UltraCanvas/Plugins/QRCode/third_party, with its licence). It is the
library's byte mode alone - a link is bytes - in every version (1 to 40)
and all four error correction levels, with the mask chosen by the
standard's penalty rules.

    modules = qr.encode("https://store.example/r/anna/")   # rows of booleans, True is dark
    qr.svg(modules)                                        # text
    qr.png(modules, scale=8)                               # bytes

Version: 0.12.0
"""

from __future__ import annotations

import io

LEVELS = {"L": 0, "M": 1, "Q": 2, "H": 3}
_FORMAT_BITS = (1, 0, 3, 2)   # per level, as the format information spells them
MIN_VERSION, MAX_VERSION = 1, 40
QUIET_ZONE = 4                # the light border the standard asks for, in modules

_ECC_CODEWORDS_PER_BLOCK = (
    (-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30),
    (-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28),
    (-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30),
    (-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30),
)
_NUM_ERROR_CORRECTION_BLOCKS = (
    (-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25),
    (-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49),
    (-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68),
    (-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81),
)
_PENALTY_N1, _PENALTY_N2, _PENALTY_N3, _PENALTY_N4 = 3, 3, 40, 10


class TooLong(ValueError):
    """The text does not fit a QR code at this error correction level."""


# ----------------------------------------------------------------- capacity

def _raw_data_modules(version: int) -> int:
    result = (16 * version + 128) * version + 64
    if version >= 2:
        aligns = version // 7 + 2
        result -= (25 * aligns - 10) * aligns - 55
        if version >= 7:
            result -= 36
    return result


def _data_codewords(version: int, level: int) -> int:
    return _raw_data_modules(version) // 8 - _ECC_CODEWORDS_PER_BLOCK[level][version] * _NUM_ERROR_CORRECTION_BLOCKS[level][version]


def capacity(level: str = "M") -> int:
    """The most bytes a code can hold at this level (version 40)."""
    return (_data_codewords(MAX_VERSION, LEVELS[level]) * 8 - 4 - 16) // 8


# ------------------------------------------------------------- Reed-Solomon

def _multiply(x: int, y: int) -> int:
    z = 0
    for i in range(7, -1, -1):
        z = (z << 1) ^ ((z >> 7) * 0x11D)
        z ^= ((y >> i) & 1) * x
    return z


def _divisor(degree: int) -> list[int]:
    result = [0] * (degree - 1) + [1]
    root = 1
    for _ in range(degree):
        for j in range(degree):
            result[j] = _multiply(result[j], root)
            if j + 1 < degree:
                result[j] ^= result[j + 1]
        root = _multiply(root, 0x02)
    return result


def _remainder(data: list[int], divisor: list[int]) -> list[int]:
    result = [0] * len(divisor)
    for byte in data:
        factor = byte ^ result.pop(0)
        result.append(0)
        for i, coefficient in enumerate(divisor):
            result[i] ^= _multiply(coefficient, factor)
    return result


def _with_ecc(data: list[int], version: int, level: int) -> list[int]:
    """The data codewords in their blocks, each with its error correction,
    interleaved as they are laid into the code."""
    blocks_count = _NUM_ERROR_CORRECTION_BLOCKS[level][version]
    ecc_length = _ECC_CODEWORDS_PER_BLOCK[level][version]
    raw = _raw_data_modules(version) // 8
    short_blocks = blocks_count - raw % blocks_count
    short_length = raw // blocks_count
    divisor = _divisor(ecc_length)
    blocks, at = [], 0
    for i in range(blocks_count):
        block = data[at:at + short_length - ecc_length + (0 if i < short_blocks else 1)]
        at += len(block)
        ecc = _remainder(block, divisor)
        if i < short_blocks:
            block = block + [0]
        blocks.append(block + ecc)
    result = []
    for i in range(len(blocks[0])):
        for j, block in enumerate(blocks):
            if i != short_length - ecc_length or j >= short_blocks:   # the padding of a short block is not part of the code
                result.append(block[i])
    return result


# ------------------------------------------------------------------ the code

class _Code:
    def __init__(self, version: int, level: int):
        self.version, self.level = version, level
        self.size = version * 4 + 17
        self.modules = [[False] * self.size for _ in range(self.size)]
        self.function = [[False] * self.size for _ in range(self.size)]
        for i in range(self.size):   # the timing patterns
            self._set(6, i, i % 2 == 0)
            self._set(i, 6, i % 2 == 0)
        for x, y in ((3, 3), (self.size - 4, 3), (3, self.size - 4)):
            self._finder(x, y)
        positions = self._alignment_positions()
        last = len(positions) - 1
        for i, x in enumerate(positions):
            for j, y in enumerate(positions):
                if (i, j) not in ((0, 0), (0, last), (last, 0)):   # where the finders are
                    for dy in range(-2, 3):
                        for dx in range(-2, 3):
                            self._set(x + dx, y + dy, max(abs(dx), abs(dy)) != 1)
        self.format_bits(0)   # reserves the modules; the mask's own are drawn later
        self._version_bits()

    def _set(self, x: int, y: int, dark: bool) -> None:
        self.modules[y][x] = dark
        self.function[y][x] = True

    def _finder(self, x: int, y: int) -> None:
        for dy in range(-4, 5):
            for dx in range(-4, 5):
                xx, yy = x + dx, y + dy
                if 0 <= xx < self.size and 0 <= yy < self.size:
                    self._set(xx, yy, max(abs(dx), abs(dy)) not in (2, 4))

    def _alignment_positions(self) -> list[int]:
        if self.version == 1:
            return []
        count = self.version // 7 + 2
        step = (self.version * 8 + count * 3 + 5) // (count * 4 - 4) * 2
        return [6] + [self.size - 7 - i * step for i in range(count - 2, -1, -1)]

    def format_bits(self, mask: int) -> None:
        data = _FORMAT_BITS[self.level] << 3 | mask
        rem = data
        for _ in range(10):
            rem = (rem << 1) ^ ((rem >> 9) * 0x537)
        bits = (data << 10 | rem) ^ 0x5412
        def bit(i: int) -> bool:
            return (bits >> i) & 1 != 0
        for i in range(6):
            self._set(8, i, bit(i))
        self._set(8, 7, bit(6))
        self._set(8, 8, bit(7))
        self._set(7, 8, bit(8))
        for i in range(9, 15):
            self._set(14 - i, 8, bit(i))
        for i in range(8):
            self._set(self.size - 1 - i, 8, bit(i))
        for i in range(8, 15):
            self._set(8, self.size - 15 + i, bit(i))
        self._set(8, self.size - 8, True)   # always dark

    def _version_bits(self) -> None:
        if self.version < 7:
            return
        rem = self.version
        for _ in range(12):
            rem = (rem << 1) ^ ((rem >> 11) * 0x1F25)
        bits = self.version << 12 | rem
        for i in range(18):
            dark = (bits >> i) & 1 != 0
            a, b = self.size - 11 + i % 3, i // 3
            self._set(a, b, dark)
            self._set(b, a, dark)

    def codewords(self, data: list[int]) -> None:
        """The codewords in the zigzag from the bottom right corner."""
        i, total = 0, len(data) * 8
        right = self.size - 1
        while right >= 1:
            if right == 6:
                right = 5
            for vert in range(self.size):
                for j in range(2):
                    x = right - j
                    upward = (right + 1) & 2 == 0
                    y = self.size - 1 - vert if upward else vert
                    if not self.function[y][x] and i < total:
                        self.modules[y][x] = (data[i >> 3] >> (7 - (i & 7))) & 1 != 0
                        i += 1
            right -= 2

    def mask(self, mask: int) -> None:
        """Applied twice, a mask is undone."""
        tests = (lambda x, y: (x + y) % 2 == 0, lambda x, y: y % 2 == 0, lambda x, y: x % 3 == 0, lambda x, y: (x + y) % 3 == 0,
                 lambda x, y: (x // 3 + y // 2) % 2 == 0, lambda x, y: x * y % 2 + x * y % 3 == 0,
                 lambda x, y: (x * y % 2 + x * y % 3) % 2 == 0, lambda x, y: ((x + y) % 2 + x * y % 3) % 2 == 0)
        invert = tests[mask]
        for y in range(self.size):
            row, function = self.modules[y], self.function[y]
            for x in range(self.size):
                if not function[x] and invert(x, y):
                    row[x] = not row[x]

    def penalty(self) -> int:
        size, modules = self.size, self.modules
        result = 0

        def add_history(run: int, history: list[int]) -> None:
            if history[0] == 0:
                run += size   # a light border before the first run
            history.insert(0, run)
            history.pop()

        def count_patterns(history: list[int]) -> int:
            n = history[1]
            core = n > 0 and history[2] == n and history[3] == n * 3 and history[4] == n and history[5] == n
            return (1 if core and history[0] >= n * 4 and history[6] >= n else 0) + (1 if core and history[6] >= n * 4 and history[0] >= n else 0)

        def line(cells: list[bool]) -> int:
            score, colour, run, history = 0, False, 0, [0] * 7
            for cell in cells:
                if cell == colour:
                    run += 1
                    if run == 5:
                        score += _PENALTY_N1
                    elif run > 5:
                        score += 1
                else:
                    add_history(run, history)
                    if not colour:
                        score += count_patterns(history) * _PENALTY_N3
                    colour, run = cell, 1
            if colour:
                add_history(run, history)
                run = 0
            add_history(run + size, history)
            return score + count_patterns(history) * _PENALTY_N3

        for y in range(size):
            result += line(modules[y])
        for x in range(size):
            result += line([modules[y][x] for y in range(size)])
        for y in range(size - 1):
            for x in range(size - 1):
                if modules[y][x] == modules[y][x + 1] == modules[y + 1][x] == modules[y + 1][x + 1]:
                    result += _PENALTY_N2
        dark = sum(sum(row) for row in modules)
        total = size * size
        result += ((abs(dark * 20 - total * 10) + total - 1) // total - 1) * _PENALTY_N4
        return result


def encode(text: str | bytes, level: str = "M", *, mask: int | None = None) -> list[list[bool]]:
    """The QR code of a text, as rows of modules from the top: True is
    dark. The smallest version that holds it at this error correction level
    (L, M, Q or H); `mask` 0 to 7 fixes the mask, which is otherwise the one
    the standard's penalty rules like best. TooLong when nothing holds it."""
    if level not in LEVELS:
        raise ValueError("The error correction level is L, M, Q or H")
    if mask is not None and not 0 <= mask <= 7:
        raise ValueError("A mask is 0 to 7")
    data = text.encode("utf-8") if isinstance(text, str) else bytes(text)
    ecl = LEVELS[level]
    for version in range(MIN_VERSION, MAX_VERSION + 1):
        count_bits = 8 if version < 10 else 16
        room = _data_codewords(version, ecl) * 8
        if len(data) < (1 << count_bits) and 4 + count_bits + len(data) * 8 <= room:
            break
    else:
        raise TooLong(f"{len(data)} bytes do not fit a QR code at level {level}: {capacity(level)} at most")
    bits: list[int] = []
    def append(value: int, length: int) -> None:
        bits.extend((value >> i) & 1 for i in range(length - 1, -1, -1))
    append(0x4, 4)                  # byte mode
    append(len(data), count_bits)
    for byte in data:
        append(byte, 8)
    append(0, min(4, room - len(bits)))   # the terminator
    append(0, -len(bits) % 8)
    pad = 0xEC
    while len(bits) < room:
        append(pad, 8)
        pad ^= 0xEC ^ 0x11
    codewords = [0] * (len(bits) // 8)
    for i, bit in enumerate(bits):
        codewords[i >> 3] |= bit << (7 - (i & 7))
    code = _Code(version, ecl)
    code.codewords(_with_ecc(codewords, version, ecl))
    if mask is None:
        best = None
        for candidate in range(8):
            code.mask(candidate)
            code.format_bits(candidate)
            score = code.penalty()
            if best is None or score < best:
                mask, best = candidate, score
            code.mask(candidate)
    code.mask(mask)
    code.format_bits(mask)
    return code.modules


# -------------------------------------------------------------- the pictures

def _colour(value: str, fallback: str) -> str:
    """A colour as #rrggbb, or the fallback: nothing else reaches a picture."""
    value = (value or "").strip().lstrip("#").lower()
    if len(value) == 3:
        value = "".join(c * 2 for c in value)
    return "#" + value if len(value) == 6 and all(c in "0123456789abcdef" for c in value) else fallback


def svg(modules: list[list[bool]], *, border: int = QUIET_ZONE, dark: str = "#000000", light: str = "#ffffff", size: int | None = None,
        title: str = "") -> str:
    """The code as an SVG picture: sharp at any size. `size` is its width
    and height in pixels (without it the picture scales to where it is put);
    `title` is read out in place of the picture."""
    dark, light = _colour(dark, "#000000"), _colour(light, "#ffffff")
    side = len(modules) + border * 2
    parts = []
    for y, row in enumerate(modules):
        x = 0
        while x < len(row):
            if row[x]:
                start = x
                while x < len(row) and row[x]:
                    x += 1
                parts.append(f"M{start + border},{y + border}h{x - start}v1h-{x - start}z")
            else:
                x += 1
    box = f' width="{size}" height="{size}"' if size else ""
    label = title.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")
    described = f' role="img" aria-label="{label}"><title>{label}</title>' if label else ">"
    return (f'<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {side} {side}"{box} '
            f'shape-rendering="crispEdges"{described}<rect width="{side}" height="{side}" fill="{light}"/>'
            f'<path d="{"".join(parts)}" fill="{dark}"/></svg>\n')


def png(modules: list[list[bool]], *, scale: int = 8, border: int = QUIET_ZONE, dark: str = "#000000", light: str = "#ffffff") -> bytes:
    """The code as a PNG picture, each module `scale` pixels wide."""
    from PIL import Image

    dark, light = _colour(dark, "#000000"), _colour(light, "#ffffff")
    def rgb(colour: str) -> tuple[int, int, int]:
        return int(colour[1:3], 16), int(colour[3:5], 16), int(colour[5:7], 16)
    side = len(modules) + border * 2
    small = Image.new("P", (side, side), 0)
    small.putpalette([*rgb(light), *rgb(dark)])
    pixels = small.load()
    for y, row in enumerate(modules):
        for x, cell in enumerate(row):
            if cell:
                pixels[x + border, y + border] = 1
    scale = max(1, min(int(scale), 64))
    out = io.BytesIO()
    small.resize((side * scale, side * scale), Image.NEAREST).save(out, format="PNG", optimize=True)
    return out.getvalue()
