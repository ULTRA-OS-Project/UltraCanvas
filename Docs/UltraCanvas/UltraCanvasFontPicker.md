# UltraCanvasFontPicker and the installed fonts

## What it is

`UltraCanvasFontPicker` (`UltraCanvasFontPicker.h`) is a font list in the
WYSIWYG style of a word processor or a drawing program. Closed, it is a box
that shows the current family **in its own font**. Open, it is a list - wider
than the box, 300 px by default - of:

1. a **default font** row, when the host asks for one (`SetDefaultFontEntry`),
   which chooses `""`;
2. **In this document**: the families the document uses (the host names them,
   `SetDocumentFonts`). A family the computer lacks is marked: grey italics
   with *(not installed)*, or *(shown in Arial)* where an installed font stands
   in for it by name (`FontFamilySubstitute`);
3. **Recent**: the fonts chosen last, newest first (`SetRecentFonts`;
   `onRecentFontsChanged` tells the host to keep them in its settings);
4. a divider and **All fonts**: every installed family, A to Z.

Every family's name is drawn in that family at one size
(`FontPickerStyle::previewSize`, 14), with a small icon for the format of its
files at the left: **O** for OpenType, **TT** for TrueType, **a** for Type 1, a
pixel **A** for a bitmap font (`media/icons/fonts/`). A symbol or dingbat font
whose own glyphs cannot spell its name - Wingdings, Symbol, Webdings, an icon
font, an emoji font, a font of another script - is named in the interface's
font with a few of its own characters after the name.

A family with styles that **Bold** and **Italic** cannot reach - Light,
SemiBold, Black, Condensed - has an arrow. Resting the pointer on it (or
**Right**) opens its styles beside the list, each drawn in itself; choosing one
gives the family with that weight, slant and width. A family of Regular, Bold,
Italic and Bold Italic alone has no arrow: the B and I buttons beside the list
reach those four. `SetStylesSubmenu(FontPickerStylesSubmenu::EveryMultiStyleFamily)`
gives every family with more than one style the arrow, `::Off` none.

## Using it

<!-- doc-check: std::shared_ptr<UltraCanvasToolbar> bar; std::shared_ptr<UltraCanvasRichTextEdit> editor; std::vector<std::string> recentFromSettings; void SaveRecentFonts(const std::vector<std::string>& families); -->
```cpp
auto font = CreateFontPicker("FontList", 0, 0, 130, 26);
font->SetListWidth(300);                    // the open list; the box stays 130 px
font->SetDefaultFontEntry(Tr("Default font"));
font->SetRecentFonts(recentFromSettings);
font->SetAcceptsFocus(false);               // a toolbar's: the keyboard stays with the document
font->onListOpening = [editor, raw = font.get()]() {
    raw->SetDocumentFonts(editor->GetFontReport().namedFamilies);
};
font->onRecentFontsChanged = [](const std::vector<std::string>& families) { SaveRecentFonts(families); };
font->onFontChosen = [editor](const FontPickerChoice& choice) {
    RichCharFormatDelta delta;
    delta.setFontFamily = true;
    delta.fontFamily = choice.family;       // "" from the default row
    if (choice.fromStyles) {                // a style: bold and italic as it is
        delta.setBold = true;
        delta.bold = choice.weight >= 600;
        delta.setItalic = true;
        delta.italic = choice.slant != FontSlant::Normal;
    }
    editor->ApplyCharFormat(delta);
};
bar->AddWidget("FontList", font);
```

The host keeps the box in step with the text: `SetCurrentFont(family)` for one
family, `SetMixed()` for a selection in several (the box is blank).
`FontPickerChoice` carries what was chosen:

| Field | From a family's row | From the styles list |
|---|---|---|
| `family` | the family (`""` from the default row) | the family |
| `style` | `""` | the style's name, as the font names it ("SemiBold Italic") |
| `weight`, `slant`, `stretch` | 400, upright, 100 | the style's (CSS weight 100 to 1000, stretch in per cent) |
| `faceFamily`, `faceFamilyBold` | `""`, false | the face's own family name, where the text engine reaches the face by it, and whether with bold ("Ubuntu Thin"; "" where it cannot) |
| `fromStyles` | false | true |

A document that knows bold and italic but no weights - the rich text
document - keeps a Light or a Condensed face by `faceFamily`, the face's name
in the fonts' four-style naming that Windows and Word use ("Segoe UI Light",
"Arial Black", "DejaVu Sans Condensed"), with bold as `faceFamilyBold` says.
In that naming a family has a regular and a bold face, and the text engine
takes the face of that name nearest the weight asked for: Ubuntu's "Ubuntu
Light" holds Light and Medium, and fontconfig gives it without bold as Medium,
which is nearer the regular weight - so Light cannot be kept by that name and
`faceFamily` is empty for it (`InstalledFontStyle::faceFamilyReach`). Where the
face has no name the engine gives back (that case; a variable font's named
instances; every font on macOS, whose text engine takes no such names), the
nearest is bold or not. UltraOffice does exactly this (`ApplyFontChoice` in
`Apps/UltraOffice/UltraOfficeHost.cpp`). A name the document gives that is a
face's own name is found as its family's row (`FindInstalledFontFamilyOfName`),
and the rich text editor's font notice does not report it as missing.

### The keys

With the list open: **Up**, **Down**, **Page Up**, **Page Down**, **Home**,
**End** move over the rows (the captions are stepped over); **Return**
chooses; **Right** opens the highlighted family's styles and **Left** closes
them; **Escape** closes the styles, then the list. Letters typed go to the
first family of *All fonts* whose name starts with them - typed within a
second of each other they build one name ("ubuntu m"), the same letter again
steps on, **Backspace** takes the last one back. With `SetAcceptsFocus(false)`
(a toolbar's list, which leaves the keyboard with the document) the open list
still gets the keys typed.

### Speed

The rows are virtual (an `UltraCanvasListView` in a popup): only the rows on
screen are drawn, however many fonts there are. Each name's picture - its
layout in its own font - is made once and kept. Loading a font is the dear
part (a few milliseconds the first time), so the pictures are prepared ahead in
slices of about 6 ms every 15 ms on the UI thread's timer once the box is on
screen: first the rows the list opens on, then the rest, as
`UltraCanvasDropdown` prepares its font previews. A row not prepared when it
scrolls into view is prepared as it is drawn. This is the UI thread on
purpose: the text engine (Pango and its font map) is not thread-safe, and the
framework has no worker that may shape text.

## The installed fonts

`GetInstalledFontCatalogue()` (`UltraCanvasInstalledFonts.h`) is what the list
shows: every installed family, A to Z regardless of case, each with

- `styles` - every style the font has, named as the font names it, with its
  `weight` (CSS 100 to 1000), `slant` and `stretch` (per cent of the normal
  width), the `format` and `file`, and the face's own family name
  (`faceFamily`); narrow to wide, light to heavy, upright before italic;
- `format` (`FontFileFormat` of `UltraCanvasFontFile.h`: `OpenType`, `TrueType`, `Type1`, `BitmapFont`, `WOFF`), its
  regular style's; `scalable`, `monospace`, `variable`;
- `symbol` and `sample` for a font whose letters cannot spell its name.

<!-- doc-check: std::string family; -->
```cpp
auto fonts = GetInstalledFontCatalogue();     // kept until the fonts change
const InstalledFontStyle* face = nullptr;
if (const InstalledFontFamily* found = FindInstalledFontFamilyOfName(*fonts, family, &face)) {
    for (const InstalledFontStyle& style : found->styles) {
        (void)style;   // style.name, style.weight, style.slant, style.format
    }
}
bool knows = IsInstalledFontName("Segoe UI Semibold");   // a family, or a face's own name
(void)knows;
```

It is made once and kept until `GetFontSetGeneration()` moves - a font
registered with `RegisterFontFile`, the system fonts arriving after a cold start
on Windows - and the list follows: it compares the count when the windows are
laid out again after such a change. `GetInstalledFontFamilies()`
(`UltraCanvasApplication.h`) is the names of the same enumeration, with the
engine's generic names ("Sans", "Serif", "Monospace").

Left out, as other programs leave them out: Windows' vertical families (a name
starting with `@`), macOS' private system families (a name starting with `.`)
and LastResort (`IsHiddenFontFamilyName`), the generic names the engine adds
(their faces are all synthesized), a face the engine only synthesizes (an
italic slanted from the upright: not a style of the font), and a family with no
characters at all.

### Per platform

There is one enumeration, the text engine's own - Pango's default font map -
so every name in the list is one the renderer draws. What Pango's list lacks
(the format, the file, the face's own family name, the characters) is filled in
from the engine underneath it (`FillNativeFontDetails`):

| Platform | Families and styles | Formats, files, characters |
|---|---|---|
| Linux, the BSDs, Android, the browser | Pango over fontconfig | fontconfig's records (`FcFontList`: `FC_FONTFORMAT`, `FC_FILE`, `FC_CHARSET`, the second `FC_FAMILY` name), read from its cache - no font file is opened |
| Windows | Pango over fontconfig, the framework's text backend there: its fonts.conf names the system's font folder and the user's own (`%LOCALAPPDATA%\Microsoft\Windows\Fonts`), the two DirectWrite reads, so a font installed for one user is in the list | fontconfig's records, as above; the face's own family name is the one GDI and Word use ("Segoe UI Semibold") |
| macOS | Pango over CoreText | CoreText's font descriptors (`kCTFontFormatAttribute`, `kCTFontURLAttribute`, `kCTFontCharacterSetAttribute`, the symbolic class), `OS/MacOS/UltraCanvasMacOSInstalledFonts.mm`; no face family names |

The format comes from the outlines: CFF outlines (or an `.otf` / `.otc` file)
are OpenType, TrueType outlines in a `.ttf` / `.ttc` TrueType, Adobe Type 1
(`.pfb`, `.t1`, CID-keyed) Type 1, and `.pcf` / `.bdf` / `.fon` (fixed sizes)
bitmap. Where one style comes as two files - fonts-urw-base35 ships each face
as `.otf` and `.t1` - it is one style, and the better format is shown.

A family is a **symbol font** when its regular face cannot spell its name, has
nothing beyond Latin-1 but the symbol page (a symbol character map or a custom
encoding hands fontconfig its pictures as Latin-1 letters: Zapf Dingbats as
Type 1, Wingdings), is in the list of known ones (`IsKnownSymbolFontFamily`:
Wingdings, Webdings, Symbol, Marlett, the Segoe icon fonts ...), or - on macOS -
is in CoreText's symbolic class. Its sample (`ChooseFontSample`) is from the
symbol page (U+F041 on), else its pictographs, else its first letters beyond
Latin-1, else its first characters.

## Where it is used

UltraOffice's document *Home* font list, its presentation *Text* tab's font
list, the Settings window's font for new documents, and the font of the
framework's Styles dialog (`ShowStylesDialog`, `UltraCanvasRichTextEditDialogs.h`).

## Version history

- 1.0.0 - the font list, and the installed fonts' catalogue under it.
