# UltraCanvasMathFont

Reads an **OpenType math font** straight from its file: the `MATH` table
(constants, italics correction, top-accent attachment, extended shapes, math
kerning, size variants, glyph assemblies), the glyph metrics and the glyph
outlines. It is everything a TeX-style math typesetter needs from a font, in
font units, with no `.clm2` conversion step, no fontconfig, no Pango and no
render context.

Header: `include/Plugins/LaTeX/UltraCanvasMathFont.h`. Implementation:
`Plugins/LaTeX/UltraCanvasMathFont.cpp`, built into the on-demand
`libUltraCanvasLaTeX` module. Backed by FreeType, a hard dependency of the
framework. Test: `Tests/MathFontTest.cpp`.

This is Phase 0 of
[`UltraCanvasLaTeXEngineProposal.md`](UltraCanvasLaTeXEngineProposal.md): the
font layer of the native math engine that is to replace the vendored MicroTeX.
Today the shipped LaTeX view still typesets through MicroTeX and its `.clm2`
metrics; this reader is what makes any font with a `MATH` table — Latin
Modern Math, STIX, the TeX Gyre math family, Libertinus Math, Cambria Math —
usable at runtime without FontForge.

## Reading a font

```cpp
#include "Plugins/LaTeX/UltraCanvasMathFont.h"

UltraCanvas::UltraCanvasMathFont font;
if (!font.Load("/usr/share/fonts/opentype/stix-word/STIXMath-Regular.otf")) {
    std::cerr << font.GetLastError() << "\n";
    return;
}
if (!font.HasMathTable()) { /* a text font: metrics and outlines only */ }

const int upem = font.GetUnitsPerEm();                       // 1000 for Latin Modern
const int axis = font.GetConstant(MathConstant::AxisHeight); // font units
const float axisPx = font.GetConstantScaled(MathConstant::AxisHeight, 20.f); // at 20 px
```

`Load` returns false, with the reason in `GetLastError()`, for a missing or
unreadable file or one that is not a scalable font. A font **without** a
`MATH` table still loads — `HasMathTable()` is false and every `MATH` query
returns its documented "absent" value — so the same object can serve a text
face for `\text{…}`. A malformed `MATH` table is reported the same way
(`GetLastError()` says "malformed MATH table") and the rest of the font stays
usable; it never crashes on a bad file.

`LoadFromMemory(data, size)` opens a font image (the bytes are copied);
`Unload()` releases everything. Each object owns its own FreeType library, so
different objects can be used from different threads; one object must not be
used from two threads at once.

## Units

Every length is in **font units** with y pointing up, exactly as the font
file stores them; scale once by `fontSizePx / GetUnitsPerEm()`. The three
percentage constants (`ScriptPercentScaleDown`, `ScriptScriptPercentScaleDown`,
`RadicalDegreeBottomRaisePercent`) are percentages; `GetConstantScaled`
returns them as a factor (`0.7`) rather than a length.

## The MATH table

| Call | What it returns |
|---|---|
| `GetConstant(MathConstant)` | One of the 56 MathConstants, in table order (`MathConstant::AxisHeight`, `FractionRuleThickness`, `SuperscriptShiftUp`, …); `MathConstantName()` gives the spec's spelling |
| `GetItalicsCorrection(glyph)` / `HasItalicsCorrection` | Italics correction, 0 when the font defines none |
| `GetTopAccentAttachment(glyph)` / `HasTopAccentAttachment` | Where an accent is centred; when the font defines none, the specification's default of half the advance |
| `IsExtendedShape(glyph)` | Tall operators whose superscripts are placed by height |
| `GetMathKern(glyph, MathKernCorner)` / `GetMathKernValue(glyph, corner, height)` | The kern table for a corner (null when absent) and the value in effect at a height; `MathKernTable` holds n correction heights and n+1 values |
| `GetMinConnectorOverlap()` | Minimum overlap of assembly parts |
| `GetGlyphVariants(glyph, MathStretchDirection)` | Pre-drawn size variants, smallest first, each with its extent in the stretch direction |
| `GetGlyphAssembly(glyph, direction, out)` | The recipe for arbitrary sizes: parts with connector lengths, full advance and the extender flag, plus the assembly's italics correction |

Typical stretching code asks for the variants first and, when the largest
is still too small, builds the assembly:

```cpp
const uint32_t brace = font.GetGlyphIndex('{');
for (const MathGlyphVariant& v : font.GetGlyphVariants(brace, MathStretchDirection::Vertical)) {
    if (v.advance >= neededHeight) { use(v.glyph); break; }
}
MathGlyphAssembly assembly;
if (font.GetGlyphAssembly(brace, MathStretchDirection::Vertical, assembly)) {
    // assembly.parts: bottom, extender, middle, extender, top for a brace
}
```

## Glyphs, metrics and outlines

| Call | What it returns |
|---|---|
| `GetGlyphIndex(char32_t)` | Glyph index of a code point, 0 when the font lacks it |
| `GetGlyphName(glyph)` | The PostScript glyph name (`"parenleft"`) when the font carries names |
| `GetGlyphMetrics(glyph, out)` | Advance and the **exact** outline bounding box; `Height()` and `Depth()` are the TeX height above and depth below the baseline, never negative |
| `GetGlyphOutline(glyph, out)` | The outline as `MoveTo` / `LineTo` / `QuadTo` / `CubicTo` / `Close` segments in font units, cached per glyph; a blank glyph such as a space returns true with an empty outline, an out-of-range index returns false |

Feeding an outline to a render context is one loop:

```cpp
MathGlyphOutline outline;
font.GetGlyphOutline(glyph, outline);
const double s = fontSizePx / font.GetUnitsPerEm();
ctx->ClearPath();
for (const MathOutlineSegment& seg : outline.segments) {
    switch (seg.command) {
        case MathOutlineCommand::MoveTo:  ctx->MoveTo(x + seg.x1 * s, y - seg.y1 * s); break;
        case MathOutlineCommand::LineTo:  ctx->LineTo(x + seg.x1 * s, y - seg.y1 * s); break;
        case MathOutlineCommand::QuadTo:  ctx->QuadraticCurveTo(x + seg.x1 * s, y - seg.y1 * s,
                                                                x + seg.x2 * s, y - seg.y2 * s); break;
        case MathOutlineCommand::CubicTo: ctx->BezierCurveTo(x + seg.x1 * s, y - seg.y1 * s,
                                                             x + seg.x2 * s, y - seg.y2 * s,
                                                             x + seg.x3 * s, y - seg.y3 * s); break;
        case MathOutlineCommand::Close:   ctx->ClosePath(); break;
    }
}
ctx->Fill();
```

(`y` is the baseline in the context's y-down space, hence the sign flip.)

## Face facts

`GetFamilyName()`, `GetStyleName()`, `GetUnitsPerEm()`, `GetAscender()`,
`GetDescender()` (negative), `GetXHeight()` (OS/2 `sxHeight`, 0 when the
font has none), `GetGlyphCount()`, `GetPath()`.

## How it is verified

The bundled Latin Modern Math exists twice in the tree: as
`media/microtex/latinmodern-math.otf` and as the `.clm2` that FontForge
produced from that same file for MicroTeX. `Tests/MathFontTest.cpp` links the
vendored engine as an oracle and compares the two readers over **all 4,802
glyphs**: every constant, advance, height, depth, italics correction,
top-accent attachment, variant list and assembly must be equal — and is.
Math kerning, which none of the math fonts at hand carries, is checked on a
synthetic `MATH` table appended to a text font and loaded from memory; the
same path shows that a truncated table is refused cleanly. When STIX Math or
a TeX Gyre math font is installed, the test loads it too. During development
the reader was also compared with HarfBuzz's `hb_ot_math_*` API on seven
math fonts with identical results, extended-shape flags included.

## Limits

- **No shaping and no cmap fallback.** The reader maps one code point to one
  glyph and knows nothing about ligatures or fallback fonts; the engine
  above it owns the Unicode-math style mapping (`𝑥` for an italic `x`) and
  the fallback chain. `\text{…}` keeps going through the framework's Pango
  layout.
- **Device tables are ignored.** The `MATH` table can carry per-size hinting
  deltas; every vector math renderer ignores them and so does this one.
- **One face per object.** A `.ttc` collection is opened by face index;
  there is no enumeration here — use
  [`UltraCanvasFontFile`](UltraCanvasFontFile.md) for that.
