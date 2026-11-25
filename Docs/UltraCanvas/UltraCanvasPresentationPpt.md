# UltraCanvasPresentationPpt — PowerPoint 97-2003 decks (.ppt), read

`UltraCanvasPresentationPpt.h` reads the binary PowerPoint format of
PowerPoint 97 to 2003 - a `.ppt`, and the same format saved as a show (`.pps`)
or a template (`.pot`) - into the deck model every presentation reader fills
([UltraCanvasPresentationPptx](UltraCanvasPresentationPptx.md)), the one the
`.pptx` and the [`.odp`](UltraCanvasPresentationOdp.md) readers fill. So a
`.ppt` is drawn by `RenderPptxSlide`, shown by the page views
([UltraCanvasPresentationPages](UltraCanvasPresentationPages.md)), played by
the show ([UltraCanvasSlideShowPlayer](UltraCanvasSlideShowPlayer.md)),
exported to PDF, edited in the slide editor
([UltraCanvasSlideEdit](UltraCanvasSlideEdit.md)) and read as an outline, as
the other two are - and "Save as" writes it as `.pptx` or `.odp`. There is no
`.ppt` writer.

The format is [MS-PPT]: a compound file
([UltraCanvasCompoundFile](UltraCanvasCompoundFile.md)) whose "PowerPoint
Document" stream is a tree of records, its drawings in the OfficeArt format
of [MS-ODRAW] - the one a `.doc` keeps its shapes in. The reader shares the
`.doc` reader's OfficeArt layer (`Plugins/Documents/Word/UltraCanvasOfficeArt.h`:
shape properties, the picture store, PNG / JPEG / EMF / WMF pictures,
freeforms, the shape types as DrawingML presets).

## Opening one

Through `UltraCanvasFileLoader`, as every file a user opens:
`LoadPresentation` reads any deck into the model, the reader chosen by the
file's content - a compound file with a "PowerPoint Document" stream is a
`.ppt` whatever its name:

<!-- doc-check: std::string path; void ShowError(const std::string& message); -->
```cpp
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPptxSlideRender.h"

using namespace UltraCanvas;

PptxPresentation deck;
std::string error;
// The last argument keeps the pictures' bytes, to draw the slides and to
// save them again as .pptx or .odp.
if (!UltraCanvasFileLoader::LoadPresentation(path, deck, error, "", true)) {
    ShowError(error);   // "The presentation is protected with a password ..."
} else {
    auto firstSlide = RenderPptxSlide(deck, 0, 1280);
    (void)firstSlide;
}
```

`LoadTextDocument` opens one as the outline of its slides (the PowerPoint
reader's outline: per slide its number and title, its bullets, tables,
pictures and notes), `ExtractText` reads its words, and
`DetectOfficeDocumentKind` calls it a presentation - by its extension, or by
its content when the name says nothing. The format inventory
(`GetSupportedFormats`) lists `ppt` with `pps` and `pot` as read, not
written, so `AddOfficeDocumentLoadTypes` offers it among the presentations
and `AddPresentationSaveTypes` does not.

The reader itself, for a file or for bytes already in memory:

<!-- doc-check: std::string path; std::vector<uint8_t> bytes; void ShowError(const std::string& message); -->
```cpp
#include "UltraCanvasPresentationPpt.h"

using namespace UltraCanvas;

PptxPresentation deck;
std::string error;
if (IsPptExtension(path) && !IsPptPresentationFile(path)) {
    ShowError("named .ppt, but not a PowerPoint 97-2003 file");
} else if (!ReadPptPresentation(path, deck, error)) {
    ShowError(error);
} else {
    for (const PptxSlide& slide : deck.slides) {
        (void)slide.title;
        (void)slide.hidden;
        for (const PptxParagraph& note : slide.notes) (void)note.Text();
    }
}
PptxPresentation fromMemory;
ReadPptPresentationFromMemory(bytes, fromMemory, error, true);
```

## What is read

| Part | Read into the model |
|---|---|
| The edits | The "Current User" stream's offset to the newest `UserEditAtom`, then each older one: their persist directories, newest first, so a deck saved incrementally ("fast save") reads as its last edit. Without a usable Current User stream, the last `UserEditAtom` of the stream. |
| The document | The slide size (`DocumentAtom`, 576 master units to the inch, kept in EMU), the font collection, the document's text defaults, the picture store (BStore, its pictures embedded or in the "Pictures" stream), the hyperlinks (`ExObjList`), the master, slide and notes lists (`SlideListWithText`) with the outline text PowerPoint keeps there. |
| Masters | Each main master as a `PptxMaster`: its placeholders (title, body, date, footer, slide number) with their prompt text and look, its background and colour scheme (`deck.themeColors`, the title and body typefaces as `majorFont` / `minorFont`), and its text styles (`TxMasterStyleAtom`: title, body by level, notes, other, and the centred / half / quarter body variants) - what a run that says nothing inherits. A title master is the master's *Title Slide* layout; the other layouts are made from the slide layouts the slides use (`SlideAtom` geom: Title and Content, Title Only, Two Content, Blank ...), each slide on its `layoutIndex`. |
| Shapes | Place (master units in EMU; a group's members mapped from its coordinate space; a shape turned by 45 to 135 degrees stored with its box turned), rotation and mirroring, the shape type as a DrawingML preset (about 150 of them) or a freeform's outline, fill (solid; a shade as a two- or three-stop gradient), outline (colour, width, dashes, arrowheads), text insets, anchor, wrap and vertical text. |
| Placeholders | `OEPlaceholderAtom` as the model's kinds: title and centred title as `Title`, subtitle as `Subtitle`, body, object and the other content kinds as `Body`; one with no place of its own takes its master's (`placeInherited`). An empty one keeps one empty paragraph in its look, for an editor to type into. Date, footer and slide number are kept on masters, left out on slides, as the other readers do. |
| Text | `TextCharsAtom` / `TextBytesAtom`, or the outline text an `OutlineTextRefAtom` points at; `StyleTextPropAtom`'s paragraph runs (level, alignment, line and paragraph spacing, margin and indent - else the `TextRulerAtom`'s -, the bullet with its character, typeface, colour and size) and character runs (bold, italic, underline, size, colour - a scheme index resolved through the slide's colour scheme -, typeface, super- and subscript); numbering from PowerPoint 2000's `StyleTextProp9Atom`; hyperlinks (`TextInteractiveInfoAtom` to its `ExHyperlink`: a URL, or `#slideN` for a jump to a slide). |
| Tables | A group marked as a table (`tableProperties`): its cells on the grid their edges make, with their text, fills, insets and anchors, merged cells from the cells that span the grid, borders from the lines along its edges. A group that cannot be laid on a grid stays its shapes. |
| Pictures | PNG, JPEG, EMF and WMF from the store, by `pib`, as `ppt/media/imageN.<ext>` (`PptxShape::pictureTarget`; the bytes in `deck.pictures` when read `withPictures`), with their crop and description. |
| Slides | Notes (the notes page's body text), hidden, the background (the slide's own, else its master's), the scheme's text colour, and the transition (`SSSlideInfoAtom`: fade, push, wipe, split, cover, zoom, anything else as a fade; the speed as its length; the automatic advance). |

What is not read: animations other than the transition, sounds, embedded
OLE objects and charts (the picture the file keeps of one is drawn),
comments, picture and pattern fills (their colour is kept), shadows and the
other effects, WordArt, the handout, a master's shapes other than its
placeholders, and VBA. A password-protected deck (its "PowerPoint Document"
encrypted with RC4 CryptoAPI) is refused with a reason - save it without the
password, or as `.pptx`, in PowerPoint or LibreOffice.

## A damaged file

Every length in a `.ppt` is someone else's: each record is checked against
the stream and the container it sits in, offsets from the persist directory
against the stream, run counts against the text, nesting and loops are
capped, and the chain of edits cannot go round in a circle. A damaged file
gives an error, or what could be read - never a crash or a hang.
`Tests/PresentationPptTest.cpp` reads every truncation of a deck, and
truncated and scrambled copies of the fixtures; `PptReaderFuzzSmoke`
(`Tests/Fuzz/PptReaderFuzz.cpp`) mutates the seed streams in `corpus/ppt` on
every build.

## Applications

UltraOffice opens a `.ppt` in its slide editor; saving it asks for a new
name, as for a `.doc` or an `.xls`, and writes `.pptx` or `.odp`.
UltraViewer shows its slides (and its outline with O) and plays its show;
UltraFiler previews its first slide. Their desktop entries list
`application/vnd.ms-powerpoint`.

## Fixtures

`Tests/fixtures/ppt-features.ppt` and `office-pptx-sample.ppt` are written
by LibreOffice ("MS PowerPoint 97") from decks python-pptx builds -
`Tests/fixtures/make-ppt-fixtures.py` rebuilds them, and its head says what
each slide holds. The test also builds a deck record by record, as
PowerPoint writes one: text in the slide list, looks from the master, two
edits, an encrypted copy.

Specification: [MS-PPT] PowerPoint (.ppt) Binary File Format; [MS-ODRAW]
Office Drawing Binary File Format; [MS-CFB] Compound File Binary File Format.
