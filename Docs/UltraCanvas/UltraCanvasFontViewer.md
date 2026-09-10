# UltraCanvasFontViewer

Browse every glyph in a font file: a scrolling grid of the font's own glyphs,
a picker for the ranges it covers, and a size control.

Header: `include/UltraCanvasFontViewer.h`. Implementation:
`core/UltraCanvasFontViewer.cpp`. Built on
[`UltraCanvasFontFace`](UltraCanvasFontFile.md), so **the font does not have to
be installed** — a folder of candidates can be browsed before any of them is
registered.

## What it is for

The thumbnail in [`UltraCanvasFontFile`](UltraCanvasFontFile.md) answers *what
does this font look like*. This answers *what is actually in it* — which
characters a font covers is the question you have when choosing between two
downloads, and a single line of sample text cannot answer it.

`UltraCanvasMediaViewer` embeds it as the display for `MediaKind::Font`, which
is what puts a font in a file manager's detail pane. It is equally usable on
its own: a font manager, or a font picker that wants to show coverage rather
than just a family name.

## Using it

```cpp
auto viewer = UltraCanvas::CreateFontViewer("fonts", 0, 0, 640, 480);
container->AddChild(viewer);

if (viewer->LoadFont("/home/me/Downloads/Inter.ttf")) {
    viewer->SetCellSize(64);
    viewer->ScrollToCodepoint(U'A');
}

viewer->onGlyphSelected = [viewer](size_t entry) {
    if (entry == UltraCanvas::UltraCanvasFontViewer::NoEntry) return;
    const auto& g = viewer->GetFace().Glyphs()[entry];
    std::cout << "U+" << std::hex << g.codepoint
              << "  " << viewer->GetFace().GlyphName(entry) << "\n";
};
```

| | |
|---|---|
| `LoadFont(path, faceIndex = 0)` | Opens one face and shows its coverage. False when the file cannot be read; the previous font is closed either way. |
| `CloseFont()` / `IsFontLoaded()` / `GetFontPath()` | |
| `GetFace()` | The open `UltraCanvasFontFace` — its metadata, coverage and ranges, without re-reading the file. |
| `GetFaceCount()` / `GetFaceIndex()` / `SetFaceIndex()` | The faces of a collection (`.ttc`). The face picker appears only when there is more than one. |
| `SetCellSize()` / `GetCellSize()` | Cell edge in logical pixels, clamped to a legible range. |
| `ShowRange(i)` / `ScrollToCodepoint(cp)` / `ScrollToEntry(e)` | Jump. All no-ops for something the font does not have. |
| `GetSelectedEntry()` / `SetSelectedEntry()` | Selecting scrolls the glyph into view. |
| `onGlyphSelected` / `onGlyphActivated` | Selection, and double-click — a host's cue to insert or copy the character. |
| `SetControlsVisible()` | Drop the control bar and information line for a host with its own chrome. |
| `SetStyle()` / `GetStyle()` | `FontViewerStyle`: colours, cell spacing, caption height, padding. |

## What it shows

The grid draws one cell per glyph, each with the glyph and its codepoint
underneath (`U+0041`), or its glyph index for a face with no usable character
map — the information line says which. Hovering describes the glyph under the
pointer; selecting keeps it described.

Cells share a size and a baseline, because `UltraCanvasFontFace::RenderGlyph()`
scales the *face's bounding box* into the cell rather than the individual
glyph. That is what makes a row read as text rather than as unrelated
pictures: an `A` sits above the baseline and a `g` hangs below it.

The **range picker** lists the runs of coverage the font actually has, named
after their Unicode blocks. That is what makes a 20 000-glyph CJK font
navigable — scrolling to Hiragana is one choice rather than a long drag. It
also follows the grid: a wheel out of Basic Latin and into Latin Extended-A
moves it, so it always names what you are looking at rather than the last
thing that was picked.

The scrollbar appears only when there is something to scroll. A font that fits
in the view, and the empty state before a font is loaded, show none.

## Controls are real elements

The grid is a **self-rendered view** in the sense the house rules allow: the
cells are *content*, painted the way the filer paints its tiles and the album
its thumbnails. Everything that takes input is a real element — the range and
face pickers are `UltraCanvasDropdown`, the size control an
`UltraCanvasSlider`, the information line an `UltraCanvasLabel`, and the bar on
the right an `UltraCanvasScrollbar`. Scrolling eases through
`UltraCanvasSmoothScroll`, the same animator the filer and album use, so a
wheel notch feels identical everywhere.

Because they are real elements they go through the layout engine, and the
viewer places them **out of flow** — `SetElementSize()` plus
`SetElementAbsolutePosition()`, not `SetBounds()` alone. `SetBounds()` writes
`finalBounds`, which the parent's next `Arrange()` pass overwrites: an in-flow
child is re-stacked at the top-left however it was placed, which puts the whole
control bar in one column on top of the grid. Any container that positions its
own chrome has the same obligation.

The placement is re-run from **both** `SetBounds()` and `Arrange()`. A viewer
positioned by hand is sized through the first; one inside a flex or grid
parent — the media viewer's column, say — only ever through the second, and it
is built at no size and given one later. Re-flowing from `SetBounds()` alone
leaves such a viewer with the chrome placement it had at a size of zero, which
is none at all.

## Performance

Glyphs are rasterized one cell at a time and cached by (entry, device-pixel
cell edge). Only the rows on screen are ever drawn, so a face with tens of
thousands of glyphs costs a screenful — a hundred or so — not the font. Changing
the size drops the cache rather than showing stale sizes, and the cache is
bounded: when it fills it is dropped whole, because tracking which cells
scrolled away costs more than re-rasterizing a screenful.

Keyboard: arrows move the selection by one cell, Page Up / Page Down by a
screen, Home / End to the ends of the font.

## Geometry, and why it is public

`GetColumnCount()`, `GetRowCount()`, `GetMaxScroll()`, `GetScrollOffset()`,
`EntryAtPoint()` and `GetCellRect()` are part of the API. A host anchoring a
popup over a glyph needs `GetCellRect()`; the rest exist because a wrong column
count or a scroll range that stops short of the last row hides part of a font
with nothing on screen to say so, and `Tests/FontViewerTest.cpp` pins them.
